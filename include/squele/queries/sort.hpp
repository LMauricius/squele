#pragma once
#ifndef H_INCLUDED_SQUELE_QUERIES_SORT
#define H_INCLUDED_SQUELE_QUERIES_SORT

#include "squele/bits/callable.hpp"
#include "squele/core/concepts.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace squele {

/**
 * A `KeyCallable` that is a strict weak order on `TItemT`
 */
template <class C, class TItemT>
concept SortComparator =
    KeyCallable<C> &&
    std::strict_weak_order<const C &, const TItemT &, const TItemT &>;

template <ListViewable TSourceT, class TCmpT = std::ranges::less>
class ArraySortView;

/**
 * A query for the items of a source, sorted by a comparator.
 * Its view is `ArraySortView`.
 * @note The comparator is a `SortComparator`, keyed like the `Filter`
 * predicate. The order of equal items is unspecified (the sort is not stable).
 */
template <Queryable TSourceQ, class TCmpT = std::ranges::less>
    requires ListViewable<typename TSourceQ::View> &&
             SortComparator<TCmpT, typename TSourceQ::View::Item>
class Sort {
  public:
    using SourceQ = TSourceQ;
    using Source = typename SourceQ::View;
    using Item = typename Source::Item;
    using Compare = TCmpT;
    using View = ArraySortView<Source, Compare>;

    /**
     * @param source the source's query (`From{p_view}` for an original)
     * @param cmp the comparator; a function pointer must not be null
     * (asserted). It must not throw
     */
    explicit Sort(TSourceQ source, Compare cmp = {})
        : m_source(std::move(source)), m_cmp(std::move(cmp)) {
        if constexpr (std::is_pointer_v<Compare>)
            assert(m_cmp);
    }

    std::shared_ptr<View> result(Ledger &ledger) const {
        return std::make_shared<View>(ledger, *this);
    }

    const SourceQ &source() const { return m_source; }
    const Compare &compare() const { return m_cmp; }

    bool operator==(const Sort &o) const {
        return m_source == o.m_source && callableEqual(m_cmp, o.m_cmp);
    }

  private:
    SourceQ m_source;
    [[no_unique_address]] Compare m_cmp;
};

} // namespace squele

template <class TSourceQ, class TCmpT>
struct std::hash<squele::Sort<TSourceQ, TCmpT>> {
    std::size_t operator()(const squele::Sort<TSourceQ, TCmpT> &q) const {
        return squele::combineCallableHash(std::hash<TSourceQ>{}(q.source()),
                                           q.compare());
    }
};

// The views last: they include this header, and `result()` needs them
// complete, so including the query alone is enough to use it
#include "squele/views/arraySortView.hpp"

#endif // H_INCLUDED_SQUELE_QUERIES_SORT
