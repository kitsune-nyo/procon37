#include <algorithm>
#include <queue>
#include <set>

#include "map.cpp"
#include "spot.cpp"
#include "agent.cpp"

#define INACTION INT_MIN
#define WAIT -1
#define BIG 99999

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
            if (agent.kind == AgentKind::SUPPLY) continue;
            Spot* s = spotMgr.findAt(agent.pos);
            if (s == nullptr) continue;
            bool canCollect = (agent.visitedSpotPos.count(s->pos) == 0);
            if (canCollect) {
                if (spotMgr.consume(s->pos)) {
                    udonSum++;
                    udonBrand.insert(s->brand);
                    agent.visitedSpotPos.insert(s->pos);
                }
            }
        }
    }
};

void evaluate(
    State& state, Map& map, std::vector<ReverseDijkstraResult>& md, std::vector<ReverseDijkstraResult>& dd
) {
    double agentsToNearestSpotScore = 0;
    for (Agent& agent: state.agentMgr.agents) {
        if (agent.kind == AgentKind::SUPPLY) continue;
        double nearest = BIG;
        for (Spot& spot: state.spotMgr.spots) {
            if (agent.visitedSpotPos.count(spot.pos) > 0 || spot.pos == agent.pos) continue;
            nearest = std::min((int)nearest, md[spot.pos].dist[agent.pos].time);
        }
        agentsToNearestSpotScore += (map.width + map.height) / nearest;
    }

    double fuel = 0;
    for (Agent& agent: state.agentMgr.agents) {
        if (agent.kind == AgentKind::SUPPLY) continue;
        fuel += agent.fuel;
    }

    state.score = 0;
    std::vector<double> x = {
        (double)state.udonBrand.size(), 
        (double)state.udonSum,
        fuel,
        agentsToNearestSpotScore
    };
    std::vector<double> w = {
        10000,
        1000,
        1,
        10
    };
    for (int i = 0; i < x.size(); i++) state.score += w[i] * x[i];
}

std::vector<State> separate(
    State& state, Map& map, std::vector<ReverseDijkstraResult>& md, std::vector<ReverseDijkstraResult>& dd
) {
    std::vector<State> ret;

    for (int i = 0; i < state.agentMgr.agents.size(); i++) {
        if (!state.agentMgr.agents[i].actions.empty()) continue;
        Agent& agent = state.agentMgr.agents[i];
        std::vector<std::vector<int>> actions;

        if (agent.kind == AgentKind::PATROL) {
            for (Spot& target : state.spotMgr.spots) {
                std::vector<int> path = map.getPath(md[target.pos].parent, agent.pos, target.pos, agent.fuel);
                if (!path.empty()) actions.push_back(path);
            }
            for (Agent& target : state.agentMgr.agents) {
                if (target.kind != AgentKind::SUPPLY) continue;
                std::vector<int> path = map.getPath(md[target.pos].parent, agent.pos, target.pos, agent.fuel);
                if (!path.empty()) actions.push_back(path);
            }
            actions.push_back({-1});
        } else {
            for (Agent& target : state.agentMgr.agents) {
                if (target.kind != AgentKind::PATROL) continue;
                std::vector<int> path = map.getPath(md[target.pos].parent, agent.pos, target.pos, INT_MAX);
                if (!path.empty()) actions.push_back(path);
            }
            for (Spot& target : state.spotMgr.spots) {
                std::vector<int> path = map.getPath(md[target.pos].parent, agent.pos, target.pos, INT_MAX);
                if (!path.empty()) actions.push_back(path);
            }
        }

        for (auto& action : actions) {
            State s = state;
            auto& newAgent = s.agentMgr.agents[i];
            while (!newAgent.actions.empty()) newAgent.actions.pop();
            for (int a : action) newAgent.actions.push(a);
            ret.push_back(s);
        }

        return ret;
    }

    return ret;
}

bool update(State& state, Map& map, int steps) {
    for (Agent& agent: state.agentMgr.agents) {
        if (agent.actions.empty()) return false;
    }

    for (Agent& agent: state.agentMgr.agents) {
        if (agent.history.size() < state.step + 1) agent.history.push_back(WAIT);
        int nextPos = agent.actions.front();

        if (nextPos == WAIT) {
            agent.history[state.step] = WAIT;
            agent.actions.pop();
            continue;
        }

        if (state.step == 0 || agent.history[state.step] != INACTION) {
            MoveCost cost = map.getMoveCost(agent.pos);
            if (steps - state.step < cost.time) {
                agent.history[state.step] = state.step - steps;
                continue;
            }
            else agent.history[state.step] = map.getDirection(agent.pos, nextPos);
            agent.pos = nextPos;
            if (agent.kind == AgentKind::PATROL) agent.fuel -= cost.fuel;
            for (int j = 1; j < cost.time; j++) agent.history.push_back(INACTION); 
            agent.actions.pop();
        }
    }

    state.applySupply();
    state.collectUdon();
    state.step++;

    return true;
}