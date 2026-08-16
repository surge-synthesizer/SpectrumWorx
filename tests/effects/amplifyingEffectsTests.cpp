////////////////////////////////////////////////////////////////////////////////
///
/// amplifyingEffectsTests.cpp
/// --------------------------
///
///   Properties for the nine effects the golden contract deliberately holds
/// loosely, because a one-ulp FFT difference becomes a percent-level output
/// difference in each of them.
///
///   Those nine -- Pitch Spring, Pitch Spring (PV), Pitch Magnet, Octaver,
/// PVD start, PVD stop, Imploder, Exploder and Slew Limiter -- each make a
/// *decision* somewhere: a pitch detector picks a maximum, a phase vocoder
/// unwraps a phase, the ex/imploder thresholds a bin, the slew limiter compares
/// a rate of change against a limit. One ulp of difference in the spectrum flips
/// a comparison, the chosen bin moves, and the output moves by percent -- 21 %
/// on a peak for Pitch Spring between macOS/Accelerate and Linux/pffft. So
/// `Tolerances::amplified()` is what they are held to off the machine that minted
/// the fixture file, and a bound that wide is not much of a test.
///
///   These are the test instead. Not "the output is these numbers" but "the
/// effect does what it is called": a magnet lands on its target, a spring
/// oscillates and in the direction it was told to, an octaver puts energy an
/// octave away, PVD start and PVD stop are inverses, an imploder sustains, an
/// exploder grows, a slew limiter slows a change down. None of that moves when a
/// bin does, so all of it holds on any platform and in either build type -- the
/// goldens render in Release only, and these do not.
///
/// \note The properties are deliberately one-sided where the effect is:
/// "Up never goes below the input pitch" is a real guarantee, "Up reaches
/// exactly +N cents" is a claim about a pitch detector's accuracy and would be a
/// tolerance argument dressed up as a property.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "goldens/engineHarness.hpp"

#include "le/spectrumworx/effects/configuration/effectNames.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
namespace Effects = LE::SW::Effects;
using SWTest::Slot;

constexpr double pi{std::numbers::pi};

constexpr std::uint32_t sampleRate{44100};
constexpr std::uint8_t channels{2};

/// \note 1024/4 rather than the goldens' 512/4 and 2048/8. The step is then
/// 256 samples, 172 a second, which is what every "per second" parameter here
/// is quantised to -- a slew rate, a glissando, a magnet's speed. Fine enough
/// that a 200 ms oscillation has ~34 frames to draw itself with, coarse enough
/// that a one second render is a few hundred frames rather than a few thousand
/// in a checked build.
constexpr SWTest::RenderSetup standardSetup{1024, 4, channels, sampleRate, 256};

/// \note 2048 for the one property that is a claim about a *frequency* rather
/// than about a level or a direction: Pitch Magnet landing on its target. A
/// phase-vocoder shift resolves at the engine's bin spacing, and at 1024 bins
/// (43 Hz apart) a partial shifted two octaves up smears across three of them --
/// measured, not guessed: the peak landed 377 cents flat of an 880 Hz target,
/// with the target itself the second loudest thing in the spectrum. At 2048 the
/// same render lands within **0.2 cents** of 880, 330 and 110 alike.
///
///   4096 is worse again for the 880 case (82 cents sharp) and not better for
/// the others, so this is not "more bins are better" -- it is one setting where
/// the measurement is unambiguous, chosen by measuring. That the answer is
/// FFT-size dependent at all belongs in issue #19, not in a wider
/// tolerance here.
constexpr SWTest::RenderSetup pitchSetup{2048, 4, channels, sampleRate, 256};

constexpr std::uint32_t oneSecond{sampleRate};

std::int8_t effect(std::string_view const name)
{
    auto const index(Effects::effectIndex(name));
    REQUIRE(index >= 0);
    return index;
}

//------------------------------------------------------------------------------
// Signals
//------------------------------------------------------------------------------

/// A pure tone. One partial, so "the dominant frequency" has one answer.
std::vector<float> tone(double const frequency, std::uint32_t const frames,
                        float const amplitude = 0.5f)
{
    std::vector<float> signal(frames);
    for (std::uint32_t frame(0); frame < frames; ++frame)
        signal[frame] =
            amplitude * static_cast<float>(std::sin(2 * pi * frequency * frame / sampleRate));
    return signal;
}

