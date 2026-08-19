////////////////////////////////////////////////////////////////////////////////
///
/// \file slicer.hpp
/// ----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef slicer_hpp__5484C80F_BF0B_46A9_BCB8_8F68F22B46DA
#define slicer_hpp__5484C80F_BF0B_46A9_BCB8_8F68F22B46DA
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
/// \class Slicer
///
/// \ingroup Effects
///
/// \brief Slice the signal into two parts.
///
/// This effect slices the signal into chunks that are alternated with one of
/// three different options.
///
////////////////////////////////////////////////////////////////////////////////

struct Slicer
{
    LE_ENUMERATED_PARAMETER(Mode, Hold, Silence, Side);

    LE_DEFINE_PARAMETER(TimeOn, LinearUnsignedInteger, Minimum<10>, Maximum<1000>, Default<250>,
                        Unit<" ms">);
    LE_DEFINE_PARAMETER(TimeOff, LinearUnsignedInteger, Minimum<10>, Maximum<1000>, Default<100>,
                        Unit<" ms">);
    LE_DEFINE_PARAMETERS(TimeOn, TimeOff, Mode);

    /// \typedef TimeOn
    /// \brief Determines the length of the main input slices, this slice goes
    /// through unmodified.
    /// \typedef TimeOff
    /// \brief Determines the length of the modified slices.
    /// \typedef Mode
    /// \brief Determines what modified slices contain.
    ///   - Hold: fills the gaps with a "frozen" sample of the last frame from
    /// the main input before the slice began.
    ///   - Silence: fills the slice with silence.
    ///   - Side: fills the slice with Side channel.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Slicer UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Slicer::TimeOn, "On Time")
EFFECT_PARAMETER_NAME(Slicer::TimeOff, "Slice Time")
EFFECT_PARAMETER_NAME(Slicer::Mode, "Slice Content")

EFFECT_PARAMETER_STREAMING_NAME(Slicer::TimeOn, "On time")
EFFECT_PARAMETER_STREAMING_NAME(Slicer::TimeOff, "Slice time")
EFFECT_PARAMETER_STREAMING_NAME(Slicer::Mode, "Slice content")

EFFECT_ENUMERATED_PARAMETER_STRINGS(Slicer, Mode,
    {Hold, "Sample&Hold"},
    {Silence, "Silence"},
    {Side, "Sidechain"})

EFFECT_ENUMERATED_PARAMETER_SHORT_STRINGS(Slicer, Mode,
    {Hold, "S&H"},
    {Silence, "Silence"},
    {Side, "Sidechain"})

} // namespace LE::SW::Effects

#endif // slicer_hpp
