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

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Which of the three sources the patch selects.
    ///
    /// \note Defaulted to `Main`, which is **not** the plugin's default -- that
    /// is `Host`. This field is the arrangement a case asks for rather than the
    /// one a fresh instance is in, and `run()` always sets it, so an omitted
    /// `.source` means "the source in which neither the port nor a file is
    /// heard". That is the arrangement every case in this file was implicitly in
    /// before there was anything to name. \see pluginTests.cpp for the default.
    ///
    /// \note `loadSample` and `source` are independent on purpose. A file can be
    /// loaded while another source is selected -- that is the whole of what the
    /// old "a loaded file always wins" precedence could not express, and the case
    /// below is about it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    LE::SW::SideChainSource source{LE::SW::SideChainSource::Main};
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
    plugin.flush(&*fillSlotOne);

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

    /// \note After the load, because loading a file *is* selecting it -- a case
    /// that wants a file loaded and a different source has to say so second.
    editorHostOf(*plugin).setSideChainSource(arrangement.source);

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

TEST_CASE("Each of the three sources reaches the DSP, and only the selected one",
          "[external-audio][side-chain][issue-113]")
{
    juce::ScopedJuceInitialiser_GUI const juceIsUp;

    using LE::SW::SideChainSource;

    ////////////////////////////////////////////////////////////////////////////
    // The three, each alone.
    ////////////////////////////////////////////////////////////////////////////

    auto const self(run({.loadSample = false, .connectPort = false}));
    auto const file(
        run({.loadSample = true, .connectPort = false, .source = SideChainSource::File}));
    auto const host(
        run({.loadSample = false, .connectPort = true, .source = SideChainSource::Host}));

    REQUIRE(self.size() == blockSize);
    CHECK(peak(self) > 0); // the effect is producing audio at all

    // Three genuinely different renders, or nothing below distinguishes anything.
    CHECK(file != self);
    CHECK(host != self);
    CHECK(host != file);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And now with everything present at once, three times over. The
    /// selected source is the *only* one heard: not preferred, not mixed with,
    /// not used for the channels another does not have. Bit-identical to the same
    /// source alone is the only outcome consistent with that.
    ///
    ///   Two of these three were unaskable before 18.08.2026, and that is the
    /// point of the change rather than a by-product of it. A loaded file beat the
    /// port unconditionally, so "the host's send, and keep my file loaded" had no
    /// spelling at all, and "the main input, with a file loaded" had none either.
    /// \see issue #113.
    ///
    ////////////////////////////////////////////////////////////////////////////

    CHECK(run({.loadSample = true, .connectPort = true, .source = SideChainSource::File}) == file);
    CHECK(run({.loadSample = true, .connectPort = true, .source = SideChainSource::Host}) == host);
    CHECK(run({.loadSample = true, .connectPort = true, .source = SideChainSource::Main}) == self);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note What each source does when the thing it names is not there. Neither is
/// an error and both land on the main input, which is what makes "a side chain
/// with nothing behind it blends the signal with itself" true of all three.
///
/// \note "Not there" means the host offers no port at all, which is the only
/// thing `Host` falls back from. A port that is present and carrying silence is
/// silence -- the plugin stopped reading `constant_mask` to guess otherwise on
/// 19.08.2026. \see issue #117 and doc/tech/sidechain-approach.md §2.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A source with nothing behind it is the main input",
          "[external-audio][side-chain][issue-113]")
{
    juce::ScopedJuceInitialiser_GUI const juceIsUp;

    using LE::SW::SideChainSource;

    auto const self(run({.loadSample = false, .connectPort = false}));
    REQUIRE(self.size() == blockSize);

    // `Host` with no second port offered.
    CHECK(run({.loadSample = false, .connectPort = false, .source = SideChainSource::Host}) ==
          self);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `File` with nothing loaded is not a state that can be reached at
    /// all: the setter refuses it and leaves `Main` selected, so the selector can
    /// never come to be showing a file that is not there and the engine can never
    /// hold a source it cannot honour. Asserted on the *host* rather than only
    /// through the audio, because "it sounds like Main" and "it is Main" are two
    /// different claims and the second is the one being made.
    ///
    ////////////////////////////////////////////////////////////////////////////
    {
        Entry const entry;
        SWTest::ScopedProblemCounter const quiet;
        ActivePlugin plugin(sampleRate, blockSize);

        REQUIRE(editorHostOf(*plugin).currentSampleFile().empty());
        editorHostOf(*plugin).setSideChainSource(SideChainSource::File);
        CHECK(editorHostOf(*plugin).sideChainSource() == SideChainSource::Main);

        // ...and with one loaded it is honoured, so the refusal is about the
        // absent file rather than about the value.
        editorHostOf(*plugin).setNewSample(carrier());
        editorHostOf(*plugin).setSideChainSource(SideChainSource::File);
        CHECK(editorHostOf(*plugin).sideChainSource() == SideChainSource::File);

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \note And selecting either of the others **discards** the file rather
        /// than leaving it loaded and unheard. That is what keeps the selector's
        /// three answers the whole of the state: a patch cannot come to carry
        /// audio nothing will play, and `stateSave` cannot write a `Sample=` its
        /// own source contradicts.
        ///
        ///   Which also puts `File` back out of reach, by the rule above -- the
        /// two assertions together are the invariant, not two separate facts.
        ///
        ////////////////////////////////////////////////////////////////////////
        editorHostOf(*plugin).setSideChainSource(SideChainSource::Host);
        CHECK(editorHostOf(*plugin).sideChainSource() == SideChainSource::Host);
        CHECK(editorHostOf(*plugin).currentSampleFile().empty());

        editorHostOf(*plugin).setSideChainSource(SideChainSource::File);
        CHECK(editorHostOf(*plugin).sideChainSource() == SideChainSource::Main);
    }

    CHECK(run({.loadSample = false, .connectPort = false, .source = SideChainSource::File}) ==
          self);
    CHECK(run({.loadSample = false, .connectPort = true, .source = SideChainSource::File}) == self);
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
    plugin.flush(&*fillSlotOne);

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

TEST_CASE("Clearing the sample stops it being heard", "[external-audio][side-chain]")
{
    /// \note The other half of the swap, and the one a user reaches by loading a
    /// file and then removing it. `publishSample(nullptr)` has to leave the
    /// engine reading its fallback again rather than the last chunk it saw -- a
    /// stale `pSample_` would keep feeding a file nothing has open.
    ///
    /// \note The case was "Clearing the sample gives the port back" until
    /// 18.08.2026, and the port is not what it gives back any more: clearing a
    /// file selects `Main`, so the fallback is the main input and the port is not
    /// consulted either before the clear or after it. The port stays connected
    /// below all the same, because a stale-pointer bug that only shows up when a
    /// second source is present is exactly the shape this is watching for.
    /// \see issue #113.
    juce::ScopedJuceInitialiser_GUI const juceIsUp;

    Entry const entry;
    SWTest::ScopedProblemCounter const quiet;

    ActivePlugin plugin(sampleRate, blockSize);

    auto const colorifer(SWTest::effectByStreamingName("Colorifer"));
    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), colorifer);
    plugin.flush(&*fillSlotOne);

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