/// A tone that starts at \p onset and stops at \p offset, both in frames. What
/// an envelope property needs: an attack to be slowed and a release to be held.
std::vector<float> gatedTone(double const frequency, std::uint32_t const frames,
                             std::uint32_t const onset, std::uint32_t const offset)
{
    auto signal(tone(frequency, frames));
    std::fill(signal.begin(), signal.begin() + std::min(onset, frames), 0.0f);
    if (offset < frames)
        std::fill(signal.begin() + offset, signal.end(), 0.0f);
    return signal;
}

//------------------------------------------------------------------------------
// Measurement
//------------------------------------------------------------------------------

/// One channel of an interleaved render, as a span over a frame window.
std::vector<float> window(std::span<float const> interleaved, std::uint32_t const first,
                          std::uint32_t const count, std::uint8_t const channel = 0)
{
    std::vector<float> mono;
    mono.reserve(count);
    auto const frames(interleaved.size() / channels);
    for (std::uint32_t frame(first); (frame < first + count) && (frame < frames); ++frame)
        mono.push_back(interleaved[static_cast<std::size_t>(frame) * channels + channel]);
    return mono;
}

float rms(std::span<float const> mono)
{
    if (mono.empty())
        return 0;
    double sum{0};
    for (auto const sample : mono)
        sum += static_cast<double>(sample) * sample;
    return static_cast<float>(std::sqrt(sum / mono.size()));
}

float peak(std::span<float const> mono)
{
    float largest{0};
    for (auto const sample : mono)
        largest = std::max(largest, std::abs(sample));
    return largest;
}

bool allFinite(std::span<float const> samples)
{
    return std::all_of(samples.begin(), samples.end(),
                       [](float const sample) { return std::isfinite(sample); });
}

/// \brief The magnitude of one frequency, Hann-windowed.
///
/// \note A single-frequency DFT rather than a whole transform. What these
/// properties ask is always "how much is there at *this* frequency" or "where is
/// the largest of these candidates", and both are cheaper and clearer this way
/// than through a spectrum whose bin spacing then has to be argued with.
double magnitudeAt(std::span<float const> mono, double const frequency)
{
    auto const count(mono.size());
    if (count < 2)
        return 0;
    double real{0}, imaginary{0};
    for (std::size_t n(0); n < count; ++n)
    {
        auto const w(0.5 * (1 - std::cos(2 * pi * static_cast<double>(n) / (count - 1))));
        auto const angle(-2 * pi * frequency * static_cast<double>(n) / sampleRate);
        real += mono[n] * w * std::cos(angle);
        imaginary += mono[n] * w * std::sin(angle);
    }
    return std::sqrt(real * real + imaginary * imaginary) / count;
}

/// \brief The loudest frequency between \p lowest and \p highest, in Hz.
///
/// Searched in cents rather than in bins, coarse then fine, because everything
/// being asserted about it is musical -- "an octave above", "within N cents of
/// the target" -- and because a linear scan fine enough to resolve cents at
/// 100 Hz is wasted at 2 kHz.
double dominantFrequency(std::span<float const> mono, double const lowest = 60,
                         double const highest = 4000)
{
    auto const scan([&](double const from, double const to, double const centsPerStep) {
        double best{from}, bestMagnitude{-1};
        auto const step(std::pow(2.0, centsPerStep / 1200));
        for (double frequency(from); frequency <= to; frequency *= step)
        {
            auto const magnitude(magnitudeAt(mono, frequency));
            if (magnitude > bestMagnitude)
            {
                bestMagnitude = magnitude;
                best = frequency;
            }
        }
        return best;
    });

    auto const coarse(scan(lowest, highest, 25));
    return scan(std::max(lowest, coarse / 1.05), std::min(highest, coarse * 1.05), 2);
}

double cents(double const from, double const to) { return 1200 * std::log2(to / from); }

/// How far apart two renders of the same length are, relative to the louder.
float relativeDifference(std::span<float const> a, std::span<float const> b)
{
    REQUIRE(a.size() == b.size());
    float largestDifference{0};
    for (std::size_t index(0); index < a.size(); ++index)
        largestDifference = std::max(largestDifference, std::abs(a[index] - b[index]));
    auto const reference(std::max(peak(a), peak(b)));
    return (reference > 0) ? (largestDifference / reference) : largestDifference;
}

//------------------------------------------------------------------------------
// The chains under test
//------------------------------------------------------------------------------

/// Base parameter indices, in Effects::BaseParameters' declaration order.
enum BaseParameter : std::uint8_t
{
    bypass = 0,
    gain = 1,
    wet = 2,
    startFrequency = 3,
    stopFrequency = 4
};

using Module = LE::SW::Engine::ModuleParameters;

