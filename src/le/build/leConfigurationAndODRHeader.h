////////////////////////////////////////////////////////////////////////////////
//
// LittleEndian root ODR and ABI configuration header.
// ---------------------------------------------------
//
// Copyright (c) 2009 - 2016. Little Endian Ltd.
// SPDX-License-Identifier: GPL-3.0-or-later
//
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef leConfigurationAndODRHeader_h__C79BE937_90BA_4DF1_9D66_5429633644F6
#define leConfigurationAndODRHeader_h__C79BE937_90BA_4DF1_9D66_5429633644F6
#ifdef _MSC_VER
#pragma once
#endif // _MSC_VER
//------------------------------------------------------------------------------

// Implementation note:
//   This header is force-included rather than #included, because what it sets
// has to be seen before any other header. cmake/sw-our-sources.cmake attaches it
// per source file, to files under src/, tests/ and tools/ and to nothing else;
// tests/checkODRHeaderScope.cmake fails the build if that ever stops being true.
//
//   What it is *for* changed on 04.08.2026 and the difference is worth knowing.
// It used to also define LE_IMPL_NAMESPACE_BEGIN/END, which 47 files used to
// open a namespace without declaring where the macro came from -- so a file that
// missed this header did not fail cleanly, it failed as thirty-odd errors that
// named everything except the cause. Those macros are written out now, and every
// one of our 148 translation units compiles with the force-include removed.
//
//   So what is left is configuration rather than syntax, and it is all the more
// silent for it: the NDEBUG policy below decides whether the ~1200 asserts exist
// at all, and the Windows blocks decide which API surface everything after them
// sees. A translation unit that misses this header now builds -- as a different
// build. That is the thing checkODRHeaderScope.cmake is guarding, and it is a
// worse failure than the one it was written for, not a better one.
//
//   It used to be a PUBLIC compile option on sw-dsp, so it reached every
// translation unit of every target that links sw-dsp -- JUCE, fmt and
// clap-wrapper included. Five separate Windows failures came of that, none of
// them in our code; stage 7.5 of doc/tech/old/implementation_sequence.md lists them.
//
//   Nothing here applies to C. Now that the header only reaches our own
// sources, none of which are C, this guard is belt rather than braces -- kept
// because the guard that was *supposed* to do this job, $<COMPILE_LANGUAGE:CXX>,
// is silently ignored for compile options by the Visual Studio generator, and a
// header that is inert in C cannot be mis-applied by a generator.
#ifdef __cplusplus
//------------------------------------------------------------------------------

#ifndef LE_CHECKED_BUILD
// By default we use checked builds in all non-release builds.
#ifdef NDEBUG
#define LE_CHECKED_BUILD 0
#else
#define LE_CHECKED_BUILD 1
#endif // NDEBUG
#endif // LE_CHECKED_BUILD

// Include asserts in all "checked" builds.
#undef NDEBUG
#if !LE_CHECKED_BUILD
#define NDEBUG
#ifndef LE_PUBLIC_BUILD
#define LE_PUBLIC_BUILD
#endif // LE_PUBLIC_BUILD
#endif // LE_CHECKED_BUILD

/// \note LE_IMPL_NAMESPACE_BEGIN/END stood here. Under LE_SW_SDK_BUILD they
/// nested an anonymous namespace inside the named one, so that two SDKs sharing
/// this code could be linked into one binary without their internals clashing;
/// otherwise they were `namespace X {` and `}`. There is no SDK build any more
/// (stage 7 settled that macro), so they were 55 obfuscated
/// namespace openings across 47 files -- and 47 files that used them without
/// declaring where they came from, which is the whole reason this header is
/// force-included. Both are written out as of 04.08.2026.
///                                           (06.10.2014.) (Domagoj Saric)

////////////////////////////////////////////////////////////////////////////////
//
// Operating system specifics.
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Microsoft Windows.
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
#include "sdkddkver.h"

// Implementation note:
//   There were four #undefs above this block -- of WINVER, _WIN32_WINNT,
// _WIN32_IE and NTDDI_VERSION -- which made the #ifndef below unconditionally
// true. So whatever the build, or sdkddkver.h above, had settled on was thrown
// away and every translation unit was pinned to Vista SP2.
//
//   This header is force-included into all of them, JUCE's among them, and JUCE
// 8 draws through Direct2D: pinned to a 2009 API surface its backend cannot see
// ID2D1DeviceContext3/4, IDWriteFactory4, IDCompositionDevice or the
// DWRITE_GLYPH_IMAGE_FORMATS_* enumerators, and fails to compile in its own
// sources -- a hundred errors, none of them in this project's code.
//
//   Without the #undefs sdkddkver.h has already chosen, and it chooses the
// newest the installed SDK supports. What remains is a floor for the case where
// nothing has: Windows 10, which is what JUCE 8 requires.
#ifndef _WIN32_WINNT
#define WINVER _WIN32_WINNT_WIN10
#define _WIN32_WINNT _WIN32_WINNT_WIN10
#define _WIN32_IE 0x0A00
#define NTDDI_VERSION NTDDI_WIN10
#endif // _WIN32_WINNT

