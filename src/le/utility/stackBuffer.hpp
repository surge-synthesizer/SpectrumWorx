////////////////////////////////////////////////////////////////////////////////
///
/// \file stackBuffer.hpp
/// ---------------------
///
///   Short lived, automatically sized scratch buffers, allocated on the stack.
/// Replaces boost/simd/preprocessor/stack_buffer.hpp. Its third macro, the
/// "scoped" aligned buffer, destroyed its elements at scope exit; every element
/// type here is trivially destructible, so it has no counterpart.
///
///   Note that alloca() allocates in the *enclosing function's* frame, not the
/// enclosing block's: a buffer taken inside a loop is not released until the
/// function returns. That was true of the NT2 macros too, and every call site
/// takes its buffer at most once per call.
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef stackBuffer_hpp__5D0C6E17_4B9A_4F02_9C3E_84B5A1D7F620
#define stackBuffer_hpp__5D0C6E17_4B9A_4F02_9C3E_84B5A1D7F620
//------------------------------------------------------------------------------
#include "intrinsics.hpp"
#include "span.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

#if defined(_MSC_VER)
#include <malloc.h>
#define LE_AUX_ALLOCA(bytes) ::_alloca(bytes)
#else
#define LE_AUX_ALLOCA(bytes) __builtin_alloca(bytes)
#endif // _MSC_VER

namespace LE::Utility::Detail
{

/// \note Clang's alloca() returns unaligned pointers, which is why even the
/// pointer-array call sites in processor.cpp take the aligned macro. So we
/// over-allocate and align by hand rather than trust the allocation.
inline void *alignToVector(void *const pStorage) noexcept
{
    auto constexpr alignment{static_cast<std::uintptr_t>(Constants::vectorAlignment)};
    auto const address{reinterpret_cast<std::uintptr_t>(pStorage)};
    return reinterpret_cast<void *>((address + alignment - 1) & ~(alignment - 1));
}

} // namespace LE::Utility::Detail

/// \brief count elements of T on the stack, unaligned. \p name is a T *.
#define LE_STACK_BUFFER(name, T, count)                                                            \
    T *const name { static_cast<T *>(LE_AUX_ALLOCA(sizeof(T) * (count))) }

/// \brief count elements of T on the stack, aligned to the SIMD vector width.
/// \p name is an LE::Utility::Span<T>.
#define LE_ALIGNED_STACK_BUFFER(name, T, count)                                                    \
    static_assert(std::is_trivially_destructible_v<T>, "Stack buffers hold trivial types only");   \
    std::size_t const name##Count_{static_cast<std::size_t>(count)};                               \
    ::LE::Utility::Span<T> const name                                                              \
    {                                                                                              \
        static_cast<T *>(::LE::Utility::Detail::alignToVector(LE_AUX_ALLOCA(                       \
            sizeof(T) * name##Count_ + ::LE::Utility::Constants::vectorAlignment - 1))),           \
            name##Count_                                                                           \
    }

//------------------------------------------------------------------------------
#endif // stackBuffer_hpp