/// The chain with nothing in it: the engine's own analysis/resynthesis, which
/// is what "transparent" is measured against. An effect cannot be compared to
/// its input -- there is an FFT's worth of latency and a window in between.
std::vector<float> dryRender(std::span<float const> input,
                             SWTest::RenderSetup const &setup = standardSetup)
{
    Slot const empty[]{{-1, {}}};
    return SWTest::renderChain(setup, empty, input);
}

std::vector<float> renderOne(std::string_view const name, std::span<float const> input,
                             std::function<void(Module &)> configure = {},
                             SWTest::RenderSetup const &setup = standardSetup)
{
    Slot const slots[]{{effect(name), std::move(configure)}};
    return SWTest::renderChain(setup, slots, input);
}

/// The nine, by the names the golden keys carry.
constexpr std::string_view amplifyingEffects[]{
    "Pitch Spring", "Pitch Spring (PV)", "Pitch Magnet", "Octaver",      "PVD start",
    "PVD stop",     "Imploder",          "Exploder",     "Slew Limiter",
};
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
// What holds for all nine
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A bypassed amplifying effect is exactly the empty chain", "[effects][property]")
{
    // The strongest transparency claim available, and it has to be exact: bypass
    // is a branch taken before any of the effect's own code, so anything other
    // than bit-equality means the *chain* changed, not the effect.
    auto const input(tone(220, oneSecond / 2));
    auto const dry(dryRender(input));

    for (auto const name : amplifyingEffects)
    {
        UNSCOPED_INFO(name);
        auto const bypassed(
            renderOne(name, input, [](Module &module) { module.setBaseParameter(bypass, 1); }));
        REQUIRE(bypassed.size() == dry.size());
        CHECK(bypassed == dry);
    }
}

TEST_CASE("Every amplifying effect renders finite, bounded audio", "[effects][property]")
{
    /// \note The golden suite has this for all 57, and skips in a checked build
    /// -- so on the configuration that runs the ~1200 asserts, nothing checked
    /// it. These nine are the ones whose decisions can land on a denormal or a
    /// divide, so these nine are the ones worth having it for in both.
    auto const input(tone(220, oneSecond / 2));

    for (auto const name : amplifyingEffects)
    {
        UNSCOPED_INFO(name);
        auto const output(renderOne(name, input));
        CHECK(allFinite(output));
        CHECK(peak(output) < 100.0f);
    }
}

TEST_CASE("Every amplifying effect renders the same twice", "[effects][property]")
{
    // A decision that reads uninitialised state, or state carried between
    // renders, shows up here and nowhere else in the suite -- the goldens render
    // each fixture once.
    auto const input(tone(220, oneSecond / 4));

    for (auto const name : amplifyingEffects)
    {
        UNSCOPED_INFO(name);
        CHECK(renderOne(name, input) == renderOne(name, input));
    }
}

////////////////////////////////////////////////////////////////////////////////
// Pitch Spring: an oscillation, and a direction
////////////////////////////////////////////////////////////////////////////////

namespace
{
/// Pitch Spring's effect parameters, in declaration order.
enum SpringParameter : std::uint8_t
{
    springType = 0,
    springDepth = 1,
    springPeriod = 2
};

/// CommonParameters::SpringType's enumerators.
enum SpringDirection : int
{
    symmetric = 0,
    up = 1,
    down = 2
};

/// The dominant frequency in each of \p count windows spread over the render's
/// second half -- the first half is where the effect's own state settles.
std::vector<double> pitchOverTime(std::span<float const> render, unsigned const count)
{
    auto const frames(static_cast<std::uint32_t>(render.size() / channels));
    auto const span(frames / 2);
    auto const width(span / count);
    std::vector<double> pitches;
    for (unsigned index(0); index < count; ++index)
        pitches.push_back(dominantFrequency(window(render, span + index * width, width)));
    return pitches;
}
} // anonymous namespace

TEST_CASE("A pitch spring at zero depth does not move the pitch", "[effects][property][pitch]")
{
    constexpr double input{220};
    auto const rendered(renderOne("Pitch Spring", tone(input, oneSecond), [](Module &module) {
        module.setEffectParameter(springDepth, 0);
    }));

    for (auto const pitch : pitchOverTime(rendered, 4))
    {
        UNSCOPED_INFO(pitch << " Hz");
        CHECK(std::abs(cents(input, pitch)) < 15);
    }
}

