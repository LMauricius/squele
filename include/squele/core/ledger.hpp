#pragma once
#ifndef H_INCLUDED_SQUELE_CORE_LEDGER
#define H_INCLUDED_SQUELE_CORE_LEDGER

#include "squele/core/concepts.hpp"
#include "squele/core/from.hpp"

#include <concepts>
#include <memory>
#include <typeindex>
#include <unordered_map>

namespace squele {

/**
 * Keeps one view per query: equal queries share a view, which is created only
 * on a cache miss. The ledger owns its views until `collect()` or `clear()`.
 * @note Not thread-safe.
 * @note A consumer that reads a view repeatedly should hold its `shared_ptr`
 * and call `sync()` on it, instead of calling `get()` each time (a hash lookup
 * plus a `shared_ptr` copy).
 */
class Ledger {
  public:
    /**
     * Gets the view of `q`, synced if it is `Syncable`
     * @returns the cached view, created on the first call with an equal query
     */
    template <Queryable Q> std::shared_ptr<typename Q::View> get(const Q &q) {
        std::shared_ptr<typename Q::View> p_view = getUnsynced(q);
        if constexpr (Syncable<typename Q::View>)
            p_view->sync();
        return p_view;
    }

    /**
     * Gets the view of `q` without syncing it
     * @returns the cached view, created on the first call with an equal query
     */
    template <Queryable Q>
    std::shared_ptr<typename Q::View> getUnsynced(const Q &q) {
        // From is not cached: the key would keep its own view alive forever
        if constexpr (std::same_as<Q, From<typename Q::View>>) {
            return q.result(*this);
        } else {
            // The cache object is heap-allocated, so this reference survives
            // result() adding caches for other query types
            auto &map = cacheOf<Q>().map;
            if (const auto it = map.find(q); it != map.end())
                return it->second;
            // result() may call back into the ledger (to get sources), so no
            // iterator is held across it, and the entry is inserted after it
            std::shared_ptr<typename Q::View> p_view = q.result(*this);
            map.emplace(q, p_view);
            return p_view;
        }
    }

    /**
     * Releases every view that nobody but the ledger uses. Repeats until a
     * pass releases nothing, since releasing a view can release its sources
     * (a chain of depth d takes d passes).
     */
    void collect() {
        bool released;
        do {
            released = false;
            for (auto &entry : m_caches)
                released |= entry.second->collect();
        } while (released);
    }

    /**
     * Releases every cached view
     */
    void clear() { m_caches.clear(); }

  private:
    /**
     * Type-erased cache of one query type. Virtual (unlike everything else)
     * because collect() must iterate all caches generically.
     */
    struct CacheBase {
        virtual ~CacheBase() = default;

        /**
         * @returns whether any view was released
         */
        virtual bool collect() = 0;
    };

    template <class Q> struct Cache final : CacheBase {
        std::unordered_map<Q, std::shared_ptr<typename Q::View>> map;

        bool collect() override {
            return std::erase_if(map, [](const auto &e) {
                       return e.second.use_count() == 1;
                   }) != 0;
        }
    };

    /**
     * @returns the cache for query type Q, created on first use
     */
    template <class Q> Cache<Q> &cacheOf() {
        // Allocate before inserting, so no null entry is left if it throws
        const std::type_index key(typeid(Q));
        auto it = m_caches.find(key);
        if (it == m_caches.end())
            it = m_caches.emplace(key, std::make_unique<Cache<Q>>()).first;
        return static_cast<Cache<Q> &>(*it->second);
    }

    std::unordered_map<std::type_index, std::unique_ptr<CacheBase>> m_caches;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_CORE_LEDGER
