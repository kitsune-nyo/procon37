// ============================================================
// 第37回 全国高等専門学校 プログラミングコンテスト
// 競技部門「ヘキサうどん」通信クライアント + ビームサーチ (統合版)
//
// 構成:
//   hexudon_main.cpp  ... このファイル (通信 + 日ごとの探索呼び出し + main)
//   state.cpp         ... 探索 (State / evaluate / separate / update)
//   map.cpp / spot.cpp / agent.cpp ... state.cpp から #include される
//
// ビルド方法:
//   g++ -std=c++17 -O2 hexudon_main.cpp -lcurl -o hexudon_main
//   ※ state.cpp などは #include で取り込むので単体でコンパイルしないこと
//   ※ nlohmann/json のシングルヘッダ "json.hpp" を同じフォルダに置くこと
//     https://github.com/nlohmann/json/releases から json.hpp をDL
// ============================================================
#pragma region including
// ※ state.cpp が #define WAIT / BIG などを定義するので、
//    外部ライブラリのincludeは必ずその前に書くこと

#include <iostream>
#include <curl/curl.h>

#include "json.hpp"
#include "state.cpp"

using json = nlohmann::json;
#pragma endregion

// ============================================================
// 設定値
// TODO: BASE_URL / TEAM_TOKEN は公式サイト公開後に実値へ差し替え
// ============================================================
#pragma region setting
static const std::string BASE_URL   = "http://localhost:8080";
// static const std::string TEAM_TOKEN = "token-p0";
static const std::string TEAM_TOKEN = "?token=token-p0";

constexpr int    BEAM_WIDTH        = 2000;  // ビーム幅の上限
constexpr double TIME_BUDGET_RATIO = 0.75;   // 1日(daySeconds)のうち探索に使ってよい割合
#pragma endregion

// ============================================================
// 1. protocol : サーバーとのJSONフォーマット変換
// ============================================================
#pragma region protocol
struct SpotConfig {
    int brand;
    int pos;
    int stocks;
};

struct MapData {
    int height;
    int width;
    std::vector<std::vector<int>> cells;
};

struct MatchConfig {
    long long startsAt;
    std::vector<int> daySeconds;
    std::vector<int> daySteps;
    MapData map;
    std::vector<SpotConfig> spots;
    std::vector<int> agents;
    int fuelLimits;
    int players;
    int busyThreshold;
    int jammedThreshold;
};

struct AgentState {
    int kind;   // 0: パトロール, 1: 補給
    int pos;
    int fuel;
};

struct OtherTeam {
    int id;
    std::vector<AgentState> agents;
};

struct TrafficInfo {
    int pos;
    int status;
};

struct DayInfo {
    long long endsAt;
    int day;
    std::vector<AgentState> agents;
    std::vector<OtherTeam> others;
    std::vector<TrafficInfo> traffics;
};

inline MatchConfig parseMatchConfig(const json& j) {
    MatchConfig c;
    // 公式サンプルは "startAt"。念のため "startsAt" も受け付ける (使っていない項目なので無くても可)
    c.startsAt = j.value("startAt", j.value("startsAt", 0LL));
    c.daySeconds = j.at("daySeconds").get<std::vector<int>>();
    c.daySteps = j.at("daySteps").get<std::vector<int>>();

    const auto& mapJson = j.at("map");
    c.map.height = mapJson.at("height").get<int>();
    c.map.width = mapJson.at("width").get<int>();
    c.map.cells = mapJson.at("cells").get<std::vector<std::vector<int>>>();

    for (const auto& s : j.at("spots")) {
        SpotConfig spot;
        spot.brand = s.at("brand").get<int>();
        spot.pos = s.at("pos").get<int>();
        spot.stocks = s.at("stocks").get<int>();
        c.spots.push_back(spot);
    }

    c.agents = j.at("agents").get<std::vector<int>>();
    c.fuelLimits = j.at("fuelLimits").get<int>();
    c.players = j.at("players").get<int>();
    c.busyThreshold = j.at("busyThreshold").get<int>();
    c.jammedThreshold = j.at("jammedThreshold").get<int>();
    return c;
}

