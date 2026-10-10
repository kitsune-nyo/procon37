#include <unordered_map>
#include <algorithm>
#include <map>

#include "map.cpp"
#include "spot.cpp"
#include "agent.cpp"

#define INACTION INT_MIN
#define WAIT -1

const double fuelPerMax = 0.3;
const std::vector<double> evaluateWeight = {10000, 10000, 10000, 1, 100};

// 巡回車1台あたり、スポットへ向かう候補をいくつまで作るか（系列ごとの最寄りは別枠で必ず入れる）
constexpr int PATROL_SPOT_CANDIDATES = 4;

// 先読みボーナスの重み（実際に取った分の重み 10000 より小さくして、取得済みを常に優先させる）
constexpr double PENDING_BRAND_WEIGHT  = 5000;
constexpr double PENDING_UDON_WEIGHT   = 3000;
constexpr double PENDING_REFUEL_WEIGHT = 1;

// 日の終わりにスポット上にいる巡回車は、翌日のステップ1で1玉取れる（A7・A17）。その見込みの価値。
// 今日取る1玉（10000）より小さく、今日取れる見込み（PENDING_UDON_WEIGHT）とは同程度にする。
constexpr double PARK_WEIGHT = 3000;

// ゴール地点 → 逆ダイクストラ結果。
// ゴールは「スポット」と「日の開始時点でスポット外にいる巡回車の位置（救出用）」だけ。
// 探索開始前にすべて計算し、探索中に追加の計算はしない。
using MapDijkstra = std::unordered_map<int, ReverseDijkstraResult>;

const ReverseDijkstraResult* findRoute(const MapDijkstra& routes, int goal) {
    auto it = routes.find(goal);
    return it == routes.end() ? nullptr : &it->second;
}

bool reachable(const ReverseDijkstraResult* route, int start) {
    return route && start >= 0 && start < (int)route->dist.size() &&
        route->parent.size() == route->dist.size() && route->dist[start].time < BIG;
}

// スポット + extraGoals を始点に逆ダイクストラを計算する。
// 件数は (スポット数 + 巡回車数) 以下で、1回あたり数十マイクロ秒なので締め切りは見ない。
void precomputeRoutes(MapDijkstra& routes, Map& map, const SpotManager& spots,
                      const std::vector<int>& extraGoals = {}) {
    std::vector<int> goals;
    for (const Spot& spot : spots.spots) goals.push_back(spot.pos);
    goals.insert(goals.end(), extraGoals.begin(), extraGoals.end());
    routes.reserve(goals.size());
    for (int goal : goals) {
        if (goal < 0 || goal >= (int)map.cells.size() || map.terrainAt(goal) == Terrain::POND) continue;
        if (routes.count(goal)) continue;
        routes.emplace(goal, map.reverseDijkstra(goal));
    }
}

class State {
public:
    SpotManager spotMgr;
    AgentManager agentMgr;

    int step = 0;
    int daySteps = 0; // その日の総ステップ数（beamSearch / rollout の開始時に設定）
    double score = 0;
    int udonSum = 0;
    std::set<int> udonBrand;

    bool operator>(const State& r) const {
        if (step != r.step) return step > r.step;
        return score > r.score;
    }

    State(SpotManager s, AgentManager a) {
        spotMgr = s;
        agentMgr = a;
    }

    void applySupply() {
        for (Agent& s: agentMgr.agents) {
            if (s.kind == AgentKind::PATROL) continue;
            for (Agent& p: agentMgr.agents) {
                if (p.kind == AgentKind::SUPPLY) continue;
                if (s.pos == p.pos) p.fuel = agentMgr.fuel;
            }
        }
    }

    void collectUdon() {
        for (Agent& agent: agentMgr.agents) {
            if (agent.kind == AgentKind::SUPPLY) continue;
            Spot* s = spotMgr.findAt(agent.pos);
            if (s == nullptr) continue;
            bool canCollect = (agent.visitedSpotPos.count(s->pos) == 0);
            if (canCollect) {
                if (spotMgr.consume(s->pos)) {
                    udonSum++;
                    udonBrand.insert(s->brand);
                    agent.visitedSpotPos.insert(s->pos);
                }
            }
        }
    }
};

