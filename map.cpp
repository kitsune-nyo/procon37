#include <climits>
#include <vector>
#include <queue>
#include <chrono>

#define BIG 99999

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
        return fuel > r.fuel; // 同じ時間なら燃料が少ない方を「小さい」とする
    }
    bool operator<(const MoveCost& r) const {
        if (time != r.time) return time < r.time;
        return fuel < r.fuel;
    }
    MoveCost operator+(const MoveCost& r) const { return {time + r.time, fuel + r.fuel}; }
};

struct ReverseDijkstraResult {
    std::vector<MoveCost> dist;
    std::vector<int> parent;
};

enum class Direction : int { NW = 0, NE = 1, E = 2, SE = 3, SW = 4, W = 5 };

inline const int DIR_TABLE[2][6][2] = {
    { {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {0, -1}, },
    { {-1, -1}, {-1, 0}, {0, 1}, {1, 0}, {1, -1}, {0, -1}, }
};

class Map {
public:
    int width, height;
    std::vector<Terrain> cells;
    std::vector<RoadStatus> roadStat;

    void init(int w, int h, const std::vector<std::vector<int>>& grid)
    {
        width = w, height = h;
        cells.assign(w * h, Terrain::PLAIN);
        roadStat.assign(w * h, RoadStatus::SMOOTH);
        for (int r = 0; r < h; r++) {
            for (int c = 0; c < w; c++) cells[r * w + c] = static_cast<Terrain>(grid[r][c]);
        }
    }

    void setRoadStatus(int pos, RoadStatus road) { roadStat[pos] = road; }

    inline int indexOf(int r, int c) const { return r * width + c; }
    inline int rowOf(int idx) const { return idx / width; }
    inline int colOf(int idx) const { return idx % width; }

    Terrain terrainAt(int idx) const { return cells[idx]; }
    RoadStatus roadStatAt(int idx) const { return roadStat[idx]; }

    int getDirection(int current, int next) {
        if (current < 0 || current >= (int)cells.size() || next < 0 || next >= (int)cells.size()) return -1;
        int r = rowOf(current);
        int c = colOf(current);
        for (int d = 0; d < 6; d++) {
            int nr = r + DIR_TABLE[r % 2][d][0];
            int nc = c + DIR_TABLE[r % 2][d][1];
            if (nr == rowOf(next) && nc == colOf(next)) return d;
        }
        return -1;
    }

    inline int getNeighbor(int idx, Direction dir) {
        int r = rowOf(idx), c = colOf(idx);
        int parity = r % 2;
        int dr = DIR_TABLE[parity][(int)dir][0], dc = DIR_TABLE[parity][(int)dir][1];
        if (r + dr < 0 || r + dr > height - 1 || c + dc < 0 || c + dc > width - 1) return -1;
        return indexOf(r + dr, c + dc);
    }

    inline std::vector<int> getAllNeighbors(int idx) {
        std::vector<int> result;
        for (int d = 0; d < 6; d++) {
            int r = rowOf(idx), c = colOf(idx);
            int parity = r % 2;
            int dr = DIR_TABLE[parity][d][0], dc = DIR_TABLE[parity][d][1];
            if (r + dr < 0 || r + dr > height - 1 || c + dc < 0 || c + dc > width - 1) continue;
            result.push_back(indexOf(r + dr, c + dc));
        }
        return result;
    }

    inline MoveCost getMoveCost(int pos) {
        Terrain t = terrainAt(pos);
        RoadStatus r = roadStatAt(pos);
        switch (t) {
            case Terrain::PLAIN:
                return {2, 1};
            case Terrain::MOUNTAIN:
                return {3, 2};
            case Terrain::ROAD:
                switch (r) {
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

    ReverseDijkstraResult reverseDijkstra(
        int goal, std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()
    ) {
        std::vector<MoveCost> dist(width * height, {BIG, BIG});
        std::vector<int> parent(width * height, -1);
        
        using p = std::pair<MoveCost, int>;
        std::priority_queue<p, std::vector<p>, std::greater<p>> search;

        dist[goal] = {0, 0};
        search.push({dist[goal], goal});

        while (!search.empty()) {
            if (std::chrono::steady_clock::now() >= deadline) return {};
            MoveCost d = search.top().first;
            int current = search.top().second;
            search.pop();

            if (d != dist[current]) continue;

            for (int next: getAllNeighbors(current)) {
                if (terrainAt(next) == Terrain::POND) continue;
                MoveCost newDist = d + getMoveCost(next);
                if (newDist < dist[next]) {
                    dist[next] = newDist;
                    parent[next] = current;
                    search.push({newDist, next});
                }
            }
        }

        return {dist, parent};
    }

    std::vector<int> getPath(const std::vector<int>& v, int start, int goal, int fuel) {
        std::vector<int> ret;

        int current = start;
        int f = fuel;
        while (current != -1 && current != INT_MAX) {
            if (current == goal) break;

            int next = v[current];
            if (next == -1 || next == INT_MAX) break;

            bool adjacent = false;
            for (int n : getAllNeighbors(current)) {
                if (n == next) {
                    adjacent = true;
                    break;
                }
            }
            if (!adjacent) break;

            MoveCost cost = getMoveCost(current);
            if (f < cost.fuel) break;
            f -= cost.fuel;

            ret.push_back(next);
            current = next;
        }

        return ret;
    }

    std::vector<int> getHalfPath(const std::vector<int>& v, int start, int goal, int fuel) {
        std::vector<int> ret;
        
        std::vector<int> path = getPath(v, start, goal, fuel);
        if (path.empty()) return path;

        for (int i = 0; i < (path.size() + 1) / 2; i++) ret.push_back(path[i]);

        return ret;
    }
};