////////////////////////////////////////////////////////////////////////////////
///
/// \file shapeless.hpp
/// --------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef shapeless_hpp__DBB27280_19FF_4CF5_B1E6_A9CFB52749B7
#define shapeless_hpp__DBB27280_19FF_4CF5_B1E6_A9CFB52749B7
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
/// \class Shapeless
///
/// \ingroup Effects
///
/// \brief Applies frequency shape from side channel.
///
/// Transfers frequency shape from the side-channel to the input signal. Width
/// parameter regulates the coarseness of the shape estimators.
///
////////////////////////////////////////////////////////////////////////////////

struct Shapeless
{
    LE_DEFINE_PARAMETER(Width, LinearUnsignedInteger, Minimum<0>, Maximum<4000>, Default<200>,
                        Unit<" Hz">);
    LE_DEFINE_PARAMETERS(Width);

    /// \typedef Width
    /// \brief Width of the region to collect the shape from.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Shapeless UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Shapeless::Width, "Width")

EFFECT_PARAMETER_STREAMING_NAME(Shapeless::Width, "Shape width")

} // namespace LE::SW::Effects

#endif // shapeless_hpp
