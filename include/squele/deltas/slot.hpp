#pragma once
#ifndef H_INCLUDED_SQUELE_DELTAS_SLOT
#define H_INCLUDED_SQUELE_DELTAS_SLOT

#include "squele/bits/slotBits.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace squele {

/**
 * The net change of one slot since the last flush. `nothing` (e.g. create then
 * erase) differs from `untouched`: the slot was recorded, but its changes
 * cancel out.
 */
enum class SlotChange : std::uint8_t {
    untouched = 0,
    nothing,
    created,
    deleted,
    replaced,
    modified
};

/**
 * The changes of 64 consecutive slots (one `BitWord` each) as four bit planes:
 * - `touched`: recorded since the last flush;
 * - `existed`: held an item at the last flush;
 * - `exists`: holds an item now;
 * - `same`: holds the item it held at the last flush (changed in place only).
 *
 * Only touched bits are meaningful, and `same` is a subset of
 * `existed & exists`, so every `SlotChange` is a mask expression and a whole
 * word is recorded with a few bit operations. The recording operations
 * compose like changes do: a create after a delete gives a replace, a delete
 * after a create gives nothing.
 * @note Recording an impossible sequence (a create on a slot that holds an
 * item, an erase or modify on one that does not) is UB; touched bits are
 * asserted, untouched ones cannot be checked.
 */
struct SlotWordChange {
    BitWord touched = 0;
    BitWord existed = 0;
    BitWord exists = 0;
    BitWord same = 0;

    BitWord created() const { return exists & ~existed; }
    BitWord deleted() const { return existed & ~exists; }
    BitWord replaced() const { return existed & exists & ~same; }
    BitWord modified() const { return same; }
    BitWord nothing() const { return touched & ~existed & ~exists; }

    /**
     * @returns the change of bit `b`
     */
    SlotChange kind(std::size_t b) const {
        const BitWord m = BitWord{1} << b;
        if (!(touched & m))
            return SlotChange::untouched;
        if (same & m)
            return SlotChange::modified;
        if (existed & m)
            return exists & m ? SlotChange::replaced : SlotChange::deleted;
        return exists & m ? SlotChange::created : SlotChange::nothing;
    }

    /**
     * Records that the free slots `m` became taken
     */
    void create(BitWord m) {
        assert(!(touched & exists & m));
        exists |= m;
        same &= ~m;
        touched |= m;
    }

    /**
     * Records that the taken slots `m` became free
     */
    void erase(BitWord m) {
        assert(!(touched & ~exists & m));
        existed |= m & ~touched; // newly touched slots held their item
        exists &= ~m;
        same &= ~m;
        touched |= m;
    }

    /**
     * Records that the items of the taken slots `m` changed in place
     */
    void modify(BitWord m) {
        assert(!(touched & ~exists & m));
        const BitWord fresh = m & ~touched;
        existed |= fresh;
        exists |= fresh;
        same |= fresh; // touched slots keep their kind
        touched |= m;
    }

    /**
     * Appends the changes `later` (recorded after these, starting from the
     * state these end in): the result is the net change of both.
     * A slot touched only by `later` takes its change; one touched by both
     * keeps its `existed` from these, takes `exists` from `later`, and is
     * still the same item only if both kept it.
     * @warning `later` must start where these end: a slot touched by both
     * existed in `later` exactly when it exists here (asserted).
     */
    void fold(const SlotWordChange &later) {
        const BitWord t = later.touched;
        assert(!((later.existed ^ exists) & touched & t));
        existed |= later.existed & ~touched;
        exists = (exists & ~t) | later.exists;
        same = (same & ~t) | ((same | ~touched) & later.same);
        touched |= t;
    }
};

/**
 * Flags naming the members of `SlotDelta` a listener reads, one bit per
 * member. A listener adds them to its source (`addInterests`), and the source
 * fills only the members someone asked for; the rest stay empty.
 * `count`, `slotCount`, `touchedCount` and `relocated` are always
 * filled.
 * `SPARSE_SPANS` asks for the four index spans only while the delta is
 * `sparse()`: a listener that handles dense deltas through the bit members
 * adds it instead of the four span flags, so a source with only such listeners
 * never builds spans for bulk changes.
 */
