////////////////////////////////////////////////////////////////////////////////
///
/// \file stateTests.cpp
/// --------------------
///
///   `clap_plugin_state`, which until 02.08.2026 had no test of any kind --
/// `grep -r CLAP_EXT_STATE tests/` returned nothing, on the one surface every
/// user of every format meets every time they reopen a project.
///
///   A `clap_ostream` and a `clap_istream` over a `std::vector<char>` are about
///   thirty lines, and everything here is a mutation of them: what the plugin
/// writes, what it does with what it is given, and what it does with what no
/// sane host would give it.
///
///   Two things are reached through `clap_plugin::plugin_data` rather than the C
/// API -- the loaded sample and the editor pointer -- because neither has a C
/// entry point and both are exactly what this change is about. The precedent is
/// processLockTests.cpp, which does the same to hold a lock a host cannot name.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "spectrumWorxCLAP.hpp"
#include "swClapEntryImpl.hpp"

#include "core/automatedModuleChain.hpp"
#include "core/parameterID.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/presetStorage.hpp" // savePreset, maximumPresetSize

/// \note For ScopedProblemCounter, which swaps the default preset-problem
/// reporter -- a `juce::AlertWindow` per problem -- for a counter. Without it a
/// 2011 preset loaded here raises one message box per parameter its effect grew
/// later, in a process with no message thread, and leaks an AsyncUpdater for
/// each. See the note on the class.
#include "presets/presetHarness.hpp"

/// \note For `ActivePlugin` and `OneParameterEvent` -- the local `Plugin` below
/// never runs a block, and one case here has to write a parameter the way the
/// audio thread does.
#include "testHost.hpp"

#include <clap/clap.h>

#include <juce_core/juce_core.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

constexpr std::uint32_t blockSize{256};
constexpr double sampleRate{48000};

////////////////////////////////////////////////////////////////////////////////
// The streams
////////////////////////////////////////////////////////////////////////////////

/// \brief A `clap_ostream` over a growable buffer.
///
/// \param chunk the most it will accept per call, so that a plugin which assumes
/// its whole write lands in one go is caught. `writeFully()` loops; something
/// that did not, would not.
class OutStream
{
  public:
    explicit OutStream(std::int64_t const chunk = 0) : chunk_(chunk)
    {
        stream_.ctx = this;
        stream_.write = [](clap_ostream const *const stream, void const *const buffer,
                           std::uint64_t const size) -> std::int64_t {
            auto &self(*static_cast<OutStream *>(stream->ctx));
            if (self.failAfter_ >= 0 && self.written_ >= self.failAfter_)
                return -1;
            auto const accepted(
                self.chunk_ > 0 ? std::min<std::uint64_t>(size, std::uint64_t(self.chunk_)) : size);
            auto const *const bytes(static_cast<char const *>(buffer));
            self.data_.insert(self.data_.end(), bytes, bytes + accepted);
            self.written_ += std::int64_t(accepted);
            return std::int64_t(accepted);
        };
    }

    OutStream(OutStream const &) = delete; // holds a pointer to itself
    OutStream &operator=(OutStream const &) = delete;

    clap_ostream const *operator&() const { return &stream_; }

    /// After this many bytes, every write reports an error.
    void failAfter(std::int64_t const bytes) { failAfter_ = bytes; }

    std::vector<char> const &data() const { return data_; }

    /// What was written, as a string -- the trailing NUL the plugin writes is
    /// not part of it.
    std::string text() const
    {
        if (data_.empty())
            return {};
        return std::string(data_.data(), data_.size() - (data_.back() == '\0' ? 1 : 0));
    }

  private:
    clap_ostream stream_{};
    std::vector<char> data_;
    std::int64_t chunk_;
    std::int64_t written_{0};
    std::int64_t failAfter_{-1};
}; // class OutStream

/// \brief A `clap_istream` over a fixed buffer.
///
/// \param chunk the most it will hand back per call. A host is free to answer
/// one byte at a time and CLAP says so, so this is not a hypothetical.
class InStream
{
  public:
    explicit InStream(std::vector<char> data, std::int64_t const chunk = 0)
        : data_(std::move(data)), chunk_(chunk)
    {
        stream_.ctx = this;
        stream_.read = [](clap_istream const *const stream, void *const buffer,
                          std::uint64_t const size) -> std::int64_t {
            auto &self(*static_cast<InStream *>(stream->ctx));
            if (self.fail_)
                return -1;
            auto available(self.data_.size() - self.read_);
            if (available == 0)
                return 0;
            auto const wanted(
                self.chunk_ > 0 ? std::min<std::uint64_t>(size, std::uint64_t(self.chunk_)) : size);
            auto const handed(std::min<std::uint64_t>(wanted, available));
            std::memcpy(buffer, self.data_.data() + self.read_, handed);
            self.read_ += handed;
            return std::int64_t(handed);
        };
    }

    explicit InStream(std::string const &text, std::int64_t const chunk = 0)
        : InStream(std::vector<char>(text.begin(), text.end()), chunk)
    {
    }

    InStream(InStream const &) = delete;
    InStream &operator=(InStream const &) = delete;

    clap_istream const *operator&() const { return &stream_; }

    /// Reports an error on the first read, which is not the same as being empty.
    void fail() { fail_ = true; }

  private:
    clap_istream stream_{};
    std::vector<char> data_;
    std::size_t read_{0};
    std::int64_t chunk_;
    bool fail_{false};
}; // class InStream

////////////////////////////////////////////////////////////////////////////////
// The plugin
////////////////////////////////////////////////////////////////////////////////

