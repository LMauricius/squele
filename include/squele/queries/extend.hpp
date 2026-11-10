#pragma once
#ifndef H_INCLUDED_SQUELE_QUERIES_EXTEND
#define H_INCLUDED_SQUELE_QUERIES_EXTEND

#include "squele/bits/callable.hpp"
#include "squele/bits/hash.hpp"
#include "squele/bits/result.hpp"
#include "squele/core/concepts.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace squele {

namespace detail {

/**
 * The `Item` argument that `Extend`'s deduction guide passes: the item is
 * then `std::pair` of two members, else `std::tuple`
 */
struct AutoItem {};

/**
 * What one argument (or view part) of `Extend` is. Queries and their views
 * map to the same members, so the query and the view compute one item type.
 * A function by default.
 */
template <class P> struct ExtendPart {
    static constexpr bool isSource = false;
    using ItemTuple = std::tuple<>;
    using ViewTuple = std::tuple<>;
    using ViewOrFn = P;
};
template <Queryable P> struct ExtendPart<P> {
    static constexpr bool isSource = true;
    using View = typename P::View;
    using ItemTuple = std::tuple<typename View::Item>;
    using ViewTuple = std::tuple<View>;
    using ViewOrFn = View;
};
template <Viewable P> struct ExtendPart<P> {
    static constexpr bool isSource = true;
    using View = P;
    using ItemTuple = std::tuple<typename View::Item>;
    using ViewTuple = std::tuple<View>;
    using ViewOrFn = View;
};

/**
 * The query part of a view part: a source view's query, or the function
 */
template <class P> struct ExtendQueryPart {
    using type = P;
};
template <Viewable P> struct ExtendQueryPart<P> {
    using type = typename P::Query;
};

/**
 * The source items (or source views) of the parts, in order, as a tuple
 */
template <class... P>
using ExtendSourceItems = decltype(std::tuple_cat(
    std::declval<typename ExtendPart<P>::ItemTuple>()...));
template <class... P>
using ExtendSourceViews = decltype(std::tuple_cat(
    std::declval<typename ExtendPart<P>::ViewTuple>()...));

/**
 * Whether the function `F` fits the source items `TItems` (a tuple): a
 * plain `KeyCallable` called with one `const` item per source
 */
template <class F, class TItems>
inline constexpr bool extendFunctionFits = false;
template <class F, class... I>
inline constexpr bool extendFunctionFits<F, std::tuple<I...>> =
    KeyCallable<F> && !isComparedResult<F> &&
    std::regular_invocable<const F &, const I &...>;

/**
 * The item member of part `P` over the source items `TItems`: a pointer to
 * a source's item, or a function's owned result
 */
template <class P, class TItems> struct ExtendMember {
    using type = const typename ExtendPart<P>::View::Item *;
};
template <class P, class... I>
    requires(!ExtendPart<P>::isSource)
struct ExtendMember<P, std::tuple<I...>> {
    using type = std::decay_t<std::invoke_result_t<const P &, const I &...>>;
};

/**
 * Whether part `P` is valid over the source items `TItems`
 */
template <class P, class TItems> consteval bool extendPartValid() {
    if constexpr (ExtendPart<P>::isSource)
        return ListViewable<typename ExtendPart<P>::View>;
    else
        return extendFunctionFits<P, TItems>;
}

/**
 * The item type: `TItemT`, or the default for `AutoItem`
 */
template <class TItemT, class... M> struct ExtendItemOf {
    using type = TItemT;
};
template <class... M> struct ExtendItemOf<AutoItem, M...> {
    using type = std::tuple<M...>;
};
template <class M0, class M1> struct ExtendItemOf<AutoItem, M0, M1> {
    using type = std::pair<M0, M1>;
};

template <class TItemT, class... P>
using ExtendItem = typename ExtendItemOf<
    TItemT, typename ExtendMember<P, ExtendSourceItems<P...>>::type...>::type;

} // namespace detail

