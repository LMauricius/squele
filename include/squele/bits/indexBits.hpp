#pragma once
#ifndef H_INCLUDED_SQUELE_BITS_INDEXBITS
#define H_INCLUDED_SQUELE_BITS_INDEXBITS

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <vector>

namespace squele {

/**
 * One word of bit storage; bit `i` lives in word `i / bitWordBits` at bit
 * `i % bitWordBits`
 */
using BitWord = std::uint64_t;
inline constexpr std::size_t bitWordBits = 64;

/**
 * Words are aligned to one cache line, and their count is padded to a whole
 * cache line, so bulk loops over words never split a line and need no scalar
 * tail
 */
inline constexpr std::size_t bitWordAlign = 64;
inline constexpr std::size_t bitWordPad = bitWordAlign / sizeof(BitWord);

/**
 * `std::allocator` aligns only to `alignof(std::max_align_t)` (usually 16), so
 * `BitVector` needs its own allocator for cache line alignment
 */
template <class T, std::size_t TAlign> struct AlignedAllocator {
    using value_type = T;

    // allocator_traits cannot rebind a non-type template parameter itself
    template <class U> struct rebind {
        using other = AlignedAllocator<U, TAlign>;
    };

    AlignedAllocator() = default;
    template <class U>
    AlignedAllocator(const AlignedAllocator<U, TAlign> &) noexcept {}

    T *allocate(std::size_t n) {
        return static_cast<T *>(
            ::operator new(n * sizeof(T), std::align_val_t{TAlign}));
    }

    void deallocate(T *p, std::size_t) noexcept {
        ::operator delete(p, std::align_val_t{TAlign});
    }

    bool operator==(const AlignedAllocator &) const = default;
};

/**
 * A read-only view of the bits of a `BitVector`, like `std::span` for bits.
 * Valid until the viewed `BitVector` changes size or is destroyed.
 * @note Every bit at or past `size()` up to the end of `words()` is zero.
 */
class BitSpan {
  public:
    BitSpan() = default;

    /**
     * @param p_words `bitWordAlign`-aligned words; their count is
     * `size` rounded up to a multiple of `bitWordPad` words, and every bit
     * past `size` is zero
     */
    BitSpan(const BitWord *p_words, std::size_t size)
        : m_words(p_words), m_size(size) {}

    /**
     * @returns the number of bits
     */
    std::size_t size() const { return m_size; }

    /**
     * @returns whether bit `i` is set
     * @warning `i >= size()` is UB (not checked).
     */
    bool test(std::size_t i) const {
        return (m_words[i / bitWordBits] >> (i % bitWordBits)) & 1;
    }

    /**
     * @returns all words, including the zero padding
     */
    std::span<const BitWord> words() const {
        return {std::assume_aligned<bitWordAlign>(m_words),
                paddedWords(m_size)};
    }

    /**
     * @returns the first set bit at or after `i`, or `size()` if none. Skips
     * a whole word of clear bits per step.
     */
    std::size_t nextSet(std::size_t i) const {
        if (i >= m_size)
            return m_size;
        // Fast path for dense bits: a set bit in the rest of i's word.
        // Padding bits are zero, so any found bit is below size()
        std::size_t w = i / bitWordBits;
        if (const BitWord word = m_words[w] >> (i % bitWordBits))
            return i + static_cast<std::size_t>(std::countr_zero(word));
        const std::size_t count = (m_size + bitWordBits - 1) / bitWordBits;
        while (++w < count)
            if (const BitWord word = m_words[w])
                return w * bitWordBits +
                       static_cast<std::size_t>(std::countr_zero(word));
        return m_size;
    }

    /**
     * @returns the number of words that hold `size` bits, padded to a
     * multiple of `bitWordPad`
     */
    static constexpr std::size_t paddedWords(std::size_t size) {
        constexpr std::size_t padBits = bitWordPad * bitWordBits;
        return (size + padBits - 1) / padBits * bitWordPad;
    }

  private:
    const BitWord *m_words = nullptr;
    std::size_t m_size = 0;
};

/**
 * Owning, word-addressable bit storage. Unlike `std::vector<bool>`, it exposes
 * its words, so callers can process 64 bits per step.
 * @note Words are `bitWordAlign`-aligned and padded (see `BitSpan`); every bit
 * past `size()` stays zero.
 */
class BitVector {
  public:
    std::size_t size() const { return m_size; }

    /**
     * @warning For all bit accessors, `i >= size()` is UB (not checked).
     */
    bool test(std::size_t i) const { return BitSpan(*this).test(i); }
    void set(std::size_t i) {
        m_words[i / bitWordBits] |= BitWord{1} << (i % bitWordBits);
    }
    void reset(std::size_t i) {
        m_words[i / bitWordBits] &= ~(BitWord{1} << (i % bitWordBits));
    }

    /**
     * Sets the size to `size` bits; new bits are clear
     * @note Strong exception guarantee.
     */
    void resize(std::size_t size) {
        m_words.resize(BitSpan::paddedWords(size));
        // Clear the dropped bits, so the padding stays zero
        if (size < m_size) {
            const std::size_t w = size / bitWordBits;
            if (w < m_words.size()) {
                m_words[w] &= (BitWord{1} << (size % bitWordBits)) - 1;
                std::fill(m_words.begin() + static_cast<std::ptrdiff_t>(w + 1),
                          m_words.end(), BitWord{0});
            }
        }
        m_size = size;
    }

    /**
     * Appends one bit
     * @note Strong exception guarantee.
     */
    void push_back(bool bit) {
        resize(m_size + 1);
        if (bit)
            set(m_size - 1);
    }

    /**
     * @returns all words, including the zero padding
     * @warning Setting a bit at or past `size()` through them is UB.
     */
    std::span<BitWord> words() {
        return {std::assume_aligned<bitWordAlign>(m_words.data()),
                m_words.size()};
    }

    operator BitSpan() const { return {m_words.data(), m_size}; }

  private:
    std::vector<BitWord, AlignedAllocator<BitWord, bitWordAlign>> m_words;
    std::size_t m_size = 0;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_BITS_INDEXBITS
