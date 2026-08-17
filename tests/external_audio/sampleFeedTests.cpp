////////////////////////////////////////////////////////////////////////////////
///
/// sampleFeedTests.cpp
/// -------------------
///
///   The loaded sample, reaching the DSP.
///
///   `sampleTests.cpp` proves all seventeen factory samples decode, and
/// `stateTests.cpp` proves one survives a session -- but nothing proved that
/// what `runEngine()` does with it happens at all. The block is twenty lines
/// (`spectrumWorxCLAP.cpp:928-946`): when a sample is loaded, the side channel
/// comes from the file **in place of the port**, chunk by chunk, wrapping at the
/// end. It had no test of any kind, and the two facts either side of it read as
/// though it did.
///
///   That block is also the one the 02.08.2026 threading change was written for.
/// Its own note says a load, a preset or an FFT-size change used to make a block
/// play the host's side chain port instead of the sample, because the reader
/// took a `try_lock` and the writer held the processing lock. There is no lock
/// now; what says so is a case that can tell the two sources apart, which is
/// what this is.
///
/// \note Four arrangements rather than two, because "the output changed" is not
/// the claim. The claim is *in place of the port*: a sample and a port carrying
/// different audio have to produce what the sample alone produces, and that
/// needs the port arm measured as well.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "clap/testHost.hpp"

#include "external_audio/sample.hpp"
#include "goldens/engineHarness.hpp"

/// \note For ScopedProblemCounter: nothing here loads a preset, but the editor
/// host's default problem reporter is a `juce::AlertWindow` in a process with no
/// message thread, and a sample that fails to load raises one.
#include "presets/presetHarness.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace SWTest;

constexpr float sampleRate{48000};
constexpr std::uint32_t blockSize{512};
/// Well past the engine's latency, and long enough that the sample has been
/// read for a while rather than just started.
constexpr unsigned int blocks{32};

/// \note "Carrier.mp3", which is what `stateTests.cpp` uses and is named for
/// exactly this job. A bare name with no directory is the one spelling that
/// resolves against the embedded set -- see `Sample::load()`.
fs::path carrier() { return "Carrier.mp3"; }

void fillWithSine(std::vector<float> &buffer, float const frequency, std::uint32_t const startFrame)
{
    for (std::size_t frame(0); frame < buffer.size(); ++frame)
        buffer[frame] = 0.5f * std::sin(2 * std::numbers::pi_v<float> * frequency *
                                        static_cast<float>(startFrame + frame) / sampleRate);
}

float peak(std::vector<float> const &buffer)
{
    float largest{0};
    for (auto const sample : buffer)
        largest = std::max(largest, std::abs(sample));
    return largest;
}

bool allFinite(std::vector<float> const &buffer)
{
    return std::all_of(buffer.begin(), buffer.end(),
                       [](float const sample) { return std::isfinite(sample); });
}

/// How the side channel is arranged for one run.
struct Arrangement
{
    bool loadSample{false};
    bool connectPort{false};
};

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Runs \p blocks of a 440 Hz sine through a side-chain effect and hands
/// back the last block's left channel.
///
/// \note Colorifer in slot 1: one of the fifteen effects measured to read a side
/// chain, and one of the eleven that does so at its defaults.
/// \see tests/effects/sideChainTests.cpp
///
////////////////////////////////////////////////////////////////////////////////

std::vector<float> run(Arrangement const arrangement)
{
    Entry const entry;
    SWTest::ScopedProblemCounter const quiet;

    ActivePlugin plugin(sampleRate, blockSize);

    auto const colorifer(SWTest::effectByStreamingName("Colorifer"));
    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), colorifer);
    parameters(*plugin).flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());

    std::vector<float> leftIn(blockSize), rightIn(blockSize);
    std::vector<float> sideLeft(blockSize), sideRight(blockSize);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);

    if (arrangement.connectPort)
        plugin.connectSideChain(sideLeft, sideRight);

    if (arrangement.loadSample)
    {
        editorHostOf(*plugin).setNewSample(carrier());
        REQUIRE(editorHostOf(*plugin).currentSampleFile().filename() == "Carrier.mp3");
    }

    for (unsigned int block(0); block < blocks; ++block)
    {
        fillWithSine(leftIn, 440.0f, block * blockSize);
        rightIn = leftIn;
        // 1100 Hz on the port: a partial the main input does not have and the
        // sample is most unlikely to, so whichever source reaches the engine is
        // identifiable from the output.
        fillWithSine(sideLeft, 1100.0f, block * blockSize);
        sideRight = sideLeft;

        plugin.process(leftIn, rightIn, leftOut, rightOut);
        REQUIRE(allFinite(leftOut));
    }
    return leftOut;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("A loaded sample feeds the side channel in place of the port",
          "[external-audio][side-chain]")
{
    juce::ScopedJuceInitialiser_GUI const juceIsUp;

    auto const nothing(run({.loadSample = false, .connectPort = false}));
    auto const portOnly(run({.loadSample = false, .connectPort = true}));
    auto const sampleOnly(run({.loadSample = true, .connectPort = false}));
    auto const both(run({.loadSample = true, .connectPort = true}));

    REQUIRE(nothing.size() == blockSize);
    CHECK(peak(nothing) > 0); // the effect is producing audio at all

    // The sample reached the DSP: with nothing on the port, loading a file
    // changed what came out.
    CHECK(sampleOnly != nothing);

    // The control, so that the line above is about the *sample* rather than
    // about anything a second source does: the port reaches the engine too.
    CHECK(portOnly != nothing);
    CHECK(portOnly != sampleOnly);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And the claim the case is named for. `runEngine()` overwrites
    /// `sideChannels` with the sample's chunks *after* choosing the port, so a
    /// loaded sample wins outright -- the port is not mixed with it, not
    /// preferred over it, and not used for the channels the sample does not
    /// have. Bit-identical to the sample alone is the only outcome consistent
    /// with that, and it is what a user assumes when they load a file.
    ///
    ////////////////////////////////////////////////////////////////////////////
    CHECK(both == sampleOnly);
}

