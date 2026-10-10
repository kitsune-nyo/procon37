// 第37回 全国高等専門学校 プログラミングコンテスト
// 競技部門「ヘキサうどん」回答用プログラム
//
// ファイル構成:
//   hexudon_main.cpp ... このファイル (通信 + 日ごとの探索呼び出し + main)
//   state.cpp ... 探索 (State / evaluate / separate / update)
//   map.cpp / spot.cpp / agent.cpp ... state.cpp から #include される
//
// コンパイル方法:
//   https://github.com/nlohmann/json/releases から json.hpp をDL
//   g++ -std=c++17 -O2 hexudon_main.cpp -lcurl -o hexudon_main

// ToDo:
//   変更:
//     目的地が新しい brand であれば評価関数のスコアを大きくする
//   計測:
//     1 回分岐した時の時間計測
//     1 回更新したときの時間計測
//     1 stepの時間計測
//     増えた state 数の計測

// ========================================================================
// 0. including
// ========================================================================
#pragma region including

#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <thread>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <curl/curl.h>

#include "json.hpp"
#include "state.cpp"

using json = nlohmann::json;

#pragma endregion

// ========================================================================
// 1. setting
// ========================================================================
#pragma region setting

static const std::string BASE_URL     = std::getenv("HEXUDON_BASE_URL") ?
    std::getenv("HEXUDON_BASE_URL") : "http://172.28.0.10:8080";
// static const std::string TEAM_TOKEN   = "?token=token-p0";
static const std::string TEAM_TOKEN   = "?token=nara246ce53ecc529c2d7e86e2cf4a32980439256b914570b3f6d6bc9deb9bd6";
static const std::string SETTING_PATH = "/setting";
static const std::string AGENT_PATH   = "/agent";
static const std::string PROBLEM_PATH = "/";

constexpr int    BEAM_WIDTH        = 3000; // ビーム幅の上限
constexpr double TIME_BUDGET_RATIO = 0.65; // 1日(daySeconds)のうち探索に使ってよい割合
constexpr double HTTP_RESERVE_SEC = 1.0;

#pragma endregion

// ========================================================================
// 2. protocol
// ========================================================================
#pragma region protocol

struct SpotConfig { int brand, pos, stocks; };

struct AgentState { int kind, pos, fuel; };

struct TrafficInfo { int pos, status; };

struct MapData {
    int height, width;
    std::vector<std::vector<int>> cells;
};

struct MatchConfig {
    long long startsAt;
    std::vector<int> daySeconds ,daySteps;
    MapData map;
    std::vector<SpotConfig> spots;
    std::vector<int> agents;
    int fuelLimits, players;
    int busyThreshold, jammedThreshold;
};

struct OtherTeam {
    int id;
    std::vector<AgentState> agents;
};

struct DayInfo {
    long long endsAt;
    int day;
    std::vector<AgentState> agents;
    std::vector<OtherTeam> others;
    std::vector<TrafficInfo> traffics;
};

void from_json(const json& j, MapData& m) {
    m.height = j.at("height").get<int>();
    m.width  = j.at("width").get<int>();
    m.cells  = j.at("cells").get<std::vector<std::vector<int>>>();
}

void from_json(const json& j, SpotConfig& s) {
    s.brand  = j.at("brand").get<int>();
    s.pos    = j.at("pos").get<int>();
    s.stocks = j.at("stocks").get<int>();
}

void from_json(const json& j, MatchConfig& in) {
    in.startsAt        = j.at("startsAt").get<long long>();
    in.daySeconds      = j.at("daySeconds").get<std::vector<int>>();
    in.daySteps        = j.at("daySteps").get<std::vector<int>>();
    in.map             = j.at("map").get<MapData>();
    in.spots           = j.at("spots").get<std::vector<SpotConfig>>();
    in.agents          = j.at("agents").get<std::vector<int>>();
    in.fuelLimits      = j.at("fuelLimits").get<int>();
    in.players         = j.at("players").get<int>();
    in.busyThreshold   = j.at("busyThreshold").get<int>();
    in.jammedThreshold = j.at("jammedThreshold").get<int>();
}

