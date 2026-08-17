////////////////////////////////////////////////////////////////////////////////
///
/// automationTimingTests.cpp
/// -------------------------
///
///   *When* a host's parameter event is heard. `clap_event_header::time` is a
/// sample offset into the block and every host fills it in; the plugin applied
/// the whole list before rendering any of the block, so an automation move
/// three quarters of the way through was heard from the very start of it.
///
///   The measure is the output's level, quarter by quarter, with the output
/// gain as the parameter -- a value whose effect needs no interpretation. Every
/// claim below is a comparison between two renders of the same block rather
/// than an absolute number, so the engine's latency and its WOLA ripple cancel
/// out of it.
///
/// \note The three patterns are the ones that can distinguish "applied in time
/// order, in pieces" from "applied all at once, up front":
///
///   - event / process           — one edit, late in the block
///   - event / event / process   — two edits in the *same* block, so the second
///                                 must not be visible where the first is
///   - event / process / event / process — one edit per block, so neither may
///                                 leak into the other's block
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "clap/testHost.hpp"

#include "core/host_interop/parameters.hpp"
#include "le/spectrumworx/engine/configuration.hpp" // defaultOverlapFactor
#include "le/spectrumworx/engine/parameters.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
using namespace LE;
using namespace LE::SW;
using namespace SWTest;

constexpr std::uint32_t sampleRate{48000};

/// \note A big block, because what is being measured is a position *inside* one.
/// A host that hands over 128 samples cannot show the difference this is about:
/// a hop is 128 samples at the FFT size below, so the whole block is one piece.
constexpr std::uint32_t blockSize{4096};

clap_id outputGainID()
{
    ParameterID parameterID;
    parameterID.value.type = ParameterID::GlobalParameter;
    parameterID.value._.global.index =
        LE::Parameters::IndexOf<LE::SW::GlobalParameters::Parameters,
                                LE::SW::GlobalParameters::OutputGain>::value;
    return parameterID.binaryValue;
}

/// \note Native units, and `OutputGain` is a **linear multiplier** -- 0.001 to
/// 2.0, default 1.0 -- despite the interface printing it as dB. A value outside
/// that clamps to the nearest end, which is how the first draft of this file
/// managed to make "loud" and "quiet" the same silence.
constexpr double loud{1.0};
constexpr double quiet{0.01};