clap_host const &nullHost()
{
    static clap_host host{CLAP_VERSION,
                          nullptr,
                          "sw-tests",
                          "SpectrumWorx",
                          "",
                          "0",
                          [](clap_host const *, char const *) -> void const * { return nullptr; },
                          [](clap_host const *) {},
                          [](clap_host const *) {},
                          [](clap_host const *) {}};
    return host;
}

/// `clap_plugin_state` off any plugin, for the cases that drive `SWTest`'s
/// harness rather than the local one. \see Plugin::state()
clap_plugin_state const &stateOf(clap_plugin const &plugin)
{
    auto const *const pState(
        static_cast<clap_plugin_state const *>(plugin.get_extension(&plugin, CLAP_EXT_STATE)));
    REQUIRE(pState != nullptr);
    return *pState;
}

clap_plugin_factory const &factory()
{
    auto const *const pFactory(static_cast<clap_plugin_factory const *>(
        LE::SW::ClapFirst::getFactory(CLAP_PLUGIN_FACTORY_ID)));
    REQUIRE(pFactory != nullptr);
    return *pFactory;
}

/// \brief RAII around the refcounted entry point. \see pluginTests.cpp
class Entry
{
  public:
    Entry() { REQUIRE(LE::SW::ClapFirst::clapInit("sw-tests")); }
    ~Entry() { LE::SW::ClapFirst::clapDeinit(); }

    Entry(Entry const &) = delete; // makes non-copyable
    Entry &operator=(Entry const &) = delete;
}; // class Entry

/// \brief An initialised plugin, activated only if asked.
///
/// \note Inactive by default, because that is when a host restores state: it
/// creates the plugin, hands back the project's bytes and only then activates.
/// A state format that needs a sample rate to load would fail exactly there and
/// nowhere else.
class Plugin
{
  public:
    explicit Plugin(clap_host const &host = nullHost(), bool const active = false)
    {
        auto const *const pDescriptor(factory().get_plugin_descriptor(&factory(), 0));
        REQUIRE(pDescriptor != nullptr);
        pPlugin_ = factory().create_plugin(&factory(), &host, pDescriptor->id);
        REQUIRE(pPlugin_ != nullptr);
        REQUIRE(pPlugin_->init(pPlugin_));
        if (active)
        {
            REQUIRE(pPlugin_->activate(pPlugin_, sampleRate, 1, blockSize));
            active_ = true;
        }
    }

    ~Plugin()
    {
        if (active_)
            pPlugin_->deactivate(pPlugin_);
        pPlugin_->destroy(pPlugin_);
    }

    Plugin(Plugin const &) = delete; // makes non-copyable
    Plugin &operator=(Plugin const &) = delete;

    /// \brief The second half of that order: a rate arrives only now, after the
    /// session has already been restored.
    void activate(double const rate = sampleRate)
    {
        REQUIRE(!active_);
        REQUIRE(pPlugin_->activate(pPlugin_, rate, 1, blockSize));
        active_ = true;
    }

    clap_plugin const &operator*() const { return *pPlugin_; }
    clap_plugin const *operator->() const { return pPlugin_; }

    /// \note clap-helpers keeps the C++ object in `plugin_data`. Needed for the
    /// two things state now carries that the C API cannot ask about.
    LE::SW::SpectrumWorxCLAP &implementation() const
    {
        auto *const pHelper(static_cast<LE::SW::PluginHelper *>(pPlugin_->plugin_data));
        REQUIRE(pHelper != nullptr);
        return *static_cast<LE::SW::SpectrumWorxCLAP *>(pHelper);
    }

    /// \note The sample virtuals are `EditorHost`'s and protected on the plugin,
    /// because the editor is what is supposed to call them. A test is not an
    /// editor; going in through the interface the editor uses is closer to the
    /// truth than widening the plugin's own would be.
    LE::SW::GUI::EditorHost &editorHost() const { return implementation(); }

    clap_plugin_state const &state() const
    {
        auto const *const pState(static_cast<clap_plugin_state const *>(
            pPlugin_->get_extension(pPlugin_, CLAP_EXT_STATE)));
        REQUIRE(pState != nullptr);
        return *pState;
    }

    clap_plugin_params const &params() const
    {
        auto const *const pParams(static_cast<clap_plugin_params const *>(
            pPlugin_->get_extension(pPlugin_, CLAP_EXT_PARAMS)));
        REQUIRE(pParams != nullptr);
        return *pParams;
    }

    /// Every parameter the plugin exports, by id, in the order it exports them.
    std::vector<std::pair<clap_id, double>> allParameters() const
    {
        auto const &parameters(params());
        std::vector<std::pair<clap_id, double>> values;
        auto const count(parameters.count(pPlugin_));
        for (std::uint32_t index(0); index < count; ++index)
        {
            clap_param_info info{};
            REQUIRE(parameters.get_info(pPlugin_, index, &info));
            double value{0};
            REQUIRE(parameters.get_value(pPlugin_, info.id, &value));
            values.emplace_back(info.id, value);
        }
        return values;
    }

    void setParameter(clap_id const id, double const value) const
    {
        implementation().setParameter(LE::SW::ParameterID{LE::Plugins::ParameterID{id}},
                                      static_cast<LE::Plugins::AutomatedParameterValue>(value));
    }

  private:
    /// \note First, so it outlives every load this plugin does. A state load
    /// reports its problems from wherever it reaches them and the default
    /// reporter is a message box; one per parameter a 2011 preset never
    /// mentioned, in a process with no message thread.
    SWTest::ScopedProblemCounter quiet_;

