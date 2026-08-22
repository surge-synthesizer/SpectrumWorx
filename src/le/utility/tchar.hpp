////////////////////////////////////////////////////////////////////////////////
///
/// \file tchar.hpp
/// ---------------
///
/// Selected (Microsoft's) tchar.h bits for compilers that do not provide them.
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef tchar_hpp__5137b405_2BF4_4714_8BB3_8F3342D64267
#define tchar_hpp__5137b405_2BF4_4714_8BB3_8F3342D64267
//------------------------------------------------------------------------------
#include <string_view>
#ifdef _MSC_VER
#include "tchar.h"
#else
#include <cstdlib>
#include <cstring>

#ifdef _UNICODE
typedef wchar_t TCHAR;

#define _T(x) L##x

#define _itot _itow
#define _ltot _ltow
#define _ultot _ultow

#define _tcscpy wcscpy
#define _tcslen wcslen
#define _tcscmp wcscmp
#define _tcsrchr wcsrchr

#define _stprintf swprintf
#define _sntprintf _snwprintf
#else
typedef char TCHAR;

#define _T(x) x

#define _itot _itoa
#define _ltot _ltoa
#define _ultot _ultoa

#define _tcscpy strcpy
#define _tcslen strlen
#define _tcscmp strcmp
#define _tcsrchr strrchr

#define _stprintf sprintf
#define _sntprintf snprintf
#endif
#endif // _MSC_VER

#include "platformSpecifics.hpp"

/// \note There was a global operator==( std::string_view, std::string_view )
/// here, comparing with memcmp over begin(). The standard library has provided
/// that comparison since C++17, so it was a second candidate for every
/// string_view comparison in the codebase -- and it only ever compiled because a
/// string_view iterator happens to be a `char const *` in libc++ and libstdc++.
/// MSVC's is a class type, which is what finally objected.

//------------------------------------------------------------------------------
#endif // tchar_hpp