TEST_CASE("A pitch spring oscillates, and only where it is told to", "[effects][property][pitch]")
{
    constexpr double input{220};
    constexpr double depthInCents{600};

    auto const spring([&](SpringDirection const direction, std::string_view const name) {
        return pitchOverTime(renderOne(name, tone(input, 2 * oneSecond),
                                       [direction](Module &module) {
                                           module.setEffectParameter(springType, direction);
                                           module.setEffectParameter(springDepth, depthInCents);
                                           module.setEffectParameter(springPeriod, 250);
                                       }),
                             16);
    });

    /// \note Both spellings of the effect. The PVD one is the same oscillator
    /// driving a phase-vocoder shifter instead of the plain one, so the *pitch*
    /// property is identical and is exactly what should be asserted of both --
    /// the goldens can only say that their samples differ.
    for (auto const name : {"Pitch Spring", "Pitch Spring (PV)"})
    {
        UNSCOPED_INFO(name);

        auto const upwards(spring(up, name));
        auto const downwards(spring(down, name));

        auto const extremes([](std::vector<double> const &pitches) {
            auto const [lowest, highest](std::ranges::minmax_element(pitches));
            return std::pair{*lowest, *highest};
        });

        auto const [upLow, upHigh](extremes(upwards));
        auto const [downLow, downHigh](extremes(downwards));
        UNSCOPED_INFO("up " << upLow << ".." << upHigh << " Hz, down " << downLow << ".."
                            << downHigh << " Hz");

        // It oscillates: the range it covers is a real fraction of the depth it
        // was given, rather than one value repeated.
        CHECK(cents(upLow, upHigh) > (depthInCents / 4));
        CHECK(cents(downLow, downHigh) > (depthInCents / 4));

        /// \note One-sided, with a semitone of slack for the detector rather
        /// than for the effect. Up is "the input pitch and above"; the claim
        /// worth making is that it never goes the other way.
        CHECK(cents(input, upLow) > -100);
        CHECK(cents(input, downHigh) < 100);

        // And neither exceeds the depth it was asked for.
        CHECK(cents(input, upHigh) < (depthInCents + 100));
        CHECK(cents(downLow, input) < (depthInCents + 100));
    }
}

////////////////////////////////////////////////////////////////////////////////
// Pitch Magnet: it lands on the target, from either side
////////////////////////////////////////////////////////////////////////////////

namespace
{
enum MagnetParameter : std::uint8_t
{
    magnetTarget = 0,
    magnetSpeed = 1
};
} // anonymous namespace

TEST_CASE("A pitch magnet at zero strength does not move the pitch", "[effects][property][pitch]")
{
    constexpr double input{220};
    auto const rendered(renderOne(
        "Pitch Magnet", tone(input, oneSecond),
        [](Module &module) {
            module.setEffectParameter(magnetTarget, 880);
            module.setEffectParameter(magnetSpeed, 0);
        },
        pitchSetup));

    auto const settled(dominantFrequency(window(rendered, oneSecond / 2, oneSecond / 4)));
    UNSCOPED_INFO(settled << " Hz");
    CHECK(std::abs(cents(input, settled)) < 15);
}

TEST_CASE("A pitch magnet arrives at its target and stays there", "[effects][property][pitch]")
{
    // The property the effect is named for, and the one a golden cannot state:
    // whatever the pitch detector picks and however the bins land, the output
    // pitch ends up at the target frequency. Two octaves up, one down and a
    // fifth up, because the clamp that limits the movement is two-sided and the
    // distance is what decides how long it takes.
    constexpr double input{220};

    auto const magnet([&](double const target) {
        auto const rendered(renderOne(
            "Pitch Magnet", tone(input, 2 * oneSecond),
            [target](Module &module) {
                module.setEffectParameter(magnetTarget, static_cast<float>(target));
                // 60 semitones a second: two octaves inside half a second, so a
                // two second render is settling time and then a long look.
                module.setEffectParameter(magnetSpeed, 60);
            },
            pitchSetup));
        return dominantFrequency(window(rendered, oneSecond, oneSecond / 2), 60, 4000);
    });

    for (double const target : {880.0, 330.0, 110.0})
    {
        auto const arrived(magnet(target));
        UNSCOPED_INFO("target " << target << " Hz, arrived at " << arrived << " Hz, "
                                << cents(target, arrived) << " cents off");
        /// \note Twenty cents, which is a hundredth of the smallest distance
        /// being travelled. It could be one cent -- all three measure at 0.2 --
        /// but the bound worth writing down is the one that says "this is the
        /// note it was asked for" rather than one that pins today's arithmetic.
        CHECK(std::abs(cents(target, arrived)) < 20);
    }
}

////////////////////////////////////////////////////////////////////////////////
// Octaver: an octave, where there was not one
////////////////////////////////////////////////////////////////////////////////

