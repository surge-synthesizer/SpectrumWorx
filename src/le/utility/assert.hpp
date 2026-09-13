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

#endif // assert_hpp
