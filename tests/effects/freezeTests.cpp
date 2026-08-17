////////////////////////////////////////////////////////////////////////////////
///
/// freezeTests.cpp
/// ---------------
///
///   What Freeze does to a signal, which nothing measured. Its two controls are
/// `TriggerParameter`s -- events rather than values -- so a golden cannot see
/// them: a fixture is rendered from settings made before the first block, and an
/// event set there either does nothing or freezes the whole render.
///
///   So this fires them mid-render and watches the output. The signal is a sine
/// sweep, whose dominant frequency rises the whole way through, and the measure
/// is its zero-crossing rate: no FFT in the test, and a number that tracks the
/// sweep monotonically. Frozen, the spectrum stops moving, so the rate stops
/// rising; melted, it tracks the input again.
///
/// \note Written against issue #65, where the question "the button shows frozen
/// -- is it?" could not be answered from the tree. It could not be answered
/// because nothing here ran the effect at all.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "goldens/engineHarness.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <cstdint>
#include <span>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
using namespace LE;
using namespace LE::SW;

constexpr std::uint32_t sampleRate{48000};
constexpr std::uint32_t blockSize{512};
constexpr SWTest::RenderSetup setup{512, 4, 1, sampleRate, blockSize};

/// Freeze's own parameters, in LE_DEFINE_PARAMETERS order.
enum FreezeParameter : std::uint8_t
{
    freezeTrigger,
    meltTrigger,
    transitionTime
};

////////////////////////////////////////////////////////////////////////////////
///
/// \brief How often \p window crosses zero, per second.
///
///   A proxy for "where the energy is", and the right one here: the input is a
/// single swept partial, so this rises smoothly with it, and it needs no
/// transform to compute. What it cannot do is tell two spectra apart that happen
/// to cross zero equally often -- which is why every claim below is about the
/// number *moving* rather than about its value.
///
////////////////////////////////////////////////////////////////////////////////

double zeroCrossingRate(std::span<float const> const window)
{
    if (window.size() < 2)
        return 0.0;

    std::size_t crossings(0);
    for (std::size_t index(1); index < window.size(); ++index)
        if ((window[index - 1] < 0.0f) != (window[index] < 0.0f))
            ++crossings;

    return (static_cast<double>(crossings) * sampleRate) / static_cast<double>(window.size());
}

/// \brief The rate over the eighth of a second starting at \p second.
double rateAt(std::vector<float> const &rendered, double const second)
{
    auto const from(static_cast<std::size_t>(second * sampleRate));
    auto const length(static_cast<std::size_t>(sampleRate / 8));
    REQUIRE(from + length <= rendered.size());
    return zeroCrossingRate(std::span<float const>(rendered).subspan(from, length));
}

/// \brief A sine sweeping from \p from to \p to over \p frames, linearly.
///
/// \note Its own generator rather than SWTest::Signal::Sweep: what these cases
/// need is one partial whose frequency is a known monotonic function of time,
/// and the golden sweep is shaped for broadband coverage instead.
std::vector<float> sineSweep(std::uint32_t const frames, double const from, double const to)
{
    std::vector<float> signal(frames);
    double phase(0.0);
    for (std::uint32_t frame(0); frame < frames; ++frame)
    {
        double const through(static_cast<double>(frame) / frames);
        phase += 2 * std::numbers::pi * (from + (to - from) * through) / sampleRate;
        signal[frame] = static_cast<float>(0.5 * std::sin(phase));
    }
    return signal;
}

std::int8_t freezeIndex() { return SWTest::effectByStreamingName("Freeze"); }

