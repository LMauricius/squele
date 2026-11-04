#pragma once
#ifndef H_INCLUDED_SQUELE_BITS_HASH
#define H_INCLUDED_SQUELE_BITS_HASH

#include <cstddef>

namespace squele {

/**
 * Mixes `h` into `seed`. Composite queries (e.g. a source query plus a
 * predicate) hash all their parts with it, so equal parts in different
 * positions do not cancel out as with a plain xor.
 */
constexpr std::size_t combineHash(std::size_t seed, std::size_t h) {
    return seed ^ (h + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
}

} // namespace squele

#endif // H_INCLUDED_SQUELE_BITS_HASH
