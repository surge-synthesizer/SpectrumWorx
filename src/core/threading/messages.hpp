////////////////////////////////////////////////////////////////////////////////
///
/// \file messages.hpp
/// ------------------
///
///   What crosses between the main thread and the audio thread, in both
/// directions, as values.
///
///   Every message is trivially copyable and owns nothing: a pointer in one is a
/// *transfer*, and which side is responsible after it lands is written on each
/// case below. That is the whole of the memory management -- there is no shared
/// ownership anywhere in the protocol, and nothing is deleted on the audio thread.
///
/// See doc/tech/threading_model.md §3 and §5.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef messages_hpp__2F86A1D4_7E30_4B9C_A5D2_6C0F31E8B7A5
#define messages_hpp__2F86A1D4_7E30_4B9C_A5D2_6C0F31E8B7A5
//------------------------------------------------------------------------------
#include "core/threading/spscQueue.hpp"

#include <cstdint>

namespace LE::SW::Threading
{

////////////////////////////////////////////////////////////////////////////////
///
/// \struct ToEngine
///
/// \brief Main thread -> audio thread. Drained at the top of process().
///
////////////////////////////////////////////////////////////////////////////////

struct ToEngine
{
    enum struct Kind : std::uint8_t
    {
        None,
        /// A parameter's **base** value, in the parameter's own units, snapped by
        /// the sender -- snapping is a pure function of the static ParameterInfo,
        /// so the interface can do it without waiting for an answer.
        SetBaseParameter,
        /// Which effect a slot holds. The module travels *with* the message,
        /// already built and sized by the main thread; the audio thread only
        /// relinks. See the note on `setSlot` below.
        SetSlot,
        /// Two modules trading places, by slot index. Pure relinking, so nothing
        /// travels and nothing comes back.
        MoveModule,
        /// A whole module chain, built on the main thread. The audio thread
        /// exchanges it with the live one and hands the same object back as
        /// ToUI::Retire, now holding what used to be live -- so nothing is ever
        /// destroyed under the callback.
        SwapChain,
        /// Likewise a decoded Sample -- and, in the same message, what the side
        /// channel is to be fed from. The two travel together because they are
        /// one user act ("play this file", "take the host's port") and because a
        /// block that saw one without the other would be a block fed from the
        /// wrong place. \see SideChainSource.
        SwapSample,
        /// An LFO sub-parameter that has no `ParameterID` -- Waveform and
        /// SyncTypes, both past `ParameterCounts::lfoExportedParameters`.
        /// Addressed by index because there is no identifier to address it by;
        /// everything else the interface sends is a `SetBaseParameter`.
        SetUnexportedLFOParameter
    };

    Kind kind{Kind::None};

    union
    {
        struct
        {
            std::uint32_t parameterID; ///< packed, i.e. SW::ParameterID::binaryValue
            float value;
        } setBaseParameter;

        /// \note `pModule` carries one reference, transferred. Null means empty
        /// the slot, which is also what `effectIndex == -1` says -- the index is
        /// there so the event coming back can name it without dereferencing
        /// anything.
        struct
        {
            void *pModule;
            std::uint8_t slot;
            std::int8_t effectIndex;
        } setSlot;

        struct
        {
            std::uint8_t from;
            std::uint8_t to;
        } moveModule;

        /// \note `void *` rather than the real type: this header is the protocol
        /// and has no business including the engine. The two sites that put a
        /// pointer in and take it out are one file apart and both static_cast.
        struct
        {
            void *pChain;
        } swapChain;

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \note `replacesSample` distinguishes "here is a new sample, or none"
        /// from "keep the one you have, only the source has changed". Selecting
        /// `Main` or `Host` sends the first, with a null pointer, because those
        /// discard a loaded file; restoring a patch that names one sends the
        /// second, because the sample it refers to has just been published and
        /// must not be cleared by the source arriving behind it.
        ///
        /// \note `source` is `std::uint8_t` rather than the enum for the reason
        /// the note above `swapChain` gives: this header is the protocol and has
        /// no business including anything. The two sites that put a value in and
        /// take it out are one file apart.
        ///
        ////////////////////////////////////////////////////////////////////////
        struct
        {
            void *pSample;
            bool replacesSample;
            std::uint8_t source;
        } swapSample;

