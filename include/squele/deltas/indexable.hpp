#pragma once
#ifndef H_INCLUDED_SQUELE_DELTAS_INDEXABLE
#define H_INCLUDED_SQUELE_DELTAS_INDEXABLE

#include <concepts>
#include <span>

namespace squele {

template <std::integral TIndexT> struct IndexableDelta {
    using IndexT = TIndexT;

    /**
     * The number of *used* indices in the source
     */
    IndexT activeCount;

    /**
     * The largest possibly used index
     */
    IndexT maxIndex;

    /**
     * An iterable of *ordered* indices of added elements
     */
    std::span<IndexT> addedIndices;

    /**
     * An iterable of *ordered* indices of removed elements
     */
    std::span<IndexT> removedIndices;

    /**
     * An iterable of *ordered* indices of modified elements
     */
    std::span<IndexT> modifiedIndices;
};
} // namespace squele

#endif // H_INCLUDED_SQUELE_DELTAS_INDEXABLE