#ifndef LEB_INCLUDE_FULL_WINDOWS_HEADERS
// Implementation note:
//   WIN32_LEAN_AND_MEAN was defined here too, and is not any more. It trims
// <windows.h> down, which is a fine thing to ask for in our own translation
// units and not ours to ask for in anybody else's -- and this header is
// force-included into every one of them. Among the headers it excludes is
// shellapi.h, so clap-wrapper's standalone entry point lost CommandLineToArgvW.
//
//   le/utility/windowsLite.hpp defines it for itself, which is where the request
// belongs: in the header that then includes <windows.h>.

// We use std::min/std::max(). Kept: NOMINMAX only suppresses two macros that
// nothing wants, and the third-party code here defines it for itself anyway.
#define NOMINMAX
#endif // LEB_INCLUDE_FULL_WINDOWS_HEADERS
#endif // _WIN32

////////////////////////////////////////////////////////////////////////////////
//
// Build tool specifics.
// ---------------------
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Microsoft Visual C++.
////////////////////////////////////////////////////////////////////////////////

#if defined(_MSC_VER)

#pragma once

// Implementation note:
//   A deficiency in CMake allows only MBCS or UNICODE Visual Studio builds.
// To work around this we use the CMake default (MBCS) and then manually
// undefine the relevant macro here (to get ASCII behaviour). Last tested
// with CMake 2.8.2.
//                                        (21.10.2010.) (Domagoj Saric)
#ifdef _MBCS
#undef _MBCS
#endif // _MBCS

// Implementation note:
//   Was ( _MSC_VER < 1400 ) || ( _MSC_VER > 1900 ) -- VS 2005 through VS 2015,
// which means every compiler newer than 2015 was warned about, once per
// translation unit. A ceiling on a supported compiler version is a guess about
// the future that ages into noise; only the floor is a fact, and the fact is now
// C++20 rather than whatever VS 2005 could manage.
#if _MSC_VER < 1929
#pragma message(                                                                                   \
    "WARNING: SpectrumWorx needs an MSVC with C++20 support -- 19.29 (VS 2019 16.10) or newer.")
#endif

#if (defined(_M_IX86) && (_M_IX86_FP == 1))
#define LE_HAS_SSE1
#endif

#if (defined(_M_IX86) && (_M_IX86_FP >= 2)) || defined(_M_X64)
#define LE_HAS_SSE1
#define LE_HAS_SSE2
#endif

// Implementation note:
//   Guarded because this header is force-included ahead of everything, ours and
// third-party alike, and the third party sets some of these too: JUCE's Harfbuzz
// unit defines _CRT_SECURE_NO_WARNINGS itself and got a macro-redefinition
// warning for its trouble. Defining a macro someone else also defines is only
// silent when both spell it the same way, which is not a thing to rely on.
#ifndef _ATL_SECURE_NO_WARNINGS
#define _ATL_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _SCL_SECURE_NO_WARNINGS
#define _SCL_SECURE_NO_WARNINGS
#endif
// Implementation note:
//   _CRT_DISABLE_PERFCRIT_LOCKS was defined here. It is not a hint: MSVC's
// <stdio.h> reads it and rewrites the standard names as macros --
//
//     #define fwrite _fwrite_nolock
//     #define fflush _fflush_nolock
//     #define fputc  _fputc_nolock
//
// -- so any qualified call turns into one naming a function that does not exist
// there. `std::fwrite( ... )` becomes `std::_fwrite_nolock( ... )`, and fmt,
// which qualifies properly, stopped compiling: "'_fwrite_nolock': is not a
// member of 'std'".
//
//   Force-included, so this was done to every dependency in the build to save a
// lock acquisition per stdio call in ours. It also silently makes stdio
// non-thread-safe, which is a poor trade to impose on code that never asked.
//
//   And it came back without us. On MSVC 14.51 `std::fputc` still expands to
// `std::_fputc_nolock` -- nothing in this tree or under libs/ defines
// _CRT_DISABLE_PERFCRIT_LOCKS, so the rewrite is the toolchain's own. What is
// certain is the shape: `fputc` is a macro there and `fputs` on the adjacent
// line is not. Our five one-character writes were all newlines, so they are
// `std::fputs( "\n", stderr )` now -- one call, no macro to be caught by, and
// nothing to re-diagnose the next time a compiler decides differently.

// Implementation note:
//   Five defines opting out of the secure CRT stood here, all of them
// LE_CHECKED_BUILD, so all of them off in a release build:
//
//     __STDC_WANT_SECURE_LIB__, _SECURE_ATL, and the three
//     _CRT_SECURE_CPP_OVERLOAD_{SECURE_NAMES,STANDARD_NAMES,STANDARD_NAMES_COUNT}
//
//   The opt-out is not available: __STDC_WANT_SECURE_LIB__ is what guards the
// declarations of sprintf_s and swprintf_s in <stdio.h>, and MSVC's own <string>
// calls both -- unguarded, as `_CSTD sprintf_s` -- to implement std::to_string
// and std::to_wstring. Setting it to zero makes the standard library fail to
// compile against itself.
//
//   That it ever built is the shim below: it defined those two names as
// object-like macros, so <string> line 490 read ::_snprintf instead and the
// contradiction went unnoticed for seventeen years. Removing the shim is what
// surfaced it. Both belong to one 2009 gesture, and neither half of it survives.
//
//   Nothing is lost by their going. Every one is a switch over which
// *declarations* exist, not over generated code -- there is no release build
// speed here to trade away, unlike the iterator debugging below. They also made
// checked and release builds see different overload sets, which is its own way
// to find out at release time that something does not compile.