// ------------------------------------------------------------------------
// 先読み: エージェントが今の行動キューを最後まで実行したとき、
// どこに・何ステップ目に・燃料いくつで着くか。
// ------------------------------------------------------------------------
struct Forecast { int dest, eta, fuel; };

Forecast forecast(const Agent& a, Map& map, int step) {
    int pos = a.pos, t = step, fuel = a.fuel;
    if (a.nextPos != -1) {
        pos = a.nextPos;
        t = a.moveEndStep;
        if (a.kind == AgentKind::PATROL) fuel -= a.nextFuel;
    }
    for (int x : a.actions) {
        if (x == WAIT) { t++; continue; }
        MoveCost c = map.getMoveCost(pos);
        t += c.time;
        if (a.kind == AgentKind::PATROL) fuel -= c.fuel;
        pos = x;
    }
    return {pos, t, fuel};
}

// 候補1つ = 行動列 + （補給車なら）担当する巡回車の番号
struct Candidate { std::vector<int> actions; int target = -1; bool parking = false; };

inline bool isIdle(const Agent& a) { return a.actions.empty() && a.nextPos == -1; }

inline void appendWaits(std::vector<int>& action, int n) {
    for (int k = 0; k < n; k++) action.push_back(WAIT);
}

// 経路のうち、日の終わり（steps）までに到着できる部分だけを返す
std::vector<int> truncateByTime(Map& map, int start, const std::vector<int>& path, int step, int steps) {
    std::vector<int> ret;
    int pos = start, t = step;
    for (int x : path) {
        t += map.getMoveCost(pos).time;
        if (t > steps) break;
        ret.push_back(x);
        pos = x;
    }
    return ret;
}

// ------------------------------------------------------------------------
// 候補生成。返り値は「良さそうな順」に並んでいる（先頭が貪欲法での選択）。
// ------------------------------------------------------------------------

