// Fuzz test: random store mutations checked against a naive model, through
// nested filters pulled from random chain levels; deltas are replayed into the
// model. Asserts are the checks; build without NDEBUG.
#include "squele/core/ledger.hpp"
#include "squele/views/filter.hpp"
#include "squele/views/slotStore.hpp"

#include <cassert>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <span>

using namespace squele;
using Store = SlotStore<int>;
using Index = Store::Index;

bool isEven(const int &x) { return x % 2 == 0; }
bool isBig(const int &x) { return x > 50; }

// Replays deltas into a model set; checks it matches the view after each flush
template <class V> struct Mirror {
    const V &view;
    std::map<Index, int> model; // index -> value snapshot
    explicit Mirror(const V &v) : view(v) {
        for (auto it = v.begin(); it != v.end(); ++it)
            model[it.index()] = *it;
    }
    void onDelta(const SlotDelta<Index> &d) {
        std::set<Index> seen;
        auto once = [&](Index i) { assert(seen.insert(i).second); };
        for (Index i : d.deleted) {
            once(i);
            assert(model.erase(i) == 1);
        }
        for (Index i : d.created) {
            once(i);
            assert(!model.count(i));
            model[i] = view[i];
        }
        for (Index i : d.replaced) {
            once(i);
            assert(model.count(i));
            model[i] = view[i];
        }
        for (Index i : d.modified) {
            once(i);
            assert(model.count(i));
            model[i] = view[i];
        }
        assert(d.activeCount == model.size());
        check();
    }
    void check() const {
        std::map<Index, int> actual;
        for (auto it = view.begin(); it != view.end(); ++it)
            actual[it.index()] = *it;
        assert(actual == model);
        assert(view.activeCount() == model.size());
    }
};

int main() {
    std::mt19937 rng(42);
    auto store = std::make_shared<Store>();
    Ledger ledger;
    auto evens = ledger.get(Filter{From{store}, isEven});
    auto bigEvens = ledger.get(Filter{Filter{From{store}, isEven}, isBig});
    static_assert(SlotViewable<decltype(bigEvens)::element_type>);

    // Mutations before anyone listens are not recorded: the mirrors below
    // snapshot the views and must receive no delta for these items
    store->emplace(2);
    store->emplace(3);
    pull(*bigEvens);
    assert(evens->activeCount() == 1 && bigEvens->activeCount() == 0);

    Mirror<Store> ms(*store);
    Mirror<decltype(evens)::element_type> me(*evens);
    Mirror<decltype(bigEvens)::element_type> mb(*bigEvens);
    store->addListener(&ms);
    evens->addListener(&me);
    bigEvens->addListener(&mb);

    std::vector<Index> live{0, 1};
    for (int round = 0; round < 2000; ++round) {
        const int ops = int(rng() % 8);
        for (int k = 0; k < ops; ++k) {
            const int op = int(rng() % 3);
            if (live.empty() || op == 0) {
                live.push_back(store->emplace(int(rng() % 100)));
            } else if (op == 1) {
                size_t j = rng() % live.size();
                store->erase(live[j]);
                live[j] = live.back();
                live.pop_back();
            } else {
                store->modify(live[rng() % live.size()]) = int(rng() % 100);
            }
        }
        if (ops)
            assert(evens->dirty() && bigEvens->dirty());
        // Randomly pull from different levels
        switch (rng() % 3) {
        case 0:
            pull(*bigEvens);
            break;
        case 1:
            pull(*evens);
            pull(*bigEvens);
            break;
        case 2:
            pull(*store);
            pull(*evens);
            pull(*bigEvens);
            break;
        }
        assert(!evens->dirty() && !bigEvens->dirty());
        ms.check();
        me.check();
        mb.check();
        // model cross-check
        for (auto &[i, v] : ms.model) {
            assert(me.model.count(i) == isEven(v));
            assert(mb.model.count(i) == (isEven(v) && isBig(v)));
        }
    }
    store->removeListener(&ms);
    evens->removeListener(&me);
    bigEvens->removeListener(&mb);

    // collect chain: dropping bigEvens frees it and evens after 2 passes
    std::weak_ptr<decltype(evens)::element_type> we = evens;
    evens.reset();
    bigEvens.reset();
    ledger.collect();
    assert(we.expired());
    std::puts("FUZZ_OK");
}
