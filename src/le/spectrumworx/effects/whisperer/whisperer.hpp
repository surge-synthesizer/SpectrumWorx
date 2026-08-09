////////////////////////////////////////////////////////////////////////////////
///
/// \file whisperer.hpp
/// -------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef whisperer_hpp__7E88348A_B8DC_4D2C_B805_7E845F3B5ACF
#define whisperer_hpp__7E88348A_B8DC_4D2C_B805_7E845F3B5ACF
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Whisperer
///
/// \ingroup Effects
///
/// \brief Creates a whispering sound. Use with small frame sizes.
///
/// Provides a whispering effect by randomizing the phases.
///
/// \note Needs a short frame size for proper operation.
///
////////////////////////////////////////////////////////////////////////////////

struct Whisperer
{
    static char const title[];
    static char const description[];
};

} // namespace LE::SW::Effects

#endif // whisperer_hpp