namespace SlotInterest {
using Mask = std::uint8_t;
inline constexpr Mask CREATED = 1 << 0;
inline constexpr Mask DELETED = 1 << 1;
inline constexpr Mask REPLACED = 1 << 2;
inline constexpr Mask MODIFIED = 1 << 3;
inline constexpr Mask SWAPPED = 1 << 4;
inline constexpr Mask WORDS = 1 << 5;
inline constexpr Mask TOUCHED_WORDS = 1 << 6;
inline constexpr Mask SPARSE_SPANS = 1 << 7;
inline constexpr Mask ALL = 0xFF;
} // namespace SlotInterest

/**
 * Changes of a slot-based view since its last flush.
 * A delta is temporary: its spans view storage owned by the flushing view, so
 * it is valid only during the listener call.
 * The changes come in two forms; a listener reads the cheaper one for its work:
 * - index spans (`created`, ...), for per-index work on a few changes;
 * - word blocks (`words`, `touchedWords`), for word-level work such as
 *   updating per-slot flags 64 at a time.
 * Both forms describe the changes after the `swapped` ones: a listener first
 * applies the swaps, in order, to its state at the last flush.
 * An item keeps its address unless it is created, replaced or swapped, or
 * `relocated` is set; a modified item keeps it too.
 * @note Every index appears in at most one span. Spans are *unordered*; a
 * consumer that needs index order walks the touched words instead.
 * @note A member is filled only while some listener has its `SlotInterest`
 * flag added; otherwise it is empty.
 */
template <std::integral TIndexT> struct SlotDelta {
    using IndexT = TIndexT;
    using Interests = SlotInterest::Mask;

    /**
     * Two indices whose items (or free slots) traded places; either may be
     * free. An item keeps its value, but not its address
     */
    struct Swap {
        IndexT a;
        IndexT b;
    };

    /**
     * The number of *taken* indices in the source
     */
    IndexT count;

    /**
     * One past the largest index that may be taken, and past every index this
     * delta touches. A view that shrinks (a dense view freeing its last
     * positions) applies the shrink only after the delta: it reports its
     * largest count since the last flush here, and the smaller one in `count`
     */
    IndexT slotCount;

    /**
     * The number of indices recorded since the last flush (the touched bits,
     * whether or not `words` is filled)
     */
    IndexT touchedCount;

    /**
     * All or nothing: when true, every item address read before this delta
     * is invalid; when false, every one stays valid, except those of the
     * items this delta lists as created, replaced or swapped. Always filled
     */
    bool relocated;

    /**
     * @returns whether the changes are sparse: below 2 per touched word, where
     * per-index work beats word-level work (measured with benchmarks/slot.cpp)
     * @note Needs `touchedWords` filled.
     */
    bool sparse() const {
        return static_cast<std::size_t>(touchedCount) < 2 * touchedWords.size();
    }

    /**
     * Indices that were free and are now taken
     */
    std::span<const IndexT> created;

    /**
     * Indices that were taken and are now free
     */
    std::span<const IndexT> deleted;

    /**
     * Indices whose item was deleted and a new one created in its place
     */
    std::span<const IndexT> replaced;

    /**
     * Indices whose item changed in place; its address stays the same (a
     * new address is reported as replaced)
     */
    std::span<const IndexT> modified;

    /**
     * Swaps in the order they happened, applied before every other change.
     * Not counted in `touchedCount`: a swapped item that did not change
     * otherwise is untouched at its new index
     */
    std::span<const Swap> swapped;

    /**
     * The changes per 64 slots, indexed by word (`index / bitWordBits`).
     * Meaningful only at the `touchedWords` entries; it may be shorter than
     * `slotCount` needs.
     */
    std::span<const SlotWordChange> words;

    /**
     * Indices of the `words` with at least one touched bit; unordered, each
     * once. Visiting only these keeps word-level work O(changed words).
     * @note It may also list words whose touched bits were all swapped away;
     * their blocks are all zero.
     */
    std::span<const IndexT> touchedWords;
};

/**
 * Self-sufficient accumulator of slot changes between flushes.
 * Coalesces repeated changes to the same index in O(1) and produces a
 * `SlotDelta` in O(w), for w touched words. Changes are recorded per index or
 * per word (a mask of 64 slots at once).
 * @note Recording an impossible sequence for one index (e.g. a create after a
 * create) is UB; it is asserted where it can be (see `SlotWordChange`).
 */
