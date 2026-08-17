////////////////////////////////////////////////////////////////////////////////
///
/// \file effectNames.cpp
/// ---------------
///
/// Effect index <-> display name.
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "allEffects.hpp"
#include "effectNames.hpp"
#include "effectsList.hpp"

#include "constants.hpp"
#include "le/utility/platformSpecifics.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include <cstdint>

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
// Titles that have moved since a preset named them.
//
// \note An entry here means an effect's title changed after files had been
// written naming the old one; the pin is what keeps those files loading. Nothing
// else may be added -- a *new* effect has no history and its title is its
// streaming name.
//
// \note The nine below are the phase-vocoder variants, retitled from "(pvd)" to
// "(PV)" for issue #80. Every one of the 303 factory presets and every user file
// written before that names them the old way, so the old spelling is what stays
// in the file. \see doc/tech/streaming_format.md.
//                                            (16.08.2026.)
////////////////////////////////////////////////////////////////////////////////

LE_SW_EFFECT_STREAMING_NAME(PhaseVocoderAnalysis, "PVD start")
LE_SW_EFFECT_STREAMING_NAME(PhaseVocoderSynthesis, "PVD stop")
LE_SW_EFFECT_STREAMING_NAME(PVPitchShifter, "Pitch Shifter (pvd)")
LE_SW_EFFECT_STREAMING_NAME(PitchFollowerPVD, "Pitch Follower (pvd)")
LE_SW_EFFECT_STREAMING_NAME(TuneWorxPVD, "TuneWorx (pvd)")
LE_SW_EFFECT_STREAMING_NAME(PitchMagnetPVD, "Pitch Magnet (pvd)")
LE_SW_EFFECT_STREAMING_NAME(PitchSpringPVD, "Pitch Spring (pvd)")
LE_SW_EFFECT_STREAMING_NAME(PVImploder, "Imploder (pvd)")
LE_SW_EFFECT_STREAMING_NAME(PVExploder, "Exploder (pvd)")

namespace
{
using EffectNames = std::array<char const *LE_RESTRICT const, Constants::numberOfEffects>;

#define LE_SW_AUX_EFFECT_TITLE(folder, module, name, group) name::title,
EffectNames const effectNames = {{LE_SW_EFFECT_LIST(LE_SW_AUX_EFFECT_TITLE)}};
#undef LE_SW_AUX_EFFECT_TITLE

#define LE_SW_AUX_EFFECT_STREAMING_NAME(folder, module, name, group)                               \
    EffectStreamingName<name>::string_,
EffectNames const effectStreamingNames = {{LE_SW_EFFECT_LIST(LE_SW_AUX_EFFECT_STREAMING_NAME)}};
#undef LE_SW_AUX_EFFECT_STREAMING_NAME
} // anonymous namespace

char const *effectName(std::uint8_t const effectIndex) { return effectNames[effectIndex]; }

std::int8_t effectIndex(std::string_view const effectName)
{
    auto const pFoundEffectName(std::ranges::find(effectNames, effectName));
    if (pFoundEffectName == effectNames.end())
        return -1;
    return static_cast<std::int8_t>(pFoundEffectName - effectNames.begin());
}

char const *effectStreamingName(std::uint8_t const effectIndex)
{
    return effectStreamingNames[effectIndex];
}

std::int8_t effectIndexFromStreamingName(std::string_view const streamingName)
{
    auto const pFound(std::ranges::find(effectStreamingNames, streamingName));
    if (pFound == effectStreamingNames.end())
        return -1;
    return static_cast<std::int8_t>(pFound - effectStreamingNames.begin());
}

} // namespace LE::SW::Effects