void from_json(const json& j, AgentState& a) {
    a.kind = j.at("kind").get<int>();
    a.pos  = j.at("pos").get<int>();
    a.fuel = j.at("fuel").get<int>();
}

void from_json(const json& j, OtherTeam& o) {
    o.id     = j.at("id").get<int>();
    o.agents = j.at("agents").get<std::vector<AgentState>>();
}

void from_json(const json& j, TrafficInfo& t) {
    t.pos    = j.at("pos").get<int>();
    t.status = j.at("status").get<int>();
}

void from_json(const json& j, DayInfo& in) {
    in.endsAt   = j.at("endsAt").get<long long>();
    in.day      = j.at("day").get<int>();
    in.agents   = j.at("agents").get<std::vector<AgentState>>();
    in.others   = j.at("others").get<std::vector<OtherTeam>>();
    in.traffics = j.at("traffics").get<std::vector<TrafficInfo>>();
}

#pragma endregion

// ========================================================================
// 3. solver
// ========================================================================
#pragma region solver

// historyを「方向 or まとめた待機(負の値)」の列に変換する (サーバーへ送る形式)
inline std::vector<int> compress(const std::vector<int>& v) {
    std::vector<int> ret;
    int sum = 0;
    for (int e : v) {
        if (e == INACTION) continue;
        if (e < 0) {
            sum += e;
        } else {
            if (sum < 0) { ret.push_back(sum); sum = 0; }
            ret.push_back(e);
        }
    }
    if (sum < 0) ret.push_back(sum);
    return ret;
}

// その日の渋滞状況を反映したマップを作る (道路コストが日ごとに変わるので毎日作り直す)
inline Map buildMap(const MatchConfig& config, const DayInfo& info) {
    Map map;
    map.init(config.map.width, config.map.height, config.map.cells);
    int n = config.map.width * config.map.height;
    for (const auto& t : info.traffics) {
        if (t.pos < 0 || t.pos >= n) continue;
        map.setRoadStatus(t.pos, static_cast<RoadStatus>(t.status));
    }
    return map;
}