std::vector<float> sine(std::uint32_t const frames)
{
    std::vector<float> signal(frames);
    for (std::uint32_t frame(0); frame < frames; ++frame)
        signal[frame] = 0.5f * std::sin(2 * std::numbers::pi_v<float> * 440 * frame / sampleRate);
    return signal;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The level in one quarter of the block the host sent, read out of the
/// output at the offset the gain change actually lands on.
///
/// \note **One hop, not one latency**, and the difference is worth knowing.
/// `OutputGain` is not applied per sample -- `Processor::processSingleChannel`
/// scales a whole frame's worth as that frame is produced -- so an event timed at
/// sample `p` takes effect from the next frame boundary, which is `p + hop`.
/// Measured directly: with the event at 3072 and a 512-sample hop, the level
/// drops at 3584.
///
/// \note The offset exists at all because these cases used to read quarter `q` at
/// buffer position `q * 1024` and get the right answer by coincidence -- before
/// #83 the whole output sat one hop earlier, which cancelled this hop exactly. So
/// they were measuring "the change is at the event's own sample", which was true
/// of the buffer and never true of the audio. #83 moved the output and the
/// coincidence went with it, on renders that are otherwise bit-identical.
///
///   `\p samples` is two blocks joined, so a quarter offset by a hop is still in
/// range.
///
////////////////////////////////////////////////////////////////////////////////

double rmsOfQuarter(std::vector<float> const &samples, std::size_t const quarter,
                    std::uint32_t const hop)
{
    auto const length(blockSize / 4);
    auto const first(hop + quarter * length);
    REQUIRE((first + length) <= samples.size());
    double sum(0);
    for (std::size_t index(first); index < first + length; ++index)
        sum += double(samples[index]) * samples[index];
    return std::sqrt(sum / length);
}

/// Two renders end to end, so a quarter offset by the latency is still in range.
std::vector<float> operator+(std::vector<float> lhs, std::vector<float> const &rhs)
{
    lhs.insert(lhs.end(), rhs.begin(), rhs.end());
    return lhs;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief A plugin with a sine running through it, warmed up past the engine's
/// latency, ready to be handed one measured block.
///
/// \note The warm-up matters: the first blocks out of a WOLA engine are the
/// window filling, so a level read there says nothing about a parameter.
///
////////////////////////////////////////////////////////////////////////////////

class Running
{
  public:
    explicit Running(TestHost &host) : plugin_(sampleRate, blockSize, host), input_(sine(blockSize))
    {
        // The engine's own setup: a small FFT, so the latency is a small part of
        // the block and a quarter of it is a meaningful window to measure in.
        for (int warmUp(0); warmUp < 8; ++warmUp)
            render(nullptr);
    }

    /// One block, with \p events, returning the left output.
    std::vector<float> render(clap_input_events const *const events)
    {
        std::vector<float> left(input_), right(input_);
        std::vector<float> leftOut(blockSize, 0.0f), rightOut(blockSize, 0.0f);
        plugin_.process(left, right, leftOut, rightOut, nullptr, events);
        return leftOut;
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The engine's hop, which is what a gain change is quantised to.
    ///
    /// \note Derived from what the plugin reports rather than written down.
    /// `latencyGet()` is the FFT size (\see doc/tech/latency.md), and the default
    /// overlap factor is 4 -- read from the parameter rather than assumed, so
    /// that changing the default moves this with it instead of breaking it
    /// silently.
    ///
    ////////////////////////////////////////////////////////////////////////////
    std::uint32_t hop() const
    {
        auto const &plugin(*plugin_);
        auto const *const latency(static_cast<clap_plugin_latency const *>(
            plugin.get_extension(&plugin, CLAP_EXT_LATENCY)));
        REQUIRE(latency != nullptr);

        /// \note `latencyGet()` is the FFT size -- \see doc/tech/latency.md --
        /// and nothing in these cases moves the overlap factor off its default,
        /// so the hop is the one divided by the other. Read from the engine's own
        /// constant rather than written down, and *not* from the CLAP parameter:
        /// `params.get_value` does not hand back the raw factor, which is how an
        /// earlier version of this got 1024 for a hop that measures 512.
        auto const hop(latency->get(&plugin) / LE::SW::Engine::Constants::defaultOverlapFactor);
        REQUIRE(hop > 0);
        return hop;
    }

  private:
    ActivePlugin plugin_;
    std::vector<float> input_;
}; // class Running
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A parameter event is heard where the host timed it", "[clap][automation]")
{
    Entry const entry;

    double firstQuarterWhenEarly(0), firstQuarterWhenLate(0);
    double lastQuarterWhenEarly(0), lastQuarterWhenLate(0);

    {
        // The edit at the very start of the block: the whole block is quiet.
        TestHost host{{.threadCheck = true, .log = true}};
        Running running(host);
        TimedParameterEvents const atZero{{0, outputGainID(), quiet}};
        auto const rendered(running.render(&*atZero) + running.render(nullptr));
        firstQuarterWhenEarly = rmsOfQuarter(rendered, 0, running.hop());
        lastQuarterWhenEarly = rmsOfQuarter(rendered, 3, running.hop());
    }
    {
        // The same edit three quarters of the way in: the start of the block is
        // still at full level and only the end of it is quiet.
        TestHost host{{.threadCheck = true, .log = true}};
        Running running(host);
        TimedParameterEvents const atThreeQuarters{{3 * blockSize / 4, outputGainID(), quiet}};
        auto const rendered(running.render(&*atThreeQuarters) + running.render(nullptr));
        firstQuarterWhenLate = rmsOfQuarter(rendered, 0, running.hop());
        lastQuarterWhenLate = rmsOfQuarter(rendered, 3, running.hop());
    }

    CAPTURE(firstQuarterWhenEarly, firstQuarterWhenLate, lastQuarterWhenEarly, lastQuarterWhenLate);

    /// \note The whole claim. Before the block was rendered in pieces both
    /// renders were identical, because both applied the event before any audio
    /// was produced -- so this comparison is the one that could not pass.
    CHECK(firstQuarterWhenLate > 2 * firstQuarterWhenEarly);

    // ...and by the last quarter the late edit has caught up with the early one.
    CHECK(lastQuarterWhenLate < 2 * lastQuarterWhenEarly);
}

TEST_CASE("Two events in one block are heard in their own halves", "[clap][automation]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note event / event / process. Both edits are delivered before a single
    /// `process()` call, and the plugin has to keep them apart *within* it: down
    /// at a quarter, back up at three quarters. Applying the list up front left
    /// only the last value standing, so the block came out uniformly loud.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;
    TestHost host{{.threadCheck = true, .log = true}};
    Running running(host);

    TimedParameterEvents const downThenUp{{blockSize / 4, outputGainID(), quiet},
                                          {3 * blockSize / 4, outputGainID(), loud}};
    auto const rendered(running.render(&*downThenUp) + running.render(nullptr));

    auto const first(rmsOfQuarter(rendered, 0, running.hop()));
    auto const middle(rmsOfQuarter(rendered, 1, running.hop()));
    auto const last(rmsOfQuarter(rendered, 3, running.hop()));

    CAPTURE(first, middle, last);

    // Loud, then quiet, then loud again -- all inside one host block.
    CHECK(middle < first / 2);
    CHECK(last > 2 * middle);
}

TEST_CASE("An event stays in the block the host put it in", "[clap][automation]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note event / process / event / process. The complement of the case above:
    /// there the risk is two edits collapsing into one, here it is one edit
    /// leaking into the next block or arriving in the previous one. The second
    /// block's edit is at its very start, so the first block must not show it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;
    TestHost host{{.threadCheck = true, .log = true}};
    Running running(host);

    // Block one: quiet from three quarters in.
    TimedParameterEvents const late{{3 * blockSize / 4, outputGainID(), quiet}};
    auto const blockOne(running.render(&*late));

    // Block two: back to full level, from its first sample.
    TimedParameterEvents const atOnce{{0, outputGainID(), loud}};
    auto const blockTwo(running.render(&*atOnce));
    auto const blockThree(running.render(nullptr));

    auto const oneFirst(rmsOfQuarter(blockOne + blockTwo, 0, running.hop()));
    auto const oneLast(rmsOfQuarter(blockOne + blockTwo, 3, running.hop()));
    auto const twoFirst(rmsOfQuarter(blockTwo + blockThree, 0, running.hop()));

    CAPTURE(oneFirst, oneLast, twoFirst);

    // The first block heard its own edit, late...
    CHECK(oneLast < oneFirst / 2);
    // ...and did not hear the second block's, which had not been delivered yet.
    CHECK(twoFirst > 2 * oneLast);
}
