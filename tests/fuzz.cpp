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
    void onDelta(const typename V::Delta &d) {
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
        checkBits(d, seen);
        check();
    }
    // The bit form of a delta must describe exactly the index spans
    static void checkBits(const typename V::Delta &d,
                          const std::set<Index> &inSpans) {
        std::set<Index> touched, words;
        for (Index w : d.touchedWords) {
            assert(words.insert(w).second);
            assert(d.touched.words()[w] != 0);
        }
        for (std::size_t i = 0; i < d.touched.size(); ++i) {
            if (!d.touched.test(i))
                continue;
            touched.insert(Index(i));
            assert(words.count(Index(i / bitWordBits)));
            assert(d.changes[i] != SlotChange::untouched);
            assert(inSpans.count(Index(i)) ==
                   (d.changes[i] != SlotChange::nothing));
        }
        for (Index i : inSpans)
            assert(touched.count(i));
        for (Index i : d.replaced)
            assert(d.changes[i] == SlotChange::replaced);
        checkPadding(d.touched);
    }
    // Bits past size() must stay zero; word-level code relies on it
    static void checkPadding(BitSpan bits) {
        for (std::size_t i = bits.size(); i < bits.words().size() * bitWordBits;
             ++i)
            assert(!((bits.words()[i / bitWordBits] >> (i % bitWordBits)) & 1));
    }
    void check() const {
        std::map<Index, int> actual;
        for (auto it = view.begin(); it != view.end(); ++it)
            actual[it.index()] = *it;
        assert(actual == model);
        assert(view.activeCount() == model.size());
        const BitSpan taken = view.takenBits();
        assert(taken.size() == view.slotCount());
        for (Index i = 0; i < view.slotCount(); ++i)
            assert(taken.test(i) == view.isTaken(i));
        checkPadding(taken);
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
    store->addInterests(SlotInterest::ALL);
    evens->addInterests(SlotInterest::ALL);
    bigEvens->addInterests(SlotInterest::ALL);

    // A bulk start spreads indices over several words, so both onDelta paths
    // (per index and per word) run
    std::vector<Index> live{0, 1};
    for (int k = 0; k < 300; ++k)
        live.push_back(store->emplace(int(rng() % 100)));
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
    store->removeInterests(SlotInterest::ALL);
    evens->removeInterests(SlotInterest::ALL);
    bigEvens->removeInterests(SlotInterest::ALL);

    // collect chain: dropping bigEvens frees it and evens after 2 passes
    std::weak_ptr<decltype(evens)::element_type> we = evens;
    evens.reset();
    bigEvens.reset();
    ledger.collect();
    assert(we.expired());

    // Interests (the filters are gone, so only `rec` holds any): counted
    // per flag, and only interesting members are filled. The delta is read
    // inside the call: its spans die with the flush
    {
        namespace SI = SlotInterest;
        struct Rec {
            Index e = 0;
            int n = 0;
            std::vector<Index> created;
            std::size_t others = 0; // total size of the other members
            bool touchedE = false;
            void onDelta(const Store::Delta &d) {
                ++n;
                created.assign(d.created.begin(), d.created.end());
                others = d.deleted.size() + d.replaced.size() +
                         d.modified.size() + d.touchedWords.size() +
                         d.changes.size();
                touchedE = e < d.touched.size() && d.touched.test(e);
            }
        } rec;
        store->addListener(&rec);
        store->addInterests(SI::CREATED | SI::TOUCHED);
        store->addInterests(SI::CREATED);
        store->removeInterests(SI::CREATED);
        assert(store->interests() == (SI::CREATED | SI::TOUCHED));
        rec.e = store->emplace(1);
        store->erase(live.back());
        live.pop_back();
        pull(*store);
        assert(rec.n == 1 && rec.created == std::vector{rec.e});
        assert(rec.others == 0 && rec.touchedE);

        // Counts only: still notified, every member empty
        store->removeInterests(SI::CREATED | SI::TOUCHED);
        assert(store->interests() == 0);
        store->modify(rec.e) = 3;
        pull(*store);
        assert(rec.n == 2 && rec.created.empty() && rec.others == 0 &&
               !rec.touchedE);
        store->removeListener(&rec);
    }
    std::puts("FUZZ_OK");
}