inline double remainingSeconds(long long endsAt) {
    return endsAt - std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline double searchBudget(double nominalSec, double remainingSec) {
    return std::max(0.0, std::min({nominalSec * TIME_BUDGET_RATIO,
        remainingSec * TIME_BUDGET_RATIO, remainingSec - HTTP_RESERVE_SEC}));
}

inline long submissionTimeout(long long endsAt) {
    // startsAt == 0 は開始時刻未確定（公式 MatchSetting）。期限を推測しない。
    if (endsAt == 10) return 15000;
    return (long)std::clamp(remainingSeconds(endsAt) * 1000.0, 1.0, 15000.0);
}

// 探索が途中で終わった候補は、決まっている行動はそのまま実行し、
// 残りのステップは貪欲法（rollout）で埋める。待機で埋めることはしない。
inline std::vector<std::vector<int>> finishPlans(
    State state, Map& map, const MapDijkstra& md, int steps, std::set<int>* acquiredBrands
) {
    rollout(state, map, md, steps);
    if (acquiredBrands) *acquiredBrands = state.udonBrand;
    std::vector<std::vector<int>> plans;
    for (Agent& agent : state.agentMgr.agents) plans.push_back(compress(agent.history));
    return plans;
}

inline std::vector<std::vector<int>> agentKinds(int numAgents) {
    std::vector<std::vector<int>> ret;
    // 補給車数の既存の探索範囲を維持する。公式のエージェント数は3～8。
    for (int i = numAgents / 3; i <= numAgents / 2; ++i) {
        for (int j = 0; j < (1 << numAgents); ++j) {
            std::vector<int> k;
            int agt = 0;
            for (int bit = numAgents - 1; bit >= 0; --bit) {
                int kind = (j >> bit) & 1;
                k.push_back(kind);
                agt += kind;
            }
            if (agt == i) ret.push_back(k);
        }
    }
    return ret;
}

// ビームサーチ本体（solveDay / agentsKind 共通）。
// 1ステップごとに「暇なエージェントを1体決める → 上位 width 個に絞る」を暇な車がいなくなるまで繰り返し、
// そのあと全状態を1ステップ進める。組み合わせを全部展開してから絞ると、エージェントが多いときに
// 1ステップ目で時間切れになるため、1体ごとに絞る。
// 時間切れになったら、その時点で一番良い状態を返す（残りは呼び出し側で rollout する）。
constexpr size_t BEAM_START_WIDTH = 100;

inline State beamSearch(
    std::vector<State> beam, Map& map, MapDijkstra& md, int steps, double budgetSec,
    std::chrono::steady_clock::time_point deadline, const std::set<int>& brands, size_t* lastWidth = nullptr
) {
    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();
    auto elapsed = [&]() { return std::chrono::duration<double>(Clock::now() - t0).count(); };
    std::vector<ReverseDijkstraResult> dd; // 未使用（evaluate / separate の引数合わせ）
    size_t width = BEAM_START_WIDTH;
    double emaSecPerWidth = -1;

    auto better = [](const State& a, const State& b) {
        if (a.step != b.step) return a.step > b.step;
        return a.score > b.score;
    };
    auto keepTop = [&](std::vector<State>& v) {
        if (v.size() <= width) return;
        std::nth_element(v.begin(), v.begin() + width, v.end(), better);
        v.erase(v.begin() + width, v.end());
    };
    auto bestOf = [&](std::vector<State>& v) -> State {
        return *std::min_element(v.begin(), v.end(), better);
    };

    for (State& st : beam) { st.daySteps = steps; evaluate(st, map, md, dd, brands); }
    keepTop(beam);

    while (!beam.empty() && beam.front().step < steps) {
        double stepStart = elapsed();

        // 1) 暇なエージェントを1体ずつ決める
        while (true) {
            std::vector<State> next;
            bool expanded = false;
            for (size_t k = 0; k < beam.size(); k++) {
                if (Clock::now() >= deadline) {
                    for (size_t r = k; r < beam.size(); r++) next.push_back(std::move(beam[r]));
                    if (lastWidth) *lastWidth = width;
                    return bestOf(next);
                }
                if (pickIdle(beam[k]) == -1) { next.push_back(std::move(beam[k])); continue; }
                expanded = true;
                for (State& c : separate(beam[k], map, md, dd, steps)) {
                    evaluate(c, map, md, dd, brands);
                    next.push_back(std::move(c));
                }
            }
            beam = std::move(next);
            keepTop(beam);
            if (!expanded) break;
        }

        // 2) 全状態を1ステップ進める
        for (State& st : beam) {
            update(st, map, steps);
            evaluate(st, map, md, dd, brands);
        }
        keepTop(beam);

        // 3) 残り時間に合わせてビーム幅を調整する
        // 1ステップの重さは「暇な車が何台いるか」で大きく変わるので、移動平均で見る
        double now = elapsed();
        double stepSec = std::max(now - stepStart, 1e-6);
        emaSecPerWidth = emaSecPerWidth < 0 ? stepSec / width : 0.8 * emaSecPerWidth + 0.2 * stepSec / width;
        int remaining = steps - beam.front().step;
        if (remaining > 0) {
            double target = std::max(0.0, budgetSec - now) / remaining;
            double want = target / emaSecPerWidth;
            want = std::clamp(want, width * 0.7, width * 1.3 + 1);
            width = std::clamp<size_t>((size_t)want, 1, BEAM_WIDTH);
        }
        if (Clock::now() >= deadline) break;
    }
    if (lastWidth) *lastWidth = width;
    return bestOf(beam);
}

// 1日分のビームサーチを実行し、エージェントごとの行動計画を返す
inline std::vector<std::vector<int>> solveDay(
    Map& map, const MatchConfig& config, const DayInfo& info, int steps, double budgetSec,
    const std::set<int>& brands, std::set<int>* acquiredBrands = nullptr
) {
    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();
    auto elapsed = [&]() { return std::chrono::duration<double>(Clock::now() - t0).count(); };
    auto deadline = t0 + std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(std::max(0.0, budgetSec)));

    // スポット
    SpotManager spotMgr;
    for (const SpotConfig& s : config.spots) spotMgr.placeSpot(s.brand, s.pos, s.stocks);

    // エージェント
    AgentManager agentMgr;
    agentMgr.fuel = config.fuelLimits;
    for (size_t i = 0; i < info.agents.size(); ++i) {
        agentMgr.placeAgent(info.agents[i].pos, info.agents[i].fuel);
        if (info.agents[i].kind == 1) agentMgr.decideSupply((int)i);
    }
    State initial(spotMgr, agentMgr);

    // キャッシュ: スポット始点の逆ダイクストラだけを最初に計算する。
    // 加えて、スポット外で日を始めた巡回車の位置も始点にする（補給車が迎えに行けるように）。
    std::vector<int> strandedPatrols;
    for (const Agent& a : initial.agentMgr.agents)
        if (a.kind == AgentKind::PATROL && !initial.spotMgr.findAt(a.pos)) strandedPatrols.push_back(a.pos);
    MapDijkstra mapDijkstra;
    precomputeRoutes(mapDijkstra, map, spotMgr, strandedPatrols);
    // dropDijkstra は現在の evaluate / separate では使われていないので空のまま渡す
    std::vector<ReverseDijkstraResult> dropDijkstra;

    // ビームサーチ
    size_t beamWidth = 0;
    State best = beamSearch({initial}, map, mapDijkstra, steps, budgetSec, deadline, brands, &beamWidth);

    int reachedStep = best.step;
    std::vector<std::vector<int>> plans = finishPlans(best, map, mapDijkstra, steps, acquiredBrands);
    std::cout << "  Searching result: reached step " << reachedStep << "/" << steps
              << " (rest by greedy), brands=" << (acquiredBrands ? (int)acquiredBrands->size() : -1)
              << " (searching " << elapsed() << " seconds, last beam width " << beamWidth << ")" << std::endl;
    return plans;
}

