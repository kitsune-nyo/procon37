#include <stdexcept>
#include <climits>
#include <vector>
#include <queue>

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
    int time, fuel;
    bool operator!=(const MoveCost& r) const { return !((time == r.time) && (fuel == r.fuel)); }
    bool operator>(const MoveCost& r) const {
        if (time != r.time) return time > r.time;
        return fuel > r.fuel;
    }
    bool operator<(const MoveCost& r) const {
        if (time != r.time) return time < r.time;
        return fuel < r.fuel;
    }
    MoveCost operator+(const MoveCost& r) const { return {time + r.time, fuel + r.fuel}; }
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

enum class Direction : int { NW = 0, NE = 1, E = 2, SE = 3, SW = 4, W = 5 };

inline const int DIR_TABLE[2][6][2] = {
    // 偶数行 (r % 2 == 0)
    { {-1, -1}, {-1, 0}, {0, 1}, {1, -1}, {1, 0}, {0, -1}, },
    // 奇数行 (r % 2 == 1)
    { {-1, 0}, {-1, 1}, {0, 1}, {1, 0}, {1, 1}, {0, -1}, }
};

class Map {
public:
    int width;
    int height;
    std::vector<Terrain> cells;
    std::vector<RoadStatus> roadStat;

    void init(int w, int h, const std::vector<std::vector<int>>& grid)
    {
        width = w;
        height = h;
        cells.assign(w * h, Terrain::PLAIN);
        roadStat.assign(w * h, RoadStatus::SMOOTH);
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

    void setRoadStatus(int pos, RoadStatus road) { roadStat[pos] = road; }

    inline int indexOf(int r, int c) const {
        if (r < 0 || r >= height || c < 0 || c >= width) return -1;
        return r * width + c;
    }
    inline int rowOf(int idx) const { return idx / width; }
    inline int colOf(int idx) const { return idx % width; }

    Terrain terrainAt(int idx) const { return cells[idx]; }
    RoadStatus roadStatAt(int idx) const { return roadStat[idx]; }

    int getDirection(int pos, int next) {
        int r = rowOf(pos);
        int c = colOf(pos);
        for (int d = 0; d < 6; d++) {
            int nr = r + DIR_TABLE[r % 2][d][0];
            int nc = c + DIR_TABLE[r % 2][d][1];
            if (nr == rowOf(next) && nc == colOf(next)) return d;
        }
        return -1;
    }
};

inline int getNeighbor(const Map& map, int idx, Direction dir) {
    int r = map.rowOf(idx);
    int c = map.colOf(idx);
    int parity = r % 2;
    int dr = DIR_TABLE[parity][(int)dir][0];
    int dc = DIR_TABLE[parity][(int)dir][1];
    int nidx = map.indexOf(r + dr, c + dc);
    if (nidx < 0) return -1;
    return nidx;
}

inline std::vector<int> getAllNeighbors(const Map& map, int idx) {
    std::vector<int> result;
    for (int d = 0; d < 6; ++d) {
        int n = getNeighbor(map, idx, static_cast<Direction>(d));
        if (n != -1) result.push_back(n);
    }
    return result;
}

struct ReverseDijkstraResult {
    std::vector<MoveCost> dist;
    std::vector<int> parent;
};

ReverseDijkstraResult reverseDijkstra(Map& map, int goal) {
    std::vector<MoveCost> dist(map.width * map.height, {INT_MAX, INT_MAX});
    std::vector<int> parent(map.width * map.height, -1);
    
    using p = std::pair<MoveCost, int>;
    std::priority_queue<p, std::vector<p>, std::greater<p>> search;

    dist[goal] = {0, 0};
    search.push({{0, 0}, goal});

    while (!search.empty()) {
        MoveCost d = search.top().first;
        int p = search.top().second;
        search.pop();

        if (d != dist[p]) continue;
        std::vector move = getAllNeighbors(map, p);

        for (int m = 0; m < move.size(); m++) {
            int next = move[m];
            if (map.terrainAt(next) == Terrain::POND) continue;
            MoveCost cost = getMoveCost(map.terrainAt(next), map.roadStat[next]);
            MoveCost newDist = d + cost;
            if (newDist.time < dist[next].time) {
                dist[next] = newDist;
                parent[next] = p;
                search.push({newDist, next});
            }
        }
    }

    return {dist, parent};
}

std::vector<int> getPath(std::vector<int>& v, int start, int goal) {
    std::vector<int> ret;

    int current = start;
    while (current != INT_MAX) {
        ret.push_back(current);
        if (current == goal) break;
        current = v[current];
    }

    if (ret.back() != goal) return {};
    return ret;
}