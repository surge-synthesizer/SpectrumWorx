////////////////////////////////////////////////////////////////////////////////
///
/// assertionHandler.cpp
/// --------------------
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
/// \note Was ( !NDEBUG || LE_PUBLIC_BUILD ) && LE_ENABLE_ASSERT_HANDLER, but
/// assert.hpp only declares assertionFailed() when the asserts themselves are
/// live, which NDEBUG now decides. A public build that wants its asserts back
/// asks for them with LE_CHECKED_BUILD=1.
#if !defined(NDEBUG) && defined(LE_ENABLE_ASSERT_HANDLER)
//------------------------------------------------------------------------------
#include "platformSpecifics.hpp"
#include "tchar.hpp"

#include "assert.hpp"

/// \note Six chains through this file had an Android or an iOS arm: an
/// __android_log_assert() reporting path, a TargetConditionals.h/MacTypes.h
/// include cascade, and `!(TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR)` guards
/// on the macOS ones. Neither is a target of this plugin, so each chain now
/// spells out only what macOS, Windows and Linux were already selecting -- and
/// three of them had nothing left to choose between: the CoreServices include
/// below, the backtrace one, and printDebugMessage()'s whole body.
#if defined(__APPLE__)
#include "CoreServices/CoreServices.h"
#include "signal.h"
#include "syslog.h"
#elif defined(_WIN32)
#include "windowsLite.hpp"
#endif

/// \note breakIntoDebugger()'s fallback arm below is `raise( SIGINT )`, and
/// `signal.h` was included only under `__APPLE__` — so every other POSIX target
/// took an arm whose declarations it had not seen. Debug-build-only, since a
/// release build has no assert handler to break from.
/// \note sst-plugininfra's, not `execinfo.h` directly: it has a Windows arm
/// over DbgHelp, which `backtrace()` does not exist on at all, and Windows is
/// the platform whose failures arrive here as a log rather than a debugger
/// session.
#define LE_ASSERT_HAS_BACKTRACE
#include <sst/plugininfra/misc_platform.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
/// \note std::terminate lives here. Both uses of it are inside _WIN32 or
/// LE_PUBLIC_BUILD arms, so no other configuration compiles the line that needs
/// the declaration -- and libc++ hands it over transitively anyway, which is why
/// only MSVC ever asked.
#include <exception>
#include <source_location>

namespace LE
{

#pragma message("LEB: assertion handling enabled.")

LE_WEAK_SYMBOL_CONST char const assertionFailureMessageTitle[] = "LE SDK assertion failure";

/// \note **No JUCE dialog from a failed assertion**, which is also what keeps
/// `sw-dsp` from naming JUCE at all: a plugin cannot know whether it has a
/// message thread to put one on. \see doc/tech/threading_model.md §6.

} // namespace LE

#pragma warning(push)
#pragma warning(disable : 4702) // Unreachable code.

namespace
{
[[maybe_unused]] static void printAssertionFailureTitle()
{
#if defined(__APPLE__)
    /// \note Was DebugStr(), deprecated since 10.8 and now gone. stderr is
    /// what a DAW log and a test runner both capture.
    std::fputs(LE::assertionFailureMessageTitle, stderr);
    std::fputs("\n", stderr);
#elif defined(_WIN32)
    ::OutputDebugStringA(LE::assertionFailureMessageTitle);
    std::fputs(LE::assertionFailureMessageTitle, stderr);
#else
    std::fputs(LE::assertionFailureMessageTitle, stderr);
#endif // platform/compiler
}

static void printDebugMessage(char const *const message)
{
    /// \note Was LE::Utility::Tracer::error() with its tag swapped to the title
    /// for the duration. The tracer is gone; this is what it did with it.
    char formatted[4096];
    std::snprintf(formatted, sizeof(formatted), "%s, ERROR: %s", LE::assertionFailureMessageTitle,
                  message);
#if defined(__APPLE__)
    ::syslog(LOG_ERR, "%s", formatted);
#elif defined(_WIN32)
    ::OutputDebugStringA(formatted);
    ::OutputDebugStringA("\n");
#endif // platform
    std::fputs(formatted, stderr);
    std::fputs("\n", stderr);
}

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 4505) // Unreferenced local function has been removed.
static void printDebugMessage(wchar_t const *const message)
{
    printAssertionFailureTitle();

    ::OutputDebugStringW(message);
    ::fputws(message, stderr);
}
#pragma warning(pop) // was a second push
#endif               // _WIN32

#ifdef _WIN32
#define LE_ASSERT_HAS_MSGBOX

