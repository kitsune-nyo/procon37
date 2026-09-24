#include <iostream>
#include <climits>
#include <set>

#include "map.cpp"
#include "spot.cpp"
#include "agent.cpp"

struct ReverseDijkstraResult {
    std::vector<int> dist;
    std::vector<int> parent;
};

ReverseDijkstraResult reverseDijkstra(Map& map, int goal) {
    std::vector<int> dist(map.width * map.height, INT_MAX);
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

    void evaluate() {
        score = 0;
        std::vector<int> x = {
            (int)udonBrand.size(), 
            udonSum,
            fuelSum()
        };
        std::vector<int> w = {
            1000, 
            100,
            1
        };
        for (int i = 0; i < x.size(); i++) score += w[i] * x[i];
    }

    void update(Map& map) {
        for (Agent& agent: agentMgr.agents) {
            if (agent.actions.empty()) {
                if (agent.kind == AgentKind::PATROL) {
                    // 補給車の本流・分流、スポットの本流・分流、待機
                } else {
                    // 巡回車の本流・分流、スポットの本流・分流、待機
                }
                // 日の最後は行動しない
                agent.actions.push(agent.pos + map.width);
                agent.actions.push(agent.pos);
            }
            if (agent.history.size() < step + 1) agent.history.push_back(-1);
            if (step == 0 || agent.history[step] != INT_MIN) {
                int action = agent.actions.front();
                MoveCost cost = getMoveCost(map.terrainAt(agent.pos), map.roadStatAt(agent.pos));
                agent.history[step] = map.getDirection(agent.pos, action);
                agent.pos = action;
                agent.fuel -= cost.fuel;
                for (int i = 1; i < cost.time; i++) agent.history.push_back(-1); 
                agent.actions.pop();
            }
        }
        applySupply();
        collectUdon();
        step++;
    }
};

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
    map.init(8, 8,
        {
            {0, 0, 2, 0, 0, 0, 0, 0},
            {0, 3, 3, 0, 2, 2, 0, 0},
            {0, 0, 3, 0, 0, 2, 0, 1},
            {1, 1, 1, 1, 1, 1, 1, 0},
            {0, 0, 2, 0, 0, 3, 0, 0},
            {0, 2, 0, 0, 0, 3, 3, 0},
            {0, 0, 0, 1, 1, 1, 1, 0},
            {0, 0, 0, 0, 0, 0, 0, 0},
        }
    );

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
        while (states.top().step != s) {
            State state = states.top();
            state.update(map);
            state.evaluate();
            states.pop();
            states.push(state);
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
