////////////////////////////////////////////////////////////////////////////////
///
/// \file armonizer.hpp
/// -------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef armonizer_hpp__D2DC61B5_FEEC_4FD5_B4DA_E89976A55507
#define armonizer_hpp__D2DC61B5_FEEC_4FD5_B4DA_E89976A55507
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/symmetric/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Armonizer
///
/// \ingroup Effects
///
/// \brief Pitch harmonizer.
///
/// Adds a pitch-shifted copy of an incoming signal to the original. Blending
/// of the original and shifted signals can be controlled with the
/// BaseParameters::Wet parameter. In the special case of the Armonizer
/// effect, this (Wet) parameter defaults to the value of 50% (as opposed to
/// 100%).
///
////////////////////////////////////////////////////////////////////////////////

struct Armonizer
{
    LE_DEFINE_PARAMETER(Interval, SymmetricFloat, MaximumOffset<24>, Unit<"'">);
    LE_DEFINE_PARAMETERS(Interval);

    /// \typedef Interval
    /// \brief Controls the amount of pitch shifting.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Armonizer UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Armonizer::Interval, "Interval")

} // namespace LE::SW::Effects

#endif // armonizer_hpp
