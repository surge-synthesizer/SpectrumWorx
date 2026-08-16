////////////////////////////////////////////////////////////////////////////////
///
/// chunkTransparencyTests.cpp
/// --------------------------
///
///   **Handing the engine a block in hop-sized pieces sounds exactly like
/// handing it the whole block.**
///
///   The plugin depends on this and nothing said so. Since "Hear a parameter
/// event where the host timed it", `SpectrumWorxCLAP::process()` cuts every host
/// block into `engineChunkSize()` pieces before `SpectrumWorxCore::process()`
/// sees it, so that a parameter event can be applied at the sample the host
/// timed it for. If chunking were not transparent, everyone's output would have
/// moved with that commit, with no automation involved anywhere -- and the only
/// evidence would have been "it sounds slightly different since the update".
///
///   \see issue #79.
///
/// ## What is compared
///
///   One engine, configured once, driven twice over the same samples: once with
/// the host block handed over whole -- which is what the plugin did before the
/// chunking commit -- and once cut up exactly as `SpectrumWorxCLAP::process()`
/// cuts it, **including its short last piece**. `blockSize % chunk` is not an
/// edge case here; it is what the plugin does on every block whenever the host's
/// block size is not a multiple of the hop, which is most of the time.
///
///   `RenderSetup` had to grow `callSize` for this: `blockSize` feeds
/// `Engine::setBlockSize()`, so rendering twice with different block sizes
/// compares two differently *configured* engines -- different buffer sizing,
/// different latency -- rather than one engine called two ways. Splitting the
/// call granularity off from the configuration is what makes the question
/// askable.
///
/// ## What it deliberately does not claim
///
///   **The unmodulated path.** Every case here leaves the LFOs alone. An LFO is
/// driven by a clock the engine advances once per `process()` call, so a patch
/// with one *legitimately* renders differently when called in pieces -- issue #78
/// is about making that motion per chunk rather than per host block, and it will
/// change the numbers a modulated render produces. Neither state of #78 touches
/// anything below.
///
///   The comparison is bit-exact rather than "within a tolerance", which is a
/// stronger claim than the goldens make of themselves across platforms and can
/// be: both renders run in the same process, on the same build, over the same
/// samples, through an identically configured engine. The only difference is how
/// many times `process()` was called. Nothing about the arithmetic changes, so
/// nothing about the result may.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "goldens/engineHarness.hpp"

#include "le/spectrumworx/effects/configuration/constants.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
using namespace LE::SW;

constexpr std::uint32_t sampleRate{44100};
constexpr std::uint8_t channels{2};

/// Half a second: long enough for the WOLA to fill, run, and be measured.
constexpr std::uint32_t renderedFrames{sampleRate / 2};

/// \brief One spectral configuration and the hop that follows from it.
struct Configuration
{
    std::uint16_t fftSize;
    std::uint8_t overlapFactor;

    std::uint32_t hop() const { return fftSize / overlapFactor; }
};

/// The golden matrix's two, so that a failure here and a failure there are
/// talking about the same engine: hops of 128 and 256.
constexpr Configuration configurations[]{{512, 4}, {2048, 8}};

/// \brief The first sample at which two renders differ, or -1.
///
/// \note The index rather than a bool: "they differ" is not a useful failure
/// message, and *where* says which frame it was -- an early index is the first
/// analysis window, a late one is a state that drifted.
std::ptrdiff_t firstDifference(std::vector<float> const &left, std::vector<float> const &right)
{
    REQUIRE(left.size() == right.size());
    auto const mismatch(std::ranges::mismatch(left, right));
    if (mismatch.in1 == left.end())
        return -1;
    return mismatch.in1 - left.begin();
}

