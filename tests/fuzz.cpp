// Fuzz test: random store mutations checked against a naive model, through
// nested filters pulled from random chain levels; deltas are replayed into the
// model. Asserts are the checks; build without NDEBUG.
#include "squele/bits/order.hpp"
#include "squele/core/ledger.hpp"
#include "squele/views/filter.hpp"
#include "squele/views/slotStore.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <span>
#include <vector>

using namespace squele;
using Store = SlotStore<int>;
using Index = Store::Index;

bool isEven(const int &x) { return x % 2 == 0; }
bool isBig(const int &x) { return x > 50; }

// Replays deltas into a model set; checks it matches the view after each flush
template <class V> struct Mirror {
    const V &view;
    std::map<Index, int> model; // index -> value snapshot
    bool relocated = false;     // seen in a delta since last reset
    explicit Mirror(const V &v) : view(v) {
        for (auto it = v.begin(); it != v.end(); ++it)
            model[it.index()] = *it;
    }
    void onDelta(const typename V::Delta &d) {
        relocated |= d.relocated;
        // Swaps first, in order; the other members describe what follows
        for (const auto &[a, b] : d.swapped) {
            auto na = model.extract(a), nb = model.extract(b);
            if (na) {
                na.key() = b;
                model.insert(std::move(na));
            }
            if (nb) {
                nb.key() = a;
                model.insert(std::move(nb));
            }
        }
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
        assert(d.count == model.size());
        checkBits(d, seen);
        check();
    }
    // The word form of a delta must describe exactly the index spans
    static void checkBits(const typename V::Delta &d,
                          const std::set<Index> &inSpans) {
        std::set<Index> touched, words;
        for (Index w : d.touchedWords) {
            assert(words.insert(w).second);
            const SlotWordChange &c = d.words[w];
            // Every plane is a subset of touched; same of existed & exists
            assert(!((c.existed | c.exists) & ~c.touched));
            assert(!(c.same & ~(c.existed & c.exists)));
            for (std::size_t b = 0; b < bitWordBits; ++b) {
                if (!((c.touched >> b) & 1))
                    continue;
                const Index i = Index(w * bitWordBits + b);
                touched.insert(i);
                assert(i < d.slotCount);
                assert(inSpans.count(i) == (c.kind(b) != SlotChange::nothing));
            }
        }
        assert(touched.size() == d.touchedCount);
        for (Index i : inSpans)
            assert(touched.count(i));
        auto kindOf = [&](Index i) {
            return d.words[i / bitWordBits].kind(i % bitWordBits);
        };
        for (Index i : d.created)
            assert(kindOf(i) == SlotChange::created);
        for (Index i : d.deleted)
            assert(kindOf(i) == SlotChange::deleted);
        for (Index i : d.replaced)
            assert(kindOf(i) == SlotChange::replaced);
        for (Index i : d.modified)
            assert(kindOf(i) == SlotChange::modified);
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
        assert(view.count() == model.size());
        const BitSpan taken = view.takenBits();
        assert(taken.size() == view.slotCount());
        for (Index i = 0; i < view.slotCount(); ++i)
            assert(taken.test(i) == view.isTaken(i));
        checkPadding(taken);
    }
};

/**
 * Records every one- and two-step sequence of changes on one index, and checks
 * the coalesced kind against the table of net changes
 */
void checkCoalescing() {
    enum Op { C, E, M };
    using K = SlotChange;
    struct Case {
        std::vector<Op> ops;
        K kind;
    };
    const Case cases[] = {
        {{C}, K::created},        {{E}, K::deleted},
        {{M}, K::modified},       {{C, M}, K::created},
        {{C, E}, K::nothing},     {{E, C}, K::replaced},
        {{M, M}, K::modified},    {{M, E}, K::deleted},
        {{E, C, M}, K::replaced}, {{E, C, E}, K::deleted},
        {{C, E, C}, K::created},
    };
    for (const Case &c : cases) {
        SlotDiff<Index> diff;
        for (Op op : c.ops)
            op == C   ? diff.create(70)
            : op == E ? diff.erase(70)
                      : diff.modify(70);
        const auto d = diff.delta(0, 128, SlotInterest::ALL);
        assert(d.touchedCount == 1 && d.touchedWords.size() == 1);
        assert(d.words[1].kind(6) == c.kind);
        const std::size_t spans = d.created.size() + d.deleted.size() +
                                  d.replaced.size() + d.modified.size();
        assert(spans == (c.kind != K::nothing));
    }
}

/**
 * Records random valid changes into one diff, and the same changes into
 * another that is flushed at random points with its deltas folded into a
 * third; the folded diff must equal the whole one, plane by plane
 */