// 巡回車: スポットへ行く / 補給車の行き先で待ち合わせる / 1ステップ待つ
std::vector<Candidate> patrolCandidates(State& state, int i, Map& map, const MapDijkstra& md, int steps) {
    Agent& agent = state.agentMgr.agents[i];
    std::vector<Candidate> ret;

    // --- スポット: 今日まだ取っていない・在庫あり・燃料で行ける・今日中に着く
    struct C { bool newBrand; int time; const Spot* spot; };
    std::vector<C> cs;
    bool fuelBlocked = false; // 時間は間に合うのに燃料が足りなくて行けないスポットがある
    for (const Spot& sp : state.spotMgr.spots) {
        if (sp.stock <= 0 || agent.visitedSpotPos.count(sp.pos) || sp.pos == agent.pos) continue;
        const auto* route = findRoute(md, sp.pos);
        if (!reachable(route, agent.pos)) continue;
        const MoveCost& d = route->dist[agent.pos];
        if (state.step + d.time > steps) continue;      // 今日中に着かない
        if (d.fuel > agent.fuel) { fuelBlocked = true; continue; } // 途中で燃料切れになる経路は出さない
        cs.push_back({state.udonBrand.count(sp.brand) == 0, d.time, &sp});
    }
    std::sort(cs.begin(), cs.end(), [](const C& a, const C& b) {
        if (a.newBrand != b.newBrand) return a.newBrand;
        return a.time < b.time;
    });
    std::set<int> pickedPos, pickedBrand;
    auto take = [&](const C& c) {
        if (pickedPos.count(c.spot->pos)) return;
        pickedPos.insert(c.spot->pos);
        ret.push_back({map.getPath(findRoute(md, c.spot->pos)->parent, agent.pos, c.spot->pos, agent.fuel)});
    };
    // 今日まだ取っていない系列は、系列ごとの最寄りを必ず候補にする
    for (const C& c : cs) {
        if (!c.newBrand || pickedBrand.count(c.spot->brand)) continue;
        pickedBrand.insert(c.spot->brand);
        take(c);
    }
    // それ以外は近い順に数個
    int extra = 0;
    for (const C& c : cs) {
        if (extra >= PATROL_SPOT_CANDIDATES) break;
        if (pickedPos.count(c.spot->pos)) continue;
        take(c);
        extra++;
    }

    // --- 日の終わりの位置取り: 今日取れるスポットに間に合わないとき
    // (1) 今日中に着けるスポット（今日取ったスポットでもよい）へ行き、そこで日を終える
    //     → 翌日のステップ1でタダで1玉取れる
    // (2) どこにも間に合わないなら、最寄りのスポットへ途中まで進んで翌日の出発位置を近づける
    // 燃料不足で行けないだけのときは位置取りをせず、下の補給車との待ち合わせに任せる
    if (cs.empty() && !fuelBlocked) {
        bool onSpot = state.spotMgr.findAt(agent.pos) != nullptr;
        // 他の巡回車が日の終わりに向かう（または居座る）スポットは、最大在庫を超えて重ならないようにする
        std::map<int, int> parkedAt;
        for (int j = 0; j < (int)state.agentMgr.agents.size(); j++) {
            const Agent& o = state.agentMgr.agents[j];
            if (j == i || o.kind != AgentKind::PATROL) continue;
            parkedAt[forecast(o, map, state.step).dest]++;
        }
        struct P { int time; const Spot* spot; };
        std::vector<P> parks;
        int nearestTime = BIG; const Spot* nearest = nullptr;
        for (const Spot& sp : state.spotMgr.spots) {
            if (sp.pos == agent.pos) continue;
            const auto* route = findRoute(md, sp.pos);
            if (!reachable(route, agent.pos)) continue;
            const MoveCost& d = route->dist[agent.pos];
            if (d.time < nearestTime) { nearestTime = d.time; nearest = &sp; }
            if (d.fuel > agent.fuel || state.step + d.time > steps) continue;
            if (parkedAt[sp.pos] >= sp.maxStock) continue;
            parks.push_back({d.time, &sp});
        }
        std::sort(parks.begin(), parks.end(), [](const P& a, const P& b) { return a.time < b.time; });
        // 今スポットの上にいて、他の巡回車と最大在庫を超えて重なっていないなら、その場で日を終える
        // （別のスポットへ移っても得はなく、燃料を使って補給車を引っ張るだけになる）
        bool stayHere = onSpot && parkedAt[agent.pos] < state.spotMgr.findAt(agent.pos)->maxStock;
        if (stayHere) ret.push_back({{WAIT}, -1, true});
        for (int k = 0; !stayHere && k < (int)parks.size() && k < PATROL_SPOT_CANDIDATES; k++) {
            const Spot* sp = parks[k].spot;
            ret.push_back({map.getPath(findRoute(md, sp->pos)->parent, agent.pos, sp->pos, agent.fuel), -1, true});
        }
        if (parks.empty() && !onSpot && nearest) {
            std::vector<int> toward = truncateByTime(map, agent.pos,
                map.getPath(findRoute(md, nearest->pos)->parent, agent.pos, nearest->pos, agent.fuel),
                state.step, steps);
            if (!toward.empty()) ret.push_back({toward});
        }
    }

    // --- 補給車との待ち合わせ: 補給車の「行き先」を先読みしてそこへ行き、到着まで待つ
    if (agent.fuel < state.agentMgr.fuel) {
        std::vector<std::pair<int, std::vector<int>>> meet; // (合流ステップ, 行動)
        for (Agent& s : state.agentMgr.agents) {
            if (s.kind != AgentKind::SUPPLY) continue;
            Forecast f = forecast(s, map, state.step);
            if (f.eta > steps) continue;
            std::vector<int> action;
            int arrival = state.step;
            if (f.dest != agent.pos) {
                const auto* route = findRoute(md, f.dest);
                if (!reachable(route, agent.pos)) continue;
                const MoveCost& d = route->dist[agent.pos];
                if (d.fuel > agent.fuel) continue;
                arrival = state.step + d.time;
                if (arrival > steps) continue;
                action = map.getPath(route->parent, agent.pos, f.dest, agent.fuel);
                if (action.empty()) continue;
            }
            appendWaits(action, std::max(0, f.eta - arrival));
            if (action.empty()) continue;
            meet.push_back({std::max(arrival, f.eta), action});
        }
        std::sort(meet.begin(), meet.end(), [](auto& a, auto& b) { return a.first < b.first; });
        // スポット候補の後ろに並べる（燃料不足でスポット候補がなければ、待ち合わせが先頭＝貪欲法での選択になる）
        for (auto& m : meet) ret.push_back({m.second});
    }

    ret.push_back({{WAIT}});
    return ret;
}