inline std::vector<int> agentsKind(const MatchConfig& config) {
    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();
    auto elapsed = [&]() { return std::chrono::duration<double>(Clock::now() - t0).count(); };

    int steps = config.daySteps[0];
    double remainingSec = remainingSeconds(config.startsAt);
    double budgetSec = searchBudget(remainingSec, remainingSec);
    auto deadline = t0 + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(budgetSec));
    auto kinds = agentKinds((int)config.agents.size());

    std::cout << "Started searching agents types (steps=" << steps
                << ", " << budgetSec << " seconds)" << std::endl;

    DayInfo tmp;
    std::set<int> brands;
    Map map = buildMap(config, tmp);

    SpotManager spotMgr;
    for (const SpotConfig& s : config.spots) spotMgr.placeSpot(s.brand, s.pos, s.stocks);

    // 初期位置はスポット外なので、補給車が迎えに行けるよう始点に加える
    MapDijkstra mapDijkstra;
    precomputeRoutes(mapDijkstra, map, spotMgr, config.agents);
    // dropDijkstra は現在の evaluate / separate では使われていないので空のまま渡す
    std::vector<ReverseDijkstraResult> dropDijkstra;

    AgentManager agentMgr;
    agentMgr.fuel = config.fuelLimits;
    for (size_t i = 0; i < config.agents.size(); ++i) agentMgr.placeAgent(config.agents[i], config.fuelLimits);

    // まず全候補を貪欲法で1日分走らせ、一番良いものを保険にする（数ミリ秒×候補数）
    std::vector<int> fallback = kinds.front();
    double fallbackScore = -1e18;
    for (const auto& k : kinds) {
        AgentManager candidateMgr = agentMgr;
        for (int l = 0; l < (int)k.size(); ++l) if (k[l] == 1) candidateMgr.decideSupply(l);
        State st(spotMgr, candidateMgr);
        rollout(st, map, mapDijkstra, steps);
        evaluate(st, map, mapDijkstra, dropDijkstra, brands);
        if (st.score > fallbackScore) { fallbackScore = st.score; fallback = k; }
    }
    if (Clock::now() >= deadline) return fallback;

    std::vector<State> initials;
    for (const auto& k : kinds) {
        AgentManager candidateMgr = agentMgr;
        for (int l = 0; l < (int)k.size(); ++l) if (k[l] == 1) candidateMgr.decideSupply(l);
        initials.emplace_back(spotMgr, candidateMgr);
    }
    size_t beamWidth = 0;
    double remainSec = std::chrono::duration<double>(deadline - Clock::now()).count();
    State best = beamSearch(initials, map, mapDijkstra, steps, remainSec, deadline, brands, &beamWidth);
    int reachedStep = best.step;
    rollout(best, map, mapDijkstra, steps); // 途中で時間切れなら残りを貪欲法で埋めて比較する
    evaluate(best, map, mapDijkstra, dropDijkstra, brands);

    std::cout << "  Searching result: reached step " << reachedStep << "/" << steps
              << " brand=" << best.udonBrand.size() << " supply=" << best.agentMgr.supplyNum
              << " udon=" << best.udonSum
              << " score=" << best.score << " (greedy best " << fallbackScore << ")"
              << " (searching " << elapsed() << " seconds, last beam width " << beamWidth << ")" << std::endl;
    if (best.score < fallbackScore) return fallback;

    std::vector<int> ret;
    for (auto& i : best.agentMgr.agents) ret.push_back(i.kind == AgentKind::SUPPLY ? 1 : 0);
    return ret;
}

