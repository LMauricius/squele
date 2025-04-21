#pragma once
#ifndef H_INCLUDED_SQUELE_CORE_CONCEPTS
#define H_INCLUDED_SQUELE_CORE_CONCEPTS

#include "squele/components/flushable.hpp"
#include "squele/components/syncable.hpp"

#include <concepts>
#include <memory>
#include <utility>

namespace squele {

class Ledger;

/**
 * A view of some data, that can notify listeners of changes.
 * Views are commonly iterables, possibly accessible by indices, but not
 * necessarily.
 */
template <typename T>
concept View =
    std::derived_from<T, Flushable> && std::derived_from<T, Syncable> &&
    requires { typename T::DeltaT; };

template <typename T>
concept Query = requires(T q, Ledger l) {
    typename T::ViewT;
    requires View<typename T::ViewT>;

    { q.create(l) } -> std::convertible_to<std::shared_ptr<typename T::ViewT>>;
} && requires(T l, T r) {
    { std::less(l, r) } -> std::same_as<bool>;
};

template <typename T, class S>
concept ListenerOf = requires(T l, S s) {
    s.addListener(l);
    s.removeListener(l);
};

template <typename T, class ViewT>
concept SelectionOf = View<T> && ListenerOf<T, ViewT>;

} // namespace squele

#endif // H_INCLUDED_SQUELE_CORE_CONCEPTS