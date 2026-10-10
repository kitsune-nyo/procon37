#pragma once
#include <algorithm>
#include "state_key.h"

class StateQueue {
    struct Entry { State state; size_t serial; size_t assigned; };
    struct Earlier {
        bool operator()(const Entry& a, const Entry& b) const {
            if (a.state.step != b.state.step) return a.state.step > b.state.step;
            if (a.state.score != b.state.score) return a.state.score > b.state.score;
            // Finish assigning vehicles before broadening equal-score choices.
            // This only resolves ties; step and evaluation remain primary.
            if (a.assigned != b.assigned) return a.assigned < b.assigned;
            return a.serial > b.serial;
        }
    };
    struct Worse {
        bool operator()(const Entry& a, const Entry& b) const {
            if (a.state.step != b.state.step) return a.state.step > b.state.step;
            if (a.state.score != b.state.score) return a.state.score > b.state.score;
            if (a.assigned != b.assigned) return a.assigned > b.assigned;
            return a.serial < b.serial;
        }
    };
    std::vector<Entry> pending, ready;
    std::unordered_set<std::vector<int>, StateKeyHash> seen;
    size_t serial = 0;
    int barrier = INT_MAX;
    size_t capacity = SIZE_MAX;
    template<class Compare> static Entry takeEntry(std::vector<Entry>& heap, Compare compare) {
        std::pop_heap(heap.begin(), heap.end(), compare);
        Entry entry = std::move(heap.back());
        heap.pop_back();
        return entry;
    }
    bool usePending() const {
        return !pending.empty() && (ready.empty() || pending.front().state.step < ready.front().state.step);
    }
public:
    bool empty() const { return pending.empty() && ready.empty(); }
    size_t size() const { return pending.size() + ready.size(); }
    const State& top() const { return (usePending() ? pending : ready).front().state; }
    void beginStep(int step, size_t width) {
        seen.clear();
        barrier = step; capacity = std::max<size_t>(1, width);
        // Rebuild once per simulation step. Partial assignments use a buffered
        // top-K selection, preventing exponential Cartesian products for 4-7
        // vehicles. Same-score ties prefer deeper assignments, then earlier
        // insertion; complete-frontier ties retain the earlier insertion too.
        while (!ready.empty()) pending.push_back(takeEntry(ready, Worse{}));
        std::make_heap(pending.begin(), pending.end(), Earlier{});
    }
    bool push(State&& state) {
        SEARCH_COUNT(searchProfile.reached = std::max(searchProfile.reached, state.step));
        size_t assigned = 0;
        for (const auto& a : state.agentMgr.agents)
            if (!a.actions.empty() || a.nextPos != -1) ++assigned;
        Entry entry{std::move(state), serial++, assigned};
        auto& heap = entry.state.step < barrier ? pending : ready;
        if (&heap == &ready && heap.size() >= capacity) {
            if (!Worse{}(entry, heap.front())) return false;
        }
        if (!seen.insert(stateKey(entry.state)).second) {
            SEARCH_COUNT(++searchProfile.duplicates);
            return false;
        }
        if (&heap == &ready && heap.size() >= capacity) {
            auto discarded = takeEntry(heap, Worse{});
        }
        heap.push_back(std::move(entry));
        if (&heap == &ready) std::push_heap(heap.begin(), heap.end(), Worse{});
        else {
            std::push_heap(heap.begin(), heap.end(), Earlier{});
            if (capacity != SIZE_MAX && heap.size() > capacity * 2) {
                auto better = [](const Entry& a, const Entry& b) { return Worse{}(a,b); };
                std::nth_element(heap.begin(), heap.begin()+capacity, heap.end(), better);
                heap.erase(heap.begin()+capacity, heap.end());
                std::make_heap(heap.begin(), heap.end(), Earlier{});
            }
        }
        SEARCH_COUNT(searchProfile.peak = std::max(searchProfile.peak, size()));
        return true;
    }
    State take() {
        if (usePending()) return std::move(takeEntry(pending, Earlier{}).state);
        return std::move(takeEntry(ready, Worse{}).state);
    }
    void pop() { auto discarded = take(); }
};
