////////////////////////////////////////////////////////////////////////////////
///
/// comparingEffectsTests.cpp
/// -------------------------
///
///   Properties for the three effects that compare two spectra bin by bin and
/// substitute one for the other: Ethereal, Vaxateer and Merger.
///
///   All three read a value from the main channel and a value from the side
/// channel, compare them -- to each other or to a threshold -- and where the
/// comparison holds, copy the side channel's bin over the main channel's. What
/// they are is a *selector*, so what can be asserted about them is categorical
/// rather than numeric: with two signals a decade apart in level at a given
/// bin, the branch is decided by tens of dB and nothing an ulp does can flip it.
///
///   Both fixture files held these three -- and only these three -- at
/// `Tolerances::sameBuildOnly()`, on the reading that a per-bin decision
/// between two computed spectra amplifies a rounding difference into a swapped
/// value. It does not. The reason no numeric bound described them was that all
/// three were miscompiled: each picked its comparison operands as a
/// `ReadOnlyDataRange const &` before the loop, that reference came from
/// `SubRange`'s const accessor, and the accessor type-punned a `Span<float>`
/// into a `Span<float const>`. Under that aliasing promise the optimiser
/// hoisted the source pointer out of the loop, so every bin the effect replaced
/// got bin zero's value. Hence a peak of 81 where the correct answer was 0.5,
/// and hence 99 % disagreement between two architectures compiling the same
/// undefined behaviour differently. \see issue #21.
///
/// \note The first case below is the one that would have caught it, without a
/// fixture and without a tolerance: feed the side chain the main signal and
/// every one of these effects must be transparent at every setting, because
/// replacing a bin with an identical bin is not a change.
///
/// \note Measured against the unfixed engine, all four cases fail in Release
/// -- the invariant on nine of its 168 settings -- and all four *pass* in
/// Debug, because the miscompile needs the optimiser. So the net this file
/// holds under undefined behaviour is an optimised one, which is what every
/// Release leg of CI builds; a checked build alone would not have seen it.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "goldens/engineHarness.hpp"

#include "le/spectrumworx/effects/ethereal/ethereal.hpp"
#include "le/spectrumworx/effects/merger/merger.hpp"
#include "le/spectrumworx/effects/vaxateer/vaxateer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
using SWTest::effectByStreamingName;
using SWTest::Slot;
using Module = LE::SW::Engine::ModuleParameters;

namespace Effects = LE::SW::Effects;

constexpr double pi{std::numbers::pi};

constexpr std::uint32_t sampleRate{44100};
constexpr std::uint8_t channels{2};

/// \note 1024/4, as `amplifyingEffectsTests.cpp` uses: 43 Hz bins, so the two
/// partials below sit twenty bins apart and neither leaks into the other's.
constexpr SWTest::RenderSetup standardSetup{1024, 4, channels, sampleRate, 256};

constexpr std::uint32_t renderedFrames{sampleRate / 2};

/// The two partials. A decade apart in bin index and nowhere near each other,
/// so "did the side channel's partial arrive" has one answer.
constexpr double mainPartial{440};
constexpr double sidePartial{1320};

//------------------------------------------------------------------------------
// Signals
//------------------------------------------------------------------------------

std::vector<float> tone(double const frequency, float const amplitude = 0.5f)
{
    std::vector<float> signal(renderedFrames);
    for (std::uint32_t frame(0); frame < renderedFrames; ++frame)
        signal[frame] =
            amplitude * static_cast<float>(std::sin(2 * pi * frequency * frame / sampleRate));
    return signal;
}

//------------------------------------------------------------------------------
// Measurement
//------------------------------------------------------------------------------

/// One channel of an interleaved render, from \p first to the end.
std::vector<float> mono(std::span<float const> interleaved, std::uint32_t const first = 0)
{
    std::vector<float> single;
    auto const frames(interleaved.size() / channels);
    for (auto frame(static_cast<std::size_t>(first)); frame < frames; ++frame)
        single.push_back(interleaved[frame * channels]);
    return single;
}

