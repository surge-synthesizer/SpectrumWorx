////////////////////////////////////////////////////////////////////////////////
///
/// \file talkBox.hpp
/// -----------------
///
/// Copyright (c) 2015 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef talkBox_hpp__5FCE18B2_5D73_4BE1_A361_A694B2655204
#define talkBox_hpp__5FCE18B2_5D73_4BE1_A361_A694B2655204
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/spectrumworx/effects/synth/synth.hpp"
#include "le/parameters/boolean/parameter.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class TalkBox
///
/// \ingroup Effects
///
/// \brief Vocoder with a builtin 'robotic' carrier.
///
////////////////////////////////////////////////////////////////////////////////

struct TalkBox
{
    /// \name Parameters
    /// @{
    typedef Synth::HarmonicSlope HarmonicSlope;
    typedef Synth::FlangeIntensity FlangeIntensity;
    typedef Synth::FlangeOffset FlangeOffset;
    /// @}

    LE_DEFINE_PARAMETER(ExternalCarrier, Boolean);
    LE_DEFINE_PARAMETER(BaseFrequency, LinearFloat, Minimum<40>, Maximum<400>, Default<100>);
    LE_DEFINE_PARAMETER(CutOff, LinearUnsignedInteger, Minimum<0>, Maximum<12000>, Default<9000>,
                        Unit<"Hz">);
    LE_DEFINE_PARAMETERS(ExternalCarrier, BaseFrequency, CutOff, HarmonicSlope, FlangeIntensity,
                         FlangeOffset);

    /// \typedef BaseFrequency
    /// \brief .
    /// \typedef Flanging
    /// \brief .
    /// \typedef Whispering
    /// \brief .

    static char const title[];
    static char const description[];
}; // struct TalkBox

////////////////////////////////////////////////////////////////////////////////
//
// TalkBox UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(TalkBox::ExternalCarrier, "External carrier")
EFFECT_PARAMETER_NAME(TalkBox::BaseFrequency, "Base frequency")
EFFECT_PARAMETER_NAME(TalkBox::CutOff, "Cutoff")

} // namespace LE::SW::Effects

#endif // talkBox_hpp
