////////////////////////////////////////////////////////////////////////////////
///
/// \file midiInputTests.cpp
/// ------------------------
///
///   The note port, from the host's event list to the pixels.
///
///   Nothing in the DSP reads a note yet, so what these cases are about is
/// whether a note *arrives* -- the port being declared, the event being
/// recognised in both dialects, and something visible happening. That is the
/// whole of what the feature currently claims.
///
/// \note Both dialects, and both are load-bearing. A note arrives as
/// CLAP_EVENT_NOTE_ON under the CLAP dialect; a controller has no CLAP note
/// event at all and only ever arrives as a raw CLAP_EVENT_MIDI. A plugin that
/// declared one dialect would silently see half of this.
///
/// \see doc/tech/midi-input.md
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "clap/testHost.hpp"

#include "core/modules/moduleDSPAndGUI.hpp"
#include "goldens/engineHarness.hpp"
#include "gui/editor/spectrumWorxEditor.hpp"
#include "spectrumWorxCLAP.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace SWTest;

constexpr float sampleRate{48000};
constexpr std::uint32_t blockSize{512};

/// \brief One block's worth of note events, as a host hands them over.
class NoteEvents
{
  public:
    void noteOn(std::int16_t const key, double const velocity = 0.8)
    {
        note(CLAP_EVENT_NOTE_ON, key, velocity);
    }
    void noteOff(std::int16_t const key) { note(CLAP_EVENT_NOTE_OFF, key, 0.0); }
    void choke(std::int16_t const key) { note(CLAP_EVENT_NOTE_CHOKE, key, 0.0); }

    /// \brief A raw MIDI 1.0 message, which is the only way a controller arrives.
    void midi(std::uint8_t const status, std::uint8_t const first, std::uint8_t const second)
    {
        clap_event_midi event{};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.port_index = 0;
        event.data[0] = status;
        event.data[1] = first;
        event.data[2] = second;
        midis_.push_back(event);
        order_.push_back({false, midis_.size() - 1});
    }

    void controller(std::uint8_t const number, std::uint8_t const value)
    {
        midi(0xB0, number, value);
    }

    clap_input_events const *operator*() const { return &list_; }

  private:
    void note(std::uint16_t const type, std::int16_t const key, double const velocity)
    {
        clap_event_note event{};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.note_id = -1;
        event.velocity = velocity;
        notes_.push_back(event);
        order_.push_back({true, notes_.size() - 1});
    }

    struct Slot
    {
        bool isNote;
        std::size_t index;
    };

    static std::uint32_t size(clap_input_events const *const list)
    {
        return static_cast<std::uint32_t>(self(list).order_.size());
    }

    static clap_event_header const *get(clap_input_events const *const list,
                                        std::uint32_t const index)
    {
        auto const &events(self(list));
        auto const &slot(events.order_[index]);
        return slot.isNote ? &events.notes_[slot.index].header : &events.midis_[slot.index].header;
    }

    static NoteEvents const &self(clap_input_events const *const list)
    {
        return *static_cast<NoteEvents const *>(list->ctx);
    }

    /// \note Deques rather than vectors would do as well; what matters is that
    /// the headers handed out do not move, so the two stores are reserved once
    /// and the case's event count is small.
    std::vector<clap_event_note> notes_{[] {
        std::vector<clap_event_note> reserved;
        reserved.reserve(64);
        return reserved;
    }()};
    std::vector<clap_event_midi> midis_{[] {
        std::vector<clap_event_midi> reserved;
        reserved.reserve(64);
        return reserved;
    }()};
    std::vector<Slot> order_;

    clap_input_events list_{this, &NoteEvents::size, &NoteEvents::get};
}; // class NoteEvents

/// \note Through the editor's interface rather than the plugin class: the
/// monitor is what a *host of the editor* offers, and reaching it that way is
/// what proves the editor can.
LE::SW::Threading::MIDIMonitor const &monitorOf(clap_plugin const &plugin)
{
    return editorHostOf(plugin).midiMonitor();
}

/// \brief Renders one block carrying \p events.
void deliver(ActivePlugin &plugin, NoteEvents const &events)
{
    std::vector<float> leftIn(blockSize), rightIn(blockSize);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);
    plugin.process(leftIn, rightIn, leftOut, rightOut, nullptr, *events);
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("One note input port, in both dialects", "[clap][midi]")
{
    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);

    auto const *const ports(static_cast<clap_plugin_note_ports const *>(
        plugin->get_extension(&*plugin, CLAP_EXT_NOTE_PORTS)));
    REQUIRE(ports != nullptr);

    CHECK(ports->count(&*plugin, true) == 1);
    CHECK(ports->count(&*plugin, false) == 0); // nothing here makes notes

    clap_note_port_info info{};
    REQUIRE(ports->get(&*plugin, 0, true, &info));

    // MIDI is not optional here: a controller has no CLAP note event, so a port
    // that took the CLAP dialect alone would see notes and never a CC
    CHECK((info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0);
    CHECK((info.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0);
    CHECK(info.preferred_dialect == CLAP_NOTE_DIALECT_CLAP);
}

TEST_CASE("A note reaches the monitor and lifts again", "[clap][midi]")
{
    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);
    auto const &monitor(monitorOf(*plugin));

    auto const before(monitor.changes());
    CHECK_FALSE(monitor.isNoteDown(60));

    {
        NoteEvents events;
        events.noteOn(60);
        deliver(plugin, events);
    }
    CHECK(monitor.isNoteDown(60));
    CHECK(monitor.changes() > before);

    {
        NoteEvents events;
        events.noteOff(60);
        deliver(plugin, events);
    }
    CHECK_FALSE(monitor.isNoteDown(60));
}

