////////////////////////////////////////////////////////////////////////////////
///
/// sideChainTests.cpp
/// ------------------
///
///   The side chain, fed something other than the main signal.
///
///   Until 05.08.2026 nothing ever did. `tests/goldens/engineHarness.cpp`
/// passed `inputPointers.data()` twice and the comment above it claimed the
/// side-chain effects "are driven separately"; they were not. That is the one
/// arrangement in which a side-chain effect cannot be told apart from a bug that
/// ignores the side chain entirely -- hand Blender the same spectrum on both
/// ports and it renders exactly what an effect which dropped the side channel on
/// the floor would render. Every fixture the fifteen had could not fail.
///
///   The property is stated in both directions:
///
///     - an effect that reads a side chain renders differently when the side
///       chain differs, and
///     - one that does not renders **bit-identically**, whatever is on the port.
///
///   The second is what makes the first mean something. The engine transforms
/// the side channel alongside the main input whenever the port is connected,
/// whatever is in the chain, so "the render moved" would be as consistent with
/// that analysis leaking into the main path as with an effect reading its
/// carrier. Forty-two effects say it does not leak.
///
/// \note **Four of the fifteen hear nothing at their defaults**, which is the
/// finding this file was written by. Slicer, Denoiser and Convolver each have an
/// enumerated Mode whose *first* enumerator -- and so its default -- is the one
/// that does not look at the side channel, and Burrito's replacement positions
/// are only chosen once its 250 ms period wraps around, which a short render
/// never reaches. So a side-chain fixture at default parameters is still a
/// fixture that cannot fail, and `engage` below is each effect's smallest
/// parameter change that makes it listen.
///
/// \note **What decides is the `process()` overload**, not any declaration. An
/// effect reads the side channel exactly when its `process()` takes a
/// `MainSideChannelData`, so the set below is what the effects were observed to
/// do rather than what they claim. Every effect used to carry a
/// `usesSideChannel` constant beside its title; nothing ever read it and it
/// named seven of these fifteen, so it was deleted on 08.08.2026 rather than
/// corrected -- a second answer to this question is a second answer to get
/// wrong.
///
/// \note Properties rather than fixtures, for the reason amplifyingEffectsTests
/// gives: the goldens render in Release only and these run in both build types.
/// The numbers are pinned below, under `[golden][side-chain]`.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "goldens/engineHarness.hpp"
#include "goldens/goldenDigest.hpp"

#include "le/spectrumworx/effects/configuration/constants.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
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

/// \note 512/4 over 8192 frames -- the goldens' smaller configuration at half
/// their length. Long enough for a carrier to reach the output through the
/// engine's latency, short enough that sweeping the whole effect list twice is
/// affordable in a checked build.
constexpr SWTest::RenderSetup setup{512, 4, 2, 44100, 256};
constexpr std::uint32_t frames{8192};

/// \note A sweep against pink noise: the "sine against noise" the item asked
/// for, and both generators were already in the corpus. A moving partial and a
/// dense broadband spectrum have nothing in common at any bin, so an effect
/// combining them cannot coincidentally land back on what either alone produces.
constexpr Signal mainSignal{Signal::Sweep};
constexpr Signal sideSignal{Signal::PinkNoise};

////////////////////////////////////////////////////////////////////////////////
///
/// \struct SideChainEffect
///
/// \brief One effect that reads a side chain, and what it has to be told before
/// it will.
///
/// \note `engage` runs after the module is in the chain and before the first
/// block; indices are into the effect's own LE_DEFINE_PARAMETERS order and
/// values are in its own units. Empty means the effect listens at its defaults,
/// which eleven of the fifteen do.
///
////////////////////////////////////////////////////////////////////////////////

struct SideChainEffect
{
    std::string_view name;
    std::function<void(Module &)> engage{};
};

/// Slicer's parameters, in declaration order, and its Mode enumerators.
enum SlicerParameter : std::uint8_t
{
    slicerTimeOn = 0,
    slicerTimeOff = 1,
    slicerMode = 2
};
enum SlicerMode : int
{
    slicerHold = 0,
    slicerSilence = 1,
    slicerSide = 2
};

