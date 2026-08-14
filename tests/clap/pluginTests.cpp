////////////////////////////////////////////////////////////////////////////////
///
/// \file pluginTests.cpp
/// ---------------------
///
///   Drives the plugin the way a host does: through clap_plugin_entry's
/// factory and the clap_plugin vtable, not through SpectrumWorxCLAP's C++
/// interface. That is deliberate -- the lifecycle order a DAW uses (init,
/// activate, start_processing, process, ...) is the thing most likely to be
/// wrong, and calling the C++ members directly would not exercise it.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "clap/testHost.hpp"

#include "core/host_interop/parameters.hpp" // the fixed parameter count

/// \note For the live parameter, which is not the one a host reads. Since the
/// base/modulated split, `clap_plugin_params::get_value` answers the value a user
/// set and an LFO's sweep is deliberately invisible there -- so a test asking
/// "does an enabled LFO modulate anything" has to look at the engine, and reaches
/// it through `clap_plugin::plugin_data` as processLockTests.cpp does.
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/threading/publish.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "gui/editor/presetLoading.hpp"
#include "le/parameters/lfoImpl.hpp"
#include "presets/presetHarness.hpp"
#include "gui/editor/spectrumWorxEditor.hpp"
#include "gui/editor/zoomedEditor.hpp"
#include "le/spectrumworx/presetStorage.hpp"

#include <clap/clap.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <cstring>
#include <numbers>
#include <functional>
#include <set>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace SWTest;

void fillWithSine(std::vector<float> &buffer, float const sampleRate, float const frequency,
                  std::uint32_t const startFrame)
{
    for (std::size_t frame(0); frame < buffer.size(); ++frame)
        buffer[frame] = 0.5f * std::sin(2 * std::numbers::pi_v<float> * frequency *
                                        static_cast<float>(startFrame + frame) / sampleRate);
}

bool allFinite(std::vector<float> const &buffer)
{
    return std::all_of(buffer.begin(), buffer.end(),
                       [](float const sample) { return std::isfinite(sample); });
}

float peak(std::vector<float> const &buffer)
{
    float largest{0};
    for (auto const sample : buffer)
        largest = std::max(largest, std::abs(sample));
    return largest;
}

/// An LFO's own parameters, in the order lfoImpl.hpp declares them.
enum LFOParameter : unsigned
{
    lfoEnabled = 0,
    lfoPeriodScale = 1,
    lfoPhase = 2,
    lfoLowerBound = 3,
    lfoUpperBound = 4,
    lfoSyncTypes = 5,
    lfoWaveform = 6
};

/// \note A module's LFOs are indexed by *LFO-able* parameter, which is its
/// parameter index less the one base parameter that has no LFO (Bypass, first).
/// So LFO 0 drives module parameter 1, which is Gain -- a base parameter, and so
/// one every effect has whatever is in the slot.
clap_id lfoParameterID(unsigned const moduleIndex, unsigned const lfoIndex,
                       LFOParameter const which)
{
    return parameterID(lfoType, moduleIndex, (lfoIndex << 8) | which);
}

/// The module parameter \p lfoIndex drives. \see lfoParameterID
clap_id modulatedParameterID(unsigned const moduleIndex, unsigned const lfoIndex)
{
    return parameterID(moduleType, moduleIndex, (lfoIndex + 1) << 8);
}

/// \brief The value the DSP is actually using for module \p moduleIndex's
/// parameter \p parameterIndex, right now.
///
/// \note Not `clap_plugin_params::get_value`, which since the base/modulated
/// split answers the value a *user* set: an LFO modulates the live parameter and
/// leaves that one alone, on purpose, so a host's automation lane does not jitter
/// and a saved preset does not freeze the sweep. Reaching the engine needs the C++
/// object out of `plugin_data`, which is the same trick processLockTests.cpp uses
/// and for the same reason -- what is being observed has no name in the C API.
float liveModuleParameter(clap_plugin const &plugin, std::uint8_t const moduleIndex,
                          std::uint8_t const parameterIndex)
{
    auto *const pHelper(static_cast<LE::SW::PluginHelper *>(plugin.plugin_data));
    REQUIRE(pHelper != nullptr);
    auto &implementation(*static_cast<LE::SW::SpectrumWorxCLAP *>(pHelper));
    auto const pModule(
        implementation.program().moduleChain().moduleAs<LE::SW::Module>(moduleIndex));
    REQUIRE(pModule);
    return pModule->getBaseParameter(parameterIndex);
}

/// \brief The period, in bars, that the *engine's* LFO \p lfoIndex on module
/// \p moduleIndex is running at.
///
/// \note The engine's copy rather than `clap_plugin_params::get_value`, which
/// answers from the main thread's Program. A meter change resnaps the LFOs the
/// audio thread owns -- `Processor::updateModuleLFOs()`, once per block -- and
/// reaches nothing else, so asking the host-facing side whether a resnap happened
/// is asking the copy it did not happen to. \see liveModuleParameter, which
/// reaches the same object for the same reason.
float engineLFOPeriodScale(clap_plugin const &plugin, std::uint8_t const moduleIndex,
                           std::uint8_t const lfoIndex)
{
    auto *const pHelper(static_cast<LE::SW::PluginHelper *>(plugin.plugin_data));
    REQUIRE(pHelper != nullptr);
    auto &implementation(*static_cast<LE::SW::SpectrumWorxCLAP *>(pHelper));
    auto const pModule(
        implementation.program().moduleChain().moduleAs<LE::SW::Module>(moduleIndex));
    REQUIRE(pModule);
    return pModule->lfo(lfoIndex).periodScale();
}

/// \brief A module parameter's static description, for a value inside its range.
LE::Parameters::RuntimeInformation const &moduleParameterInfo(clap_plugin const &plugin,
                                                              std::uint8_t const moduleIndex,
                                                              std::uint8_t const parameterIndex)
{
    auto *const pHelper(static_cast<LE::SW::PluginHelper *>(plugin.plugin_data));
    REQUIRE(pHelper != nullptr);
    auto &implementation(*static_cast<LE::SW::SpectrumWorxCLAP *>(pHelper));
    auto const pModule(
        implementation.program().moduleChain().moduleAs<LE::SW::Module>(moduleIndex));
    REQUIRE(pModule);
    return pModule->parameterInfo(parameterIndex);
}

/// \brief Fills slot 0, turns LFO 0 on, and reports how many distinct values its
/// target takes over \p blocks blocks of audio.
///
/// One is the failure: it means the LFO is enabled, is being asked for a value
/// every block, and is answering with the same one every time.
std::size_t distinctModulatedValues(ActivePlugin &plugin, clap_plugin_params const &params,
                                    float const sampleRate, std::uint32_t const blockSize,
                                    unsigned const blocks,
                                    clap_event_transport const *const transport)
{
    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0),
                                        0 /*the first effect in the list*/);
    params.flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());
    plugin.pumpMainThread();

    OneParameterEvent const enable(lfoParameterID(0, 0, lfoEnabled), 1);
    params.flush(&*plugin, &*enable, &discardedOutputEvents());
    plugin.pumpMainThread();

    /// \note LFO 0 drives module parameter 1, which is Gain -- the first base
    /// parameter after Bypass, and one every effect has whatever is in the slot.
    constexpr std::uint8_t gainIndex{1};

    std::vector<float> leftIn(blockSize), rightIn(blockSize);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);
    std::set<float> seen;

    for (unsigned block(0); block < blocks; ++block)
    {
        fillWithSine(leftIn, sampleRate, 440.0f, block * blockSize);
        rightIn = leftIn;
        plugin.process(leftIn, rightIn, leftOut, rightOut, transport);
        seen.insert(liveModuleParameter(*plugin, 0, gainIndex));
    }
    return seen.size();
}

/// \brief The same run, counting what the *host* would have seen.
///
/// One is the pass here: an LFO is a modulation and a host's value is not
/// supposed to move because of one.
std::size_t distinctHostVisibleValues(ActivePlugin &plugin, clap_plugin_params const &params,
                                      float const sampleRate, std::uint32_t const blockSize,
                                      unsigned const blocks,
                                      clap_event_transport const *const transport)
{
    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), 0);
    params.flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());
    plugin.pumpMainThread();

    OneParameterEvent const enable(lfoParameterID(0, 0, lfoEnabled), 1);
    params.flush(&*plugin, &*enable, &discardedOutputEvents());
    plugin.pumpMainThread();

    auto const target(modulatedParameterID(0, 0));

    std::vector<float> leftIn(blockSize), rightIn(blockSize);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);
    std::set<double> seen;

    for (unsigned block(0); block < blocks; ++block)
    {
        fillWithSine(leftIn, sampleRate, 440.0f, block * blockSize);
        rightIn = leftIn;
        plugin.process(leftIn, rightIn, leftOut, rightOut, transport);

        double value{0};
        REQUIRE(params.get_value(&*plugin, target, &value));
        seen.insert(value);
    }
    return seen.size();
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("The factory offers exactly one plugin, and it describes itself", "[clap]")
{
    Entry const entry;

    REQUIRE(factory().get_plugin_count(&factory()) == 1);

    auto const *const pDescriptor(factory().get_plugin_descriptor(&factory(), 0));
    REQUIRE(pDescriptor != nullptr);
    CHECK(std::strlen(pDescriptor->id) > 0);
    CHECK(std::strcmp(pDescriptor->name, "SpectrumWorx") == 0);
    CHECK(pDescriptor->features != nullptr);
    CHECK(std::strcmp(pDescriptor->features[0], CLAP_PLUGIN_FEATURE_AUDIO_EFFECT) == 0);
}

TEST_CASE("A plugin survives the host lifecycle in order", "[clap]")
{
    Entry const entry;
    ActivePlugin plugin(48000, 512);
    CHECK(plugin->desc != nullptr);
}

