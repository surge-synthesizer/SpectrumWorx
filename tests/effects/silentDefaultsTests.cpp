////////////////////////////////////////////////////////////////////////////////
///
/// silentDefaultsTests.cpp
/// -----------------------
///
///   **9 of the 464 golden fixtures render pure silence**, which is a fixture
/// that cannot fail: a silent render hashes identically on every platform, so
/// those 9 rows agree with every build ever made and say nothing about any of
/// them. `goldenTests.cpp`'s own drift report counts them for that reason.
///
///   An explanation and a fix were once offered -- that the sample loader
/// used to be compiled out, and that a fixture could now load a factory sample
/// by name. Neither survives contact:
///
///     - **The loader cannot be reached from here.** `Sample` is built on
///       `juce::File`, and the goldens live in `sw-dsp-tests`, which links
///       `sw-dsp` and *no JUCE at all* -- goal 3 of the threading model, and a
///       boundary the build enforces rather than a reviewer. A golden that
///       loaded a file would not link, which is the point of the split.
///     - **It is not the reason anyway.** Each effect is silent for a reason of
///       its own, none of them about external audio, and all of them ordinary
///       parameter defaults:
///
///   | Effect | Why its default render is silent |
///   |---|---|
///   | `Convolver` | `ConvolutionType` defaults to `Triggered`: the impulse response is grabbed on a button press, so until then there is no response. Not a quiet render -- an unarmed one. |
///   | `Freqnamics` | One fixture, not eight: its noise gate is at −60 dB, and an impulse spread over 2048 bins is quieter than that per bin. At 512 bins it is not, which is why only the 2048/8 row is silent. |
///
///   **There were 25 until 10.08.2026.** The other sixteen were Frecho and
/// Frevcho, and they were a statement about the *fixture length* rather than
/// about the effects: `Distance` defaults to 100 m and the delay is the round
/// trip at 343 m/s, so **583 ms** against what was then a 371 ms render. Those
/// two now render for 2 s -- `needsALongRender()` in `goldenTests.cpp` -- and
/// all sixteen sound. What this file keeps of that is the measurement underneath
/// it, below, because the constant is only obviously right while someone
/// remembers why it is not 750 ms.
///
///   So what this file does is state each of these, both ways round -- silent at
/// the default, audible with the one thing moved -- and then hold the count of
/// silent fixtures in `goldens.txt` to what it is, so it can only fall.
///
/// \note Rendering the *fix* rather than fixing the defaults. All of it is 2016
/// behaviour and changing any of it changes what a preset sounds like, which
/// is the rule the rest of the tree already follows.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "goldens/engineHarness.hpp"
#include "goldens/goldenDigest.hpp"

#include "le/spectrumworx/effects/configuration/effectNames.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
namespace Effects = LE::SW::Effects;

using Module = LE::SW::Engine::ModuleParameters;
using SWTest::Signal;
using SWTest::Slot;

/// The golden matrix's own smaller configuration and its own two lengths,
/// because what is being explained is those fixtures and not a rendering of this
/// file's choosing. See `renderedFrames` and `longRenderedFrames` in
/// `goldenTests.cpp`.
constexpr SWTest::RenderSetup setup{512, 4, 2, 44100, 256};
constexpr std::uint32_t frames{44100 / 2};     // 500 ms, the matrix default
constexpr std::uint32_t longFrames{44100 * 2}; // 2 s, what Frecho and Frevcho get

float peak(std::span<float const> samples)
{
    float largest{0};
    for (auto const sample : samples)
        largest = std::max(largest, std::abs(sample));
    return largest;
}

