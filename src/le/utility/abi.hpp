////////////////////////////////////////////////////////////////////////////////
///
/// \file abi.hpp
/// -------------
///
///   A collection of complier/platform specific ABI defining macros.
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef abi_hpp__10DEE3C0_20E4_4B36_B0A8_B02ADEFD7F38
#define abi_hpp__10DEE3C0_20E4_4B36_B0A8_B02ADEFD7F38
//------------------------------------------------------------------------------

#ifndef __cplusplus
#error LE SDKs are C++ libraries and have to be compiled with a C++ compiler
#endif // __cplusplus

/// \note An `#ifdef __ANDROID__` include of "sys/cdefs.h" stood above, and each
/// arm below carried a compiler floor: MSVC 2010 and "GCC 4.7+ or Clang 3.2+
/// with -std=c++11". This is a C++20 project and leConfigurationAndODRHeader.h
/// states the real floor -- MSVC 19.29 -- so a 2010 one only ever answered a
/// question nobody can ask any more.
///
/// \note `!defined(__GNUC__)`, where this said `!defined(__clang__)`. What the
/// arms hold is a dialect -- MSVC's keywords or GNU's -- and clang-cl writes
/// neither answer to the question that was being asked: it defines `__clang__`
/// and `_MSC_VER` but not `__GNUC__`, so it missed the first arm on the name of
/// its front end and the second on a macro it does not define, and landed on the
/// #error. Asked about `__GNUC__` the cascade sorts every compiler by the
/// dialect it actually speaks, which is what both arms are for. A clang driving
/// a GNU target still answers `__GNUC__` and still takes the second arm.
#if defined(_MSC_VER) && !defined(__GNUC__)

#define LE_RESTRICT __restrict

#elif defined(__GNUC__)

#define LE_RESTRICT __restrict__

#else

#error LE unsupported compiler

#endif

/// \note LE_BIG_ENDIAN and LE_LITTLE_ENDIAN used to be defined here, from
/// __BYTE_ORDER__. The four sites that consulted them now use std::endian,
/// which does not need every one of them to be a preprocessor branch.

//------------------------------------------------------------------------------
#endif // abi_hpp
