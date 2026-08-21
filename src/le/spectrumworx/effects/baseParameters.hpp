////////////////////////////////////////////////////////////////////////////////
///
/// \file baseParameters.hpp
/// ------------------------
///
/// Base parameters included by all effects.
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef baseParameters_hpp__8696E957_A8FB_469A_9725_CB6F63216163
#define baseParameters_hpp__8696E957_A8FB_469A_9725_CB6F63216163
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "parameters.hpp"
#include "le/parameters/factoryMacro.hpp"
#include "le/parameters/parameter.hpp"
#include "le/parameters/boolean/parameter.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/symmetric/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Engine
{
class Setup;
} // namespace LE::SW::Engine

/// \addtogroup Effects
/// @{

namespace LE::SW::Effects
{

/// \addtogroup Effects
/// @{

/// \brief Basic parameters included by all effects
namespace BaseParameters
{
/// \name Base parameters
/// \brief Basic parameters included by all effects
/// @{

LE_DEFINE_PARAMETER(Bypass, Boolean);
LE_DEFINE_PARAMETER(Gain, SymmetricFloat, MaximumOffset<20>, Unit<" dB">);
LE_DEFINE_PARAMETER(Wet, LinearFloat, Minimum<0>, Maximum<100>, Default<100>, Unit<" %">);
LE_DEFINE_PARAMETER(StartFrequency, LinearFloat, Minimum<0>, Maximum<1>, Default<0>);
LE_DEFINE_PARAMETER(StopFrequency, LinearFloat, Minimum<0>, Maximum<1>, Default<1>);
LE_DEFINE_PARAMETERS(Bypass, Gain, Wet, StartFrequency, StopFrequency);

/// \typedef Parameters
/// \brief Basic parameters shared/included by all effects.
///
/// - Bypass: Enables/disables the effect.
///
/// - Gain: Per effect attenuation or amplification, range [-20 dB, +20 dB]
///
/// - Wet: Dry/Wet blending between original and effected signal.
///
/// - StartFrequency: Normalised frequency value (range 0.0 to 1.0) representing
///                   starting frequency for the start-stop range where
///                   the effect will perform its action.
///
/// - StopFrequency: Normalised frequency value (range 0.0 to 1.0) representing
///                  ending frequency for the start-stop range where
///                  the effect will perform its action. Frequencies
///                  outside the range will remain unaffected.
///

/// @}
} // namespace BaseParameters
/// @}

////////////////////////////////////////////////////////////////////////////////
//
// BaseParameters UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(BaseParameters::Bypass, "Bypass")
EFFECT_PARAMETER_NAME(BaseParameters::Gain, "Gain")
EFFECT_PARAMETER_NAME(BaseParameters::Wet, "Wet")
EFFECT_PARAMETER_NAME(BaseParameters::StartFrequency, "Start Frequency")
EFFECT_PARAMETER_NAME(BaseParameters::StopFrequency, "Stop Frequency")

EFFECT_PARAMETER_STREAMING_NAME(BaseParameters::StartFrequency, "Start frequency")
EFFECT_PARAMETER_STREAMING_NAME(BaseParameters::StopFrequency, "Stop frequency")

} // namespace LE::SW::Effects
/// @}

namespace LE::Parameters
{

/// \note Here, and not in a .cpp beside transform()'s definition, for the reason
/// UI_NAME gives: a translation unit that builds the parameter table without
/// seeing this specialisation gets the primary template instead -- the identity
/// transform and no unit -- and nothing says so. Only transform() is out of
/// line, because normalisedFrequencyToHz() is the engine's and this is the
/// parameter layer.
template <> struct DisplayValueTransformer<SW::Effects::BaseParameters::StartFrequency>
{
    static float transform(float const &value, SW::Engine::Setup const &);
    static float inverse(float hz, SW::Engine::Setup const &);

    using Suffix = UnitString<" Hz">;
};

template <>
struct DisplayValueTransformer<SW::Effects::BaseParameters::StopFrequency>
    : DisplayValueTransformer<SW::Effects::BaseParameters::StartFrequency>
{
};

} // namespace LE::Parameters

#endif // baseParameters_hpp
