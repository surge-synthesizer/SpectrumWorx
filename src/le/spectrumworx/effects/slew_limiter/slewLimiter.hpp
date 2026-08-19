////////////////////////////////////////////////////////////////////////////////
///
/// \file slewLimiter.hpp
/// ---------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef slewLimiter_hpp__3E24E3B5_8DED_4323_8369_F01D41240B98
#define slewLimiter_hpp__3E24E3B5_8DED_4323_8369_F01D41240B98
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/enumerated/parameter.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class SlewLimiter
///
/// \ingroup Effects
///
/// \brief Limits maximum per-bin magnitude change speed.
///
/// Limits the speed of change of the magnitudes. The direction and the rate
/// of the slew are selectable.
///
////////////////////////////////////////////////////////////////////////////////

struct SlewLimiter
{
  public:
    LE_ENUMERATED_PARAMETER(Direction, RiseFall, Rise, Fall);

    LE_DEFINE_PARAMETER(SlewRate, LinearFloat, Minimum<0>, Maximum<300>, Default<50>,
                        Unit<" dB/s">);
    LE_DEFINE_PARAMETERS(Direction, SlewRate);

    /// \typedef Direction
    /// \brief Determines the slew direction.
    /// \details
    ///   - RiseFall: limits both rise and fall.
    ///   - Rise: limits rise of the amplitudes.
    ///   - Fall: limits fall of the amplitudes.
    /// \typedef SlewRate
    /// \brief Limit in decibels per second.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// SlewLimiter UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(SlewLimiter::SlewRate, "Rate")
EFFECT_PARAMETER_NAME(SlewLimiter::Direction, "Direction")

EFFECT_PARAMETER_STREAMING_NAME(SlewLimiter::SlewRate, "Slew rate")

EFFECT_ENUMERATED_PARAMETER_STRINGS(SlewLimiter, Direction,
    {RiseFall, "Rise & Fall"},
    {Rise, "Rise"},
    {Fall, "Fall"})

} // namespace LE::SW::Effects

#endif // slewLimiter_hpp