TEST_CASE("Audio ports are stereo in, stereo out, plus a side chain", "[clap]")
{
    Entry const entry;
    ActivePlugin plugin(48000, 512);

    auto const *const ports(static_cast<clap_plugin_audio_ports const *>(
        plugin->get_extension(&*plugin, CLAP_EXT_AUDIO_PORTS)));
    REQUIRE(ports != nullptr);

    CHECK(ports->count(&*plugin, true) == 2);
    CHECK(ports->count(&*plugin, false) == 1);

    clap_audio_port_info main{};
    REQUIRE(ports->get(&*plugin, 0, true, &main));
    CHECK(main.channel_count == 2);
    CHECK(std::strcmp(main.port_type, CLAP_PORT_STEREO) == 0);
    CHECK((main.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0);

    /// \note Never an in-place pair -- see the note in spectrumWorxCLAP.cpp.
    CHECK(main.in_place_pair == CLAP_INVALID_ID);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And the second port, which the case above described in its title
    /// and never asked about: it checked `count(true) == 2` and then read index
    /// 0 twice over. A `get()` that answered `false` for index 1, or handed back
    /// a port flagged `IS_MAIN`, would have passed.
    ///
    /// \note The ids are literals because that is what they are to a host --
    /// `spectrumWorxCLAP.cpp` keeps them in an anonymous namespace, and a host
    /// matching a saved routing against port 1 has nothing else to go on. The
    /// two must at least differ, which is the part a copy-paste gets wrong.
    ///
    ////////////////////////////////////////////////////////////////////////////
    clap_audio_port_info side{};
    REQUIRE(ports->get(&*plugin, 1, true, &side));
    CHECK(side.id == 1);
    CHECK(side.id != main.id);
    CHECK(side.channel_count == 2);
    CHECK(std::strcmp(side.port_type, CLAP_PORT_STEREO) == 0);
    CHECK((side.flags & CLAP_AUDIO_PORT_IS_MAIN) == 0);
    CHECK(side.in_place_pair == CLAP_INVALID_ID);
    CHECK(std::strlen(side.name) > 0);

    /// \note No out-of-range case here on purpose: clap-helpers checks the index
    /// against `count()` before `audioPortsInfo()` is reached, so asking for
    /// index 2 tests upstream's guard and prints its complaint over the run.
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The port-1 branch of `runEngine()`, driven for the first time on
/// 05.08.2026. `ActivePlugin::process` hardcoded `audio_inputs_count = 1`, so
/// every CLAP case in the tree took the *fallback* -- the side chain quietly
/// wired to the main input -- and the three lines that read the second port had
/// never run at all.
///
///   `tests/effects/sideChainTests.cpp` is the same question asked of the
/// engine. This is the plugin edge: that what a host puts on port 1 is what the
/// engine receives, and that the two ways a host can decline to fill it both
/// fall back rather than fault.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("What a host puts on the side chain port reaches the engine", "[clap][side-chain]")
{
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{512};
    constexpr unsigned int blocks{24}; // past the engine's latency

    /// \note Colorifer: one of the fifteen effects measured to read a side
    /// chain, and one of the eleven that does so at its default parameters.
    /// \see sideChainTests.cpp for the set and for the four that do not.
    auto const colorifer(LE::SW::Effects::effectIndex("Colorifer"));
    REQUIRE(colorifer >= 0);

    /// \brief One run of \p blocks, with the side chain arranged by \p connect.
    ///
    /// The main input is a 440 Hz sine throughout; what changes between runs is
    /// only what is on the other port. \p carrier is what goes on it, zero
    /// meaning "the host owns these buffers and nothing is patched into them",
    /// which is what every real DAW hands over for an unpatched side chain.
    auto const run(
        [&](std::function<void(ActivePlugin &, std::vector<float> &, std::vector<float> &)> const
                &connect,
            float const carrier = 1100.0f) {
            Entry const entry;
            ActivePlugin plugin(sampleRate, blockSize);

            OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), colorifer);
            parameters(*plugin).flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());
            plugin.pumpMainThread();

            std::vector<float> leftIn(blockSize), rightIn(blockSize);
            std::vector<float> sideLeft(blockSize, 0.0f), sideRight(blockSize, 0.0f);
            std::vector<float> leftOut(blockSize), rightOut(blockSize);

            connect(plugin, sideLeft, sideRight);

            std::vector<float> tail;
            for (unsigned int block(0); block < blocks; ++block)
            {
                fillWithSine(leftIn, sampleRate, 440.0f, block * blockSize);
                rightIn = leftIn;
                // 1100 Hz on the side chain: a different partial, in a different
                // bin, so "the output moved" cannot be the main signal leaking.
                if (carrier > 0)
                {
                    fillWithSine(sideLeft, sampleRate, carrier, block * blockSize);
                    sideRight = sideLeft;
                }

                plugin.process(leftIn, rightIn, leftOut, rightOut);
                REQUIRE(allFinite(leftOut));
                tail.assign(leftOut.begin(), leftOut.end());
            }
            return tail;
        });

    auto const noPort(run([](ActivePlugin &, std::vector<float> &, std::vector<float> &) {}));
    auto const connected(
        run([](ActivePlugin &plugin, std::vector<float> &left, std::vector<float> &right) {
            plugin.connectSideChain(left, right);
        }));
    auto const emptyPort(run([](ActivePlugin &plugin, std::vector<float> &, std::vector<float> &) {
        plugin.connectSideChainWithNoBuffers();
    }));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The fourth arrangement, and the one every real host actually hands
    /// over: a port with buffers the host owns and nothing patched into it. No
    /// pointer distinguishes it from a connected one, which is why the documented
    /// "an unpatched side chain is the main input" behaviour was unreachable
    /// until `clap_audio_buffer::constant_mask` was read.
    ///
    ///   Zeroed buffers *and* a mask declaring them constant, because a host that
    /// sets a bit has undertaken to fill the channel with the constant. Setting
    /// only one of the two would be testing a host that does not exist.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const declaredSilent(run(
        [](ActivePlugin &plugin, std::vector<float> &left, std::vector<float> &right) {
            plugin.connectSideChain(left, right);
            plugin.declareSideChainConstant(0b11);
        },
        0.0f));

    /// \note The same zeroed buffers with no claim made about them, which is
    /// every host that does not set the mask -- the hint is optional and neither
    /// clap-wrapper nor a given DAW is known to set it. This one has to keep
    /// behaving exactly as it did: silence on the port is silence in the engine.
    auto const silentButUndeclared(
        run([](ActivePlugin &plugin, std::vector<float> &left,
               std::vector<float> &right) { plugin.connectSideChain(left, right); },
            0.0f));

    REQUIRE(noPort.size() == connected.size());
    REQUIRE(noPort.size() == emptyPort.size());
    REQUIRE(noPort.size() == declaredSilent.size());
    REQUIRE(noPort.size() == silentButUndeclared.size());

    // A carrier the engine could not have got from the main input changed what
    // came out. This is the whole case: without it, every arrangement below is
    // the same arrangement.
    CHECK(connected != noPort);
    CHECK(peak(connected) > 0);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And every way of not filling the port lands on the *same* fallback,
    /// bit for bit. `runEngine()` reads the port count, `data32` and now the
    /// constant mask, so a host that declares one port, a host that declares two
    /// and fills one, and a host that declares two and says the second is a
    /// constant zero all have to be indistinguishable. The middle one is the arm
    /// that would have dereferenced null had the `&&` been the other way round.
    ///
    ////////////////////////////////////////////////////////////////////////////
    CHECK(emptyPort == noPort);
    CHECK(declaredSilent == noPort);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And the mask is the *whole* of what distinguishes them: the same
    /// zeroed buffers with nothing declared about them are a connected side chain
    /// carrying silence, and that is not the main input. A host that sets no mask
    /// therefore gets what it always got.
    ///
    ////////////////////////////////////////////////////////////////////////////
    CHECK(silentButUndeclared != noPort);
}

TEST_CASE("The engine reports the latency its FFT size implies", "[clap]")
{
    Entry const entry;
    ActivePlugin plugin(48000, 512);

    auto const *const latency(static_cast<clap_plugin_latency const *>(
        plugin->get_extension(&*plugin, CLAP_EXT_LATENCY)));
    REQUIRE(latency != nullptr);

    /// 2048 today, which is the default FFT size -- but the bound rather than
    /// the number, because the FFT size is a parameter and this test has no
    /// business pinning its default. What matters is that it is not zero: that
    /// is what the stage 1 pass-through reported, so it is what tells a live
    /// engine from a dead one.
    auto const reported(latency->get(&*plugin));
    CHECK(reported > 0);
    CHECK(reported <= 8192);
}

TEST_CASE("A sine through the default chain comes out as audio, not silence", "[clap]")
{
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{512};
    // Enough blocks to push the first input past the engine's latency.
    constexpr unsigned int blocks{32};

    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);

    std::vector<float> leftIn(blockSize), rightIn(blockSize);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);

    float largestOutput{0};
    for (unsigned int block(0); block < blocks; ++block)
    {
        fillWithSine(leftIn, sampleRate, 440.0f, block * blockSize);
        rightIn = leftIn;
        std::fill(leftOut.begin(), leftOut.end(), 0.0f);
        std::fill(rightOut.begin(), rightOut.end(), 0.0f);

        plugin.process(leftIn, rightIn, leftOut, rightOut);

        REQUIRE(allFinite(leftOut));
        REQUIRE(allFinite(rightOut));
        largestOutput = std::max(largestOutput, peak(leftOut));
    }

    // A bypassed module chain still runs the full analysis/resynthesis path, so
    // the sine has to survive it at roughly its input amplitude. The stage 1
    // pass-through would also pass this; the latency case above is what tells
    // the two apart.
    CHECK(largestOutput > 0.1f);
    CHECK(largestOutput < 2.0f);
}

