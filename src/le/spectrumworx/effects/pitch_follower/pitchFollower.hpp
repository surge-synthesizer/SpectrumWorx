////////////////////////////////////////////////////////////////////////////////
///
/// \file pitchFollower.hpp
/// -----------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef pitchFollower_hpp__2BDC51A5_6E70_4841_B2B6_E2C764777A79
#define pitchFollower_hpp__2BDC51A5_6E70_4841_B2B6_E2C764777A79
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
struct PitchFollowerBase
{
    LE_DEFINE_PARAMETER(Speed, LinearFloat, Minimum<0>, Maximum<60>, Default<1>, Unit<" '/s">);
    LE_DEFINE_PARAMETERS(Speed);

    /// \typedef Speed
    /// \brief Determines how fast the pitch is followed (in semitones per
    /// second).

    static char const description[];
};
} // namespace Detail

////////////////////////////////////////////////////////////////////////////////
///
/// \class PitchFollower
///
/// \ingroup Effects
///
/// \brief Relay pitch from side channel to main channel.
///
/// The main channel is pitch-shifted to the dominant frequency detected in
/// the side-channel. The Speed parameter determines how quickly the
/// pitch-shifting to the target frequency should be done in order to provide
/// a smoother result.
///
////////////////////////////////////////////////////////////////////////////////

struct PitchFollower : Detail::PitchFollowerBase
{
    static char const title[];
};

////////////////////////////////////////////////////////////////////////////////
///
/// \class PitchFollowerPVD
///
/// \ingroup Effects
///
/// \brief Relay pitch from side channel to main channel.
///
////////////////////////////////////////////////////////////////////////////////

struct PitchFollowerPVD : Detail::PitchFollowerBase
{
    static char const title[];
};

////////////////////////////////////////////////////////////////////////////////
//
// PitchFollower UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Detail::PitchFollowerBase::Speed, "Speed")

} // namespace LE::SW::Effects

#endif // pitchFollower_hpp
