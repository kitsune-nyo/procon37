#include <bits/stdc++.h>

enum class Terrain : int {
    PLAIN    = 0,
    ROAD     = 1,
    MOUNTAIN = 2,
    POND     = 3
};

enum class RoadStatus : int {
    SMOOTH    = 0,
    CONGESTED = 1,
    JAMMED    = 2
};

class MoveCost {
public:
    int time;
    int fuel;

    MoveCost operator +(const MoveCost& r) const {
        MoveCost ret;
        ret.fuel = fuel + r.fuel;
        ret.time = time + r.time;
        return ret;
    }
};

inline MoveCost getMoveCost(Terrain terrain, RoadStatus road = RoadStatus::SMOOTH) {
    switch (terrain) {
        case Terrain::PLAIN:
            return {2, 1};
        case Terrain::MOUNTAIN:
            return {3, 2};
        case Terrain::ROAD:
            switch (road) {
                case RoadStatus::SMOOTH:    return {1, 2};
                case RoadStatus::CONGESTED: return {2, 2};
                case RoadStatus::JAMMED:    return {4, 2};
            }
            return {1, 2};
        case Terrain::POND:
        default:
            return {0, 0};
    }
}

inline bool isPassable(Terrain t) {
    return t != Terrain::POND;
}

class Map {
public:
    int width;
    int height;
    std::vector<Terrain> cells;
    std::vector<RoadStatus> roadStat;

    Map(int w, int h, const std::vector<std::vector<int>>& grid)
        : width(w), height(h), cells(w * h), roadStat(w * h, RoadStatus::SMOOTH)
    {
        if ((int)grid.size() != h) throw std::runtime_error("マップの行数が height と一致しません");
        for (int r = 0; r < h; ++r) {
            if ((int)grid[r].size() != w) throw std::runtime_error("マップの列数が width と一致しません");
            for (int c = 0; c < w; ++c) {
                int v = grid[r][c];
                if (v < 0 || v > 3) throw std::runtime_error("不正な地形コードです");
                cells[r * w + c] = static_cast<Terrain>(v);
            }
        }
    }

    inline int indexOf(int r, int c) const {
        if (r < 0 || r >= height || c < 0 || c >= width) return -1;
        return r * width + c;
    }
    inline int rowOf(int idx) const { return idx / width; }
    inline int colOf(int idx) const { return idx % width; }

    Terrain terrainAt(int idx) const { return cells[idx]; }
};

enum class Direction : int { NW = 0, NE = 1, E = 2, SE = 3, SW = 4, W = 5 };

inline const int DIR_TABLE[2][6][2] = {
    // 偶数行 (r % 2 == 0)
    {
        {-1, -1}, {-1, 0}, {0, 1}, {1, -1}, {1, 0}, {0, -1},
    },
    // 奇数行 (r % 2 == 1)
    {
        {-1, 0}, {-1, 1}, {0, 1}, {1, 0}, {1, 1}, {0, -1},
    }
};

inline std::optional<int> getNeighbor(const Map& map, int idx, Direction dir) {
    int r = map.rowOf(idx);
    int c = map.colOf(idx);
    int parity = r % 2;
    int dr = DIR_TABLE[parity][(int)dir][0];
    int dc = DIR_TABLE[parity][(int)dir][1];
    int nidx = map.indexOf(r + dr, c + dc);
    if (nidx < 0) return std::nullopt;
    return nidx;
}

inline std::vector<int> getAllNeighbors(const Map& map, int idx) {
    std::vector<int> result;
    for (int d = 0; d < 6; ++d) {
        auto n = getNeighbor(map, idx, static_cast<Direction>(d));
        if (n.has_value()) result.push_back(n.value());
    }
    return result;
}

inline bool checkMapConnectivity(const Map& map) {
    int total = map.width * map.height;
    std::vector<bool> visited(total, false);
    int start = -1;
    for (int i = 0; i < total; ++i) {
        if (isPassable(map.terrainAt(i))) { start = i; break; }
    }
    if (start == -1) return false;

    std::queue<int> q;
    q.push(start);
    visited[start] = true;
    int visitedCount = 1;

    while (!q.empty()) {
        int cur = q.front(); q.pop();
        for (int nb : getAllNeighbors(map, cur)) {
            if (!isPassable(map.terrainAt(nb))) continue;
            if (visited[nb]) continue;
            visited[nb] = true;
            visitedCount++;
            q.push(nb);
        }
    }

    int passableTotal = 0;
    for (int i = 0; i < total; ++i) if (isPassable(map.terrainAt(i))) passableTotal++;

    return visitedCount == passableTotal;
}

enum class MoveResult { OK, OUT_OF_RANGE, BLOCKED_POND, NOT_ENOUGH_FUEL, NOT_ENOUGH_STEP };

struct MoveAttempt {
    MoveResult result;
    int nextPos = -1;
    MoveCost cost{0, 0};
};

