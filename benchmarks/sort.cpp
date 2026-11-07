// Sort view benchmark: construction (a full sort), incremental deltas of
// growing size (to compare against it and to tune the merge modes), shifts
// that swap every later position, and views over a sort following its swaps.
// Prints the best of several runs per case.
#include "bench.hpp"

#include "squele/core/ledger.hpp"
#include "squele/queries/filter.hpp"
#include "squele/queries/sort.hpp"
#include "squele/views/slotStore.hpp"

#include <cstdio>
#include <memory>
#include <random>
#include <vector>

using namespace squele;
using Store = SlotStore<int>;
using Index = Store::Index;

struct IsEven {
    bool operator()(const int &x) const { return x % 2 == 0; }
};
struct Greater {
    bool operator()(const int &a, const int &b) const { return a > b; }
};

constexpr Index items = 1 << 20;

/**
 * Runs every case on `items` items with values below `range` (a small range
 * gives many equal items)
 */
void suite(const char *kind, unsigned range) {
    std::printf("-- %s: %u items, values below %u\n", kind, items, range);
    std::mt19937 rng(1);
    auto value = [&] { return int(rng() % range); };
    auto store = std::make_shared<Store>();
    std::vector<Index> live;
    for (Index i = 0; i < items; ++i)
        live.push_back(store->emplace(value()));

    Ledger ledger;
    auto none = [] {};
    bench("construct sort (full sort)", none,
          [&] { g_sink = Sort{From{store}}.result(ledger)->count(); });

    // A filter listens to the sort, so the sort records its output
    auto sorted = ledger.get(Sort{From{store}});
    auto evens = ledger.get(Filter{Sort{From{store}}, IsEven{}});
    auto touch = [&](std::size_t n) {
        for (std::size_t k = 0; k < n; ++k)
            store->modify(live[rng() % live.size()]) = value();
    };
    // Around the merge switch (k * bit_width(n) >= n, about n / 20 here)
    for (const std::size_t n :
         {std::size_t{64}, std::size_t{items / 100}, std::size_t{items / 40},
          std::size_t{items / 20}, std::size_t{items / 10},
          std::size_t{items}}) {
        char name[64];
        std::snprintf(name, sizeof name, "delta: %zu modified", n);
        bench(name, [&] { touch(n); }, [&] { pull(*evens); });
    }
    bench(
        "delta: 64 created, 64 erased",
        [&] {
            for (int k = 0; k < 64; ++k) {
                const std::size_t at = rng() % live.size();
                store->erase(live[at]);
                live[at] = store->emplace(value());
            }
        },
        [&] { pull(*evens); });
    // Before every item, so every position shifts by one swap
    int front = -1;
    bench(
        "delta: 1 created at the front",
        [&] { live.push_back(store->emplace(front--)); },
        [&] { pull(*evens); });
    bench(
        "delta: 1 created, no listener",
        [&] { live.push_back(store->emplace(front--)); },
        [&] { pull(*sorted); });

    // A sort over the sort follows its swaps with no comparison
    auto down = ledger.get(Sort{Sort{From{store}}, Greater{}});
    bench(
        "sort of sort: 64 modified", [&] { touch(64); }, [&] { pull(*down); });
    g_sink = sorted->count() + evens->count() + down->count();
}

int main() {
    suite("distinct", ~0u);
    suite("ties", 100);
}
