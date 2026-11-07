// Slot view benchmark: iteration, filter construction and delta handling on
// dense and sparse stores. Prints the best of several runs per case.
#include "squele/core/ledger.hpp"
#include "squele/views/filter.hpp"
#include "squele/views/slotStore.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <utility>
#include <vector>

using namespace squele;
using Store = SlotStore<int>;
using Index = Store::Index;
using Evens = Filter<From<Store>>;
using Bigs = Filter<Evens>;

bool isEven(const int &x) { return x % 2 == 0; }
bool isBig(const int &x) { return x > 50; }
// The same predicates as empty functors, which the views inline
struct IsEven {
    bool operator()(const int &x) const { return x % 2 == 0; }
};
struct IsBig {
    bool operator()(const int &x) const { return x > 50; }
};
using EvensF = Filter<From<Store>, IsEven>;
using BigsF = Filter<EvensF, IsBig>;
// Distinct predicates for the tree cases: equal predicates would share a view
template <int K> bool notMultiple(const int &x) { return x % (K + 2) != 0; }

/**
 * The query of a chain of `K` filters over `q`, one per level
 */
template <int K, class Q> auto chain(Q q) {
    if constexpr (K == 0)
        return q;
    else
        return chain<K - 1>(Filter<Q>{q, notMultiple<K>});
}

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
    bench("construct filter", none,
          [&] { g_sink = Evens{From{store}, isEven}.result(ledger)->count(); });

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

    // Trees: a chain of 8 filters, and 32 sibling filters of which one is
    // pulled (the others update eagerly all the same)
    auto leaf = ledger.get(chain<8>(From{store}));
    bench("depth 8: 64 modified", [&] { touch(64); }, [&] { pull(*leaf); });
    bench(
        "depth 8: all modified", [&] { touch(live.size()); },
        [&] { pull(*leaf); });
    leaf.reset();
    ledger.collect();
    auto fans = [&]<int... K>(std::integer_sequence<int, K...>) {
        return std::array{ledger.get(Evens{From{store}, notMultiple<K>})...};
    }(std::make_integer_sequence<int, 32>{});
    bench(
        "fan-out 32: 64 modified, pull one", [&] { touch(64); },
        [&] { pull(*fans[0]); });
    bench(
        "fan-out 32: all modified, pull one", [&] { touch(live.size()); },
        [&] { pull(*fans[0]); });

    // Functor predicates: the pointer views are released first, so only the
    // functor views listen to the store
    fans = {};
    bigs.reset();
    evens.reset();
    ledger.collect();
    bench("construct filter, functor", none, [&] {
        g_sink = EvensF{From{store}, IsEven{}}.result(ledger)->count();
    });
    auto bigsF = ledger.get(BigsF{EvensF{From{store}, IsEven{}}, IsBig{}});
    bench(
        "delta functor: all modified", [&] { touch(live.size()); },
        [&] { pull(*bigsF); });
}

/**
 * A 1 KB item: one per deque block in libstdc++, many per chunk
 */
struct Large {
    int value;
    char payload[1020];
};
bool isEvenLarge(const Large &x) { return x.value % 2 == 0; }

/**
 * Fill, iteration and filter construction with large items
 */
void largeSuite() {
    using LargeStore = SlotStore<Large>;
    constexpr Index count = 1 << 16;
    std::printf("-- large items: %u items of %zu bytes\n", count,
                sizeof(Large));
    std::shared_ptr<LargeStore> store;
    bench(
        "fill store", [&] { store = std::make_shared<LargeStore>(); },
        [&] {
            for (Index i = 0; i < count; ++i)
                store->emplace(Large{int(i), {}});
        });
    bench(
        "iterate store", [] {},
        [&] {
            long long s = 0;
            for (const Large &x : *store)
                s += x.value;
            g_sink = s;
        });
    Ledger ledger;
    bench(
        "construct filter", [] {},
        [&] {
            g_sink = Filter{From{store}, isEvenLarge}.result(ledger)->count();
        });
}

int main() {
    // First, so its fill measures fresh pages in both storage layouts
    largeSuite();
    suite("dense", slots);
    suite("sparse", 1000);
}