TEST_CASE("The host sees the engine's own parameters, not a stand-in", "[clap]")
{
    Entry const entry;
    ActivePlugin plugin(48000, 512);

    auto const *const params(
        static_cast<clap_plugin_params const *>(plugin->get_extension(&*plugin, CLAP_EXT_PARAMS)));
    REQUIRE(params != nullptr);

    auto const count(params->count(&*plugin));
    REQUIRE(count > 0);

    // Every entry has to be nameable, in range, and grouped somewhere.
    bool sawAGlobal{false};
    for (std::uint32_t index(0); index < count; ++index)
    {
        clap_param_info info{};
        REQUIRE(params->get_info(&*plugin, index, &info));
        INFO("parameter " << index << " '" << info.name << "'");
        CHECK(std::strlen(info.name) > 0);
        // Strictly less: a host divides by this range, and one parameter in a
        // fixed list that no effect currently owns must not hand it a zero.
        CHECK(info.min_value < info.max_value);
        CHECK(info.default_value >= info.min_value);
        CHECK(info.default_value <= info.max_value);
        CHECK(std::strlen(info.module) > 0);
        if (std::strcmp(info.module, "Global") == 0)
            sawAGlobal = true;

        double value{0};
        CHECK(params->get_value(&*plugin, info.id, &value));

        std::array<char, CLAP_NAME_SIZE> text{};
        CHECK(params->value_to_text(&*plugin, info.id, value, text.data(), text.size()));
    }
    CHECK(sawAGlobal);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The cost of declaring every slot's parameters up front: on an empty
/// instance most of them belong to no effect. They are shown anyway.
///
///   They used to be flagged CLAP_PARAM_IS_HIDDEN, which is what the flag is for
/// and would have been right if hosts re-read it. The shipped clap-wrapper maps
/// flags once at construction and a VST3 RESCAN_INFO re-reads only the name, so
/// in a VST3 host the flags of an *empty* instance were the flags forever: an
/// automation list of eleven rows out of 286, permanently. Every parameter is
/// visible now, and this is the case that says so -- it is a decision about what
/// hosts are told, so it is worth a test that fails if someone reaches for the
/// flag again.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A parameter no effect currently owns is shown anyway, and answers", "[clap]")
{
    Entry const entry;
    ActivePlugin plugin(48000, 512);

    auto const *const params(
        static_cast<clap_plugin_params const *>(plugin->get_extension(&*plugin, CLAP_EXT_PARAMS)));
    REQUIRE(params != nullptr);

    std::uint32_t ownedByNoEffect{0};
    for (std::uint32_t index(0); index < params->count(&*plugin); ++index)
    {
        clap_param_info info{};
        REQUIRE(params->get_info(&*plugin, index, &info));
        INFO("parameter " << index << " '" << info.name << "'");

        CHECK((info.flags & CLAP_PARAM_IS_HIDDEN) == 0);

        // Shown means usable: a host may write to any of these, so all of them
        // have to answer and none may hand back a zero-width range to divide by.
        double value{0};
        CHECK(params->get_value(&*plugin, info.id, &value));
        CHECK(info.min_value < info.max_value);
        CHECK((info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0);

        ownedByNoEffect += isNormalisedType(info.id);
    }

    // ...and the list really does contain both kinds, or the above says nothing:
    // the globals and the five slot selectors are real with nothing loaded, and
    // everything else is a parameter waiting for an effect.
    CHECK(ownedByNoEffect > 0);
    CHECK(ownedByNoEffect < params->count(&*plugin));
}

TEST_CASE("Filling a module slot renames its parameters without adding any", "[clap]")
{
    // A slot's effect is itself a parameter, and setting it changes what the
    // slot's other parameters *are*. What it must not change is how many there
    // are: ext/params.h only lets a plugin add or remove parameters through
    // clap_host->restart() and a deactivated CLAP_PARAM_RESCAN_ALL, so the
    // count a host reads once has to stay good. Every slot's full complement is
    // therefore declared up front and an unused one reads as N/A.
    Entry const entry;
    ActivePlugin plugin(48000, 512);

    auto const &params(parameters(*plugin));

    auto const before(allParameterInfo(*plugin, params));

    // Every module parameter of every slot, plus the LFOs, plus the globals --
    // present with no effect loaded at all, which is the point.
    CHECK(before.size() == LE::SW::ParameterCounts::maxNumberOfParameters);

    // Slot 1's parameters exist already; they just have nothing to name them.
    auto const slotOne(
        [](clap_param_info const &info) { return std::strncmp(info.module, "Slot 1", 6) == 0; });
    auto const slotOneBefore(std::count_if(before.begin(), before.end(), slotOne));
    CHECK(slotOneBefore > 0);

    // None of slot 1's module or LFO parameters is usable yet, and every one of
    // them says so -- in its *display*, which is where an unusable parameter says
    // it now. The slot's *selector* is not one of them: it is what fills the slot,
    // so it is always live, and "Slot 1" is its module path too.
    for (auto const &info : before)
        if (slotOne(info) && isNormalisedType(info.id))
        {
            std::array<char, CLAP_NAME_SIZE> text{};
            REQUIRE(params.value_to_text(&*plugin, info.id, 0.5, text.data(), text.size()));
            CHECK(std::strncmp(text.data(), "N/A", 3) == 0);
        }

    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0),
                                        0 /*the first effect in the list*/);
    params.flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());
    plugin.pumpMainThread();

    auto const after(allParameterInfo(*plugin, params));

    // The count is the contract: it did not move.
    REQUIRE(after.size() == before.size());
    CHECK(std::count_if(after.begin(), after.end(), slotOne) == slotOneBefore);

    // What moved is the description, and only the parts CLAP_PARAM_RESCAN_INFO
    // names: the parameters picked up the effect's names, and became usable
    // rather than "N/A". Their ranges did not move, and could not have -- see the
    // range test below.
    //
    /// \note And the *flags* did not move either, which is a stronger statement
    /// than it looks. They cannot: the shipped clap-wrapper reads them once, at
    /// construction, so anything here that changed with the slot would be a
    /// description a VST3 host was never told about. That is what the hidden flag
    /// used to do.
    std::uint32_t usable{0}, renamed{0};
    for (std::size_t index(0); index < after.size(); ++index)
    {
        if (!slotOne(after[index]))
            continue;

        CHECK(after[index].flags == before[index].flags);

        if (std::strcmp(after[index].name, before[index].name) != 0)
            ++renamed;

        if (!isNormalisedType(after[index].id))
            continue;

        std::array<char, CLAP_NAME_SIZE> text{};
        REQUIRE(params.value_to_text(&*plugin, after[index].id, 0.5, text.data(), text.size()));
        usable += (std::strncmp(text.data(), "N/A", 3) != 0);
    }
    CHECK(usable > 0);
    CHECK(renamed > 0);
}

TEST_CASE("Every parameter accepts the bounds it advertises", "[clap]")
{
    // A host is entitled to write min_value and max_value to anything it is
    // shown, and clap-validator's param-fuzz-bounds does exactly that. Whatever
    // the plugin then does with the value, it may not be out of the range the
    // parameter itself considers valid.
    Entry const entry;
    ActivePlugin plugin(48000, 512);

    auto const &params(parameters(*plugin));

    /// \note Every effect in turn, not just the first. The parameters that have
    /// gone out of range are effect-specific ones, and a slot only ever has the
    /// parameters of whatever is currently in it -- so a sweep that fills the
    /// slot once tests one effect's parameters and calls it coverage.
    clap_param_info selector{};
    for (auto const &info : allParameterInfo(*plugin, params))
        if (info.id == parameterID(moduleChainType, 0))
            selector = info;
    REQUIRE(selector.max_value > selector.min_value);

    for (double effect(selector.min_value); effect <= selector.max_value; ++effect)
    {
        OneParameterEvent const fill(selector.id, effect);
        params.flush(&*plugin, &*fill, &discardedOutputEvents());
        plugin.pumpMainThread();

        for (auto const &info : allParameterInfo(*plugin, params))
        {
            if ((std::strncmp(info.module, "Slot 1", 6) != 0) || (info.id == selector.id))
                continue;

            for (double const value :
                 {info.min_value, info.max_value, (info.min_value + info.max_value) / 2})
            {
                CAPTURE(effect, info.id, info.name, value);
                OneParameterEvent const edit(info.id, value);
                params.flush(&*plugin, &*edit, &discardedOutputEvents());
                plugin.pumpMainThread();

                double read{0};
                REQUIRE(params.get_value(&*plugin, info.id, &read));
                CHECK(read >= info.min_value);
                CHECK(read <= info.max_value);

                /// \note And asked to render it, which is the other half of what
                /// a host does when it rescans -- get_info, get_value,
                /// value_to_text over the whole list. The renderer used to build
                /// a throwaway parameter and assign the value to it, and a
                /// detached parameter is what asserted; it prints the converted
                /// value now (printer.hpp's AutomatedParameterPrinter).
                std::array<char, 128> text{};
                CHECK(params.value_to_text(&*plugin, info.id, value, text.data(), text.size()));
            }
        }
    }
}

TEST_CASE("An effect chosen before activate still processes audio", "[clap]")
{
    // The order a session restore happens in, and the order a standalone starts
    // in: the parameter that fills a slot arrives while the plugin is merely
    // initialised, so the engine has no sample rate, no bins and no step time to
    // configure the effect against. Configuring it anyway is what asserted --
    // Bandpass and Bandstop indexed an empty spectrum, Denoiser divided an empty
    // amplitude range -- so it is deferred to the resize that activate() performs.
    //
    //   Which makes this the test that matters: deferring is only correct if the
    // effect really is set up by the time audio arrives.
    constexpr std::uint32_t blockSize{512};
    constexpr float sampleRate{48000};

    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize, nullHost(), [](clap_plugin const &inactive) {
        auto const *const params(static_cast<clap_plugin_params const *>(
            inactive.get_extension(&inactive, CLAP_EXT_PARAMS)));
        REQUIRE(params != nullptr);

        OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), 0);
        params->flush(&inactive, &*fillSlotOne, &discardedOutputEvents());
    });

    // The slot really is filled, and by an effect rather than by nothing.
    auto const &params(parameters(*plugin));
    double selector{-1};
    REQUIRE(params.get_value(&*plugin, parameterID(moduleChainType, 0), &selector));
    CHECK(selector >= 0);

    std::vector<float> leftIn(blockSize), rightIn(blockSize);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);
    fillWithSine(leftIn, sampleRate, 440, 0);
    fillWithSine(rightIn, sampleRate, 440, 0);

    // Long enough to clear the analysis latency, so silence would be a real answer
    // rather than a not-yet-answer.
    for (std::uint32_t block(0); block < 16; ++block)
    {
        fillWithSine(leftIn, sampleRate, 440, block * blockSize);
        fillWithSine(rightIn, sampleRate, 440, block * blockSize);
        plugin.process(leftIn, rightIn, leftOut, rightOut);
        CHECK(allFinite(leftOut));
        CHECK(allFinite(rightOut));
    }
    CHECK(peak(leftOut) > 0);
}

TEST_CASE("Filling a slot makes the host re-read the descriptions", "[clap]")
{
    // The names, the module paths and the hidden flags of a slot's parameters
    // all change when its effect does, and a host only learns that from
    // CLAP_PARAM_RESCAN_INFO. Without it the parameters keep the names they were
    // first read with -- "N/A" for a slot that was empty at startup -- which is
    // what a host shows however correct get_info would be if it were asked again.
    Entry const entry;
    TestHost host{{.params = true}};
    ActivePlugin plugin(48000, 512, host);

    auto const &params(parameters(*plugin));

    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), 0);
    params.flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());
    plugin.pumpMainThread();

    // Deferred, because a rescan is main-thread-only and flush() is not: the
    // plugin asks for a callback and does it there.
    CHECK(host.mainThreadCallbacks > 0);
    plugin->on_main_thread(&*plugin);

    CHECK((host.rescanFlags & CLAP_PARAM_RESCAN_INFO) != 0);
    CHECK((host.rescanFlags & CLAP_PARAM_RESCAN_TEXT) != 0);
    CHECK((host.rescanFlags & CLAP_PARAM_RESCAN_VALUES) != 0);

    /// \note And never RESCAN_ALL, which is the one a host may only be given
    /// while the plugin is deactivated. This one is active.
    CHECK((host.rescanFlags & CLAP_PARAM_RESCAN_ALL) == 0);

    // The names really did move, so the rescan had something to find.
    std::uint32_t named{0};
    for (auto const &info : allParameterInfo(*plugin, params))
        if (isNormalisedType(info.id) && (std::strncmp(info.module, "Slot 1", 6) == 0) &&
            (std::strcmp(info.name, "N/A") != 0))
            ++named;
    CHECK(named > 0);
}

TEST_CASE("A host with state and no thread check survives a parameter write", "[clap]")
{
    // `clap.thread-check` is optional; `clap.state` is what makes
    // markCurrentProgramAsModified() get as far as asking for it. Before the fix
    // this asserted in a checked build and dereferenced null in a shipping one,
    // on the path every automated parameter change takes.
    //
    /// \note State and nothing else, deliberately -- the combination is the point
    /// of the case. hostInteropTests.cpp is the same plugin against a host that
    /// *can* be asked, which is the other arm of the same branch.
    Entry const entry;
    TestHost host{{.state = true}};
    ActivePlugin plugin(48000, 512, host);

    auto const &params(parameters(*plugin));

    // From the main thread's side: params.flush() outside process().
    //
    /// \note And no `pumpMainThread()` after it, unlike every other raw
    /// `params.flush()` here. Pumping *is* `on_main_thread`, which is the thing
    /// this case measures the absence of -- one added here would discharge the
    /// deferral before the check below asks whether it is still pending, and the
    /// case would pass while proving nothing.
    ///                                       (06.08.2026.) (SW port)
    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), 0);
    params.flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());

    // And from the audio thread's: the same write, delivered in process().
    std::vector<float> leftIn(512, 0.0f), rightIn(512, 0.0f);
    std::vector<float> leftOut(512), rightOut(512);
    plugin.process(leftIn, rightIn, leftOut, rightOut);

    // Not marked yet -- it cannot be, since the plugin has no way to establish
    // that this is the main thread, and mark_dirty is main-thread-only.
    CHECK(host.dirtyMarks == 0);
    CHECK(host.mainThreadCallbacks > 0);

    // It is the callback that discharges it, which is what the deferral is for.
    plugin->on_main_thread(&*plugin);
    CHECK(host.dirtyMarks == 1);
}

