////////////////////////////////////////////////////////////////////////////////
///
/// \file freeze.hpp
/// ----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef freeze_hpp__1C855F51_D0CA_48B9_8F40_4B259941C1F9
#define freeze_hpp__1C855F51_D0CA_48B9_8F40_4B259941C1F9
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/trigger/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Freeze
///
/// \ingroup Effects
///
/// \brief Time freeze with smooth transition between normal and frozen state.
///
////////////////////////////////////////////////////////////////////////////////

struct Freeze
{
    ////////////////////////////////////////////////////////////////////////////
    // Parameters
    ////////////////////////////////////////////////////////////////////////////

    LE_DEFINE_PARAMETER(FreezeTrigger, TriggerParameter);
    LE_DEFINE_PARAMETER(MeltTrigger, TriggerParameter);
    LE_DEFINE_PARAMETER(TransitionTime, LinearUnsignedInteger, Minimum<0>, Maximum<10000>,
                        Default<500>, Unit<" ms">);
    LE_DEFINE_PARAMETERS(FreezeTrigger, MeltTrigger, TransitionTime);

    /// \typedef FreezeTrigger
    /// \brief Initiates the freezing process.
    /// \typedef MeltTrigger
    /// \brief Initiates the melting process.
    /// \typedef TransitionTime
    /// \brief Transition time between two states.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Freeze UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Freeze::FreezeTrigger, "Freeze")
EFFECT_PARAMETER_NAME(Freeze::MeltTrigger, "Melt")
EFFECT_PARAMETER_NAME(Freeze::TransitionTime, "Transition")

EFFECT_PARAMETER_STREAMING_NAME(Freeze::TransitionTime, "Transition time")

} // namespace LE::SW::Effects

#endif // freeze_hpp