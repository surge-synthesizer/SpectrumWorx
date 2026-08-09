////////////////////////////////////////////////////////////////////////////////
///
/// \file bandpass.hpp
/// ------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef bandpass_hpp__3E2335FA_F97E_47A9_9BED_977505A90353
#define bandpass_hpp__3E2335FA_F97E_47A9_9BED_977505A90353
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

namespace Detail
{
struct BandGain
{
    LE_DEFINE_PARAMETER(Attenuation, LinearFloat, Minimum<0>, Maximum<60>, Default<0>, Unit<" dB">);
    LE_DEFINE_PARAMETERS(Attenuation);

    /// \typedef Attenuation
    /// \brief Amount of signal attenuation (within the selected frequency
    /// band).
};
} // namespace Detail

////////////////////////////////////////////////////////////////////////////////
///
/// \class Bandpass
///
/// \ingroup Effects
///
/// \brief Band pass filter.
///
/// A simple bandpass filter with a variable pass band. The pass band is defined
/// using the standard StartFrequency and StopFrequency parameters. This effect
/// somewhat deviates from the usual behaviour because it affects (attenuates)
/// the parts of the signal _outside_ the specified frequency range.
///
////////////////////////////////////////////////////////////////////////////////

struct Bandpass : Detail::BandGain
{
    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
///
/// \class Bandstop
///
/// \ingroup Effects
///
/// \brief Band-stop filter.
///
/// A simple bandstop filter with a variable stop band. The stop band is defined
/// using the standard StartFrequency and StopFrequency parameters.
///
////////////////////////////////////////////////////////////////////////////////

struct Bandstop : Detail::BandGain
{
    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// BandGain UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Detail::BandGain::Attenuation, "Attenuation")

} // namespace LE::SW::Effects

#endif // bandpass_hpp