namespace
{
enum OctaverParameter : std::uint8_t
{
    octave1 = 0,
    gainOctave1 = 1,
    octave2 = 2,
    gainOctave2 = 3,
    cutoffFrequency = 4
};

/// Octaver's Octave1/Octave2 enumerators. `Off` is 2, and the implementation
/// takes `value - 2` as the number of octaves -- which is why this reads as an
/// offset rather than as a list.
enum Octave : int
{
    twoDown = 0,
    oneDown = 1,
    off = 2,
    oneUp = 3,
    twoUp = 4
};

/// \note The cutoff is a low pass over the *output*, and it defaults to 350 Hz
/// -- so an Octaver left alone removes everything above 350 Hz including the
/// octave it just added. Every case here opens it, which is the only way to
/// measure what the effect does rather than what its default filter does.
constexpr float openCutoff{16000};
} // anonymous namespace

TEST_CASE("An octaver with both octaves off passes the signal through",
          "[effects][property][octave]")
{
    auto const input(tone(220, oneSecond / 2));
    auto const dry(dryRender(input));
    auto const rendered(renderOne("Octaver", input, [](Module &module) {
        module.setEffectParameter(octave1, off);
        module.setEffectParameter(octave2, off);
        module.setEffectParameter(cutoffFrequency, openCutoff);
    }));

    // Not bit-exact: the signal still goes out through the ReIm domain and back,
    // which is arithmetic the empty chain does not do. Audibly the same, though.
    CHECK(relativeDifference(rendered, dry) < 0.02f);
}

TEST_CASE("An octaver puts energy an octave away", "[effects][property][octave]")
{
    constexpr double input{220};
    auto const signal(tone(input, oneSecond));

    auto const octaver([&](Octave const first, float const octaveGain) {
        return renderOne("Octaver", signal, [first, octaveGain](Module &module) {
            module.setEffectParameter(octave1, first);
            module.setEffectParameter(gainOctave1, octaveGain);
            module.setEffectParameter(octave2, off);
            module.setEffectParameter(cutoffFrequency, openCutoff);
        });
    });

    auto const late([&](std::vector<float> const &rendered) {
        return window(rendered, oneSecond / 2, oneSecond / 4);
    });

    auto const dry(late(dryRender(signal)));
    auto const upOne(late(octaver(oneUp, 0)));
    auto const downOne(late(octaver(oneDown, 0)));

    // The input has essentially nothing an octave either side of itself; the
    // effect's whole job is to put something there.
    CHECK(magnitudeAt(upOne, 2 * input) > (10 * magnitudeAt(dry, 2 * input)));
    CHECK(magnitudeAt(downOne, input / 2) > (10 * magnitudeAt(dry, input / 2)));

    // And the original is still there underneath -- an octaver adds, it does not
    // replace. That is the difference between it and a pitch shifter.
    CHECK(magnitudeAt(upOne, input) > (0.5 * magnitudeAt(dry, input)));
}

TEST_CASE("An octaver's gain decides how much octave there is", "[effects][property][octave]")
{
    // Monotone in a parameter, which is the shape of property a golden cannot
    // express at all: it pins one setting and says nothing about the map.
    constexpr double input{220};
    auto const signal(tone(input, oneSecond));

    double previous{0};
    for (float const octaveGain : {-24.0f, -12.0f, 0.0f, 12.0f})
    {
        auto const rendered(renderOne("Octaver", signal, [octaveGain](Module &module) {
            module.setEffectParameter(octave1, oneUp);
            module.setEffectParameter(gainOctave1, octaveGain);
            module.setEffectParameter(octave2, off);
            module.setEffectParameter(cutoffFrequency, openCutoff);
        }));
        auto const present(magnitudeAt(window(rendered, oneSecond / 2, oneSecond / 4), 2 * input));
        UNSCOPED_INFO(octaveGain << " dB -> " << present);
        CHECK(present > previous);
        previous = present;
    }
}

TEST_CASE("An octaver's cutoff is a low pass on what comes out", "[effects][property][octave]")
{
    /// \note Worth its own case because the parameter is called "Low pass" in
    /// the editor and `CutoffFrequency` in the source, and because its default
    /// of 350 Hz silently removes most of what the effect produces -- which is
    /// the sort of thing that reads as "the octaver is broken".
    constexpr double input{220};
    auto const signal(tone(input, oneSecond));

    auto const withCutoff([&](float const cutoff) {
        auto const rendered(renderOne("Octaver", signal, [cutoff](Module &module) {
            module.setEffectParameter(octave1, oneUp);
            module.setEffectParameter(octave2, off);
            module.setEffectParameter(cutoffFrequency, cutoff);
        }));
        return magnitudeAt(window(rendered, oneSecond / 2, oneSecond / 4), 2 * input);
    });

    // 300 Hz is below the added octave at 440 and below the input at 220.
    CHECK(withCutoff(300) < (0.1 * withCutoff(openCutoff)));
}

