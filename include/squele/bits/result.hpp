#pragma once
#ifndef H_INCLUDED_SQUELE_BITS_RESULT
#define H_INCLUDED_SQUELE_BITS_RESULT

#include "squele/bits/callable.hpp"

#include <concepts>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace squele {

/**
 * A result function paired with an equality on its results, so a view of
 * owned results (`Map`, `Extend`) reports a recomputed result only when it
 * differs from the old one. Written `ComparedResult{eq, fn}`.
 * @note Without it, every recomputed result is reported modified: `==` may
 * miss changes behind pointers or other data a result refers to.
 */
template <KeyCallable TEqT, KeyCallable TFnT> struct ComparedResult {
    using EqT = TEqT;
    using FnT = TFnT;

    [[no_unique_address]] EqT eq;
    [[no_unique_address]] FnT fn;

    bool operator==(const ComparedResult &o) const {
        return callableEqual(eq, o.eq) && callableEqual(fn, o.fn);
    }
};

namespace detail {

/**
 * Whether `F` is a `ComparedResult`; `resultFn` and `recomputeResult` need
 * it to tell the two forms apart
 */
template <class F> inline constexpr bool isComparedResult = false;
template <class E, class G>
inline constexpr bool isComparedResult<ComparedResult<E, G>> = true;

} // namespace detail

/**
 * The function that computes results: `f` itself, or the `fn` of a
 * `ComparedResult`. Views call it to construct a new result.
 */
template <class F> const auto &resultFn(const F &f) {
    if constexpr (detail::isComparedResult<F>)
        return f.fn;
    else
        return f;
}

/**
 * A result function (plain or `ComparedResult`) over `const TArgs &...`,
 * with an equality on its results when compared; a `KeyCallable`, so it
 * can be part of a query key
 */
template <class F, class... TArgs>
concept ResultFunction =
    KeyCallable<F> &&
    std::regular_invocable<const std::remove_cvref_t<decltype(resultFn(
                               std::declval<const F &>()))> &,
                           const TArgs &...> &&
    (!detail::isComparedResult<F> ||
     std::equivalence_relation<
         const typename F::EqT &,
         const std::decay_t<
             std::invoke_result_t<const typename F::FnT &, const TArgs &...>> &,
         const std::decay_t<std::invoke_result_t<const typename F::FnT &,
                                                 const TArgs &...>> &>);

/**
 * The owned result type of `F` over `TArgs...`: the decayed return type
 */
template <class F, class... TArgs>
using ResultOf = std::decay_t<std::invoke_result_t<
    decltype(resultFn(std::declval<const F &>())), const TArgs &...>>;

/**
 * Recomputes `r` from `args`. The shared update step of `Map` and `Extend`.
 * @returns whether `r` changed: always true for a plain function; for a
 * `ComparedResult`, whether `eq` found the new result different (only then
 * is it assigned)
 */
template <class F, class R, class... TArgs>
bool recomputeResult(const F &f, R &r, const TArgs &...args) {
    if constexpr (detail::isComparedResult<F>) {
        R next = std::invoke(f.fn, args...);
        if (std::invoke(f.eq, std::as_const(r), std::as_const(next)))
            return false;
        r = std::move(next);
    } else {
        r = std::invoke(f, args...);
    }
    return true;
}

} // namespace squele

template <class TEqT, class TFnT>
struct std::hash<squele::ComparedResult<TEqT, TFnT>> {
    std::size_t operator()(const squele::ComparedResult<TEqT, TFnT> &c) const {
        return squele::combineCallableHash(squele::combineCallableHash(0, c.eq),
                                           c.fn);
    }
};

#endif // H_INCLUDED_SQUELE_BITS_RESULT