        /// \note The value in the parameter's own units, as a float, which is
        /// what the interface already converts to before it stores one.
        struct
        {
            float value;
            std::uint8_t moduleIndex;
            std::uint8_t moduleParameterIndex;
            std::uint8_t lfoParameterIndex;
        } setUnexportedLFOParameter;
    };
}; // struct ToEngine

////////////////////////////////////////////////////////////////////////////////
///
/// \struct ToUI
///
/// \brief Audio thread -> main thread. Drained on the main thread.
///
/// \note Not the modulated value of a parameter: that is a signal at block rate
/// and goes through the ValueMailbox. What is here is what has *happened*.
///
////////////////////////////////////////////////////////////////////////////////

struct ToUI
{
    enum struct Kind : std::uint8_t
    {
        None,
        /// A base value the engine settled on, after snapping or after a host
        /// automation event. Latchable: the model takes it and the host is told.
        BaseParameterChanged,
        /// The chain changed shape -- a slot filled or emptied, two modules
        /// swapped, a whole chain installed. The interface makes its rack a
        /// function of the chain again; see SpectrumWorxEditor::resyncModuleRack().
        ChainChanged,
        /// The host's bar duration or meter moved, so a synced LFO's period is a
        /// different number of seconds and its snap grid a different grid. The
        /// interface redraws the LFO panel; see
        /// SpectrumWorxEditor::updateForNewTimingInfo().
        ///
        /// \note Carries nothing: what changed is engine state the main thread
        /// can read for itself, and the message is only the news that it did.
        /// **Coalesced by the sender**, unlike everything else here -- a host
        /// ramping the tempo moves it on every block, and this ring also carries
        /// the retirements, where a drop is a leak rather than a stale reading.
        /// \see SpectrumWorxCLAP::timingChanged().
        TimingChanged,
        /// Something the audio thread unlinked and the main thread must delete.
        /// Dropping one of these is a leak, which is why this is a ring and not a
        /// mailbox.
        Retire
    };

    /// What a Retire is carrying, since the main thread has to know what to call.
    ///
    /// \note `Module` is a reference to release, not an object to `delete`: the
    /// chain's modules are intrusively counted and the interface may still hold
    /// one of its own.
    enum struct Retired : std::uint8_t
    {
        None,
        Module,
        Chain,
        Sample
    };

    Kind kind{Kind::None};

    union
    {
        struct
        {
            std::uint32_t parameterID;
            float value;
        } baseParameterChanged;

        struct
        {
            Retired what;
            void *pObject;
        } retire;
    };
}; // struct ToUI

////////////////////////////////////////////////////////////////////////////////
// The two rings.
//
// \note 1024 each, which is what UIEdits carried and what a block's worth of host
// automation has never come close to. They refuse rather than overwrite when full;
// see spscQueue.hpp for why that is the right way round for commands.
////////////////////////////////////////////////////////////////////////////////

using ToEngineQueue = SPSCQueue<ToEngine, 1024>;
using ToUIQueue = SPSCQueue<ToUI, 1024>;

////////////////////////////////////////////////////////////////////////////////
// Makers.
//
// \note Because a union member cannot be initialised positionally without saying
// which one, and `message.setSlot = {3, 17}` at a call site says nothing about
// what kind of message it is. These do.
////////////////////////////////////////////////////////////////////////////////

/// \param parameterID packed, i.e. `SW::ParameterID::binaryValue`.
/// \param value in the parameter's own units, not normalised.
inline ToEngine setBaseParameter(std::uint32_t const parameterID, float const value)
{
    ToEngine message{};
    message.kind = ToEngine::Kind::SetBaseParameter;
    message.setBaseParameter = {parameterID, value};
    return message;
}

/// \param pModule an `SW::Module *`, one reference, transferred; null empties.
inline ToEngine setSlot(std::uint8_t const slot, std::int8_t const effectIndex, void *const pModule)
{
    ToEngine message{};
    message.kind = ToEngine::Kind::SetSlot;
    message.setSlot = {pModule, slot, effectIndex};
    return message;
}

inline ToEngine moveModule(std::uint8_t const from, std::uint8_t const to)
{
    ToEngine message{};
    message.kind = ToEngine::Kind::MoveModule;
    message.moveModule = {from, to};
    return message;
}

/// \param pChain an `SW::AutomatedModuleChain *`, owned by the message.
inline ToEngine swapChain(void *const pChain)
{
    ToEngine message{};
    message.kind = ToEngine::Kind::SwapChain;
    message.swapChain = {pChain};
    return message;
}

/// \param pSample an `LE::Sample *`, owned by the message.
inline ToEngine swapSample(void *const pSample, bool const replacesSample,
                           std::uint8_t const source)
{
    ToEngine message{};
    message.kind = ToEngine::Kind::SwapSample;
    message.swapSample = {pSample, replacesSample, source};
    return message;
}

inline ToEngine setUnexportedLFOParameter(std::uint8_t const moduleIndex,
                                          std::uint8_t const moduleParameterIndex,
                                          std::uint8_t const lfoParameterIndex, float const value)
{
    ToEngine message{};
    message.kind = ToEngine::Kind::SetUnexportedLFOParameter;
    message.setUnexportedLFOParameter = {value, moduleIndex, moduleParameterIndex,
                                         lfoParameterIndex};
    return message;
}

inline ToUI baseParameterChanged(std::uint32_t const parameterID, float const value)
{
    ToUI message{};
    message.kind = ToUI::Kind::BaseParameterChanged;
    message.baseParameterChanged = {parameterID, value};
    return message;
}

inline ToUI chainChanged()
{
    ToUI message{};
    message.kind = ToUI::Kind::ChainChanged;
    return message;
}

inline ToUI timingChanged()
{
    ToUI message{};
    message.kind = ToUI::Kind::TimingChanged;
    return message;
}

inline ToUI retire(ToUI::Retired const what, void *const pObject)
{
    ToUI message{};
    message.kind = ToUI::Kind::Retire;
    message.retire = {what, pObject};
    return message;
}

} // namespace LE::SW::Threading

#endif // messages_hpp
