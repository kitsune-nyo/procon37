#include "spot.cpp"

enum AgentKind {
    PATROL, SUPPLY
};

struct Agent {
    int pos, fuel;
    AgentKind kind;
};

class AgentManager {
public:
    std::vector<Agent> agents;
    int count, fuel, patrolNum, supplyNum;

    void init(int f) {
        agents.clear();
        count = 0;
        fuel = f;
        patrolNum = 0;
        supplyNum = 0;
    }

    void placeAgentsRandomly(Map& map, int cnt, int f, unsigned seed = 42) {
        std::vector<int> plainCells;
        for (int i = 0; i < map.width * map.height; ++i) {
            if (map.terrainAt(i) == Terrain::PLAIN) plainCells.push_back(i);
        }
        if ((int)plainCells.size() < cnt) throw std::runtime_error("平地セルが不足しています");

        std::mt19937 rng(seed);
        std::shuffle(plainCells.begin(), plainCells.end(), rng);

        agents.clear();

        count = cnt, fuel = f;
        for (int i = 0; i < cnt; ++i) {
            Agent agent;
            agent.pos = plainCells[i];
            agent.fuel = f;
            agent.kind = AgentKind::PATROL;
            agents.push_back(agent);
        }
    }

    void placeAgent(int pos) {
        Agent agent;
        agent.pos = pos;
        agent.fuel = fuel;
        agent.kind = AgentKind::PATROL;
        agents.push_back(agent);
        count++;
    }

    void assignKinds(std::vector<AgentKind> kinds) {
        for (int a = 0; a < kinds.size(); a++) {
            if (a >= count) throw std::runtime_error("エージェントに対するクエリが多すぎます");
            else {
                agents[a].kind = kinds[a];
                if (kinds[a] == AgentKind::PATROL) patrolNum++;
                else supplyNum++;
            }
        }
    }

    void applySupply() {
        for (int s = 0; s < count; s++) {
            if (agents[s].kind == AgentKind::PATROL) continue;
            for (int p = 0; p < count; p++) {
                if (agents[p].kind == AgentKind::SUPPLY) continue;
                if (agents[s].pos == agents[p].pos) agents[p].fuel = fuel;
            }
        }
    }
};