inline AgentState parseAgentState(const json& j) {
    AgentState a;
    a.kind = j.at("kind").get<int>();
    a.pos = j.at("pos").get<int>();
    a.fuel = j.at("fuel").get<int>();
    return a;
}

inline DayInfo parseDayInfo(const json& j) {
    DayInfo d;
    d.endsAt = j.at("endsAt").get<long long>();
    d.day = j.at("day").get<int>();

    for (const auto& a : j.at("agents")) d.agents.push_back(parseAgentState(a));

    for (const auto& o : j.at("others")) {
        OtherTeam team;
        team.id = o.at("id").get<int>();
        for (const auto& a : o.at("agents")) team.agents.push_back(parseAgentState(a));
        d.others.push_back(team);
    }

    for (const auto& t : j.at("traffics")) {
        TrafficInfo info;
        info.pos = t.at("pos").get<int>();
        info.status = t.at("status").get<int>();
        d.traffics.push_back(info);
    }

    return d;
}

inline json serializeAgentTypes(const std::vector<int>& types) { return json(types); }
inline json serializeActionPlan(const std::vector<std::vector<int>>& plan) { return json(plan); }
#pragma endregion

// ============================================================
// 2. solver : サーバーの情報 -> アルゴリズムの入力 -> 行動計画
// ============================================================
#pragma region solver
// historyを「方向 or まとめた待機(負の値)」の列に変換する (サーバーへ送る形式)
inline std::vector<int> compress(const std::vector<int>& v) {
    std::vector<int> ret;
    int sum = 0;
    for (int e : v) {
        if (e == INACTION) continue;   // 移動中のステップは数えない
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
    Map& map, const MatchConfig& config, const DayInfo& info, int steps, double budgetSec
) {
    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();
    auto elapsed = [&]() { return std::chrono::duration<double>(Clock::now() - t0).count(); };

    // ---- スポット: うどんの在庫は毎日リセットされる ----
    SpotManager spotMgr;
    for (const auto& s : config.spots) spotMgr.placeSpot(s.brand, s.pos, s.stocks);

    // ---- エージェント: 位置・燃料・種別はサーバーの値を使う ----
    AgentManager agentMgr;
    agentMgr.fuel = config.fuelLimits;
    for (size_t i = 0; i < info.agents.size(); ++i) {
        agentMgr.placeAgent(info.agents[i].pos, info.agents[i].fuel);
        if (info.agents[i].kind == 1) agentMgr.decideSupply(static_cast<int>(i));
    }

    // ---- キャッシュ (渋滞状況に依存するので毎日計算) ----
    std::vector<ReverseDijkstraResult> mapDijkstra;
    mapDijkstra.reserve(map.cells.size());
    for (int i = 0; i < (int)map.cells.size(); i++) mapDijkstra.push_back(map.reverseDijkstra(i));
    // dropDijkstra は現在の evaluate / separate では使われていないので空のまま渡す
    std::vector<ReverseDijkstraResult> dropDijkstra;

    // ---- ビームサーチ ----
    size_t beamWidth = BEAM_WIDTH;
    std::priority_queue<State, std::vector<State>, std::greater<State>> states;
    states.push(State(spotMgr, agentMgr));

    for (int s = 1; s <= steps; s++) {
        double stepStart = elapsed();
        size_t startSize = states.size();

        while (!states.empty() && states.top().step < s) {
            State state = states.top();
            states.pop();
            std::vector<State> newStates = separate(state, map, mapDijkstra, dropDijkstra);
            if (update(state, map, steps)) states.push(state);
            for (State& next : newStates) {
                evaluate(next, map, mapDijkstra, dropDijkstra);
                states.push(next);
            }
        }
        while (states.size() > beamWidth) states.pop();
        if (states.empty()) break;

        // 残り時間から1ステップあたりに使える時間を割り出し、ビーム幅を増減する
        // (処理した状態数あたりの所要時間は大きく変わらない、という近似)
        double now = elapsed();
        double stepSec = std::max(now - stepStart, 1e-6);
        int remainingSteps = steps - s;
        if (remainingSteps > 0) {
            double target = std::max(0.0, budgetSec - now) / remainingSteps;
            double ratio = std::min(4.0, target / stepSec * 0.9);
            size_t w = static_cast<size_t>(std::max<double>(1.0, startSize * ratio));
            beamWidth = std::min<size_t>(BEAM_WIDTH, std::max<size_t>(1, w));
            while (states.size() > beamWidth) states.pop();
        }
    }

    if (states.empty()) return {};

    // 一番スコアの高い状態だけ残す
    while (states.size() > 1) states.pop();
    State best = states.top();

    std::cout << "  Searching result: brand=" << best.udonBrand.size()
              << " udon=" << best.udonSum
              << " score=" << best.score
              << " (searching " << elapsed() << " seconds, last beam width " << beamWidth << ")" << std::endl;

    std::vector<std::vector<int>> plans;
    for (Agent& agent : best.agentMgr.agents) plans.push_back(compress(agent.history));
    return plans;
}
#pragma endregion

// ============================================================
// 3. validator : 行動計画の事前検証
//   構造のみ検証する (範囲外・池・ステップ合計)。
//   補給による燃料回復は探索側が考慮済みなので、ここでは燃料残量は見ない。
// ============================================================
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

// ============================================================
// 4. http_client : libcurlを使ったHTTP通信
// ============================================================
#pragma region http_client
struct HttpResponse {
    long statusCode = 0;
    std::string body;
    bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

class HttpClient {
public:
    HttpClient() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~HttpClient() { curl_global_cleanup(); }

    HttpResponse get(const std::string& url,
                      const std::map<std::string, std::string>& headers = {}) {
        HttpResponse res;
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("curl_easy_init failed");

        struct curl_slist* headerList = nullptr;
        for (const auto& [k, v] : headers) {
            std::string h = k + ": " + v;
            headerList = curl_slist_append(headerList, h.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &HttpClient::writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec_);
        if (headerList) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

        CURLcode code = curl_easy_perform(curl);
        if (code != CURLE_OK) {
            curl_slist_free_all(headerList);
            curl_easy_cleanup(curl);
            throw std::runtime_error(std::string("curl GET failed: ") + curl_easy_strerror(code));
        }

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        res.statusCode = httpCode;

        curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);
        return res;
    }

    HttpResponse postJson(const std::string& url,
                          const std::string& jsonBody,
                          const std::map<std::string, std::string>& headers = {}) {
        HttpResponse res;
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("curl_easy_init failed");

        struct curl_slist* headerList = nullptr;
        headerList = curl_slist_append(headerList, "Content-Type: application/json");
        for (const auto& [k, v] : headers) {
            std::string h = k + ": " + v;
            headerList = curl_slist_append(headerList, h.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)jsonBody.size());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &HttpClient::writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec_);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

        CURLcode code = curl_easy_perform(curl);
        if (code != CURLE_OK) {
            curl_slist_free_all(headerList);
            curl_easy_cleanup(curl);
            throw std::runtime_error(std::string("curl POST failed: ") + curl_easy_strerror(code));
        }

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        res.statusCode = httpCode;

        curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);
        return res;
    }

    void setTimeout(long seconds) { timeoutSec_ = seconds; }

private:
    long timeoutSec_ = 10;

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t totalSize = size * nmemb;
        std::string* buf = static_cast<std::string*>(userp);
        buf->append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }
};
#pragma endregion

