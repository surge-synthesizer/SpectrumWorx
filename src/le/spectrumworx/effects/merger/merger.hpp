////////////////////////////////////////////////////////////////////////////////
///
/// \file merger.hpp
/// ----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef merger_hpp__6BCD6590_5CD7_40EE_9566_2D273B6A9B1C
#define merger_hpp__6BCD6590_5CD7_40EE_9566_2D273B6A9B1C
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
/// \class Merger
///
/// \ingroup Effects
///
/// \brief Combines two samples.
///
/// This effect provides conditional combinations of the input signal with that
/// of the side channel. Copies the side channel to the input channel if the
/// selected conditions are met, and depending on the threshold setting.
///
////////////////////////////////////////////////////////////////////////////////

struct Merger
{
    LE_ENUMERATED_PARAMETER(Operation, MainLargerThanSide, SideLargerThanMain, MainAboveThreshold,
                            SideAboveThreshold, MainBelowThreshold, SideBelowThreshold);

    LE_DEFINE_PARAMETER(Threshold, LinearFloat, Minimum<-120>, Maximum<0>, Default<-20>,
                        Unit<" dB">);
    LE_DEFINE_PARAMETERS(Operation, Threshold);

    /// \typedef Operation
    /// \brief Defines the operation to be executed.
    /// \details
    ///   - MainLargerThanSide: replaces main channel with side where main larger than side.
    ///   - SideLargerThanMain: replaces main channel with side where side larger than main.
    ///   - MainAboveThreshold: replaces main channel with side where main above threshold.
    ///   - SideAboveThreshold: replaces main channel with side where side above threshold.
    ///   - MainBelowThreshold: replaces main channel with side where main below threshold.
    ///   - SideBelowThreshold: replaces main channel with side where side above threshold.
    /// \typedef Threshold
    /// \brief Threshold used for comparisons.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Merger UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Merger::Threshold, "Threshold")
EFFECT_PARAMETER_NAME(Merger::Operation, "Condition")

EFFECT_ENUMERATED_PARAMETER_STRINGS(Merger, Operation,
    {MainLargerThanSide, "Main>Side"},
    {SideLargerThanMain, "Side>Main"},
    {MainAboveThreshold, "Main>Thr"},
    {SideAboveThreshold, "Side>Thr"},
    {MainBelowThreshold, "Main<Thr"},
    {SideBelowThreshold, "Side<Thr"})

} // namespace LE::SW::Effects

#endif // merger_hpp