void checkFolding(std::mt19937 &rng) {
    constexpr Index n = 300;
    SlotDiff<Index> whole, part, folded;
    std::vector<bool> taken(n);
    for (int step = 0; step < 5000; ++step) {
        const Index i = Index(rng() % n);
        auto both = [&](auto f) {
            f(whole);
            f(part);
        };
        if (const Index j = Index(rng() % n); rng() % 4 == 0 && i != j) {
            both([&](auto &d) { d.swap(i, j); });
            taken.swap(taken[i], taken[j]);
        } else if (!taken[i]) {
            both([&](auto &d) { d.create(i); });
            taken[i] = true;
        } else if (rng() % 2) {
            both([&](auto &d) { d.erase(i); });
            taken[i] = false;
        } else {
            both([&](auto &d) { d.modify(i); });
        }
        if (rng() % 16 == 0) {
            folded.fold(part.delta(0, n, SlotInterest::ALL));
            part.clear();
        }
        if (rng() % 512 == 0) {
            folded.fold(part.delta(0, n, SlotInterest::ALL));
            part.clear();
            const auto a = whole.delta(0, n, SlotInterest::ALL);
            const auto b = folded.delta(0, n, SlotInterest::ALL);
            assert(a.touchedCount == b.touchedCount);
            // Swaps may leave listed words empty, so the lists may differ;
            // the blocks may not
            auto block = [](std::span<const SlotWordChange> ws, std::size_t w) {
                return w < ws.size() ? ws[w] : SlotWordChange{};
            };
            for (std::size_t w = 0; w < n / bitWordBits + 1; ++w) {
                const SlotWordChange x = block(a.words, w),
                                     y = block(b.words, w);
                assert(x.touched == y.touched && x.existed == y.existed &&
                       x.exists == y.exists && x.same == y.same);
            }
            assert(a.swapped.size() == b.swapped.size());
            for (std::size_t k = 0; k < a.swapped.size(); ++k)
                assert(a.swapped[k].a == b.swapped[k].a &&
                       a.swapped[k].b == b.swapped[k].b);
            whole.clear();
            folded.clear();
        }
    }
}

/**
 * Item references must survive store growth past many chunks, for small
 * items and for items larger than a chunk
 */
template <class T> void checkStableItems(int n) {
    SlotStore<T> store;
    std::vector<const T *> refs;
    for (int k = 0; k < n; ++k) {
        const auto i = store.emplace();
        assert(std::size_t(i) == refs.size());
        refs.push_back(&store[i]);
    }
    for (std::size_t i = 0; i < refs.size(); ++i)
        assert(&store[typename SlotStore<T>::Index(i)] == refs[i]);
}

/**
 * A test-only original that swaps items (no real view does yet): a fixed
 * number of slots, each toggled between free and taken by hand
 */
class SwapSource : public Broadcaster<SlotDelta<Index>> {
  public:
    using Item = int;
    using Index = ::Index;
    using Delta = SlotDelta<Index>;
    using Query = From<SwapSource>;
    using Iterator = SlotIterator<SwapSource>;

    explicit SwapSource(Index n) : m_items(n) { m_taken.resize(n); }

    void toggle(Index i, int v) {
        record();
        if (isTaken(i)) {
            m_taken.reset(i);
            m_diff.erase(i);
        } else {
            m_taken.set(i);
            m_items[i] = v;
            m_diff.create(i);
        }
    }
    void modify(Index i, int v) {
        assert(isTaken(i));
        record();
        m_items[i] = v;
        m_diff.modify(i);
    }
    // Items do not really move; a relocation only tests forwarding
    void relocate() {
        record();
        m_diff.relocate();
    }
    void swap(Index a, Index b) {
        record();
        m_diff.swap(a, b);
        std::swap(m_items[a], m_items[b]);
        const bool ta = isTaken(a), tb = isTaken(b);
        ta ? m_taken.set(b) : m_taken.reset(b);
        tb ? m_taken.set(a) : m_taken.reset(a);
    }
    void flush() {
        if (m_diff.empty())
            return;
        broadcast(m_diff.delta(count(), slotCount(), interests()));
        m_diff.clear();
    }

    const int &operator[](Index i) const { return m_items[i]; }
    bool isTaken(Index i) const { return i < slotCount() && m_taken.test(i); }
    BitSpan takenBits() const { return m_taken; }
    Index count() const {
        Index n = 0;
        for (BitWord w : takenBits().words())
            n += Index(std::popcount(w));
        return n;
    }
    Index slotCount() const { return Index(m_items.size()); }
    Iterator begin() const { return {this, 0}; }
    Iterator end() const { return {this, slotCount()}; }

