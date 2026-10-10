#pragma once
#include <chrono>
#include <cstddef>

// Detailed clocks are opt-in; normal competition builds pay no profiling cost.
struct SearchProfile {
    size_t copies = 0, expanded = 0, decisions = 0, candidates = 0, duplicates = 0, peak = 0;
    int reached = 0;
    double copySeconds = 0, separateSeconds = 0, evaluateSeconds = 0;
};
inline SearchProfile searchProfile;
#ifdef HEXUDON_PROFILE
struct SearchProfileTimer {
    double& result;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    explicit SearchProfileTimer(double& value) : result(value) {}
    ~SearchProfileTimer() {
        result += std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    }
};
#define SEARCH_COUNT(expression) do { expression; } while (false)
#define SEARCH_TIMER(field) SearchProfileTimer profileTimer(searchProfile.field)
#else
#define SEARCH_COUNT(expression) do {} while (false)
#define SEARCH_TIMER(field) do {} while (false)
#endif