// 補給車: 巡回車の行き先を先読みして先回りし、巡回車の到着まで待つ / 1ステップ待つ
// 巡回車と同じセルにいる場合は、同じ経路をたどる＝並走になる（並走中は燃料が減らない）。
//
// 補給車が複数あるときは「担当」を予約し合う:
//   他の補給車が担当中（target）の巡回車は後回しにして、まだ誰も向かっていない巡回車を
//   燃料が少ない順に選ぶ。これで補給車が1台の巡回車に固まらず、ばらけて動く。
std::vector<Candidate> supplyCandidates(State& state, int i, Map& map, const MapDijkstra& md, int steps) {
    Agent& agent = state.agentMgr.agents[i];
    const auto& agents = state.agentMgr.agents;

    std::vector<int> claimedBy(agents.size(), 0); // 巡回車ごとに、担当している「他の」補給車の数
    for (int j = 0; j < (int)agents.size(); j++) {
        if (j == i || agents[j].kind != AgentKind::SUPPLY) continue;
        int t = agents[j].target;
        if (t >= 0 && t < (int)agents.size()) claimedBy[t]++;
    }

    struct M { int fuel; int patrol; std::vector<int> action; };
    std::vector<M> freeMeet, claimedMeet;
    for (int p = 0; p < (int)agents.size(); p++) {
        if (agents[p].kind != AgentKind::PATROL) continue;
        Forecast f = forecast(agents[p], map, state.step);
        if (f.fuel >= state.agentMgr.fuel) continue; // 補給しても得がない
        if (f.eta > steps) continue;
        std::vector<int> action;
        int arrival = state.step;
        if (f.dest != agent.pos) {
            const auto* route = findRoute(md, f.dest);
            if (!reachable(route, agent.pos)) continue;
            arrival = state.step + route->dist[agent.pos].time;
            if (arrival > steps) continue;
            action = map.getPath(route->parent, agent.pos, f.dest, INT_MAX);
            if (action.empty()) continue;
        }
        appendWaits(action, std::max(0, f.eta - arrival));
        if (action.empty()) continue;
        (claimedBy[p] ? claimedMeet : freeMeet).push_back({f.fuel, p, action});
    }
    auto byFuel = [](const M& a, const M& b) { return a.fuel < b.fuel; };
    std::sort(freeMeet.begin(), freeMeet.end(), byFuel);
    std::sort(claimedMeet.begin(), claimedMeet.end(), byFuel);

    // 並び順 = 誰も担当していない巡回車（燃料が少ない順）→ 待機 → 担当済みの巡回車
    // 先頭が貪欲法での選択になるので、担当済みの巡回車に2台目が向かうことは基本的にない。
    std::vector<Candidate> ret;
    for (auto& m : freeMeet) ret.push_back({m.action, m.patrol});
    ret.push_back({{WAIT}, -1});
    for (auto& m : claimedMeet) ret.push_back({m.action, m.patrol});
    return ret;
}

std::vector<Candidate> candidates(State& state, int i, Map& map, const MapDijkstra& md, int steps) {
    if (state.agentMgr.agents[i].kind == AgentKind::PATROL) return patrolCandidates(state, i, map, md, steps);
    return supplyCandidates(state, i, map, md, steps);
}

// 行動が空いているエージェントを1体選ぶ。巡回車を先に決め、補給車はその計画を先読みして決める。
int pickIdle(const State& state) {
    const auto& agents = state.agentMgr.agents;
    for (int i = 0; i < (int)agents.size(); i++)
        if (agents[i].kind == AgentKind::PATROL && isIdle(agents[i])) return i;
    for (int i = 0; i < (int)agents.size(); i++)
        if (agents[i].kind == AgentKind::SUPPLY && isIdle(agents[i])) return i;
    return -1;
}