#ifndef _ITERATOR_DEBUG_LEVEL
#define _SECURE_SCL LE_CHECKED_BUILD
#define _ITERATOR_DEBUG_LEVEL LE_CHECKED_BUILD
#endif // _ITERATOR_DEBUG_LEVEL
#if defined(DEBUG) || defined(_DEBUG)
#define _HAS_ITERATOR_DEBUGGING LE_CHECKED_BUILD
#if LE_CHECKED_BUILD
#undef _ITERATOR_DEBUG_LEVEL
#define _ITERATOR_DEBUG_LEVEL 2
#endif // LE_CHECKED_BUILD
#endif // DEBUG || _DEBUG

// Implementation note:
//   A 78-line "secure CRT" shim dated 2009 stood here, under
// `#if !__STDC_WANT_SECURE_LIB__` -- which is `#if !LE_CHECKED_BUILD`, so it was
// compiled into every release MSVC build rather than merely stale. It undefined
// __STDC_SECURE_LIB__ and __GOT_SECURE_LIB__, then supplied its own versions of
// what they announce:
//
//   - memcpy_s, memmove_s, wmemcpy_s, wmemmove_s, strcpy_s, strcat_s, wcscpy_s,
//     wcscat_s, strncpy_s and wcsncpy_s as inline forwarders that *discarded*
//     the destination-size argument and returned 0 -- the bounds check is the
//     entire difference between those names and the ones they call;
//   - sprintf_s and swprintf_s as object-like macros for _snprintf/_snwprintf,
//     so those two names were rewritten in every header our sources include,
//     ours or not, and neither replacement null-terminates on truncation;
//   - wcstok_s as a macro dropping its third argument, which today's three-
//     argument wcstok would reject outright.
//
//   Its 'broken' headers/libraries were ATL and a Dinkumware <xlocnum> of the
// era. Nothing in src/, tests/ or tools/ names any of these, and for a third-
// party header the rewrite could only break things: the vendored single-header
// Catch2 -- which our tests do not include, taking v3's modular headers --
// writes `sprintf_s( buffer, "%.3f", duration )`, which does not compile as
// _snprintf. So the shim was answering a 2009 question with a 2026 hazard.
//
//   With one client after all, found by deleting it: MSVC's own <string>, whose
// std::to_string and std::to_wstring call ::sprintf_s and ::swprintf_s. The
// macros were answering those calls. Taking them away left <string> naming two
// functions that __STDC_WANT_SECURE_LIB__ 0 had kept undeclared, and every
// Windows translation unit that reaches <string> stopped compiling -- which is
// to say all of them. The define above went with it, and that is the fix; the
// shim was never the thing holding this up, only the thing hiding it.

//   As we use a lot of heavy template (meta)programing it is actually
// useful to instruct the MSVC++ compiler to be maximally aggressive with
// inlining.
#pragma inline_depth(255)
#pragma inline_recursion(on)

#elif defined(__GNUC__) // compiler

/// \note This arm also defined __MMX__ itself when __SSE__ was set without it,
/// which was true of the 2011 x86 iOS simulator and of nothing this builds for:
/// on x86 both Clang and GCC define __MMX__ alongside __SSE__, and on ARM
/// neither is defined at all.
#if defined(__SSE__)
#define LE_HAS_SSE1
#endif

#if defined(__SSE2__)
#define LE_HAS_SSE2
#endif

#else

#error Your compiler is not supported by the Little Endian build system.

#endif // compiler

/// \note An `#ifdef __APPLE__` block stood here that added a std::nullptr_t
/// typedef for a libstdc++ older than 20110325, and included <cstddef> to have
/// somewhere to put it. Apple has been libc++ since well before that date, so
/// the `!defined( _LIBCPP_VERSION )` half was already false; the include went
/// with it, being the only thing in this header that was not a macro.

/// \note A "3rd party library specifics" section stood here, and by the end it
/// was three Boost macros -- BOOST_NO_IOSTREAM, BOOST_NO_TYPEID and
/// BOOST_EXCEPTION_DISABLE -- configuring Fusion, MPL and Preprocessor. Those
/// three went with the parameter system, and scripts/check_boost_allowlist.sh
/// now fails the build on a single Boost include anywhere in src/. So there is
/// no library left for these to configure.

//------------------------------------------------------------------------------
#endif // __cplusplus
//------------------------------------------------------------------------------
#endif // leConfigurationAndODRHeader_h
