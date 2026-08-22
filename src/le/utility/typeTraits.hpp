////////////////////////////////////////////////////////////////////////////////
///
/// \file typeTraits.hpp
/// --------------------
///
///   This used to teach the standard library's type traits about restrict
/// qualified pointers, and to carry TR1 fallbacks for pre-2011 libstdc++.
/// Both are gone: libc++, libstdc++ and the MS STL all answer is_pointer,
/// is_trivially_default_constructible and is_trivially_destructible correctly
/// for `T * __restrict` today, and C++20 makes specialising them ill-formed.
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef typeTraits_hpp__45496FE2_5F16_4115_8225_39355C7AB4D5
#define typeTraits_hpp__45496FE2_5F16_4115_8225_39355C7AB4D5
//------------------------------------------------------------------------------
#include <type_traits>

#include "abi.hpp"
#include "assert.hpp" // LE_ASSUME
//------------------------------------------------------------------------------

/// \note Kept because a `new (pRestrictPointer) T` in the engine relied on it
/// under Clang; drop it once the placement new call sites are confirmed to bind
/// to the standard `operator new( size_t, void * )`.
#ifdef __clang__
template <typename T>
void *__attribute__((nothrow)) operator new(std::size_t /*count*/, T * LE_RESTRICT *const pStorage)
{
    LE_ASSUME(pStorage);
    return reinterpret_cast<void *>(reinterpret_cast<std::size_t>(pStorage));
}
#endif // __clang__

//------------------------------------------------------------------------------
#endif // typeTraits_hpp
