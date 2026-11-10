#pragma once
#ifndef H_INCLUDED_SQUELE_QUERIES_MAP
#define H_INCLUDED_SQUELE_QUERIES_MAP

#include "squele/bits/callable.hpp"
#include "squele/bits/result.hpp"
#include "squele/core/concepts.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace squele {

template <ListViewable TSourceT, class TFnT> class MapView;

/**
 * A query for the results of a function over each item of a source. The
 * results are owned values, recomputed only when their source item changes.
 * @note `Fn` is a `ResultFunction`: a plain function, or `ComparedResult{eq,
 * fn}` to skip reporting results equal to the old ones. Key rules as for the
 * `Filter` predicate. Results must be movable: the view swaps and moves
 * them when its source swaps items.
 */
template <Queryable TSourceQ, class TFnT>
    requires ListViewable<typename TSourceQ::View> &&
             ResultFunction<TFnT, typename TSourceQ::View::Item> &&
             std::movable<ResultOf<TFnT, typename TSourceQ::View::Item>>
class Map {
  public:
    using SourceQ = TSourceQ;
    using Source = typename SourceQ::View;
    using Fn = TFnT;
    using Item = ResultOf<Fn, typename Source::Item>;
    using View = MapView<Source, Fn>;

    /**
     * @param source the source's query (`From{p_view}` for an original)
     * @param fn the function; a function pointer must not be null
     * (asserted). It must not throw
     */
    Map(TSourceQ source, Fn fn)
        : m_source(std::move(source)), m_fn(std::move(fn)) {
        if constexpr (std::is_pointer_v<Fn>)
            assert(m_fn);
    }

    std::shared_ptr<View> result(Ledger &ledger) const {
        return std::make_shared<View>(ledger, *this);
    }

    const SourceQ &source() const { return m_source; }
    const Fn &function() const { return m_fn; }

    bool operator==(const Map &o) const {
        return m_source == o.m_source && callableEqual(m_fn, o.m_fn);
    }

  private:
    SourceQ m_source;
    [[no_unique_address]] Fn m_fn;
};

} // namespace squele

template <class TSourceQ, class TFnT>
struct std::hash<squele::Map<TSourceQ, TFnT>> {
    std::size_t operator()(const squele::Map<TSourceQ, TFnT> &q) const {
        return squele::combineCallableHash(std::hash<TSourceQ>{}(q.source()),
                                           q.function());
    }
};

// The views last: they include this header, and `result()` needs them
// complete, so including the query alone is enough to use it
#include "squele/views/mapView.hpp"

#endif // H_INCLUDED_SQUELE_QUERIES_MAP