////////////////////////////////////////////////////////////////////////////////
// PVD start and PVD stop: inverses
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("PVD start followed by PVD stop is transparent", "[effects][property][pvd]")
{
    // The one property that says what these two are *for*. Analysis converts
    // each bin's phase into an instantaneous frequency and synthesis converts it
    // back; with nothing in between, the pair has to return what it was given.
    // Everything in the PVD group is only meaningful inside that sandwich.
    auto const input(tone(220, oneSecond));
    auto const dry(dryRender(input));

    Slot const sandwich[]{{effect("PVD start"), {}}, {effect("PVD stop"), {}}};
    auto const rendered(SWTest::renderChain(standardSetup, sandwich, input));

    REQUIRE(rendered.size() == dry.size());

    /// \note Compared over the second half. The first is where the phase
    /// vocoder's own accumulator is still catching up with a signal that started
    /// abruptly, and a round trip is not claimed to be transparent through that.
    auto const window([](std::vector<float> const &render) {
        return ::window(render, oneSecond / 2, oneSecond / 2);
    });
    CHECK(relativeDifference(window(rendered), window(dry)) < 0.05f);
}

TEST_CASE("PVD start alone is not transparent", "[effects][property][pvd]")
{
    /// \note The control for the case above, and not a formality: if the pair
    /// were transparent because *neither one did anything*, the round trip would
    /// pass and mean nothing. A phase treated as a frequency is a different
    /// signal, and this is that difference.
    auto const input(tone(220, oneSecond));
    auto const dry(dryRender(input));
    auto const analysed(renderOne("PVD start", input));

    CHECK(relativeDifference(window(analysed, oneSecond / 2, oneSecond / 2),
                             window(dry, oneSecond / 2, oneSecond / 2)) > 0.2f);
}

////////////////////////////////////////////////////////////////////////////////
// Imploder and Exploder: a magnitude that decays, and one that grows
////////////////////////////////////////////////////////////////////////////////

namespace
{
/// PVImploder's parameters (Decay, Gliss, Threshold, Gate) and PVExploder's
/// (Growth, Gliss, Threshold, Gate) share their layout, which is what
/// Detail::ExImPloder exists for.
enum ExImPloderParameter : std::uint8_t
{
    magnitudeScale = 0, ///< Decay for the Imploder, Growth for the Exploder
    glissando = 1,
    limit = 2,
    gate = 3
};
} // anonymous namespace

TEST_CASE("An imploder sustains a note after it stops", "[effects][property][eximploder]")
{
    // What "spectral implosion" amounts to: a bin's magnitude is held and let
    // down slowly rather than following the input. So a note that stops dead
    // leaves a tail, and a longer decay leaves a longer one.
    constexpr double input{440};
    auto const signal(gatedTone(input, 2 * oneSecond, 0, oneSecond / 2));
    auto const dry(dryRender(signal));

    auto const imploder([&](float const decaySeconds) {
        return renderOne("Imploder", signal, [decaySeconds](Module &module) {
            module.setEffectParameter(magnitudeScale, decaySeconds);
            module.setEffectParameter(glissando, 0); // no pitch drift to confuse the measurement
            module.setEffectParameter(limit, -120);
            module.setEffectParameter(gate, -120);
        });
    });

    /// Well after the note stopped and well after the engine's latency, so
    /// anything here is the effect's doing.
    auto const tail([](std::vector<float> const &rendered) {
        return rms(window(rendered, oneSecond, oneSecond / 2));
    });

    auto const dryTail(tail(dry));
    auto const shortDecay(tail(imploder(1)));
    auto const longDecay(tail(imploder(200)));

    UNSCOPED_INFO("dry " << dryTail << ", 1 s decay " << shortDecay << ", 200 s decay "
                         << longDecay);

    // There is a tail at all, and it is the effect's rather than the window's.
    CHECK(longDecay > (10 * dryTail));
    // And it is monotone in the decay time, which is what the parameter means.
    CHECK(longDecay > shortDecay);
}

