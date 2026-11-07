// Shared timing helpers of the benchmarks
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>

constexpr int runs = 7;

/**
 * Keeps results alive, so the compiler cannot drop the measured work
 */
inline volatile long long g_sink;

/**
 * Prints the best time of `runs` calls of `setup()` then `work()`; only
 * `work()` is timed
 */
template <class S, class W> void bench(const char *name, S setup, W work) {
    double best = 1e300;
    for (int r = 0; r < runs; ++r) {
        setup();
        const auto t0 = std::chrono::steady_clock::now();
        work();
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(
            best, std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    std::printf("%-34s %12.1f us\n", name, best);
}
