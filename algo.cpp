#include <iostream>

#include "state.cpp"

std::vector<int> compress(std::vector<int>& v) {
    std::vector<int> ret;

    int sum = 0;
    for (int e: v) {
        if (e != INT_MIN) {
            if (e < 0) sum += e;
            else {
                if (sum < 0) {
                    ret.push_back(sum);
                    sum = 0;
                }
                ret.push_back(e);
            }
        }
    }
    if (sum < 0) ret.push_back(sum);

    return ret;
}

int main() {
    // ===GENERAL===
    int agentCount = 4;
    int fuelLimit = 20;
    int steps = 50;

    // ===MAP===
    Map map;
    map.init(32, 32,
        {
            {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0},
            {0, 3, 3, 0, 2, 2, 0, 0, 0, 3, 3, 0, 2, 2, 0, 0, 0, 3, 3, 0, 2, 2, 0, 0, 0, 3, 3, 0, 2, 2, 0, 0},
            {0, 0, 3, 0, 0, 2, 0, 1, 0, 0, 3, 0, 0, 2, 0, 1, 0, 0, 3, 0, 0, 2, 0, 1, 0, 0, 3, 0, 0, 2, 0, 1},
            {1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0},
            {0, 0, 2, 0, 0, 3, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0},
            {0, 2, 0, 0, 0, 3, 3, 0, 0, 2, 0, 0, 0, 3, 3, 0, 0, 2, 0, 0, 0, 3, 3, 0, 0, 2, 0, 0, 0, 3, 3, 0},
            {0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0},
            {0, 3, 3, 0, 2, 2, 0, 0, 0, 3, 3, 0, 2, 2, 0, 0, 0, 3, 3, 0, 2, 2, 0, 0, 0, 3, 3, 0, 2, 2, 0, 0},
            {0, 0, 3, 0, 0, 2, 0, 1, 0, 0, 3, 0, 0, 2, 0, 1, 0, 0, 3, 0, 0, 2, 0, 1, 0, 0, 3, 0, 0, 2, 0, 1},
            {1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0},
            {0, 0, 2, 0, 0, 3, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0},
            {0, 2, 0, 0, 0, 3, 3, 0, 0, 2, 0, 0, 0, 3, 3, 0, 0, 2, 0, 0, 0, 3, 3, 0, 0, 2, 0, 0, 0, 3, 3, 0},
            {0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0},
            {0, 3, 3, 0, 2, 2, 0, 0, 0, 3, 3, 0, 2, 2, 0, 0, 0, 3, 3, 0, 2, 2, 0, 0, 0, 3, 3, 0, 2, 2, 0, 0},
            {0, 0, 3, 0, 0, 2, 0, 1, 0, 0, 3, 0, 0, 2, 0, 1, 0, 0, 3, 0, 0, 2, 0, 1, 0, 0, 3, 0, 0, 2, 0, 1},
            {1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0},
            {0, 0, 2, 0, 0, 3, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0},
            {0, 2, 0, 0, 0, 3, 3, 0, 0, 2, 0, 0, 0, 3, 3, 0, 0, 2, 0, 0, 0, 3, 3, 0, 0, 2, 0, 0, 0, 3, 3, 0},
            {0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0},
            {0, 3, 3, 0, 2, 2, 0, 0, 0, 3, 3, 0, 2, 2, 0, 0, 0, 3, 3, 0, 2, 2, 0, 0, 0, 3, 3, 0, 2, 2, 0, 0},
            {0, 0, 3, 0, 0, 2, 0, 1, 0, 0, 3, 0, 0, 2, 0, 1, 0, 0, 3, 0, 0, 2, 0, 1, 0, 0, 3, 0, 0, 2, 0, 1},
            {1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0},
            {0, 0, 2, 0, 0, 3, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0},
            {0, 2, 0, 0, 0, 3, 3, 0, 0, 2, 0, 0, 0, 3, 3, 0, 0, 2, 0, 0, 0, 3, 3, 0, 0, 2, 0, 0, 0, 3, 3, 0},
            {0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
        }
    );
    map.setRoadStatus(96, RoadStatus::JAMMED);

    // ===SPOTS===
    SpotManager spotMgr;
    spotMgr.placeSpot(0, 5, 3);
    spotMgr.placeSpot(1, 16, 4);
    spotMgr.placeSpot(2, 32, 1);
    spotMgr.placeSpot(0, 40, 2);

    // ===AGENTS===
    AgentManager agentMgr;
    agentMgr.fuel = fuelLimit;
    agentMgr.placeAgent(15);
    agentMgr.placeAgent(32);
    agentMgr.placeAgent(1);
    agentMgr.placeAgent(43);
    agentMgr.assignKinds({AgentKind::SUPPLY, AgentKind::SUPPLY, AgentKind::PATROL, AgentKind::PATROL});

    // ===CACHE===

    std::vector<ReverseDijkstraResult> mapDijkstra;
    for (int i = 0; i < map.cells.size(); i++) mapDijkstra.push_back(map.reverseDijkstra(i));
    std::vector<ReverseDijkstraResult> dropDijkstra;
    for (int i = 0; i < map.cells.size(); i++) {
        if (map.cells[i] != Terrain::POND) {
            Terrain tmp = map.cells[i];
            map.cells[i] = Terrain::POND;
            dropDijkstra.push_back(map.reverseDijkstra(i));
            map.cells[i] = tmp;
        } else {
            std::vector<MoveCost> a;
            std::vector<int> b;
            dropDijkstra.push_back({a, b});
        }
    }

    // ===SEARCH===

    int beamWidth = 800;
    std::priority_queue<State, std::vector<State>, std::greater<State>> states;
    State init(spotMgr, agentMgr);
    states.push(init);
    for (int s = 1; s <= steps; s++) {
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
        std::cout << (s / (double)steps) * 100 << " \% completed, "
            << "step " << s << ", " << "max score " << states.top().score << std::endl;
    }
    while (states.size() > 1) states.pop();
    State bestState = states.top();

    std::cout << "\n";
    for (Agent& agent: bestState.agentMgr.agents) std::cout << "Agent: { " << "fuel: " << agent.fuel << ", pos: " << agent.pos << " }\n";
    std::cout << "\n";

    std::cout << "brand:";
    for (auto it: bestState.udonBrand) std::cout << " " << it;
    std::cout << "\n";
    std::cout << "udon: " << bestState.udonSum << "\n\n";

    for (Agent& agent: bestState.agentMgr.agents) {
        std::cout << "Agent action: ";
        std::vector<int> answer = compress(agent.history);
        for (int i = 0; i < answer.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << answer[i];
        }
        std::cout << "\n";
    }

    return 0;
}