/// \brief One effect over \p signal, optionally configured.
float peakOf(std::string_view const name, Signal const signal,
             std::function<void(Module &)> configure = {},
             SWTest::RenderSetup const &configuration = setup,
             std::uint32_t const renderedFrames = frames)
{
    auto const effect(Effects::effectIndex(name));
    REQUIRE(effect >= 0);
    Slot const slots[]{{effect, std::move(configure)}};
    return peak(SWTest::renderChain(configuration, slots, signal, renderedFrames));
}
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("Convolver is silent because nothing has armed it", "[effects][silent-defaults]")
{
    /// Convolver's parameters, in declaration order.
    enum : std::uint8_t
    {
        convolutionType = 0,
        grabIR = 1,
        phase = 2
    };
    /// ConvolutionType's enumerators. `Triggered` is first, and so the default.
    enum : int
    {
        triggered = 0,
        continuous = 1
    };

    for (auto const signal : {Signal::Sweep, Signal::PinkNoise, Signal::Voice})
    {
        CAPTURE(SWTest::name(signal));

        // The golden fixture: silent, exactly.
        CHECK(peakOf("Convolver", signal) == 0.0f);

        /// \note And with the type set to `Continuous` it convolves the main
        /// input with the side channel every block. The golden harness feeds the
        /// side chain the main signal unless told otherwise, so this is the main
        /// input convolved with itself -- which is a real render and not silence.
        CHECK(peakOf("Convolver", signal, [](Module &module) {
                  module.setEffectParameter(convolutionType, continuous);
              }) > 0.0f);
    }
}

TEST_CASE("Frecho's echo needs a render longer than its round trip", "[effects][silent-defaults]")
{
    /// Frecho's parameters, in declaration order. Frevcho shares them.
    enum : std::uint8_t
    {
        distance = 0,
        absorption = 1,
        echoPitch = 2
    };

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Arithmetic rather than a guess: the delay is
    /// `Distance * 2 * 1000 / 343` milliseconds (`frechoImpl.cpp`), so the 100 m
    /// default is a **583 ms** round trip. Against the matrix's 500 ms default
    /// length that is silence, and against the 2 s these two are given it is an
    /// echo -- which is the whole of why `needsALongRender()` exists.
    ///
    ///   Both halves are asserted because either alone is consistent with
    /// something else: silence alone could be a broken effect, and sound alone
    /// could be an effect that ignores `Distance`.
    ///
    ////////////////////////////////////////////////////////////////////////////
    for (auto const name : {"Frecho", "Frevcho"})
        for (auto const signal : {Signal::Sweep, Signal::PinkNoise, Signal::Voice})
        {
            CAPTURE(name, SWTest::name(signal));

            // At the matrix's default length the echo has not come back yet...
            CHECK(peakOf(name, signal) == 0.0f);
            // ...at the length these two actually render for, it has.
            CHECK(peakOf(name, signal, {}, setup, longFrames) > 0.0f);
            // ...and pulling the distance in brings it inside the short render,
            // which says the reading is the delay rather than a broken effect.
            // 17 m is the parameter's minimum and puts the round trip at 99 ms.
            CHECK(peakOf(name, signal,
                         [](Module &module) { module.setEffectParameter(distance, 17); }) > 0.0f);
        }

    /// \note And the boundary, which is what says the subject is the *delay*
    /// rather than "small numbers work": the short render is 500 ms, so a
    /// distance whose round trip is just under that sounds and one just over it
    /// does not. 80 m is 466 ms; 90 m is 525 ms.
    CHECK(peakOf("Frecho", Signal::PinkNoise,
                 [](Module &module) { module.setEffectParameter(distance, 80); }) > 0.0f);
    CHECK(peakOf("Frecho", Signal::PinkNoise,
                 [](Module &module) { module.setEffectParameter(distance, 90); }) == 0.0f);
}

TEST_CASE("Frevcho needs more than one round trip, which is why the long render is 2 s",
          "[effects][silent-defaults]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The measurement that stops `longRenderedFrames` from being
    /// "583 ms plus a bit". Two things stack on top of the round trip:
    ///
    ///     - `Signal::Impulse` puts its transient at `frames / 4`, so the
    ///       transient moves with the render and its echo lands at
    ///       `frames / 4 + 583 ms`. That alone needs 777 ms of render.
    ///     - Frevcho is a reverser in front of the same delay
    ///       (`FrevchoImpl::process` runs `getCurrentStepData` and then
    ///       `FrechoImpl::doProcess`), and needs more than the one trip Frecho
    ///       does.
    ///
    ///   Measured at 512/4 and 2048/8: Frevcho's impulse render is silent at
    /// 1000 ms, is 4e-9 at 1050, and only lands properly at 1100. So a 750 ms
    /// matrix leaves four rows silent and a 1000 ms one leaves two; 2 s clears
    /// both configurations with most of a second to spare.
    ///
    /// \note This is the case that should fail if someone shortens the long
    /// render back down. Without it the constant looks like an arbitrary round
    /// number and the four impulse rows go quietly back to pinning nothing.
    ///
    ////////////////////////////////////////////////////////////////////////////
    constexpr std::uint32_t oneSecond{44100};

    // Frecho is inside one second...
    CHECK(peakOf("Frecho", Signal::Impulse, {}, setup, oneSecond) > 0.0f);
    // ...and Frevcho, over the same signal at the same length, is not.
    CHECK(peakOf("Frevcho", Signal::Impulse, {}, setup, oneSecond) == 0.0f);

    // Both are inside the length the matrix gives them, at both of its
    // configurations -- which is what the sixteen fixtures rest on.
    constexpr SWTest::RenderSetup wide{2048, 8, 2, 44100, 256};
    for (auto const &configuration : {setup, wide})
        for (auto const name : {"Frecho", "Frevcho"})
        {
            CAPTURE(name, configuration.fftSize);
            CHECK(peakOf(name, Signal::Impulse, {}, configuration, longFrames) > 0.0f);
        }
}

