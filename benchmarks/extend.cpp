// Extend view benchmark: an extend of one source and a function against a
// map plus source reads by index, and a zip of two sorts against reading
// both by position; construction, deltas, and one full read pass each.
// Prints the best of several runs per case.
#include "bench.hpp"

#include "squele/core/ledger.hpp"
#include "squele/queries/extend.hpp"
#include "squele/queries/map.hpp"
#include "squele/queries/sort.hpp"
#include "squele/views/slotStore.hpp"

#include <cstdio>
#include <memory>
#include <random>
#include <vector>

using namespace squele;
using Store = SlotStore<int>;
using Index = Store::Index;

struct Twice {
    int operator()(const int &x) const { return 2 * x + 1; }
};
struct Greater {
    bool operator()(const int &a, const int &b) const { return a > b; }
};

constexpr Index items = 1 << 20;

int main() {
    std::printf("-- %u items\n", items);
    std::mt19937 rng(1);
    auto value = [&] { return int(rng() % 1000); };
    auto store = std::make_shared<Store>();
    std::vector<Index> live;
    for (Index i = 0; i < items; ++i)
        live.push_back(store->emplace(value()));
    Ledger ledger;
    auto none = [] {};

    // One source and a function
    bench("construct map", none,
          [&] { g_sink = Map{From{store}, Twice{}}.result(ledger)->count(); });
    bench("construct extend", none, [&] {
        g_sink = Extend{From{store}, Twice{}}.result(ledger)->count();
    });
    auto map = ledger.get(Map{From{store}, Twice{}});
    auto ext = ledger.get(Extend{From{store}, Twice{}});
    auto touch = [&](std::size_t n) {
        for (std::size_t k = 0; k < n; ++k)
            store->modify(live[rng() % live.size()]) = value();
    };
    for (const std::size_t n : {std::size_t{64}, std::size_t{items / 10}}) {
        char name[64];
        std::snprintf(name, sizeof name, "map: %zu modified", n);
        bench(name, [&] { touch(n); }, [&] { pull(*map); });
        std::snprintf(name, sizeof name, "extend: %zu modified", n);
        bench(name, [&] { touch(n); }, [&] { pull(*ext); });
    }
    pull(*map);
    pull(*ext);
    bench("read map + source by index", none, [&] {
        long long s = 0;
        for (auto it = map->begin(); it != map->end(); ++it)
            s += (*store)[it.index()] + *it;
        g_sink = s;
    });
    bench("read extend", none, [&] {
        long long s = 0;
        for (auto it = ext->begin(); it != ext->end(); ++it)
            s += *(*it).first + (*it).second;
        g_sink = s;
    });

    // Two dense sources
    auto up = ledger.get(Sort{From{store}});
    auto down = ledger.get(Sort{From{store}, Greater{}});
    auto zip =
        ledger.get(Extend{Sort{From{store}}, Sort{From{store}, Greater{}}});
    bench("zip: 64 modified", [&] { touch(64); }, [&] { pull(*zip); });
    bench(
        "sorts alone: 64 modified", [&] { touch(64); },
        [&] {
            pull(*up);
            pull(*down);
        });
    pull(*zip);
    bench("read sorts by position", none, [&] {
        long long s = 0;
        for (Index q = 0; q < up->count(); ++q)
            s += (*up)[q] - (*down)[q];
        g_sink = s;
    });
    bench("read zip", none, [&] {
        long long s = 0;
        for (auto it = zip->begin(); it != zip->end(); ++it)
            s += *(*it).first - *(*it).second;
        g_sink = s;
    });
}