  private:
    void record() {
        if (m_diff.empty())
            broadcastDirty();
    }

    std::vector<int> m_items;
    BitVector m_taken;
    SlotDiff<Index> m_diff;
};

// Counts its calls, to check that swaps never re-check an item
struct CountedEven {
    static inline long calls = 0;
    bool operator()(const int &x) const { return ++calls, x % 2 == 0; }
};

/**
 * Random toggles, modifications and swaps through two nested filters (one
 * read every round, one only now and then, so it folds swaps); rounds of
 * swaps alone must not call the predicate. Random relocations must reach
 * both filters' listeners exactly when they happened since their last read
 */
void checkSwaps(std::mt19937 &rng) {
    static_assert(Original<SwapSource> && SlotViewable<SwapSource>);
    constexpr Index n = 500;
    auto src = std::make_shared<SwapSource>(n);
    for (Index i = 0; i < n; i += 2)
        src->toggle(i, int(rng() % 100));
    Ledger ledger;
    auto evens = ledger.get(Filter{From{src}, CountedEven{}});
    auto bigEvens = ledger.get(Filter{Filter{From{src}, CountedEven{}}, isBig});
    Mirror<SwapSource> ms(*src);
    Mirror<decltype(evens)::element_type> me(*evens);
    Mirror<decltype(bigEvens)::element_type> mb(*bigEvens);
    src->addListener(&ms);
    evens->addListener(&me);
    bigEvens->addListener(&mb);
    src->addInterests(SlotInterest::ALL);
    evens->addInterests(SlotInterest::ALL);
    bigEvens->addInterests(SlotInterest::ALL);

    bool relocatedForBig = false;
    for (int round = 0; round < 2000; ++round) {
        const bool swapsOnly = rng() % 3 == 0;
        const bool relocated = rng() % 8 == 0;
        if (relocated)
            src->relocate();
        relocatedForBig |= relocated;
        const long calls = CountedEven::calls;
        // Bulk rounds spread changes over many words, for the word path
        const int ops = int(rng() % (round % 7 ? 8 : 400));
        for (int k = 0; k < ops; ++k) {
            const Index i = Index(rng() % n), j = Index(rng() % n);
            const int op = swapsOnly ? 0 : int(rng() % 3);
            if (op == 0) {
                if (i != j)
                    src->swap(i, j);
            } else if (op == 1 || !src->isTaken(i))
                src->toggle(i, int(rng() % 100));
            else
                src->modify(i, int(rng() % 100));
        }
        pull(*evens);
        me.check();
        assert(me.relocated == relocated);
        me.relocated = false;
        if (rng() % 4 == 0) {
            pull(*bigEvens);
            mb.check();
            assert(mb.relocated == relocatedForBig);
            mb.relocated = relocatedForBig = false;
            for (auto &[i, v] : ms.model)
                assert(mb.model.count(i) == (v % 2 == 0 && isBig(v)));
        }
        if (swapsOnly)
            assert(CountedEven::calls == calls);
        ms.check();
        for (auto &[i, v] : ms.model)
            assert(me.model.count(i) == (v % 2 == 0));
    }
    src->removeListener(&ms);
    evens->removeListener(&me);
    bigEvens->removeListener(&mb);
    src->removeInterests(SlotInterest::ALL);
    evens->removeInterests(SlotInterest::ALL);
    bigEvens->removeInterests(SlotInterest::ALL);
}

/**
 * The order tools against the standard algorithms: a merge must give a
 * sorted multiset of both inputs and keep every element before the position
 * it reports, and
 * a permutation applied by swaps must place every element and use at most
 * one swap per moved element
 */
void checkOrderTools(std::mt19937 &rng) {
    for (int round = 0; round < 2000; ++round) {
        const std::size_t n = rng() % 40, k = rng() % 10;
        std::vector<int> out(n + k), batch(k);
        // Few distinct values, so equal elements are common
        for (std::size_t i = 0; i < n; ++i)
            out[i] = int(rng() % 16);
        for (int &x : batch)
            x = int(rng() % 16);
        std::sort(out.begin(), out.begin() + std::ptrdiff_t(n));
        std::sort(batch.begin(), batch.end());
        const std::vector<int> before(out.begin(),
                                      out.begin() + std::ptrdiff_t(n));
        const std::size_t p = mergeSortedBack(
            std::span<int>(out), n, std::span<const int>(batch), std::less<>{});
        std::vector<int> expected = before;
        expected.insert(expected.end(), batch.begin(), batch.end());
        std::sort(expected.begin(), expected.end());
        assert(out == expected);
        assert(p <= n);
        for (std::size_t i = 0; i < p; ++i)
            assert(out[i] == before[i]);

        std::vector<Index> to(n);
        for (std::size_t i = 0; i < n; ++i)
            to[i] = Index(i);
        std::shuffle(to.begin(), to.end(), rng);
        std::vector<int> items(n), moved(n);
        for (std::size_t i = 0; i < n; ++i)
            items[i] = int(i);
        for (std::size_t i = 0; i < n; ++i)
            moved[to[i]] = items[i];
        std::size_t fixed = 0, swaps = 0;
        for (std::size_t i = 0; i < n; ++i)
            fixed += to[i] == i;
        permuteBySwaps(std::span<Index>(to), Index(0), [&](Index a, Index b) {
            assert(a != b);
            std::swap(items[a], items[b]);
            ++swaps;
        });
        assert(items == moved);
        assert(swaps <= n - fixed);
        for (std::size_t i = 0; i < n; ++i)
            assert(to[i] == i);
    }
}