/**
 * The arguments of `Extend` (queries or views and functions, in any order):
 * at least two, at least one a source, every source `ListViewable`, every
 * function a plain `KeyCallable` called with one item per source, and the
 * item constructible as `Item{members...}` in argument order and movable
 * (rebuilt items move into place; one source's swaps move items)
 */
template <class TItemT, class... P>
concept ExtendArguments =
    sizeof...(P) >= 2 && (detail::ExtendPart<P>::isSource || ...) &&
    (detail::extendPartValid<P, detail::ExtendSourceItems<P...>>() && ...) &&
    requires(
        typename detail::ExtendMember<P, detail::ExtendSourceItems<P...>>::type
            &&...m) { detail::ExtendItem<TItemT, P...>{std::move(m)...}; } &&
    std::movable<detail::ExtendItem<TItemT, P...>>;

template <class TItemT, class... TPartsT> class ExtendView;

/**
 * A query that combines sources and function results into one view: the
 * n-th item holds a pointer to the n-th item of each source and the result
 * of each function over those items, in argument order; `count()` is the
 * smallest source count. Without functions it zips its sources.
 * @note `Item` defaults (through the deduction guide) to `std::pair` of two
 * members and `std::tuple` of more. A struct with named members works too:
 * `Extend<Row, Q, Fn>{q, fn}`. Functions follow the key rules of the
 * `Filter` predicate, get one `const` item per source, and must not throw.
 */
template <class TItemT, class... TArgsT>
    requires ExtendArguments<TItemT, TArgsT...>
class Extend {
  public:
    using Item = detail::ExtendItem<TItemT, TArgsT...>;
    using View =
        ExtendView<TItemT, typename detail::ExtendPart<TArgsT>::ViewOrFn...>;
    using Args = std::tuple<TArgsT...>;

    /**
     * @param args the source queries (`From{p_view}` for an original) and
     * functions; a function pointer must not be null (asserted)
     */
    explicit Extend(TArgsT... args) : m_args(std::move(args)...) {
        std::apply([](const auto &...a) { ((assertNotNull(a)), ...); }, m_args);
    }

    std::shared_ptr<View> result(Ledger &ledger) const {
        return std::make_shared<View>(ledger, *this);
    }

    const Args &args() const { return m_args; }

    bool operator==(const Extend &o) const {
        return [&]<std::size_t... K>(std::index_sequence<K...>) {
            return (partEqual(std::get<K>(m_args), std::get<K>(o.m_args)) &&
                    ...);
        }(std::index_sequence_for<TArgsT...>{});
    }

  private:
    template <class A> static void assertNotNull([[maybe_unused]] const A &a) {
        if constexpr (std::is_pointer_v<A>)
            assert(a);
    }
    template <class A> static bool partEqual(const A &a, const A &b) {
        if constexpr (detail::ExtendPart<A>::isSource)
            return a == b;
        else
            return callableEqual(a, b);
    }

    Args m_args;
};

template <class... TArgsT>
Extend(TArgsT...) -> Extend<detail::AutoItem, TArgsT...>;

} // namespace squele

template <class TItemT, class... TArgsT>
struct std::hash<squele::Extend<TItemT, TArgsT...>> {
    std::size_t operator()(const squele::Extend<TItemT, TArgsT...> &q) const {
        std::size_t h = 0;
        std::apply([&](const auto &...a) { ((h = part(h, a)), ...); },
                   q.args());
        return h;
    }

  private:
    template <class A> static std::size_t part(std::size_t h, const A &a) {
        if constexpr (squele::detail::ExtendPart<A>::isSource)
            return squele::combineHash(h, std::hash<A>{}(a));
        else
            return squele::combineCallableHash(h, a);
    }
};

// The views last: they include this header, and `result()` needs them
// complete, so including the query alone is enough to use it
#include "squele/views/extendView.hpp"

#endif // H_INCLUDED_SQUELE_QUERIES_EXTEND