/// \brief A Freeze slot that fires \p parameter once, at \p second.
///
/// \note Once. `TriggerParameter::setValue` only ORs true in and the engine
/// disarms it in `consumeValue()`, so setting it every block would be a
/// different property -- one this deliberately does not test.
SWTest::Slot freezeFiring(double const freezeAt, double const meltAt,
                          std::uint16_t const transition = 10)
{
    auto const frameOf(
        [](double const second) { return static_cast<std::uint32_t>(second * sampleRate); });

    return SWTest::Slot{freezeIndex(),
                        ////////////////////////////////////////////////////////
                        ///
                        /// \note A short transition rather than the 500 ms
                        /// default, so that the property is about freezing
                        /// rather than about the crossfade into it.
                        ///
                        /// \note **Short, not zero, and zero is the interesting
                        /// number.** The range starts at 0 and process() says a
                        /// zero period means "no transition" -- but
                        /// `inverseTransitionTime_` is `1 / steps`, so zero
                        /// makes it infinity, and the first frame multiplies it
                        /// by a zero frame counter to get NaN. A checked build
                        /// trips `LE_ASSUME( blendFactor >= 0 )` on it; a
                        /// shipping one mixes the NaN into the spectrum.
                        ///                   (16.08.2026.) (SW port)
                        ///
                        ////////////////////////////////////////////////////////
                        [transition](Engine::ModuleParameters &parameters) {
                            parameters.setEffectParameter(transitionTime, transition);
                        },
                        [freezeAt, meltAt, frameOf](std::uint32_t const offset,
                                                    Engine::ModuleParameters &parameters) {
                            auto const firedIn([offset, frameOf](double const second) {
                                auto const frame(frameOf(second));
                                return (frame >= offset) && (frame < offset + blockSize);
                            });
                            if ((freezeAt >= 0) && firedIn(freezeAt))
                                parameters.setEffectParameter(freezeTrigger, 1);
                            if ((meltAt >= 0) && firedIn(meltAt))
                                parameters.setEffectParameter(meltTrigger, 1);
                        }};
}

std::vector<float> renderSweep(SWTest::Slot const &slot, std::uint32_t const frames)
{
    auto const input(sineSweep(frames, 200.0, 4000.0));
    std::array<SWTest::Slot, 1> const slots{slot};
    return SWTest::renderChain(setup, slots, input);
}
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////

TEST_CASE("An unfrozen Freeze passes the sweep through", "[effects][freeze]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The control, and the case that says the measure works at all: with
    /// neither trigger fired, the output has to follow the input the whole way.
    /// Without it, "the rate stopped rising" below would also be satisfied by an
    /// effect that output silence.
    ///
    ////////////////////////////////////////////////////////////////////////////
    constexpr std::uint32_t frames{2 * sampleRate};
    auto const rendered(renderSweep(SWTest::Slot{freezeIndex(), {}, {}}, frames));

    auto const early(rateAt(rendered, 0.4));
    auto const middle(rateAt(rendered, 1.0));
    auto const late(rateAt(rendered, 1.6));

    CAPTURE(early, middle, late);
    CHECK(middle > early * 1.2);
    CHECK(late > middle * 1.2);
}

