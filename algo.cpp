#include <iostream>
#include <queue>
#include <set>

#include "map.cpp"
#include "spot.cpp"
#include "agent.cpp"

class State {
public:
    SpotManager spotMgr;
    AgentManager agentMgr;

    int step = 0, score = 0;
    int udonSum = 0;
    std::set<int> udonBrand;

    bool operator>(const State& r) const {
        if (step != r.step) return step > r.step;
        return score > r.score;
    }

    State(SpotManager s, AgentManager a) {
        spotMgr = s;
        agentMgr = a;
    }

    int fuelSum() {
        int ret = 0;
        for (Agent& agent: agentMgr.agents) ret += agent.fuel;
        return ret;
    }

    void applySupply() {
        for (Agent& s: agentMgr.agents) {
            if (s.kind == AgentKind::PATROL) continue;
            for (Agent& p: agentMgr.agents) {
                if (p.kind == AgentKind::SUPPLY) continue;
                if (s.pos == p.pos) p.fuel = agentMgr.fuel;
            }
        }
    }

    void collectUdon() {
        for (Agent& agent: agentMgr.agents) {
            Spot* s = spotMgr.findAt(agent.pos);
            if (s == nullptr) continue;
            bool canCollect = true;
            for (int pos: agent.visitedSpotPos) {
                if (s->pos == pos) canCollect = false;
            }
            if (canCollect) {
                bool c = spotMgr.consume(s->pos);
                if (c) {
                    udonSum++;
                    udonBrand.insert(s->brand);
                    agent.visitedSpotPos.push_back(s->pos);
                }
            }
        }
    }
};

void evaluate(State& state) {
    state.score = 0;
    std::vector<int> x = {
        (int)state.udonBrand.size(), 
        state.udonSum,
        state.fuelSum()
    };
    std::vector<int> w = {
        1000, 
        100,
        1
    };
    for (int i = 0; i < x.size(); i++) state.score += w[i] * x[i];
}

std::vector<State> separate(State& state) {
    std::vector<State> ret;

    for (int i = 0; i < state.agentMgr.agents.size(); i++) {
        if (state.agentMgr.agents[i].actions.empty()) {
            State s = state;
            Agent& agent = s.agentMgr.agents[i];
            std::vector<std::vector<int>> actions;
            if (agent.kind == AgentKind::PATROL) {
                actions.push_back({-1});
                // 補給車の本流・分流、スポットの本流・分流
            } else {
                actions.push_back({-1});
                // 巡回車の本流・分流、スポットの本流・分流
            }
            std::queue<int>& newAction = agent.actions;
            for (std::vector<int>& action: actions) {
                while (!newAction.empty()) newAction.pop();
                for (int& a: action) newAction.push(a);
                ret.push_back(s);
            }
        }
    }

    return ret;
}

bool update(State& state, Map& map, int steps) {
    for (int i = 0; i < state.agentMgr.agents.size(); i++) {
        if (state.agentMgr.agents[i].actions.empty()) return false;
    }

    for (int i = 0; i < state.agentMgr.agents.size(); i++) {
        Agent& current = state.agentMgr.agents[i];
        if (current.history.size() < state.step + 1) current.history.push_back(-1);
        int action = current.actions.front();
        if (action == -1) {
            current.history[state.step] = -1;
            current.actions.pop();
            continue;
        }
        if (state.step == 0 || current.history[state.step] != INT_MIN) {
            MoveCost cost = getMoveCost(map.terrainAt(current.pos), map.roadStatAt(current.pos));
            if (steps - state.step < cost.time) current.history[state.step] = state.step - steps;
            else current.history[state.step] = map.getDirection(current.pos, action);
            current.pos = action;
            current.fuel -= cost.fuel;
            for (int i = 1; i < cost.time; i++) current.history.push_back(INT_MIN); 
            current.actions.pop();
        }
    }
    state.applySupply();
    state.collectUdon();
    state.step++;

    return true;
}

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
    int steps = 10;

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
    
    std::vector<ReverseDijkstraResult> mapDijkstra;
    for (int i = 0; i < map.cells.size(); i++) mapDijkstra.push_back(reverseDijkstra(map, i));
    std::vector<ReverseDijkstraResult> dropDijkstra;
    for (int i = 0; i < map.cells.size(); i++) {
        if (map.cells[i] != Terrain::POND) {
            Terrain tmp = map.cells[i];
            map.cells[i] = Terrain::POND;
            mapDijkstra.push_back(reverseDijkstra(map, i));
            map.cells[i] = tmp;
        } else {
            std::vector<MoveCost> a;
            std::vector<int> b;
            mapDijkstra.push_back({a, b});
        }
    }

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
    agentMgr.assignKinds({AgentKind::PATROL, AgentKind::SUPPLY, AgentKind::PATROL, AgentKind::PATROL});

    // ===SEARCH===

    int beamWidth = 200;
    std::priority_queue<State, std::vector<State>, std::greater<State>> states;
    State init(spotMgr, agentMgr);
    states.push(init);
    for (int s = 1; s <= steps; s++) {
        while (!states.empty() && states.top().step < s) {
            State state = states.top();
            states.pop();
            std::vector<State> newStates = separate(state);
            if (update(state, map, steps)) states.push(state);
            for (State& next : newStates) {
                evaluate(next);
                states.push(next);
            }
        }
        while (states.size() > beamWidth) states.pop();
    }
    while (states.size() > 1) states.pop();
    State bestState = states.top();

    for (Agent& agent: bestState.agentMgr.agents) {
        std::cout << "=== Agent ===" << std::endl;
        std::vector<int> answer = compress(agent.history);
        for (int i = 0; i < answer.size(); i++) {
            if (i > 0) std::cout << " ";
            std::cout << answer[i];
        }
        std::cout << "\n\n";
    }

    return 0;
}