TEST_CASE("An imploder never makes a frequency louder than it has been",
          "[effects][property][eximploder]")
{
    /// \note The energy bound stage 4.4 asks for, and it is an invariant of the
    /// algorithm rather than a measured limit: the accumulator takes the current
    /// amplitude only when that is the *larger*, and otherwise multiplies by a
    /// decay below 1. So no bin can exceed the loudest it has been -- which is
    /// what separates this effect from the Exploder, whose scale is deliberately
    /// above 1.
    ///
    /// \note Stated per frequency and not as a sample peak, because the sample
    /// peak is not bounded and should not be expected to be: four overlapping
    /// windows all holding the same magnitude add up in phase where the input's
    /// did not, and this render measures **1.6x** the dry peak while every bin
    /// in it obeys the rule. An invariant of the spectrum, asserted in the
    /// spectrum.
    constexpr double input{440};
    auto const signal(gatedTone(input, oneSecond, 0, oneSecond / 2));
    auto const dry(dryRender(signal));
    auto const rendered(renderOne("Imploder", signal, [](Module &module) {
        module.setEffectParameter(magnitudeScale, 200);
        module.setEffectParameter(glissando, 0);
        module.setEffectParameter(limit, -120);
        module.setEffectParameter(gate, -120);
    }));

    auto const at([&](std::vector<float> const &render, std::uint32_t const first) {
        return magnitudeAt(window(render, first, oneSecond / 8), input);
    });

    auto const dryWhileSounding(at(dry, oneSecond / 8));
    auto const whileSounding(at(rendered, oneSecond / 8));
    auto const afterwards(at(rendered, 3 * oneSecond / 4));

    UNSCOPED_INFO("dry " << dryWhileSounding << ", sounding " << whileSounding << ", tail "
                         << afterwards);

    // Never louder than the input was at that frequency, while the note plays...
    CHECK(whileSounding <= (1.05 * dryWhileSounding));
    // ...nor afterwards, when the accumulator is all there is left.
    CHECK(afterwards <= whileSounding);
    // And it really is a decay rather than a mute: the tail is still there.
    CHECK(afterwards > (0.1 * whileSounding));
}

TEST_CASE("An exploder grows a steady note", "[effects][property][eximploder]")
{
    // The mirror image of the Imploder: the magnitude scale is deliberately
    // above 1, so a constant input comes out rising.
    //
    /// \note A *quiet* input, and the growth measured inside the first second.
    /// "Limit" is not a ceiling the level approaches -- reaching it resets the
    /// accumulator to whatever the input is doing, and the effect starts again
    /// from there. So the honest property is about the growth phase, and a test
    /// that measured "later is louder than earlier" across a whole render would
    /// be sampling a sawtooth at two arbitrary points. Measured: at Growth 1 s
    /// and Limit -20 dB this render is a ~7x climb over 1 s and then a reset.
    constexpr double input{440};
    auto const signal(tone(input, 2 * oneSecond, 0.05f));

    auto const exploder([&](float const growthSeconds) {
        return renderOne("Exploder", signal, [growthSeconds](Module &module) {
            module.setEffectParameter(magnitudeScale, growthSeconds);
            module.setEffectParameter(glissando, 0);
            module.setEffectParameter(limit, -20);
            module.setEffectParameter(gate, -120);
        });
    });

    /// Quarter-second windows across the first second: the growth phase.
    auto const climb([](std::vector<float> const &rendered) {
        std::vector<float> levels;
        for (std::uint32_t quarter(0); quarter < 4; ++quarter)
            levels.push_back(rms(window(rendered, quarter * oneSecond / 4, oneSecond / 4)));
        return levels;
    });

    auto const dry(climb(dryRender(signal)));
    auto const fast(climb(exploder(1)));
    auto const slow(climb(exploder(200)));

    UNSCOPED_INFO("dry " << dry.front() << ".." << dry.back() << ", fast " << fast.front() << ".."
                         << fast.back() << ", slow " << slow.front() << ".." << slow.back());

    // It grows, monotonically, over an input that does not. Measured at 6.7x
    // over its own start and 17x over the dry render; the bounds are half that,
    // so they say "it climbed" rather than "it climbed this far".
    CHECK(std::ranges::is_sorted(fast));
    CHECK(fast.back() > (3 * fast.front()));
    CHECK(fast.back() > (8 * dry.back()));

    // Faster growth gets further, which is what the parameter means. At 200
    // seconds to climb 120 dB it has barely left the input behind.
    CHECK(fast.back() > slow.back());
    CHECK(slow.back() < (2 * dry.back()));

    // And it does not run away: the reset is what stops it.
    auto const rendered(exploder(1));
    CHECK(allFinite(rendered));
    CHECK(peak(rendered) < 100.0f);
}