enum DenoiserParameter : std::uint8_t
{
    denoiserMode = 0,
    denoiserIntensity = 1
};
enum DenoiserMode : int
{
    denoiserFromMain = 0,
    denoiserFromSide = 1,
    denoiserFromSum = 2
};

enum ConvolverParameter : std::uint8_t
{
    convolutionType = 0,
    convolverGrabIR = 1,
    convolverPhase = 2
};
enum ConvolutionType : int
{
    convolverTriggered = 0,
    convolverContinuous = 1
};

enum BurritoParameter : std::uint8_t
{
    burritoMode = 0,
    burritoRange = 1,
    burritoPeriod = 2,
    burritoSideGain = 3
};

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The fifteen, and what each needs.
///
/// \note Measured on 05.08.2026 by rendering all 57 both ways and reading off
/// which moved -- the numbers are in the case below -- rather than read off any
/// declaration. Adding a row should mean the same work: render it both ways and
/// look at the number. A row without one is an effect nobody drove.
///
/// \note Named by *streaming* name, which is why one of them reads "(pvd)"
/// where the interface says "(PV)". \see sideChainEffect() below.
///
////////////////////////////////////////////////////////////////////////////////

SideChainEffect const sideChainEffects[]{
    {"Pitch Follower"},
    {"Pitch Follower (pvd)"},
    {"Talking Wind"},
    {"Ethereal"},
    {"Vaxateer"},
    {"Shapeless"},
    {"Colorifer"},
    {"Merger"},
    {"Blender"},
    {"Inserter"},
    {"Sumo Pitch"},

    /// \note Mode defaults to `Hold`, which repeats the last main-channel frame.
    /// `Side` is the enumerator that fills the slice from the side channel, and
    /// it is the third of three. The times come down with it because a 250 ms
    /// slice never completes inside this render.
    {"Slicer",
     [](Module &module) {
         module.setEffectParameter(slicerMode, slicerSide);
         module.setEffectParameter(slicerTimeOn, 20);
         module.setEffectParameter(slicerTimeOff, 20);
     }},

    /// \note Mode defaults to `Main` -- the noise footprint taken from the
    /// signal being denoised, which needs no side channel at all. Intensity goes
    /// up with it so that the subtraction is audible rather than arithmetic.
    {"Denoiser",
     [](Module &module) {
         module.setEffectParameter(denoiserMode, denoiserFromSide);
         module.setEffectParameter(denoiserIntensity, 100);
     }},

    /// \note ConvolutionType defaults to `Triggered`: the impulse response is
    /// taken from the side channel when the GrabIR button is pressed, and until
    /// then there is no response and the effect renders **silence**. Which is
    /// three of the 25 identically-hashed golden fixtures issue #22
    /// records -- Convolver at its defaults is not a quiet render, it is an
    /// unarmed one.
    {"Convolver",
     [](Module &module) { module.setEffectParameter(convolutionType, convolverContinuous); }},

    /// \note Nothing to do with the side channel and everything to do with time:
    /// the replacement positions are chosen when the frame counter wraps its
    /// Period, which defaults to 250 ms. This render is 186 ms long, so at the
    /// default the position set is still empty when it ends and no bin is ever
    /// replaced. The minimum period wraps eighteen times instead.
    {"Burrito", [](Module &module) { module.setEffectParameter(burritoPeriod, 10); }},
};

/// \note Matched on the streaming name, and so is the table above: a title is
/// free to move and a row that stopped matching would quietly drop an effect
/// from the sweep rather than fail it. \see SWTest::effectByStreamingName().
SideChainEffect const *sideChainEffect(std::uint8_t const effect)
{
    std::string_view const name(Effects::effectStreamingName(effect));
    auto const found(std::ranges::find(sideChainEffects, name, &SideChainEffect::name));
    return (found != std::end(sideChainEffects)) ? &*found : nullptr;
}

//------------------------------------------------------------------------------
// Measurement
//------------------------------------------------------------------------------

float peak(std::span<float const> samples)
{
    float largest{0};
    for (auto const sample : samples)
        largest = std::max(largest, std::abs(sample));
    return largest;
}

bool allFinite(std::span<float const> samples)
{
    return std::all_of(samples.begin(), samples.end(),
                       [](float const sample) { return std::isfinite(sample); });
}

