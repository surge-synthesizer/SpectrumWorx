////////////////////////////////////////////////////////////////////////////////
///
/// \file pitchShifter.hpp
/// ----------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef pitchShifter_hpp__4124BAA3_F146_4A5D_9C9B_BB09EFCE82DB
#define pitchShifter_hpp__4124BAA3_F146_4A5D_9C9B_BB09EFCE82DB
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/symmetric/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

namespace Detail
{
struct PitchShifterBase
{
    LE_DEFINE_PARAMETER(SemiTones, SymmetricFloat, MaximumOffset<24>, Unit<"'">);
    LE_DEFINE_PARAMETER(Cents, SymmetricInteger, MaximumOffset<100>, Unit<"''">);
    LE_DEFINE_PARAMETERS(SemiTones, Cents);

    /// \typedef SemiTones
    /// \brief Specifies the number of semitones to pitch shift.
    /// \typedef Cents
    /// \brief Specifies the number of cents to pitch shift (adds to
    /// semitones).

    static char const description[];
};
} // namespace Detail

////////////////////////////////////////////////////////////////////////////////
///
/// \class PitchShifter
///
/// \ingroup Effects
///
/// \brief Pitch shift only into the selected band.
///
/// This module is as straight forward as it gets. It shifts the pitch of the
/// incoming signal in semitones and cents. You can go anywhere between two
/// octaves higher and two octaves lower than the original signal.
///
////////////////////////////////////////////////////////////////////////////////

struct PitchShifter : Detail::PitchShifterBase
{
    static char const title[];
};

////////////////////////////////////////////////////////////////////////////////
///
/// \class PVPitchShifter
///
/// \ingroup Effects
///
/// \brief Pitch shift only into the selected band.
///
////////////////////////////////////////////////////////////////////////////////

struct PVPitchShifter : Detail::PitchShifterBase
{
    static char const title[];
};

////////////////////////////////////////////////////////////////////////////////
//
// PitchShifter UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Detail::PitchShifterBase::SemiTones, "Semitones")
EFFECT_PARAMETER_NAME(Detail::PitchShifterBase::Cents, "Cents")

} // namespace LE::SW::Effects

#endif // pitchShifter_hpp