#pragma endregion

// ========================================================================
// 4. http
// ========================================================================
#pragma region http

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string http_get(const std::string& url, long timeout_sec = 15) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "field-client/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        std::string msg = std::string("curl GET error: ") + curl_easy_strerror(rc);
        curl_easy_cleanup(curl);
        throw std::runtime_error(msg);
    }
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    if (code < 200 || code >= 300) throw std::runtime_error("HTTP GET status " + std::to_string(code));
    return body;
}

json get(const std::string& path) {
    const std::string fullURL = BASE_URL + path + TEAM_TOKEN;
    std::string text = http_get(fullURL);
    return json::parse(text);
}

std::string post_json(const json& body_json, const std::string& url, long timeout_ms = 15000) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    std::string resp;
    std::string body = body_json.dump();

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, std::max(1L, timeout_ms));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, std::min(5000L, std::max(1L, timeout_ms)));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);

    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) throw std::runtime_error(curl_easy_strerror(rc));
    if (code < 200 || code >= 300) throw std::runtime_error("HTTP POST failed: status=" + std::to_string(code) + " body=" + resp);

    return resp;
}

std::string post_agent_types(const std::vector<int>& types, long timeout_ms = 15000) {
    return post_json(json(types), BASE_URL + AGENT_PATH + TEAM_TOKEN, timeout_ms);
}

std::string post_actions(const std::vector<std::vector<int>>& plans, long timeout_ms = 15000) {
    std::string resp = post_json(json(plans), BASE_URL + PROBLEM_PATH + TEAM_TOKEN, timeout_ms);
    // 公式 API の ActionAccepted は HTTP 200 でも revision < 0 なら不受理。
    if (json::parse(resp).at("revision").get<int>() < 0) throw std::runtime_error("Action plan rejected: " + resp);
    return resp;
}

#pragma endregion

// ========================================================================
// 5. main
// ========================================================================
#pragma region main