// ============================================================
// 5. main : 実行エントリーポイント
// ============================================================
int main() {
    try {
        HttpClient http;
        http.setTimeout(10);

        // std::map<std::string, std::string> headers = {
        //     {"Authorization", "Bearer " + TEAM_TOKEN}
        // };

        // ---- 試合設定の取得 ----
        // HttpResponse configRes = http.get(BASE_URL + "/setting", headers);
        HttpResponse configRes = http.get(BASE_URL + "/setting" + TEAM_TOKEN);
        if (!configRes.ok()) {
            std::cerr << "Failed getting the map structure: " << configRes.statusCode
                      << "\n" << configRes.body << std::endl;
            return 1;
        }
        MatchConfig config = parseMatchConfig(json::parse(configRes.body));
        int numDays = static_cast<int>(config.daySteps.size());

        // ---- エージェントの役割決定 (先頭から補給役。1台しかいないときは全員パトロール) ----
        int n = config.agents.size();
        int numSupply = (n >= 2) ? std::max(1, n / 4) : 0;
        std::vector<int> agentTypes(n, 0);
        for (int i = 0; i < numSupply; ++i) agentTypes[i] = 1;

        // HttpResponse typeRes = http.postJson(BASE_URL + "/agent",
        //                                      serializeAgentTypes(agentTypes).dump(),
        //                                      headers);
        HttpResponse typeRes = http.postJson(BASE_URL + "/agent" +TEAM_TOKEN,
                                             serializeAgentTypes(agentTypes).dump());
        if (!typeRes.ok()) {
            std::cerr << "Failed to send agent type: " << typeRes.statusCode << "\n" << typeRes.body << std::endl;
        }
        while (!http.get(BASE_URL + TEAM_TOKEN).ok()) {}

        int error_day = -1;
        for (int day = 0; day < numDays; ++day) {
            // HttpResponse dayRes = http.get(BASE_URL + "/", headers);
            HttpResponse dayRes = http.get(BASE_URL + TEAM_TOKEN);
            if (!dayRes.ok()) {
                if (error_day != day) {
                    std::cerr << "Day " << day << " failed getting contents: " << dayRes.statusCode << std::endl;
                    error_day = day;
                }
                day--;
                continue;
            }
            DayInfo info = parseDayInfo(json::parse(dayRes.body));
            if (info.day < day) {
                day--;
                continue;
            }

            // サーバーが返した day を優先 (範囲外ならループ変数で代用)
            int d = (info.day >= 0 && info.day < numDays) ? info.day : day;
            int totalSteps = config.daySteps[d];
            double daySec = (d < (int)config.daySeconds.size()) ? config.daySeconds[d] : 10;
            double budgetSec = daySec * TIME_BUDGET_RATIO;

            std::cout << "Day " << d << " started searching (steps=" << totalSteps
                      << ", " << budgetSec << " seconds)" << std::endl;

            Map map = buildMap(config, info);
            auto plans = solveDay(map, config, info, totalSteps, budgetSec);

            auto validation = validatePlans(map, info.agents, plans, totalSteps);
            if (!validation.valid) {
                std::cerr << "The action array is so illigal that all agents actions changed waiting as the fallback: "
                          << validation.errorMessage << std::endl;
                plans.clear();
                for (size_t i = 0; i < info.agents.size(); ++i) { plans.push_back({ -totalSteps }); }
            }

            // HttpResponse planRes = http.postJson(BASE_URL + "/",
            //                                       serializeActionPlan(plans).dump(),
            //                                       headers);
            HttpResponse planRes = http.postJson(BASE_URL + TEAM_TOKEN, serializeActionPlan(plans).dump());
            if (!planRes.ok()) {
                std::cerr << "Day " << day << " failed sending actions array: " << planRes.statusCode
                          << "\n" << planRes.body << std::endl;
            } else {
                std::cout << "Day " << day << " sent actions array successfully" << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
