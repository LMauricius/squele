#pragma once
#ifndef H_INCLUDED_SQUELE_VIEWS_EXTENDVIEW
#define H_INCLUDED_SQUELE_VIEWS_EXTENDVIEW

#include "squele/bits/indexBits.hpp"
#include "squele/bits/pull.hpp"
#include "squele/bits/slotCell.hpp"
#include "squele/bits/slotChunks.hpp"
#include "squele/bits/slotIterator.hpp"
#include "squele/components/broadcaster.hpp"
#include "squele/core/concepts.hpp"
#include "squele/core/ledger.hpp"
#include "squele/deltas/index.hpp"
#include "squele/queries/extend.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace squele {

namespace detail {

/**
 * Take no space in an extend view for the state of the path it does not
 * use; two types, so both members can overlap
 */
struct NoExtendIndexState {};
struct NoExtendGeneralState {};

} // namespace detail

/**
 * The view of `Extend`. Each item is built whole, as `Item{members...}`,
 * into a stable cell, so it keeps its address until it is created or
 * erased; a change of any source item at a position rebuilds that item and
 * calls every function again. It never sets `relocated`. It is
 * `SlotViewable` and reports its changes as `IndexDelta`s.
 *
 * It listens to each source with the source's number as tag, and always
 * defers: a function reads every source, so nothing is rebuilt until
 * `sync()` has pulled all of them.
 *
 * Two paths, picked at compile time:
 * - Index (one `SlotViewable` source, or only `OrderedViewable` ones):
 *   position `i` is index `i` in every source, so only the touched
 *   positions are rebuilt. One source keeps its index space and follows its
 *   swaps by swapping items; with several, a swap replaces both positions.
 * - General (any other mix of `ListViewable` sources): every sync with a
 *   change walks all sources side by side and rebuilds every position.
 *
 * A rebuilt position is reported replaced when a source reports a new item
 * there (the index path) or a pointer changed (the general path), else
 * modified. Positions are a prefix, except with one sparse source; the slot
 * count never shrinks.
 */