// JSON Lines keeps completed records readable even if the client stops mid-match.
// Logging failures must never prevent a plan from being submitted.
class ReplayLog {
    std::ofstream stream;
public:
    ReplayLog() {
        try {
            const char* overridePath = std::getenv("HEXUDON_REPLAY_PATH");
            auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            std::filesystem::path path = overridePath ? overridePath :
                "output/replays/match-" + std::to_string(stamp) + ".jsonl";
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
            stream.open(path, std::ios::trunc);
            if (stream) std::cout << "Replay log: " << path.string() << std::endl;
            else std::cerr << "Warning: could not open replay log " << path.string() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Warning: replay logging unavailable: " << e.what() << std::endl;
        }
    }
    void write(const json& record) {
        if (!stream) return;
        stream << record.dump() << '\n';
        stream.flush();
        if (!stream) std::cerr << "Warning: replay log write failed" << std::endl;
    }
};

DayInfo getDayInfo(int day) {
    DayInfo ret;
    std::string error = "HTTP GET status 403";
    while (true) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(250)
        );
        try {
            json d = get(PROBLEM_PATH);
            ret = d.get<DayInfo>();
            if (day == ret.day) break;
        } catch (const std::exception& e) {
            if (error != e.what()) {
                error = e.what();
                std::cout << e.what() << std::endl;
            }
        }
    }
    return ret;
}

int main() {
    try {
        json problem = get(SETTING_PATH);
        MatchConfig config = problem.get<MatchConfig>();
        ReplayLog replay;
        replay.write({{"type", "setting"}, {"setting", problem}, {"version", 1}});

        std::vector<int> agentTypes = agentsKind(config);
        post_agent_types(agentTypes, submissionTimeout(config.startsAt));
        replay.write({{"type", "roles"}, {"kinds", agentTypes}});

        std::set<int> brands;
        int numDays = (int)config.daySteps.size();

        for (int day = 0; day < numDays; ++day) {
            DayInfo info = getDayInfo(day);
            json startAgents = json::array(), traffic = json::array(), others = json::array();
            for (const auto& a : info.agents)
                startAgents.push_back({{"kind", a.kind}, {"pos", a.pos}, {"fuel", a.fuel}});
            for (const auto& t : info.traffics)
                traffic.push_back({{"pos", t.pos}, {"status", t.status}});
            for (const auto& team : info.others) {
                json agents = json::array();
                for (const auto& a : team.agents)
                    agents.push_back({{"kind", a.kind}, {"pos", a.pos}, {"fuel", a.fuel}});
                others.push_back({{"id", team.id}, {"agents", agents}});
            }
            replay.write({{"type", "day_start"}, {"info", {{"day", day}, {"endsAt", info.endsAt},
                {"agents", startAgents}, {"traffics", traffic}, {"others", others}}}});

            int totalSteps = config.daySteps[day];
            double daySec = config.daySeconds[day];
            double budgetSec = searchBudget(daySec, remainingSeconds(info.endsAt));
            auto searchStart = std::chrono::steady_clock::now();

            std::cout << "Day " << day << " started searching (steps=" << totalSteps
                      << ", " << budgetSec << " seconds)" << std::endl;

            Map map = buildMap(config, info);
            budgetSec = std::max(0.0, budgetSec - std::chrono::duration<double>(
                std::chrono::steady_clock::now() - searchStart).count());
            std::set<int> acquiredBrands;
            std::vector<std::vector<int>> plans = solveDay(map, config, info, totalSteps, budgetSec, brands, &acquiredBrands);

            replay.write({{"type", "plan"}, {"day", day}, {"plans", plans}});
            try {
                std::string reply = post_actions(plans, submissionTimeout(info.endsAt));
                replay.write({{"type", "submission"}, {"day", day}, {"accepted", true},
                    {"response", json::parse(reply)}});
            } catch (const std::exception& e) {
                replay.write({{"type", "submission"}, {"day", day}, {"accepted", false}, {"error", e.what()}});
                throw;
            }
            // 公式の日次情報には取得ブランドがないため、受理された計画の再現結果を引き継ぐ。
            brands.insert(acquiredBrands.begin(), acquiredBrands.end());
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

#pragma endregion
