#pragma once
#ifndef H_INCLUDED_SQUELE_VIEWS_MAPVIEW
#define H_INCLUDED_SQUELE_VIEWS_MAPVIEW

#include "squele/bits/indexBits.hpp"
#include "squele/bits/pull.hpp"
#include "squele/bits/result.hpp"
#include "squele/bits/slotCell.hpp"
#include "squele/bits/slotChunks.hpp"
#include "squele/bits/slotIterator.hpp"
#include "squele/components/broadcaster.hpp"
#include "squele/core/concepts.hpp"
#include "squele/core/ledger.hpp"
#include "squele/deltas/index.hpp"
#include "squele/queries/map.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace squele {

namespace detail {

/**
 * Take no space in a map view for the state of the path it does not use;
 * two types, so both members can overlap
 */
struct NoMapSlotState {};
struct NoMapGeneralState {};

} // namespace detail

/**
 * The view of `Map`: the results of a function over each source item, owned
 * by the view in stable storage, so a result keeps its address until its
 * source item is created, replaced or erased. It never sets `relocated`. It
 * is `SlotViewable` and reports its changes as `IndexDelta`s.
 *
 * Two paths, picked at compile time:
 * - Slot (a `SlotViewable` source): the source's index space. A result sits
 *   at its item's index; created, modified and replaced items are
 *   recomputed, erased ones destroyed, and a swap swaps two results without
 *   a call. A dense source gives a dense view.
 * - General (any `ListViewable` source): dense positions in iteration
 *   order; every source flush recomputes every result.
 *
 * A `ComparedResult` function skips reporting recomputed results that equal
 * the old ones. Like `FilterView`, the view applies deltas eagerly while it
 * is read every cycle, and otherwise defers them to its next `sync()`.
 */
template <ListViewable TSourceT, class TFnT>
class MapView : public Broadcaster<IndexDelta<ViewIndex<TSourceT>>> {
  public:
    using SourceT = TSourceT;
    using Fn = TFnT;
    using Query = Map<typename SourceT::Query, Fn>;
    using Item = ResultOf<Fn, typename SourceT::Item>;
    using Index = ViewIndex<SourceT>;
    using Delta = IndexDelta<Index>;
    using Iterator = SlotIterator<MapView>;

    /**
     * Gets the source from `ledger`, computes every result and starts
     * listening. The view starts clean.
     */
    MapView(Ledger &ledger, const Query &query)
        : m_source(ledger.getUnsynced(query.source())), m_fn(query.function()) {
        static_assert(Dependent<MapView, SourceT> && SlotViewable<MapView>);
        // Make the source consistent and leave nothing unflushed, so no
        // change is missed between this pass and addListener(). Nothing is
        // recorded: there are no listeners yet
        pull(*m_source);
        try {
            computeAll();
        } catch (...) {
            // The destructor does not run for a throwing constructor
            destroyAll();
            throw;
        }
        if constexpr (slotPath)
            m_source->addInterests(sourceInterests);
        m_source->addListener(this);
    }

    /**
     * Stops listening and destroys every result
     */
    ~MapView() {
        m_source->removeListener(this);
        if constexpr (slotPath)
            m_source->removeInterests(sourceInterests);
        destroyAll();
    }

