// Minimal demo: store items, query them through a ledger, watch views update.
// See example1.cpp for listeners, deltas and memory release.

#include "squele/core/ledger.hpp"
#include "squele/views/filter.hpp"
#include "squele/views/slotStore.hpp"

#include <cstdio>
#include <memory>

using namespace squele;

bool isEven(const int &x) { return x % 2 == 0; }

// Prints every item a view currently holds
template <class V> void print(const char *name, const V &view) {
    std::printf("%s:", name);
    for (int x : view)
        std::printf(" %d", x);
    std::printf("\n");
}

int main() {
    // A store holds the original items; each item gets a stable index
    auto store = std::make_shared<SlotStore<int>>();
    auto k_one = store->emplace(1);
    store->emplace(2);
    store->emplace(3);
    auto k_four = store->emplace(4);

    // A ledger answers queries; asking the same query twice returns the same
    // view, so the work is done once
    Ledger ledger;
    auto evens = ledger.get(Filter{From{store}, isEven});
    if (evens != ledger.get(Filter{From{store}, isEven}))
        return 1;
    print("store", *store); // store: 1 2 3 4
    print("evens", *evens); // evens: 2 4

    // Change the store; views update incrementally on their next sync()
    store->modify(k_one) = 10;
    store->erase(k_four);
    store->emplace(6);
    evens->sync();
    print("store", *store); // store: 10 2 3 6
    print("evens", *evens); // evens: 10 2 6

    return 0;
}
