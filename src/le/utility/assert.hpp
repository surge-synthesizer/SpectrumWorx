////////////////////////////////////////////////////////////////////////////////
///
/// \file assert.hpp
/// ----------------
///
///   LE_ASSERT and friends. Previously a thin skin over Boost.Assert; the
/// handler hook it provided is reproduced here so that assertionHandler.cpp
/// still gets to route failures to the platform debugger and the GUI.
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef assert_hpp__6E5E1CC9_AFB3_4326_A27F_473AF9722938
#define assert_hpp__6E5E1CC9_AFB3_4326_A27F_473AF9722938
//------------------------------------------------------------------------------
#include "abi.hpp"

#if defined(LE_ENABLE_ASSERT_HANDLER) && !defined(NDEBUG)
#include <source_location>
#else
#include <cassert>
#endif // LE_ENABLE_ASSERT_HANDLER

namespace LE::Utility
{

/// \note LE_ASSERT compiles away under NDEBUG, which the handler branch used
/// not to do. The tree assumes it does, in two ways that only a release build
/// reveals: moduleChainImpl.cpp declares an assert's operand inside
/// `#ifndef NDEBUG`, and ModuleNode is only polymorphic under !NDEBUG, so the
/// dynamic_cast in an assert three lines further down does not even compile
/// without it. LE_VERIFY still evaluates its expression -- that is the whole
/// difference between the two.
#if defined(LE_ENABLE_ASSERT_HANDLER) && !defined(NDEBUG)

/// Defined in assertionHandler.cpp. Never returns unless the user chooses to
/// ignore, which only the Windows and JUCE message boxes offer.
///
/// \note The function, file and line used to be LE_CURRENT_FUNCTION, __FILE__
/// and __LINE__ threaded through the macro. std::source_location::current()
/// captures all three at the call site as a defaulted argument.
void assertionFailed(char const *expression, char const *message,
                     std::source_location const &location = std::source_location::current());

#define LE_ASSERT_MSG(expression, message)                                                         \
    ((expression) ? static_cast<void>(0) : ::LE::Utility::assertionFailed(#expression, message))
#define LE_ASSERT(expression) LE_ASSERT_MSG(expression, #expression)
#define LE_VERIFY LE_ASSERT

#elif defined(NDEBUG)

#define LE_ASSERT(expression) static_cast<void>(0)
#define LE_ASSERT_MSG(expression, message) static_cast<void>(0)
#define LE_VERIFY(expression) static_cast<void>(expression)

#else // no handler, checked build

#define LE_ASSERT assert
#define LE_ASSERT_MSG(expression, message) assert((expression) && (message))
#define LE_VERIFY(expression) assert(expression)

#endif // LE_ENABLE_ASSERT_HANDLER

} // namespace LE::Utility

/// \note LE_ASSUME lives here, in the one header that has LE_ASSERT_MSG, because
/// it used to live in two: abi.hpp defined it as a bare unreachable guard and
/// platformSpecifics.hpp #undef'd it and redefined it with an assert. Which one
/// a translation unit got was decided by which of the two headers it had
/// included -- and typeTraits.hpp calls it from inside the set that includes
/// only abi.hpp, so the assert silently was not there.
///
///   [[assume]] is C++23, so the hint is still per-compiler. It expands to a
/// statement, not an expression.
#if defined(_MSC_VER) && !defined(__clang__)
#define LE_AUX_ASSUME_HINT(condition) __assume(condition)
#elif defined(__clang__)
#define LE_AUX_ASSUME_HINT(condition) __builtin_assume(condition)
#else // GCC has neither __builtin_assume nor, before 13, an assume attribute.
#define LE_AUX_ASSUME_HINT(condition)                                                              \
    if (!(condition))                                                                              \
    __builtin_unreachable()
#endif

#define LE_ASSUME(condition)                                                                       \
    do                                                                                             \
    {                                                                                              \
        LE_ASSERT_MSG(condition, "Assumption broken.");                                            \
        LE_AUX_ASSUME_HINT(condition);                                                             \
    } while (false)

#endif // assert_hpp