struct Big {
    char bytes[100000]; // larger than a chunk: one cell per chunk
};

int main() {
    checkStableItems<int>(20000);
    checkStableItems<std::array<char, 1000>>(2000);
    checkStableItems<Big>(100);
    checkCoalescing();
    std::mt19937 rng(42);
    checkFolding(rng);
    checkSwaps(rng);
    checkOrderTools(rng);
    auto store = std::make_shared<Store>();
    Ledger ledger;
    auto evens = ledger.get(Filter{From{store}, isEven});
    auto bigEvens = ledger.get(Filter{Filter{From{store}, isEven}, isBig});
    // A sibling read only now and then: it turns lazy and folds the deltas
    // its siblings pull from the store
    auto bigs = ledger.get(Filter{From{store}, isBig});
    // A lambda predicate: its type is the key, and the call is inlined
    auto thirds = ledger.get(
        Filter{From{store}, [](const int &x) { return x % 3 == 0; }});
    static_assert(SlotViewable<decltype(bigEvens)::element_type>);

    // Mutations before anyone listens are not recorded: the mirrors below
    // snapshot the views and must receive no delta for these items
    store->emplace(2);
    store->emplace(3);
    pull(*bigEvens);
    assert(evens->count() == 1 && bigEvens->count() == 0);

    Mirror<Store> ms(*store);
    Mirror<decltype(evens)::element_type> me(*evens);
    Mirror<decltype(bigEvens)::element_type> mb(*bigEvens);
    Mirror<decltype(bigs)::element_type> mg(*bigs);
    Mirror<decltype(thirds)::element_type> mt(*thirds);
    store->addListener(&ms);
    evens->addListener(&me);
    bigEvens->addListener(&mb);
    bigs->addListener(&mg);
    thirds->addListener(&mt);
    thirds->addInterests(SlotInterest::ALL);
    bigs->addInterests(SlotInterest::ALL);
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
        if (rng() % 2 == 0) {
            pull(*thirds);
            mt.check();
            for (auto &[i, v] : ms.model)
                assert(mt.model.count(i) == (v % 3 == 0));
        }
        if (rng() % 5 == 0) {
            pull(*bigs);
            mg.check();
            for (auto &[i, v] : ms.model)
                assert(mg.model.count(i) == isBig(v));
        }
    }
    pull(*bigs);
    mg.check();
    bigs->removeListener(&mg);
    bigs->removeInterests(SlotInterest::ALL);
    bigs.reset();
    thirds->removeListener(&mt);
    thirds->removeInterests(SlotInterest::ALL);
    thirds.reset();
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
                         d.modified.size() + d.touchedWords.size();
                const std::size_t w = e / bitWordBits;
                touchedE =
                    w < d.words.size() &&
                    d.words[w].kind(e % bitWordBits) != SlotChange::untouched;
            }
        } rec;
        store->addListener(&rec);
        store->addInterests(SI::CREATED | SI::WORDS);
        store->addInterests(SI::CREATED);
        store->removeInterests(SI::CREATED);
        assert(store->interests() == (SI::CREATED | SI::WORDS));
        rec.e = store->emplace(1);
        store->erase(live.back());
        live.pop_back();
        pull(*store);
        assert(rec.n == 1 && rec.created == std::vector{rec.e});
        assert(rec.others == 0 && rec.touchedE);

        // Counts only: still notified, every member empty
        store->removeInterests(SI::CREATED | SI::WORDS);
        assert(store->interests() == 0);
        store->modify(rec.e) = 3;
        pull(*store);
        assert(rec.n == 2 && rec.created.empty() && rec.others == 0 &&
               !rec.touchedE);
        store->removeListener(&rec);
    }
    std::puts("FUZZ_OK");
}