inline MoveAttempt tryMove(const Map& map, int currentPos, Direction dir,
                           int availableFuel, int availableSteps, bool isPatrolCar) {
    MoveAttempt attempt;

    auto next = getNeighbor(map, currentPos, dir);
    if (!next.has_value()) {
        attempt.result = MoveResult::OUT_OF_RANGE;
        return attempt;
    }
    Terrain nextTerrain = map.terrainAt(next.value());
    if (!isPassable(nextTerrain)) {
        attempt.result = MoveResult::BLOCKED_POND;
        return attempt;
    }

    Terrain curTerrain = map.terrainAt(currentPos);
    RoadStatus curRoad = map.roadStat[currentPos];
    MoveCost cost = getMoveCost(curTerrain, curRoad);

    if (isPatrolCar && availableFuel < cost.fuel) {
        attempt.result = MoveResult::NOT_ENOUGH_FUEL;
        return attempt;
    }
    if (availableSteps < cost.time) {
        attempt.result = MoveResult::NOT_ENOUGH_STEP;
        return attempt;
    }

    attempt.result = MoveResult::OK;
    attempt.nextPos = next.value();
    attempt.cost = cost;
    return attempt;
}

struct Spot {
    int pos;
    int brand;
    int maxStock;
    int stock;
};

class SpotManager {
public:
    std::vector<Spot> spots;

    void placeSpotsRandomly(const Map& map, int spotCount, int brandCount,
                     int agentCount, int maxStockCap, unsigned seed = 42) {
        if (spotCount < agentCount)
            throw std::runtime_error("スポット数はエージェント数以上である必要があります");
        if (spotCount > map.width * map.height)
            throw std::runtime_error("スポット数がマップサイズを超えています");
        if (brandCount < 1 || brandCount > spotCount)
            throw std::runtime_error("系列数は1以上スポット数以下である必要があります");

        std::vector<int> plainCells;
        for (int i = 0; i < map.width * map.height; ++i) {
            if (map.terrainAt(i) == Terrain::PLAIN) plainCells.push_back(i);
        }
        if ((int)plainCells.size() < spotCount)
            throw std::runtime_error("平地セルが不足しています");

        std::mt19937 rng(seed);
        std::shuffle(plainCells.begin(), plainCells.end(), rng);

        spots.clear();
        std::uniform_int_distribution<int> brandDist(0, brandCount - 1);
        std::uniform_int_distribution<int> stockDist(1, maxStockCap);

        for (int i = 0; i < spotCount; ++i) {
            Spot sp;
            sp.pos = plainCells[i];
            sp.brand = brandDist(rng);
            sp.maxStock = stockDist(rng);
            sp.stock = sp.maxStock;
            spots.push_back(sp);
        }
    }

    Spot* findAt(int pos) {
        for (auto& s : spots) if (s.pos == pos) return &s;
        return nullptr;
    }

    bool consume(int pos) {
        Spot* s = findAt(pos);
        if (!s) return false;
        if (s->stock <= 0) return false;
        s->stock--;
        return true;
    }

    void refillAll() {
        for (auto& s : spots) s.stock = s.maxStock;
    }
};

enum class AgentKind {
    PATROL, SUPPLY
};

struct Agent {
    int pos, fuel;
    long long id;
    AgentKind kind;
    bool isPatrol() { return kind == AgentKind::PATROL; }
    void consumeFuel(int f) {
        if (fuel < f) std::cerr << "燃料が足りません" << std::endl;
        fuel -= f;
    }
};

class AgentManager {
public:
    std::vector<Agent> agents;
    int count, fuel;

    void placeAgentsRandomly(Map& map, SpotManager spotMgr, int cnt, int f, unsigned seed = 42) {
        std::vector<int> plainCells;
        for (int i = 0; i < map.width * map.height; ++i) {
            if (map.terrainAt(i) == Terrain::PLAIN) plainCells.push_back(i);
        }
        if ((int)plainCells.size() < cnt) throw std::runtime_error("平地セルが不足しています");

        std::mt19937 rng(seed);
        std::shuffle(plainCells.begin(), plainCells.end(), rng);

        agents.clear();
        std::uniform_int_distribution<long long> idDist(1, LLONG_MAX);

        count = cnt, fuel = f;
        for (int i = 0; i < cnt; ++i) {
            Agent agent;
            agent.pos = plainCells[i];
            agent.fuel = f;
            agent.id = idDist(rng);
            agent.kind = AgentKind::PATROL;
            agents.push_back(agent);
        }
    }

    void assignKinds(std::vector<AgentKind> kinds) {
        for (int a = 0; a < kinds.size(); a++) {
            if (a >= count) throw std::runtime_error("エージェントに対するクエリが多すぎます");
            else agents[a].kind = kinds[a];
        }
    }

    void tryCollectUdon(Agent& agent, SpotManager& spotMgr) {
        if (!spotMgr.consume(agent.pos)) std::cerr << "ストックがないか、スポットのマスにいません" << std::endl;
    }

    void applySupply() {
        for (int s = 0; s < count; s++) {
            if (agents[s].isPatrol()) continue;
            for (int p = 0; p < count; p++) {
                if (!agents[p].isPatrol()) continue;
                if (agents[s].pos == agents[p].pos) agents[p].fuel = fuel;
            }
        }
    }
};

