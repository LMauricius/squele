#pragma once
#ifndef H_INCLUDED_SQUELE_CORE_CONCEPTS
#define H_INCLUDED_SQUELE_CORE_CONCEPTS

#include "squele/deltas/slot.hpp"

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

namespace squele {

class Ledger;
template <class TViewT> class From;

/**
 * Has `flush()`, which sends the changes since the last flush to listeners.
 * @note Call `flush()` only when the contents are consistent: listeners read
 * the flushing object while handling its delta.
 */
template <class T>
concept Flushable = requires(T &t) { t.flush(); };

/**
 * Has `sync()`, which brings the contents up to date with the sources, and
 * `dirty()`, which is true when `sync()` would change the contents.
 * @note `sync()` returns at once when the object is clean.
 */
template <class T>
concept Syncable = requires(T &t, const T &ct) {
    t.sync();
    { ct.dirty() } -> std::same_as<bool>;
};

namespace detail {

/**
 * Archetype: the smallest listener of `TDeltaT`, used only to check that a
 * template `addListener` accepts listeners. Declared only (never called).
 */
template <class TDeltaT> struct ListenerArchetype {
    void onDelta(const TDeltaT &);
};

} // namespace detail

/**
 * Accepts listeners of `T::Delta`, without dictating how it stores them
 * (`Broadcaster` is one way).
 * - `T::Delta`: sent to listeners; valid only during the call.
 * - `addListener(p_l)`/`removeListener(p_l)`: for any listener `L` with
 *   `onDelta(const T::Delta &)` (`onDirty()` is optional).
 * - `hasListeners()`: whether anyone listens.
 * - `addInterests(m)`/`removeInterests(m)`: count interests in the
 *   `T::Delta::Interests` flags `m`; only members someone is interested in
 *   are filled.
 */
template <class T>
concept Listenable = requires(T &t, const T &ct,
                              detail::ListenerArchetype<typename T::Delta> *p_l,
                              typename T::Delta::Interests m) {
    t.addListener(p_l);
    t.removeListener(p_l);
    { ct.hasListeners() } -> std::same_as<bool>;
    t.addInterests(m);
    t.removeInterests(m);
};

/**
 * A view: `Flushable` and `Listenable`, but not necessarily `Syncable`.
 * - `T::Query`: the query type that creates this view.
 */
template <class T>
concept Viewable =
    Flushable<T> && Listenable<T> && requires { typename T::Query; };

/**
 * A query: a cache key of the `Ledger` that creates a `T::View` on a miss.
 * @note Queries of different types are never compared: the `Ledger` keeps
 * one cache per query type.
 */
template <class T>
concept Queryable = std::copyable<T> && std::equality_comparable<T> &&
                    requires(const T &q, Ledger &l) {
                        typename T::View;
                        requires Viewable<typename T::View>;
                        { std::hash<T>{}(q) } -> std::same_as<std::size_t>;
                        {
                            q.result(l)
                        } -> std::same_as<std::shared_ptr<typename T::View>>;
                    };

/**
 * `L` receives the deltas of `S`. It inherits nothing; `S` stores it by
 * pointer.
 */
template <class L, class S>
concept Listening = requires(L &l, S &s, const typename S::Delta &d) {
    l.onDelta(d);
    s.addListener(&l);
    s.removeListener(&l);
};

/**
 * `V` is a view derived from the source `S`: it listens to `S` (including its
 * `onDirty()` calls, which make `V` dirty) and syncs to it.
 */
template <class V, class S>
concept Dependent = Viewable<V> && Syncable<V> && Listening<V, S> &&
                    requires(V &v) { v.onDirty(); };

/**
 * A view without sources that is modified by hand. It is never its own query
 * (it is not copyable); `From<T>` stands in for it.
 */
template <class T>
concept Original =
    Viewable<T> && !Syncable<T> && std::same_as<typename T::Query, From<T>>;

/**
 * A view whose items can be listed in some order.
 * - `T::Item`: the item type.
 * - `begin()`/`end()`: iterate the items as `const Item &`.
 * - `count()`: the number of items.
 */
template <class T>
concept ListViewable =
    Viewable<T> &&
    requires(const T &t, decltype(std::declval<const T &>().begin()) it) {
        typename T::Item;
        { *it } -> std::same_as<const typename T::Item &>;
        ++it;
        { it == t.end() } -> std::convertible_to<bool>;
        { t.count() } -> std::integral;
    };

/**
 * A view whose items are accessed by sparse, reusable indices.
 * - `isTaken(i)`: whether index `i` holds an item.
 * - `takenBits()`: the same flags as a `BitSpan` (one bit per index, length
 *   `slotCount()`), for word-level scans.
 * - `operator[](i)`: the item at a taken index.
 * - `count()`: the number of taken indices.
 * - `slotCount()`: one past the largest index that may be taken.
 * - `begin()`/`end()`: iterate items of taken indices only; `it.index()` is
 *   the current index.
 */
template <class T>
concept SlotViewable =
    ListViewable<T> && std::integral<typename T::Index> &&
    std::same_as<typename T::Delta, SlotDelta<typename T::Index>> &&
    requires(const T &t, typename T::Index i) {
        typename T::Item;
        { t.isTaken(i) } -> std::same_as<bool>;
        { t.takenBits() } -> std::same_as<BitSpan>;
        { t[i] } -> std::same_as<const typename T::Item &>;
        { t.count() } -> std::same_as<typename T::Index>;
        { t.slotCount() } -> std::same_as<typename T::Index>;
        { *t.begin() } -> std::same_as<const typename T::Item &>;
        { t.begin().index() } -> std::same_as<typename T::Index>;
        { t.begin() == t.end() } -> std::same_as<bool>;
    };

/**
 * A slot view whose items are sorted by `T::Compare`.
 * - Dense: `slotCount() == count()`, and every index below it is
 *   taken.
 * - Sorted: the item at `i` is never less than the item at `i - 1` under
 *   `compare()`; the order of equal items is unspecified.
 * - `compare()`: the strict weak order the items are sorted by.
 * @note Density and order are contracts, not checked.
 */
template <class T>
concept OrderedViewable = SlotViewable<T> && requires(const T &t) {
    typename T::Compare;
    { t.compare() } -> std::same_as<const typename T::Compare &>;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_CORE_CONCEPTS
