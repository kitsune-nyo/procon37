#include <set>
#include <queue>

enum AgentKind { PATROL, SUPPLY };

struct Agent {
    int pos, fuel;
    AgentKind kind;
    std::queue<int> actions;
    std::vector<int> history;
    std::set<int> visitedSpotPos;
    int nextPos = -1, nextFuel = -1, moveEndStep = -1;;
};

class AgentManager {
public:
    std::vector<Agent> agents;
    int fuel = 0, patrolNum = 0, supplyNum = 0;

    void placeAgent(int pos, int f) {
        Agent agent;
        agent.pos = pos;
        agent.fuel = f;
        patrolNum++;
        agent.kind = AgentKind::PATROL;
        agents.push_back(agent);
    }

    void decideSupply(int agent) {
        if (agents[agent].kind == AgentKind::SUPPLY) return;
        agents[agent].kind = AgentKind::SUPPLY;
        patrolNum--;
        supplyNum++;
    }
};