#pragma once
#ifndef H_INCLUDED_SQUELE_CORE_FROM
#define H_INCLUDED_SQUELE_CORE_FROM

#include "squele/core/concepts.hpp"

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

namespace squele {

/**
 * A query that stands in for an existing view (usually an `Original`, which
 * cannot be copied and so is never its own query): it gives the held pointer
 * back and never constructs a view.
 * Compares and hashes by identity.
 * @note The `Ledger` does not cache `From`; a cached query containing a `From`
 * keeps its view alive until that query is collected.
 */
template <class TViewT> class From {
  public:
    using View = TViewT;

    /**
     * @param p_view the view; must not be null (asserted)
     */
    explicit From(std::shared_ptr<View> p_view) : m_view(std::move(p_view)) {
        assert(m_view);
    }

    /**
     * @returns the held view
     */
    std::shared_ptr<View> result(Ledger &) const { return m_view; }

    /**
     * @returns the held view, without needing a `Ledger`
     */
    const std::shared_ptr<View> &view() const { return m_view; }

    bool operator==(const From &) const = default;

  private:
    std::shared_ptr<View> m_view;
};

} // namespace squele

template <class TViewT> struct std::hash<squele::From<TViewT>> {
    std::size_t operator()(const squele::From<TViewT> &q) const noexcept {
        return std::hash<std::shared_ptr<TViewT>>{}(q.view());
    }
};

#endif // H_INCLUDED_SQUELE_CORE_FROM