struct ReverseDijkstraResult {
    std::vector<int> dist;
    std::vector<int> parent;
};

ReverseDijkstraResult reverseDijkstra(Map& map, int goal) {
    std::vector<int> dist(map.width * map.height, 9999999);
    std::vector<int> parent(map.width * map.height, -1);
    
    using pint = std::pair<int, int>;
    std::priority_queue<pint, std::vector<pint>, std::greater<pint>> search;

    dist[goal] = 0;
    search.push({0, goal});

    while (!search.empty()) {
        int d = search.top().first, p = search.top().second;
        search.pop();

        if (d != dist[p]) continue;
        std::vector move = getAllNeighbors(map, p);

        for (int m = 0; m < move.size(); m++) {
            int next = move[m];
            if (map.terrainAt(next) == Terrain::POND) continue;
            int time = getMoveCost(map.terrainAt(next), map.roadStat[next]).time;
            int newDist = d + time;
            if (newDist < dist[next]) {
                dist[next] = newDist;
                parent[next] = p;
                search.push({newDist, next});
            }
        }
    }

    return {dist, parent};
}

struct State {
    std::vector<Spot> spots;
    std::vector<Agent> agents;
    int step, score;
};

int main() {
    // ===MAP===
    std::vector<std::vector<int>> sampleMapData = {
        {0, 0, 2, 0, 0, 0, 0, 0},
        {0, 3, 3, 0, 2, 2, 0, 0},
        {0, 0, 3, 0, 0, 2, 0, 1},
        {1, 1, 1, 1, 1, 1, 1, 0},
        {0, 0, 2, 0, 0, 3, 0, 0},
        {0, 2, 0, 0, 0, 3, 3, 0},
        {0, 0, 0, 1, 1, 1, 1, 0},
        {0, 0, 0, 0, 0, 0, 0, 0},
    };
    Map map(8, 8, sampleMapData);
    if (!checkMapConnectivity(map)) std::cerr << "[NG] マップが連結していません\n";
    else std::cout << "[OK] マップは全て連結しています\n";

    // ===SPOTS===
    SpotManager spotMgr;
    int agentCount = 4;
    int fuelLimit = 20;
    spotMgr.placeSpotsRandomly(map, 6, 3, agentCount, agentCount);
    std::cout << "[OK] スポット配置数: " << spotMgr.spots.size() << "\n";
    for (auto& s : spotMgr.spots) {
        std::cout << "  spot pos=" << s.pos << " brand=" << s.brand
                  << " stock=" << s.stock << "/" << s.maxStock << "\n";
    }

    // ===AGENTS===
    AgentManager agentMgr;
    agentMgr.placeAgentsRandomly(map, spotMgr, agentCount, fuelLimit);
    agentMgr.assignKinds({AgentKind::PATROL, AgentKind::SUPPLY,
                          AgentKind::PATROL, AgentKind::PATROL});
    std::cout << "[OK] エージェント配置完了\n";
    for (auto& a : agentMgr.agents) {
        std::cout << "  agent id=" << a.id << " pos=" << a.pos
                  << " kind=" << (a.isPatrol() ? "PATROL" : "SUPPLY")
                  << " fuel=" << a.fuel << "\n";
    }

/*
    // ===SAMPLE====
    #pragma region 
    Agent& car = agentMgr.agents[0];
    Direction dir = Direction::E;
    auto attempt = tryMove(map, car.pos, dir, car.fuel, 10, car.isPatrol());

    switch (attempt.result) {
        case MoveResult::OK:
            car.consumeFuel(attempt.cost.fuel);
            car.pos = attempt.nextPos;
            std::cout << "[OK] 移動成功 → pos=" << car.pos
                      << " 残り燃料=" << car.fuel
                      << " (time=" << attempt.cost.time << ")\n";
            agentMgr.tryCollectUdon(car, spotMgr);
            break;
        case MoveResult::OUT_OF_RANGE:
            std::cerr << "[NG] 範囲外への移動です\n"; break;
        case MoveResult::BLOCKED_POND:
            std::cerr << "[NG] 池には進入できません\n"; break;
        case MoveResult::NOT_ENOUGH_FUEL:
            std::cerr << "[NG] 燃料不足です\n"; break;
        case MoveResult::NOT_ENOUGH_STEP:
            std::cerr << "[NG] ステップ数が不足しています\n"; break;
    }

    agentMgr.agents[1].pos = car.pos;
    agentMgr.applySupply();
    std::cout << "[OK] 補給後の巡回車燃料: " << car.fuel << "\n";
    #pragma endregion
*/

    // ===SEARCH===
    ReverseDijkstraResult c = reverseDijkstra(map, 0);
    for (int i = 0; i < map.height; i++) {
        for (int j = 0; j < map.width; j++) {
            if (j > 0) std::cout << " ";
            std::cout << c.dist[map.indexOf(i, j)];
        }
        std::cout << std::endl;
    }

    return 0;
}
