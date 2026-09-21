#include "agent.cpp"

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

class State {
    std::vector<Spot> spots;
    std::vector<Agent> agents;
    int step, udonBrandSum, udonSum, fuelSum;
/*
    double evaluate(Map& map, AgentManager& agentMgr) {
        if (!isValid()) return -1e18;
        const double weight[4] = {1.0, 1.0, 1.0, -1.0};
        return 
    }

    bool isValid() {

    }
*/
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

    // ===SPOTS===
    SpotManager spotMgr;
    spotMgr.placeSpot(0, 5, 3);
    spotMgr.placeSpot(1, 16, 4);
    spotMgr.placeSpot(2, 32, 1);
    spotMgr.placeSpot(0, 40, 2);

    // ===AGENTS===
    AgentManager agentMgr;
    int agentCount = 4;
    int fuelLimit = 20;
    agentMgr.init(fuelLimit);
    agentMgr.placeAgent(15);
    agentMgr.placeAgent(32);
    agentMgr.placeAgent(1);
    agentMgr.placeAgent(43);
    agentMgr.assignKinds({AgentKind::PATROL, AgentKind::SUPPLY, AgentKind::PATROL, AgentKind::PATROL});

    // ===SEARCH===

    return 0;
}