/// \brief The magnitude of one frequency, Hann-windowed.
///
/// A single-frequency DFT, as `amplifyingEffectsTests.cpp` uses: everything
/// asked here is "how much is there at this frequency", which is cheaper and
/// clearer this way than through a spectrum whose bin spacing has to be argued
/// with.
double magnitudeAt(std::span<float const> signal, double const frequency)
{
    auto const count(signal.size());
    if (count < 2)
        return 0;
    double real{0}, imaginary{0};
    for (std::size_t n(0); n < count; ++n)
    {
        auto const w(0.5 * (1 - std::cos(2 * pi * static_cast<double>(n) / (count - 1))));
        auto const angle(-2 * pi * frequency * static_cast<double>(n) / sampleRate);
        real += signal[n] * w * std::cos(angle);
        imaginary += signal[n] * w * std::sin(angle);
    }
    return std::sqrt(real * real + imaginary * imaginary) / count;
}

float peak(std::span<float const> signal)
{
    float largest{0};
    for (auto const sample : signal)
        largest = std::max(largest, std::abs(sample));
    return largest;
}

/// How far apart two renders are, relative to the louder.
float relativeDifference(std::span<float const> a, std::span<float const> b)
{
    REQUIRE(a.size() == b.size());
    float largest{0};
    for (std::size_t index(0); index < a.size(); ++index)
        largest = std::max(largest, std::abs(a[index] - b[index]));
    auto const reference(std::max(peak(a), peak(b)));
    return (reference > 0) ? (largest / reference) : largest;
}

//------------------------------------------------------------------------------
// The chains under test
//------------------------------------------------------------------------------

/// Parameter indices, in each effect's own LE_DEFINE_PARAMETERS order.
enum EtherealParameter : std::uint8_t
{
    etherealCondition = 0,
    etherealThreshold = 1,
    etherealMode = 2
};

enum MergerParameter : std::uint8_t
{
    mergerOperation = 0,
    mergerThreshold = 1
};

enum VaxateerParameter : std::uint8_t
{
    vaxateerRMSTarget = 0,
    vaxateerRMSGain = 1,
    vaxateerMode = 2
};

/// The engine's own analysis/resynthesis, which "transparent" is measured
/// against: an effect cannot be compared to its input, there being a window and
/// an FFT's worth of latency in between.
std::vector<float> dryRender(std::span<float const> input)
{
    Slot const empty[]{{-1, {}}};
    return SWTest::renderChain(standardSetup, empty, input);
}

std::vector<float> renderOne(std::string_view const name, std::span<float const> main,
                             std::span<float const> side,
                             std::function<void(Module &)> configure = {})
{
    Slot const slots[]{{effectByStreamingName(name), std::move(configure)}};
    return SWTest::renderChain(standardSetup, slots, main, side);
}

/// One setting to drive an effect with, and a name for the failure message.
struct Setting
{
    std::string description;
    std::function<void(Module &)> configure;
};

/// \brief A spread across every parameter each effect decides on.
///
/// Not a fuzz: every enumerator of every selector, and both ends of each
/// threshold, because the bug this file is against lived in the *replacement*
/// and every setting that replaces anything at all exposes it.
std::vector<Setting> settingsFor(std::string_view const effect)
{
    std::vector<Setting> settings;
    if (effect == "Ethereal")
    {
        for (auto const condition :
             {Effects::Ethereal::Condition::DiffHigher, Effects::Ethereal::Condition::DiffLower})
            for (auto const mode : {Effects::CommonParameters::Mode::Both,
                                    Effects::CommonParameters::Mode::Magnitudes,
                                    Effects::CommonParameters::Mode::Phases})
                for (auto const threshold : {-30, 0, 30})
                    settings.push_back(
                        {"condition " + std::to_string(condition) + " mode " +
                             std::to_string(mode) + " threshold " + std::to_string(threshold),
                         [condition, mode, threshold](Module &module) {
                             module.setEffectParameter(etherealCondition,
                                                       static_cast<float>(condition));
                             module.setEffectParameter(etherealMode, static_cast<float>(mode));
                             module.setEffectParameter(etherealThreshold,
                                                       static_cast<float>(threshold));
                         }});
    }
    else if (effect == "Merger")
    {
        for (int operation(0); operation < 6; ++operation)
            for (auto const threshold : {-120, -20, 0})
                settings.push_back(
                    {"operation " + std::to_string(operation) + " threshold " +
                         std::to_string(threshold),
                     [operation, threshold](Module &module) {
                         module.setEffectParameter(mergerOperation, static_cast<float>(operation));
                         module.setEffectParameter(mergerThreshold, static_cast<float>(threshold));
                     }});
    }
    else
    {
        for (int mode(0); mode < 8; ++mode)
            for (auto const target :
                 {Effects::Vaxateer::RMSTarget::MainRMS, Effects::Vaxateer::RMSTarget::SideRMS})
                for (auto const gain : {-24, 0, 24})
                    settings.push_back(
                        {"mode M" + std::to_string(mode + 1) + " target " + std::to_string(target) +
                             " gain " + std::to_string(gain),
                         [mode, target, gain](Module &module) {
                             module.setEffectParameter(vaxateerMode, static_cast<float>(mode));
                             module.setEffectParameter(vaxateerRMSTarget,
                                                       static_cast<float>(target));
                             module.setEffectParameter(vaxateerRMSGain, static_cast<float>(gain));
                         }});
    }
    return settings;
}