/// How far apart two renders of the same length are, relative to the louder.
/// \see amplifyingEffectsTests.cpp, which measures transparency the same way.
float relativeDifference(std::span<float const> a, std::span<float const> b)
{
    REQUIRE(a.size() == b.size());
    float largestDifference{0};
    for (std::size_t index(0); index < a.size(); ++index)
        largestDifference = std::max(largestDifference, std::abs(a[index] - b[index]));
    auto const reference(std::max(peak(a), peak(b)));
    return (reference > 0) ? (largestDifference / reference) : largestDifference;
}

/// \brief One effect, engaged, over \p main with \p side on the side chain.
std::vector<float> renderAt(std::uint8_t const effect, SWTest::RenderSetup const &configuration,
                            std::uint32_t const length, Signal const main, Signal const side)
{
    std::vector<float> mainInput(length), sideInput(length);
    SWTest::generate(main, mainInput, static_cast<float>(configuration.sampleRate));
    SWTest::generate(side, sideInput, static_cast<float>(configuration.sampleRate));

    auto const *const known(sideChainEffect(effect));
    Slot const slots[]{{static_cast<std::int8_t>(effect), known ? known->engage : nullptr}};
    return SWTest::renderChain(configuration, slots, mainInput, sideInput);
}

/// \brief The property cases' render: the standard setup, over the main signal,
/// with \p side on the side chain.
///
/// \param side the main signal for the arrangement every render outside this
/// file uses, and something else for the one that says whether the effect can
/// tell the difference.
std::vector<float> render(std::uint8_t const effect, Signal const side)
{
    return renderAt(effect, setup, frames, mainSignal, side);
}

/// What changing the side chain does to \p effect's output, relative to its own
/// peak. Zero is an effect that cannot hear it.
float movedBySideChain(std::uint8_t const effect)
{
    return relativeDifference(render(effect, mainSignal), render(effect, sideSignal));
}
} // anonymous namespace
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
// The property, both ways round
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A side-chain effect hears what is on the side chain", "[effects][side-chain]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note A tenth of the louder render's peak, which is a floor rather than a
    /// tolerance. Measured, the fifteen move between 0.36 and 1.8: a side chain
    /// carrying an unrelated spectrum is not a perturbation of the render, it is
    /// a different render, and a bound anywhere in the middle says "this effect
    /// combines two signals" without claiming anything about how.
    ///
    ///   The failure it is written against is the one that was live until this
    /// file existed: **exactly zero**, which is what an effect wired to its own
    /// main signal measures -- and, at their defaults, what four of these
    /// fifteen measure too.
    ///
    ////////////////////////////////////////////////////////////////////////////
    constexpr float leastAnEffectThatHearsItMoves{0.1f};

    unsigned int heard{0};
    for (std::uint8_t effect(0); effect < Effects::Constants::numberOfEffects; ++effect)
    {
        if (!sideChainEffect(effect))
            continue;

        INFO(Effects::effectName(effect));
        auto const moved(movedBySideChain(effect));
        UNSCOPED_INFO(std::string(Effects::effectName(effect)) + " moved by " +
                      std::to_string(moved));
        CHECK(moved > leastAnEffectThatHearsItMoves);

        // ...and what it produced is still audio.
        auto const rendered(render(effect, sideSignal));
        CHECK(allFinite(rendered));
        CHECK(peak(rendered) < 100.0f);
        ++heard;
    }

    // Every row of the table is an effect that exists, spelled the way
    // effectStreamingName() spells it -- a typo would otherwise silently test
    // nothing.
    CHECK(heard == std::size(sideChainEffects));
}

TEST_CASE("An effect with no side chain cannot tell what is on the port", "[effects][side-chain]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The control, and what makes the case above mean something. The
    /// engine transforms the side channel alongside the main input whenever the
    /// port is connected, regardless of what is in the chain -- so a render that
    /// moved would be as consistent with that analysis reaching the main path as
    /// with an effect reading its carrier.
    ///
    ///   Bit-identical rather than within a tolerance, deliberately. The side
    /// data goes into buffers of its own and an effect that never asks for them
    /// cannot see them; anything but equality here is a finding about the engine
    /// rather than a rounding argument.
    ///
    ////////////////////////////////////////////////////////////////////////////
    unsigned int checked{0};
    for (std::uint8_t effect(0); effect < Effects::Constants::numberOfEffects; ++effect)
    {
        if (sideChainEffect(effect))
            continue;

        INFO(Effects::effectName(effect));
        CHECK(render(effect, mainSignal) == render(effect, sideSignal));
        ++checked;
    }

    // No silent narrowing: the sweep says how much of the list it covered.
    UNSCOPED_INFO(std::to_string(checked) + " deaf effects compared");
    CHECK((checked + std::size(sideChainEffects)) == Effects::Constants::numberOfEffects);
}

