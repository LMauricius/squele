#pragma once
#ifndef H_INCLUDED_SQUELE_COMPONENTS_SYNCABLE
#define H_INCLUDED_SQUELE_COMPONENTS_SYNCABLE

namespace squele {

/**
 * An object with some sources, that occasionally needs to sync to them.
 */
class Syncable {
  public:
    virtual ~Syncable() = default;

    /**
     * Ensures the contents of this object (if any) are up to date with any
     * dependency data
     */
    virtual void sync() = 0;
};
}

#endif // H_INCLUDED_SQUELE_COMPONENTS_SYNCABLE