TEST_CASE("A module parameter's range and step flag survive an effect swap", "[clap]")
{
    // The reason module and LFO parameters present a fixed 0..1 edge instead of
    // their effect's real range. ext/params.h puts min_value, max_value and the
    // is_stepped flag in the CLAP_PARAM_RESCAN_ALL list -- "can only be used
    // while the plugin is deactivated" -- and a slot's effect changes through an
    // ordinary parameter event, while active. So none of the three may move, for
    // any effect, ever.
    Entry const entry;
    ActivePlugin plugin(48000, 512);

    auto const &params(parameters(*plugin));
    auto const empty(allParameterInfo(*plugin, params));

    // Every effect the slot selector can hold, not just the first.
    clap_param_info selector{};
    bool foundSelector{false};
    for (auto const &info : empty)
        if (info.id == parameterID(moduleChainType, 0))
        {
            selector = info;
            foundSelector = true;
        }
    REQUIRE(foundSelector);

    // The selector itself is one of the parameters that keeps its real range, so
    // it is a discrete choice in a host's generic panel rather than a bare 0..1.
    CHECK((selector.flags & CLAP_PARAM_IS_STEPPED) != 0);
    CHECK(selector.max_value > selector.min_value);

    for (double effect(selector.min_value); effect <= selector.max_value; ++effect)
    {
        OneParameterEvent const swap(selector.id, effect);
        params.flush(&*plugin, &*swap, &discardedOutputEvents());
        plugin.pumpMainThread();

        auto const filled(allParameterInfo(*plugin, params));
        REQUIRE(filled.size() == empty.size());

        for (std::size_t index(0); index < filled.size(); ++index)
        {
            REQUIRE(filled[index].id == empty[index].id);
            CHECK(filled[index].min_value == empty[index].min_value);
            CHECK(filled[index].max_value == empty[index].max_value);
            CHECK((filled[index].flags & CLAP_PARAM_IS_STEPPED) ==
                  (empty[index].flags & CLAP_PARAM_IS_STEPPED));

            // And the edge those module parameters sit on is the unit interval.
            if (isNormalisedType(filled[index].id))
            {
                CHECK(filled[index].min_value == 0);
                CHECK(filled[index].max_value == 1);
                CHECK((filled[index].flags & CLAP_PARAM_IS_STEPPED) == 0);
            }
        }
    }
}

TEST_CASE("A normalised parameter round-trips through the host edge", "[clap]")
{
    // What the host writes is what the host reads back, even though the engine
    // stored it in the effect's own units in between.
    Entry const entry;
    ActivePlugin plugin(48000, 512);

    auto const &params(parameters(*plugin));

    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), 0);
    params.flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());
    plugin.pumpMainThread();

    /// \note Every module parameter the effect in slot 1 owns, not just the
    /// first: some of them are discrete underneath -- a bool, an enumeration --
    /// and a 0..1 edge cannot represent that. Writing 0.25 to a boolean and
    /// reading back 0 is correct behaviour, not a round-trip failure.
    ///
    ///   So the property asserted for all of them is the weaker one a host
    /// actually relies on: whatever comes back is *stable*. Write it again and it
    /// does not drift. Exactness is then asserted separately, over the
    /// continuous ones, so that "everything snapped to zero" cannot pass.
    std::uint32_t stable{0}, exact{0};
    for (auto const &info : allParameterInfo(*plugin, params))
    {
        if (!isNormalisedType(info.id))
            continue;

        /// \note "N/A" is how a parameter no effect owns reads, and skipping
        /// those is what the CLAP_PARAM_IS_HIDDEN flag used to do here before it
        /// stopped being set. The display is the better test of it anyway: it is
        /// what a *user* is shown, rather than a flag their host may have read
        /// once and cached.
        std::array<char, CLAP_NAME_SIZE> owned{};
        REQUIRE(params.value_to_text(&*plugin, info.id, 0.5, owned.data(), owned.size()));
        if (std::strncmp(owned.data(), "N/A", 3) == 0)
            continue;

        constexpr double wanted{0.25};
        OneParameterEvent const edit(info.id, wanted);
        params.flush(&*plugin, &*edit, &discardedOutputEvents());
        plugin.pumpMainThread();

        double read{-1};
        REQUIRE(params.get_value(&*plugin, info.id, &read));
        CHECK(read >= 0);
        CHECK(read <= 1);

        OneParameterEvent const again(info.id, read);
        params.flush(&*plugin, &*again, &discardedOutputEvents());
        plugin.pumpMainThread();

        double reread{-1};
        REQUIRE(params.get_value(&*plugin, info.id, &reread));
        CAPTURE(info.id, info.name);
        CHECK_THAT(reread, Catch::Matchers::WithinAbs(read, 1e-4));
        ++stable;

        if (std::abs(read - wanted) < 1e-4)
            ++exact;
    }
    CHECK(stable > 0);
    CHECK(exact > 0);
}

TEST_CASE("A normalised parameter still reads in the effect's own units", "[clap]")
{
    // Normalising the range is only tolerable because the *text* is not
    // normalised: a host showing 0.25 also shows what 0.25 means. This is where
    // the enumerated module parameters keep their names, too, now that they can
    // no longer advertise a step count.
    //
    /// \note What is checked here is that the text is in the effect's units.
    /// That it is the *asked-about* value's text rather than the parameter's own
    /// is parameterTextTests.cpp's, which is where the pair of them is held to
    /// being each other's inverse.
    ///
    ///   The edge asked about is 1.0, the top of the normalised range, and that
    /// is what makes the count below mean something: a parameter converting into
    /// its own units reads as its maximum in those units, so a text of "1" says
    /// nothing was converted.
    Entry const entry;
    ActivePlugin plugin(48000, 512);

    auto const &params(parameters(*plugin));

    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), 0);
    params.flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());
    plugin.pumpMainThread();

    std::uint32_t checked{0}, differedFromTheEdge{0};
    for (auto const &info : allParameterInfo(*plugin, params))
    {
        if (!isNormalisedType(info.id))
            continue;

        std::array<char, 128> text{};
        REQUIRE(params.value_to_text(&*plugin, info.id, 1.0, text.data(), text.size()));
        CAPTURE(info.id, info.name, info.module);
        CHECK(text[0] != '\0');

        /// \note A parameter no effect owns reads as "N/A" -- there are no units
        /// to convert into. What used to filter those out here was
        /// CLAP_PARAM_IS_HIDDEN, which is no longer set on anything.
        if (std::strncmp(text.data(), "N/A", 3) == 0)
            continue;

        ++checked;

        // Some parameter in the slot has to read as something other than a bare
        // edge value, or nothing is being converted into the effect's units at
        // all -- which is the whole justification for a 0..1 range.
        if (std::strncmp(text.data(), "1", 2) != 0)
            ++differedFromTheEdge;
    }
    CHECK(checked > 0);
    CHECK(differedFromTheEdge > 0);
}

TEST_CASE("Silence in is silence out", "[clap]")
{
    constexpr std::uint32_t blockSize{512};

    Entry const entry;
    ActivePlugin plugin(48000, blockSize);

    std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);

    for (unsigned int block(0); block < 8; ++block)
    {
        // Deliberately dirty, so that "silence out" means the engine wrote it
        // rather than that nobody touched the buffer.
        std::fill(leftOut.begin(), leftOut.end(), 0.5f);
        std::fill(rightOut.begin(), rightOut.end(), 0.5f);

        plugin.process(leftIn, rightIn, leftOut, rightOut);

        CHECK(peak(leftOut) < 1.0e-6f);
        CHECK(peak(rightOut) < 1.0e-6f);
    }
}

////////////////////////////////////////////////////////////////////////////////
// LFO timing
//
//   All three of these failed before SpectrumWorxCLAP::updateLFOTiming() existed.
// The engine's LFO clock only ever moved in SpectrumWorxSharedImpl::process(),
// the 2016 host layer the CLAP does not inherit, so an enabled LFO answered with
// its value at position 0 for the plugin's lifetime -- which is what "setting up
// an LFO does not modulate anything" was.
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("An enabled LFO modulates when the host reports no transport", "[clap][lfo]")
{
    // The standalone, and any host that does not fill in transport. There is no
    // song position to follow, so the block length has to move the clock.
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{512};
    // ~0.85 of a bar at the engine's assumed 120 BPM 4/4, so a default one-bar
    // sine sweeps most of its range whatever the parameter's resolution.
    constexpr unsigned int blocks{160};

    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);

    CHECK(distinctModulatedValues(plugin, parameters(*plugin), sampleRate, blockSize, blocks,
                                  nullptr) > 1);
}

TEST_CASE("An edit made in the interface reaches the engine through the queue", "[clap][protocol]")
{
    // What the editor does when a knob moves: push a command and ask the host to
    // collect. It used to write the engine on the message thread, in the middle of
    // whatever process() was doing with it.
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{512};
    constexpr std::uint8_t gainIndex{1};

    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);
    auto const &params(parameters(*plugin));

    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), 0);
    params.flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());
    plugin.pumpMainThread();

    auto const &info(moduleParameterInfo(*plugin, 0, gainIndex));
    auto const before(liveModuleParameter(*plugin, 0, gainIndex));
    auto const wanted(info.minimum + ((info.maximum - info.minimum) * 0.25f));
    REQUIRE(wanted != before);

    /// \note `editParameter()`, not a bare `toEngine().push()`. The push alone is
    /// what the editor used to do and it moves the engine only; the host reads
    /// `programMain_`, so the last assertion here was asking one copy about a
    /// write made to the other. One call is the whole edit -- see the note on
    /// `EditorHost::editParameter`.
    ///                                       (06.08.2026.) (SW port)
    editorHostOf(*plugin).editParameter(
        LE::SW::ParameterID{LE::Plugins::ParameterID{modulatedParameterID(0, 0)}}, wanted);

    // Nothing yet: the engine belongs to the audio thread, and no block or flush
    // has run. That is the point -- a queue that applied itself on push would be
    // the direct write with extra steps.
    CHECK(liveModuleParameter(*plugin, 0, gainIndex) == before);

    // ...and a flush is what applies it. A host reaches this through
    // clap_host_params::request_flush, which the editor asks for after every edit.
    params.flush(&*plugin, &noInputEvents(), &discardedOutputEvents());
    plugin.pumpMainThread();
    CHECK_THAT(liveModuleParameter(*plugin, 0, gainIndex),
               Catch::Matchers::WithinAbs(wanted, 1e-3));

    // And the host sees the same thing, because it is the unmodulated value.
    double hostValue{0};
    REQUIRE(params.get_value(&*plugin, modulatedParameterID(0, 0), &hostValue));
    CHECK_THAT(hostValue, Catch::Matchers::WithinAbs(
                              (wanted - info.minimum) / (info.maximum - info.minimum), 1e-3));
}

