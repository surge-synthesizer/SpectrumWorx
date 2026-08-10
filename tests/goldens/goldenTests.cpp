////////////////////////////////////////////////////////////////////////////////
///
/// goldenTests.cpp
/// ---------------
///
///   The stage 3.6 golden fixtures: every shipped effect, at two FFT sizes and
/// two overlap factors, over four deterministic signals.
///
///   Regenerate with SW_GOLDEN_UPDATE=1 in the environment, and read the diff
/// before committing it. A golden that changes is either a bug introduced or a
/// bug fixed, and the two look identical from here.
///
///   The caveat from the plan bears repeating: these capture 2016 source as
/// compiled by a 2026 toolchain, not the behaviour of the 2016 binaries. Any
/// difference from C++20 semantics, from the UB this port has fixed, or from
/// thirteen years of compiler change is already baked into the baseline.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "engineHarness.hpp"
#include "goldenDigest.hpp"

#include "le/spectrumworx/effects/configuration/constants.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
//------------------------------------------------------------------------------

namespace
{
namespace Effects = LE::SW::Effects;

/// The matrix. Two FFT sizes two octaves apart and two overlap factors, per
/// the plan; 512/4 is what the plugin ships defaulted to.
struct Configuration
{
    std::uint16_t fftSize;
    std::uint8_t overlapFactor;
};
constexpr Configuration configurations[]{{512, 4}, {2048, 8}};

constexpr SWTest::Signal signals[]{SWTest::Signal::Impulse, SWTest::Signal::Sweep,
                                   SWTest::Signal::PinkNoise, SWTest::Signal::Voice};

constexpr std::uint32_t sampleRate{44100};
constexpr std::uint32_t blockSize{256};
constexpr std::uint8_t channels{2};

/// Half a second, which is everything all but two of these effects have to say.
constexpr std::uint32_t renderedFrames{sampleRate / 2};

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The effects whose own delay is longer than the matrix renders for.
///
///   The matrix rendered 16384 frames -- 371 ms -- until 10.08.2026, and Frecho
/// and Frevcho delay by the round trip of their `Distance` at 343 m/s: **583 ms**
/// at the 100 m default (`frechoImpl.cpp`). All sixteen of their rows were
/// therefore pure silence, and a silent render hashes identically on every
/// machine with zeroes in every numeric column -- so those rows agreed with any
/// build ever made. Sixteen fixtures that could not fail.
///
///   Two seconds rather than "just over 583 ms", for two measured reasons:
///
///     - `Signal::Impulse` puts its transient at `frames / 4`, so the transient
///       moves with the render and its echo lands at `frames / 4 + 583 ms`. The
///       render has to reach 777 ms before that is inside the buffer at all.
///     - Frevcho is a reverser in front of the same delay, and needs more than
///       one round trip: measured, its impulse rows are silent at 1000 ms, are
///       4e-9 at 1050, and only land properly at **1100 ms**.
///
///   1100 ms would be sitting on that boundary. 2 s puts the transient at 500 ms
/// and its echo around 1.1 s, leaving most of a second of tail.
///
/// \note Two effects and not the whole matrix, because the cost is per render:
/// 464 rows at half a second is about a minute, and two seconds for all of them
/// would be four. The sixteen long rows add a handful of seconds.
///
/// \note This is the *length* half of the silent-fixture problem and not the
/// whole of it. Convolver and Freqnamics are silent for reasons of their own --
/// ordinary parameter defaults, not fixture length -- and stay that way;
/// `silentDefaultsTests.cpp` states each and holds the count, now 9 rather
/// than 25.
///                                       (10.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////
constexpr std::uint32_t longRenderedFrames{sampleRate * 2};

bool needsALongRender(std::int8_t const effectIndex)
{
    static constexpr std::string_view slow[]{"Frecho", "Frevcho"};
    if (effectIndex < 0) // the bypassed chain
        return false;
    std::string_view const effect(Effects::effectName(effectIndex));
    return std::find(std::begin(slow), std::end(slow), effect) != std::end(slow);
}

std::string fixturePath() { return std::string(SW_GOLDEN_DATA_DIR) + "/goldens.txt"; }

std::string keyFor(std::int8_t const effectIndex, SWTest::Signal const signal,
                   Configuration const &configuration)
{
    std::string const effect(effectIndex < 0 ? "(bypass)" : Effects::effectName(effectIndex));
    std::string key;
    for (auto const character : effect)
        key += (character == ' ') ? '_' : character;
    return key + "/" + SWTest::name(signal) + "/" + std::to_string(configuration.fftSize) + "/" +
           std::to_string(configuration.overlapFactor);
}

std::vector<SWTest::Fixture> renderAll()
{
    std::vector<SWTest::Fixture> fixtures;
    // -1 is the bypassed chain: it pins the engine's own analysis/synthesis
    // path, so a break there says the WOLA changed rather than an effect did.
    for (int effect(-1); effect < Effects::Constants::numberOfEffects; ++effect)
        for (auto const &configuration : configurations)
            for (auto const signal : signals)
            {
                SWTest::RenderSetup const setup{configuration.fftSize, configuration.overlapFactor,
                                                channels, sampleRate, blockSize};
                auto const frames(needsALongRender(static_cast<std::int8_t>(effect))
                                      ? longRenderedFrames
                                      : renderedFrames);
                auto const output(
                    SWTest::render(setup, static_cast<std::int8_t>(effect), signal, frames));
                fixtures.push_back(
                    {keyFor(static_cast<std::int8_t>(effect), signal, configuration),
                     SWTest::Digest::of(output, channels, static_cast<float>(sampleRate))});
            }
    return fixtures;
}

/// \note The reader and the writer are `goldenDigest`'s since 05.08.2026: the
/// side-chain fixtures are a second file of the same shape, and the provenance
/// marker is a property of the format rather than of this case.
constexpr char const *fixturePreamble[]{
    "SpectrumWorx golden fixtures -- generated, do not hand edit.",
    "Regenerate with SW_GOLDEN_UPDATE=1 ./sw-dsp-tests \"[golden]\"",
    "",
    "One row per effect/signal/fftSize/overlapFactor. Columns:",
    "  key  hash  peak  rms  dcOffset  nonFinite  band0..band7 (dB)",
    "",
    "Every row renders 500 ms at 44100 except Frecho and Frevcho, which",
    "render 2 s because their default delay is a 583 ms round trip and a",
    "shorter render catches only the silence before it -- see",
    "needsALongRender() in goldenTests.cpp.",
    "",
    "hash is FNV-1a over the raw sample bits and is a same-platform",
    "contract only; the numeric columns are what a different",
    "architecture or compiler is held to.",
};

/// \brief The effects whose output is not a continuous function of their input.
///
///   Every one of these makes a *decision* somewhere: pitch detection picks a
/// maximum, the phase vocoder unwraps a phase, Imploder and Exploder threshold a
/// bin, the slew limiter compares a rate of change against a limit. One ulp of
/// difference in the spectrum flips the comparison, the chosen bin moves, and the
/// output moves by percent — 21 % on a peak for Pitch Spring. That is not a
/// tolerance problem and no tighter FFT removes it; two compilers on one machine
/// would do the same.
///
///   So they are held to Tolerances::amplified() rather than strict() on a
/// machine that did not mint the fixture file. On the machine that did they are
/// still bit-exact, like everything else, which is where their real regression
/// cover lives.
///
/// \note **This list is a measurement, not a property.** It is the set that
/// exceeded strict() across macOS/Accelerate against Linux/pffft, and a third
/// platform may well surface a tenth. Adding to it should mean the same work:
/// look at why, and confirm it is a decision boundary rather than a bug. Slew
/// Limiter was nearly missed for that reason — it diverges on DC offset and one
/// band while its peak and RMS stay at 1e-7, so a filter written over peak and
/// RMS alone did not catch it.
///
/// \note The right long-term answer for these nine is property tests — the
/// detected pitch lands within N cents of the input's, latency is exact, a
/// bypassed slot is transparent — rather than a fixture with a wide bound. That
/// is what actually tests them; this declines to. See stage 4.4.
///                                       (29.07.2026.) (SW port)
///
/// \note **Written, 01.08.2026: `tests/effects/amplifyingEffectsTests.cpp`.**
/// Nineteen cases, and they run in both build types where this file renders in
/// Release only. So the wide bound below is no longer the whole of what holds
/// these nine to anything; it is the same-platform regression net, and what the
/// effects *do* is asserted there.
///                                       (01.08.2026.) (SW port)
bool amplifiesRounding(std::string const &key)
{
    // Keys carry the effect name with spaces turned into underscores, as
    // keyFor() writes them.
    static constexpr std::string_view chaotic[]{
        "Pitch_Spring",
        "Pitch_Spring_(pvd)",
        "Pitch_Magnet",
        "Octaver",
        "PVD_start",
        "PVD_stop",
        "Imploder",
        "Exploder",
        "Slew_Limiter",
        /// \note The tenth, and the measurement the note above asked for: added
        /// 06.08.2026 off the first Linux/x86_64 run of this file, at rms 1.198e-4
        /// against a 1e-4 bound -- twenty per cent over, on figures that agree to
        /// four significant digits (0.2557 against 0.2557).
        ///
        ///   The decision is a log: `process()` is add(epsilon) -> ln() ->
        /// cepstral low pass -> exp(), so a difference the size of an ulp in a
        /// near-silent bin becomes a *multiplicative* one on the way back out,
        /// and the file's own note has said since 2012 that it "produces invalid
        /// values due to taking the logarithm of zero or near-zero values". No
        /// bin moves; the amplification is the arithmetic.
        ///                                   (06.08.2026.) (SW port)
        "Talking_Wind",
    };
    auto const effect(std::string_view(key).substr(0, key.find('/')));
    return std::find(std::begin(chaotic), std::end(chaotic), effect) != std::end(chaotic);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The effects that no numeric bound describes at all.
///
///   One so far, and it is a decision boundary of a worse kind than the ten
/// above: theirs sits somewhere the inputs occasionally reach, Ethereal's sits
/// exactly where they all are. Nothing in `goldenTests.cpp` drives a side
/// chain, so `ChannelData::setNewTimeDomainData` leaves the side spectrum as
/// `clearSideChannelData()` left it at setup -- silent -- and the effect's test
///
///     data.side().amps().front() < (data.main().amps().front() * threshold)
///
/// is therefore `0 < main * threshold`. Whether a near-silent bin is exactly
/// zero or a denormal is a question x86 and aarch64 answer differently, so the
/// branch flips across the bottom of the spectrum; each flipped bin then takes
/// the silent side's phase of 0 and lines up with its neighbours, which is
/// where a peak of 80.9 against 0.78 comes from.
///
/// \note Measured, macOS/arm64 -> Linux/x86_64, over all six of its rows:
/// relative peak 0.86 to 0.99, rms 0.42 to 0.87, and 111 dB in band 7. It is
/// not that `amplified()` is too tight -- a bound that admits 0.99 admits
/// silence and a doubled gain, so widening it would delete the test rather than
/// loosen it.
///
/// \note What Ethereal *does* is consequently not covered here on a machine
/// that did not mint the file, and a behavioural case is what would cover it --
/// the same answer `amplifyingEffectsTests.cpp` gave for the nine. Renders that
/// disagree by 99 % between two architectures may also be worth a second look
/// on their own account: this says only that a fixture cannot referee them.
///                                       (06.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////
bool divergesWithoutBound(std::string const &key)
{
    auto const effect(std::string_view(key).substr(0, key.find('/')));
    return effect == "Ethereal";
}

SWTest::Tolerances tolerancesFor(std::string const &key)
{
    if (divergesWithoutBound(key))
        return SWTest::Tolerances::sameBuildOnly();
    return amplifiesRounding(key) ? SWTest::Tolerances::amplified() : SWTest::Tolerances::strict();
}

/// \brief What the drift actually is, rather than which field noticed it first.
///
///   On a platform that did not mint the file, `compare( …, exact )` reports
/// "bit-exact hash mismatch" for nearly every row and stops there, which says
/// only that the two builds are not the same build. This says how far apart they
/// are, which is the question a backend swap actually raises.
///                                       (29.07.2026.) (SW port)
std::string driftReport(std::vector<SWTest::Fixture> const &rendered,
                        std::map<std::string, SWTest::Digest> const &golden,
                        std::string const &goldenProvenance)
{
    struct Row
    {
        std::string key;
        SWTest::Deltas deltas;
    };
    std::vector<Row> rows;
    std::vector<float> peaks, rmss;
    unsigned int identicalHashes{0}, silent{0};
    for (auto const &fixture : rendered)
    {
        auto const entry(golden.find(fixture.key));
        if (entry == golden.end())
            continue;
        rows.push_back({fixture.key, SWTest::deltas(entry->second, fixture.digest)});
        peaks.push_back(rows.back().deltas.peak);
        rmss.push_back(rows.back().deltas.rms);
        if (entry->second.hash == fixture.digest.hash)
        {
            ++identicalHashes;
            // A silent render hashes identically on any machine and pins nothing.
            if ((entry->second.peak == 0) && (fixture.digest.peak == 0))
                ++silent;
        }
    }

    auto const percentile([](std::vector<float> values, double const fraction) {
        if (values.empty())
            return 0.0f;
        std::sort(values.begin(), values.end());
        auto const index(
            std::min(values.size() - 1, static_cast<std::size_t>(fraction * values.size())));
        return values[index];
    });

    std::vector<Row> over;
    for (auto const &row : rows)
        if (!SWTest::withinTolerance(row.deltas, tolerancesFor(row.key)))
            over.push_back(row);
    std::sort(over.begin(), over.end(),
              [](Row const &a, Row const &b) { return a.deltas.worst() > b.deltas.worst(); });

    std::ostringstream report;
    auto const strict(SWTest::Tolerances::strict());
    auto const amplified(SWTest::Tolerances::amplified());
    report << "\ngolden drift: " << (goldenProvenance.empty() ? "(unmarked)" : goldenProvenance)
           << "  ->  " << SWTest::provenance() << '\n'
           << "  contract: peak " << strict.peak << ", rms " << strict.rms << ", dc "
           << strict.dcOffset << ", bands " << strict.band << " dB above "
           << SWTest::bandAudibilityFloor() << " dB\n"
           << "            the ten amplifying effects instead get peak " << amplified.peak
           << ", rms " << amplified.rms << ", dc " << amplified.dcOffset << ", bands "
           << amplified.band << " dB, and are marked [amplified] below\n"
           << "            Ethereal gets no numeric bound at all off its minting\n"
           << "            machine, and is marked [same-build only] -- see\n"
           << "            divergesWithoutBound()\n"
           << "  " << rows.size() << " fixtures compared; " << (rows.size() - over.size())
           << " within tolerance, " << over.size() << " outside\n"
           << "  bit-identical: " << identicalHashes << " (" << silent
           << " of them a silent render, which pins nothing)\n"
           << "  relative peak: median " << percentile(peaks, 0.5) << "  p90 "
           << percentile(peaks, 0.9) << "  max " << percentile(peaks, 1.0) << '\n'
           << "  relative rms : median " << percentile(rmss, 0.5) << "  p90 "
           << percentile(rmss, 0.9) << "  max " << percentile(rmss, 1.0) << '\n';

    if (!over.empty())
    {
        constexpr std::size_t listed{25};
        report << "  worst " << std::min(listed, over.size()) << ", by relative amplitude:\n";
        for (std::size_t index(0); (index < listed) && (index < over.size()); ++index)
        {
            auto const &row(over[index]);
            report << "    " << row.key << "  peak " << row.deltas.peak << "  rms "
                   << row.deltas.rms << "  dc " << row.deltas.dcOffset << "  band"
                   << row.deltas.worstBand << " " << row.deltas.band << " dB"
                   << (divergesWithoutBound(row.key) ? "  [same-build only]"
                       : amplifiesRounding(row.key)  ? "  [amplified]"
                                                     : "")
                   << (row.deltas.nonFiniteDiffers ? "  NON-FINITE COUNT DIFFERS" : "") << '\n';
        }
        if (over.size() > listed)
            report << "    ... and " << (over.size() - listed) << " more\n";
    }
    return report.str();
}

} // anonymous namespace

/// \note Rendering the whole matrix in a checked build aborts on a debug-only
/// verification assert: Math::symmetricMovingAverage carries a running sum
/// across thousands of bins, and over pink noise the accumulated rounding
/// drifts a hair below zero, so Smoother hands amph2DFT() a negative
/// "amplitude". Benign in the output -- a sign flip on a near-silent bin --
/// but it is a real numerical weakness, and fixing it means changing DSP
/// output, which is precisely what should not happen in the commit that mints
/// the baseline. It belongs with the vector primitives in stage 4.
///
/// The goldens are therefore a release-build artifact, which is also the build
/// that ships. The checked build still runs every other test.
///                                       (28.07.2026.) (SW port)
#ifndef NDEBUG
#define LE_SW_GOLDENS_NEED_A_RELEASE_BUILD                                                         \
    SKIP("Goldens render in a release build; a checked build aborts inside Smoother -- see the "   \
         "note in goldenTests.cpp.")
#else
#define LE_SW_GOLDENS_NEED_A_RELEASE_BUILD static_cast<void>(0)
#endif

TEST_CASE("Golden fixtures", "[golden]")
{
    LE_SW_GOLDENS_NEED_A_RELEASE_BUILD;

    auto const fixtures(renderAll());
    REQUIRE(fixtures.size() == (Effects::Constants::numberOfEffects + 1) *
                                   std::size(configurations) * std::size(signals));

    if (SWTest::goldenUpdateRequested())
    {
        SWTest::writeFixtures(fixturePath(), fixtures, fixturePreamble);
        WARN("Golden fixtures regenerated -- read the diff before committing it.");
        return;
    }

    auto const golden(SWTest::readFixtures(fixturePath()));
    REQUIRE_FALSE(golden.fixtures.empty());

    /// The hash is a contract against the build that produced the file; the
    /// numeric summary is what has to hold anywhere else. Which of the two is
    /// being checked is now decided by the file's own provenance marker rather
    /// than assumed.
    ///
    ///   Stage 4 is what made this necessary. The fixtures were minted on
    /// macOS/arm64 over Accelerate; on Linux over pffft, 439 of 464 rows have a
    /// different hash and every one of them is legitimate -- a bit-exact hash
    /// over float output cannot survive a different FFT implementation, a
    /// different libm or a different compiler, and was never meant to. Checking
    /// it anyway turned the whole cross-platform question into 439 identical
    /// "bit-exact hash mismatch" lines that answered nothing.
    ///                                   (29.07.2026.) (SW port)
    auto const sameBuild(golden.mintedByThisBuild());

    std::vector<std::string> failures;
    for (auto const &fixture : fixtures)
    {
        auto const entry(golden.fixtures.find(fixture.key));
        if (entry == golden.fixtures.end())
        {
            failures.push_back(fixture.key + ": no golden");
            continue;
        }
        auto const result(
            SWTest::compare(entry->second, fixture.digest, sameBuild, tolerancesFor(fixture.key)));
        if (!result.matches)
            failures.push_back(fixture.key + ": " + result.explanation);
    }

    if (!sameBuild)
        UNSCOPED_INFO(driftReport(fixtures, golden.fixtures, golden.provenance));
    else
        UNSCOPED_INFO("bit-exact against " << golden.provenance);

    for (auto const &failure : failures)
        UNSCOPED_INFO(failure);
    CHECK(failures.empty());
}

TEST_CASE("Every effect leaves the output finite and bounded", "[golden][sanity]")
{
    LE_SW_GOLDENS_NEED_A_RELEASE_BUILD;

    // Independent of the fixture file: whatever the goldens say, no effect may
    // emit a NaN or run away, and this stays meaningful across a deliberate
    // golden update.
    SWTest::RenderSetup const setup{512, 4, channels, sampleRate, blockSize};
    std::vector<std::string> offenders;
    for (int effect(0); effect < Effects::Constants::numberOfEffects; ++effect)
    {
        auto const output(
            SWTest::render(setup, static_cast<std::int8_t>(effect), SWTest::Signal::Voice, 8192));
        auto const digest(SWTest::Digest::of(output, channels, static_cast<float>(sampleRate)));
        if (digest.nonFiniteSamples)
            offenders.push_back(std::string(Effects::effectName(effect)) + ": " +
                                std::to_string(digest.nonFiniteSamples) + " non-finite samples");
        else if (digest.peak > 100.0f)
            offenders.push_back(std::string(Effects::effectName(effect)) + ": peak " +
                                std::to_string(digest.peak));
    }
    for (auto const &offender : offenders)
        UNSCOPED_INFO(offender);
    CHECK(offenders.empty());
}
