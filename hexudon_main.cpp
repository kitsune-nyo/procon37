
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
//
// 注意事項:
//   2. setting の BASE_URL と TEAM_TOKEN は変更

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
#include <curl/curl.h>

#include "json.hpp"
#include "state.cpp"

using json = nlohmann::json;

#pragma endregion

// ========================================================================
// 1. setting
// ========================================================================
#pragma region setting

static const std::string BASE_URL     = "http://localhost:8080";
static const std::string TEAM_TOKEN   = "?token=token-p0";
static const std::string SETTING_PATH = "/setting";
static const std::string AGENT_PATH   = "/agent";
static const std::string PROBLEM_PATH = "/";

constexpr int    BEAM_WIDTH        = 5000; // ビーム幅の上限
constexpr double TIME_BUDGET_RATIO = 0.75; // 1日(daySeconds)のうち探索に使ってよい割合
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
    if (endsAt == 0) return 15000;
    return (long)std::clamp(remainingSeconds(endsAt) * 1000.0, 1.0, 15000.0);
}

// 中断した候補は、開始済みの移動だけを完了させ、残りを待機で埋める。
inline std::vector<std::vector<int>> finishPlans(
    State state, Map& map, int steps, std::set<int>* acquiredBrands
) {
    for (Agent& agent : state.agentMgr.agents) {
        while (!agent.actions.empty()) agent.actions.pop();
    }
    while (state.step < steps) {
        for (Agent& agent : state.agentMgr.agents) {
            if (agent.nextPos == -1) agent.actions.push(WAIT);
        }
        update(state, map, steps);
    }
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

    // キャッシュ
    std::vector<ReverseDijkstraResult> mapDijkstra;
    mapDijkstra.reserve(map.cells.size());
    for (int i = 0; i < (int)map.cells.size(); i++) {
        if (Clock::now() >= deadline) return finishPlans(initial, map, steps, acquiredBrands);
        auto result = map.reverseDijkstra(i, deadline);
        if (result.dist.empty()) return finishPlans(initial, map, steps, acquiredBrands);
        mapDijkstra.push_back(std::move(result));
    }
    // dropDijkstra は現在の evaluate / separate では使われていないので空のまま渡す
    std::vector<ReverseDijkstraResult> dropDijkstra;

    // ビームサーチ
    size_t beamWidth = BEAM_WIDTH;
    std::priority_queue<State, std::vector<State>, std::greater<State>> states;
    evaluate(initial, map, mapDijkstra, dropDijkstra, brands);
    states.push(initial);

    for (int s = 1; s <= steps; s++) {
        if (Clock::now() >= deadline) break;
        double stepStart = elapsed();
        size_t processedStates = 0;

        while (!states.empty() && states.top().step < s) {
            if (Clock::now() >= deadline) break;
            State state = states.top();
            states.pop();
            processedStates++;
            std::vector<State> newStates = separate(state, map, mapDijkstra, dropDijkstra, deadline);
            if (update(state, map, steps)) {
                evaluate(state, map, mapDijkstra, dropDijkstra, brands);
                states.push(state);
            }
            for (State& next : newStates) {
                evaluate(next, map, mapDijkstra, dropDijkstra, brands);
                states.push(next);
            }
        }
        while (states.size() > beamWidth) states.pop();
        if (states.empty()) break;

        // 残り時間から1ステップあたりに使える時間を割り出し、ビーム幅を増減する
        // (処理した状態数あたりの所要時間は大きく変わらない、という近似)
        double now = elapsed();
        double secPerState = (processedStates > 0) ? (now - stepStart) / processedStates : 1e-6;
        int remainingSteps = steps - s;
        if (remainingSteps > 0) {
            double targetSec = std::max(0.0, budgetSec - now) / remainingSteps;
            size_t targetWidth = (size_t)std::clamp(targetSec / std::max(secPerState, 1e-6), 1.0, (double)BEAM_WIDTH);
            beamWidth = std::clamp<size_t>(
                targetWidth,
                std::max<size_t>(1, beamWidth * 8 / 10),
                std::min<size_t>(BEAM_WIDTH, beamWidth * 12 / 10)
            );
        }
    }

    if (states.empty()) return finishPlans(initial, map, steps, acquiredBrands);

    // ベスト
    while (states.size() > 1) states.pop();
    State best = states.top();

    std::cout << "  Searching result: brand=" << best.udonBrand.size()
              << " supply=" << best.agentMgr.supplyNum
              << " udon=" << best.udonSum
              << " score=" << best.score
              << " (searching " << elapsed()
              << " seconds, last beam width " << beamWidth << ")" << std::endl;

    return finishPlans(best, map, steps, acquiredBrands);
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
    const std::vector<int> fallback = kinds.front();
    if (Clock::now() >= deadline) return fallback;

    std::cout << "Started searching agents types (steps=" << steps
                << ", " << budgetSec << " seconds)" << std::endl;

    DayInfo tmp;
    std::set<int> brands;
    Map map = buildMap(config, tmp);

    SpotManager spotMgr;
    for (const SpotConfig& s : config.spots) spotMgr.placeSpot(s.brand, s.pos, s.stocks);

    std::vector<ReverseDijkstraResult> mapDijkstra;
    mapDijkstra.reserve(map.cells.size());
    for (int i = 0; i < (int)map.cells.size(); i++) {
        if (Clock::now() >= deadline) return fallback;
        auto result = map.reverseDijkstra(i, deadline);
        if (result.dist.empty()) return fallback;
        mapDijkstra.push_back(std::move(result));
    }
    // dropDijkstra は現在の evaluate / separate では使われていないので空のまま渡す
    std::vector<ReverseDijkstraResult> dropDijkstra;

    AgentManager agentMgr;
    agentMgr.fuel = config.fuelLimits;
    for (size_t i = 0; i < config.agents.size(); ++i) agentMgr.placeAgent(config.agents[i], config.fuelLimits);

    size_t beamWidth = BEAM_WIDTH;
    std::priority_queue<State, std::vector<State>, std::greater<State>> states;
    for (const auto& k : kinds) {
        if (Clock::now() >= deadline) break;
        AgentManager candidateMgr = agentMgr;
        for (int l = 0; l < (int)k.size(); ++l) {
            if (k[l] == 1) candidateMgr.decideSupply(l);
        }
        State initial(spotMgr, candidateMgr);
        evaluate(initial, map, mapDijkstra, dropDijkstra, brands);
        states.push(initial);
    }

    for (int s = 1; s <= steps; s++) {
        if (Clock::now() >= deadline) break;
        double stepStart = elapsed();
        size_t startSize = states.size();

        while (!states.empty() && states.top().step < s) {
            if (Clock::now() >= deadline) break;
            State state = states.top();
            states.pop();
            std::vector<State> newStates = separate(state, map, mapDijkstra, dropDijkstra, deadline);
            if (update(state, map, steps)) {
                evaluate(state, map, mapDijkstra, dropDijkstra, brands);
                states.push(state);
            }
            for (State& next : newStates) {
                evaluate(next, map, mapDijkstra, dropDijkstra, brands);
                states.push(next);
            }
        }
        while (states.size() > beamWidth) states.pop();
        if (states.empty()) break;

        double now = elapsed();
        double stepSec = std::max(now - stepStart, 1e-6);
        int remainingSteps = steps - s;
        if (remainingSteps > 0) {
            double target = std::max(0.0, budgetSec - now) / remainingSteps;
            double ratio = std::min(4.0, target / stepSec);
            size_t w = static_cast<size_t>(std::max<double>(1.0, startSize * ratio));
            beamWidth = std::min<size_t>(BEAM_WIDTH, std::max<size_t>(1, w));
            while (states.size() > beamWidth) states.pop();
        }
    }

    if (states.empty()) return fallback;

    while (states.size() > 1) states.pop();
    State best = states.top();

    std::cout << "  Searching result: brand=" << best.udonBrand.size()
              << " supply=" << best.agentMgr.supplyNum
              << " udon=" << best.udonSum
              << " score=" << best.score
              << " (searching " << elapsed()
              << " seconds, last beam width " << beamWidth << ")" << std::endl;

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

        std::vector<int> agentTypes = agentsKind(config);
        post_agent_types(agentTypes, submissionTimeout(config.startsAt));

        std::set<int> brands;
        int numDays = (int)config.daySteps.size();

        for (int day = 0; day < numDays; ++day) {
            DayInfo info = getDayInfo(day);

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

            post_actions(plans, submissionTimeout(info.endsAt));
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