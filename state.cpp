#include "map.cpp"
#include "spot.cpp"
#include "agent.cpp"

#define INACTION INT_MIN
#define WAIT -1

const double fuelPerMax = 0.3;
const std::vector<double> evaluateWeight = {100000000, 1000000, 10000, 1, 100};

class State {
public:
    SpotManager spotMgr;
    AgentManager agentMgr;

    int step = 0;
    long long score = 0;
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

void evaluate(
    State& state, Map& map, std::vector<ReverseDijkstraResult>& md, std::vector<ReverseDijkstraResult>& dd, const std::set<int>& brands
) {
    double agentsToNearestSpotScore = 0;
    for (Agent& agent: state.agentMgr.agents) {
        if (agent.kind == AgentKind::SUPPLY) continue;
        double nearest = BIG;
        for (Spot& spot: state.spotMgr.spots) {
            if (agent.visitedSpotPos.count(spot.pos) > 0 || spot.pos == agent.pos) continue;
            nearest = std::min((int)nearest, md[spot.pos].dist[agent.pos].time);
        }
        agentsToNearestSpotScore += (map.width + map.height) / nearest;
    }

    double fuel = 0;
    for (Agent& agent: state.agentMgr.agents) {
        if (agent.kind == AgentKind::SUPPLY) continue;
        fuel += agent.fuel;
    }
    fuel /= std::max(1, state.agentMgr.patrolNum);

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
    for (int i = 0; i < x.size(); i++) state.score += (long long)(w[i] * x[i]);
}

std::vector<State> separate(
    State& state, Map& map, std::vector<ReverseDijkstraResult>& md, std::vector<ReverseDijkstraResult>& dd,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()
) {
    std::vector<State> ret;

    for (int i = 0; i < state.agentMgr.agents.size(); i++) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        if (!state.agentMgr.agents[i].actions.empty()) continue;
        Agent& agent = state.agentMgr.agents[i];
        // 移動が反映されてから、新しい現在地・燃料で経路を生成する。
        if (agent.nextPos != -1) continue;
        std::vector<std::vector<int>> actions;

        std::vector<int> path;
        if (agent.kind == AgentKind::PATROL) {
            for (Spot& target : state.spotMgr.spots) {
                if ((agent.visitedSpotPos.find(target.pos) != agent.visitedSpotPos.end()
                        || agent.visitedSpotPos.size() == state.spotMgr.brands.size())
                        && target.stock != 0) {
                    path = map.getPath(md[target.pos].parent, agent.pos, target.pos, agent.fuel);
                    if (!path.empty()) actions.push_back(path);
                }
            }
            for (Agent& target : state.agentMgr.agents) {
                if (target.kind != AgentKind::SUPPLY) continue;
                path = map.getHalfPath(md[target.pos].parent, agent.pos, target.pos, agent.fuel);
                if (!path.empty()) actions.push_back(path);
            }
        } else {
            for (Agent& target : state.agentMgr.agents) {
                if (target.kind != AgentKind::PATROL) continue;
                path = map.getHalfPath(md[target.pos].parent, agent.pos, target.pos, INT_MAX);
                if (!path.empty()) actions.push_back(path);
            }
            for (Spot& target : state.spotMgr.spots) {
                path = map.getPath(md[target.pos].parent, agent.pos, target.pos, INT_MAX);
                if (!path.empty()) actions.push_back(path);
            }
        }
        actions.push_back({-1});

        for (std::vector<int>& action : actions) {
            if (std::chrono::steady_clock::now() >= deadline) break;
            State s = state;
            Agent& newAgent = s.agentMgr.agents[i];
            while (!newAgent.actions.empty()) newAgent.actions.pop();
            for (int a : action) newAgent.actions.push(a);
            ret.push_back(s);
        }
        break;
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
            while (!agent.actions.empty()) agent.actions.pop();
            continue; // history は WAIT のまま。不正な移動を提出しない。
        }

        MoveCost cost = map.getMoveCost(agent.pos);

        if (state.step + cost.time > steps) {
            agent.history[state.step] = WAIT;
            continue;
        }

        if (agent.kind == AgentKind::PATROL && agent.fuel < cost.fuel) continue;

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