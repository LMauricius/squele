#pragma once
#ifndef H_INCLUDED_SQUELE_BITS_SLOTINDEXPOOL
#define H_INCLUDED_SQUELE_BITS_SLOTINDEXPOOL

#include "squele/bits/slotBits.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <limits>
#include <vector>

namespace squele {

/**
 * Hands out and reclaims slot indices; freed indices are reused last freed
 * first, in O(1).
 */
template <std::integral TIndexT> class SlotIndexPool {
  public:
    using IndexT = TIndexT;

    /**
     * Takes a free index, or a new one equal to the old `slotCount()` when none
     * is free (so the owner knows to grow its storage). O(1) amortized.
     * @note Strong exception guarantee.
     * @warning Running out of `IndexT` values is UB (asserted).
     */
    IndexT acquire() {
        if (m_freeCount > 0) {
            const IndexT i = m_free[--m_freeCount];
            m_taken.set(static_cast<std::size_t>(i));
            return i;
        }
        const std::size_t i = m_taken.size();
        assert(i <
               static_cast<std::size_t>(std::numeric_limits<IndexT>::max()));
        // m_free grows first: if m_taken then throws, the spare entry is
        // reused by the next append
        if (m_free.size() == i)
            m_free.emplace_back();
        m_taken.push_back(true);
        return static_cast<IndexT>(i);
    }

    /**
     * Frees the index `i`. Never throws.
     * @warning Releasing a free index is UB (asserted).
     */
    void release(IndexT i) noexcept {
        assert(isTaken(i));
        m_taken.reset(static_cast<std::size_t>(i));
        m_free[m_freeCount++] = i;
    }

    /**
     * @returns whether `i` is taken; false for any index out of range
     */
    bool isTaken(IndexT i) const {
        const auto at = static_cast<std::size_t>(i);
        return at < m_taken.size() && m_taken.test(at);
    }

    /**
     * @returns the taken flags, one bit per index
     */
    BitSpan takenBits() const { return m_taken; }

    /**
     * @returns the number of taken indices
     */
    IndexT count() const {
        return static_cast<IndexT>(m_taken.size() - m_freeCount);
    }

    /**
     * @returns one past the largest index that may be taken
     */
    IndexT slotCount() const { return static_cast<IndexT>(m_taken.size()); }

  private:
    BitVector m_taken;
    // Free stack. Sized ahead to m_taken.size(), so release() never allocates;
    // only the first m_freeCount entries are meaningful
    std::vector<IndexT> m_free;
    std::size_t m_freeCount = 0;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_BITS_SLOTINDEXPOOL
