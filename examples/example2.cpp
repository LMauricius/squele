// Sorting demo: a sorted view of a store, and a filter over it that follows
// the sort's reordering without checking any item again.
// Asserts serve as a self-check; build without NDEBUG.

#include "squele/core/ledger.hpp"
#include "squele/queries/filter.hpp"
#include "squele/queries/sort.hpp"
#include "squele/views/slotStore.hpp"

#include <cassert>
#include <cstdio>
#include <memory>

using namespace squele;

// Counts its calls, to show which changes make the filter check an item
struct IsEven {
    static inline int calls = 0;
    bool operator()(const int &x) const { return ++calls, x % 2 == 0; }
};

// Prints every item a view currently holds, in its index order
template <class V> void print(const char *name, const V &view) {
    std::printf("%s:", name);
    for (int x : view)
        std::printf(" %d", x);
    std::printf("\n");
}

int main() {
    auto store = std::make_shared<SlotStore<int>>();
    store->emplace(4);
    const auto k_seven = store->emplace(7);
    store->emplace(2);
    store->emplace(9);

    // A sort holds the items at dense positions 0..count-1, in order. A
    // filter over it keeps that order, since it reuses the sort's positions
    Ledger ledger;
    auto sorted = ledger.get(Sort{From{store}});
    auto evens = ledger.get(Filter{Sort{From{store}}, IsEven{}});
    print("sorted", *sorted); // sorted: 2 4 7 9
    print("evens", *evens);   // evens: 2 4

    // A new smallest item moves every other item one position up. The sort
    // reports the moves as swaps, so the filter moves its flags along and
    // checks only the new item
    IsEven::calls = 0;
    store->emplace(0);
    evens->sync();
    print("sorted", *sorted); // sorted: 0 2 4 7 9
    print("evens", *evens);   // evens: 0 2 4
    assert(IsEven::calls == 1);

    // A modified item is checked again, wherever it moves to; the items it
    // passes are only swapped
    IsEven::calls = 0;
    store->modify(k_seven) = 1;
    evens->sync();
    print("sorted", *sorted); // sorted: 0 1 2 4 9
    print("evens", *evens);   // evens: 0 2 4
    assert(IsEven::calls == 1);

    return 0;
}
