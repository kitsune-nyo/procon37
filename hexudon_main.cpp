
// 第37回 全国高等専門学校 プログラミングコンテスト
// 競技部門「ヘキサうどん」通信クライアント + ビームサーチ (統合版)
//
// 構成:
//   hexudon_main.cpp ... このファイル (通信 + 日ごとの探索呼び出し + main)
//   state.cpp ... 探索 (State / evaluate / separate / update)
//   map.cpp / spot.cpp / agent.cpp ... state.cpp から #include される
//
// ビルド方法:
//   https://github.com/nlohmann/json/releases から json.hpp をDL
//   g++ -std=c++17 -O2 hexudon_main.cpp -lcurl -o hexudon_main
//
// 注意:
//   2. setting の BASE_URL と TEAM_TOKEN は変更

// ========================================================================
// 1. including
// ========================================================================
#pragma region including

#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <thread>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "json.hpp"
#include "state.cpp"

using json = nlohmann::json;

#pragma endregion

// ========================================================================
// 2. setting
// ========================================================================
#pragma region setting

static const std::string BASE_URL     = "http://localhost:8080";
static const std::string TEAM_TOKEN   = "?token=token-p0";
static const std::string SETTING_PATH = "/setting";
static const std::string AGENT_PATH   = "/agent";
static const std::string PROBLEM_PATH = "/";

constexpr int    BEAM_WIDTH        = 3000; // ビーム幅の上限
constexpr double TIME_BUDGET_RATIO = 0.65; // 1日(daySeconds)のうち探索に使ってよい割合

#pragma endregion

// ========================================================================
// 3. protocol
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
// 4. solver
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

// 1日分のビームサーチを実行し、エージェントごとの行動計画を返す
inline std::vector<std::vector<int>> solveDay(
    Map& map, const MatchConfig& config, const DayInfo& info, int steps, double budgetSec, std::set<int>& brands
) {
    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();
    auto elapsed = [&]() { return std::chrono::duration<double>(Clock::now() - t0).count(); };

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

    // キャッシュ
    std::vector<ReverseDijkstraResult> mapDijkstra;
    mapDijkstra.reserve(map.cells.size());
    for (int i = 0; i < (int)map.cells.size(); i++) mapDijkstra.push_back(map.reverseDijkstra(i));
    // dropDijkstra は現在の evaluate / separate では使われていないので空のまま渡す
    std::vector<ReverseDijkstraResult> dropDijkstra;

    // ビームサーチ
    size_t beamWidth = BEAM_WIDTH;
    std::priority_queue<State, std::vector<State>, std::greater<State>> states;
    states.push(State(spotMgr, agentMgr));

    for (int s = 1; s <= steps; s++) {
        double stepStart = elapsed();
        size_t processedStates = 0;

        while (!states.empty() && states.top().step < s) {
            State state = states.top();
            states.pop();
            processedStates++;
            std::vector<State> newStates = separate(state, map, mapDijkstra, dropDijkstra);
            if (update(state, map, steps, brands)) states.push(state);
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
            size_t targetWidth = std::clamp<size_t>(targetSec / secPerState, 1, BEAM_WIDTH);
            beamWidth = std::clamp<size_t>(
                targetWidth,
                std::max<size_t>(1, beamWidth * 8 / 10),
                std::min<size_t>(BEAM_WIDTH, beamWidth * 12 / 10)
            );
        }
    }

    if (states.empty()) return {};

    // ベスト
    while (states.size() > 1) states.pop();
    State best = states.top();

    std::cout << "  Searching result: brand=" << best.udonBrand.size() << " supply=" << best.agentMgr.supplyNum
              << " udon=" << best.udonSum
              << " score=" << best.score
              << " (searching " << elapsed() << " seconds, last beam width " << beamWidth << ")" << std::endl;

    std::vector<std::vector<int>> plans;
    for (Agent& agent : best.agentMgr.agents) plans.push_back(compress(agent.history));
    return plans;
}