    clap_plugin const *pPlugin_;
    bool active_{false};
}; // class Plugin

/// A module parameter, by the slot holding it and its index within the module.
LE::SW::ParameterID moduleParameterID(std::uint8_t const slot, std::uint8_t const parameterIndex)
{
    LE::SW::ParameterID parameterID;
    parameterID.value.type = LE::SW::ParameterID::ModuleParameter;
    parameterID.value._.module.moduleIndex = slot;
    parameterID.value._.module.moduleParameterIndex = parameterIndex;
    return parameterID;
}

/// A global parameter, by its index in `GlobalParameters::Parameters`.
LE::SW::ParameterID globalParameterID(std::uint8_t const index)
{
    LE::SW::ParameterID parameterID;
    parameterID.value.type = LE::SW::ParameterID::GlobalParameter;
    parameterID.value._.global.index = index;
    return parameterID;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Fills three slots and moves a handful of parameters off their
/// defaults, so that a round trip has something to lose.
///
/// \note Through `EditorHost`, which is what a user moving a control goes
/// through and the only entry point that moves *both* Programs. This wrote
/// `implementation().program()` -- the engine's copy -- directly, and `stateSave`
/// reads the main thread's: every case built on this was saving a plugin nobody
/// had touched. The three that checked a module chain failed on it; the fourth,
/// "A session's parameters come back through a second plugin instance", passed
/// while comparing one set of defaults against another.
///
///   The same shape as the 303-preset case: a harness
/// writing through one path and reading through the other, which a green result
/// hides for as long as both ends agree about nothing.
///                                           (06.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void driveIntoAState(Plugin const &plugin)
{
    auto &host(plugin.editorHost());

    for (std::uint8_t slot(0); slot < 3; ++slot)
        REQUIRE(host.editSlot(slot, static_cast<std::int8_t>(slot + 2)));

    /// Every module's Gain and Wet, and the global input gain: enough that a
    /// dropped attribute anywhere in the document shows up.
    ///
    /// \note Native units, CLAP being a `FullRangeAutomatedParameter` protocol.
    for (std::uint8_t slot(0); slot < 3; ++slot)
    {
        host.editParameter(moduleParameterID(slot, 1 /*Gain*/), -3.0f - float(slot));
        host.editParameter(moduleParameterID(slot, 2 /*Wet*/), 90.0f - float(slot));
    }

    host.editParameter(
        globalParameterID(LE::Parameters::IndexOf<LE::SW::GlobalParameters::Parameters,
                                                  LE::SW::GlobalParameters::InputGain>::value),
        0.75f);
}

std::vector<char> asBuffer(std::string const &text)
{
    std::vector<char> buffer(text.begin(), text.end());
    buffer.push_back('\0');
    return buffer;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
// The round trip
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A session's parameters come back through a second plugin instance", "[clap][state]")
{
    Entry const entry;

    OutStream saved;
    std::vector<std::pair<clap_id, double>> original;
    {
        Plugin const plugin;
        driveIntoAState(plugin);
        original = plugin.allParameters();

        REQUIRE(plugin.state().save(&*plugin, &saved));
    }

    REQUIRE_FALSE(saved.data().empty());

    /// \note A *second instance*, not the same one reloaded. Restoring into the
    /// object that saved would pass with a state format that wrote nothing at
    /// all, because everything is already where it belongs.
    Plugin const restored;
    InStream stream(saved.data());
    REQUIRE(restored.state().load(&*restored, &stream));

    auto const reloaded(restored.allParameters());
    REQUIRE(reloaded.size() == original.size());
    for (std::size_t index(0); index < original.size(); ++index)
    {
        INFO("parameter " << index << " of " << original.size());
        CHECK(reloaded[index].first == original[index].first);
        CHECK(reloaded[index].second == Catch::Approx(original[index].second));
    }
}

TEST_CASE("A session restores before the plugin has a sample rate", "[clap][state]")
{
    Entry const entry;

    OutStream saved;
    {
        Plugin const plugin(nullHost(), true /*active*/);
        driveIntoAState(plugin);
        REQUIRE(plugin.state().save(&*plugin, &saved));
    }

    /// \note Inactive, which is where a host restores: create, set state,
    /// activate. Nothing in the load may need a sample rate.
    Plugin const restored;
    InStream stream(saved.data());
    REQUIRE(restored.state().load(&*restored, &stream));

    std::uint8_t modules(0);
    restored.implementation().program().moduleChain().forEach<LE::SW::Engine::ModuleParameters>(
        [&](LE::SW::Engine::ModuleParameters const &) { ++modules; });
    CHECK(modules == 3);
}

////////////////////////////////////////////////////////////////////////////////
// What the bytes are
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Session state is the preset format plus a dawExtraState block", "[clap][state]")
{
    Entry const entry;
    Plugin const plugin;
    driveIntoAState(plugin);

    OutStream saved;
    REQUIRE(plugin.state().save(&*plugin, &saved));
    auto const text(saved.text());
    INFO("state:\n" << text);

    /// It is a preset: same root, same version stamp, same parameter shape.
    CHECK(text.find("<SpectrumWorxPreset") != std::string::npos);
    CHECK(text.find("Format=\"3\"") != std::string::npos);
    CHECK(text.find("<p n=") != std::string::npos);

    /// \note And it is *more* than a preset, which is the whole reason the two
    /// can share a serialisation. Empty today; present regardless, so that the
    /// day it is not empty is not also the day this starts being written.
    CHECK(text.find("<dawExtraState") != std::string::npos);

    /// The terminator goes into the stream: loadFrom() parses a C string and a
    /// host may hand back exactly these bytes and nothing after them.
    CHECK(saved.data().back() == '\0');

    /// \note A preset written to a file must *not* carry the block. If it did,
    /// opening somebody's preset would silently overwrite where your browser was
    /// pointing and which settings you had -- the exact confusion between "what
    /// this sounds like" and "where I was" that the block exists to avoid.
    auto const asPreset(LE::SW::savePreset({}, {}, plugin.implementation().program()));
    CHECK(asPreset.find("<SpectrumWorxPreset") != std::string::npos);
    CHECK(asPreset.find("<dawExtraState") == std::string::npos);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note **An event always streams off**, and reads off. A trigger is armed by
/// a press and disarmed by `TriggerParameter::consumeValue()` on the *engine's*
/// Program; nothing disarms the main thread's, which is the copy `stateSave`
/// reads and `clap_plugin_params::get_value` answers from. So one press left
/// Freeze reading 1 for the rest of the session and wrote it into every state
/// saved after it -- and restoring that state re-armed the trigger, so the next
/// processed block froze a session nobody had asked to freeze.
///
///   Measured before the fix, through this same save: at rest `Freeze" v="0"`,
/// after the press `v="1"`, and after the release `v="1"` still. \see
/// LE::Parameters::isAnEvent, ParametersSaver::valueToStream() and
/// AutomatedModuleImpl::getEffectSpecificAutomatedParameter(). Issue #65.
///                                           (16.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A fired event is saved at rest and reads at rest", "[clap][state]")
{
    Entry const entry;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note **Activated**, unlike most of this file, and it has to be. An
    /// inactive plugin has no spectral setup -- `fftSize` 0, `stepSize` 0 -- and
    /// several of `Engine::Setup`'s conversions divide by them:
    /// `milliSecondsToSteps` by the step, `frequencyRangePerBin` by the FFT
    /// size. On arm64 an integer division by zero yields zero and nothing is
    /// said; on x86 it traps. \see issue #81, which is that hazard on its own --
    /// a host really does restore a session before it activates.
    ///
    ///   Activating is the right fixture rather than a way round anything: a
    /// trigger is something a user fires while audio is running, so the state
    /// this case is about is a state a *running* plugin gets into. It was
    /// inactive only because most of this file is.
    ///                                       (17.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    Plugin const plugin{nullHost(), true /*active*/};

    auto const freeze(LE::SW::Effects::effectIndex("Freeze"));
    REQUIRE(freeze >= 0);
    REQUIRE(plugin.editorHost().editSlot(0, static_cast<std::int8_t>(freeze)));

    /// The Freeze trigger: 00 Bypass, 01 Gain, 02 Wet, 03/04 the frequency
    /// range, 05 Freeze. \see tests/parameters/data/parameterTable.txt.
    auto const trigger(moduleParameterID(0, 5));

    auto const savedState = [&] {
        OutStream saved;
        REQUIRE(stateOf(*plugin).save(&*plugin, &saved));
        return std::string(saved.data().begin(), saved.data().end());
    };

    auto const freezeAttribute = [](std::string const &state) {
        auto const at(state.find("Freeze\" v="));
        REQUIRE(at != std::string::npos); // written either way: the list is full
        return state.substr(at, std::strlen("Freeze\" v=\"0"));
    };

    REQUIRE(freezeAttribute(savedState()) == "Freeze\" v=\"0");

    // Fired the way the button fires it, and never released -- setValue on a
    // trigger only ORs true in, so a release could not clear it anyway.
    plugin.editorHost().editParameter(trigger, 1.0f);

    CHECK(freezeAttribute(savedState()) == "Freeze\" v=\"0");

    /// \note And what the host is told, which is the other half: `get_value`
    /// reads the same copy. A DAW's generic panel showed this stuck at 1.
    bool found(false);
    for (auto const &[id, value] : plugin.allParameters())
    {
        if (id != trigger.binaryValue)
            continue;
        found = true;
        CHECK(value == 0.0);
    }
    CHECK(found);
}

TEST_CASE("A 2011 preset is legal session state", "[clap][state]")
{
    Entry const entry;
    Plugin const plugin;

    /// \note The formats are one now, so the bytes of a factory preset are bytes
    /// `stateLoad` must accept. Worth pinning because it is the cheapest
    /// possible statement of "state is the preset serialisation" -- and because
    /// a host that migrates an old project by handing over a preset file is not
    /// a strange host.
    fs::path const preset(fs::path(SW_PRESET_DATA_DIR) / "Voices" / "Autotalk.swp");

    std::error_code error;
    auto const size(std::filesystem::file_size(preset, error));
    REQUIRE(!error);

    /// \note `<fstream>`, where this was `juce::File::loadFileAsData` into a
    /// `juce::MemoryBlock`. The *exact* bytes, terminator included -- this preset
    /// is one of the 193 that end in a NUL, and what is pinned here is that
    /// `stateLoad` accepts a factory preset's bytes as they sit on disk.
    std::vector<char> contents(static_cast<std::size_t>(size));
    {
        std::ifstream file(preset, std::ios::binary);
        REQUIRE(file.read(contents.data(), static_cast<std::streamsize>(contents.size())));
    }

    InStream stream(std::move(contents));
    REQUIRE(plugin.state().load(&*plugin, &stream));

    std::vector<std::string> effects;
    plugin.implementation().program().moduleChain().forEach<LE::SW::Engine::ModuleParameters>(
        [&](LE::SW::Engine::ModuleParameters const &module) {
            effects.emplace_back(LE::SW::Effects::effectStreamingName(module.effectTypeIndex()));
        });
    REQUIRE(effects.size() == 2);
    CHECK(effects[0] == "Talking Wind");
    CHECK(effects[1] == "TuneWorx");
}

////////////////////////////////////////////////////////////////////////////////
// The sample
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The loaded sample survives a session", "[clap][state]")
{
    Entry const entry;

    /// \note A factory sample, named without a directory. That is the one
    /// spelling that survives a move between machines -- Sample::load() resolves
    /// a bare name against the embedded set when there is nothing on disk -- and
    /// therefore the one worth pinning here.
    fs::path const sample("Carrier.mp3");

    OutStream saved;
    {
        Plugin const plugin(nullHost(), true /*active*/);
        plugin.editorHost().setNewSample(sample);
        REQUIRE(!plugin.editorHost().currentSampleFile().empty());

        REQUIRE(plugin.state().save(&*plugin, &saved));
        INFO("state:\n" << saved.text());
        CHECK(saved.text().find("Carrier.mp3") != std::string::npos);
    }

    Plugin const restored(nullHost(), true /*active*/);
    REQUIRE(restored.editorHost().currentSampleFile().empty());

    InStream stream(saved.data());
    REQUIRE(restored.state().load(&*restored, &stream));

    /// \note The bug the 3.0 state format was written to close: a session that
    /// restored everything except which audio file was loaded.
    CHECK(restored.editorHost().currentSampleFile().filename() == "Carrier.mp3");
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The order every host restores a session in, and the one the sample's
/// rate handling was blind to: create, load state, *then* activate. `Sample::load`
/// is given the plugin's own rate, which is zero until activate() -- so a
/// restored sample is decoded at the file's own rate and records zero.
///
///   `activate()` re-decodes when the rate it is given is not the one the sample
/// was decoded for, and used to skip that whenever the recorded rate was zero --
/// which is every restored session. The sample then played at the file's rate
/// against an engine running at another, for the life of the instance: the exact
/// 2016 bug the re-read was added to fix, still there, and reachable only through
/// the path nothing tested.
///
///   A sample loaded from the *menu* never had it, because by then the plugin has
/// a rate. That is why this needs the inactive constructor.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A sample restored before the host names a rate is decoded again for it", "[clap][state]")
{
    Entry const entry;

    fs::path const sample("Carrier.mp3");

    OutStream saved;
    {
        Plugin const plugin(nullHost(), true /*active*/);
        plugin.editorHost().setNewSample(sample);
        REQUIRE(!plugin.editorHost().currentSampleFile().empty());
        REQUIRE(plugin.state().save(&*plugin, &saved));
    }

    Plugin restored; // inactive, as a host creates one
    InStream stream(saved.data());
    REQUIRE(restored.state().load(&*restored, &stream));

    // Decoded at the file's own rate, because there is no engine rate to decode
    // for yet. This is what the old guard read as "there is no sample".
    REQUIRE(restored.editorHost().currentSampleFile().filename() == "Carrier.mp3");
    REQUIRE(restored.implementation().decodedSampleRate() == 0);

    restored.activate();

    CHECK(restored.implementation().decodedSampleRate() == static_cast<unsigned int>(sampleRate));
    // ...and it is still the same sample, not one dropped by a failed re-read.
    CHECK(restored.editorHost().currentSampleFile().filename() == "Carrier.mp3");
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The sample was the one thing a session could carry that loading another
/// session did not replace. `setSample` returned early on an empty name and did
/// nothing on a name it could not load, so in both cases the *previous* session's
/// audio file went on playing -- and `stateSave` writes `sampleFile_`, so the
/// next save then claimed a file this session had never named. A user's project
/// quietly acquired the sample from whatever they had open before it.
///
///   Both cases below are a session restore, which is where it matters: nobody is
/// watching, and the wrong answer is one that persists into the file.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A session that names no sample clears the one that was loaded", "[clap][state]")
{
    Entry const entry;

    // A session with a sample in it, and one without.
    OutStream withASample, withNone;
    {
        Plugin const plugin(nullHost(), true /*active*/);
        REQUIRE(plugin.state().save(&*plugin, &withNone));

        plugin.editorHost().setNewSample(fs::path("Carrier.mp3"));
        REQUIRE(!plugin.editorHost().currentSampleFile().empty());
        REQUIRE(plugin.state().save(&*plugin, &withASample));
    }

    Plugin const restored(nullHost(), true /*active*/);

    InStream first(withASample.data());
    REQUIRE(restored.state().load(&*restored, &first));
    REQUIRE(restored.editorHost().currentSampleFile().filename() == "Carrier.mp3");

    // ...and now one that says nothing about a sample, on top of it.
    InStream second(withNone.data());
    REQUIRE(restored.state().load(&*restored, &second));

    CHECK(restored.editorHost().currentSampleFile().empty());
    CHECK(restored.implementation().decodedSampleRate() == 0);

    /// \note And the session it saves from here says so too, which is the half a
    /// user would have found later: the name goes into the file.
    OutStream resaved;
    REQUIRE(restored.state().save(&*restored, &resaved));
    CHECK(resaved.text().find("Carrier.mp3") == std::string::npos);
}

TEST_CASE("A session naming a sample that will not load does not keep the previous one",
          "[clap][state]")
{
    Entry const entry;

    OutStream withASample;
    {
        Plugin const plugin(nullHost(), true /*active*/);
        plugin.editorHost().setNewSample(fs::path("Carrier.mp3"));
        REQUIRE(plugin.state().save(&*plugin, &withASample));
    }

    /// \note The same session with the file name replaced by one that is neither
    /// on this machine nor embedded -- a project moved between machines, which is
    /// the case this is really about. Same length, so nothing else in the
    /// document moves.
    std::string named(withASample.text());
    auto const nameAt(named.find("Carrier.mp3"));
    REQUIRE(nameAt != std::string::npos);
    named.replace(nameAt, std::strlen("Carrier.mp3"), "Missing.wav");
    std::vector<char> const missing(named.begin(), named.end() + 1); // the terminator too

    Plugin const restored(nullHost(), true /*active*/);

    InStream present(withASample.data());
    REQUIRE(restored.state().load(&*restored, &present));
    REQUIRE(restored.editorHost().currentSampleFile().filename() == "Carrier.mp3");

    InStream absent(missing);
    REQUIRE(restored.state().load(&*restored, &absent));

    // Not Carrier.mp3, which is what the engine would still have been playing.
    CHECK(restored.editorHost().currentSampleFile().empty());
    CHECK(restored.implementation().decodedSampleRate() == 0);

    OutStream resaved;
    REQUIRE(restored.state().save(&*restored, &resaved));
    CHECK(resaved.text().find("Carrier.mp3") == std::string::npos);
}

TEST_CASE("Loading a sample marks the session dirty", "[clap][state]")
{
    Entry const entry;

    /// \brief A host that offers `clap.state` and counts what it is told.
    ///
    /// \note And deliberately no `clap.thread-check`, matching pluginTests.cpp's
    /// StatefulHost: the mark is then always deferred to on_main_thread(), which
    /// is correct from either thread and is the arm that has to work.
    static struct Counters
    {
        unsigned dirtyMarks{0};
        unsigned callbacks{0};
    } counters;
    counters = {};

    static clap_host_state stateExtension{[](clap_host const *) { ++counters.dirtyMarks; }};
    clap_host host{CLAP_VERSION,
                   nullptr,
                   "sw-tests",
                   "SpectrumWorx",
                   "",
                   "0",
                   [](clap_host const *, char const *const id) -> void const * {
                       return (std::strcmp(id, CLAP_EXT_STATE) == 0) ? &stateExtension : nullptr;
                   },
                   [](clap_host const *) {},
                   [](clap_host const *) {},
                   [](clap_host const *) { ++counters.callbacks; }};

    Plugin const plugin(host, true /*active*/);

    plugin.editorHost().setNewSample(fs::path("Carrier.mp3"));

    CHECK(counters.callbacks > 0);
    CHECK(counters.dirtyMarks == 0); // deferred, not skipped

    plugin->on_main_thread(&*plugin);
    CHECK(counters.dirtyMarks == 1);
}

////////////////////////////////////////////////////////////////////////////////
// What a host may do to the stream
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A state stream is read whatever size the pieces arrive in", "[clap][state]")
{
    Entry const entry;

    OutStream saved(7 /*bytes per write*/);
    {
        Plugin const plugin;
        driveIntoAState(plugin);
        REQUIRE(plugin.state().save(&*plugin, &saved));
    }

    /// \note One byte per read, which CLAP permits and which a state format that
    /// reads a fixed-size header would survive by luck rather than by design.
    Plugin const restored;
    InStream stream(saved.data(), 1);
    REQUIRE(restored.state().load(&*restored, &stream));

    std::uint8_t modules(0);
    restored.implementation().program().moduleChain().forEach<LE::SW::Engine::ModuleParameters>(
        [&](LE::SW::Engine::ModuleParameters const &) { ++modules; });
    CHECK(modules == 3);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief A host may save between a block and the callback the plugin asked for.
///
/// \note `clap-cpp-validator`'s `state-reproducibility-flush`, which is where
/// this was found: it sets the same parameters on two instances, one through
/// `flush()` and one through `process()`, and compares the state files. They
/// differed at the same length -- 1782 bytes against 1782 -- because a value the
/// host wrote in `process()` is echoed to the main thread over `ToUI` and only
/// `onMainThread()` drained it. Nothing in CLAP obliges a host to run that
/// callback before asking for state, so a session saved just after automation
/// moved a knob stored the value from before the move.
///
///   `paramsFlush()` already makes this argument for an inactive plugin and
/// drains its own echo; `stateSave()` is the active half of it.
///                                           (06.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A parameter written during process() is in a state saved before the callback",
          "[clap][state]")
{
    Entry const entry;

    constexpr std::uint8_t inputGainIndex(
        LE::Parameters::IndexOf<LE::SW::GlobalParameters::Parameters,
                                LE::SW::GlobalParameters::InputGain>::value);
    auto const target(SWTest::parameterID(SWTest::globalType, inputGainIndex));
    constexpr double wanted{0.6};

    // Told through flush(), which brings the main thread's copy level itself.
    OutStream viaFlush;
    {
        SWTest::ActivePlugin plugin(sampleRate, blockSize);
        SWTest::OneParameterEvent const event(target, wanted);
        plugin.flush(&*event);
        REQUIRE(stateOf(*plugin).save(&*plugin, &viaFlush));
    }

    // And told through process(), with no on_main_thread() after it -- which is
    // the whole of the case. A host is allowed to save right here.
    OutStream viaProcess;
    {
        SWTest::ActivePlugin plugin(sampleRate, blockSize);
        SWTest::OneParameterEvent const event(target, wanted);
        std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
        std::vector<float> leftOut(blockSize), rightOut(blockSize);
        plugin.process(leftIn, rightIn, leftOut, rightOut, nullptr, &*event);
        REQUIRE(stateOf(*plugin).save(&*plugin, &viaProcess));
    }

    REQUIRE_FALSE(viaFlush.data().empty());
    CHECK(viaFlush.data() == viaProcess.data());
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The other half of `state-reproducibility-flush`, and the half that
/// survived the fix above.
///
/// \note The case above sets a *global* parameter on two **active** instances.
/// The validator does neither: its first instance is only `init()`ed and never
/// activated, and its fuzzer writes the slot selectors, so each instance
/// *constructs modules* -- and a module construction is where the LFO defaults
/// are taken.
///
///   Those two facts together are the whole bug. An inactive plugin never runs
/// `updateLFOTiming()`; an active one runs it at `spectrumWorxCLAP.cpp:861`,
/// *after* the event loop at `:848` that built the module. So the engine's copy
/// was built before the transport was known and the main thread's -- built when
/// the echo drains, and the copy `stateSave` reads -- after it. With the sync
/// default reading a process-global "has a host reported a tempo" flag, the two
/// instances wrote `sync="0"` and `sync="1"` on all 225 LFO parameters: same
/// length, 1782 bytes, and every byte of the difference in one attribute.
///
///   Two things made this expensive to find and are worth keeping. `sync` is the
/// only LFO attribute `LFODataSaver` writes unconditionally -- every other one is
/// omitted at its default -- so a wrong default shows up on all of them at once.
/// And an in-tree harness that *looked* equivalent passed 200 random seeds
/// without reproducing it, because it drove both instances the same way.
///                                           (06.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A slot filled while inactive and one filled under a transport save the same state",
          "[clap][state]")
{
    Entry const entry;

    auto const slotSelector(SWTest::parameterID(SWTest::moduleChainType, 0 /*slot*/));
    constexpr double someEffect{3};

    // Never activated, so nothing ever tells this one a tempo.
    OutStream viaFlush;
    {
        Plugin const plugin;
        SWTest::OneParameterEvent const event(slotSelector, someEffect);
        plugin.params().flush(&*plugin, &*event, &SWTest::discardedOutputEvents());
        REQUIRE(plugin.state().save(&*plugin, &viaFlush));
    }

    // Activated, and told a tempo by the same block that fills the slot.
    OutStream viaProcess;
    {
        SWTest::ActivePlugin plugin(sampleRate, blockSize);
        SWTest::OneParameterEvent const event(slotSelector, someEffect);
        auto const transport(SWTest::transportAt(120, 0, CLAP_TRANSPORT_IS_PLAYING));
        std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
        std::vector<float> leftOut(blockSize), rightOut(blockSize);
        plugin.process(leftIn, rightIn, leftOut, rightOut, &transport, &*event);
        REQUIRE(stateOf(*plugin).save(&*plugin, &viaProcess));
    }

    REQUIRE_FALSE(viaFlush.data().empty());
    CHECK(viaFlush.data().size() == viaProcess.data().size());
    CHECK(viaFlush.data() == viaProcess.data());
}

TEST_CASE("A save whose stream fails is reported as a failure", "[clap][state]")
{
    Entry const entry;
    Plugin const plugin;
    driveIntoAState(plugin);

    OutStream saved(16);
    saved.failAfter(32); // a disk that fills up part way through
    CHECK_FALSE(plugin.state().save(&*plugin, &saved));
}

////////////////////////////////////////////////////////////////////////////////
// What a host must not be able to do
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A state that cannot be read is refused rather than half applied", "[clap][state]")
{
    Entry const entry;

    /// \note Every one of these leaves the plugin as it was. The failure that
    /// matters is not the `false` -- it is a chain half rebuilt from a document
    /// that stopped making sense in the middle, which is what a two-pass apply
    /// over a truncated array used to be able to do.
    auto const refuses([](std::vector<char> data, char const *const what) {
        INFO(what);
        Plugin const plugin;
        driveIntoAState(plugin);
        auto const before(plugin.allParameters());

        InStream stream(std::move(data));
        CHECK_FALSE(plugin.state().load(&*plugin, &stream));
        CHECK(plugin.allParameters() == before);
    });

    refuses({}, "an empty stream");
    refuses(asBuffer("not xml at all"), "text that is not a document");
    refuses(asBuffer("<SpectrumWorxPreset Format=\"3\"><Global>"), "a document cut in half");
    refuses(asBuffer("<SomethingElse Format=\"3\"></SomethingElse>"), "the wrong root element");
    refuses(
        asBuffer("<SpectrumWorxPreset Format=\"99\"><Global /><Modules /></SpectrumWorxPreset>"),
        "a format from the future");

    /// \note The private binary blob this replaced. Nothing shipped with it, so
    /// there is no reader; what there must be is a clean refusal rather than a
    /// parse of arbitrary bytes as if they were text.
    std::vector<char> legacyBlob{'S', 'W', 'X', '1', 0x1e, 0x01, 0x00, 0x00};
    legacyBlob.resize(3440, '\0');
    refuses(std::move(legacyBlob), "an SWX1 blob from a development session");
}

TEST_CASE("A stream that errors is not mistaken for an empty one", "[clap][state]")
{
    Entry const entry;
    Plugin const plugin;

    InStream stream(std::vector<char>{'<'});
    stream.fail();
    CHECK_FALSE(plugin.state().load(&*plugin, &stream));
}

TEST_CASE("State naming an effect this build does not have loads the rest", "[clap][state]")
{
    Entry const entry;
    Plugin const plugin;

    /// \note Degrading rather than refusing, which is the preset format's
    /// long-standing contract and now the session's too: an edition without an
    /// effect, or a project from a build that had one this does not, still opens
    /// with everything it can understand.
    std::string const state(
        "<SpectrumWorxPreset Format=\"3\" Version=\"3.0\" LastModified=\"\" Comment=\"\">"
        "<Global><p n=\"In\" v=\"0.25\" /></Global>"
        "<Modules>"
        "<Module effect=\"No Such Effect\"><p n=\"Bypass\" v=\"0\" /></Module>"
        "<Module effect=\"Ah-ah\"><p n=\"Bypass\" v=\"0\" /></Module>"
        "</Modules></SpectrumWorxPreset>");

    InStream stream(asBuffer(state));
    REQUIRE(plugin.state().load(&*plugin, &stream));

    std::vector<std::string> effects;
    plugin.implementation().program().moduleChain().forEach<LE::SW::Engine::ModuleParameters>(
        [&](LE::SW::Engine::ModuleParameters const &module) {
            effects.emplace_back(LE::SW::Effects::effectStreamingName(module.effectTypeIndex()));
        });
    REQUIRE(effects.size() == 1);
    CHECK(effects[0] == "Ah-ah");
    CHECK(plugin.implementation()
              .program()
              .parameters()
              .get<LE::SW::GlobalParameters::InputGain>()
              .getValue() == Catch::Approx(0.25f));
}

////////////////////////////////////////////////////////////////////////////////
//
// A stream that never ends
// ------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note `readWholeStream`'s only exit was the stream saying it had no more, so
/// a host handing over one that never does -- a corrupt project, a pipe nobody
/// closes -- grew the buffer until the allocation threw. `stateLoad` is
/// `noexcept`, so that throw is `std::terminate` and the host dies opening a
/// project.
///
///   Held to the preset reader's cap, which is the same grammar and about seven
/// thousand times the largest thing the format can produce.
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
/// \brief A `clap_istream` that answers every read in full, forever.
class EndlessStream
{
  public:
    EndlessStream()
    {
        stream_.ctx = this;
        stream_.read = [](clap_istream const *const stream, void *const buffer,
                          std::uint64_t const size) -> std::int64_t {
            auto &self(*static_cast<EndlessStream *>(stream->ctx));
            std::memset(buffer, ' ', size);
            self.handed_ += size;
            return static_cast<std::int64_t>(size);
        };
    }

    EndlessStream(EndlessStream const &) = delete;
    EndlessStream &operator=(EndlessStream const &) = delete;

    clap_istream const *operator&() const { return &stream_; }

    std::uint64_t handed() const { return handed_; }

  private:
    clap_istream stream_{};
    std::uint64_t handed_{0};
}; // class EndlessStream
} // anonymous namespace

TEST_CASE("A state stream that never ends is given up on", "[clap][state][hostile]")
{
    Plugin const plugin;

    EndlessStream stream;
    CHECK(!plugin.state().load(&*plugin, &stream));

    // It stopped, and it stopped near the cap rather than at whatever the
    // allocator happened to refuse.
    CHECK(stream.handed() <= LE::SW::maximumPresetSize + (1u << 12));
}

////////////////////////////////////////////////////////////////////////////////
//
// An exception on the way through
// -------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note `stateSave` and `stateLoad` are `noexcept`, like every CLAP entry
/// point, and between them they buffer a host-supplied stream, build the whole
/// module chain out of it and decode whatever audio file the state names. All of
/// that allocates; `Sample::load` asks for hundreds of megabytes for a long
/// file, from inside `stateLoad`. An exception out of any of it was
/// `std::terminate` -- the host dying while opening a project rather than being
/// told the project would not open.
///
/// \note The throw is injected through the host's own `read`, which is not where
/// a real one would come from -- a host does not throw through a C callback.
/// It is a way to raise an exception at a point genuinely inside the entry
/// point's body, which is the thing being pinned; `bad_alloc` from the real
/// allocation sites cannot be provoked on demand and would prove the same
/// property. Without the handler this case does not fail, it aborts the run.
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
/// \brief A `clap_istream` that throws on the first read.
class ThrowingStream
{
  public:
    ThrowingStream()
    {
        stream_.ctx = this;
        stream_.read = [](clap_istream const *, void *, std::uint64_t) -> std::int64_t {
            throw std::bad_alloc();
        };
    }

    ThrowingStream(ThrowingStream const &) = delete;
    ThrowingStream &operator=(ThrowingStream const &) = delete;

    clap_istream const *operator&() const { return &stream_; }

  private:
    clap_istream stream_{};
}; // class ThrowingStream
} // anonymous namespace

TEST_CASE("An exception inside stateLoad is answered rather than fatal", "[clap][state][hostile]")
{
    Plugin const plugin;

    ThrowingStream stream;
    CHECK(!plugin.state().load(&*plugin, &stream));

    // ...and the instance is still usable afterwards, which is the other half of
    // "the call failed" as against "the plugin is now wreckage".
    InStream good(asBuffer(std::string(
        "<SpectrumWorxPreset Format=\"3\" Version=\"3.0\" LastModified=\"\" Comment=\"\">"
        "<Global><p n=\"In\" v=\"0.25\" /></Global><Modules /></SpectrumWorxPreset>")));
    CHECK(plugin.state().load(&*plugin, &good));
    CHECK(plugin.implementation()
              .program()
              .parameters()
              .get<LE::SW::GlobalParameters::InputGain>()
              .getValue() == Catch::Approx(0.25f));
}
