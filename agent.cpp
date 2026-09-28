#include <set>
#include <queue>

enum AgentKind { PATROL, SUPPLY };

struct Agent {
    int pos, fuel;
    AgentKind kind;
    std::queue<int> actions;
    std::vector<int> history;
    std::set<int> visitedSpotPos;
};

class AgentManager {
public:
    std::vector<Agent> agents;
    int fuel = 0, patrolNum = 0, supplyNum = 0;

    void placeAgent(int pos) {
        Agent agent;
        agent.pos = pos;
        agent.fuel = fuel;
        agent.kind = AgentKind::PATROL;
        agents.push_back(agent);
    }

    void assignKinds(const std::vector<AgentKind>& kinds) {
        for (int a = 0; a < kinds.size(); a++) {
            agents[a].kind = kinds[a];
            if (kinds[a] == AgentKind::PATROL) patrolNum++;
            else supplyNum++;
        }
    }
};