constexpr std::string_view comparingEffects[]{"Ethereal", "Vaxateer", "Merger"};
} // anonymous namespace

//------------------------------------------------------------------------------
// The invariant
//------------------------------------------------------------------------------

TEST_CASE("Replacing a bin with an identical bin is not a change", "[effects][comparing]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The whole of the property, and it needs no fixture and no
    /// tolerance argument: with the side chain fed the main signal the two
    /// spectra are bit-identical, so whichever bins one of these effects decides
    /// to replace, it writes the value that was already there. Every setting is
    /// therefore transparent, on every platform and in either build type.
    ///
    ///   That is what a hoisted source pointer breaks and what nothing in the
    /// suite asked. The bound is loose on purpose: a percent of the peak is far
    /// under any real change and far over the engine's own round trip, which
    /// measures 3e-7 here.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const input(tone(mainPartial));
    auto const dry(mono(dryRender(input)));

    for (auto const effect : comparingEffects)
        for (auto const &setting : settingsFor(effect))
        {
            auto const wet(mono(renderOne(effect, input, input, setting.configure)));
            auto const difference(relativeDifference(dry, wet));
            UNSCOPED_INFO(effect << ", " << setting.description << ": relative difference "
                                 << difference);
            CHECK(difference < 0.01f);
        }
}

//------------------------------------------------------------------------------
// What each of the three does
//------------------------------------------------------------------------------

TEST_CASE("Ethereal swaps in the side channel on the condition it was given",
          "[effects][comparing]")
{
    auto const main(tone(mainPartial));
    auto const side(tone(sidePartial));

    // What each partial measures on its own, so the assertions below are about
    // arrival and removal rather than about a level.
    auto const mainAlone(magnitudeAt(mono(dryRender(main)), mainPartial));
    auto const sideAlone(magnitudeAt(mono(dryRender(side)), sidePartial));

    SECTION("DiffHigher keeps the main's partial and brings the side's in")
    {
        // The side is weaker at the main's bin, so that bin is left alone; it is
        // stronger at its own, so that bin is taken. The output carries both.
        auto const rendered(mono(renderOne("Ethereal", main, side)));
        CHECK(magnitudeAt(rendered, mainPartial) > (mainAlone / 2));
        CHECK(magnitudeAt(rendered, sidePartial) > (sideAlone / 2));
    }

    SECTION("DiffLower removes the main's partial instead")
    {
        // The mirror condition: the bins replaced are the ones where the side is
        // weaker, which is where the main's partial is.
        auto const rendered(mono(renderOne("Ethereal", main, side, [](Module &module) {
            module.setEffectParameter(etherealCondition, Effects::Ethereal::Condition::DiffLower);
        })));
        CHECK(magnitudeAt(rendered, mainPartial) < (mainAlone / 100));
        CHECK(magnitudeAt(rendered, sidePartial) < (sideAlone / 100));
    }

    SECTION("The side's partial arrives only when its phase comes with it")
    {
        // What Mode selects, and the answer is not symmetric: a magnitude on its
        // own does not resynthesise, because with the main channel's phase kept
        // the overlapping frames disagree about where the partial is and the
        // overlap-add cancels most of it. Measured 89 times down against Both,
        // and a phase with no magnitude behind it is down by 20,000.
        auto const magnitudes(mono(renderOne("Ethereal", main, side, [](Module &module) {
            module.setEffectParameter(etherealMode, Effects::CommonParameters::Mode::Magnitudes);
        })));
        auto const phases(mono(renderOne("Ethereal", main, side, [](Module &module) {
            module.setEffectParameter(etherealMode, Effects::CommonParameters::Mode::Phases);
        })));
        CHECK(magnitudeAt(mono(renderOne("Ethereal", main, side)), sidePartial) > (sideAlone / 2));
        CHECK(magnitudeAt(magnitudes, sidePartial) < (sideAlone / 10));
        CHECK(magnitudeAt(phases, sidePartial) < (sideAlone / 100));
    }
}

