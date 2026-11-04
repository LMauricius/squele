#pragma once
#ifndef H_INCLUDED_SQUELE_BITS_PULL
#define H_INCLUDED_SQUELE_BITS_PULL

#include "squele/core/concepts.hpp"

namespace squele {

/**
 * Brings `view` up to date (if it is `Syncable` and dirty), then flushes it, so
 * every pending change reaches its listeners.
 * A dependent view calls this on its source before reading it: an original may
 * hold an unflushed erase, and reading the erased item is UB.
 * @note O(1) when nothing changed.
 */
template <Flushable TViewT> void pull(TViewT &view) {
    if constexpr (Syncable<TViewT>) {
        if (view.dirty())
            view.sync();
    }
    view.flush();
}

} // namespace squele

#endif // H_INCLUDED_SQUELE_BITS_PULL
