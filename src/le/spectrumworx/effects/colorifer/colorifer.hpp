////////////////////////////////////////////////////////////////////////////////
///
/// \file colorifer.hpp
/// -------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef colorifer_hpp__B6E8308A_87E9_4d87_8E9E_C537504C54DA
#define colorifer_hpp__B6E8308A_87E9_4d87_8E9E_C537504C54DA
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
/// \class Colorifer
///
/// \ingroup Effects
///
/// \brief Spectral colouring.
///
/// Transfers the frequency shape from the side-channel to the signal arriving
/// through the main input. It calculates main/side over the selected bandwidth,
/// and then applies this "colour" to main. There is an option to also replace
/// phases of the main channel with those from side.
///
////////////////////////////////////////////////////////////////////////////////

struct Colorifer
{
    LE_ENUMERATED_PARAMETER(SpectrumPreprocess, NotUsed, SquareRoot, Square, Exponential);
    LE_ENUMERATED_PARAMETER(ReplacePhase, No, Yes);

    LE_DEFINE_PARAMETER(BandWidth, LinearUnsignedInteger, Minimum<0>, Maximum<6000>, Default<1000>,
                        Unit<" Hz">);
    LE_DEFINE_PARAMETERS(SpectrumPreprocess, BandWidth, ReplacePhase);

    /// \typedef SpectrumPreprocess
    /// \brief Specifies if preprocessing is done on the input signal.
    /// \details
    ///   - NotUsed: no preprocessing done.
    ///   - SquareRoot: square root applied to signal before further calculation.
    ///   - Square: square applied to signal before further calculation.
    ///   - Exponential: exponent applied to signal before further calculation.
    /// \typedef BandWidth
    /// \brief Bandwidth of the signal to take colour from.
    /// \typedef ReplacePhase
    /// \brief Specifies if input should take over side channel's phases.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Colorifer UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Colorifer::BandWidth, "Shape width")
EFFECT_PARAMETER_NAME(Colorifer::SpectrumPreprocess, "Spectrum preprocess")
EFFECT_PARAMETER_NAME(Colorifer::ReplacePhase, "Replace phase")

EFFECT_ENUMERATED_PARAMETER_STRINGS(Colorifer, SpectrumPreprocess,
    {NotUsed, "None"},
    {SquareRoot, "Square root"},
    {Square, "Square"},
    {Exponential, "Exponent"})

EFFECT_ENUMERATED_PARAMETER_STRINGS(Colorifer, ReplacePhase,
    {No, "No"},
    {Yes, "Yes"})

} // namespace LE::SW::Effects

#endif // colorifer_hpp
