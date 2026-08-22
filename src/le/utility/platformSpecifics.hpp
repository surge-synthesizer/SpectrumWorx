////////////////////////////////////////////////////////////////////////////////
///
/// \file platformSpecifics.hpp
/// ---------------------------
///
///   An internal collection of macros that wrap platform specific details/non
/// standard extensions (expands the public parts exposed in abi.hpp).
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef platformSpecifics_hpp__B98C0700_01F9_4B49_AADC_C1AF5BB3EB9B
#define platformSpecifics_hpp__B98C0700_01F9_4B49_AADC_C1AF5BB3EB9B
//------------------------------------------------------------------------------
#include "abi.hpp"

#include "assert.hpp"
//------------------------------------------------------------------------------

/// \note `!defined(__GNUC__)` rather than `!defined(__clang__)`, and abi.hpp
/// says why: the arms are dialects, and clang-cl speaks the first one.
/// __forceinline, __declspec(noinline), __declspec(selectany) and __assume are
/// all accepted by it -- measured, not assumed.
#if defined(_MSC_VER) && !defined(__GNUC__)

#define LE_FORCEINLINE __forceinline
#define LE_NOINLINE __declspec(noinline)

#define LE_WEAK_SYMBOL __declspec(selectany)
#define LE_WEAK_SYMBOL_CONST __declspec(selectany) extern

#define LE_UNREACHABLE_CODE()                                                                      \
    LE_ASSERT_MSG(false, "This code should not be reached.");                                      \
    __assume(false)

#elif defined(__GNUC__)

#ifdef _DEBUG
#define LE_FORCEINLINE inline
#else
#define LE_FORCEINLINE __attribute__((always_inline)) inline
#endif
#define LE_NOINLINE __attribute__((noinline))

#define LE_WEAK_SYMBOL __attribute__((weak))
#define LE_WEAK_SYMBOL_CONST LE_WEAK_SYMBOL extern

/// \note The three-armed cascade this replaces chose between __builtin_assume,
/// __builtin_unreachable and neither, and carried a GCC 4.6 pessimisation
/// workaround. Both live arms defined LE_UNREACHABLE_CODE identically, and the
/// third answered a compiler that abi.hpp's own #error already rules out.
#define LE_UNREACHABLE_CODE()                                                                      \
    LE_ASSERT_MSG(false, "This code should not be reached.");                                      \
    __builtin_unreachable()

/// \note **No per-function optimisation or fast-math attributes.** `-O3`/`-Os` is
/// a size/speed knob the build type already decides, GCC's own documentation
/// restricts the underlying attribute to debugging, and fast-math is worse than
/// inert: GCC acts on `optimize("associative-math")` and vectorises float
/// reductions under it, so honouring it on one platform alone would reorder sums
/// the others do not -- and make a golden difference impossible to attribute to
/// the FFT backend it is meant to be measuring.

#else

#error Unkown compiler

#endif

#define LE_DEFAULT_CASE_UNREACHABLE()                                                              \
    default:                                                                                       \
        LE_UNREACHABLE_CODE();                                                                     \
        break

#endif // platformSpecifics_hpp
