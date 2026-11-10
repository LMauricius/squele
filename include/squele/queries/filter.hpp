#pragma once
#ifndef H_INCLUDED_SQUELE_QUERIES_FILTER
#define H_INCLUDED_SQUELE_QUERIES_FILTER

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
 * A `KeyCallable` that accepts a `const TItemT &` and returns whether the
 * item passes
 */
template <class P, class TItemT>
concept FilterPredicate =
    KeyCallable<P> && std::predicate<const P &, const TItemT &>;

namespace detail {

/**
 * The function pointer type that `Filter` uses by default, so existing code
 * that names `Filter<Q>` keeps its meaning
 */
template <class TSourceQ>
using PredicatePtr = bool (*)(const typename TSourceQ::View::Item &);

} // namespace detail

// Views take their source view, not its query, so their names stay short
// (`FilterView<Store>`); the query is recovered from the source's query
// and the predicate type
template <Viewable TSourceT,
          class TPredT = detail::PredicatePtr<typename TSourceT::Query>>
    requires SlotViewable<TSourceT>
class FilterView;

/**
 * A query for the items of a source that pass a predicate.
 * Its view is `FilterView`, which supports only `SlotViewable` sources yet.
 * @note The predicate is a `FilterPredicate`, so the query is comparable and
 * hashable. A function pointer is called indirectly; an empty functor or a
 * captureless lambda (`Filter{q, [](const T &x) { ... }}`) is inlined into
 * the view. A lambda is a new type each time it is written, so two written
 * lambdas give two views (as two pointers do); name the predicate to share
 * one view.
 */
template <Queryable TSourceQ, class TPredT = detail::PredicatePtr<TSourceQ>>
    requires FilterPredicate<TPredT, typename TSourceQ::View::Item>
class Filter {
  public:
    using SourceQ = TSourceQ;
    using Source = typename SourceQ::View;
    using Item = typename Source::Item;
    using Predicate = TPredT;
    using View = FilterView<Source, Predicate>;

    /**
     * @param source the source's query (`From{p_view}` for an original)
     * @param pred the predicate; a function pointer must not be null
     * (asserted). It must not throw
     */
    Filter(TSourceQ source, Predicate pred)
        : m_source(std::move(source)), m_pred(std::move(pred)) {
        if constexpr (std::is_pointer_v<Predicate>)
            assert(m_pred);
    }

    std::shared_ptr<View> result(Ledger &ledger) const {
        return std::make_shared<View>(ledger, *this);
    }

    const SourceQ &source() const { return m_source; }
    const Predicate &predicate() const { return m_pred; }

    bool operator==(const Filter &o) const {
        return m_source == o.m_source && callableEqual(m_pred, o.m_pred);
    }

  private:
    SourceQ m_source;
    [[no_unique_address]] Predicate m_pred;
};

} // namespace squele

template <class TSourceQ, class TPredT>
struct std::hash<squele::Filter<TSourceQ, TPredT>> {
    std::size_t operator()(const squele::Filter<TSourceQ, TPredT> &q) const {
        return squele::combineCallableHash(std::hash<TSourceQ>{}(q.source()),
                                           q.predicate());
    }
};

// The views last: they include this header, and `result()` needs them
// complete, so including the query alone is enough to use it
#include "squele/views/filterView.hpp"

#endif // H_INCLUDED_SQUELE_QUERIES_FILTER
