// Basic demo: a filter view over a slot store, shared through a ledger.
// Asserts serve as a self-check; build without NDEBUG.

#include "squele/core/ledger.hpp"
#include "squele/views/filter.hpp"
#include "squele/views/slotStore.hpp"

#include <algorithm>
#include <cassert>
#include <concepts>
#include <iterator>
#include <memory>
#include <span>
#include <vector>

using namespace squele;

using Store = SlotStore<int>;
using Index = Store::Index;
using Indices = std::vector<Index>;

static_assert(Original<Store> && SlotViewable<Store>);
static_assert(Queryable<Filter<From<Store>>>);
static_assert(std::same_as<Filter<From<Store>>::View, SlotFilterView<Store>>);
static_assert(std::forward_iterator<Store::Iterator>);

/**
 * A named function, so every Filter built from it is the same cache key
 */
bool isEven(const int &x) { return x % 2 == 0; }

/**
 * An empty functor: it is part of the query type, so its calls are inlined
 * into the view, and every Filter built from it is the same cache key
 */
struct IsPositive {
    bool operator()(const int &x) const { return x > 0; }
};
static_assert(sizeof(Filter<From<Store>, IsPositive>) == sizeof(From<Store>));

/**
 * Records the last delta, with sorted copies of its unordered spans
 */
struct Recorder {
    int deltas = 0;
    Index count = 0;
    Indices created, deleted, replaced, modified;

    void onDelta(const Store::Delta &d) {
        ++deltas;
        count = d.count;
        created = sorted(d.created);
        deleted = sorted(d.deleted);
        replaced = sorted(d.replaced);
        modified = sorted(d.modified);
    }

    static Indices sorted(std::span<const Index> s) {
        Indices v(s.begin(), s.end());
        std::ranges::sort(v);
        return v;
    }
};

/**
 * @returns the items of a slot view, in index order
 */
template <SlotViewable V> std::vector<int> items(const V &view) {
    return {view.begin(), view.end()};
}

int main() {
    // 1. A store and a ledger
    auto store = std::make_shared<Store>();
    Ledger ledger;

    // 2. Equal queries share one view
    auto evens = ledger.get(Filter{From{store}, isEven});
    assert(evens == ledger.get(Filter{From{store}, isEven}));
    static_assert(Dependent<decltype(evens)::element_type, Store>);
    auto positives = ledger.get(Filter{From{store}, IsPositive{}});
    assert(positives == ledger.get(Filter{From{store}, IsPositive{}}));

    // 3. A recording listener
    Recorder rec;
    evens->addListener(&rec);
    // A listener asks for the delta members it reads; the rest stay empty
    constexpr auto recorded = SlotInterest::CREATED | SlotInterest::DELETED |
                              SlotInterest::REPLACED | SlotInterest::MODIFIED;
    evens->addInterests(recorded);

    // Brings the filter up to date (pulling the store) and flushes it
    auto syncAndFlush = [&] {
        assert(evens->dirty());
        evens->sync();
        assert(!evens->dirty());
        evens->flush();
    };

    // 4a. Add items
    const Index a = store->emplace(1);
    const Index b = store->emplace(2);
    const Index c = store->emplace(4);
    syncAndFlush();
    assert(rec.deltas == 1 && rec.count == 2);
    assert((rec.created == Indices{b, c}));
    assert((items(*evens) == std::vector{2, 4}));
    positives->sync();
    assert((items(*positives) == std::vector{1, 2, 4}));

    // A flush with nothing to report notifies nobody
    evens->flush();
    assert(rec.deltas == 1);

    // 4b. Modify items: odd -> even, even -> odd, even -> even
    store->modify(a) = 6;
    store->modify(b) = 3;
    store->modify(c) = 8;
    syncAndFlush();
    assert((rec.created == Indices{a} && rec.deleted == Indices{b} &&
            rec.modified == Indices{c}));
    assert((items(*evens) == std::vector{6, 8}));

    // 4c. Erase an item
    store->erase(c);
    syncAndFlush();
    assert((rec.deleted == Indices{c} && rec.created.empty()));
    assert((items(*evens) == std::vector{6}));

    // 5. Freed indices are reused, last freed first; an erase and a create at
    // the same index coalesce into a replace
    store->erase(a);
    assert(store->emplace(10) == a);
    assert(store->emplace(12) == c);
    syncAndFlush();
    assert((rec.replaced == Indices{a} && rec.created == Indices{c}));
    assert((items(*evens) == std::vector{10, 12}));
    // A view not read for a while catches up on its next sync()
    positives->sync();
    assert((items(*positives) == std::vector{10, 3, 12}));
    assert(store->count() == 3 && store->slotCount() == 3);

    // 6. A view nobody uses is released by collect()
    evens->removeListener(&rec);
    evens->removeInterests(recorded);
    std::weak_ptr<Filter<From<Store>>::View> weak = evens;
    evens.reset();
    ledger.collect();
    assert(weak.expired());

    return 0;
}
