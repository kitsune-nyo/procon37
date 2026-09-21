#include <stdexcept>
#include <queue>
#include <optional>

enum class Terrain : int {
    PLAIN    = 0,
    ROAD     = 1,
    MOUNTAIN = 2,
    POND     = 3
};

enum class RoadStatus {
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

    void init(int w, int h, std::vector<std::vector<int>> grid)
    {
        width = w;
        height = h;
        for (int i = 0; i < w * h; i++) cells.push_back(Terrain::PLAIN);
        for (int i = 0; i < w * h; i++) roadStat.push_back(RoadStatus::SMOOTH);
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