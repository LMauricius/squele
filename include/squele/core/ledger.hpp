#pragma once
#ifndef H_INCLUDED_SQUELE_CORE_LEDGER
#define H_INCLUDED_SQUELE_CORE_LEDGER

#include "squele/core/concepts.hpp"

#include <any>
#include <map>
#include <memory>
#include <typeindex>

namespace squele {

template <Query TQueryT>
using ViewCache = std::map<TQueryT, typename TQueryT::ViewT>;

/**
 * A record keeper of all used queries and their views.
 * Caches views for easier updating of their contents.
 */
class Ledger {
  public:
    /**
     * Gets a view as a result of the query, with up-to-date content
     * @note the view is cached, so only a first call with this query constructs
     * the view. Others will return the same view ptr
     * @param q the query
     * @returns a shared ptr to the desired view
     */
    template <Query QueryT>
    std::shared_ptr<typename QueryT::ViewT> get(QueryT &&q) {
        std::shared_ptr<typename QueryT::ViewT> p =
            get_unsynced(std::forward(q));
        p->sync();
        return p;
    }

    /**
     * Gets a view as a result of the query, but doesn't update its content
     * @note the view is cached, so only a first call with this query constructs
     * the view. Others will return the same view ptr
     * @param q the query
     * @returns a shared ptr to the desired view
     */
    template <Query QueryT>
    std::shared_ptr<typename QueryT::ViewT> get_unsynced(QueryT &&q) {
        // cache typing
        using CacheT = ViewCache<QueryT>;
        constexpr auto ind = std::type_index(typeid(QueryT));

        // get the cache for this query type
        auto it = m_viewCaches4queryT.find(ind);
        if (it == m_viewCaches4queryT.end()) {
            it = m_viewCaches4queryT.emplace(ind).first;
        }
        CacheT &cache = *std::any_cast<CacheT *>(&(it->second));

        // get the view
        auto it2 = cache.find(q);
        if (it2 == cache.end()) {
            auto p_view = q.create(*this);
            cache.emplace(std::forward(q), p_view);
            return p_view;
        } else {
            return it2->second;
        }
    }

  private:
    std::map<std::type_index, std::any> m_viewCaches4queryT;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_CORE_LEDGER