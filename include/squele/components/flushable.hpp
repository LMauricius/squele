#pragma once
#ifndef H_INCLUDED_SQUELE_COMPONENTS_FLUSHABLE
#define H_INCLUDED_SQUELE_COMPONENTS_FLUSHABLE

namespace squele {

/**
 * A view of some data, that can notify listeners of changes.
 * Views are commonly iterables, possibly accessible by indices, but not
 * necessarily.
 */
class Flushable {
  public:
    virtual ~Flushable() = default;

    /**
     * Ensures that the listeners (if listenable) get notified of
     * changes
     */
    virtual void flush() = 0;
};
} // namespace squele

#endif // H_INCLUDED_SQUELE_COMPONENTS_FLUSHABLE