template <class TItemT, class... TPartsT>
class ExtendView
    : public Broadcaster<IndexDelta<ViewIndex<
          std::tuple_element_t<0, detail::ExtendSourceViews<TPartsT...>>>>> {
    using SourceViews = detail::ExtendSourceViews<TPartsT...>;
    static constexpr std::size_t sourceCount = std::tuple_size_v<SourceViews>;
    template <std::size_t K>
    using Source = std::tuple_element_t<K, SourceViews>;

  public:
    using Query =
        Extend<TItemT, typename detail::ExtendQueryPart<TPartsT>::type...>;
    using Item = typename Query::Item;
    using Index = ViewIndex<Source<0>>;
    using Delta = IndexDelta<Index>;
    using Iterator = SlotIterator<ExtendView>;

    /**
     * Gets every source from `ledger`, builds every item and starts
     * listening. The view starts clean.
     */
    ExtendView(Ledger &ledger, const Query &query) : m_query(query) {
        static_assert(SlotViewable<ExtendView>);
        forEachSource([&]<std::size_t K>() {
            std::get<K>(m_sources) =
                ledger.getUnsynced(std::get<partOf(K)>(m_query.args()));
        });
        // Consistent and nothing unflushed, so no change is missed between
        // this build and addListener(). Nothing is recorded: no listeners
        forEachSource([&]<std::size_t K>() { pull(*std::get<K>(m_sources)); });
        try {
            if constexpr (indexPath)
                refreshAll();
            else
                rebuildGeneral();
        } catch (...) {
            // The destructor does not run for a throwing constructor
            destroyAll();
            throw;
        }
        forEachSource([&]<std::size_t K>() {
            if constexpr (indexPath)
                std::get<K>(m_sources)->addInterests(sourceInterests);
            std::get<K>(m_sources)->addListener(this, ListenerTag{K});
        });
    }

    ~ExtendView() {
        forEachSource([&]<std::size_t K>() {
            std::get<K>(m_sources)->removeListener(this, ListenerTag{K});
            if constexpr (indexPath)
                std::get<K>(m_sources)->removeInterests(sourceInterests);
        });
        destroyAll();
    }

    /**
     * Folds the delta of source `tag` into its pending diff (index path) or
     * marks a rebuild due (general path); applied on the next `sync()`
     */
    template <class D>
    void onDelta([[maybe_unused]] const D &d,
                 [[maybe_unused]] ListenerTag tag) noexcept {
        if constexpr (indexPath) {
            forEachSource([&]<std::size_t K>() {
                if constexpr (std::same_as<D, typename Source<K>::Delta>) {
                    if (tag == K) {
                        auto &p = std::get<K>(m_index.pending);
                        p.diff.fold(d);
                        // The largest since the last apply, as in MapView
                        p.slotCount = std::max(p.slotCount, d.slotCount);
                    }
                }
            });
        } else {
            m_general.due = true;
        }
    }

    /**
     * Marks this view dirty, forwarding only the first call per cycle
     */
    void onDirty() noexcept {
        if (m_dirty)
            return;
        m_dirty = true;
        this->broadcastDirty();
    }

    bool dirty() const { return m_dirty; }

    /**
     * Pulls every source if dirty, then rebuilds what changed
     */
    void sync() {
        if (!m_dirty)
            return;
        forEachSource([&]<std::size_t K>() { pull(*std::get<K>(m_sources)); });
        // Every source was just pulled, so all are consistent with the
        // deferred changes
        if constexpr (indexPath) {
            applyPending();
        } else if (m_general.due) {
            rebuildGeneral();
            m_general.due = false;
        }
        m_dirty = false;
    }

    /**
     * Sends the changes since the last flush to listeners
     */
    void flush() {
        if (m_diff.empty())
            return;
        this->broadcast(m_diff.delta(m_count, slotCount(), this->interests()));
        m_diff.clear();
    }

    /**
     * @warning Reading a free index is UB (not checked).
     */
    const Item &operator[](Index i) const {
        return m_items[static_cast<std::size_t>(i)].get();
    }

    bool isTaken(Index i) const {
        const auto at = static_cast<std::size_t>(i);
        return at < m_taken.size() && m_taken.test(at);
    }
    BitSpan takenBits() const { return m_taken; }
    Index count() const { return m_count; }
    Index slotCount() const { return static_cast<Index>(m_taken.size()); }

    Iterator begin() const { return {this, 0}; }
    Iterator end() const { return {this, slotCount()}; }

  private:
    // Compile-time tables of the parts
    static constexpr std::array<bool, sizeof...(TPartsT)> isSourcePart{
        detail::ExtendPart<TPartsT>::isSource...};

    /**
     * The part index of source `k`; the constructor needs it to find the
     * source's query among the arguments
     */
    static constexpr std::size_t partOf(std::size_t k) {
        for (std::size_t p = 0;; ++p)
            if (isSourcePart[p] && k-- == 0)
                return p;
    }

    /**
     * The source number of part `p` (the sources before it)
     */
    static constexpr std::size_t sourceOf(std::size_t p) {
        std::size_t k = 0;
        for (std::size_t q = 0; q < p; ++q)
            k += isSourcePart[q];
        return k;
    }

    static constexpr bool indexPath =
        []<std::size_t... K>(std::index_sequence<K...>) {
            return (sourceCount == 1 && SlotViewable<Source<0>>) ||
                   (OrderedViewable<Source<K>> && ...);
        }(std::make_index_sequence<sourceCount>{});

    // A position's source item pointers, one per source
    using Ptrs = decltype([]<std::size_t... K>(std::index_sequence<K...>) {
        return std::tuple<const typename Source<K>::Item *...>{};
    }(std::make_index_sequence<sourceCount>{}));

    /**
     * Index path: one source's deferred changes and its slot count at the
     * last of them
     */
    template <class S> struct Pending {
        IndexDiff<typename S::Index> diff;
        typename S::Index slotCount = 0;
    };
    struct IndexState {
        decltype([]<std::size_t... K>(std::index_sequence<K...>) {
            return std::tuple<Pending<Source<K>>...>{};
        }(std::make_index_sequence<sourceCount>{})) pending;
        // Scratch words, all zero between syncs: the positions a source
        // changed, those where it reports a new item, and those whose
        // pointers may have moved (swapped in the one source, or relocated);
        // and the words with a bit in any of them, so a sync costs
        // O(touched words), not O(slots)
        std::vector<BitWord> touched, fresh, moved;
        std::vector<std::size_t> listed;
    };
    struct GeneralState {
        bool due = false;       // a rebuild is deferred to sync()
        std::vector<Ptrs> next; // scratch: the new pointers per position
    };

    // Index path: the words for folding and for the touched positions, and
    // the swaps
    static constexpr IndexInterest::Mask sourceInterests =
        IndexInterest::SWAPPED | IndexInterest::WORDS |
        IndexInterest::TOUCHED_WORDS;

    /**
     * Calls `f.template operator()<K>()` for each source number `K`
     */
    template <class F> static void forEachSource(F &&f) {
        [&]<std::size_t... K>(std::index_sequence<K...>) {
            (f.template operator()<K>(), ...);
        }(std::make_index_sequence<sourceCount>{});
    }

    /**
     * Builds the item of the pointers `p`: each source part takes its
     * pointer, each function part its result over all source items
     */
    Item makeItem(const Ptrs &p) const {
        return [&]<std::size_t... P>(std::index_sequence<P...>) {
            return Item{member<P>(p)...};
        }(std::index_sequence_for<TPartsT...>{});
    }
    template <std::size_t P> decltype(auto) member(const Ptrs &p) const {
        if constexpr (isSourcePart[P])
            return std::get<sourceOf(P)>(p);
        else
            return std::apply(
                [&](const auto *...ptr) {
                    return std::invoke(std::get<P>(m_query.args()), *ptr...);
                },
                p);
    }

    /**
     * Index path: the source pointers at `i`, and whether every source
     * holds an item there
     */
    bool readIndex(Index i, Ptrs &p) const
        requires indexPath
    {
        bool taken = true;
        forEachSource([&]<std::size_t K>() {
            const auto &s = *std::get<K>(m_sources);
            const auto at = static_cast<typename Source<K>::Index>(i);
            taken = taken &&
                    static_cast<std::size_t>(i) <
                        static_cast<std::size_t>(s.slotCount()) &&
                    s.isTaken(at);
            if (taken)
                std::get<K>(p) = &s[at];
        });
        return taken;
    }

    /**
     * Brings position `i` to the state `taken` with pointers `p`, recording
     * the change. Kept positions are rebuilt; `fresh` (or a changed pointer
     * on the general path) reports them replaced, else modified.
     */
    void update(Index i, bool taken, const Ptrs &p, bool fresh, bool record) {
        const auto at = static_cast<std::size_t>(i);
        const bool was = m_taken.test(at);
        if (was && !taken) {
            cell(i).destroy();
            m_taken.reset(at);
            --m_count;
            if (record)
                m_diff.erase(i);
        } else if (!was && taken) {
            cell(i).construct(makeItem(p));
            m_ptrs[at] = p;
            m_taken.set(at);
            ++m_count;
            if (record)
                m_diff.create(i);
        } else if (was) {
            if constexpr (!indexPath)
                fresh = fresh || p != m_ptrs[at];
            // Built first, so a throwing function leaves the old item
            Item next = makeItem(p);
            cell(i).destroy();
            cell(i).construct(std::move(next));
            m_ptrs[at] = p;
            // Erase before create, so the record coalesces to replaced
            if (record && fresh) {
                m_diff.erase(i);
                m_diff.create(i);
            } else if (record) {
                m_diff.modify(i);
            }
        }
    }

    /**
     * Index path: builds every position from the sources (construction)
     */
    void refreshAll()
        requires indexPath
    {
        std::size_t slots = 0;
        forEachSource([&]<std::size_t K>() {
            slots = std::max(slots, static_cast<std::size_t>(
                                        std::get<K>(m_sources)->slotCount()));
        });
        grow(slots);
        Ptrs p{};
        for (std::size_t at = 0; at < slots; ++at) {
            const auto i = static_cast<Index>(at);
            if (readIndex(i, p))
                update(i, true, p, false, false);
        }
    }

    /**
     * Index path: applies the deferred changes of every source. Swaps
     * first (one source: the items follow them; several: both positions are
     * replaced), then every touched position is rebuilt from the sources; a
     * relocation touches every position
     */
    void applyPending()
        requires indexPath
    {
        const bool record = this->hasListeners();
        // The deltas may name indices past a source's current slot count
        // (a dense source reports erased last positions)
        std::size_t slots = m_taken.size();
        forEachSource([&]<std::size_t K>() {
            slots = std::max(
                {slots,
                 static_cast<std::size_t>(std::get<K>(m_sources)->slotCount()),
                 static_cast<std::size_t>(
                     std::get<K>(m_index.pending).slotCount)});
        });
        grow(slots);
        const std::size_t words = m_taken.words().size();
        auto &touched = m_index.touched, &fresh = m_index.fresh,
             &moved = m_index.moved;
        auto &listed = m_index.listed;
        // Growing adds zero words; the old ones were cleared after use
        touched.resize(words);
        fresh.resize(words);
        moved.resize(words);
        // Lists word `w` once, the first time a bit is set in it
        auto list = [&](std::size_t w) {
            if (!(touched[w] | moved[w]))
                listed.push_back(w);
        };
        auto mark = [&](std::vector<BitWord> &v, std::size_t at) {
            list(at / bitWordBits);
            v[at / bitWordBits] |= BitWord{1} << (at % bitWordBits);
        };
        bool relocated = false;
        forEachSource([&]<std::size_t K>() {
            auto &p = std::get<K>(m_index.pending);
            if (p.diff.empty())
                return;
            const auto d = p.diff.delta(0, p.slotCount, sourceInterests);
            relocated = relocated || d.relocated;
            for (const auto &[a, b] : d.swapped) {
                const auto at = static_cast<std::size_t>(a);
                const auto bt = static_cast<std::size_t>(b);
                if constexpr (sourceCount == 1) {
                    if (swapItems(at, bt) && record)
                        m_diff.swap(static_cast<Index>(a),
                                    static_cast<Index>(b));
                }
                // One source: reread the moved pointers; several: the
                // combined items are new
                for (const std::size_t x : {at, bt}) {
                    if constexpr (sourceCount == 1) {
                        mark(moved, x);
                    } else {
                        mark(touched, x);
                        mark(fresh, x);
                    }
                }
            }
            for (const auto w : d.touchedWords) {
                const auto at = static_cast<std::size_t>(w);
                const IndexWordChange &c = d.words[at];
                if (!c.touched)
                    continue;
                list(at);
                touched[at] |= c.touched;
                // Created or replaced in the source
                fresh[at] |= c.exists & ~c.same;
            }
            p.diff.clear();
            p.slotCount = 0;
        });
        if (relocated) [[unlikely]] {
            listed.clear();
            for (std::size_t w = 0; w < words; ++w)
                listed.push_back(w);
            std::ranges::fill(moved, ~BitWord{0});
        }

        Ptrs p{};
        for (const std::size_t w : listed) {
            for (BitWord m = touched[w] | moved[w]; m; m &= m - 1) {
                const int b = std::countr_zero(m);
                const std::size_t at = w * bitWordBits + std::size_t(b);
                if (at >= slots)
                    break;
                const auto i = static_cast<Index>(at);
                const bool taken = readIndex(i, p);
                // A position whose pointers only may have moved needs no
                // rebuild when they did not
                if (!((touched[w] >> b) & 1) && taken && m_taken.test(at) &&
                    p == m_ptrs[at])
                    continue;
                update(i, taken, p, (fresh[w] >> b) & 1, record);
            }
            touched[w] = fresh[w] = moved[w] = 0;
        }
        listed.clear();
    }

    /**
     * Index path, one source: exchanges the items (or free cells) at `a`
     * and `b`, their pointers and taken bits
     * @returns whether either was taken
     */
    bool swapItems(std::size_t a, std::size_t b)
        requires indexPath
    {
        const bool ta = m_taken.test(a), tb = m_taken.test(b);
        if (ta && tb) {
            std::ranges::swap(m_items[a].get(), m_items[b].get());
        } else if (ta || tb) {
            SlotCell<Item> &from = m_items[ta ? a : b];
            m_items[ta ? b : a].construct(std::move(from.get()));
            from.destroy();
            if (ta) {
                m_taken.reset(a);
                m_taken.set(b);
            } else {
                m_taken.reset(b);
                m_taken.set(a);
            }
        }
        std::swap(m_ptrs[a], m_ptrs[b]);
        return ta || tb;
    }

    /**
     * General path: walks every source side by side and rebuilds every
     * position below the smallest count; positions past it are erased
     */
    void rebuildGeneral()
        requires(!indexPath)
    {
        const bool record = this->hasListeners();
        auto &next = m_general.next;
        next.clear();
        [&]<std::size_t... K>(std::index_sequence<K...>) {
            auto its = std::tuple{std::get<K>(m_sources)->begin()...};
            while (
                ((std::get<K>(its) != std::get<K>(m_sources)->end()) && ...)) {
                next.push_back(Ptrs{&*std::get<K>(its)...});
                (++std::get<K>(its), ...);
            }
        }(std::make_index_sequence<sourceCount>{});
        const std::size_t n1 = next.size();
        const std::size_t n0 = static_cast<std::size_t>(m_count);
        grow(n1);
        for (std::size_t at = 0; at < std::max(n0, n1); ++at)
            update(static_cast<Index>(at), at < n1, at < n1 ? next[at] : Ptrs{},
                   false, record);
    }

    SlotCell<Item> &cell(Index i) {
        return m_items[static_cast<std::size_t>(i)];
    }

    /**
     * Destroys every live item; the view is unusable after it
     */
    void destroyAll() noexcept {
        for (auto it = begin(); it != end(); ++it)
            cell(it.index()).destroy();
    }

    /**
     * Grows the cells, pointers and taken bits to `slots`; never shrinks
     */
    void grow(std::size_t slots) {
        while (m_items.size() < slots)
            m_items.emplace_back();
        if (m_ptrs.size() < slots)
            m_ptrs.resize(slots);
        if (m_taken.size() < slots)
            m_taken.resize(slots);
    }

    Query m_query; // the functions, and the source queries for the ledger
    decltype([]<std::size_t... K>(std::index_sequence<K...>) {
        return std::tuple<std::shared_ptr<Source<K>>...>{};
    }(std::make_index_sequence<sourceCount>{})) m_sources;
    SlotChunks<SlotCell<Item>> m_items; // per position; taken ones hold items
    std::vector<Ptrs> m_ptrs;           // per position: the pointers in it
    BitVector m_taken;
    Index m_count = 0;
    IndexDiff<Index> m_diff;
    bool m_dirty = false;

    [[no_unique_address]] std::conditional_t<
        indexPath, IndexState, detail::NoExtendIndexState> m_index;
    [[no_unique_address]] std::conditional_t<
        indexPath, detail::NoExtendGeneralState, GeneralState> m_general;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_VIEWS_EXTENDVIEW
