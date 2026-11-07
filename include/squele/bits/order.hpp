#pragma once
#ifndef H_INCLUDED_SQUELE_BITS_ORDER
#define H_INCLUDED_SQUELE_BITS_ORDER

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <span>
#include <utility>

namespace squele {

/**
 * Merges the sorted `batch` into the sorted first `n` elements of `out`, in
 * place from the back. A sort view needs it to re-sort only its changed
 * items: O(k log n) comparisons and O(n - p) moves for k batch elements,
 * instead of a full sort.
 * A batch element goes after its equals already in `out` (an upper bound), so
 * fewer elements move.
 * @param out `n` sorted elements, then `batch.size()` elements to overwrite
 * @param batch sorted by `less`
 * @returns the first position whose element changed (`n` if `batch` is empty)
 */
template <class T, class TLess>
std::size_t mergeSortedBack(std::span<T> out, std::size_t n,
                            std::span<const T> batch, TLess less) {
    // Largest batch element first: it lands past every element it does not
    // precede, and the search range shrinks to the elements left before it
    std::size_t end = n;
    for (std::size_t j = batch.size(); j-- > 0;) {
        const auto at = static_cast<std::size_t>(
            std::upper_bound(out.begin(), out.begin() + end, batch[j], less) -
            out.begin());
        // Shift the elements after the insertion point past the j batch
        // elements still to come, then place this one
        std::move_backward(out.begin() + at, out.begin() + end,
                           out.begin() + end + j + 1);
        out[at + j] = batch[j];
        end = at;
    }
    return end;
}

/**
 * Applies the permutation `to` by swaps: the element at offset `i` goes to
 * offset `to[i]`, and `swap(base + i, base + j)` is called for each swap. A
 * sort view reports its reordering as swaps; this cycle walk needs at most one
 * swap per moved element and no memory besides `to`.
 * @param to a permutation of `0..to.size()-1`; left as the identity
 */
template <std::integral TIndexT, class TSwapF>
void permuteBySwaps(std::span<TIndexT> to, TIndexT base, TSwapF &&swap) {
    for (std::size_t i = 0; i < to.size(); ++i) {
        // Each swap puts the element now at i where it belongs (j); i then
        // holds j's old element, which takes over j's destination
        while (static_cast<std::size_t>(to[i]) != i) {
            const TIndexT j = to[i];
            swap(static_cast<TIndexT>(base + static_cast<TIndexT>(i)),
                 static_cast<TIndexT>(base + j));
            std::swap(to[i], to[static_cast<std::size_t>(j)]);
        }
    }
}

} // namespace squele

#endif // H_INCLUDED_SQUELE_BITS_ORDER
