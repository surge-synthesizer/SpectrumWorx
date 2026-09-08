////////////////////////////////////////////////////////////////////////////////
///
/// \file consumesMIDI.hpp
/// ----------------------
///
///   Whether an effect declared that it consumes notes.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef consumesMIDI_hpp__4B7E2D80_1A96_4F35_9C4E_86D0B7F2A519
#define consumesMIDI_hpp__4B7E2D80_1A96_4F35_9C4E_86D0B7F2A519
//------------------------------------------------------------------------------
#include "indexRange.hpp"

#include "le/spectrumworx/engine/midiNoteStatus.hpp"
#include "le/spectrumworx/engine/setup.hpp"

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Whether \p Effect takes the optional third setup() parameter.
///
/// \note The signature is the declaration, and there is no second place to say
/// so -- the rule effect_contract.md §1.8 arrived at for the side chain, where a
/// `usesSideChannel` constant spent ten years naming seven effects the engine
/// treated as fifteen. A constant that has to agree with an overload signature
/// is a second answer to a question that already has one.
///
/// \note The two forms are mutually exclusive: a two-argument setup() is not
/// invocable with three and the reverse, so a mistyped third parameter fails to
/// compile rather than quietly selecting the noteless form and never running.
///
////////////////////////////////////////////////////////////////////////////////

template <class Effect>
concept ConsumesMIDI =
    requires(Effect &effect, IndexRange const &workingRange, Engine::Setup const &engineSetup,
             Engine::MIDINoteStatus const &midiNotes) {
        effect.setup(workingRange, engineSetup, midiNotes);
    };

} // namespace LE::SW::Effects

#endif // consumesMIDI_hpp