void evaluate(
    State& state, Map& map, const MapDijkstra& md, std::vector<ReverseDijkstraResult>& dd, const std::set<int>& brands
) {
    double agentsToNearestSpotScore = 0;
    for (Agent& agent: state.agentMgr.agents) {
        if (agent.kind == AgentKind::SUPPLY) continue;
        double nearest = BIG;
        for (Spot& spot: state.spotMgr.spots) {
            if (spot.stock <= 0 || agent.visitedSpotPos.count(spot.pos) > 0 || spot.pos == agent.pos) continue;
            const auto* route = findRoute(md, spot.pos);
            if (reachable(route, agent.pos))
                nearest = std::min((int)nearest, route->dist[agent.pos].time);
        }
        agentsToNearestSpotScore += (map.width + map.height) / nearest;
    }

    double fuel = 0;
    for (Agent& agent: state.agentMgr.agents) {
        if (agent.kind == AgentKind::SUPPLY) continue;
        fuel += agent.fuel;
    }
    fuel /= std::max(1, state.agentMgr.patrolNum);

    // 先読み: 今の行動キューの行き先で、これから取れそうなうどん・系列・補給を少し加点する。
    // （同じステップで行動を決めた直後の状態どうしは位置が同じなので、これがないと差がつかない）
    double pendingUdon = 0, pendingRefuel = 0;
    std::map<int, double> pendingBrand;
    std::vector<std::pair<Forecast, int>> patrolPlans; // (先読み, 巡回車の番号)
    double scale = map.width + map.height;
    for (int i = 0; i < (int)state.agentMgr.agents.size(); i++) {
        Agent& agent = state.agentMgr.agents[i];
        if (agent.kind != AgentKind::PATROL) continue;
        Forecast f = forecast(agent, map, state.step);
        patrolPlans.push_back({f, i});
        if (f.dest == agent.pos && agent.nextPos == -1) continue;
        Spot* sp = state.spotMgr.findAt(f.dest);
        if (!sp || sp->stock <= 0 || agent.visitedSpotPos.count(sp->pos)) continue;
        double discount = 1.0 / (1.0 + (f.eta - state.step) / scale);
        pendingUdon += discount;
        if (!state.udonBrand.count(sp->brand))
            pendingBrand[sp->brand] = std::max(pendingBrand[sp->brand], discount);
    }
    // 補給車が先に（または同時に）着いて待っている合流なら、補給できる燃料分を加点。
    // 巡回車ごとに1回だけ数えるので、2台目の補給車が同じ巡回車に向かっても点は増えない。
    std::vector<Forecast> supplyPlans;
    for (Agent& s : state.agentMgr.agents)
        if (s.kind == AgentKind::SUPPLY) supplyPlans.push_back(forecast(s, map, state.step));
    for (auto& [fp, i] : patrolPlans) {
        for (const Forecast& fs : supplyPlans) {
            if (fp.dest == fs.dest && fs.eta <= fp.eta) {
                pendingRefuel += std::max(0, state.agentMgr.fuel - fp.fuel);
                break;
            }
        }
    }
    double pendingBrands = 0;
    for (auto& [b, d] : pendingBrand) pendingBrands += d;

    // 日の終わりの位置取り: 最終ステップでは実際にスポット上にいる巡回車を、
    // それより前は「スポットで日を終えるため」に向かっている巡回車を数える（スポットの最大在庫まで）
    double parked = 0;
    {
        std::map<int, int> used;
        bool last = state.daySteps > 0 && state.step >= state.daySteps;
        for (int k = 0; k < (int)patrolPlans.size(); k++) {
            const Agent& agent = state.agentMgr.agents[patrolPlans[k].second];
            const Forecast& f = patrolPlans[k].first;
            int pos;
            if (last) pos = agent.pos;
            else if (agent.parking && f.eta <= state.daySteps) pos = f.dest;
            else continue;
            Spot* sp = state.spotMgr.findAt(pos);
            if (!sp || used[pos] >= sp->maxStock) continue;
            used[pos]++;
            parked += 1;
        }
    }

    state.score = 0;
    size_t totalBrands = brands.size();
    for (int brand : state.udonBrand) {
        if (brands.count(brand) == 0) ++totalBrands;
    }
    std::vector<double> x = {
        (double)totalBrands,
        (double)state.udonBrand.size(),
        (double)state.udonSum,
        fuel,
        agentsToNearestSpotScore
    };
    std::vector<double> w = evaluateWeight;
    for (int i = 0; i < x.size(); i++) state.score += w[i] * x[i];
    state.score += PENDING_BRAND_WEIGHT * pendingBrands + PENDING_UDON_WEIGHT * pendingUdon
                 + PENDING_REFUEL_WEIGHT * pendingRefuel / std::max(1, state.agentMgr.patrolNum)
                 + PARK_WEIGHT * parked;
}

