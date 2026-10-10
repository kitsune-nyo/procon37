#pragma once
#include <unordered_set>

// Length-delimited exact future state. History is deliberately omitted: the
// retained State still owns a complete legal history for this same future.
inline std::vector<int> stateKey(const State& s) {
    std::vector<int> key;
    key.reserve(32 + s.spotMgr.spots.size()*3);
    key.insert(key.end(), {s.step, s.udonSum, s.agentMgr.fuel,
        s.agentMgr.patrolNum, s.agentMgr.supplyNum, (int)s.udonBrand.size()});
    key.insert(key.end(), s.udonBrand.begin(), s.udonBrand.end());
    key.push_back((int)s.spotMgr.spots.size());
    for (const auto& spot : s.spotMgr.spots)
        key.insert(key.end(), {spot.pos, spot.brand, spot.stock});
    key.push_back((int)s.agentMgr.agents.size());
    for (const auto& a : s.agentMgr.agents) {
        key.insert(key.end(), {a.pos, a.fuel, (int)a.kind, a.nextPos,
            a.nextFuel, a.moveEndStep, (int)a.visitedSpotPos.size()});
        key.insert(key.end(), a.visitedSpotPos.begin(), a.visitedSpotPos.end());
        auto actions = a.actions;
        key.push_back((int)actions.size());
        while (!actions.empty()) { key.push_back(actions.front()); actions.pop(); }
    }
    return key;
}
struct StateKeyHash {
    size_t operator()(const std::vector<int>& key) const {
        size_t hash = 1469598103934665603ULL;
        for (int x : key) { hash ^= (unsigned)x; hash *= 1099511628211ULL; }
        return hash;
    }
};
