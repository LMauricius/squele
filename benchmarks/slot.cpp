// Slot view benchmark: iteration, filter construction and delta handling on
// dense and sparse stores. Prints the best of several runs per case.
#include "squele/core/ledger.hpp"
#include "squele/views/filter.hpp"
#include "squele/views/slotStore.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

using namespace squele;
using Store = SlotStore<int>;
using Index = Store::Index;
using Evens = Filter<From<Store>>;
using Bigs = Filter<Evens>;

bool isEven(const int &x) { return x % 2 == 0; }
bool isBig(const int &x) { return x > 50; }

constexpr Index slots = 1 << 20;
constexpr int runs = 7;

/**
 * Keeps results alive, so the compiler cannot drop the measured work
 */
volatile long long g_sink;

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

/**
 * Sums the items of a view through its iterator
 */
template <class V> long long sum(const V &v) {
    long long s = 0;
    for (const int &x : v)
        s += x;
    return s;
}

/**
 * Runs every case on a store with `slots` slots, keeping `keep` items
 */
void suite(const char *kind, Index keep) {
    std::printf("-- %s: %u slots, %u items\n", kind, slots, keep);
    std::mt19937 rng(1);
    auto store = std::make_shared<Store>();
    std::vector<Index> live;
    for (Index i = 0; i < slots; ++i)
        live.push_back(store->emplace(int(rng() % 100)));
    std::shuffle(live.begin(), live.end(), rng);
    while (live.size() > keep) {
        store->erase(live.back());
        live.pop_back();
    }

    Ledger ledger;
    auto bigs = ledger.get(Bigs{Evens{From{store}, isEven}, isBig});
    auto evens = ledger.get(Evens{From{store}, isEven});

    auto none = [] {};
    bench("iterate store", none, [&] { g_sink = sum(*store); });
    bench("iterate filter", none, [&] { g_sink = sum(*evens); });
    bench("construct filter", none, [&] {
        g_sink = Evens{From{store}, isEven}.result(ledger)->activeCount();
    });

    // Small delta: a few scattered modifications through two filter levels
    auto touch = [&](std::size_t n) {
        for (std::size_t k = 0; k < n; ++k)
            store->modify(live[rng() % live.size()]) = int(rng() % 100);
    };
    bench("delta: 64 modified", [&] { touch(64); }, [&] { pull(*bigs); });
    bench(
        "delta: all modified", [&] { touch(live.size()); },
        [&] { pull(*bigs); });
    // Bulk churn: erase and re-create every item, so the delta is replaced
    bench(
        "delta: all replaced",
        [&] {
            for (Index &i : live) {
                store->erase(i);
                i = store->emplace(int(rng() % 100));
            }
        },
        [&] { pull(*bigs); });
}

int main() {
    suite("dense", slots);
    suite("sparse", 1000);
}