TEST_CASE("The engine's own path ignores the side chain", "[effects][side-chain]")
{
    /// \note The control's control: with no module in the chain at all, the
    /// analysis/resynthesis is the same render whatever the side chain carries.
    /// If this ever fails, every number in `goldens.txt` is in question rather
    /// than one effect's.
    CHECK(SWTest::render(setup, -1, mainSignal, frames) ==
          SWTest::renderWithSideChain(setup, -1, mainSignal, sideSignal, frames));
}

TEST_CASE("A side-chain render is reproducible", "[effects][side-chain]")
{
    // The side chain is a second stream of state through the engine, and state
    // carried between renders -- or read before it is written -- shows up here
    // and nowhere else: the goldens render each fixture once.
    for (std::uint8_t effect(0); effect < Effects::Constants::numberOfEffects; ++effect)
    {
        if (!sideChainEffect(effect))
            continue;
        INFO(Effects::effectName(effect));
        CHECK(render(effect, sideSignal) == render(effect, sideSignal));
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// The fixtures
// ------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   What the property cases above cannot say: the numbers.
///
///   The same digest, file format and provenance rule as `goldenTests.cpp` --
/// see the long note there -- over a fixture set that file cannot produce,
/// because every render in it feeds the side chain the main signal. One row per
/// effect / signal pair / FFT size, so a side-chain path that changes is a
/// change somebody has to read rather than a green run.
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
/// Two configurations, as the goldens use, and two deliberately dissimilar
/// pairs: a moving partial against noise, and a dense harmonic stack against a
/// single click.
constexpr SWTest::RenderSetup fixtureSetups[]{{512, 4, 2, 44100, 256}, {2048, 8, 2, 44100, 256}};

struct SignalPair
{
    Signal main;
    Signal side;
};
constexpr SignalPair fixturePairs[]{{Signal::Sweep, Signal::PinkNoise},
                                    {Signal::Voice, Signal::Impulse}};

constexpr std::uint32_t fixtureFrames{16384};

std::string fixturePath() { return std::string(SW_GOLDEN_DATA_DIR) + "/sideChain.txt"; }

constexpr char const *fixturePreamble[]{
    "SpectrumWorx side-chain fixtures -- generated, do not hand edit.",
    "Regenerate with SW_GOLDEN_UPDATE=1 ./sw-dsp-tests \"[golden][side-chain]\"",
    "",
    "The fifteen effects that read a side chain, rendered with the side chain",
    "fed a different signal from the main input -- which is the arrangement",
    "goldens.txt cannot produce. Four of them are also given the parameter",
    "that makes them listen at all; see sideChainTests.cpp.",
    "",
    "One row per effect/mainSignal-sideSignal/fftSize/overlapFactor. Columns:",
    "  key  hash  peak  rms  dcOffset  nonFinite  band0..band7 (dB)",
    "",
    "hash is FNV-1a over the raw sample bits and is a same-platform contract",
    "only; the numeric columns are what a different architecture or compiler",
    "is held to.",
};

/// \note By streaming name, for the reason keyFor() in goldenTests.cpp gives at
/// length: a retitled effect must not orphan its fixture rows.
std::string keyFor(std::uint8_t const effect, SignalPair const &pair,
                   SWTest::RenderSetup const &configuration)
{
    std::string key;
    for (auto const character : std::string_view(Effects::effectStreamingName(effect)))
        key += (character == ' ') ? '_' : character;
    return key + "/" + SWTest::name(pair.main) + "-" + SWTest::name(pair.side) + "/" +
           std::to_string(configuration.fftSize) + "/" +
           std::to_string(configuration.overlapFactor);
}
} // anonymous namespace

TEST_CASE("Side-chain fixtures", "[golden][side-chain]")
{
    /// \note The same skip and the same reason as `goldenTests.cpp`: the hash is
    /// a same-build contract, and several of the effects in this set render
    /// different bits in a checked build.
#ifndef NDEBUG
    SKIP("Fixtures render in a release build -- see the note in goldenTests.cpp.");
#endif

    std::vector<SWTest::Fixture> fixtures;
    for (std::uint8_t effect(0); effect < Effects::Constants::numberOfEffects; ++effect)
    {
        if (!sideChainEffect(effect))
            continue;
        for (auto const &configuration : fixtureSetups)
            for (auto const &pair : fixturePairs)
                fixtures.push_back(
                    {keyFor(effect, pair, configuration),
                     SWTest::Digest::of(
                         renderAt(effect, configuration, fixtureFrames, pair.main, pair.side),
                         configuration.numberOfChannels,
                         static_cast<float>(configuration.sampleRate))});
    }

    REQUIRE(fixtures.size() ==
            (std::size(sideChainEffects) * std::size(fixtureSetups) * std::size(fixturePairs)));

    if (SWTest::goldenUpdateRequested())
    {
        SWTest::writeFixtures(fixturePath(), fixtures, fixturePreamble);
        WARN("Side-chain fixtures regenerated -- read the diff before committing it.");
        return;
    }

    auto const golden(SWTest::readFixtures(fixturePath()));
    REQUIRE_FALSE(golden.fixtures.empty());
    auto const sameBuild(golden.mintedByThisBuild());

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `amplified()` for all fifteen off the minting machine, rather than
    /// the split `goldenTests.cpp` makes between its nine and its forty-nine.
    ///
    ///   That split is a *measurement* -- the set that exceeded `strict()` with
    /// macOS/Accelerate on one side and Linux/pffft on the other -- and the
    /// equivalent measurement has not been made for these renders. Guessing at
    /// it would be inventing the result: three of the fifteen are pitch
    /// detectors, and Burrito, Slicer and Denoiser each make a per-bin decision
    /// the side chain is now an input to, so the population plausibly leans the
    /// other way from the main file's.
    ///
    ///   The wide bound is therefore what an unmeasured platform gets, and the
    /// real regression net here is the same-platform hash -- which is exactly
    /// what `goldenTests.cpp` says its own nine amount to. When a second
    /// platform runs this, the drift report is what decides the split.
    ///
    /// \note Ethereal, Vaxateer and Merger stood outside even this bound, on
    /// `sameBuildOnly()`. They are held to the same one as the other twelve now
    /// that they are compiled correctly, and what they do is asserted in
    /// `comparingEffectsTests.cpp`. \see issue #21.
    ///
    ////////////////////////////////////////////////////////////////////////////
    std::vector<std::string> failures;
    for (auto const &fixture : fixtures)
    {
        auto const entry(golden.fixtures.find(fixture.key));
        if (entry == golden.fixtures.end())
        {
            failures.push_back(fixture.key + ": no golden");
            continue;
        }
        auto const result(SWTest::compare(entry->second, fixture.digest, sameBuild,
                                          SWTest::Tolerances::amplified()));
        if (!result.matches)
            failures.push_back(fixture.key + ": " + result.explanation);
    }

    /// \note And that nothing in this set renders silence, which is the failure
    /// the whole file exists to close: a silent render hashes identically on
    /// every machine and pins nothing. 25 rows of `goldens.txt` are exactly
    /// that, Convolver's three among them -- and Convolver is in this set
    /// *because* it is armed here.
    for (auto const &fixture : fixtures)
        if (fixture.digest.peak == 0)
            failures.push_back(fixture.key + ": silent, and so pins nothing");

    // Parenthesised whole: UNSCOPED_INFO expands to `builder << log`, and `<<`
    // binds tighter than `?:`.
    UNSCOPED_INFO((sameBuild ? std::string("bit-exact against ") + golden.provenance
                             : std::string("minted by ") + golden.provenance + ", running on " +
                                   SWTest::provenance() + " -- numeric columns only"));
    for (auto const &failure : failures)
        UNSCOPED_INFO(failure);
    CHECK(failures.empty());
}
