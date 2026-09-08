////////////////////////////////////////////////////////////////////////////////
///
/// \file midiMonitor.hpp
/// ---------------------
///
///   What arrived on the note port, for the interface to draw.
///
///   A monitor rather than a channel: nothing downstream of this makes a sound,
/// and the engine does not read it. It exists to answer "is MIDI routable" with
/// something visible, and it is the shape the eventual note handling will be
/// wired behind. \see doc/tech/midi-input.md.
///
/// \note **Deliberately not a snapshot.** Each slot is its own atomic, so a
/// reader can land between the two writes of a block that moved two controllers
/// and see one of them. That is fine for something drawn at 30 Hz and would not
/// be fine for anything an engine acted on -- which is why this is separate from
/// the ring in spscQueue.hpp, where order and delivery are guaranteed and paid
/// for. See threading_model.md §3 for why the two exist.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef midiMonitor_hpp__2C7F4A19_5D83_4E60_9B21_7A3E0C85F6D4
#define midiMonitor_hpp__2C7F4A19_5D83_4E60_9B21_7A3E0C85F6D4
//------------------------------------------------------------------------------
#include <array>
#include <atomic>
#include <cstdint>

namespace LE::SW::Threading
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class MIDIMonitor
///
/// \brief Which keys are down and which controllers have moved.
///
/// \note Channel is not recorded, on purpose. Every channel writes the same
/// slot, so a key held on channel 1 and released on channel 2 lifts -- which is
/// what "any channel" means and is the whole of the routing question this
/// answers.
///
////////////////////////////////////////////////////////////////////////////////

class MIDIMonitor
{
  public:
    static constexpr std::size_t slots{128};

    /// \brief Audio thread. \p key and \p controller are 7 bit and are not
    /// checked by the caller, so they are checked here.
    void noteOn(std::uint8_t const key) { setNote(key, true); }
    void noteOff(std::uint8_t const key) { setNote(key, false); }

    void controller(std::uint8_t const controller, std::uint8_t const value)
    {
        if (controller >= slots) [[unlikely]]
            return;
        controllerValues_[controller].store(value, std::memory_order_relaxed);
        /// \note The stamp the change counter is *about* to take, so that a
        /// reader comparing a stamp against a window it derived from the counter
        /// sees this controller inside it rather than one tick before it.
        controllerStamps_[controller].store(changes_.load(std::memory_order_relaxed) + 1,
                                            std::memory_order_relaxed);
        bumpChanges();
    }

    /// \brief Interface thread. Anything but equal to the last value read means
    /// something arrived; the magnitude of the difference is not a count of
    /// events and is not meant to be.
    std::uint32_t changes() const { return changes_.load(std::memory_order_acquire); }

    bool isNoteDown(std::size_t const key) const
    {
        return (key < slots) && notes_[key].load(std::memory_order_relaxed);
    }

    /// \note Zero for a controller that has never been written, which is also a
    /// real value -- `controllerStamp()` is what tells the two apart.
    std::uint8_t controllerValue(std::size_t const controller) const
    {
        return (controller < slots) ? controllerValues_[controller].load(std::memory_order_relaxed)
                                    : std::uint8_t{0};
    }

    /// \brief The value of `changes()` when this controller was last written, 0
    /// for never. \see controller()
    std::uint32_t controllerStamp(std::size_t const controller) const
    {
        return (controller < slots) ? controllerStamps_[controller].load(std::memory_order_relaxed)
                                    : 0u;
    }

  private:
    void setNote(std::uint8_t const key, bool const down)
    {
        if (key >= slots) [[unlikely]]
            return;
        notes_[key].store(down, std::memory_order_relaxed);
        bumpChanges();
    }

    /// \note Release, and acquire in changes(), so a reader that sees the count
    /// move also sees the slot that moved it.
    void bumpChanges() { changes_.fetch_add(1, std::memory_order_release); }

    std::array<std::atomic<bool>, slots> notes_{};
    std::array<std::atomic<std::uint8_t>, slots> controllerValues_{};
    std::array<std::atomic<std::uint32_t>, slots> controllerStamps_{};
    std::atomic<std::uint32_t> changes_{0};
}; // class MIDIMonitor

} // namespace LE::SW::Threading

//------------------------------------------------------------------------------
#endif // midiMonitor_hpp