float worstDifference(std::vector<float> const &left, std::vector<float> const &right)
{
    float worst{0};
    for (std::size_t index(0); index < left.size(); ++index)
        worst = std::max(worst, std::abs(left[index] - right[index]));
    return worst;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Renders `slots` twice over the same host block size -- whole, then cut
/// into `chunk`-sized calls -- and requires the two to be identical.
///
////////////////////////////////////////////////////////////////////////////////

void requireTransparent(std::span<SWTest::Slot const> const slots,
                        Configuration const &configuration, std::uint32_t const hostBlock,
                        std::uint32_t const chunk, SWTest::Signal const signal)
{
    SWTest::RenderSetup const asOneCall{
        configuration.fftSize, configuration.overlapFactor, channels, sampleRate, hostBlock, 0};
    SWTest::RenderSetup const inChunks{
        configuration.fftSize, configuration.overlapFactor, channels, sampleRate, hostBlock, chunk};

    auto const whole(SWTest::renderChain(asOneCall, slots, signal, renderedFrames));
    auto const chunked(SWTest::renderChain(inChunks, slots, signal, renderedFrames));

    REQUIRE(whole.size() == chunked.size());

    auto const difference(firstDifference(whole, chunked));
    INFO("fft " << configuration.fftSize << '/' << unsigned(configuration.overlapFactor) << ", hop "
                << configuration.hop() << ", host block " << hostBlock << " in one call against "
                << chunk << "-sample calls (last call "
                << (hostBlock % chunk ? hostBlock % chunk : chunk) << ')');
    if (difference >= 0)
        UNSCOPED_INFO("first differs at sample " << difference << " of " << whole.size()
                                                 << ", worst difference "
                                                 << worstDifference(whole, chunked));
    CHECK(difference == -1);
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
///
/// \note The bypassed chain first and on its own, because it is the engine's own
/// analysis/synthesis path with nothing in it. If this fails, the WOLA itself
/// carries per-call state and no effect result below means anything.
///
/// \note **Block sizes that are whole multiples of the hop.** That restriction is
/// not a convenience and it is not the property being weakened to fit -- it is
/// where the property turns out to hold and where it turns out not to. Measured
/// on the bypassed chain at 512/4, hop 128:
///
///     one call of 512 against four calls of 128     identical
///     one call of 512 against one call of 500       differs from sample 768
///     one call of 512 against one call of 116       differs from sample 2
///     one call of 500 against 128,128,128,116       differs from sample 10752
///
///   The second and third lines have no chunking in them at all. **The engine has
/// never been indifferent to a call whose length is not a multiple of the hop**,
/// and `SpectrumWorxCLAP::process()` chunking at the hop did not introduce that
/// -- it inherited it, and reaches it through the short last call of a block. The
/// case below is what states it, and issue #83 is where the engine bug lives.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The engine's own WOLA path does not care how the block was cut up", "[chunking]")
{
    auto const hostBlock(GENERATE(std::uint32_t{512}, 1024, 2048));
    SWTest::Slot const bypassed[]{{-1, {}}};

    for (auto const &configuration : configurations)
        requireTransparent(bypassed, configuration, hostBlock, configuration.hop(),
                           SWTest::Signal::Sweep);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The two effects whose output moves when the *number of calls* moves,
/// even with every call a whole hop.
///
///   Measured, 512/4, hop 128, a 1024-sample block handed over whole against the
/// same block as eight calls of 128:
///
///     Freqverb    differs from sample 259, worst 4.30
///     Whisperer   differs from sample 3,   worst 0.90
///
///   Every other effect is bit-identical, so this is not the engine's WOLA
/// bookkeeping -- that is pinned above and passes. `ModuleDSP::preProcess()`
/// (`module.cpp:27`) runs `updateBaseParametersFromLFOs`,
/// `updateEffectParametersFromLFOs` and `setup()` **once per `process()` call**,
/// and chunking multiplies the number of calls by the number of chunks. For an
/// effect whose `setup()` merely samples its parameters that is free; for one
/// with per-call state behind it, it is not. Which of the three it is for these
/// two is not yet established -- neither reads the RNG, and `FreqverbImpl::setup`
/// reads as idempotent, so the answer is somewhere under `doPreProcess`.
///
///   Both renders are deterministic -- the same render run twice is bit-identical
/// for both effects -- so this is a real property of the engine and not an
/// uninitialised-state artefact of the harness.
///
/// \note Excluded by name rather than by a wider tolerance. A bound loose enough
/// to admit a peak difference of 4.3 would admit anything, and naming them keeps
/// the sweep below a bit-exact claim about the other sixty-odd.
///                                           (16.08.2026.)
///
////////////////////////////////////////////////////////////////////////////////

bool callCountDependent(std::uint8_t const effect)
{
    std::string_view const name(Effects::effectName(effect));
    return (name == "Freqverb") || (name == "Whisperer");
}

/// \brief Skipped for a reason that has nothing to do with chunking: Smoother
/// writes a negative amplitude, and the engine's own `Negative` check on
/// `amPhData.amps()` (`channelData.cpp:125`) aborts a Debug run the moment
/// anything renders it. A single whole-block render does it; this file is simply
/// the first thing to render every effect in a checked build. \see issue #84,
/// with which this exclusion should come off.
///                                           (16.08.2026.)
bool tripsTheEngineInDebug(std::uint8_t const effect)
{
    return std::string_view(Effects::effectName(effect)) == "Smoother";
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note And then every effect, at one block size. Sixty-odd effects times three
/// block sizes times two configurations is a render matrix this file does not
/// need -- the property is the engine's, and an effect participates in it only
/// through whatever per-call state it keeps. What finds such a state is running
/// *every* effect at least once, not running a few of them many ways.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("No effect can tell how the block was cut up", "[chunking]")
{
    for (std::uint8_t effect(0); effect < Effects::Constants::numberOfEffects; ++effect)
    {
        if (callCountDependent(effect) || tripsTheEngineInDebug(effect))
            continue;
        INFO("effect " << unsigned(effect) << " (" << Effects::effectName(effect) << ')');
        SWTest::Slot const slots[]{{static_cast<std::int8_t>(effect), {}}};
        requireTransparent(slots, configurations[0], 1024, configurations[0].hop(),
                           SWTest::Signal::Sweep);
    }
}

/// \brief The two above, so the exclusion carries its own reproduction.
TEST_CASE("Freqverb and Whisperer depend on the number of calls", "[.][chunking-known-bad]")
{
    for (std::uint8_t effect(0); effect < Effects::Constants::numberOfEffects; ++effect)
    {
        if (!callCountDependent(effect))
            continue;
        INFO("effect " << unsigned(effect) << " (" << Effects::effectName(effect) << ')');
        SWTest::Slot const slots[]{{static_cast<std::int8_t>(effect), {}}};
        requireTransparent(slots, configurations[0], 1024, configurations[0].hop(),
                           SWTest::Signal::Sweep);
    }
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The half that does **not** hold, stated rather than left out. `[.]`, so
/// it runs only when asked for by name or by tag.
///
///   A host whose block size is not a multiple of the hop -- 500 samples against a
/// 128-sample hop, and any block at all once the FFT is large enough that the hop
/// exceeds it -- gets a last call of `block % hop` samples, and the engine's
/// output then depends on how the block was cut. \see issue #83 for the mechanism
/// (`Processor::processSingleChannel` zero-fills whenever a loop iteration finds
/// less ready output than it must produce, and never makes the shortfall up, so
/// each occurrence inserts silence and delays everything after it).
///
/// \note Hidden rather than deleted, and hidden rather than `[!mayfail]`. Deleting
/// it would lose the reproduction; `[!mayfail]` would run two renders per effect
/// on every CI run to record a failure nobody reads. Hidden keeps it one command
/// away -- `./sw-dsp-tests "[chunking-known-bad]"` -- and keeps the numbers above
/// honest by keeping the thing that produced them.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A block that is not a whole number of hops is cut-dependent", "[.][chunking-known-bad]")
{
    SWTest::Slot const bypassed[]{{-1, {}}};
    for (auto const &configuration : configurations)
        requireTransparent(bypassed, configuration, 500, configuration.hop(),
                           SWTest::Signal::Sweep);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note A chain rather than a slot, because the modules hand each other a
/// spectrum: a phase vocoder pair with something between them is the arrangement
/// in which one module's per-call state could reach another's. PVD start and PVD
/// stop are what bracket it, which is what the factory presets do.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A phase-vocoder chain does not care either", "[chunking]")
{
    auto const start(Effects::effectIndex("PVD start"));
    auto const stop(Effects::effectIndex("PVD stop"));
    auto const shifter(Effects::effectIndex("Pitch Shifter (PV)"));
    REQUIRE(start >= 0);
    REQUIRE(stop >= 0);
    REQUIRE(shifter >= 0);

    SWTest::Slot const chain[]{{start, {}}, {shifter, {}}, {stop, {}}};

    auto const hostBlock(GENERATE(std::uint32_t{512}, 1024));
    for (auto const &configuration : configurations)
        requireTransparent(chain, configuration, hostBlock, configuration.hop(),
                           SWTest::Signal::Voice);
}
