////////////////////////////////////////////////////////////////////////////////
///
/// \file convolver.hpp
/// -------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef convolver_hpp__A7F10DAF_A6F4_44E2_B6B4_460805ACC405
#define convolver_hpp__A7F10DAF_A6F4_44E2_B6B4_460805ACC405
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/enumerated/parameter.hpp"
#include "le/parameters/trigger/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Convolver
///
/// \ingroup Effects
///
/// \brief Performs multiplications in frequency domain i.e. convolution in
/// time domain.
///
////////////////////////////////////////////////////////////////////////////////

struct Convolver
{
  public: // LE::Algorithm required interface.
    ////////////////////////////////////////////////////////////////////////////
    // Parameters
    ////////////////////////////////////////////////////////////////////////////

    LE_ENUMERATED_PARAMETER(ConvolutionType, Triggered, Continuous);
    LE_ENUMERATED_PARAMETER(Phase, Sum, Side, Main);

    LE_DEFINE_PARAMETER(GrabIR, TriggerParameter);
    LE_DEFINE_PARAMETERS(ConvolutionType, GrabIR, Phase);

    /// \typedef ConvolutionType
    /// \brief Defines the type of convolution.
    /// \details
    ///   - Continuous: continuous convolution with the Side channel.
    ///   - Triggered: new Impulse Response is taken from Side channel
    ///                after trigger button is activated.
    /// \typedef GrabIR
    /// \brief Grabs the impulse response from the side channel.
    /// \typedef Phase
    /// \brief Defines which phase will the output signal take.
    /// \details
    ///   - Sum: sum main and side channel phases
    ///   - Side: forward side channel phases output
    ///   - Main: forward main channel phases output

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Convolver UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Convolver::ConvolutionType, "Type")
EFFECT_PARAMETER_NAME(Convolver::GrabIR, "Grab IR")
EFFECT_PARAMETER_NAME(Convolver::Phase, "Phase")

EFFECT_ENUMERATED_PARAMETER_STRINGS(Convolver, ConvolutionType,
    {Triggered, "Triggered"},
    {Continuous, "Continuous"})

EFFECT_ENUMERATED_PARAMETER_STRINGS(Convolver, Phase,
    {Sum, "Sum"},
    {Side, "Side"},
    {Main, "Main"})

} // namespace LE::SW::Effects

#endif // convolver_hpp
