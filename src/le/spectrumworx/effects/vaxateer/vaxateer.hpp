////////////////////////////////////////////////////////////////////////////////
///
/// \file vaxateer.hpp
/// ------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef vaxateer_hpp__BE1E8F69_E3AC_444F_91A4_4906B7C669E4
#define vaxateer_hpp__BE1E8F69_E3AC_444F_91A4_4906B7C669E4
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/enumerated/parameter.hpp"
#include "le/parameters/symmetric/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Vaxateer
///
/// \ingroup Effects
///
/// \brief Spectral composition based on RMS.
///
/// Conditional replacement of the Main signal with the Side-channel based on
/// the RMS value (Root Mean Square) of the target signal (can be Main or
/// Side-channel). If the "Swap condition" is satisfied, Main signal is replaced
/// with the Side-channel content. There are eight available Swap conditions.
///
////////////////////////////////////////////////////////////////////////////////

struct Vaxateer
{
    LE_ENUMERATED_PARAMETER(RMSTarget, MainRMS, SideRMS);
    LE_ENUMERATED_PARAMETER(Mode, M1, M2, M3, M4, M5, M6, M7, M8);

    LE_DEFINE_PARAMETER(RMSGain, SymmetricInteger, MaximumOffset<24>, Unit<" dB">);
    LE_DEFINE_PARAMETERS(RMSTarget, RMSGain, Mode);

    /// \typedef RMSTarget
    /// \brief Target channel for RMS calculation, can be Main or Side.
    /// \typedef RMSGain
    /// \brief Gain applied to the calculated RMS, to be used as threshold for
    /// comparison.
    /// \typedef Mode
    /// \brief Specifies the swap condition.
    /// \details
    ///   - M1 (Main: >Thr >Side): Main should be higher than threshold and higher than Side.
    ///   - M2 (Main: >Thr <Side): Main should be higher than threshold and lower than Side.
    ///   - M3 (Main: <Thr >Side): Main should be lower than threshold and higher than Side.
    ///   - M4 (Main: <Thr <Side): Main should be lower than threshold and lower than Side.
    ///   - M5 (Side: >Thr >Main): Side should be higher than threshold and higher than Main.
    ///   - M6 (Side: >Thr <Main): Side should be higher than threshold and lower than Main.
    ///   - M7 (Side: <Thr >Main): Side should be lower than threshold and higher than Main.
    ///   - M8 (Side: <Thr <Main): Side should be lower than threshold and lower than Main.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Vaxateer UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Vaxateer::RMSTarget, "RMS Target")
EFFECT_PARAMETER_NAME(Vaxateer::RMSGain, "Threshold")
EFFECT_PARAMETER_NAME(Vaxateer::Mode, "Swap Condition")

EFFECT_PARAMETER_STREAMING_NAME(Vaxateer::RMSTarget, "RMS target")
EFFECT_PARAMETER_STREAMING_NAME(Vaxateer::RMSGain, "RMS threshold gain")
EFFECT_PARAMETER_STREAMING_NAME(Vaxateer::Mode, "Swap condition")

EFFECT_ENUMERATED_PARAMETER_STRINGS(Vaxateer, RMSTarget,
    {MainRMS, "Main"},
    {SideRMS, "Sidechain"})

EFFECT_ENUMERATED_PARAMETER_STRINGS(Vaxateer, Mode,
    {M1, "Main: >Threshold >Sidechain"},
    {M2, "Main: >Threshold <Sidechain"},
    {M3, "Main: <Threshold >Sidechain"},
    {M4, "Main: <Threshold <Sidechain"},
    {M5, "Sidechain: >Threshold >Main"},
    {M6, "Sidechain: >Threshold <Main"},
    {M7, "Sidechain: <Threshold >Main"},
    {M8, "Sidechain: <Threshold <Main"})

/// \note Eight conditions, sixteen characters each, in a sixty-pixel box: the
/// widget squeezed them to a tenth of their width and the reading was a smear.
/// The menu still lists them in full -- this is only what the box reads once one
/// is chosen. \see ShortValues and issue #120.
EFFECT_ENUMERATED_PARAMETER_SHORT_STRINGS(Vaxateer, Mode,
    {M1, "Main: >T >SC"},
    {M2, "Main: >T <SC"},
    {M3, "Main: <T >SC"},
    {M4, "Main: <T <SC"},
    {M5, "SC: >T >M"},
    {M6, "SC: >T <M"},
    {M7, "SC: <T >M"},
    {M8, "SC: <T <M"})

} // namespace LE::SW::Effects

#endif // vaxateer_hpp
