////////////////////////////////////////////////////////////////////////////////
///
/// \file ahAh.hpp
/// --------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef ahah_hpp__D12349D97_2DB9_4249_87EF_851D727FAF71
#define ahah_hpp__D12349D97_2DB9_4249_87EF_851D727FAF71
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/symmetric/parameter.hpp"
#include "le/parameters/uiElements.hpp" // EFFECT_PARAMETER_STREAMING_NAME, below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class AhAh
///
/// \ingroup Effects
///
/// \brief Wah wah.
///
/// A kind of a wah-wah. It filters a region with a variable centre frequency.
/// An LFO must be used to modulate the Center frequency knob for the intended
/// effect.
///
////////////////////////////////////////////////////////////////////////////////

struct AhAh
{
    LE_DEFINE_PARAMETER(Center, LinearUnsignedInteger, Minimum<0>, Maximum<6000>, Default<2000>,
                        Unit<" Hz">);
    LE_DEFINE_PARAMETER(Width, LinearUnsignedInteger, Minimum<50>, Maximum<2000>, Default<1000>,
                        Unit<" Hz">);
    LE_DEFINE_PARAMETER(Strength, SymmetricFloat, MaximumOffset<24>, Unit<" dB">);
    LE_DEFINE_PARAMETERS(Center, Width, Strength);

    /// \typedef Center
    /// \brief Filter center frequency.
    /// \typedef Width
    /// \brief Bandwidth of the filter.
    /// \typedef Strength
    /// \brief Amplification of the moving region.

    static char const title[];
    static char const description[];
}; // struct AhAh

////////////////////////////////////////////////////////////////////////////////
///
/// \note The one parameter in the plugin whose display name and streaming name
/// differ, and deliberately so -- it is the worked example for the mechanism.
/// "Center (LFO me!)" is a 2011 instruction to the user wearing a parameter
/// name; it is also what the shipped presets and every user preset since
/// call this knob, and `<Center_(LFO_me!)>` is one of the element names
/// repairLegacyElementNames() exists to make parseable at all. So the label
/// moved and the key did not.
///
///   What proves the pin works: streamingNames.txt does not move across this
/// rename, and neither does presetFixtures.txt. Only parameterTable.txt does,
/// which is the display side.
///
/// \note This was the first of the two to be written here rather than in
/// ahAhImpl.cpp, and the argument for it -- a specialisation has to be visible
/// wherever `Detail::info<>()` builds the parameter table -- turned out to be
/// the argument for the display name as well. Both are here now; see UI_NAME.
///
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_STREAMING_NAME(AhAh::Center, "Center (LFO me!)")

////////////////////////////////////////////////////////////////////////////////
//
// AhAh UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(AhAh::Center, "Center")
EFFECT_PARAMETER_NAME(AhAh::Width, "Width")
EFFECT_PARAMETER_NAME(AhAh::Strength, "Strength")

} // namespace LE::SW::Effects

#endif // ahAh_hpp
