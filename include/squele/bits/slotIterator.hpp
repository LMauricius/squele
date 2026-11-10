#pragma once
#ifndef H_INCLUDED_SQUELE_BITS_SLOTITERATOR
#define H_INCLUDED_SQUELE_BITS_SLOTITERATOR

#include "squele/bits/indexBits.hpp"

#include <bit>
#include <cstddef>

namespace squele {

/**
 * Forward iterator over the items of a slot-based view, skipping free indices.
 * Shared by all slot views, so each one only provides `takenBits()`,
 * `operator[]` and `slotCount()`.
 * @note Invalidated by any change to the view.
 */
template <class TViewT> class SlotIterator {
  public:
    using ViewT = TViewT;
    using IndexT = typename ViewT::Index;
    using value_type = typename ViewT::Item;
    using difference_type = std::ptrdiff_t;

    SlotIterator() = default;

    /**
     * @param p_view the iterated view
     * @param i the first index to consider; free indices after it are skipped
     */
    SlotIterator(const ViewT *p_view, IndexT i) : m_view(p_view) {
        seek(static_cast<std::size_t>(i));
    }

    const value_type &operator*() const { return (*m_view)[m_index]; }

    SlotIterator &operator++() {
        // Only `m_word &= m_word - 1` carries over between steps; the index
        // is derived from it, so dense iteration is not latency-bound
        m_word &= m_word - 1;
        const auto base =
            static_cast<std::size_t>(m_index) & ~(bitWordBits - 1);
        if (m_word)
            m_index = static_cast<IndexT>(
                base + static_cast<std::size_t>(std::countr_zero(m_word)));
        else
            seek(base + bitWordBits);
        return *this;
    }

    SlotIterator operator++(int) {
        SlotIterator old = *this;
        ++*this;
        return old;
    }

    /**
     * Compares positions of iterators over the same view
     */
    bool operator==(const SlotIterator &o) const {
        return m_index == o.m_index;
    }

    /**
     * @returns the current index
     */
    IndexT index() const { return m_index; }

  private:
    /**
     * Moves to the first taken index >= i, or slotCount() if none; skips 64
     * free indices per step. Loads that index's word, minus the bits below it.
     */
    void seek(std::size_t i) {
        const BitSpan taken = m_view->takenBits();
        const std::size_t at = taken.nextSet(i);
        m_index = static_cast<IndexT>(at);
        m_word = at < taken.size() ? taken.words()[at / bitWordBits] &
                                         (~BitWord{0} << (at % bitWordBits))
                                   : 0;
    }

    const ViewT *m_view = nullptr;
    IndexT m_index = 0;
    // The taken bits of m_index's word, at and above m_index
    BitWord m_word = 0;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_BITS_SLOTITERATOR