static bool assertMessageBox(char const *const message)
{
    int const userChoice(
        ::MessageBoxA(nullptr, message, LE::assertionFailureMessageTitle, MB_ABORTRETRYIGNORE));
    switch (userChoice)
    {
    case IDRETRY:
        return false;
    case IDIGNORE:
        return true;
    default:
        std::terminate();
    }
}

#endif // LE_ASSERT_HAS_MSGBOX

////////////////////////////////////////////////////////////////////////////////
//
// printBacktrace()
// ----------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note An assertion that says what was wrong but not how it was reached costs
/// a round trip -- more, when the person who can reproduce it and the person
/// reading the code are not the same. The stack is free to print and is the
/// first question anyone asks.
///
/// \note To stderr, alongside the message, because that is what a DAW's log
/// window and a test runner both capture. Symbol names come from the dynamic
/// symbol table, so a bundle built with -fvisibility=hidden shows fewer of them
/// than a debugger would; the addresses are still there to run through atos or
/// addr2line.
///
////////////////////////////////////////////////////////////////////////////////

static void printBacktrace()
{
#ifdef LE_ASSERT_HAS_BACKTRACE
    std::fputs("\n", stderr);
    std::fputs(sst::plugininfra::misc_platform::stackTraceToString().c_str(), stderr);
    std::fputs("\n", stderr);
#endif // LE_ASSERT_HAS_BACKTRACE
}

void breakIntoDebugger()
{
#if defined(_MSC_VER)
    _CrtDbgBreak();
#else
    // SIGINT rather than a trap instruction: an attached debugger stops here,
    // and a run without one is still resumable.
    std::raise(SIGINT);
#endif
}

static LE_NOINLINE void assertionFailedMsgAux([[maybe_unused]] char const *const expression,
                                              char const *const message,
                                              [[maybe_unused]] char const *const function,
                                              [[maybe_unused]] char const *const file,
                                              long const line)
{

    bool ignore;

    char fullMessage[4096] = {0};

#ifdef LE_PUBLIC_BUILD // not to leak too much information to beta testers...
    std::strcpy(fullMessage, message);
    std::strcat(fullMessage, " (");
#else
    if (message != expression)
    {
        std::strcpy(fullMessage, message);
        std::strcat(fullMessage, "\n");
    }
    std::strcat(fullMessage, "\n expression:\n  ");
    std::strcat(fullMessage, expression);
    std::strcat(fullMessage, "\n in function:\n  ");
    std::strcat(fullMessage, function);
    std::strcat(fullMessage, "\n in file:\n  ");
    std::strcat(fullMessage, file);
    std::strcat(fullMessage, "\n at line\n  ");
#endif // LE_PUBLIC_BUILD
    // Every strcat above is unbounded, so by here the buffer may be full; the
    // line number is the one write that can be told where the end is.
    auto const used(std::strlen(fullMessage));
    std::snprintf(&fullMessage[used], sizeof(fullMessage) - used, "%ld", line);
#ifdef LE_PUBLIC_BUILD
    std::strcat(fullMessage, ")");
#endif // LE_PUBLIC_BUILD

    printDebugMessage(fullMessage);
    printBacktrace();

#ifdef LE_ASSERT_HAS_MSGBOX
    ignore = assertMessageBox(fullMessage);
#else
    ignore = false;
#endif // LE_ASSERT_HAS_MSGBOX

    if (!ignore)
    {
        breakIntoDebugger();

        // Do not forcibly terminate in internal debug builds...
#ifdef LE_PUBLIC_BUILD
        std::terminate();
#endif // LE_PUBLIC_BUILD
    }
}

} // anonymous namespace

/// \note This used to define boost::assertion_failed and
/// boost::assertion_failed_msg, the Boost.Assert hook. Stage 2 pointed
/// LE_ASSERT at LE::Utility::assertionFailed and left the handler behind, so
/// nothing had defined it since.
namespace LE::Utility
{
void assertionFailed(char const *const expression, char const *const message,
                     std::source_location const &location)
{
#ifdef LE_PUBLIC_BUILD // not to leak too much information to beta testers...
    assertionFailedMsgAux(nullptr, message, nullptr, nullptr, location.line());
#else
    assertionFailedMsgAux(expression, message, location.function_name(), location.file_name(),
                          location.line());
#endif // LE_PUBLIC_BUILD
}
} // namespace LE::Utility

#pragma warning(pop)

#endif // ( !NDEBUG || LE_PUBLIC_BUILD ) && LE_ENABLE_ASSERT_HANDLER