TEST_CASE("The sample is read forwards rather than held", "[external-audio][side-chain]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `samplePosition()` advances across blocks and wraps at the end,
    /// which is `sampleChunk()`'s whole job -- and it is unreachable from a
    /// test: `pSample_` is private and belongs to the audio thread. So the
    /// position is observed through the audio instead, which is the honest way
    /// round: a sample that never advanced would feed the same 512 frames
    /// forever, and the output would be periodic at the block rate.
    ///
    ///   Two windows of the same render, far enough apart that a whole sample's
    /// worth of file has gone by at 48 kHz.
    ///
    ////////////////////////////////////////////////////////////////////////////
    juce::ScopedJuceInitialiser_GUI const juceIsUp;

    Entry const entry;
    SWTest::ScopedProblemCounter const quiet;

    ActivePlugin plugin(sampleRate, blockSize);

    auto const colorifer(SWTest::effectByStreamingName("Colorifer"));
    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), colorifer);
    parameters(*plugin).flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());

    editorHostOf(*plugin).setNewSample(carrier());

    /// \note A *steady* main input -- the same 440 Hz phase every block rather
    /// than a continuing one -- so that anything which differs between two
    /// blocks is the side channel and not the sine.
    std::vector<float> leftIn(blockSize), rightIn(blockSize);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);
    fillWithSine(leftIn, 440.0f, 0);
    rightIn = leftIn;

    std::vector<std::vector<float>> captured;
    for (unsigned int block(0); block < 64; ++block)
    {
        plugin.process(leftIn, rightIn, leftOut, rightOut);
        REQUIRE(allFinite(leftOut));
        captured.push_back(leftOut);
    }

    // Past the engine's latency, and two blocks apart in the file.
    CHECK(captured[32] != captured[33]);
    CHECK(captured[32] != captured[63]);
    CHECK(peak(captured[63]) > 0);
}

TEST_CASE("Clearing the sample gives the port back", "[external-audio][side-chain]")
{
    /// \note The other half of the swap, and the one a user reaches by loading a
    /// file and then removing it. `publishSample(nullptr)` has to leave the
    /// engine reading the port again rather than the last chunk it saw -- a
    /// stale `pSample_` would keep feeding a file nothing has open.
    juce::ScopedJuceInitialiser_GUI const juceIsUp;

    Entry const entry;
    SWTest::ScopedProblemCounter const quiet;

    ActivePlugin plugin(sampleRate, blockSize);

    auto const colorifer(SWTest::effectByStreamingName("Colorifer"));
    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), colorifer);
    parameters(*plugin).flush(&*plugin, &*fillSlotOne, &discardedOutputEvents());

    std::vector<float> leftIn(blockSize), rightIn(blockSize);
    std::vector<float> sideLeft(blockSize), sideRight(blockSize);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);
    plugin.connectSideChain(sideLeft, sideRight);

    auto const runBlocks([&](unsigned int const count) {
        for (unsigned int block(0); block < count; ++block)
        {
            fillWithSine(leftIn, 440.0f, block * blockSize);
            rightIn = leftIn;
            fillWithSine(sideLeft, 1100.0f, block * blockSize);
            sideRight = sideLeft;
            plugin.process(leftIn, rightIn, leftOut, rightOut);
            REQUIRE(allFinite(leftOut));
        }
        return leftOut;
    });

    editorHostOf(*plugin).setNewSample(carrier());
    auto const withSample(runBlocks(blocks));

    editorHostOf(*plugin).setNewSample({});
    CHECK(editorHostOf(*plugin).currentSampleFile().empty());
    auto const afterClearing(runBlocks(blocks));

    CHECK(afterClearing != withSample);
    CHECK(peak(afterClearing) > 0);
}
