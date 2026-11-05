#pragma once
#ifndef H_INCLUDED_SQUELE_BITS_SLOTCHUNKS
#define H_INCLUDED_SQUELE_BITS_SLOTCHUNKS

#include <bit>
#include <cstddef>
#include <memory>
#include <vector>

namespace squele {

/**
 * A growable array of cells in fixed chunks: cells never move, so references
 * to them stay valid while the array grows.
 * Unlike `std::deque`, whose block size the standard library fixes (one
 * element per block for large items), each chunk holds a power-of-two number
 * of cells within `TChunkBytesT` bytes (at least one cell), so indexing is a
 * shift and a mask, and large cells still share allocations.
 * @note Never shrinks. Cells are default-initialized: a new chunk costs one
 * allocation and no per-cell work, so `TCellT` is meant to be raw storage
 * such as `SlotCell`.
 */
template <class TCellT, std::size_t TChunkBytesT = 65536> class SlotChunks {
  public:
    using CellT = TCellT;
    static constexpr std::size_t chunkBytes = TChunkBytesT;

    /**
     * Cells per chunk, a power of two
     */
    static constexpr std::size_t chunkCells =
        sizeof(CellT) >= chunkBytes
            ? 1
            : std::bit_floor(chunkBytes / sizeof(CellT));

    std::size_t size() const { return m_size; }

    /**
     * @warning `i >= size()` is UB (not checked).
     */
    CellT &operator[](std::size_t i) {
        return m_chunks[i / chunkCells][i % chunkCells];
    }
    const CellT &operator[](std::size_t i) const {
        return m_chunks[i / chunkCells][i % chunkCells];
    }

    /**
     * Appends one cell, allocating a chunk when the last one is full
     * @note Strong exception guarantee.
     */
    void emplace_back() {
        if (m_size == m_chunks.size() * chunkCells) {
            // If push_back throws, the unique_ptr frees the new chunk
            m_chunks.push_back(
                std::make_unique_for_overwrite<CellT[]>(chunkCells));
        }
        ++m_size;
    }

  private:
    std::vector<std::unique_ptr<CellT[]>> m_chunks;
    std::size_t m_size = 0;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_BITS_SLOTCHUNKS