inline std::vector<int> agentsKind(const MatchConfig& config) {
    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();
    auto elapsed = [&]() { return std::chrono::duration<double>(Clock::now() - t0).count(); };

    int numSupply = config.agents.size() / 2;

    int steps = config.daySteps[0];
    std::time_t ds = config.startsAt - std::time(nullptr);
    double budgetSec = ds * TIME_BUDGET_RATIO;

    std::cout << "Started searching agents types (steps=" << steps
                << ", " << budgetSec << " seconds)" << std::endl;

    DayInfo tmp;
    std::set<int> brands;
    Map map = buildMap(config, tmp);

    SpotManager spotMgr;
    for (const SpotConfig& s : config.spots) spotMgr.placeSpot(s.brand, s.pos, s.stocks);

    std::vector<ReverseDijkstraResult> mapDijkstra;
    mapDijkstra.reserve(map.cells.size());
    for (int i = 0; i < (int)map.cells.size(); i++) mapDijkstra.push_back(map.reverseDijkstra(i));
    // dropDijkstra は現在の evaluate / separate では使われていないので空のまま渡す
    std::vector<ReverseDijkstraResult> dropDijkstra;

    AgentManager agentMgr;
    agentMgr.fuel = config.fuelLimits;
    for (size_t i = 0; i < config.agents.size(); ++i) agentMgr.placeAgent(config.agents[i], config.fuelLimits);

    size_t beamWidth = BEAM_WIDTH;
    std::priority_queue<State, std::vector<State>, std::greater<State>> states;
    for (int i = config.agents.size() / 3; i <= config.agents.size() / 2; i++) {
        for (int j = 0; j < (1 << config.agents.size()); j++) {
            std::vector<int> k;
            int agt = 0;
            int num = j;
            int check = (1 << (config.agents.size() - 1));
            while (num != 0) {
                if (num - check >= 0) {
                    num -= check;
                    k.push_back(1);
                    agt++;
                } else k.push_back(0);
                check /= 2;
            }
            if (agt == i) {
                for (int l = 0; l < k.size(); l++) {
                    if (k[l] == 1) agentMgr.decideSupply(l);
                }
                states.push(State(spotMgr, agentMgr));
            }
        }
    }

    for (int s = 1; s <= steps; s++) {
        double stepStart = elapsed();
        size_t startSize = states.size();

        while (!states.empty() && states.top().step < s) {
            State state = states.top();
            states.pop();
            std::vector<State> newStates = separate(state, map, mapDijkstra, dropDijkstra);
            if (update(state, map, steps, brands)) states.push(state);
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

    if (states.empty()) return {};

    while (states.size() > 1) states.pop();
    State best = states.top();

    std::cout << "  Searching result: brand=" << best.udonBrand.size() << " supply=" << best.agentMgr.supplyNum
              << " udon=" << best.udonSum
              << " score=" << best.score
              << " (searching " << elapsed() << " seconds, last beam width " << beamWidth << ")" << std::endl;

    std::vector<int> ret;
    for (auto& i : best.agentMgr.agents) ret.push_back(i.kind == AgentKind::SUPPLY ? 1 : 0);
    return ret;
}

#pragma endregion

// ========================================================================
// 5. validator
// ========================================================================
#pragma region validator

struct ValidationResult {
    bool valid;
    std::string errorMessage;
};

inline ValidationResult validateAgentPlan(Map& map, const AgentState& agent,
                                          const std::vector<int>& plan, int totalSteps) {
    int pos = agent.pos;
    int stepsUsed = 0;

    for (int action : plan) {
        if (action < 0) {
            stepsUsed += -action;
        } else if (action <= 5) {
            int npos = map.getNeighbor(pos, static_cast<Direction>(action));
            if (npos == -1) return {false, "Being assigned moving out of the map"};
            if (map.terrainAt(npos) == Terrain::POND) return {false, "Being assigned moving the pond"};
            if (map.terrainAt(pos) == Terrain::POND) return {false, "Being assigned moving from the pond"};

            stepsUsed += map.getMoveCost(pos).time;
            pos = npos;
        } else {
            return {false, "Action value is illigal"};
        }

        if (stepsUsed > totalSteps) return {false, "Step count is over the maximum of the day"};
    }

    if (stepsUsed != totalSteps) return {false, "Step count is not equal the step count of the day"};
    return {true, ""};
}

inline ValidationResult validatePlans(Map& map, const std::vector<AgentState>& agents,
                                      const std::vector<std::vector<int>>& plans, int totalSteps) {
    if (plans.size() != agents.size())
        return {false, "Agents sum is not equal to actions array length"};

    for (size_t i = 0; i < plans.size(); ++i) {
        auto result = validateAgentPlan(map, agents[i], plans[i], totalSteps);
        if (!result.valid)
            return {false, "Agent " + std::to_string(i) + ": " + result.errorMessage};
    }
    return {true, ""};
}

#pragma endregion

// ========================================================================
// 6. http
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

std::string post_json(const json& body_json, const std::string& url, long timeout_sec = 15) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    std::string resp;
    std::string body = body_json.dump();

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
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

std::string post_agent_types(const std::vector<int>& types, long timeout_sec = 15) {
    return post_json(json(types), BASE_URL + AGENT_PATH + TEAM_TOKEN, timeout_sec);
}

std::string post_actions(const std::vector<std::vector<int>>& plans, long timeout_sec = 15) {
    return post_json(json(plans), BASE_URL + PROBLEM_PATH + TEAM_TOKEN, timeout_sec);
}

#pragma endregion

// ========================================================================
// 7. main
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
        post_agent_types(agentTypes);

        std::set<int> brands;
        int numDays = (int)config.daySteps.size();

        for (int day = 0; day < numDays; ++day) {
            DayInfo info = getDayInfo(day);

            int totalSteps = config.daySteps[day];
            double daySec = config.daySeconds[day];
            double budgetSec = daySec * TIME_BUDGET_RATIO;

            std::cout << "Day " << day << " started searching (steps=" << totalSteps
                      << ", " << budgetSec << " seconds)" << std::endl;

            Map map = buildMap(config, info);
            std::vector<std::vector<int>> plans = solveDay(map, config, info, totalSteps, budgetSec, brands);

            ValidationResult validation = validatePlans(map, info.agents, plans, totalSteps);
            if (!validation.valid) {
                std::cerr << "The action array is so illigal that all agents actions changed waiting as the fallback: "
                          << validation.errorMessage << std::endl;
                plans.clear();
                for (size_t i = 0; i < info.agents.size(); ++i) { plans.push_back({ -totalSteps }); }
            }

            post_actions(plans);
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

#pragma endregion