////////////////////////////////////////////////////////////////////////////////
///
/// \file ethereal.hpp
/// ------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef ethereal_hpp__A78CE259_D3CA_4556_8B65_AAF88162B342
#define ethereal_hpp__A78CE259_D3CA_4556_8B65_AAF88162B342
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/commonParameters.hpp"
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/enumerated/parameter.hpp"
#include "le/parameters/symmetric/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Ethereal
///
/// \ingroup Effects
///
/// \brief Replaces main with side channel based on magnitude comparisons.
///
/// Compares the side-channel signal with that of the input and it replaces the
/// input with the side signal if certain conditions are met:
/// (input - side > or < threshold). Can replace magnitudes or phases, or both.
///
////////////////////////////////////////////////////////////////////////////////

struct Ethereal
{
    /// \name Parameters
    /// @{
    typedef CommonParameters::Mode Mode;
    /// @}

    LE_ENUMERATED_PARAMETER(Condition, DiffHigher, DiffLower);

    LE_DEFINE_PARAMETER(Threshold, SymmetricFloat, MaximumOffset<30>, Unit<" dB">);
    LE_DEFINE_PARAMETERS(Condition, Threshold, Mode);

    /// \typedef Condition
    /// \brief Condition to meet in order to replace input with side channel.
    /// \details
    ///   - DiffHigher: replace if (input - side) > threshold
    ///   - DiffLower: replace if (input - side) < threshold
    /// \typedef Threshold
    /// \brief Comparison threshold.
    /// \typedef Mode
    /// \brief Specifies what is to be replaced.
    /// \details
    ///   - Both      : replace both magnitudes and phases.
    ///   - Magnitudes: replace only magnitudes.
    ///   - Phases    : replace only phases.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Ethereal UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Ethereal::Threshold, "Threshold")
EFFECT_PARAMETER_NAME(Ethereal::Condition, "Swap Condition")

EFFECT_PARAMETER_STREAMING_NAME(Ethereal::Condition, "Swap condition")

EFFECT_ENUMERATED_PARAMETER_STRINGS(Ethereal, Condition,
    {DiffHigher, "Main - Side > Threshold"},
    {DiffLower, "Main - Side < Threshold"})

EFFECT_ENUMERATED_PARAMETER_SHORT_STRINGS(Ethereal, Condition,
    {DiffHigher, "M - S > Thr"},
    {DiffLower, "M - S < Thr"})

} // namespace LE::SW::Effects

#endif // ethereal_hpp
