////////////////////////////////////////////////////////////////////////////////
///
/// \file midiConsumingEffects.hpp
/// ------------------------------
///
///   Which effects consume notes, by index, derived from the effects themselves.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef midiConsumingEffects_hpp__7D51E9C2_A308_4B76_88F1_3E6C0D95B4A7
#define midiConsumingEffects_hpp__7D51E9C2_A308_4B76_88F1_3E6C0D95B4A7
//------------------------------------------------------------------------------
#include "allEffectImpls.hpp"
#include "constants.hpp"
#include "effectsList.hpp"

#include "le/spectrumworx/effects/consumesMIDI.hpp"

#include <array>
#include <cstdint>

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Whether the effect at each index takes the note status, indexed the
/// way presets and automation index effects.
///
/// \note Derived from the effect implementations rather than declared beside
/// them, so it cannot fall out of step with what the engine does -- which is
/// what a `usesSideChannel` constant did for ten years. \see ConsumesMIDI.
///
/// \note An effect that consumes notes still loads, runs and streams where no
/// note can reach it; it simply hears nothing, as a side-chain effect does with
/// nothing plugged in. This says what an effect reads, not where it may be used.
///
////////////////////////////////////////////////////////////////////////////////

#define LE_SW_AUX_CONSUMES_MIDI(folder, module, name) ConsumesMIDI<name##Impl>,
inline constexpr std::array<bool, Constants::numberOfEffects> effectConsumesMIDI{
    LE_SW_EFFECT_LIST(LE_SW_AUX_CONSUMES_MIDI)};
#undef LE_SW_AUX_CONSUMES_MIDI

/// \brief How many of them there are. Zero is a legal answer.
inline constexpr std::uint8_t numberOfMIDIConsumingEffects()
{
    std::uint8_t count{0};
    for (bool const consumes : effectConsumesMIDI)
        count = static_cast<std::uint8_t>(count + (consumes ? 1 : 0));
    return count;
}

} // namespace LE::SW::Effects

#endif // midiConsumingEffects_hpp
