#pragma once
#ifndef H_INCLUDED_SQUELE_CORE_CONCEPTS
#define H_INCLUDED_SQUELE_CORE_CONCEPTS

#include "squele/deltas/slot.hpp"

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>

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

/**
 * A view: `Flushable`, but not necessarily `Syncable`.
 * - `T::Delta`: sent to listeners on `flush()`; valid only during the call.
 * - `T::Query`: the query type that creates this view.
 */
template <class T>
concept Viewable = Flushable<T> && requires {
    typename T::Delta;
    typename T::Query;
};

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
 * A view whose items are accessed by sparse, reusable indices.
 * - `isTaken(i)`: whether index `i` holds an item.
 * - `operator[](i)`: the item at a taken index.
 * - `activeCount()`: the number of taken indices.
 * - `slotCount()`: one past the largest index that may be taken.
 * - `begin()`/`end()`: iterate items of taken indices only; `it.index()` is
 *   the current index.
 */
template <class T>
concept SlotViewable =
    Viewable<T> && std::integral<typename T::Index> &&
    std::same_as<typename T::Delta, SlotDelta<typename T::Index>> &&
    requires(const T &t, typename T::Index i) {
        typename T::Item;
        { t.isTaken(i) } -> std::same_as<bool>;
        { t[i] } -> std::same_as<const typename T::Item &>;
        { t.activeCount() } -> std::same_as<typename T::Index>;
        { t.slotCount() } -> std::same_as<typename T::Index>;
        { *t.begin() } -> std::same_as<const typename T::Item &>;
        { t.begin().index() } -> std::same_as<typename T::Index>;
        { t.begin() == t.end() } -> std::same_as<bool>;
    };

} // namespace squele

#endif // H_INCLUDED_SQUELE_CORE_CONCEPTS
