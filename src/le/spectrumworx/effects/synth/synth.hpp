////////////////////////////////////////////////////////////////////////////////
///
/// \file synth.hpp
/// ---------------
///
/// Copyright (c) 2015 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef synth_hpp__84523FA6_3644_4D1D_9AA8_0C6C4E416D82
#define synth_hpp__84523FA6_3644_4D1D_9AA8_0C6C4E416D82
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/symmetric/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Synth
///
/// \ingroup Effects Instruments
///
/// \brief <B>Advanced&Experimental:</B> a pure sine/tone generating effect
/// (i.e. an 'instrument').
/// \details The waveform is generated into the side-chain bus/signal.
///
////////////////////////////////////////////////////////////////////////////////

struct Synth
{
    LE_DEFINE_PARAMETER(Frequency, LinearFloat, Minimum<40>, Maximum<8000>, Default<110>);
    LE_DEFINE_PARAMETER(HarmonicSlope, LinearUnsignedInteger, Minimum<0>, Maximum<100>, Default<50>,
                        Unit<"%">);
    LE_DEFINE_PARAMETER(FlangeIntensity, LinearUnsignedInteger, Minimum<0>, Maximum<100>,
                        Default<0>, Unit<"%">);
    LE_DEFINE_PARAMETER(FlangeOffset, SymmetricFloat, MaximumOffset<180>, Default<10>,
                        Unit<"\xB0">);
    LE_DEFINE_PARAMETERS(Frequency, HarmonicSlope, FlangeIntensity, FlangeOffset);

    /// \typedef Frequency
    /// \brief .
    /// \typedef HarmonicSlope
    /// \brief .
    /// \typedef FlangeIntensity
    /// \brief .
    /// \typedef FlangeOffset
    /// \brief .

    static char const title[];
    static char const description[];
}; // struct Synth

////////////////////////////////////////////////////////////////////////////////
//
// Synth UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Synth::Frequency, "Frequency")
EFFECT_PARAMETER_NAME(Synth::HarmonicSlope, "Slope")
EFFECT_PARAMETER_NAME(Synth::FlangeIntensity, "Flange amount")
EFFECT_PARAMETER_NAME(Synth::FlangeOffset, "Flange offset")

} // namespace LE::SW::Effects

#endif // synth_hpp