TEST_CASE("What the LFOs did reaches the interface through the mailbox", "[clap][protocol][lfo]")
{
    // The other half of severing the engine from the interface. A module used to
    // push each LFO value into a juce::Slider itself, from inside preProcess() --
    // the stack in doc/tech/threading_model.md §1. The plugin publishes the
    // same information after the block instead, into a slot the newest write
    // overwrites, and whatever draws reads it on its own thread at its own rate.
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{512};
    constexpr std::uint8_t gainIndex{1};

    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);
    auto const &params(parameters(*plugin));

    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), 0);
    params.flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());
    plugin.pumpMainThread();

    auto const &mailbox(editorHostOf(*plugin).modulatedValues());
    auto const target(LE::SW::parameterIndexFromBinaryID(modulatedParameterID(0, 0)));

    std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);

    // With no LFO enabled there is nothing to publish, however many blocks run.
    for (unsigned block(0); block < 8; ++block)
        plugin.process(leftIn, rightIn, leftOut, rightOut);
    {
        unsigned changes(0);
        mailbox.forEachChanged([&](std::size_t, float) { ++changes; });
        CHECK(changes == 0);
    }

    OneParameterEvent const enable(lfoParameterID(0, 0, lfoEnabled), 1);
    params.flush(&*plugin, &*enable, &discardedOutputEvents());
    plugin.pumpMainThread();

    std::set<float> published;
    for (unsigned block(0); block < 160; ++block)
    {
        plugin.process(leftIn, rightIn, leftOut, rightOut);
        mailbox.forEachChanged([&](std::size_t const index, float const value) {
            if (index == target)
                published.insert(value);
        });
    }

    // It swept, and the last thing published is what the DSP is actually using.
    CHECK(published.size() > 1);
    CHECK(mailbox.value(target) == liveModuleParameter(*plugin, 0, gainIndex));

    // ...and a sweep that finds nothing new reports nothing, which is what lets a
    // 30 Hz repaint sit on top of a 1500 Hz producer without doing 1500 Hz of work.
    unsigned changesWithoutABlock(0);
    mailbox.forEachChanged([&](std::size_t, float) { ++changesWithoutABlock; });
    CHECK(changesWithoutABlock == 0);
}

TEST_CASE("An LFO sweeps the DSP and not what the host reads", "[clap][lfo]")
{
    // The base/modulated split, stated where a host would notice it. Before it,
    // the LFO wrote through the same setter a user does: get_value() polled the
    // sweep, so a generic panel's slider jittered on its own and saving a preset
    // froze the LFO's instantaneous output as the parameter's value.
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{512};
    constexpr unsigned int blocks{160};

    Entry const entry;

    {
        ActivePlugin plugin(sampleRate, blockSize);
        CHECK(distinctModulatedValues(plugin, parameters(*plugin), sampleRate, blockSize, blocks,
                                      nullptr) > 1);
    }
    {
        ActivePlugin plugin(sampleRate, blockSize);
        CHECK(distinctHostVisibleValues(plugin, parameters(*plugin), sampleRate, blockSize, blocks,
                                        nullptr) == 1);
    }
}

TEST_CASE("With no transport the LFO clock is 120 BPM in four four", "[clap][lfo]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The assumed tempo, stated rather than described. A host that
    /// reports no transport -- the standalone is one -- leaves every LFO free
    /// running against this, and it is a defined answer rather than a failure:
    /// there used to be a dialog on loading any preset with a tempo-synced LFO
    /// into such a host, saying so. The 2016 sources already record it firing
    /// spuriously in Live.
    ///
    ///   Pinned because the removal of that dialog rests on it. If the assumed
    /// tempo ever stops being 120 BPM 4/4, a preset written against note ratios
    /// plays at some other rate and nothing says anything at all.
    ///                                       (02.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{512};

    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);

    std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);
    plugin.process(leftIn, rightIn, leftOut, rightOut, nullptr /*no transport at all*/);

    using Timer = LE::Parameters::LFOImpl::Timer;
    // One bar of four beats at 120 BPM is two seconds.
    CHECK_THAT(Timer::basePeriod(), Catch::Matchers::WithinAbs(2.0, 1e-6));
    CHECK(Timer::measureNumerator() == 4);
}

TEST_CASE("An enabled LFO keeps running while the transport is stopped", "[clap][lfo]")
{
    // A parked transport reports the same song position every block, so following
    // it would freeze every LFO on the rack -- which is not what auditioning a
    // patch without pressing play should do. Six Sines and surge-xt2 both keep
    // the LFO moving here, and so does this: the host's tempo and meter are kept,
    // and the phase is carried forward by the block length.
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{512};
    constexpr unsigned int blocks{160};

    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);

    auto const stopped(transportAt(120, 8 /*bar 3*/, 0 /*not CLAP_TRANSPORT_IS_PLAYING*/));
    CHECK(distinctModulatedValues(plugin, parameters(*plugin), sampleRate, blockSize, blocks,
                                  &stopped) > 1);
}

TEST_CASE("A playing transport drives the LFO from song position", "[clap][lfo]")
{
    // The case the other two stand in for: the host is playing and the LFO is
    // phase-locked to the song rather than free-running alongside it.
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{512};

    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);
    auto const &params(parameters(*plugin));

    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), 0);
    params.flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());
    plugin.pumpMainThread();

    OneParameterEvent const enable(lfoParameterID(0, 0, lfoEnabled), 1);
    params.flush(&*plugin, &*enable, &discardedOutputEvents());
    plugin.pumpMainThread();

    std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);

    /// \brief What the LFO reads with the song parked at \p beats.
    ///
    /// \note One block per position, and the position is what the host says
    /// rather than what the last block left behind -- so a repeat has to read the
    /// same as its first visit, which a free-running clock could not manage.
    ///
    /// \note The live parameter, not `get_value`: an LFO modulates and the value
    /// a host reads is deliberately not modulated. See liveModuleParameter().
    auto const valueAtBeat([&](double const beats) {
        auto const playing(transportAt(120, beats, CLAP_TRANSPORT_IS_PLAYING));
        plugin.process(leftIn, rightIn, leftOut, rightOut, &playing);
        return liveModuleParameter(*plugin, 0, 1 /*Gain*/);
    });

    // A default LFO is a one-bar sine, so bar starts agree and mid-bar does not.
    auto const barZero(valueAtBeat(0));
    auto const midBar(valueAtBeat(2));
    auto const barOne(valueAtBeat(4));

    CHECK(midBar != barZero);
    CHECK(barOne == barZero);
}

////////////////////////////////////////////////////////////////////////////////
//
// Meters other than four four
// ---------------------------
//
//   Issue #14: `SWTest::transportAt()` hardcoded `tsig_num = 4`, so every case
// that drove a transport at all drove the one meter in which a beat is a quarter
// of a bar. Two things go unmeasured that way -- the bar the clock counts, and
// the grid a synced period snaps to -- and the second of them is a *stored,
// host-visible parameter* that `updateForNewTimingInformation()` will move.
//
//   `tests/parameters/lfoTests.cpp` has the unit half, over four meters and both
// directions of the change. These two are the same claims through the plugin,
// where the meter arrives as a `clap_event_transport` rather than as an argument.
//
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The LFO clock follows the host into three four, six eight and five four", "[clap][lfo]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note **The denominator reaches nothing.** `updateLFOTiming()` builds the
    /// bar out of `tsig_num` beats of `60 / tempo` seconds each, and CLAP's tempo
    /// is quarter notes per minute -- so six eight arrives as six beats to the
    /// bar and its bar is three seconds at 120 BPM, where a musician counting it
    /// would say a second and a half. The numbers below are what the engine does
    /// today rather than an endorsement of it; three four and five four, whose
    /// beat *is* a quarter note, have no such question about them.
    ///
    ///   It matters less than it looks: what the meter decides is the LFO grid,
    /// and six divisions of a bar is what a bar of six eight has however long the
    /// bar is. A host in six eight gets the right *ratios* and a bar of twice the
    /// length.
    ///
    ////////////////////////////////////////////////////////////////////////////
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{512};
    constexpr double tempo{120};

    Entry const entry;

    struct Meter
    {
        std::uint16_t beatsPerBar;
        std::uint16_t beatUnit;
    };

    for (auto const meter : {Meter{3, 4}, Meter{6, 8}, Meter{5, 4}})
    {
        auto const beatsPerBar(meter.beatsPerBar);
        CAPTURE(beatsPerBar, meter.beatUnit);

        ActivePlugin plugin(sampleRate, blockSize);
        auto const &params(parameters(*plugin));

        OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), 0);
        params.flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());
        plugin.pumpMainThread();

        OneParameterEvent const enable(lfoParameterID(0, 0, lfoEnabled), 1);
        params.flush(&*plugin, &*enable, &discardedOutputEvents());
        plugin.pumpMainThread();

        std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
        std::vector<float> leftOut(blockSize), rightOut(blockSize);

        /// \brief What the LFO reads with the song parked at \p beats, in this
        /// meter. \see the four four case above, which this is the shape of.
        auto const valueAtBeat([&](double const beats) {
            auto const playing(transportAt(tempo, beats, CLAP_TRANSPORT_IS_PLAYING,
                                           meter.beatsPerBar, meter.beatUnit));
            plugin.process(leftIn, rightIn, leftOut, rightOut, &playing);
            return liveModuleParameter(*plugin, 0, 1 /*Gain*/);
        });

        // A default LFO is a one-bar sine, and a bar is as many beats as the
        // host says -- three, six, five -- rather than always four.
        auto const barLine(valueAtBeat(0));
        auto const midBar(valueAtBeat(beatsPerBar / 2.0));
        auto const nextBarLine(valueAtBeat(beatsPerBar));

        CHECK(midBar != barLine);
        CHECK(nextBarLine == barLine);

        // ...and the clock the rest of the engine reads says the same thing.
        using Timer = LE::Parameters::LFOImpl::Timer;
        CHECK(Timer::measureNumerator() == beatsPerBar);
        CHECK_THAT(Timer::basePeriod(), Catch::Matchers::WithinAbs(beatsPerBar * 60 / tempo, 1e-6));

        /// \note And back to the assumed 120 BPM 4/4 before the next meter, which
        /// is what a block with no transport at all *is*. `Timer`'s tempo and
        /// meter are process-wide statics -- issue #11 -- so a case that left five
        /// four behind would change what every later case in the binary measures.
        plugin.process(leftIn, rightIn, leftOut, rightOut, nullptr);
    }

    CHECK(LE::Parameters::LFOImpl::Timer::measureNumerator() == 4);
}