////////////////////////////////////////////////////////////////////////////////
// Slew Limiter: a change, slowed
////////////////////////////////////////////////////////////////////////////////

namespace
{
enum SlewParameter : std::uint8_t
{
    slewDirection = 0,
    slewRate = 1
};

/// SlewLimiter::Direction's enumerators.
enum SlewDirection : int
{
    riseAndFall = 0,
    riseOnly = 1,
    fallOnly = 2
};
} // anonymous namespace

TEST_CASE("A slew limiter at its maximum rate limits nothing", "[effects][property][slew]")
{
    // 300 dB/s over a 172 Hz frame rate is 1.7 dB a frame, which no part of a
    // steady tone asks for. The limiter is then a multiply by one.
    auto const input(tone(440, oneSecond));
    auto const dry(dryRender(input));
    auto const rendered(renderOne("Slew Limiter", input, [](Module &module) {
        module.setEffectParameter(slewDirection, riseAndFall);
        module.setEffectParameter(slewRate, 300);
    }));

    CHECK(relativeDifference(window(rendered, oneSecond / 2, oneSecond / 2),
                             window(dry, oneSecond / 2, oneSecond / 2)) < 0.05f);
}

TEST_CASE("A slew limiter slows an attack down", "[effects][property][slew]")
{
    // Rise-limited, so a note that starts abruptly may not get loud as fast as
    // it was told to -- and the higher the rate, the sooner it arrives.
    //
    /// \note The rise starts from `FLT_EPSILON` and not from the input's level:
    /// the implementation floors the previous amplitude there so that a bin can
    /// ever leave silence at all. That is 138 dB below unity, so the parameter's
    /// low end is far slower than it reads -- 3 dB/s needs **46 seconds** to
    /// open, and a three second render at that setting is indistinguishable from
    /// a mute. Measured, and the reason the rates compared here start at 60.
    constexpr std::uint32_t onset{oneSecond / 2};
    auto const signal(gatedTone(440, 3 * oneSecond, onset, 3 * oneSecond));
    auto const dry(dryRender(signal));

    auto const attack([&](float const rate, std::uint32_t const at) {
        auto const rendered(renderOne("Slew Limiter", signal, [rate](Module &module) {
            module.setEffectParameter(slewDirection, riseOnly);
            module.setEffectParameter(slewRate, rate);
        }));
        return rms(window(rendered, at, oneSecond / 4));
    });

    // Half a second after the note starts: still climbing, at every rate, so the
    // three are ordered by how fast they climb.
    constexpr std::uint32_t halfWayIn{onset + oneSecond / 2};
    auto const dryLevel(rms(window(dry, halfWayIn, oneSecond / 4)));
    auto const slowest(attack(60, halfWayIn));
    auto const middling(attack(150, halfWayIn));
    auto const quickest(attack(300, halfWayIn));

    UNSCOPED_INFO("dry " << dryLevel << ", 60 dB/s " << slowest << ", 150 " << middling << ", 300 "
                         << quickest);

    CHECK(slowest < middling);
    CHECK(middling < quickest);
    CHECK(quickest <= dryLevel);

    // Slowed and not removed: at the top of the range it is all the way up
    // within half a second, and at the bottom it is still nowhere near.
    CHECK(quickest > (0.9f * dryLevel));
    CHECK(slowest < (0.1f * dryLevel));
}

TEST_CASE("A slew limiter holds a release up", "[effects][property][slew]")
{
    // The other direction, and the reason Direction exists: fall-limited, the
    // note cannot get quiet as fast as it stopped, so there is a tail where the
    // dry signal has silence.
    constexpr std::uint32_t offset{oneSecond};
    auto const signal(gatedTone(440, 2 * oneSecond, 0, offset));
    auto const dry(dryRender(signal));

    auto const release([&](float const rate) {
        auto const rendered(renderOne("Slew Limiter", signal, [rate](Module &module) {
            module.setEffectParameter(slewDirection, fallOnly);
            module.setEffectParameter(slewRate, rate);
        }));
        return rms(window(rendered, offset + oneSecond / 8, oneSecond / 4));
    });

    auto const dryTail(rms(window(dry, offset + oneSecond / 8, oneSecond / 4)));
    auto const slow(release(3));
    auto const quick(release(60));

    UNSCOPED_INFO("dry " << dryTail << ", 60 dB/s " << quick << ", 3 dB/s " << slow);

    CHECK(slow > dryTail);
    CHECK(slow > quick);
}
