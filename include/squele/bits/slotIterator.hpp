#pragma once
#ifndef H_INCLUDED_SQUELE_BITS_SLOTITERATOR
#define H_INCLUDED_SQUELE_BITS_SLOTITERATOR

#include <cstddef>

namespace squele {

/**
 * Forward iterator over the items of a slot-based view, skipping free indices.
 * Shared by all slot views, so each one only provides `isTaken()`,
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
    SlotIterator(const ViewT *p_view, IndexT i)
        : m_view(p_view), m_index(skipFree(i)) {}

    const value_type &operator*() const { return (*m_view)[m_index]; }

    SlotIterator &operator++() {
        m_index = skipFree(static_cast<IndexT>(m_index + 1));
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
     * @returns the first taken index >= i, or slotCount() if none
     */
    IndexT skipFree(IndexT i) const {
        const IndexT end = m_view->slotCount();
        while (i < end && !m_view->isTaken(i))
            ++i;
        return i;
    }

    const ViewT *m_view = nullptr;
    IndexT m_index = 0;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_BITS_SLOTITERATOR
