////////////////////////////////////////////////////////////////////////////////
///
/// \file midiNoteStatus.hpp
/// ------------------------
///
///   Which keys are down, and where every controller last stopped.
///
///   The engine's copy, and deliberately not Threading::MIDIMonitor: this one is
/// written by the event handler and read by an effect's setup(), both on the
/// audio thread, so there is no edge to synchronise across and nothing here is
/// atomic. The monitor pays for atomics because it crosses to the message
/// thread. \see doc/tech/threading_model.md and doc/tech/midi-input.md.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef midiNoteStatus_hpp__9F4C1A73_6E28_4D51_B0A9_5C7E23F81D64
#define midiNoteStatus_hpp__9F4C1A73_6E28_4D51_B0A9_5C7E23F81D64
//------------------------------------------------------------------------------
#include <array>
#include <cstdint>

namespace LE::SW::Engine
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class MIDINoteStatus
///
/// \brief What an effect that consumes notes is handed.
///
/// \note Read only where an effect can reach it. Every module in the chain is
/// handed the same one, so a module that consumed a note would take it from
/// every later slot -- the trap effect_contract.md §1.2 documents for
/// TriggerParameter::consumeValue(). An effect that needs an edge keeps the
/// previous state itself, per channel.
///
/// \note Channel is not recorded, matching the parser: every MIDI channel
/// writes the same key, so a key held on one and released on another lifts.
///
////////////////////////////////////////////////////////////////////////////////

class MIDINoteStatus
{
  public:
    static constexpr std::size_t keys{128};
    static constexpr std::size_t controllers{128};

  public: // Read only interface for effects.
    bool isDown(std::uint8_t const key) const { return velocities_[key] != 0; }

    /// \brief 0 for a key that is up, and never 0 for one that is down -- a
    /// note-on of zero velocity is a release and the parser has already turned
    /// it into one.
    float velocity(std::uint8_t const key) const { return velocities_[key] / 127.0f; }

    std::uint8_t controllerValue(std::uint8_t const controller) const
    {
        return controllers_[controller];
    }

  public: // Non-const interface for the event handler.
    /// \note 7 bit and unchecked at the call site, so checked here.
    void noteOn(std::uint8_t const key, std::uint8_t const velocity)
    {
        if (key < keys) [[likely]]
            velocities_[key] = velocity;
    }

    void noteOff(std::uint8_t const key)
    {
        if (key < keys) [[likely]]
            velocities_[key] = 0;
    }

    void controller(std::uint8_t const controller, std::uint8_t const value)
    {
        if (controller < controllers) [[likely]]
            controllers_[controller] = value;
    }

    /// \note Processor::reset() calls this, which is the host's reset and every
    /// resume: a key held when the transport stopped would otherwise still be
    /// down when it started again and the patch would come back sounding.
    void allNotesOff() { velocities_.fill(0); }

  private:
    std::array<std::uint8_t, keys> velocities_{};
    std::array<std::uint8_t, controllers> controllers_{};
}; // class MIDINoteStatus

} // namespace LE::SW::Engine

#endif // midiNoteStatus_hpp
