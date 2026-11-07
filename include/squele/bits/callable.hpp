#pragma once
#ifndef H_INCLUDED_SQUELE_BITS_CALLABLE
#define H_INCLUDED_SQUELE_BITS_CALLABLE

#include "squele/bits/hash.hpp"

#include <concepts>
#include <cstddef>
#include <functional>
#include <type_traits>

namespace squele {

/**
 * A callable that can be part of a query key: it is copyable, and compared
 * and hashed as one of
 * - a function pointer, by value;
 * - an empty type (a named functor, a captureless lambda), by type alone:
 *   the `Ledger` keeps one cache per query type, so all such keys are equal;
 * - any other type, by its `operator==` and `std::hash`.
 */
template <class C>
concept KeyCallable =
    std::copyable<C> && (std::is_pointer_v<C> || std::is_empty_v<C> ||
                         (std::equality_comparable<C> && requires(const C &c) {
                             {
                                 std::hash<C>{}(c)
                             } -> std::convertible_to<std::size_t>;
                         }));

/**
 * Compares two key callables by the rules of `KeyCallable`, so every
 * query compares its callables the same way
 */
template <KeyCallable C> bool callableEqual(const C &a, const C &b) {
    if constexpr (std::is_empty_v<C>)
        return true;
    else
        return a == b;
}

/**
 * Mixes a key callable into the hash `seed` by the rules of
 * `KeyCallable`; an empty callable leaves it as it is, since its type is
 * part of the query type
 */
template <KeyCallable C>
std::size_t combineCallableHash(std::size_t seed, const C &c) {
    if constexpr (std::is_empty_v<C>)
        return seed;
    else
        return combineHash(seed, std::hash<C>{}(c));
}

} // namespace squele

#endif // H_INCLUDED_SQUELE_BITS_CALLABLE
