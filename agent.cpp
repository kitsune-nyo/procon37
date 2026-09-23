#include <stdexcept>
#include <queue>

enum AgentKind { PATROL, SUPPLY };

struct Agent {
    int pos, fuel;
    AgentKind kind;
    std::queue<int> actions;
    std::vector<int> history, visitedSpotPos;
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

    void assignKinds(std::vector<AgentKind> kinds) {
        for (int a = 0; a < kinds.size(); a++) {
            if (a >= agents.size()) throw std::runtime_error("エージェントに対するクエリが多すぎます");
            else {
                agents[a].kind = kinds[a];
                if (kinds[a] == AgentKind::PATROL) patrolNum++;
                else supplyNum++;
            }
        }
    }

    void applySupply() {
        for (int s = 0; s < agents.size(); s++) {
            if (agents[s].kind == AgentKind::PATROL) continue;
            for (int p = 0; p < agents.size(); p++) {
                if (agents[p].kind == AgentKind::SUPPLY) continue;
                if (agents[s].pos == agents[p].pos) agents[p].fuel = fuel;
            }
        }
    }
};