TEST_CASE("Merger's operation says which bins go to the side channel", "[effects][comparing]")
{
    auto const main(tone(mainPartial));
    auto const side(tone(sidePartial));

    auto const mainAlone(magnitudeAt(mono(dryRender(main)), mainPartial));
    auto const sideAlone(magnitudeAt(mono(dryRender(side)), sidePartial));

    SECTION("SideLargerThanMain takes the side's partial and leaves the main's")
    {
        auto const rendered(mono(renderOne("Merger", main, side, [](Module &module) {
            module.setEffectParameter(mergerOperation,
                                      Effects::Merger::Operation::SideLargerThanMain);
        })));
        CHECK(magnitudeAt(rendered, mainPartial) > (mainAlone / 2));
        CHECK(magnitudeAt(rendered, sidePartial) > (sideAlone / 2));
    }

    SECTION("MainLargerThanSide erases the main's partial")
    {
        auto const rendered(mono(renderOne("Merger", main, side, [](Module &module) {
            module.setEffectParameter(mergerOperation,
                                      Effects::Merger::Operation::MainLargerThanSide);
        })));
        CHECK(magnitudeAt(rendered, mainPartial) < (mainAlone / 100));
    }

    SECTION("The threshold operations run from the whole spectrum to none of it")
    {
        // MainAboveThreshold at the bottom of its range is a comparison every
        // bin clears, so the render is the side channel's; at the top it is one
        // no bin clears, so nothing is replaced at all.
        auto const everything(mono(renderOne("Merger", main, side, [](Module &module) {
            module.setEffectParameter(mergerOperation,
                                      Effects::Merger::Operation::MainAboveThreshold);
            module.setEffectParameter(mergerThreshold, -120);
        })));
        auto const nothing(mono(renderOne("Merger", main, side, [](Module &module) {
            module.setEffectParameter(mergerOperation,
                                      Effects::Merger::Operation::MainAboveThreshold);
            module.setEffectParameter(mergerThreshold, 0);
        })));
        CHECK(magnitudeAt(everything, sidePartial) > (sideAlone / 2));
        CHECK(magnitudeAt(everything, mainPartial) < (mainAlone / 100));
        CHECK(relativeDifference(mono(dryRender(main)), nothing) < 0.01f);
    }
}

TEST_CASE("Vaxateer gates on its RMS threshold", "[effects][comparing]")
{
    auto const main(tone(mainPartial));
    auto const side(tone(sidePartial));

    auto const mainAlone(magnitudeAt(mono(dryRender(main)), mainPartial));

    SECTION("M1 replaces the bins that clear the threshold")
    {
        // Main above the threshold and above the side, which is the main's own
        // partial and nothing else: it goes, and the side's silence there with
        // it.
        auto const rendered(mono(renderOne("Vaxateer", main, side, [](Module &module) {
            module.setEffectParameter(vaxateerMode, Effects::Vaxateer::Mode::M1);
        })));
        CHECK(magnitudeAt(rendered, mainPartial) < (mainAlone / 100));
    }

    SECTION("The RMS gain moves the gate")
    {
        // The same mode at both ends of the gain, 48 dB apart: the higher the
        // threshold, the fewer bins clear it and the more of the main channel
        // survives. A single partial over a mostly empty spectrum sits far above
        // that spectrum's RMS, so even the top of the range does not lift the
        // threshold clear of it -- what the gain buys is a ratio, not silence.
        auto const gated([&](int const gain) {
            return magnitudeAt(mono(renderOne("Vaxateer", main, side,
                                              [gain](Module &module) {
                                                  module.setEffectParameter(
                                                      vaxateerMode, Effects::Vaxateer::Mode::M1);
                                                  module.setEffectParameter(
                                                      vaxateerRMSGain, static_cast<float>(gain));
                                              })),
                               mainPartial);
        });
        CHECK(gated(24) > (10 * gated(-24)));
    }
}
