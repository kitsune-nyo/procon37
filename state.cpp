#include "map.cpp"
#include "spot.cpp"
#include "agent.cpp"

#define INACTION INT_MIN
#define WAIT -1

const double fuelPerMax = 0.3;
const std::vector<double> evaluateWeight = {10000, 10000, 10000, 1, 100};

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

    void collectUdon(std::set<int>& brands) {
        for (Agent& agent: agentMgr.agents) {
            if (agent.kind == AgentKind::SUPPLY) continue;
            Spot* s = spotMgr.findAt(agent.pos);
            if (s == nullptr) continue;
            bool canCollect = (agent.visitedSpotPos.count(s->pos) == 0);
            if (canCollect) {
                if (spotMgr.consume(s->pos)) {
                    udonSum++;
                    brands.insert(s->brand);
                    udonBrand.insert(s->brand);
                    agent.visitedSpotPos.insert(s->pos);
                }
            }
        }
    }
};

void evaluate(
    State& state, Map& map, std::vector<ReverseDijkstraResult>& md, std::vector<ReverseDijkstraResult>& dd, std::set<int>& brands
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
    fuel /= std::max(1, state.agentMgr.patrolNum);

    state.score = 0;
    std::vector<double> x = {
        (double)brands.size(),
        (double)state.udonBrand.size(), 
        (double)state.udonSum,
        fuel,
        agentsToNearestSpotScore
    };
    std::vector<double> w = evaluateWeight;
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

        std::vector<int> path;
        if (agent.kind == AgentKind::PATROL) {
            for (Spot& target : state.spotMgr.spots) {
                path = map.getPath(md[target.pos].parent, agent.pos, target.pos, agent.fuel);
                if (!path.empty()) actions.push_back(path);
            }
            for (Agent& target : state.agentMgr.agents) {
                if (target.kind != AgentKind::SUPPLY) continue;
                path = map.getHalfPath(md[target.pos].parent, agent.pos, target.pos, agent.fuel);
                if (!path.empty()) actions.push_back(path);
            }
        } else {
            for (Agent& target : state.agentMgr.agents) {
                if (target.kind != AgentKind::PATROL) continue;
                path = map.getHalfPath(md[target.pos].parent, agent.pos, target.pos, INT_MAX);
                if (!path.empty()) actions.push_back(path);
            }
            for (Spot& target : state.spotMgr.spots) {
                path = map.getPath(md[target.pos].parent, agent.pos, target.pos, INT_MAX);
                if (!path.empty()) actions.push_back(path);
            }
        }
        actions.push_back({-1});

        for (std::vector<int>& action : actions) {
            State s = state;
            Agent& newAgent = s.agentMgr.agents[i];
            while (!newAgent.actions.empty()) newAgent.actions.pop();
            for (int a : action) newAgent.actions.push(a);
            ret.push_back(s);
        }
        break;
    }

    return ret;
}

bool update(State& state, Map& map, int steps, std::set<int>& brands) {
    for (Agent& agent : state.agentMgr.agents) {
        if (agent.actions.empty()) return false;
    }

    for (Agent& agent : state.agentMgr.agents) {
        if (agent.history.size() < state.step + 1) agent.history.push_back(WAIT);

        if (agent.nextPos != -1 && state.step == agent.moveEndStep) {
            if (agent.kind == AgentKind::PATROL) agent.fuel -= agent.nextFuel;
            agent.pos = agent.nextPos;
            agent.nextPos = -1;
            agent.nextFuel = 0;
            agent.moveEndStep = -1;
        }

        if (agent.nextPos != -1) {
            agent.history[state.step] = INACTION;
            continue;
        }

        int nextPos = agent.actions.front();

        if (nextPos == WAIT) {
            agent.history[state.step] = WAIT;
            agent.actions.pop();
            continue;
        }

        MoveCost cost = map.getMoveCost(agent.pos);

        if (state.step + cost.time > steps) {
            agent.history[state.step] = WAIT;
            continue;
        }

        agent.history[state.step] = map.getDirection(agent.pos, nextPos);

        agent.nextPos = nextPos;
        agent.nextFuel = cost.fuel;
        agent.moveEndStep = state.step + cost.time;
        agent.actions.pop();
    }

    state.applySupply();
    state.collectUdon(brands);
    state.step++;

    return true;
}