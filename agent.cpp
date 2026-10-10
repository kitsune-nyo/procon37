#include <set>
#include <queue>
#include <vector>
#include <algorithm>

// std::queue<int> と同じ使い方ができる軽量版（中身は vector + 先頭位置）。
// ビームサーチで状態を大量にコピーするので、メモリ確保の少ない形にしている。
class ActionQueue {
    std::vector<int> buf;
    size_t head = 0;
public:
    bool empty() const { return head >= buf.size(); }
    size_t size() const { return buf.size() - head; }
    int front() const { return buf[head]; }
    void push(int x) {
        if (head > 0 && head == buf.size()) { buf.clear(); head = 0; }
        buf.push_back(x);
    }
    void pop() { head++; if (head == buf.size()) { buf.clear(); head = 0; } }
    void clear() { buf.clear(); head = 0; }
    std::vector<int>::const_iterator begin() const { return buf.begin() + head; }
    std::vector<int>::const_iterator end() const { return buf.end(); }
};

// std::set<int> の count / insert / size だけを持つ軽量版（要素数は1日に訪れるスポット数程度）。
class SmallSet {
    std::vector<int> v;
public:
    size_t count(int x) const { return std::find(v.begin(), v.end(), x) != v.end() ? 1 : 0; }
    void insert(int x) { if (!count(x)) v.push_back(x); }
    size_t size() const { return v.size(); }
};

enum AgentKind { PATROL, SUPPLY };

struct Agent {
    int pos, fuel;
    AgentKind kind;
    ActionQueue actions;
    std::vector<int> history;
    SmallSet visitedSpotPos;
    int nextPos = -1, nextFuel = -1, moveEndStep = -1;
    int target = -1; // 補給車が今担当している巡回車の番号（-1 = 担当なし）
    bool parking = false; // 巡回車が「スポットで日を終えるため」に移動中か
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