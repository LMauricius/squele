#pragma once
#ifndef H_INCLUDED_SQUELE_COMPONENTS_LISTENABLE
#define H_INCLUDED_SQUELE_COMPONENTS_LISTENABLE

#include "squele/components/flushable.hpp"

namespace squele {

/**
 * An object that can notify listeners of changes.
 */
template <typename TListenerT> class Listenable : public virtual Flushable {
  public:

    virtual ~Listenable() = default;

    /**
     * Adds the listener to this view.
     * It's possible to add a listener multiple times for it to get notified
     * multiple times.
     * @param p_l The listener to add
     * @note The listener can and should be removed exactly as many times as it
     * has been added
     */
    void addListener(TListenerT *p_l) { m_listeners.push_back(p_l); }

    /**
     * Removes *one* reference to the listener of this view
     * @param p_l The listener to remove
     * @warning Attempting to remove a listener that wasn't previously added to
     * this view is UB
     */
    void removeListener(TListenerT *p_l) {
        m_listeners.erase(
            std::find(m_listeners.begin(), m_listeners.end(), p_l));
    }

  protected:
    std::vector<TListenerT *> m_listeners;
};
} // namespace squele

#endif // H_INCLUDED_SQUELE_COMPONENTS_LISTENABLE