TEST_CASE("A host that opens in five four does not move the period it was given", "[clap][lfo]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Issue #14's own paragraph, through the plugin: the meter half of
    /// `Timer::establishedChange()` was reasoned rather than measured. A session
    /// that opens in five four is not a host *changing* the meter, so the stored
    /// period stays where the user left it -- and a genuine change, later in the
    /// same session, resnaps it.
    ///
    /// \note A quarter of a bar is the period to store, because it is on four
    /// four's grid and on neither of the two below: five four has a beat and a
    /// bar and nothing between them, six eight has a third. So a resnap that
    /// should not happen is visible and one that should has somewhere to go.
    ///
    ////////////////////////////////////////////////////////////////////////////
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{512};
    constexpr double tempo{120};
    constexpr float quarterOfABar{0.25f};

    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);
    auto const &params(parameters(*plugin));

    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), 0);
    params.flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());
    plugin.pumpMainThread();

    /// \note The editor's route, which is how a user sets a period: applied to the
    /// main thread's Program and queued for the engine, the flush below being what
    /// lands it there. Written *before any block has run*, so the plugin has never
    /// seen a transport and the snapping this write does is against the engine's
    /// assumed four four -- which the REQUIRE is the check of, not an aside.
    editorHostOf(*plugin).editParameter(
        LE::SW::ParameterID{LE::Plugins::ParameterID{lfoParameterID(0, 0, lfoPeriodScale)}},
        quarterOfABar);
    params.flush(&*plugin, &noInputEvents(), &discardedOutputEvents());
    plugin.pumpMainThread();
    REQUIRE_THAT(engineLFOPeriodScale(*plugin, 0, 0),
                 Catch::Matchers::WithinAbs(quarterOfABar, 1e-6));

    double hostVisibleBefore{-1};
    REQUIRE(params.get_value(&*plugin, lfoParameterID(0, 0, lfoPeriodScale), &hostVisibleBefore));

    std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);

    // The first block this plugin ever processes, and the host is in five four.
    auto const inFiveFour(transportAt(tempo, 0, CLAP_TRANSPORT_IS_PLAYING, 5, 4));
    plugin.process(leftIn, rightIn, leftOut, rightOut, &inFiveFour);

    REQUIRE(LE::Parameters::LFOImpl::Timer::measureNumerator() == 5);
    CHECK_THAT(engineLFOPeriodScale(*plugin, 0, 0),
               Catch::Matchers::WithinAbs(quarterOfABar, 1e-6));

    // ...and more of the same meter is still not a change.
    for (unsigned block(0); block < 4; ++block)
    {
        auto const later(transportAt(tempo, block + 1.0, CLAP_TRANSPORT_IS_PLAYING, 5, 4));
        plugin.process(leftIn, rightIn, leftOut, rightOut, &later);
    }
    CHECK_THAT(engineLFOPeriodScale(*plugin, 0, 0),
               Catch::Matchers::WithinAbs(quarterOfABar, 1e-6));

    // A meter change in the middle of the session is one, and six eight has a
    // third of a bar for a quarter of one to land on.
    auto const inSixEight(transportAt(tempo, 5, CLAP_TRANSPORT_IS_PLAYING, 6, 8));
    plugin.process(leftIn, rightIn, leftOut, rightOut, &inSixEight);

    REQUIRE(LE::Parameters::LFOImpl::Timer::measureNumerator() == 6);
    CAPTURE(engineLFOPeriodScale(*plugin, 0, 0));
    CHECK_THAT(engineLFOPeriodScale(*plugin, 0, 0), Catch::Matchers::WithinAbs(1.0 / 3, 1e-4));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note **And what the host reads is still the period before the resnap.**
    /// `Processor::updateModuleLFOs()` walks the modules the *audio thread* owns;
    /// `programMain_` -- which is what `paramsValue` and `stateSave` answer from
    /// -- is not among them, and nothing else resnaps it. So a meter change
    /// leaves the two copies holding two different periods, and it is the
    /// engine's that is heard.
    ///
    ///   Pinned as what the code does rather than asserted as what it should do.
    /// It is arguably wrong in both directions -- a session saved after a meter
    /// change stores a period the meter cannot hold, and the LFO panel draws that
    /// stored one rather than the one running -- and pinning it is what makes a
    /// decision about it visible when somebody makes one. \see
    /// `doc/tech/threading_model.md` on the two copies, and
    /// `doc/tech/how-lfo-rates-work.md` §4 on what a meter does to a stored
    /// period.
    ///
    ////////////////////////////////////////////////////////////////////////////
    double hostVisibleAfter{-1};
    REQUIRE(params.get_value(&*plugin, lfoParameterID(0, 0, lfoPeriodScale), &hostVisibleAfter));
    CAPTURE(hostVisibleBefore, hostVisibleAfter);
    CHECK(hostVisibleAfter == hostVisibleBefore);

    // Back to the assumed 120 BPM 4/4 for whatever runs next; see the case above.
    plugin.process(leftIn, rightIn, leftOut, rightOut, nullptr);
    CHECK(LE::Parameters::LFOImpl::Timer::measureNumerator() == 4);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The acceptance test for doc/tech/threading_model.md, and it looks
/// like an ordinary functional case on purpose. Goal 4 is "with the UI open you
/// can change params and not trigger rtsan at all"; what says so is *this run*,
/// built with `-fsanitize=realtime`, where every allocation, lock and syscall
/// inside `process()` is a failure with a stack. In an ordinary build it still
/// earns its place -- it is the only case that drives a full rack with every
/// LFO running and an editor attached.
///
///   Everything it does is what a user does: fill the five slots, open the
/// window, turn on an LFO per slot, and move knobs while audio runs. The edits go
/// through the queue, which is the route a knob drag takes.
///
/// \note The slots are filled through the *host's* parameter route, which is the
/// one remaining path that allocates inside the callback -- the granted
/// concession, recorded in issue #9. So they are filled before the measured
/// blocks, not during them, and a `SW_SANITIZER=realtime` run that reports
/// `ModuleFactory::create` here would be reporting a known one.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A full rack with LFOs running and an editor open processes cleanly", "[clap][realtime]")
{
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{512};
    constexpr unsigned int blocks{64};
    constexpr std::uint8_t slots{LE::SW::Constants::maxNumberOfModules};

    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);
    auto const &params(parameters(*plugin));

    // Five slots, five different effects, and one LFO each.
    for (std::uint8_t slot(0); slot < slots; ++slot)
    {
        OneParameterEvent const fill(parameterID(moduleChainType, slot), slot / 64.0);
        params.flush(&*plugin, &*fill, &discardedOutputEvents());
        plugin.pumpMainThread();

        OneParameterEvent const enable(lfoParameterID(slot, 0, lfoEnabled), 1);
        params.flush(&*plugin, &*enable, &discardedOutputEvents());
        plugin.pumpMainThread();
    }

    /// \note The editor, built the way the shim builds it. Its presence is the
    /// point: `pEditor_` gates the ToUI pushes, and the mailbox has a reader.
    juce::ScopedJuceInitialiser_GUI const juceIsUp;
    auto editor(std::make_unique<LE::SW::GUI::SpectrumWorxEditor>(editorHostOf(*plugin)));

    std::vector<float> leftIn(blockSize), rightIn(blockSize);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);

    auto const playing(transportAt(120, 0, CLAP_TRANSPORT_IS_PLAYING));

    for (unsigned block(0); block < blocks; ++block)
    {
        // A knob moved, on a different slot each block, the way the editor sends
        // one: a base value, in the parameter's own units, through the queue.
        auto const slot(static_cast<std::uint8_t>(block % slots));
        constexpr std::uint8_t wetIndex{2};
        auto const &info(moduleParameterInfo(*plugin, slot, wetIndex));
        auto const fraction((block % 8) / 8.0f);
        editorHostOf(*plugin).toEngine().push(LE::SW::Threading::setBaseParameter(
            parameterID(moduleType, slot, wetIndex << 8),
            info.minimum + fraction * (info.maximum - info.minimum)));

        fillWithSine(leftIn, sampleRate, 440.0f, block * blockSize);
        rightIn = leftIn;
        plugin.process(leftIn, rightIn, leftOut, rightOut, &playing);

        REQUIRE(std::isfinite(peak(leftOut)));
        REQUIRE(std::isfinite(peak(rightOut)));
    }

    /// \note The slots were filled by *host* parameter events, so the engine
    /// applied them on the audio thread and echoed them to the main thread; this
    /// is the host running the callback that lands them in the Program the editor
    /// draws from. Without it the rack is legitimately empty -- the two copies are
    /// one drain apart, which is the model rather than a fault.
    plugin.pumpMainThread();

    // The rack is what was asked for, and the sweeps reached the interface.
    CHECK(editor->moduleChain().size() == slots);

    editor.reset();
}

////////////////////////////////////////////////////////////////////////////////
//
// The editor's window
// -------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   The preset browser and the settings panel used to be drawn over the module
/// strips, because a 563 x 376 editor has nowhere else to put them. They get a
/// column of their own now, and the only way a plugin can have one is to ask the
/// host for it -- `clap_host_gui::request_resize`.
///
/// \note So this is the case that goes all the way through: an editor built the
/// way the shim builds it, over a real SpectrumWorxCLAP, over a host that answers
/// `clap.gui`. `tests/gui/overlayPanelTests.cpp` has what the editor does with
/// the answer; what nothing there can see is whether the request reaches a
/// `clap_host_gui` at all, because its harness *is* the thing being asked.
///
/// \note All three ask for `expandContract` rather than taking the default, which
/// is `alwaysVisible` and takes its column in the constructor -- so the shipping
/// arrangement asks the host for nothing after the first `guiGetSize`, and these
/// would have nothing to measure.
///                                           (06.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Opening a panel asks the host for a wider window", "[clap][gui]")
{
    using Editor = LE::SW::GUI::SpectrumWorxEditor;

    Entry const entry;
    TestHost host{{.threadCheck = true, .log = true, .gui = true}};
    ActivePlugin plugin(48000, 512, host);

    juce::ScopedJuceInitialiser_GUI const juceIsUp;
    auto editor(
        std::make_unique<Editor>(editorHostOf(*plugin), Editor::PanelPlacement::expandContract));

    // Nothing asked for by existing: the editor opens at the size the shim reads
    // off it, and `guiGetSize` is how the host learns that one.
    REQUIRE(host.resizeRequests.empty());
    REQUIRE(editor->getWidth() == Editor::estimatedWidth);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Two units, deliberately. The editor lays itself out in skin pixels
    /// and asks in them -- it does not know it is being drawn zoomed -- and
    /// SpectrumWorxCLAP scales on the way out, because a size crossing into the
    /// host is in window units. So `getWidth()` below is the skin's number and
    /// what the host was told is that number through ZoomedEditor::scaled().
    /// The editor this case builds is bare, at 1:1, which is the point: the
    /// zoom is the wrapper's and the editor is the same either way.
    ///
    ////////////////////////////////////////////////////////////////////////////
    using Zoomed = LE::SW::GUI::ZoomedEditor;

    editor->showPresetBrowser(true);
    REQUIRE(host.resizeRequests.size() == 1);
    CHECK(host.resizeRequests.back() ==
          std::pair<std::uint32_t, std::uint32_t>{Zoomed::scaled(Editor::expandedWidth),
                                                  Zoomed::scaled(Editor::estimatedHeight)});
    CHECK(editor->getWidth() == Editor::expandedWidth);

    editor->showPresetBrowser(false);
    REQUIRE(host.resizeRequests.size() == 2);
    CHECK(host.resizeRequests.back() ==
          std::pair<std::uint32_t, std::uint32_t>{Zoomed::scaled(Editor::estimatedWidth),
                                                  Zoomed::scaled(Editor::estimatedHeight)});
    CHECK(editor->getWidth() == Editor::estimatedWidth);

    editor.reset();

    /// \note `request_resize` is `[main-thread]` and clap-helpers checks it the
    /// moment a host answers the thread check, so this is also what says the
    /// editor asks from where it is allowed to.
    CHECK(host.misbehaviours().empty());
}

