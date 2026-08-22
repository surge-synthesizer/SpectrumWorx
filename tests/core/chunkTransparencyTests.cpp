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
#include "le/spectrumworx/engine/parameters.hpp"
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
/// \brief The sibling property, and the one that caught a live heap overrun:
/// **what the engine is *configured* for does not change the sound either.**
///
///   `setBlockSize()` sizes the input buffers and nothing else -- not the FFT,
/// not the OLA buffers, not the latency. Two engines differing only in it, driven
/// with the same calls over the same samples, must render identically.
///
///   16384 is the point of the case. `InputBuffers::resize` took a
/// `std::uint16_t` and computed `blockSize * sizeof(float)` in one, so at 16384
/// the byte count wrapped to zero: every channel's buffer was laid at the same
/// address, `blockSize_` still recorded 16384, and the next write of a block
/// through `mainChannel()` ran off the end of an allocation sized for none of it.
/// At 65536 the block size itself truncated to zero. Both are reachable from a
/// host: a realtime block is 128-2048, but an **offline render** is where a host
/// hands over something much larger.
///
/// \note **The input gain is what reaches the buffers, and it is why this case
/// drives the engine directly instead of going through `renderChain`.**
/// `SpectrumWorxCore::process` only copies into `buffers().mainChannel()` when
/// `InputGain != 1` -- at unity it passes the caller's pointers straight through
/// and the mis-sized buffers are never touched. The first version of this case
/// rendered at the default gain, and it passed with the bug deliberately put
/// back: it was testing nothing. A non-unity gain is not decoration here, it is
/// the whole reproduction.
///
/// \note Verified by reversion, and it took two goes to make the reversion go
/// red: with the `std::uint16_t` restored, the 16384 and 65536 cases both fail,
/// and 1024 and 4096 keep passing -- which is the shape the wrap predicts.
/// \see issue #67, where a large-render heap overrun is one of the candidate
/// mechanisms.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The configured block size does not change the sound", "[chunking][block-size]")
{
    auto const configuredFor(GENERATE(std::uint32_t{1024}, 4096, 16384, 65536));

    auto const &configuration(configurations[0]);
    auto const call(configuration.hop());

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note **A different signal per channel**, which is the second thing this
    /// case needs and the second thing an earlier version of it got wrong. With
    /// the byte count wrapped to zero every channel's buffer lands at the same
    /// address, so channel 1's gain-scaled copy overwrites channel 0's -- and if
    /// both channels carry the same samples, that overwrite puts back exactly
    /// what was there. Feeding the sweep to both channels made the aliasing
    /// invisible and the case passed with the bug deliberately in place.
    ///
    ///   Pink noise against a sweep, so a channel that has quietly taken the
    /// other's data is not a subtle difference.
    ///
    ////////////////////////////////////////////////////////////////////////////
    std::vector<std::vector<float>> perChannel(channels, std::vector<float>(renderedFrames));
    SWTest::generate(SWTest::Signal::Sweep, perChannel[0], static_cast<float>(sampleRate));
    SWTest::generate(SWTest::Signal::PinkNoise, perChannel[1], static_cast<float>(sampleRate));

    /// \brief One render at a given configured block size, always called in
    /// hop-sized pieces so the only difference is what the engine was sized for.
    auto const renderAt([&](std::uint32_t const blockSize) {
        SWTest::Engine engine;
        engine.setNumberOfChannels(channels, channels);
        engine.setSampleRate(static_cast<float>(sampleRate));
        REQUIRE(engine.setBlockSize(blockSize));
        engine.set<GlobalParameters::FFTSize>(configuration.fftSize);
        engine.set<GlobalParameters::OverlapFactor>(configuration.overlapFactor);
        REQUIRE(engine.initialise());

        /// \note Not unity. See the note above -- at 1.0 nothing writes into the
        /// buffers this case exists to size.
        REQUIRE(engine.set<GlobalParameters::InputGain>(0.5f));

        engine.resume();

        std::vector<std::vector<float>> in(perChannel);
        std::vector<std::vector<float>> out(channels, std::vector<float>(renderedFrames, 0.0f));
        std::vector<float const *> inPointers(channels);
        std::vector<float *> outPointers(channels);

        for (std::uint32_t at(0); at < renderedFrames; at += call)
        {
            auto const frames(std::min(call, renderedFrames - at));
            for (std::uint8_t channel(0); channel < channels; ++channel)
            {
                inPointers[channel] = in[channel].data() + at;
                outPointers[channel] = out[channel].data() + at;
            }
            engine.process(inPointers.data(), inPointers.data(), outPointers.data(), 1.0f, frames);
        }
        engine.suspend();

        std::vector<float> interleaved(std::size_t{renderedFrames} * channels);
        for (std::uint32_t frame(0); frame < renderedFrames; ++frame)
            for (std::uint8_t channel(0); channel < channels; ++channel)
                interleaved[std::size_t{frame} * channels + channel] = out[channel][frame];
        return interleaved;
    });

    auto const expected(renderAt(1024));
    auto const actual(renderAt(configuredFor));

    INFO("configured for " << configuredFor << " samples, called in " << call
                           << "-sample pieces, input gain 0.5");
    REQUIRE(expected.size() == actual.size());
    CHECK(firstDifference(expected, actual) == -1);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The bypassed chain first and on its own, because it is the engine's own
/// analysis/synthesis path with nothing in it. If this fails, the WOLA itself
/// carries per-call state and no effect result below means anything.
///
/// \note **Block sizes that are and are not whole multiples of the hop**, which
/// is the whole point since #83. 480 and 500 leave a short last call of 96 and
/// 116 against the 128-sample hop, and 500 leaves 244 against the 256-sample one
/// -- and a short last call is what the plugin produces on every block whenever
/// the host's block size does not divide by the hop, which is most of the time.
///
///   Before #83 was fixed this is where it failed: the engine could not answer a
/// partial hop and emitted silence into a running stream instead, so a 500-sample
/// block rendered 116 samples later than a 512-sample one, for ever. \see
/// doc/tech/latency.md.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The engine's own WOLA path does not care how the block was cut up", "[chunking]")
{
    auto const hostBlock(GENERATE(std::uint32_t{512}, 1024, 2048, 480, 500));
    SWTest::Slot const bypassed[]{{-1, {}}};

    for (auto const &configuration : configurations)
        requireTransparent(bypassed, configuration, hostBlock, configuration.hop(),
                           SWTest::Signal::Sweep);
}

/// \brief Skipped for a reason that has nothing to do with chunking: Smoother
/// writes a negative amplitude, and the engine's own `Negative` check on
/// `amPhData.amps()` (`channelData.cpp:125`) aborts a Debug run the moment
/// anything renders it. A single whole-block render does it; this file is simply
/// the first thing to render every effect in a checked build. \see issue #84,
/// with which this exclusion should come off.
bool tripsTheEngineInDebug(std::uint8_t const effect)
{
    return std::string_view(Effects::effectStreamingName(effect)) == "Smoother";
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief **A seeded render repeats**, for every effect, on a fresh engine.
///
///   Which is what `SpectrumWorxCore::setRandomSeed()` promises, what every
/// fixture in this repository quietly depends on, and what issue #105 would
/// offer to a user. It is also the only thing that catches an effect whose
/// generator is never *dealt* to: `callSeed()` finds a ChannelState's `seed()`
/// with a `requires`, so an effect that holds a `Math::Rng` and forgets to
/// expose one is not a compile error -- it silently keeps whatever state it
/// constructed with, and the bug is "two instances sound identical" rather than
/// anything a build says out loud.
///
/// \note Caught exactly that during the #86 fix, once `Math::Rng` started
/// constructing from entropy: three effects held a generator, none of them
/// declared `seed()`, and every fixture passed regardless because an unseeded
/// generator was then a shared constant. Two renders that must agree, on two
/// engines, is the shape that has an opinion about it.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The same seed renders the same samples", "[chunking][seed]")
{
    for (std::uint8_t effect(0); effect < Effects::Constants::numberOfEffects; ++effect)
    {
        if (tripsTheEngineInDebug(effect))
            continue;
        INFO("effect " << unsigned(effect) << " (" << Effects::effectName(effect) << ')');
        SWTest::Slot const slots[]{{static_cast<std::int8_t>(effect), {}}};

        auto const &configuration(configurations[0]);
        SWTest::RenderSetup const setup{
            configuration.fftSize, configuration.overlapFactor, channels, sampleRate, 1024, 0};

        /// Two separate engines: renderChain() builds one per call, so a repeat
        /// that agreed only within one instance would prove nothing.
        auto const first(SWTest::renderChain(setup, slots, SWTest::Signal::Sweep, renderedFrames));
        auto const again(SWTest::renderChain(setup, slots, SWTest::Signal::Sweep, renderedFrames));

        REQUIRE(first.size() == again.size());
        CHECK(firstDifference(first, again) == -1);
    }
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
        if (tripsTheEngineInDebug(effect))
            continue;
        INFO("effect " << unsigned(effect) << " (" << Effects::effectName(effect) << ')');
        SWTest::Slot const slots[]{{static_cast<std::int8_t>(effect), {}}};
        requireTransparent(slots, configurations[0], 1024, configurations[0].hop(),
                           SWTest::Signal::Sweep);
    }
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The effect the sweep above could not see, and the reason it could not.
///
///   Burrito draws from a generator, so it belongs to the same family as
/// Freqverb and Whisperer -- but it passed the sweep for two reasons that have
/// nothing to do with being correct. Its default 250 ms period puts its first
/// draw past the end of a half-second render, so it drew *nothing*; and the
/// sweep feeds the side chain the main signal, under which Burrito's default
/// `Replace` at 0 dB copies the input onto itself and which bins it chose cannot
/// be observed at any period.
///
///   Both have to go for the case to mean anything: a short period so it draws
/// more than once, a side chain that is not the main signal so the draws reach
/// the output, and a host block big enough to hold *several* draws -- which is
/// the shape of the bug. One draw event per call preserves its own order (the
/// engine finishes channel 0 before starting channel 1, so a single event lands
/// in the same relative place either way); two or more inside one call do not.
/// Measured before the fix: identical at a 1024-sample block, differing from
/// sample 2051 at 4096.
///
/// \note Which is to say a fixture at default settings is blind here, and was.
/// \see issue #86.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A random effect cannot tell how the block was cut up either", "[chunking]")
{
    // Mode 0, Range 1, Period 2, SideGain 3.
    constexpr std::uint8_t burritoPeriod{2};

    auto const hostBlock(GENERATE(std::uint32_t{1024}, 4096, 16384));

    std::vector<float> main(renderedFrames), side(renderedFrames);
    SWTest::generate(SWTest::Signal::Sweep, main, static_cast<float>(sampleRate));
    SWTest::generate(SWTest::Signal::PinkNoise, side, static_cast<float>(sampleRate));

    SWTest::Slot const slots[]{
        {SWTest::effectByStreamingName("Burrito"), [](LE::SW::Engine::ModuleParameters &module) {
             module.setEffectParameter(burritoPeriod, 10);
         }}};

    auto const &configuration(configurations[0]);
    SWTest::RenderSetup const asOneCall{
        configuration.fftSize, configuration.overlapFactor, channels, sampleRate, hostBlock, 0};
    SWTest::RenderSetup const inChunks{
        configuration.fftSize, configuration.overlapFactor, channels, sampleRate, hostBlock,
        configuration.hop()};

    auto const whole(SWTest::renderChain(asOneCall, slots, main, side));
    auto const chunked(SWTest::renderChain(inChunks, slots, main, side));

    REQUIRE(whole.size() == chunked.size());
    INFO("Burrito at a 10 ms period, host block " << hostBlock << " in one call against "
                                                  << configuration.hop() << "-sample calls");
    auto const difference(firstDifference(whole, chunked));
    if (difference >= 0)
        UNSCOPED_INFO("first differs at sample " << difference << ", worst difference "
                                                 << worstDifference(whole, chunked));
    CHECK(difference == -1);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief What the engine's delay actually is, which is the other half of #83.
///
///   Transparency alone does not say the delay is *right* -- an engine that
/// delayed everything by a constant thousand samples would pass every case above.
/// This says the delay is `fftSize`, which is what `Setup::latencyInSamples()`
/// reports to the host, for every block size.
///
///   Before the fix it was `fftSize - stepSize` for whole-hop blocks and crept
/// upwards from there for any other block size. So the plugin was one hop early
/// against its own PDC claim in the best case and drifting in the worst.
/// \see doc/tech/latency.md.
///
/// \note An impulse rather than a sweep, because "where did it come out" needs an
/// unambiguous subject. The generator puts it on a 1024-sample grid so it is hop
/// aligned in every configuration here -- see the note in engineHarness.cpp.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The engine delays by exactly the latency it reports", "[chunking][latency]")
{
    auto const hostBlock(GENERATE(std::uint32_t{128}, 256, 512, 1024, 480, 500));

    std::vector<float> input(renderedFrames);
    SWTest::generate(SWTest::Signal::Impulse, input, static_cast<float>(sampleRate));
    auto const inputPeak(std::ranges::max_element(input) - input.begin());
    REQUIRE(inputPeak > 0);

    SWTest::Slot const bypassed[]{{-1, {}}};

    for (auto const &configuration : configurations)
    {
        /// \note Clamped, which is what `SpectrumWorxCLAP::process()` does with
        /// its own `std::min(chunk, frames_count - cursor)`: a 128-sample host
        /// block against the 2048/8 hop of 256 is one 128-sample call, not a call
        /// larger than the block. Unclamped this trips the harness's own
        /// "A call larger than the engine was sized for" -- in the checked build
        /// only, which is what the second build directory is for.
        auto const call(std::min(configuration.hop(), hostBlock));

        SWTest::RenderSetup const setup{configuration.fftSize,
                                        configuration.overlapFactor,
                                        channels,
                                        sampleRate,
                                        hostBlock,
                                        call};
        auto const rendered(SWTest::renderChain(setup, bypassed, input));

        // De-interleave channel 0 and find where the impulse came out.
        std::vector<float> left(renderedFrames);
        for (std::uint32_t frame(0); frame < renderedFrames; ++frame)
            left[frame] = rendered[std::size_t{frame} * channels];
        auto const outputPeak(
            std::ranges::max_element(left, {}, [](float const v) { return std::abs(v); }) -
            left.begin());

        INFO("fft " << configuration.fftSize << '/' << unsigned(configuration.overlapFactor)
                    << ", host block " << hostBlock << " in " << call << "-sample calls");
        CHECK((outputPeak - inputPeak) == configuration.fftSize);
    }
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note A chain rather than a slot, because the modules hand each other a
/// spectrum: a phase vocoder pair with something between them is the arrangement
/// in which one module's per-call state could reach another's. To PV and From PV
/// are what bracket it, which is what the factory presets do.
///
/// \note Named by streaming name, which is what those presets say and what the
/// two were titled until 17.08.2026. \see SWTest::effectByStreamingName().
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A phase-vocoder chain does not care either", "[chunking]")
{
    auto const start(SWTest::effectByStreamingName("PVD start"));
    auto const stop(SWTest::effectByStreamingName("PVD stop"));
    auto const shifter(SWTest::effectByStreamingName("Pitch Shifter (pvd)"));

    SWTest::Slot const chain[]{{start, {}}, {shifter, {}}, {stop, {}}};

    auto const hostBlock(GENERATE(std::uint32_t{512}, 1024));
    for (auto const &configuration : configurations)
        requireTransparent(chain, configuration, hostBlock, configuration.hop(),
                           SWTest::Signal::Voice);
}
