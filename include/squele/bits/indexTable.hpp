#pragma once
#ifndef H_INCLUDED_SQUELE_BITS_INDEXTABLE
#define H_INCLUDED_SQUELE_BITS_INDEXTABLE

#include "squele/deltas/indexable.hpp"

namespace squele {

template <std::integral TIndexT> class IndexTable {
  public:
    using IndexT = TIndexT;

    /**
     * The number of *used* indices
     */
    IndexT activeCount() const;

    /**
     * The largest possibly used index
     */
    IndexT maxIndex() const;

    /**
     * @returns an *unused* index <=maxIndex
     */
    IndexT getUnused() {

    }

    /**
     * Makes the index *added* and *used*
     * @param ind an unused, but available index
     * @note If the index is used, it is UB
     */
    void notifyAdded(IndexT ind) {

    }
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_BITS_INDEXTABLE