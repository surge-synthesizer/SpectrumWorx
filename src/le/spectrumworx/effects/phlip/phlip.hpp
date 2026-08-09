////////////////////////////////////////////////////////////////////////////////
///
/// \file phlip.hpp
/// ---------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef phlip_hpp__5C84E8DC_452A_4397_A386_39F88ADAC1F6
#define phlip_hpp__5C84E8DC_452A_4397_A386_39F88ADAC1F6
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/enumerated/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Phlip
///
/// \ingroup Effects
///
/// \brief Inverts the phase.
///
/// This module flips the phase. The effect is quite variable, depending very
/// much on the harmonic content of the incoming signal. Odd harmonics tend to
/// result in a more dissonant sound, while even harmonics are more consonant
/// and pleasant to the ear. Also, the shorter the frame size the more audible
/// effect will be.
///
////////////////////////////////////////////////////////////////////////////////

struct Phlip
{
    LE_ENUMERATED_PARAMETER(Mode, All, Even, Odd);

    LE_DEFINE_PARAMETERS(Mode);

    /// \typedef Mode
    /// \brief Controls the target harmonics.
    /// \details
    ///   - All: targets all harmonics.
    ///   - Even: targets only even harmonics.
    ///   - Odd: targets only odd harmonics.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Phlip UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Phlip::Mode, "Target harmonics")

EFFECT_ENUMERATED_PARAMETER_STRINGS(Phlip, Mode,
    {All, "All"},
    {Even, "Even"},
    {Odd, "Odd"})

} // namespace LE::SW::Effects

#endif // phlip__hpp
