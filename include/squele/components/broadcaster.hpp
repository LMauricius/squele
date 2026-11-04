#pragma once
#ifndef H_INCLUDED_SQUELE_COMPONENTS_BROADCASTER
#define H_INCLUDED_SQUELE_COMPONENTS_BROADCASTER

#include <algorithm>
#include <cassert>
#include <vector>

namespace squele {

/**
 * Mixin that stores listeners and sends them deltas, without virtual calls.
 * A listener is anything with `onDelta(const DeltaT &)` and `onDirty()` (see
 * `Listening`); it inherits nothing.
 * @warning Adding or removing listeners from inside a listener call is UB: it
 * invalidates the broadcast loop.
 * @warning Listeners are stored as raw pointers: remove a listener before it
 * or this object dies.
 * @warning Listener calls must not throw; an exception leaves the remaining
 * listeners without the change.
 */
template <class TDeltaT> class Broadcaster {
  public:
    using DeltaT = TDeltaT;

    /**
     * Adds a listener. A listener added n times is notified n times.
     * @param p_l the listener; must be removed exactly as many times as added,
     * through a pointer of the same type
     */
    template <class L>
        requires requires(L &l, const DeltaT &d) {
            l.onDelta(d);
            l.onDirty();
        }
    void addListener(L *p_l) {
        // Captureless lambdas decay to function pointers that cast the object
        // back to L: no vtable in L, no allocation per entry
        m_listeners.push_back(
            {p_l,
             [](void *p_obj, const DeltaT &d) {
                 static_cast<L *>(p_obj)->onDelta(d);
             },
             [](void *p_obj) { static_cast<L *>(p_obj)->onDirty(); }});
    }

    /**
     * Removes *one* entry of the listener
     * @warning Removing a listener that was not added is UB (asserted)
     */
    template <class L> void removeListener(L *p_l) {
        void *const p_obj = p_l;
        const auto it =
            std::find_if(m_listeners.begin(), m_listeners.end(),
                         [&](const Entry &e) { return e.p_obj == p_obj; });
        assert(it != m_listeners.end());
        if (it != m_listeners.end())
            m_listeners.erase(it);
    }

  protected:
    Broadcaster() = default;
    // Copying would copy the listeners' back-references
    Broadcaster(const Broadcaster &) = delete;
    Broadcaster &operator=(const Broadcaster &) = delete;
    ~Broadcaster() = default;

    /**
     * Sends `d` to every listener
     */
    void broadcast(const DeltaT &d) const {
        for (const Entry &e : m_listeners)
            e.p_onDelta(e.p_obj, d);
    }

    /**
     * Tells every listener that this object has pending changes
     */
    void broadcastDirty() const {
        for (const Entry &e : m_listeners)
            e.p_onDirty(e.p_obj);
    }

  private:
    /**
     * A type-erased listener: the object and two thunks that cast it back
     */
    struct Entry {
        void *p_obj;
        void (*p_onDelta)(void *, const DeltaT &);
        void (*p_onDirty)(void *);
    };

    std::vector<Entry> m_listeners;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_COMPONENTS_BROADCASTER
