////////////////////////////////////////////////////////////////////////////////
///
/// \file gain.hpp
/// --------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef gain_hpp__FD752E55_5DB0_41FF_80CF_BFB7CE1C42E9
#define gain_hpp__FD752E55_5DB0_41FF_80CF_BFB7CE1C42E9
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen

namespace LE::SW::Effects
{
////////////////////////////////////////////////////////////////////////////////
///
/// \class Gain
///
/// \ingroup Effects
///
/// \brief Applies gain on the selected range.
///
////////////////////////////////////////////////////////////////////////////////

struct Gain
{
    static char const title[];
    static char const description[];
};

} // namespace LE::SW::Effects

#endif // gain_hpp