    /**
     * Applies the source's changes now if this view was read since the last
     * delta, else defers them to the next `sync()`.
     * @note noexcept, as in the other views. A throwing `Fn` terminates.
     */
    void onDelta([[maybe_unused]] const typename SourceT::Delta &d) noexcept {
        if constexpr (slotPath) {
            if (m_readSinceDelta && m_slot.pending.empty()) {
                apply(d);
            } else {
                m_slot.pending.fold(d);
                // The largest since the last apply: an earlier folded delta
                // may name indices past a later one's slot count
                m_slot.pendingSlotCount =
                    std::max(m_slot.pendingSlotCount, d.slotCount);
            }
        } else {
            if (m_readSinceDelta && !m_general.computeDue)
                computeAll();
            else
                m_general.computeDue = true;
        }
        m_readSinceDelta = false;
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
     * Pulls the source if dirty, then applies the deferred changes
     */
    void sync() {
        if (m_dirty) {
            pull(*m_source);
            // The source was just pulled, so it is consistent with the
            // deferred changes
            if constexpr (slotPath) {
                if (!m_slot.pending.empty()) {
                    apply(m_slot.pending.delta(0, m_slot.pendingSlotCount,
                                               sourceInterests));
                    m_slot.pending.clear();
                    m_slot.pendingSlotCount = 0;
                }
            } else if (m_general.computeDue) {
                computeAll();
                m_general.computeDue = false;
            }
            m_dirty = false;
        }
        // Set last: this read covers every delta so far, including those of
        // the pull above
        m_readSinceDelta = true;
    }

    /**
     * Sends the changes since the last flush to listeners
     * @note General path: the shrink applies after the delta, as in
     * `ArraySortView` (`slotCount` is the largest count since the last flush)
     */
    void flush() {
        if (m_diff.empty())
            return;
        if constexpr (slotPath) {
            this->broadcast(
                m_diff.delta(m_count, slotCount(), this->interests()));
        } else {
            this->broadcast(m_diff.delta(m_count,
                                         std::max(m_general.peakCount, m_count),
                                         this->interests()));
            m_general.peakCount = 0;
        }
        m_diff.clear();
    }

    /**
     * @warning Reading a free index is UB (not checked).
     */
    const Item &operator[](Index i) const {
        return m_results[static_cast<std::size_t>(i)].get();
    }

    bool isTaken(Index i) const {
        return static_cast<std::size_t>(i) < m_taken.size() &&
               m_taken.test(static_cast<std::size_t>(i));
    }
    BitSpan takenBits() const { return m_taken; }
    Index count() const { return m_count; }
    Index slotCount() const { return static_cast<Index>(m_taken.size()); }

    Iterator begin() const { return {this, 0}; }
    Iterator end() const { return {this, slotCount()}; }

  private:
    static constexpr bool slotPath = SlotViewable<SourceT>;

    /**
     * Slot path state: the deferred source changes and the source's slot
     * count at the last of them
     */
    struct SlotState {
        IndexDiff<Index> pending;
        Index pendingSlotCount = 0;
    };

    /**
     * General path state
     */
    struct GeneralState {
        bool computeDue = false; // a full recomputation is deferred to sync()
        Index peakCount = 0;     // the largest count since the last flush
    };

    // Slot path: the spans for the per-index steps, the swaps, and the words
    // for folding deferred deltas. Fixed, since every result needs every
    // change whatever this view's listeners want
    static constexpr IndexInterest::Mask sourceInterests =
        IndexInterest::CREATED | IndexInterest::DELETED |
        IndexInterest::REPLACED | IndexInterest::MODIFIED |
        IndexInterest::SWAPPED | IndexInterest::WORDS |
        IndexInterest::TOUCHED_WORDS;

    /**
     * Computes every result from the source. The construction step of both
     * paths, and the only update step of the general path.
     */
    void computeAll() {
        // Slot path: construction only, so every cell is free
        if constexpr (slotPath) {
            grow(static_cast<std::size_t>(m_source->slotCount()));
            for (auto it = m_source->begin(); it != m_source->end(); ++it) {
                construct(it.index(), *it);
                m_taken.set(static_cast<std::size_t>(it.index()));
                ++m_count;
            }
        } else {
            computeAllGeneral();
        }
    }

    /**
     * General path: recomputes in place below the old count, constructs past
     * it, then destroys the old tail. Positions are dense
     */
    void computeAllGeneral()
        requires(!slotPath)
    {
        const bool record = this->hasListeners();
        m_general.peakCount = std::max(m_general.peakCount, m_count);
        const Index oldCount = m_count;
        Index q = 0;
        for (auto it = m_source->begin(); it != m_source->end(); ++it, ++q) {
            if (q < oldCount) {
                if (recomputeResult(m_fn, cell(q).get(), *it) && record)
                    m_diff.modify(q);
            } else {
                grow(static_cast<std::size_t>(q) + 1);
                construct(q, *it);
                // Counted at once, so a throw leaves no live result untracked
                m_taken.set(static_cast<std::size_t>(q));
                m_count = q + 1;
                if (record)
                    m_diff.create(q);
            }
        }
        for (Index t = q; t < oldCount; ++t) {
            cell(t).destroy();
            if (record)
                m_diff.erase(t);
        }
        if (q < oldCount) {
            // Shrinking clears the dropped bits
            m_taken.resize(static_cast<std::size_t>(q));
            m_count = q;
        }
    }

    /**
     * Slot path: applies a delta of the source, which must be consistent
     * with it. Records only with listeners.
     */
    void apply(const typename SourceT::Delta &d) noexcept
        requires slotPath
    {
        // d.relocated is ignored: results are copies, not pointers
        const bool record = this->hasListeners();
        grow(static_cast<std::size_t>(d.slotCount));

        // Swaps first: the other changes start from the swapped state. Two
        // free slots change nothing this view holds or reports
        for (const auto &[a, b] : d.swapped) {
            if (swapResults(a, b) && record)
                m_diff.swap(a, b);
        }
        for (Index i : d.deleted) {
            cell(i).destroy();
            m_taken.reset(static_cast<std::size_t>(i));
            --m_count;
            if (record)
                m_diff.erase(i);
        }
        // Erase before create, so the record coalesces to replaced
        for (Index i : d.replaced) {
            cell(i).destroy();
            construct(i, (*m_source)[i]);
            if (record) {
                m_diff.erase(i);
                m_diff.create(i);
            }
        }
        for (Index i : d.created) {
            construct(i, (*m_source)[i]);
            m_taken.set(static_cast<std::size_t>(i));
            ++m_count;
            if (record)
                m_diff.create(i);
        }
        for (Index i : d.modified) {
            if (recomputeResult(m_fn, cell(i).get(), (*m_source)[i]) && record)
                m_diff.modify(i);
        }
    }

    /**
     * Slot path: exchanges the results (or free cells) at `a` and `b` and
     * their taken bits. Both taken: swap the items; one taken: move it into
     * the free cell and destroy the old one; none: nothing
     * @returns whether either was taken
     */
    bool swapResults(Index a, Index b)
        requires slotPath
    {
        const auto at = static_cast<std::size_t>(a);
        const auto bt = static_cast<std::size_t>(b);
        const bool ta = m_taken.test(at), tb = m_taken.test(bt);
        if (ta && tb) {
            std::ranges::swap(cell(a).get(), cell(b).get());
        } else if (ta || tb) {
            SlotCell<Item> &from = cell(ta ? a : b);
            cell(ta ? b : a).construct(std::move(from.get()));
            from.destroy();
            // Exactly one is set, so flipping both moves it
            if (ta) {
                m_taken.reset(at);
                m_taken.set(bt);
            } else {
                m_taken.reset(bt);
                m_taken.set(at);
            }
        }
        return ta || tb;
    }

    SlotCell<Item> &cell(Index i) {
        return m_results[static_cast<std::size_t>(i)];
    }

    /**
     * Constructs the result of `item` in the free cell `i`
     */
    void construct(Index i, const typename SourceT::Item &item) {
        cell(i).construct(std::invoke(resultFn(m_fn), item));
    }

    /**
     * Destroys every live result; the view is unusable after it
     */
    void destroyAll() noexcept {
        for (auto it = begin(); it != end(); ++it)
            cell(it.index()).destroy();
    }

    /**
     * Grows the cells and the taken bits to `slots`; never shrinks (cells
     * must not move, and `SlotChunks` never shrinks)
     */
    void grow(std::size_t slots) {
        while (m_results.size() < slots)
            m_results.emplace_back();
        if (m_taken.size() < slots)
            m_taken.resize(slots);
    }

    std::shared_ptr<SourceT> m_source; // owned, so it outlives this listener
    [[no_unique_address]] Fn m_fn;
    SlotChunks<SlotCell<Item>> m_results; // per index; taken ones hold items
    BitVector m_taken; // own copy: the source's runs ahead while deferred
    Index m_count = 0;
    IndexDiff<Index> m_diff;
    bool m_dirty = false;
    // Whether sync() ran since the last delta; a new view is about to be read
    bool m_readSinceDelta = true;

    [[no_unique_address]] std::conditional_t<slotPath, SlotState,
                                             detail::NoMapSlotState> m_slot;
    [[no_unique_address]] std::conditional_t<
        slotPath, detail::NoMapGeneralState, GeneralState> m_general;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_VIEWS_MAPVIEW
