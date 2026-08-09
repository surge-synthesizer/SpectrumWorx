////////////////////////////////////////////////////////////////////////////////
///
/// \file reverser.hpp
/// ------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef reverser_hpp__C3A377D7_0B2D_4B86_B9F9_5609C8675D7E
#define reverser_hpp__C3A377D7_0B2D_4B86_B9F9_5609C8675D7E
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Reverser
///
/// \ingroup Effects
///
/// \brief Reverses sound chunks.
///
/// Breaks the input down into chunks and reverses them.
///
////////////////////////////////////////////////////////////////////////////////

struct Reverser
{
    LE_DEFINE_PARAMETER(Length, LinearUnsignedInteger, Minimum<1>, Maximum<5000>, Default<1000>,
                        Unit<" ms">);
    LE_DEFINE_PARAMETERS(Length);

    /// \typedef Length
    /// \brief The length of the chunks to be reversed.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Reverser UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Reverser::Length, "Chunk length")

} // namespace LE::SW::Effects

#endif // reverser_hpp