TEST_CASE("Freezing stops the spectrum and melting lets it move again", "[effects][freeze]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The whole claim in one render: fire Freeze at 0.5 s and Melt at
    /// 1.5 s over a two-second sweep, and read the rate in four places -- once
    /// before the freeze, twice inside it, once after the melt.
    ///
    ///   The two readings *inside* the freeze are what makes this a test of
    /// freezing rather than of the trigger reaching the engine: a Freeze that
    /// merely stopped output would satisfy "it is no longer rising".
    ///
    ////////////////////////////////////////////////////////////////////////////
    constexpr std::uint32_t frames{2 * sampleRate};
    auto const rendered(renderSweep(freezeFiring(0.5, 1.5), frames));

    auto const before(rateAt(rendered, 0.3));
    auto const frozenEarly(rateAt(rendered, 0.8));
    auto const frozenLate(rateAt(rendered, 1.3));
    auto const melted(rateAt(rendered, 1.8));

    CAPTURE(before, frozenEarly, frozenLate, melted);

    // It was moving before the freeze...
    CHECK(frozenEarly > before);

    /// ...and it stops. Within 5 %, not exactly: the frozen frame is resynthesised
    /// by the WOLA every hop, so the output is a steady spectrum rather than a
    /// bit-identical repeat.
    CHECK(std::abs(frozenLate - frozenEarly) < 0.05 * frozenEarly);

    // ...and the melt hands the signal back, by then most of an octave higher.
    CHECK(melted > frozenLate * 1.2);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The bug the two cases above could not see, because they render at a
/// hop smaller than the block. A spectral frame arrives every `fftSize /
/// overlapFactor` samples and a host block every `blockSize`, and the two have
/// nothing to do with each other -- so at a large FFT under a small block there
/// are blocks that produce no frame at all.
///
///   `preProcess()` ran on every one of them, and it is where an effect samples
/// its parameters: `FreezeImpl::setup()` calls `consumeValue()`, which reads a
/// trigger *and disarms it*. A press landing in a frameless block was therefore
/// swallowed before any frame could act on it. Measured at a 2048-sample hop
/// under 512-sample blocks: **5 presses in 20**. An LFO never showed it, because
/// it rearms the parameter every block; a button arms once.
///
/// \note 374 samples, and 1000, because a real host's block size is not a power
/// of two and owes the FFT nothing. 374 against a 2048 hop means the arm usually
/// lands mid-frame with several frameless blocks either side, which is the shape
/// that failed.
///                                           (16.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A press is not swallowed by a block that produces no frame", "[effects][freeze]")
{
    auto const [fft, overlap, hostBlock] =
        GENERATE(table<std::uint16_t, std::uint8_t, std::uint32_t>(
            {{4096, 2, 512}, {4096, 2, 374}, {4096, 2, 1000}, {2048, 4, 374}, {512, 4, 374}}));

    CAPTURE(fft, overlap, hostBlock);
    SWTest::RenderSetup const awkward{fft, overlap, 1, sampleRate, hostBlock};

    /// \note Twenty presses, each in its own render, one host block apart -- so
    /// between them they land at every phase of the frame the hop defines. One
    /// green press proves nothing at a one-in-four failure rate.
    std::size_t froze(0);
    constexpr std::size_t presses{20};
    for (std::uint32_t press(0); press < presses; ++press)
    {
        auto const at(static_cast<double>(40 * hostBlock + press * hostBlock) / sampleRate);
        auto const input(sineSweep(2 * sampleRate, 200.0, 4000.0));
        std::array<SWTest::Slot, 1> const slots{freezeFiring(at, -1)};
        auto const rendered(SWTest::renderChain(awkward, slots, input));

        auto const early(rateAt(rendered, 1.2));
        auto const late(rateAt(rendered, 1.6));
        if (std::abs(late - early) < 0.05 * early)
            ++froze;
    }

    CHECK(froze == presses);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note A transition of no steps at all, which `TransitionTime`'s own range
/// allows and which `process()` documents as meaning "no transition". The
/// implementation formed `1 / steps` for it -- infinity -- and multiplied that
/// by a zero frame counter on the first frame to get NaN. A checked build
/// aborted on `LE_ASSUME( blendFactor >= 0 )`; a shipping one mixed the NaN into
/// the magnitude and frequency arrays, and everything downstream of it in the
/// chain went with it.
///
/// \note **Typing 0 is not the only way in.** A step is a hop, so any transition
/// shorter than `1000 * hop / sampleRate` milliseconds rounds to zero steps --
/// about 10 ms at a 512-sample hop and 48 kHz, and 43 ms at 2048. The second
/// section is that case, reached with a perfectly ordinary-looking 5 ms.
///                                           (16.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A transition of no steps freezes at once rather than producing NaN", "[effects][freeze]")
{
    auto const transition(GENERATE(std::uint16_t{0}, std::uint16_t{5}));
    CAPTURE(transition);

    constexpr std::uint32_t frames{2 * sampleRate};
    auto const rendered(renderSweep(freezeFiring(0.5, -1, transition), frames));

    // Nothing NaN or infinite reached the output...
    for (auto const sample : rendered)
        REQUIRE(std::isfinite(sample));

    // ...and it froze, immediately rather than over half a second.
    auto const before(rateAt(rendered, 0.3));
    auto const frozenEarly(rateAt(rendered, 0.7));
    auto const frozenLate(rateAt(rendered, 1.6));

    CAPTURE(before, frozenEarly, frozenLate);
    CHECK(frozenEarly > before);
    CHECK(std::abs(frozenLate - frozenEarly) < 0.05 * frozenEarly);
}