////////////////////////////////////////////////////////////////////////////////
///
/// \note Issue #143: the 2.x plugin took the sample back to its start when the
/// transport started and this one did not. `Sample::restart()` had been on the
/// class since 2011 with no caller at all, which is the shape of what went
/// missing in the port.
///
/// \note Measured through the audio, as the case above is and for the same
/// reason: `pSample_` is private and belongs to the audio thread. What a restart
/// looks like from outside is that the block after the transport starts carries
/// the *same* side chain as the block the file was last started at.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The sample goes back to its start when the transport does",
          "[external-audio][side-chain]")
{
    juce::ScopedJuceInitialiser_GUI const juceIsUp;

    Entry const entry;
    SWTest::ScopedProblemCounter const quiet;

    ActivePlugin plugin(sampleRate, blockSize);

    auto const colorifer(SWTest::effectByStreamingName("Colorifer"));
    OneParameterEvent const fillSlotOne(parameterID(moduleChainType, 0), colorifer);
    plugin.flush(&*fillSlotOne);

    editorHostOf(*plugin).setNewSample(carrier());

    /// \note A steady main input, so that anything differing between two blocks
    /// is the side channel and not the sine. \see the case above.
    std::vector<float> leftIn(blockSize), rightIn(blockSize);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);
    fillWithSine(leftIn, 440.0f, 0);
    rightIn = leftIn;

    auto const stopped(transportAt(120, 0, 0));
    auto const rolling(transportAt(120, 0, CLAP_TRANSPORT_IS_PLAYING));

    auto const blockWith([&](clap_event_transport const &transport) {
        plugin.process(leftIn, rightIn, leftOut, rightOut, &transport);
        REQUIRE(allFinite(leftOut));
        return leftOut;
    });

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The transport started, and the sixteen blocks after it.
    ///
    /// \note Sixteen because the *first* few cannot match and should not be
    /// expected to: the engine's FIFO still holds a window of whatever was
    /// playing before the start, and one window at the default 2048/4 is four
    /// blocks of 512. Past that the output is a function of the file's position
    /// alone, which is what a restart moves.
    ///
    ////////////////////////////////////////////////////////////////////////////

    auto const fromAStart([&] {
        std::vector<std::vector<float>> captured;
        for (unsigned int block(0); block < 16; ++block)
            captured.push_back(blockWith(rolling));
        return captured;
    });

    // Somewhere into the file, with the transport parked -- a user auditioning.
    for (unsigned int block(0); block < 8; ++block)
        blockWith(stopped);

    auto const first(fromAStart());

    // Well past where that left off, then stopped and started again.
    for (unsigned int block(0); block < 32; ++block)
        blockWith(rolling);
    blockWith(stopped);

    auto const second(fromAStart());

    // Past the engine's own history, the two runs are the same audio.
    for (std::size_t block(8); block < first.size(); ++block)
    {
        CAPTURE(block);
        CHECK(first[block] == second[block]);
    }

    ////////////////////////////////////////////////////////////////////////////
    /// \note And a *rising edge* rather than "while playing": a block that is
    /// merely still rolling may not restart it, or the file would never advance
    /// at all -- which is what these two say, being sixteen blocks apart in one
    /// unbroken run.
    ////////////////////////////////////////////////////////////////////////////
    REQUIRE(first.front() != first.back());
}