// ビームサーチの分岐: 行動が空いている1体について、候補ごとに状態を複製する
std::vector<State> separate(
    State& state, Map& map, MapDijkstra& md, std::vector<ReverseDijkstraResult>& dd, int steps,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()
) {
    std::vector<State> ret;
    int i = pickIdle(state);
    if (i == -1) return ret;

    for (Candidate& c : candidates(state, i, map, md, steps)) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        State s = state;
        Agent& newAgent = s.agentMgr.agents[i];
        for (int a : c.actions) newAgent.actions.push(a);
        newAgent.target = c.target;
        newAgent.parking = c.parking;
        ret.push_back(std::move(s));
    }
    return ret;
}

bool update(State& state, Map& map, int steps) {
    if (state.step >= steps) return false;
    for (Agent& agent : state.agentMgr.agents) {
        if (agent.actions.empty() && agent.nextPos == -1) return false;
    }

    for (Agent& agent : state.agentMgr.agents) {
        if (agent.history.size() < state.step + 1) agent.history.push_back(WAIT);

        if (agent.nextPos != -1) {
            agent.history[state.step] = INACTION;
            continue;
        }

        int nextPos = agent.actions.front();

        if (nextPos == WAIT) {
            agent.history[state.step] = WAIT;
            agent.actions.pop();
            continue;
        }

        int direction = map.getDirection(agent.pos, nextPos);
        if (direction == -1 || map.terrainAt(nextPos) == Terrain::POND) {
            agent.actions.clear();
            continue; // history は WAIT のまま。不正な移動を提出しない。
        }

        MoveCost cost = map.getMoveCost(agent.pos);

        if (state.step + cost.time > steps) {
            agent.history[state.step] = WAIT;
            continue;
        }

        if (agent.kind == AgentKind::PATROL && agent.fuel < cost.fuel) {
            // 燃料不足で動けない移動は捨てる（キューに残すと永久に止まる）
            agent.actions.clear();
            continue;
        }

        agent.history[state.step] = direction;

        agent.nextPos = nextPos;
        agent.nextFuel = cost.fuel;
        agent.moveEndStep = state.step + cost.time;
        agent.actions.pop();
    }

    state.step++;

    // step は次のアクション時刻。0 はアクションのみ、steps は反映のみ。
    // 全車の移動を反映してから、取得と補給を行う（公式 Q6）。
    for (Agent& agent : state.agentMgr.agents) {
        if (agent.nextPos != -1 && state.step == agent.moveEndStep) {
            if (agent.kind == AgentKind::PATROL) agent.fuel -= agent.nextFuel;
            agent.pos = agent.nextPos;
            agent.nextPos = -1;
            agent.nextFuel = 0;
            agent.moveEndStep = -1;
        }
    }
    state.collectUdon();
    state.applySupply();

    return true;
}

// 時間切れ用: 今ある行動キューは残したまま、空いたエージェントには貪欲に1手を割り当てて最後まで進める。
// 1日分でも数ミリ秒で終わるので、締め切りを過ぎてからでも実行できる。
void rollout(State& state, Map& map, const MapDijkstra& md, int steps) {
    state.daySteps = steps;
    while (state.step < steps) {
        int i;
        while ((i = pickIdle(state)) != -1) {
            auto cand = candidates(state, i, map, md, steps);
            for (int a : cand.front().actions) state.agentMgr.agents[i].actions.push(a);
            state.agentMgr.agents[i].target = cand.front().target;
            state.agentMgr.agents[i].parking = cand.front().parking;
        }
        update(state, map, steps);
    }
}
