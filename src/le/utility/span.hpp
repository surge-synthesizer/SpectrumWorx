////////////////////////////////////////////////////////////////////////////////
///
/// \file span.hpp
/// --------------
///
///   Replaces boost::iterator_range for the one shape this codebase uses it in:
/// a pair of raw pointers.
///
///   Deliberately not std::span. The engine slides its ranges - advance_begin()
/// and advance_end() are how the moving average, the vocoder envelope and the
/// sub-range walkers are written - and std::span has no mutating equivalent.
/// Its iterators are also not raw pointers, while several hundred call sites
/// here pass begin() straight into a pointer taking primitive. Both are worth
/// changing, but as part of stage 3 with the golden fixtures in place, not as a
/// side effect of deleting a Boost include. operator std::span() is here so
/// that migration can happen one call site at a time.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef span_hpp__7D3E61B0_45C9_4A28_9F73_2B8E05C41D6A
#define span_hpp__7D3E61B0_45C9_4A28_9F73_2B8E05C41D6A
//------------------------------------------------------------------------------
#include "abi.hpp"
#include "assert.hpp"

#include <cstddef>
#include <iterator>
#include <span>
#include <type_traits>

namespace LE::Utility
{

template <class T> class Span
{
  public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    /// \note These were `T *LE_RESTRICT`, and begin() and end() return them --
    /// which is a top level qualifier on a return type, and those are ignored.
    /// GCC 15 says so, 813 times across a build, and it is right: a restrict
    /// promise attaches to a *declaration*, so the only place the qualifier
    /// here ever took effect was the one variable declared with the alias
    /// (processor.cpp's WOLA walk, which now spells it out). It was also a
    /// promise Span cannot keep: begin() and end() of the same span are two
    /// pointers to the same array, and so are any two copies of a span.
    using iterator = T *;
    using const_iterator = T const *;
    using pointer = T *;
    using reference = T &;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    constexpr Span() noexcept : pBegin_(nullptr), pEnd_(nullptr) {}

    constexpr Span(T *const pBegin, T *const pEnd) noexcept : pBegin_(pBegin), pEnd_(pEnd) {}

    constexpr Span(T *const pBegin, size_type const size) noexcept
        : pBegin_(pBegin), pEnd_(pBegin + size)
    {
    }

    template <std::size_t N>
    constexpr Span(T (&array)[N]) noexcept : pBegin_(array), pEnd_(array + N)
    {
    }

    /// Non-const to const.
    template <class U, class = std::enable_if_t<std::is_same_v<U const, T>>>
    constexpr Span(Span<U> const &other) noexcept : pBegin_(other.begin()), pEnd_(other.end())
    {
    }

    constexpr iterator begin() const noexcept { return pBegin_; }
    constexpr iterator end() const noexcept { return pEnd_; }
    constexpr T *data() const noexcept { return pBegin_; }

    constexpr size_type size() const noexcept { return static_cast<size_type>(pEnd_ - pBegin_); }
    constexpr bool empty() const noexcept { return pBegin_ == pEnd_; }
    constexpr explicit operator bool() const noexcept { return !empty(); }

    constexpr reference front() const noexcept
    {
        LE_ASSERT(!empty());
        return *pBegin_;
    }
    constexpr reference back() const noexcept
    {
        LE_ASSERT(!empty());
        return *(pEnd_ - 1);
    }
    constexpr reference operator[](size_type const index) const noexcept
    {
        LE_ASSERT(index < size());
        return pBegin_[index];
    }

    /// \note These return *this so that they can be used inline in a range-for,
    /// as boost::iterator_range's equivalents were.
    constexpr Span &advance_begin(difference_type const offset) noexcept
    {
        pBegin_ += offset;
        return *this;
    }
    constexpr Span &advance_end(difference_type const offset) noexcept
    {
        pEnd_ += offset;
        return *this;
    }

    constexpr void pop_front() noexcept { advance_begin(1); }
    constexpr void pop_back() noexcept { advance_end(-1); }

    constexpr Span subspan(size_type const offset) const noexcept
    {
        LE_ASSERT(offset <= size());
        return Span(pBegin_ + offset, pEnd_);
    }
    constexpr Span subspan(size_type const offset, size_type const count) const noexcept
    {
        LE_ASSERT(offset + count <= size());
        return Span(pBegin_ + offset, count);
    }

    constexpr operator std::span<T>() const noexcept { return std::span<T>(pBegin_, size()); }

  private:
    T *pBegin_;
    T *pEnd_;
}; // class Span

template <class T> constexpr Span<T> makeSpan(T *const pBegin, T *const pEnd) noexcept
{
    return Span<T>(pBegin, pEnd);
}

/// Boost.Range's make_iterator_range_n replacement; the integral constraint is
/// what keeps a pointer pair out of this overload.
template <class T, class Size, class = std::enable_if_t<std::is_integral_v<Size>>>
constexpr Span<T> makeSpan(T *const pBegin, Size const size) noexcept
{
    return Span<T>(pBegin, static_cast<std::size_t>(size));
}

template <class T, std::size_t N> constexpr Span<T> makeSpan(T (&array)[N]) noexcept
{
    return Span<T>(array);
}

template <class Container, class = std::enable_if_t<!std::is_array_v<Container>>>
constexpr auto makeSpan(Container &container) noexcept
    -> Span<std::remove_pointer_t<decltype(std::data(container))>>
{
    using Element = std::remove_pointer_t<decltype(std::data(container))>;
    return Span<Element>(std::data(container), std::size(container));
}

} // namespace LE::Utility

#endif // span_hpp
