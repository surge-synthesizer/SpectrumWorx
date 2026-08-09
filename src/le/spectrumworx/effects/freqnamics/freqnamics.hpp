////////////////////////////////////////////////////////////////////////////////
///
/// \file freqnamics.hpp
/// --------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef freqnamics_hpp__66F9664C_18BF_4F34_9C54_83FCB44DC9F1
#define freqnamics_hpp__66F9664C_18BF_4F34_9C54_83FCB44DC9F1
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
/// \class Freqnamics
///
/// \ingroup Effects
///
/// \brief Dynamics processing in the frequency domain.
///
/// Provides dynamics processing in the frequency domain. Acts as both a limiter
/// and a noise gate. Everything above limiter threshold is flattened, and
/// everything below noise gate threshold is removed.
///
////////////////////////////////////////////////////////////////////////////////

struct Freqnamics
{
    LE_DEFINE_PARAMETER(LimiterThreshold, LinearFloat, Minimum<-90>, Maximum<0>, Default<-10>,
                        Unit<" dB">);
    LE_DEFINE_PARAMETER(NoisegateThreshold, LinearFloat, Minimum<-90>, Maximum<0>, Default<-60>,
                        Unit<" dB">);
    LE_DEFINE_PARAMETERS(LimiterThreshold, NoisegateThreshold);

    /// \typedef LimiterThreshold
    /// \brief Limiter threshold.
    /// \typedef NoisegateThreshold
    /// \brief Noise gate threshold.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Freqnamics UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Freqnamics::LimiterThreshold, "Limiter")
EFFECT_PARAMETER_NAME(Freqnamics::NoisegateThreshold, "Noise gate")

} // namespace LE::SW::Effects

#endif // freqnamics_hpp
