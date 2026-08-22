////////////////////////////////////////////////////////////////////////////////
///
/// \file buffers.hpp
/// -----------------
///
/// Holds generic buffer classes.
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef buffers_hpp__83BAA722_FD75_4C6D_9CE6_3194647206CE
#define buffers_hpp__83BAA722_FD75_4C6D_9CE6_3194647206CE
//------------------------------------------------------------------------------
#include "le/utility/intrinsics.hpp"
#include "le/utility/platformSpecifics.hpp"
#include "le/utility/tchar.hpp"

#include "assert.hpp"
#include "span.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>

#ifdef _MSC_VER
#include <malloc.h>
#endif // _MSC_VER

namespace LE
{

namespace Math
{
void *align(void *);
} // namespace Math

namespace Utility
{

//...mrmlj...move/rename to some better location/namespace...
inline unsigned int align(unsigned int const storageBytes)
{
    using Utility::Constants::vectorAlignment;
    unsigned int totalBytes((storageBytes + vectorAlignment - 1) & ~(vectorAlignment - 1));
    return totalBytes;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \class AlignedBuffer
///
/// \brief A template class providing an aligned block of (statically allocated)
/// storage.
///
////////////////////////////////////////////////////////////////////////////////

/// \note Used to derive from boost::simd::aligned_object<> for an over-aligned
/// operator new. C++17's aligned new honours the alignas below on its own.
template <typename Element, unsigned int numberOfElements,
          bool defaultAutomaticInitialization = true,
          unsigned int alignmentSize = std::alignment_of<Element>::value>
class AlignedBuffer
{
    static_assert(std::is_trivial_v<Element> && std::is_standard_layout_v<Element>,
                  "Only PODs supported");

  public:
    typedef Element value_type;

    /// \note Plain pointers for the reason given in span.hpp: begin() and end()
    /// return these, and a top level restrict on a return type is ignored --
    /// nothing in the tree declares a variable with either alias.
    typedef Element *iterator;
    typedef Element const *const_iterator;
    typedef Element &reference;
    typedef Element const &const_reference;

    typedef Element Array[numberOfElements];

    static unsigned int const static_size = numberOfElements;

  public:
    explicit AlignedBuffer(bool const initialize = defaultAutomaticInitialization)
    {
        if (initialize)
            clear();
    }

    iterator begin() { return buffer(); }
    const_iterator begin() const { return const_cast<AlignedBuffer &>(*this).begin(); }

    iterator end() { return begin() + size(); }
    const_iterator end() const { return const_cast<AlignedBuffer &>(*this).end(); }

    static unsigned int size() { return static_size; }

    Element &operator[](size_t const index)
    {
        LE_ASSERT_MSG(index < numberOfElements, "Buffer index out of range!");
        return buffer()[index];
    }
    Element const &operator[](size_t const index) const
    {
        return const_cast<AlignedBuffer &>(*this).operator[](index);
    }

    void clear() { std::memset(begin(), 0, size() * sizeof(Element)); }

    Array &c_array() { return reinterpret_cast<Array &>(*buffer()); }
    Array const &c_array() const { return reinterpret_cast<Array const &>(*buffer()); }

  private:
    Element *buffer()
    {
        LE_ASSERT_MSG((reinterpret_cast<std::size_t>(&storage_) % alignmentSize) == 0,
                      "Aligned buffer misaligned.");
        return reinterpret_cast<Element *>(&storage_);
    }

  private:
    // Implementation note:
    //   std::aligned_storage implementation does not give properly aligned
    // storage for alignments larger than the largest builtin type, hence
    // boost::aligned_storage originally and an explicit alignas now.
    //                                        (15.11.2010.) (Domagoj Saric)
    struct alignas(alignmentSize) Storage
    {
        char bytes[sizeof(Element) * numberOfElements];
    };
    Storage storage_;

    static_assert(sizeof(Storage) >= (sizeof(Element) * numberOfElements),
                  "Internal inconsistency");
}; // class AlignedBuffer

////////////////////////////////////////////////////////////////////////////////
///
/// \class AlignedHeapBuffer
///
////////////////////////////////////////////////////////////////////////////////

template <typename T> class AlignedHeapBuffer : public Span<T>
{
  public:
    typedef Span<T> Range;
    typedef typename Range::value_type value_type;

    AlignedHeapBuffer(AlignedHeapBuffer const &) = delete; // makes non-copyable
    AlignedHeapBuffer &operator=(AlignedHeapBuffer const &) = delete;

    AlignedHeapBuffer() { LE_ASSERT(this->empty() && !this->data()); }
    AlignedHeapBuffer(AlignedHeapBuffer &&source) : Range(source)
    {
        static_cast<Range &>(source) = Range();
    }
    /// \note Was boost::simd::aligned_free/aligned_reuse everywhere but Apple
    /// and Win64, which took plain free/realloc. Neither malloc nor realloc
    /// promises more than alignof(std::max_align_t) — 8 on arm64, half of what
    /// the SIMD paths need — so the allocation is explicit on every platform
    /// now, and reallocation is allocate-copy-free.
    ~AlignedHeapBuffer() { alignedFree(this->data()); }

    unsigned int size() const { return static_cast<unsigned int>(Range::size()); }

    bool resize(unsigned int const numberOfElements)
    {
#ifdef _MSC_VER
        //...mrmlj...MSVC10 aligned_realloc seems to reallocate even if the size
        //...mrmlj...does not change...reinvestigate...
        if (numberOfElements == this->size())
            return true;
#endif // _MSC_VER
#ifdef __APPLE__
        /// \note OSX std::realloc does not return a nullptr with 0 sizes so we
        /// explicitly handle this case out of paranoia in case some code uses
        /// Range::data() != nullptr instead of Range::empty() to check for
        /// validity.
        /// https://www.securecoding.cert.org/confluence/display/seccode/MEM30-C.+Do+not+access+freed+memory
        /// http://stackoverflow.com/questions/11455317/realloc-memory-for-a-pointer-which-has-been-freed
        /// http://www.open-std.org/jtc1/sc22/wg14/www/docs/dr_400.htm
        ///                                   (26.08.2014.) (Domagoj Saric)
        if (numberOfElements == 0)
        {
            *this = AlignedHeapBuffer();
            return true;
        }
#endif // __APPLE__
        value_type *const pNewMemory(static_cast<value_type *>(alignedReallocate(
            this->data(), size() * sizeof(value_type), numberOfElements * sizeof(value_type))));
        if (pNewMemory || !numberOfElements)
        {
            LE_ASSERT_MSG((pNewMemory != nullptr) == (numberOfElements != 0),
                          "Unexpected realloc result");
            LE_ASSERT_MSG((reinterpret_cast<std::size_t>(pNewMemory) %
                           Utility::Constants::vectorAlignment) == 0,
                          "Aligned allocation misaligned.");
            static_cast<Range &>(*this) = Range(pNewMemory, pNewMemory + numberOfElements);
            LE_ASSERT(size() == numberOfElements);
            return true;
        }
        else
        {
            return false;
        }
    }

    AlignedHeapBuffer &operator=(AlignedHeapBuffer &&source)
    {
        std::swap(static_cast<Range &>(*this), static_cast<Range &>(source));
        return *this;
    }

  private:
    static void alignedFree(void *const pMemory) noexcept
    {
#ifdef _MSC_VER
        ::_aligned_free(pMemory);
#else
        std::free(pMemory);
#endif // _MSC_VER
    }

    static void *alignedReallocate(void *const pMemory, std::size_t const oldBytes,
                                   std::size_t const newBytes)
    {
#ifdef _MSC_VER
        return ::_aligned_realloc(pMemory, newBytes, Constants::vectorAlignment);
#else
        if (!newBytes)
        {
            alignedFree(pMemory);
            return nullptr;
        }
        // std::aligned_alloc wants a size that is a multiple of the alignment.
        void *const pNewMemory(std::aligned_alloc(Constants::vectorAlignment,
                                                  align(static_cast<unsigned int>(newBytes))));
        if (pNewMemory && pMemory)
            std::memcpy(pNewMemory, pMemory, std::min(oldBytes, newBytes));
        if (pNewMemory)
            alignedFree(pMemory);
        return pNewMemory;
#endif // _MSC_VER
    }

    using Range::advance_begin;
    using Range::advance_end;
    using Range::pop_back;
    using Range::pop_front;
}; // class AlignedHeapBuffer

////////////////////////////////////////////////////////////////////////////////
///
/// \class SharedStorageBuffer
///
/// \brief A buffer that allocates no storage of its own, rather it sizes and
/// positions itself in separately preallocated and externally provided storage.
///
/// Preallocating storage in a single place and then positioning all auxiliary
/// buffers in this storage improves locality of reference. This holds both in
/// the case where all buffers would be separately dynamically allocated (as
/// they may end up in separate regions of memory) as well as when the buffers
/// are statically preallocated for their maximum size (then unused gaps appear
/// between the used regions when buffers are sized below their maximum size,
/// which is our most common use case).
///
////////////////////////////////////////////////////////////////////////////////

typedef Span<char> Storage;

#pragma warning(push)
#pragma warning(disable : 4127) // Conditional expression is constant.

template <typename T> class SharedStorageBuffer : public Span<T>
{
  public:
    SharedStorageBuffer() {}

    using Range = Span<T>;

    LE_NOINLINE void clear()
    {
        static_assert(std::is_trivially_default_constructible_v<T> || std::is_scalar_v<T>,
                      "SharedStorageBuffer supports only primitive types");
        std::memset(Range::begin(), 0, size() * sizeof(T));
    }

    LE_NOINLINE void resize(std::uint32_t const newSize, Storage &storage)
    {
        LE_ASSERT_MSG(newSize % sizeof(T) == 0, "Invalid size.");
        LE_ASSERT_MSG(static_cast<std::size_t>(storage.size()) >= newSize,
                      "Not enough shared storage space.");

        using iterator = T *LE_RESTRICT;

        bool const doAlign(!std::is_pointer_v<T>);

        /// \note static_casting through void * (instead of reinterpret_casting
        /// throguh std::size_t) fails with compiler errors on GCC 4.9 on ranges
        /// of pointers (it complains about __restrict being inapliccable to
        /// void).
        ///                                   (24.12.2014.) (Domagoj Saric)
        iterator const newBeginning(reinterpret_cast<T *>(reinterpret_cast<std::size_t>(
            doAlign ? Math::align(storage.begin()) : storage.begin())));
        auto const alignmentFixup(
            static_cast<std::uint8_t>(reinterpret_cast<std::size_t>(newBeginning) -
                                      reinterpret_cast<std::size_t>(storage.begin())));
        LE_ASSERT_MSG(static_cast<std::size_t>(storage.size()) >= alignmentFixup + newSize,
                      "Not enough shared storage space.");
        storage.advance_begin(alignmentFixup + newSize);
        iterator const newEnd(
            reinterpret_cast<T *>(reinterpret_cast<std::size_t>(storage.begin())));
        LE_ASSERT_MSG(newBeginning <= newEnd, "Failed to generate a valid range.");
        LE_ASSERT_MSG(
            !doAlign ||
                (reinterpret_cast<std::size_t>(newBeginning) % Constants::vectorAlignment == 0),
            "Failed to generate a properly aligned range.");
        static_cast<Range &>(*this) = Range(newBeginning, newEnd);
        LE_ASSERT_MSG(size() == newSize / sizeof(T), "Generated range has an invalid size.");

        if constexpr (!std::is_trivially_default_constructible_v<T>)
        {
            T *LE_RESTRICT pT(this->begin());
            while (pT != this->end())
            {
                LE_ASSUME(pT);
                T *const pNewT(new (pT) T);
                LE_ASSUME(pNewT);
                ++pT;
            }
        }
    }

    std::uint32_t size() const { return static_cast<std::uint32_t>(Range::size()); }

    void alias(SharedStorageBuffer const &other)
    {
        static_cast<Range &>(*this) = static_cast<Range const &>(other);
    }

    /// \note Span<T> and Span<T const> are both a begin/end pointer pair, so
    /// this stays the free reinterpretation it was with boost::iterator_range.
    operator Span<T const> const &() const
    {
        return reinterpret_cast<Span<T const> const &>(*this);
    }

  private:
    static_assert(std::is_trivially_destructible_v<T>,
                  "SharedStorageBuffer supports only primitive types");

    SharedStorageBuffer(SharedStorageBuffer const &);

    using Range::advance_begin;
    using Range::advance_end;
    using Range::pop_back;
    using Range::pop_front;
    using Range::operator=;
}; // class SharedStorageBuffer

#pragma warning(pop)

} // namespace Utility

} // namespace LE

#endif // buffers_hpp
