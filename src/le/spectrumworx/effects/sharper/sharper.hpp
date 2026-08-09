////////////////////////////////////////////////////////////////////////////////
///
/// \file sharper.hpp
/// -----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef sharper_hpp__F315fE63_42EA_4CF4_AE4D_180C4A01F4DE
#define sharper_hpp__F315fE63_42EA_4CF4_AE4D_180C4A01F4DE
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
/// \class Sharper
///
/// \ingroup Effects
///
/// \brief Apply sharper effect using high pass filter.
///
/// The opposite of the Smoother effect, this effect sharpens the spectrum.
/// The smoothed signal is subtracted from original to arrive at the sharpened
/// signal.
///
////////////////////////////////////////////////////////////////////////////////

struct Sharper
{
    LE_DEFINE_PARAMETER(AveragingWidth, LinearUnsignedInteger, Minimum<0>, Maximum<5000>,
                        Default<1000>, Unit<" Hz">);
    LE_DEFINE_PARAMETER(Intensity, LinearFloat, Minimum<0>, Maximum<+72>, Default<20>, Unit<" dB">);
    LE_DEFINE_PARAMETER(Limiter, LinearFloat, Minimum<-80>, Maximum<0>, Default<-20>, Unit<" dB">);
    LE_DEFINE_PARAMETERS(AveragingWidth, Intensity, Limiter);

    /// \typedef AveragingWidth
    /// \brief Width of the region to be sharpened.
    /// \typedef Intensity
    /// \brief Increases the sharpening effect.
    /// \typedef Limiter
    /// \brief Hard limiter maintains stability.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Sharper UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Sharper::AveragingWidth, "Sharpness")
EFFECT_PARAMETER_NAME(Sharper::Intensity, "Intensity")
EFFECT_PARAMETER_NAME(Sharper::Limiter, "Limit")

} // namespace LE::SW::Effects

#endif // sharper_hpp