template <std::integral TIndexT> class SlotDiff {
  public:
    using IndexT = TIndexT;
    using DeltaT = SlotDelta<IndexT>;

    /**
     * Records that the free index `i` became taken
     */
    void create(IndexT i) { createWord(word(i), bit(i)); }

    /**
     * Records that the taken index `i` became free
     */
    void erase(IndexT i) { eraseWord(word(i), bit(i)); }

    /**
     * Records that the item at the taken index `i` changed in place
     */
    void modify(IndexT i) { modifyWord(word(i), bit(i)); }

    /**
     * Records that the items (or free slots) at the distinct indices `a` and
     * `b` traded places: the changes recorded for each follow its item, and
     * `{a, b}` is appended to the swaps
     * @note Strong exception guarantee.
     */
    void swap(IndexT a, IndexT b) {
        assert(a != b);
        // Every allocating step first; growing adds untouched blocks only
        reserve(std::max(word(a), word(b)) + 1);
        const bool first = m_swaps.empty();
        m_swaps.push_back({a, b});
        if (first)
            markListed();
        swapBits(a, b);
    }

    /**
     * Word-level `create`: records the indices `w * bitWordBits + b` for
     * every set bit `b` of `m`. The same holds for `eraseWord`, `modifyWord`.
     * @note Strong exception guarantee, as for every recording call.
     */
    void createWord(std::size_t w, BitWord m) { touch(w, m).create(m); }
    void eraseWord(std::size_t w, BitWord m) { touch(w, m).erase(m); }
    void modifyWord(std::size_t w, BitWord m) { touch(w, m).modify(m); }

    /**
     * Records that every item may have moved to a new address (see
     * `SlotDelta::relocated`)
     */
    void relocate() noexcept { m_relocated = true; }

    /**
     * Appends the changes of `d` (taken from the same view after the changes
     * already here): afterwards this diff holds the net change of both, as
     * if every change of `d` had been recorded here. O(touched words of `d`).
     * @param d a delta with `words` and `touchedWords` filled
     * @note Strong exception guarantee: everything that allocates runs
     * before the first word is folded.
     */
    void fold(const DeltaT &d) {
        std::size_t words = d.words.size();
        for (const auto &s : d.swapped)
            words = std::max({words, word(s.a) + 1, word(s.b) + 1});
        reserve(words);
        const bool first = m_swaps.empty() && !d.swapped.empty();
        m_swaps.insert(m_swaps.end(), d.swapped.begin(), d.swapped.end());
        if (first)
            markListed();
        // The later swaps permute the changes recorded here, before the later
        // changes (which start after them) are folded
        for (const auto &s : d.swapped)
            swapBits(s.a, s.b);
        for (IndexT w : d.touchedWords) {
            const auto at = static_cast<std::size_t>(w);
            const SlotWordChange &later = d.words[at];
            touch(at, later.touched).fold(later);
        }
        m_relocated |= d.relocated;
    }

    /**
     * @returns the accumulated changes; only the members flagged in `wanted`
     * are filled, the rest are empty
     * @note The returned spans are valid until this diff changes
     */
    DeltaT delta(IndexT count, IndexT slotCount, SlotInterest::Mask wanted) {
        namespace SI = SlotInterest;
        // The word members view the diff's own state, so they cost nothing
        DeltaT d{};
        d.count = count;
        d.slotCount = slotCount;
        d.touchedCount = static_cast<IndexT>(m_touchedCount);
        d.relocated = m_relocated;
        if (wanted & SI::WORDS)
            d.words = m_words;
        if (wanted & SI::TOUCHED_WORDS)
            d.touchedWords = m_touchedWords;
        if (wanted & SI::SWAPPED)
            d.swapped = m_swaps;

        // SPARSE_SPANS is every span while the delta is sparse (same rule as
        // DeltaT::sparse(), on the diff's own words)
        constexpr SI::Mask spanFlags =
            SI::CREATED | SI::DELETED | SI::REPLACED | SI::MODIFIED;
        SI::Mask spans = wanted & spanFlags;
        if (wanted & SI::SPARSE_SPANS &&
            m_touchedCount < 2 * m_touchedWords.size())
            spans = spanFlags;
        if (!spans)
            return d;

        // Unwanted kinds get an all-zero mask, so the walk has no branches
        // on `spans`
        auto keep = [&](SI::Mask f) { return spans & f ? ~BitWord{0} : 0; };
        const BitWord kc = keep(SI::CREATED), kd = keep(SI::DELETED),
                      kr = keep(SI::REPLACED), km = keep(SI::MODIFIED);
        // Two buffers hold the four spans: created grows up from the start of
        // the first and deleted down from its end, replaced and modified
        // likewise in the second. Two kinds never total more than the touched
        // count, so they cannot cross. The buffers only grow (to the largest
        // touched count so far), so the walk writes through raw pointers with
        // no capacity checks, and steady-state flushing neither allocates nor
        // initializes
        if (m_spansA.size() < m_touchedCount) {
            m_spansA.resize(m_touchedCount);
            m_spansB.resize(m_touchedCount);
        }
        IndexT *const p_c0 = m_spansA.data();
        IndexT *const p_d0 = p_c0 + m_spansA.size();
        IndexT *const p_r0 = m_spansB.data();
        IndexT *const p_m0 = p_r0 + m_spansB.size();
        IndexT *p_c = p_c0, *p_d = p_d0, *p_r = p_r0, *p_m = p_m0;
        // Upward spans come out ascending, downward ones descending
        for (IndexT w : m_touchedWords) {
            const auto at = static_cast<std::size_t>(w);
            const SlotWordChange &c = m_words[at];
            // Emptied by swaps: the lone-change path below needs a set bit
            if (!c.touched)
                continue;
            const std::size_t base = at * bitWordBits;
            const BitWord created = c.created() & kc,
                          deleted = c.deleted() & kd,
                          replaced = c.replaced() & kr,
                          modified = c.modified() & km;
            // A lone change (the sparse case) is written to every span and
            // kept by advancing only its own; branching on its kind would
            // mispredict once per word. Writes stay in bounds: before this
            // index, each buffer's two spans leave at least one free entry,
            // and where the up and down cursors meet they write the same index
            if ((c.touched & (c.touched - 1)) == 0) {
                const auto i = static_cast<IndexT>(
                    base +
                    static_cast<std::size_t>(std::countr_zero(c.touched)));
                *p_c = i;
                p_c += created != 0;
                p_d[-1] = i;
                p_d -= deleted != 0;
                *p_r = i;
                p_r += replaced != 0;
                p_m[-1] = i;
                p_m -= modified != 0;
                continue;
            }
            p_c = writeBits(p_c, base, created);
            p_d = writeBitsDown(p_d, base, deleted);
            p_r = writeBits(p_r, base, replaced);
            p_m = writeBitsDown(p_m, base, modified);
        }
        d.created = {p_c0, p_c};
        d.deleted = {p_d, p_d0};
        d.replaced = {p_r0, p_r};
        d.modified = {p_m, p_m0};
        return d;
    }

    /**
     * Forgets all recorded changes, in O(w)
     */
    void clear() {
        for (IndexT w : m_touchedWords)
            m_words[static_cast<std::size_t>(w)] = {};
        if (!m_swaps.empty())
            for (IndexT w : m_touchedWords)
                m_listed.reset(static_cast<std::size_t>(w));
        m_touchedWords.clear();
        m_swaps.clear();
        m_touchedCount = 0;
        m_relocated = false;
    }

    /**
     * @returns true when nothing was recorded since the last clear()
     * @note A create followed by an erase of the same index still counts as
     * recorded; its delta is just empty. So do a swap and a relocation
     */
    bool empty() const {
        return m_touchedWords.empty() && m_swaps.empty() && !m_relocated;
    }

  private:
    static std::size_t word(IndexT i) {
        return static_cast<std::size_t>(i) / bitWordBits;
    }
    static BitWord bit(IndexT i) {
        return BitWord{1} << (static_cast<std::size_t>(i) % bitWordBits);
    }

    /**
     * Writes the indices of the set bits of `m` (word starting at `base`) to
     * `p_out`
     * @returns one past the last written index
     */
    static IndexT *writeBits(IndexT *p_out, std::size_t base, BitWord m) {
        for (; m; m &= m - 1)
            *p_out++ = static_cast<IndexT>(
                base + static_cast<std::size_t>(std::countr_zero(m)));
        return p_out;
    }

    /**
     * `writeBits` downwards: writes the indices before `p_end`
     * @returns the first written index
     */
    static IndexT *writeBitsDown(IndexT *p_end, std::size_t base, BitWord m) {
        for (; m; m &= m - 1)
            *--p_end = static_cast<IndexT>(
                base + static_cast<std::size_t>(std::countr_zero(m)));
        return p_end;
    }

    /**
     * Grows the blocks to at least `words` and reserves the word list for
     * all of them, so listing a word afterwards never allocates. Growing adds
     * untouched blocks only, so it changes nothing visible.
     */
    void reserve(std::size_t words) {
        if (words > m_words.size()) {
            m_words.resize(words);
            m_listed.resize(words);
        }
        // A word is listed at most once, so the list never outgrows the
        // blocks; reserving their capacity (which grows geometrically) keeps
        // this amortized O(1)
        if (m_touchedWords.capacity() < m_words.size())
            m_touchedWords.reserve(m_words.capacity());
    }

    /**
     * Lists word `w` unless it is listed already
     * @warning Needs `reserve()` to cover `w` (not checked).
     */
    void list(std::size_t w) noexcept {
        // Before the first swap since clear(), listed means touched, so
        // the caller's untouched word is unlisted and m_listed stays unused
        // (it costs about 10% on sparse recording)
        if (!m_swaps.empty()) {
            if (m_listed.test(w))
                return;
            m_listed.set(w);
        }
        m_touchedWords.push_back(static_cast<IndexT>(w));
    }

    /**
     * Fills `m_listed` from the word list, on the first swap since clear()
     */
    void markListed() noexcept {
        for (IndexT w : m_touchedWords)
            m_listed.set(static_cast<std::size_t>(w));
    }

    /**
     * Swaps the four plane bits of `a` and `b`; lists the word that gains a
     * touched bit. The touched count is unchanged (bits only move)
     * @warning Needs `reserve()` to cover both words (not checked).
     */
    void swapBits(IndexT a, IndexT b) noexcept {
        SlotWordChange &ca = m_words[word(a)], &cb = m_words[word(b)];
        const auto sa = static_cast<std::size_t>(a) % bitWordBits;
        const auto sb = static_cast<std::size_t>(b) % bitWordBits;
        // XOR swap of one bit per plane; ca and cb may be the same block, so
        // the difference is read before either write
        for (BitWord SlotWordChange::*p :
             {&SlotWordChange::touched, &SlotWordChange::existed,
              &SlotWordChange::exists, &SlotWordChange::same}) {
            const BitWord x = ((ca.*p >> sa) ^ (cb.*p >> sb)) & 1;
            ca.*p ^= x << sa;
            cb.*p ^= x << sb;
        }
        // A word emptied by the swap stays listed (m_listed prevents listing
        // it twice); clear() and the consumers skip it
        if (ca.touched)
            list(word(a));
        if (cb.touched)
            list(word(b));
    }

    /**
     * Prepares word `w` for recording the slots `m`: runs every allocating
     * step first (strong exception guarantee), then counts the newly touched
     * slots.
     * @returns the word's block, for the caller to record into
     */
    SlotWordChange &touch(std::size_t w, BitWord m) {
        if (w >= m_words.size())
            reserve(w + 1);
        SlotWordChange &c = m_words[w];
        // Only an untouched word can be unlisted (or emptied by swaps)
        if (!c.touched && m)
            list(w);
        const BitWord fresh = m & ~c.touched;
        // One fresh bit is the common case; it skips the popcount (a libgcc
        // call without -mpopcnt)
        m_touchedCount += (fresh & (fresh - 1)) == 0
                              ? static_cast<std::size_t>(fresh != 0)
                              : static_cast<std::size_t>(std::popcount(fresh));
        return c;
    }

    // One block per 64 slots, grown on demand; only touched blocks are
    // non-zero. Aligned so a block never splits a cache line
    std::vector<SlotWordChange, AlignedAllocator<SlotWordChange, bitWordAlign>>
        m_words;
    // Indices of the touched m_words; each appears once, which m_listed
    // (one bit per block, kept only while there are swaps) keeps true after
    // swaps empty a listed word
    std::vector<IndexT> m_touchedWords;
    BitVector m_listed;
    // Every swap since the last clear, in order
    std::vector<typename DeltaT::Swap> m_swaps;
    // The number of touched bits
    std::size_t m_touchedCount = 0;
    bool m_relocated = false;
    // Output buffers that delta() spans over (two spans each, from both
    // ends); their size is only a capacity
    std::vector<IndexT> m_spansA, m_spansB;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_DELTAS_SLOT