TEST_CASE("A host with no clap.gui still gets an editor", "[clap][gui]")
{
    /// \note The extension is optional, and a plugin may not assume it. Without
    /// it `requestEditorSize` answers no without calling anything, and the editor
    /// lays its panel over the module strips -- which is stage 6.4's arrangement,
    /// so what a host that withholds `clap.gui` gets is the editor as it shipped.
    using Editor = LE::SW::GUI::SpectrumWorxEditor;

    Entry const entry;
    TestHost host{{.threadCheck = true, .log = true}};
    ActivePlugin plugin(48000, 512, host);

    juce::ScopedJuceInitialiser_GUI const juceIsUp;
    auto editor(
        std::make_unique<Editor>(editorHostOf(*plugin), Editor::PanelPlacement::expandContract));

    editor->showPresetBrowser(true);
    CHECK(host.resizeRequests.empty());
    CHECK_FALSE(editor->panelHasOwnColumn());
    CHECK(editor->getWidth() == Editor::estimatedWidth);

    editor.reset();
    CHECK(host.misbehaviours().empty());
}

TEST_CASE("A host that refuses leaves the editor the size it was", "[clap][gui]")
{
    using Editor = LE::SW::GUI::SpectrumWorxEditor;

    Entry const entry;
    TestHost host{{.threadCheck = true, .log = true, .gui = true}};
    host.grantResizes = false;
    ActivePlugin plugin(48000, 512, host);

    juce::ScopedJuceInitialiser_GUI const juceIsUp;
    auto editor(
        std::make_unique<Editor>(editorHostOf(*plugin), Editor::PanelPlacement::expandContract));

    editor->showPresetBrowser(true);
    // It asked once and took no for an answer, rather than drawing itself wider
    // than the window it is in.
    CHECK(host.resizeRequests.size() == 1);
    CHECK_FALSE(editor->panelHasOwnColumn());
    CHECK(editor->getWidth() == Editor::estimatedWidth);

    editor.reset();
    CHECK(host.misbehaviours().empty());
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Two instances, two real audio threads, one message thread -- the
/// arrangement the two-instance deadlock was reported in, and the one every case
/// above deliberately avoids: they call `process()` on the thread that also
/// drives the editor, so the two never actually overlap.
///
///   `REQUIRE` allocates, so the worker threads record rather than assert; the
/// process loop is what is being measured and it must be what a host runs. Under
/// `SW_SANITIZER=thread` this is the acceptance test for goal 1, and under
/// `realtime` it is a second run of the callback with a window opening and
/// closing underneath it.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Two instances process while their editors come and go", "[clap][threading]")
{
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{256};
    constexpr unsigned int blocks{400};

    Entry const entry;
    juce::ScopedJuceInitialiser_GUI const juceIsUp;

    ActivePlugin first(sampleRate, blockSize);
    ActivePlugin second(sampleRate, blockSize);

    // A module apiece, so there is a chain to walk and an LFO to publish.
    for (auto *pPlugin : {&first, &second})
    {
        auto const &params(parameters(**pPlugin));
        OneParameterEvent const fill(parameterID(moduleChainType, 0), 0);
        params.flush(&**pPlugin, &*fill, &discardedOutputEvents());
        (*pPlugin).pumpMainThread();
        OneParameterEvent const enable(lfoParameterID(0, 0, lfoEnabled), 1);
        params.flush(&**pPlugin, &*enable, &discardedOutputEvents());
        (*pPlugin).pumpMainThread();
    }

    std::atomic<bool> stop{false};
    std::atomic<unsigned int> blocksRun{0};
    std::atomic<bool> sawNonFinite{false};

    auto const audioThread([&](ActivePlugin &plugin) {
        std::vector<float> leftIn(blockSize), rightIn(blockSize);
        std::vector<float> leftOut(blockSize), rightOut(blockSize);
        auto const playing(transportAt(120, 0, CLAP_TRANSPORT_IS_PLAYING));

        for (unsigned block(0); !stop.load(std::memory_order_acquire); ++block)
        {
            fillWithSine(leftIn, sampleRate, 440.0f, block * blockSize);
            rightIn = leftIn;
            if (plugin.processStatus(leftIn, rightIn, leftOut, rightOut, &playing) ==
                CLAP_PROCESS_ERROR)
                sawNonFinite.store(true, std::memory_order_release);
            if (!std::isfinite(peak(leftOut)))
                sawNonFinite.store(true, std::memory_order_release);
            blocksRun.fetch_add(1, std::memory_order_acq_rel);
        }
    });

    std::thread firstAudio(audioThread, std::ref(first));
    std::thread secondAudio(audioThread, std::ref(second));

    /// \note What the message thread does meanwhile: opens and closes windows and
    /// moves knobs. Closing one used to tear down the MessageManager the other
    /// was running on (§1B), and a knob used to write engine state directly.
    for (unsigned round(0); round < 8; ++round)
    {
        auto firstEditor(std::make_unique<LE::SW::GUI::SpectrumWorxEditor>(editorHostOf(*first)));
        auto secondEditor(std::make_unique<LE::SW::GUI::SpectrumWorxEditor>(editorHostOf(*second)));

        constexpr std::uint8_t wetIndex{2};
        auto const &info(moduleParameterInfo(*first, 0, wetIndex));
        auto const wanted(info.minimum + ((round % 4) / 4.0f) * (info.maximum - info.minimum));
        editorHostOf(*first).toEngine().push(
            LE::SW::Threading::setBaseParameter(parameterID(moduleType, 0, wetIndex << 8), wanted));
        editorHostOf(*second).toEngine().push(
            LE::SW::Threading::setBaseParameter(parameterID(moduleType, 0, wetIndex << 8), wanted));

        firstEditor.reset();
        secondEditor.reset();
    }

    // Enough blocks that the two really did overlap the eight window cycles.
    while (blocksRun.load(std::memory_order_acquire) < blocks)
    {
    }
    stop.store(true, std::memory_order_release);
    firstAudio.join();
    secondAudio.join();

    CHECK(!sawNonFinite.load());
    CHECK(juce::MessageManager::getInstanceWithoutCreating() != nullptr);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief A preset load has to reach the interface without the audio thread's
/// help.
///
///   Reported against the AudioUnit in Logic: with the transport stopped,
/// picking a preset in the browser left the old strips on screen. The same build
/// as a CLAP in Bitwig was fine, which is the clue -- Bitwig's engine runs
/// whether or not anything is playing, and Logic stops calling an AU's render
/// callback on a track that is neither playing nor monitored.
///
///   That is enough to describe the whole fault. `GUI::loadPreset` fills the main
/// thread's Program itself and *queues* the engine's copy, and nothing then told
/// the editor. The one thing that ever did was the chain's echo coming back the
/// other way: `drainCommands()` installs the queued chain, calls `chainChanged()`,
/// and `drainEngineEvents()` resyncs the rack when it sees it -- a round trip
/// through a thread that, in this host, was not running. So the rack showed the
/// previous preset until something made a block of audio happen.
///
/// \note What the case pins is the *trigger*, not the resync. Every other case
/// here runs `resyncModuleRack()` by hand because a headless build has no message
/// loop, and doing so would hide exactly this bug: the rack would come out right
/// either way. \see SpectrumWorxEditor::rackResyncRequests().
///                                           (06.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A preset reaches the rack with no audio thread running", "[clap][presets][gui]")
{
    using Editor = LE::SW::GUI::SpectrumWorxEditor;

    Entry const entry;
    juce::ScopedJuceInitialiser_GUI const juceIsUp;

    /// \note Active, which is the whole point: an *inactive* plugin installs a
    /// published chain on the calling thread (`Threading::publishChain`) and the
    /// interface would follow it for free. Logic's plugin is activated and simply
    /// never rendered.
    ActivePlugin plugin(48000, 512);
    auto &host(editorHostOf(*plugin));
    auto editor(std::make_unique<Editor>(host, Editor::PanelPlacement::overlay));

    // Something on screen first, so that "the rack never changed" and "the rack
    // was empty all along" are not the same picture.
    editor->addUserAddedModule(0);
    editor->resyncModuleRack();
    REQUIRE(editor->moduleChain().size() == 1);
    REQUIRE(editor->regionInSlot(0) != nullptr);

    /// \note Three modules, named rather than swept: what makes a stale rack
    /// visible is that the preset's module *count* differs from what is on
    /// screen, and a case that took whatever preset came first could not promise
    /// that. A rename breaks this loudly, which is the right failure.
    std::filesystem::path const presetFile(std::filesystem::path(SW_PRESET_DATA_DIR) / "Voices" /
                                           "Robokid.swp");
    REQUIRE(std::filesystem::is_regular_file(presetFile));
    auto presetData(LE::SW::readPresetFile(presetFile));
    REQUIRE(presetData);

    auto const requestsBefore(editor->rackResyncRequests());

    REQUIRE(LE::SW::GUI::loadPreset(host, editor.get(), presetData.get(),
                                    true /*ignore external samples*/, nullptr, "Robokid"));

    // The main thread's chain is the preset's the moment the load returns...
    auto const modules(editor->moduleChain().size());
    REQUIRE(modules == 3);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// ...and the editor has been told to follow it. No `process()` and no
    /// `flush()` anywhere in this case: the engine's own chain is still sitting
    /// in the command ring, and it is allowed to be. What must not depend on it
    /// is the picture.
    ///
    ////////////////////////////////////////////////////////////////////////////
    CHECK(editor->rackResyncRequests() > requestsBefore);

    // And the resync, run by hand as everything headless must, is the preset's.
    editor->resyncModuleRack();
    for (std::uint8_t slot(0); slot < modules; ++slot)
        REQUIRE(editor->regionInSlot(slot) != nullptr);
    CHECK(editor->regionInSlot(static_cast<std::uint8_t>(modules)) == nullptr);

    editor.reset();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Loading preset after preset with the window open, which is what a user
/// does with the browser and what nothing headless did. Every preset case before
/// this one loads into a *fresh* engine with no editor -- so it never saw a rack
/// being replaced, a mailbox holding values about the chain that just left, or a
/// spectral setup asking for a restart on the way past.
///
///   Three faults came out of it and all three are its business: a chain
/// destroyed while a strip still held one of its modules, a stale mailbox entry
/// delivered to whatever effect now occupies that slot, and the assertion this is
/// written against. Each turn of the loop is one browser click: load, resync the
/// rack, honour the restart if one was asked for, run audio, draw.
///
/// \note All 288 shipped presets, not a sample of them. The interesting presets are
/// the 2011 ones whose effects have grown parameters since, and there is no way
/// to know which those are without reading all of them.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Loading preset after preset with the editor open", "[clap][presets][gui]")
{
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{512};

    std::filesystem::path const banks(SW_PRESET_DATA_DIR);
    REQUIRE(std::filesystem::is_directory(banks));

    Entry const entry;
    juce::ScopedJuceInitialiser_GUI const juceIsUp;

    ActivePlugin plugin(sampleRate, blockSize);
    auto &host(editorHostOf(*plugin));
    auto editor(std::make_unique<LE::SW::GUI::SpectrumWorxEditor>(host));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The mailbox is primed first, and that is the whole setup: an effect
    /// with plenty of parameters, an LFO running on it, and blocks of audio to
    /// make it write. What the presets then do is replace that chain under it.
    ///
    ///   A mailbox entry says "slot 3, parameter 9" and keeps its dirty bit until
    /// somebody sweeps it. Load a preset that puts a two-parameter effect in slot
    /// 3 and the sweep hands parameter 9 to a strip that has three knobs --
    /// reported as "Parameter index out of range" from
    /// `effectSpecificParameterControl`.
    ///
    ///   Priming rather than rendering each preset's own chain, because rendering
    /// real spectra in a checked build aborts on the negative-amplitude
    /// verification (see presetRenderTests.cpp) and this case has to run in the
    /// build where the assertions are.
    ///                                       (02.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    {
        auto const &params(parameters(*plugin));
        for (std::uint8_t slot(0); slot < LE::SW::Constants::maxNumberOfModules; ++slot)
        {
            OneParameterEvent const fill(parameterID(moduleChainType, slot), slot / 64.0);
            params.flush(&*plugin, &*fill, &discardedOutputEvents());
            plugin.pumpMainThread();
            OneParameterEvent const enable(lfoParameterID(slot, 0, lfoEnabled), 1);
            params.flush(&*plugin, &*enable, &discardedOutputEvents());
            plugin.pumpMainThread();
        }

        std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
        std::vector<float> leftOut(blockSize), rightOut(blockSize);
        auto const playing(transportAt(120, 0, CLAP_TRANSPORT_IS_PLAYING));
        for (unsigned block(0); block < 8; ++block)
            plugin.process(leftIn, rightIn, leftOut, rightOut, &playing);
    }
    editor->resyncModuleRack();

    unsigned int loaded(0);
    for (auto const &file : std::filesystem::recursive_directory_iterator(banks))
    {
        if (file.path().extension() != ".swp")
            continue;

        auto const presetData(LE::SW::readPresetFile(file.path()));
        REQUIRE(presetData);

        ////////////////////////////////////////////////////////////////////
        ///
        /// \note No reporter of this case's own, on purpose. `GUI::loadPreset`
        /// decides for itself whether the load is worth a dialog, and a dialog
        /// here is a `juce::AlertWindow::showMessageBoxAsync` with no message
        /// loop to run it -- so JUCE's leak detector fails the run at exit and
        /// names the count. Which is how "browsing the factory banks interrupts
        /// you on one preset in three" was found: 104 leaked `AsyncUpdater`s.
        ///
        ///   presetReportTests.cpp is the direct statement of the same thing;
        /// this is the end-to-end one, with a real editor attached.
        ///                               (02.08.2026.) (SW port)
        ///
        ////////////////////////////////////////////////////////////////////
        auto const name(file.path().stem().string());
        REQUIRE(LE::SW::GUI::loadPreset(host, editor.get(), presetData.get(),
                                        true /*ignore external samples*/, nullptr, name.c_str()));
        ++loaded;

        /// \note The posted resync, run by hand: a headless test has no message
        /// loop to turn, and what the rack looks like between the load and the
        /// resync is exactly where the strips and the chain disagree.
        editor->resyncModuleRack();

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \note The engine's half of the load, which nothing here was doing.
        /// `publishChain()` queues the new chain while the plugin is active and
        /// this case never renders a block -- so before the main thread had a
        /// Program of its own, every assertion below was made against whatever
        /// chain the *priming* left in the engine, not against the preset. It
        /// read five modules for all 303 of them.
        ///
        ///   `flush()` drains the command ring the same way `process()` does and
        /// renders nothing, which is what this case needs: rendering real spectra
        /// in a checked build aborts on the negative-amplitude verification.
        ///                                   (06.08.2026.) (SW port)
        ///
        ////////////////////////////////////////////////////////////////////////
        plugin.flush();

        // And the two copies agree about what the preset asked for.
        CHECK(editor->moduleChain().size() == host.core().moduleChain().size());

        // The rack is a function of the chain, whatever the preset asked for.
        auto const modules(editor->moduleChain().size());
        for (std::uint8_t slot(0); slot < modules; ++slot)
            REQUIRE(editor->regionInSlot(slot) != nullptr);

        // A preset that moves the FFT size asks for a restart; a host honours it.
        plugin.restartIfAsked();

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \note The audio thread's part, played by hand and deliberately out of
        /// date: a value for the *last* parameter index a module can have, in
        /// every slot. That is what an LFO on a wide effect leaves behind, and a
        /// preset that puts a three-knob effect in the same slot is what makes it
        /// wrong -- the sweep below then hands parameter nine to a strip that has
        /// three.
        ///
        ///   Written rather than produced by running the wide effect, because it
        /// has to be certain: priming with a real chain publishes whatever
        /// parameters that chain's LFOs happen to drive, and the first attempt at
        /// this case published index 1, which every effect has, and proved
        /// nothing.
        ///                                   (02.08.2026.) (SW port)
        ///
        ////////////////////////////////////////////////////////////////////////
        auto &mailbox(const_cast<LE::SW::Threading::ValueMailbox &>(host.modulatedValues()));
        for (std::uint8_t slot(0); slot < LE::SW::Constants::maxNumberOfModules; ++slot)
        {
            LE::SW::ParameterID stale;
            stale.value.type = LE::SW::ParameterID::ModuleParameter;
            stale.value._.module = {LE::SW::ParameterID::Zero,
                                    LE::SW::Constants::maxNumberOfParametersPerModule - 1, slot};
            mailbox.set(LE::SW::parameterIndexFromBinaryID(stale.binaryValue), 0.5f);
        }

        // ...and then the interface draws whatever the LFOs left behind, which is
        // the sweep the reported "Parameter index out of range" came from.
        editor->pumpModulatedValues();
    }

    INFO("presets loaded: " << loaded);
    REQUIRE(loaded >= 288);

    editor.reset();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The case above loads preset after preset on the thread that also runs
/// the audio, so the chain is never installed while anything else is looking at
/// it. This is the same loop with a real audio thread underneath, which is what
/// a user browsing presets with the transport rolling actually has.
///
///   Reported as a crash in `ParameterInfoGetter<CLAP>::operator()(LFO, Program
/// const *)`, reached from `paramsInfo` -- Bitwig calls `params.value_to_text`
/// synchronously from inside `state.mark_dirty`, and `presetChangeEnd()` marks
/// dirty the instant `publishChain()` has *queued* the swap. So the host walks
/// the parameter list on the main thread in the same moment the audio thread
/// splices the chain out from under it. In a checked build it lands earlier and
/// says so plainly: `other.referenceCount_ == 3` in `moveAssign()`, the root node
/// counting one reference more than the swap can account for, because the walk
/// on the other thread is holding it.
///
///   Nothing here is specific to presets or to Bitwig. `paramsInfo`,
/// `paramsValue`, `paramsValueToText` and `stateSave` are all `[main-thread]` and
/// all resolve a slot by walking `program().moduleChain()`, which §2 rule 1 says
/// belongs to the audio thread while the plugin is activated. A preset is simply
/// the mutation with the widest window.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A host reads the parameter list while the engine installs a chain",
          "[clap][threading][presets]")
{
    constexpr float sampleRate{48000};
    constexpr std::uint32_t blockSize{64};
    constexpr unsigned int rounds{200};

    Entry const entry;

    ActivePlugin plugin(sampleRate, blockSize);
    auto &host(editorHostOf(*plugin));
    auto const &params(parameters(*plugin));
    auto const parameterCount(params.count(&*plugin));
    REQUIRE(parameterCount > 0);

    std::atomic<bool> stop{false};
    std::atomic<unsigned int> blocksRun{0};

    /// \note No `REQUIRE` on this thread -- Catch2's assertion machinery is not
    /// thread safe. \see ActivePlugin::processStatus
    std::thread audio([&] {
        std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
        std::vector<float> leftOut(blockSize), rightOut(blockSize);
        while (!stop.load(std::memory_order_acquire))
        {
            plugin.processStatus(leftIn, rightIn, leftOut, rightOut);
            blocksRun.fetch_add(1, std::memory_order_acq_rel);
        }
    });

    // Not one description read before the audio thread is really running.
    while (blocksRun.load(std::memory_order_acquire) == 0)
    {
    }

    bool everyDescriptionRead{true};
    bool everyRangeOrdered{true};

    for (unsigned int round(0); round < rounds; ++round)
    {
        ////////////////////////////////////////////////////////////////////////
        /// \note What a preset load ends in. `GUI::loadPreset` builds a chain
        /// nothing else can see and hands it to `Threading::publishChain()`,
        /// which with audio running queues it for the audio thread; the file
        /// parsing in front of that is not what this case is about.
        ////////////////////////////////////////////////////////////////////////
        LE::SW::AutomatedModuleChain replacement;
        for (std::uint8_t slot(0); slot < 1 + (round % 4); ++slot)
        {
            auto *const pModule(LE::SW::Threading::createModuleForSlot(
                host.core(), static_cast<std::int8_t>((round + slot) % 4), slot));
            REQUIRE(pModule != nullptr);
            replacement.push_back(LE::SW::Engine::node(*pModule));
            intrusive_ptr_release(&LE::SW::Engine::node(*pModule)); // the chain owns it now
        }
        LE::SW::Threading::publishChain(host.core(), host.toEngine(), replacement);

        // And what the host does the moment it is told the list may have moved.
        for (std::uint32_t index(0); index < parameterCount; ++index)
        {
            clap_param_info info{};
            if (!params.get_info(&*plugin, index, &info))
            {
                everyDescriptionRead = false;
                continue;
            }
            everyRangeOrdered = everyRangeOrdered && (info.min_value <= info.max_value);

            double value{};
            params.get_value(&*plugin, info.id, &value);
            std::array<char, 256> text{};
            params.value_to_text(&*plugin, info.id, value, text.data(),
                                 static_cast<std::uint32_t>(text.size()));
        }
    }

    stop.store(true, std::memory_order_release);
    audio.join();

    CHECK(everyDescriptionRead);
    CHECK(everyRangeOrdered);
}