/// \note The two arrangements that leave a key stuck if they are read as a press.
/// A choke is a voice cut short rather than released -- a track stopping
/// mid-note -- and a MIDI note-on of zero velocity is how a great many hosts and
/// keyboards spell a release.
TEST_CASE("A choke and a zero-velocity note-on both lift the key", "[clap][midi]")
{
    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);
    auto const &monitor(monitorOf(*plugin));

    {
        NoteEvents events;
        events.noteOn(64);
        events.choke(64);
        deliver(plugin, events);
    }
    CHECK_FALSE(monitor.isNoteDown(64));

    {
        NoteEvents events;
        events.midi(0x90, 67, 100); // a raw note-on
        deliver(plugin, events);
    }
    CHECK(monitor.isNoteDown(67));

    {
        NoteEvents events;
        events.midi(0x90, 67, 0); // ...and its release, spelt as a press
        deliver(plugin, events);
    }
    CHECK_FALSE(monitor.isNoteDown(67));
}

TEST_CASE("A controller arrives with its value", "[clap][midi]")
{
    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);
    auto const &monitor(monitorOf(*plugin));

    CHECK(monitor.controllerStamp(74) == 0); // never written

    {
        NoteEvents events;
        events.controller(74, 99);
        deliver(plugin, events);
    }
    CHECK(monitor.controllerValue(74) == 99);
    CHECK(monitor.controllerStamp(74) > 0);

    // any channel writes the same slot, which is what "any channel" means
    {
        NoteEvents events;
        events.midi(0xB5, 74, 12);
        deliver(plugin, events);
    }
    CHECK(monitor.controllerValue(74) == 12);
}

/// \note A parameter event and a note event travel in the same list, and
/// handleEvent() dispatches both. The failure this pins is a note swallowing the
/// parameter path or the other way round.
TEST_CASE("Notes do not disturb the parameter path", "[clap][midi]")
{
    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);
    auto const &monitor(monitorOf(*plugin));

    auto const slot(parameterID(moduleChainType, 0));
    OneParameterEvent const fillSlot(slot, SWTest::effectByStreamingName("Colorifer"));
    plugin.flush(&*fillSlot);

    {
        NoteEvents events;
        events.noteOn(72);
        deliver(plugin, events);
    }

    CHECK(monitor.isNoteDown(72));
    // the slot still holds what the parameter put there
    double value{0};
    REQUIRE(parameters(*plugin).get_value(&*plugin, slot, &value));
    CHECK(value == SWTest::effectByStreamingName("Colorifer"));
}
#ifdef SW_MIDI_OVERLAY

////////////////////////////////////////////////////////////////////////////////
///
/// \note The visible half. `pumpMIDIMonitor()` is the editor's clock, ticked
/// here rather than waited on -- the window is counted in timer ticks precisely
/// so that running it out does not mean sleeping for two seconds.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The editor shows arriving MIDI and stops", "[clap][midi][gui]")
{
    Entry const entry;
    TestHost host{{.threadCheck = true, .log = true, .gui = true}};
    ActivePlugin plugin(sampleRate, blockSize, host);

    auto const *const gui(
        static_cast<clap_plugin_gui const *>(plugin->get_extension(&*plugin, CLAP_EXT_GUI)));
    REQUIRE(gui != nullptr);
    REQUIRE(gui->create(&*plugin, CLAP_WINDOW_API_COCOA, false));

    auto *const pHelper(static_cast<LE::SW::PluginHelper *>(plugin->plugin_data));
    REQUIRE(pHelper != nullptr);
    auto &editor(*static_cast<LE::SW::SpectrumWorxCLAP *>(pHelper)->gui());

    auto const render([&editor] {
        juce::Image image(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
        juce::Graphics graphics(image);
        editor.paintEntireComponent(graphics, true);
        return image;
    });

    auto const differing([](juce::Image const &left, juce::Image const &right) {
        juce::Image::BitmapData const a(left, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData const b(right, juce::Image::BitmapData::readOnly);
        std::size_t count{0};
        for (int y(0); y < left.getHeight(); ++y)
            for (int x(0); x < left.getWidth(); ++x)
                count += (a.getPixelColour(x, y) != b.getPixelColour(x, y));
        return count;
    });

    // nothing has arrived, so nothing is drawn and nothing asks to be
    CHECK_FALSE(editor.pumpMIDIMonitor());
    CHECK_FALSE(editor.isShowingMIDI());
    auto const quiet(render());

    {
        NoteEvents events;
        events.noteOn(60);
        events.controller(74, 99);
        deliver(plugin, events);
    }

    CHECK(editor.pumpMIDIMonitor());
    CHECK(editor.isShowingMIDI());
    CHECK(differing(quiet, render()) > 0);

    ////////////////////////////////////////////////////////////////////////////
    /// \note One tick short of the window, then the one that closes it. A
    /// display that never expired and one that expired immediately both pass a
    /// case that only looks at the end.
    ////////////////////////////////////////////////////////////////////////////
    for (int tick(0); tick < (2 * 30) - 1; ++tick)
        CHECK_FALSE(editor.pumpMIDIMonitor());
    CHECK(editor.isShowingMIDI());

    CHECK(editor.pumpMIDIMonitor()); // the tick that closes it
    CHECK_FALSE(editor.isShowingMIDI());
    CHECK(differing(quiet, render()) == 0);

    gui->destroy(&*plugin);
}

#else

////////////////////////////////////////////////////////////////////////////////
///
/// \note The overlay is off, so there is no visible half to drive. The monitor
/// itself is still asserted above -- it is the seam, not the drawing, that the
/// note port has to reach. \see SW_MIDI_OVERLAY in spectrumWorxEditor.hpp.
///
////////////////////////////////////////////////////////////////////////////////

#endif // SW_MIDI_OVERLAY