TEST_CASE("Freqnamics gates an impulse away at 2048 bins", "[effects][silent-defaults]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The odd one out: a single silent fixture rather than a family of
    /// eight, and the only one whose *configuration* decides it. One impulse
    /// spread across 2048 bins is 12 dB quieter per bin than the same impulse
    /// across 512, and the noise gate sits at −60 dB -- so the same effect over
    /// the same signal is silent at one FFT size and not at the other.
    ///
    ///   Measured both ways: the 512-bin render sounds untouched, and opening
    /// the gate makes the 2048-bin one sound. Either alone would be consistent
    /// with something else.
    ///
    ////////////////////////////////////////////////////////////////////////////
    enum : std::uint8_t
    {
        limiterThreshold = 0,
        noisegateThreshold = 1
    };

    constexpr SWTest::RenderSetup wide{2048, 8, 2, 44100, 256};

    // The golden pair, as committed: silent at 2048/8 and not at 512/4.
    CHECK(peakOf("Freqnamics", Signal::Impulse, {}, wide) == 0.0f);
    CHECK(peakOf("Freqnamics", Signal::Impulse) > 0.0f);

    // ...and the gate is what does it, rather than the bin count on its own.
    CHECK(peakOf(
              "Freqnamics", Signal::Impulse,
              [](Module &module) { module.setEffectParameter(noisegateThreshold, -90); },
              wide) > 0.0f);
}

TEST_CASE("The number of golden fixtures that pin nothing may not grow",
          "[effects][silent-defaults]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note A silent render hashes identically on every platform and its
    /// numeric columns are all zero, so a row like that agrees with any build at
    /// all -- it is a fixture in the file and not a fixture in effect. There are
    /// 9, they are explained above, and this is what keeps that number from
    /// quietly becoming 40 the next time a default moves.
    ///
    /// \note It was 25 until the long render landed on 10.08.2026, and the
    /// sixteen it removed were Frecho's and Frevcho's. The number may only fall.
    ///
    /// \note Read out of the committed file rather than rendered, deliberately.
    /// It runs in a checked build, where the goldens themselves skip; and what
    /// is being asserted is a property of the *fixture set* rather than of a
    /// render, so re-rendering it would be asking a different question slowly.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const golden(SWTest::readFixtures(std::string(SW_GOLDEN_DATA_DIR) + "/goldens.txt"));
    REQUIRE(golden.fixtures.size() > 400);

    std::vector<std::string> silent;
    for (auto const &[key, digest] : golden.fixtures)
        if (digest.peak == 0)
            silent.push_back(key);

    for (auto const &key : silent)
        UNSCOPED_INFO(key);

    /// The two families this file explains, and nothing else. Should a default
    /// change and take a row out of the set, this is the line to lower.
    CHECK(silent.size() == 9);

    // And the side-chain fixtures have none at all, which their own case also
    // asserts -- here because it is the same property about the same kind of file.
    auto const sideChain(SWTest::readFixtures(std::string(SW_GOLDEN_DATA_DIR) + "/sideChain.txt"));
    REQUIRE_FALSE(sideChain.fixtures.empty());
    for (auto const &[key, digest] : sideChain.fixtures)
    {
        INFO(key);
        CHECK(digest.peak > 0);
    }
}
