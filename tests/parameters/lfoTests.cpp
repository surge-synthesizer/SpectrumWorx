////////////////////////////////////////////////////////////////////////////////
///
/// lfoTests.cpp
/// ------------
///
///   `lfoImpl.cpp` had no direct test of any kind. What reached it did so
/// through the plugin -- `[clap][lfo]`, five cases -- and every one of them
/// drives **LFO 0 of module 0 with a default sine on Gain**. So of the eleven
/// waveforms one was exercised, of the four sync types none, and of the two
/// bounds neither.
///
///   The five things this covers:
///
///     - **the eleven waveforms**, as a value table over one period. The three
///       random ones are in it too, seeded, because "it is random" is not a
///       reason to leave the shape of the *hold* and the *slide* unpinned.
///     - **the sync types**, which are what `snapPeriodScale()` is for: a period
///       lands on a beat division the host's meter actually has.
///     - **`PeriodScale` snapping**, including the free case, which does not
///       snap and clamps instead.
///     - **`LowerBound > UpperBound`**, which is not an error -- the setter
///       drags the other bound with it and says that it did.
///     - **the meters that are not 4/4** -- 3/4, 6/8 and 5/4, which nothing in
///       the suite drove until 10.08.2026 and which issue #14 is about: the
///       meter decides the grid a synced period snaps to, and the claim that a
///       host merely *stating* its meter must not resnap anything was reasoned
///       rather than measured.
///
/// \note The waveform table is a golden in the shape `parameterTableTests.cpp`
/// established, and for the same reason: eleven small functions whose output is
/// a curve, where "it still does what it did" is the whole assertion and a
/// property would only restate the formula. Regenerate with SW_LFO_TABLE_UPDATE=1
/// and read the diff -- a row that moves is an LFO shape that changed.
///
/// \note `Timer`'s tempo and meter are `static` -- process wide, which
/// issue #11 records -- so every case here states the timing it wants
/// rather than inheriting whatever ran before it.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "le/parameters/lfoImpl.hpp"

#include "le/math/math.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using LE::Parameters::LFO;
using LE::Parameters::LFOImpl;

std::string tablePath() { return std::string(SW_PARAMETER_DATA_DIR) + "/lfoWaveforms.txt"; }

constexpr std::string_view waveformNames[]{"Sine",   "Triangle", "Sawtooth",   "ReverseSawtooth",
                                           "Square", "Exponent", "RandomHold", "RandomSlide",
                                           "Whacko", "Dirac",    "dIRAC"};
static_assert(std::size(waveformNames) == LFO::NumberOfWaveforms);

//------------------------------------------------------------------------------
// Driving one LFO
//------------------------------------------------------------------------------

/// One bar of four beats at 120 BPM is two seconds, which is what a host that
/// reports nothing is assumed to be doing.
constexpr float twoSeconds{2.0f};

////////////////////////////////////////////////////////////////////////////////
///
/// \class ScopedHostTiming
///
/// \brief States the host's tempo and meter, and puts them back.
///
/// \note **Every case in this file needs one**, including the ones that do not
/// care what the tempo is, and that is not tidiness. `Timer`'s tempo and meter
/// are `static` -- process wide, which issue #11 records -- and
/// `adjustValueForPreset()` converts a Free LFO's period to milliseconds
/// through the bar duration. So a case that leaves 140 BPM behind changes what
/// every later preset load in the binary converts to, and 303 digests go red in
/// `presetCorpusTests.cpp`, which never mentions an LFO. That is exactly what
/// the first version of this file did.
///
/// \note What it no longer has to contain is the sync-type default. That used
/// to read a third static -- a sticky "has a host ever reported a tempo" flag --
/// so merely *establishing* a tempo here changed what a brand new LFO anywhere
/// in the process defaulted to. The flag is gone and the default is the constant
/// `Quarter`; see "A brand new LFO's sync type does not depend on the transport"
/// below, which is what stops it coming back.
///
/// \note The way back is the *no-transport* overload of
/// `updatePositionAndTimingInformation`, which restores the assumed 120 BPM 4/4
/// that `reset()` alone would leave a `barDurationChanged()` short of.
///
/// \note `reset()` on the way in, because whether the timing is *established* is
/// per timer and the first update after a reset establishes rather than changes
/// -- the whole of the 03.08.2026 fix. Skipping it reports a bar-duration change
/// nobody asked for.
///
////////////////////////////////////////////////////////////////////////////////

class ScopedHostTiming
{
  public:
    explicit ScopedHostTiming(float const barDuration = twoSeconds,
                              std::uint8_t const measureNumerator = 4)
    {
        LFOImpl::Timer timer;
        timer.reset();
        timer.updatePositionAndTimingInformation(0, barDuration, measureNumerator);
    }

    ~ScopedHostTiming()
    {
        LFOImpl::Timer timer;
        timer.reset();
        timer.updatePositionAndTimingInformation(0u /*samples*/, 48000.0f);
    }

    ScopedHostTiming(ScopedHostTiming const &) = delete; // makes non-copyable
    ScopedHostTiming &operator=(ScopedHostTiming const &) = delete;
}; // class ScopedHostTiming

/// \brief Sets \p lfo's waveform and enables it, leaving everything else at its
/// default.
///
/// \note By reference because `LFOImpl` is deliberately non-copyable -- it holds
/// the waveform's own state between periods, which is what RandomHold and
/// RandomSlide are made of, and a copy would be a second oscillator claiming to
/// be the first.
LFOImpl &withWaveform(LFOImpl &lfo, LFO::Waveform const waveform)
{
    lfo.setWaveform(waveform);
    lfo.setEnabled(true);
    return lfo;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief \p lfo sampled at \p steps evenly spaced positions across **two**
/// bars, which at the default one-bar period is two periods.
///
/// \note Two rather than one, and that is the whole reason this helper has a
/// comment. Four of the eleven waveforms -- Dirac, dIRAC, RandomHold and
/// RandomSlide -- do all their work *at a period boundary*, and a sweep over
/// exactly one period never crosses one. The first table this file minted made
/// the point: Dirac, RandomHold and RandomSlide were sixteen zeroes apiece and
/// dIRAC sixteen ones, so three different waveforms had identical rows and the
/// table would have been perfectly happy about it.
///
/// \note Through a *walking* timer rather than one placed at each position, for
/// the same reason: `newPeriodBegun` is decided by comparing the current time
/// against the previous one, so teleporting the timer makes every step look
/// like a new period -- the opposite failure, and just as invisible.
///
/// \note Sampled at the *middle* of each step rather than at its start, which
/// is not a detail either. `getValue()` reads
/// `currentTime > periodEndForPreviousTime`, strictly, and an evenly spaced
/// sweep starting at zero lands a sample exactly *on* the boundary -- where
/// strictly-greater is false. The second table this file minted was still four
/// flat rows for that reason alone. Half a step in, the boundary falls between
/// two samples and is crossed rather than landed on, which is also what a real
/// block boundary does.
///
////////////////////////////////////////////////////////////////////////////////

std::vector<float> overTwoPeriods(LFOImpl const &lfo, unsigned int const steps,
                                  std::uint8_t const measureNumerator = 4)
{
    LFOImpl::Timer timer;
    timer.reset();
    timer.updatePositionAndTimingInformation(0, twoSeconds, measureNumerator);

    std::vector<float> values;
    values.reserve(steps);
    for (unsigned int step(0); step < steps; ++step)
    {
        timer.updatePositionAndTimingInformation(2.0f * (static_cast<float>(step) + 0.5f) /
                                                     static_cast<float>(steps),
                                                 twoSeconds, measureNumerator);
        values.push_back(lfo.getValue(timer));
    }
    return values;
}

//------------------------------------------------------------------------------
// The table
//------------------------------------------------------------------------------

constexpr unsigned int samplesOverTwoPeriods{32};

std::string row(std::vector<float> const &values)
{
    std::string text;
    for (auto const value : values)
    {
        std::array<char, 32> buffer{};
        /// \note Six significant figures, as the preset dump uses and for the
        /// same reason: enough to catch a shape that moved, few enough that the
        /// last bit of a conversion does not turn the file red elsewhere.
        std::snprintf(buffer.data(), buffer.size(), "%.6g", static_cast<double>(value));
        if (!text.empty())
            text += ' ';
        text += buffer.data();
    }
    return text;
}

using Table = std::map<std::string, std::string>;

Table currentTable()
{
    Table table;
    for (std::uint8_t waveform(0); waveform < LFO::NumberOfWaveforms; ++waveform)
    {
        /// \note Seeded per row, so the three random waveforms are reproducible
        /// and so a row that moves is the *waveform* rather than whatever drew
        /// from the RNG before it. `Math::rngSeed()` with no argument takes a
        /// clock and a stack address, which is what the engine does on reset.
        LE::Math::rngSeed(0x10F0u + waveform);

        LFOImpl lfo;
        withWaveform(lfo, static_cast<LFO::Waveform>(waveform));
        table.emplace(std::string(waveformNames[waveform]),
                      row(overTwoPeriods(lfo, samplesOverTwoPeriods)));
    }
    return table;
}

bool updateRequested()
{
    auto const *const requested(std::getenv("SW_LFO_TABLE_UPDATE"));
    return requested && (*requested != '\0') && (*requested != '0');
}

//------------------------------------------------------------------------------
// Meters
//------------------------------------------------------------------------------

/// \brief A bar of \p beatsPerBar beats at the reference 120 BPM, where a beat
/// is half a second whatever the meter says.
///
/// \note Which is also what the plugin computes from a CLAP transport --
/// `tsig_num` beats of `60 / tempo` -- and it is the whole of what the meter is
/// to the engine. The *denominator* reaches nothing, so a bar of six eight is six
/// beats here rather than the three quarter notes a musician counts. See the
/// note on `The LFO clock follows the host into three four, six eight and five
/// four` in `tests/clap/pluginTests.cpp`, which is where that arrives.
constexpr float barOf(std::uint8_t const beatsPerBar)
{
    return 0.5f * static_cast<float>(beatsPerBar);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Every distinct period a quarter-note-synced LFO can hold in the meter
/// currently in force, sweeping requests from one beat up to one bar.
///
/// \note A sweep rather than a handful of requests with their answers written
/// next to them: what is being asked is which periods are *reachable*, and an
/// implementation that answered three chosen requests correctly and everything
/// else with the request itself would pass the second kind of case.
///
////////////////////////////////////////////////////////////////////////////////

std::vector<float> snapGridOfTheCurrentMeter()
{
    auto const beatsPerBar(LFOImpl::Timer::measureNumeratorFloat());
    constexpr unsigned int steps{200};

    std::vector<float> grid;
    for (unsigned int step(0); step <= steps; ++step)
    {
        auto const oneBeat(1 / beatsPerBar);
        auto const wanted(oneBeat +
                          (1 - oneBeat) * static_cast<float>(step) / static_cast<float>(steps));
        auto const snapped(LFOImpl::snapPeriodScale(wanted, LFO::Quarter).first);
        if (std::ranges::none_of(grid, [snapped](float const already) {
                return std::abs(already - snapped) < 1e-4f;
            }))
            grid.push_back(snapped);
    }
    std::ranges::sort(grid);
    return grid;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
// The waveforms
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Every LFO waveform matches the committed table", "[lfo]")
{
    ScopedHostTiming const timing;

    auto const table(currentTable());
    REQUIRE(table.size() == LFO::NumberOfWaveforms);

    if (updateRequested())
    {
        std::ofstream file(tablePath(), std::ios::trunc);
        file << "# SpectrumWorx LFO waveforms -- generated, do not hand edit.\n"
                "# Regenerate with SW_LFO_TABLE_UPDATE=1 ./sw-dsp-tests \"[lfo]\"\n"
                "#\n"
                "# <Waveform> | "
             << samplesOverTwoPeriods
             << " values, evenly spaced across two bars at 120 BPM 4/4 --\n"
                "#     two periods, because four of the eleven do all their work at a\n"
                "#     period boundary and a single period never crosses one. Six\n"
                "#     significant figures. The LFO's own range is [0, 1]; the bounds a\n"
                "#     user sets map it onto the parameter it drives.\n"
                "#\n"
                "# RandomHold, RandomSlide and Whacko draw from Math::rng, seeded per\n"
                "# row. Their rows pin the *shape* -- one value held for a period, a\n"
                "# ramp between two, a new value every query -- which is what makes\n"
                "# them three different waveforms rather than three calls to rand().\n"
                "#\n"
                "# The first period of Dirac, RandomHold and RandomSlide is their\n"
                "# initial state rather than a draw: nothing has crossed a boundary\n"
                "# yet. That is the behaviour, not an artefact of the sweep -- an LFO\n"
                "# enabled mid-bar reads its starting value until the bar ends.\n";
        for (auto const &[name, values] : table)
            file << name << " | " << values << '\n';
        WARN("SW_LFO_TABLE_UPDATE was set: " << table.size()
                                             << " rows rewritten. Read the diff before "
                                                "committing it.");
        return;
    }

    Table expected;
    {
        std::ifstream stream(tablePath());
        std::string line;
        while (std::getline(stream, line))
        {
            if (line.empty() || (line.front() == '#'))
                continue;
            auto const separator(line.find(" | "));
            REQUIRE(separator != std::string::npos);
            expected.emplace(line.substr(0, separator), line.substr(separator + 3));
        }
    }
    REQUIRE_FALSE(expected.empty()); // an absent or empty file is a failure, not a pass

    for (auto const &[name, values] : table)
    {
        INFO("waveform " << name);
        auto const found(expected.find(name));
        REQUIRE(found != expected.end());
        CHECK(found->second == values);
    }
    CHECK(table.size() == expected.size());
}

TEST_CASE("The waveforms are eleven different shapes", "[lfo]")
{
    ScopedHostTiming const timing;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note What the table cannot say. It would be perfectly happy with eleven
    /// identical rows -- which is what a `lfoFunctions[]` entry pointing at the
    /// wrong function, or an enumerator inserted in the middle of `Waveform`,
    /// actually produces. The order of that enum is also ABI: a preset stores
    /// the waveform by index.
    ///
    ////////////////////////////////////////////////////////////////////////////
    std::vector<std::vector<float>> shapes;
    for (std::uint8_t waveform(0); waveform < LFO::NumberOfWaveforms; ++waveform)
    {
        LE::Math::rngSeed(0x10F0u + waveform);
        LFOImpl lfo;
        withWaveform(lfo, static_cast<LFO::Waveform>(waveform));
        shapes.push_back(overTwoPeriods(lfo, 64));
    }

    for (std::size_t first(0); first < shapes.size(); ++first)
        for (std::size_t second(first + 1); second < shapes.size(); ++second)
        {
            INFO(waveformNames[first] << " against " << waveformNames[second]);
            CHECK(shapes[first] != shapes[second]);
        }
}

TEST_CASE("Every LFO waveform stays inside the unit interval", "[lfo]")
{
    ScopedHostTiming const timing;

    /// \note `getValue()` asserts this in a checked build -- `isValueInRange()`
    /// on the raw waveform and `isValueInBounds()` on the mapped result -- so in
    /// that build this case is the thing that *runs* those assertions over
    /// eleven waveforms rather than one. In a release build it is the only
    /// statement of the range at all.
    for (std::uint8_t waveform(0); waveform < LFO::NumberOfWaveforms; ++waveform)
    {
        INFO(waveformNames[waveform]);
        LE::Math::rngSeed(0x5A5Au + waveform);
        LFOImpl lfo;
        withWaveform(lfo, static_cast<LFO::Waveform>(waveform));
        for (auto const value : overTwoPeriods(lfo, 256))
        {
            CHECK(value >= 0.0f);
            CHECK(value <= 1.0f);
        }
    }
}

TEST_CASE("A held random waveform holds and a sliding one slides", "[lfo]")
{
    ScopedHostTiming const timing;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The three random waveforms are three different things and only
    /// their *behaviour between period boundaries* tells them apart: Whacko
    /// draws every query, RandomHold draws once and repeats it, RandomSlide
    /// draws once and ramps towards it. A table of numbers cannot say which is
    /// which -- it can only say they differ -- so this counts how often the
    /// value moves *within* a period instead.
    ///
    ////////////////////////////////////////////////////////////////////////////
    constexpr unsigned int steps{64}; // 32 per period

    /// How many of the second period's 32 steps differ from the one before it.
    auto const movesWithinAPeriod([](LFO::Waveform const waveform) {
        LE::Math::rngSeed(0xBEEFu);
        LFOImpl lfo;
        withWaveform(lfo, waveform);
        auto const values(overTwoPeriods(lfo, steps));
        std::size_t moves{0};
        // The second period only, so the boundary itself is not counted.
        for (std::size_t index(steps / 2 + 1); index < values.size(); ++index)
            moves += (values[index] != values[index - 1]);
        return moves;
    });

    // Held: drawn at the boundary, then the same number for the whole period.
    CHECK(movesWithinAPeriod(LFO::RandomHold) == 0);
    // Slid: a ramp towards the new number, so it moves at every step.
    CHECK(movesWithinAPeriod(LFO::RandomSlide) > 28);
    // Whacko: a new number every query, and so period-independent entirely.
    CHECK(movesWithinAPeriod(LFO::Whacko) > 28);

    /// \note And the pair that separates Hold from Slide, which the count above
    /// cannot: the slide is *monotone* across its period and the hold is flat.
    /// Two waveforms that both "changed 31 times" could still be the same one.
    LE::Math::rngSeed(0xBEEFu);
    LFOImpl sliding;
    withWaveform(sliding, LFO::RandomSlide);
    auto const slide(overTwoPeriods(sliding, steps));
    std::vector<float> const secondPeriod(slide.begin() + steps / 2 + 1, slide.end());
    CHECK((std::ranges::is_sorted(secondPeriod) ||
           std::ranges::is_sorted(secondPeriod, std::greater<float>{})));
}

////////////////////////////////////////////////////////////////////////////////
// The bounds
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The LFO's bounds map its range onto the parameter's", "[lfo]")
{
    ScopedHostTiming const timing;

    // The whole point of the two bounds, and nothing had ever set either: an
    // enabled LFO sweeps [lowerBound, upperBound] rather than [0, 1].
    LFOImpl lfo;
    withWaveform(lfo, LFO::Sawtooth);
    lfo.setLowerBound(0.25f);
    lfo.setUpperBound(0.75f);

    auto const values(overTwoPeriods(lfo, 64));
    auto const [lowest, highest](std::ranges::minmax_element(values));

    CHECK(*lowest >= 0.25f);
    CHECK(*highest <= 0.75f);
    // ...and it really covers the band rather than sitting in it.
    CHECK(*lowest < 0.3f);
    CHECK(*highest > 0.7f);
}

TEST_CASE("A bound crossing the other drags it along and says so", "[lfo]")
{
    ScopedHostTiming const timing;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `LowerBound > UpperBound`, which the todo lists as untested and
    /// which is not an error condition: `LFO::setLowerBound` returns whether it
    /// had to move the *upper* bound to keep the invariant, and the editor uses
    /// that answer to redraw the other slider. A user dragging the lower bound
    /// past the upper is the ordinary way to reach it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    /// \note Through `LFO&`, because the answer is only on that side: `LFOImpl`
    /// declares its own `void setLowerBound()` that writes the parameter and
    /// hides `LFO`'s `bool` one, which is the pair-keeping version. The editor
    /// reaches it as an `LFO`, so that is how it is driven here.
    {
        LFOImpl lfo;
        auto &pair(static_cast<LFO &>(lfo));
        pair.setUpperBound(0.5f);
        CHECK_FALSE(pair.setLowerBound(0.25f)); // below it: nothing to move
        CHECK(pair.setLowerBound(0.75f));       // past it: the upper bound follows
        CHECK(lfo.lowerBound() == Catch::Approx(0.75f));
        CHECK(lfo.upperBound() >= lfo.lowerBound());
    }
    {
        LFOImpl lfo;
        auto &pair(static_cast<LFO &>(lfo));
        pair.setLowerBound(0.5f);
        CHECK_FALSE(pair.setUpperBound(0.75f));
        CHECK(pair.setUpperBound(0.25f));
        CHECK(lfo.upperBound() == Catch::Approx(0.25f));
        CHECK(lfo.lowerBound() <= lfo.upperBound());
    }

    /// \note And the degenerate case a user can reach by dragging both together:
    /// the two bounds equal, which is a *constant* rather than a modulation.
    /// Worth pinning because `getValue()` maps through
    /// `Math::convertLinearRange` with a zero-width target, and a divide would
    /// live exactly there.
    {
        LFOImpl lfo;
        withWaveform(lfo, LFO::Sine);
        lfo.setLowerBound(0.5f);
        lfo.setUpperBound(0.5f);
        for (auto const value : overTwoPeriods(lfo, 32))
            CHECK(value == Catch::Approx(0.5f));
    }
}

////////////////////////////////////////////////////////////////////////////////
// Sync types and period snapping
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A brand new LFO's sync type does not depend on the transport", "[lfo]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note A parameter's *default* is a property of the parameter. This one
    /// was `Timer::hasTempoInformation() ? Quarter : Free` from 2011 until
    /// 06.08.2026 -- a property of the host's transport at the moment somebody
    /// asked, on a process-global flag that `reset()` deliberately never
    /// cleared.
    ///
    ///   `clap-cpp-validator`'s `state-reproducibility-flush` is what caught it,
    /// and the reason it took a validator is that the two answers need two
    /// *constructions at two moments* to be visible. The engine's module is
    /// built inside `process()` when the slot event is handled; the main
    /// thread's is built when the echo is drained; and `updateLFOTiming()` runs
    /// between them. `stateSave` reads the main thread's, so a session stored
    /// `sync="1"` for an LFO the audio thread was running as `Free`.
    ///
    ///   Deliberately **not** written with a `ScopedHostTiming`: the whole point
    /// is that establishing a transport must not change the answer, so this case
    /// states both timings itself and compares across them.
    ///
    ////////////////////////////////////////////////////////////////////////////

    auto const freshlyDefaultedSyncType([] {
        LFOImpl lfo;
        return lfo.syncTypes();
    });

    // No transport: the assumed 120 BPM 4/4.
    {
        LFOImpl::Timer timer;
        timer.reset();
        timer.updatePositionAndTimingInformation(0u /*samples*/, 48000.0f);
    }
    auto const withoutTransport(freshlyDefaultedSyncType());

    // A host reports one, which is what used to flip the answer for ever after.
    {
        LFOImpl::Timer timer;
        timer.reset();
        timer.updatePositionAndTimingInformation(0, twoSeconds, 4);
    }
    auto const withTransport(freshlyDefaultedSyncType());

    CAPTURE(withoutTransport, withTransport);
    CHECK(withoutTransport == withTransport);
    CHECK(withTransport == LFO::Quarter);

    // ...and back, so the rest of the binary sees what it expects.
    {
        LFOImpl::Timer timer;
        timer.reset();
        timer.updatePositionAndTimingInformation(0u /*samples*/, 48000.0f);
    }
    CHECK(freshlyDefaultedSyncType() == LFO::Quarter);
}

TEST_CASE("A free LFO's period is clamped rather than snapped", "[lfo]")
{
    ScopedHostTiming const timing;

    // Free takes any period inside the range, unchanged.
    auto const [midRange, type](LFOImpl::snapPeriodScale(0.375f, LFO::Free));
    CHECK(type == LFO::Free);
    CHECK(midRange == Catch::Approx(0.375f));

    // ...and only the ends of the range move it.
    CHECK(LFOImpl::snapPeriodScale(1000.0f, LFO::Free).first ==
          Catch::Approx(LFOImpl::currentPeriodScaleMaximum()));
    CHECK(LFOImpl::snapPeriodScale(0.0f, LFO::Free).first ==
          Catch::Approx(LFOImpl::currentPeriodScaleMinimum()));
}

TEST_CASE("A synced LFO's period lands on a division of the bar", "[lfo]")
{
    ScopedHostTiming const timing;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note What `SyncTypes` is *for*, and the parameter `clap-cpp-validator`
    /// spends all four of its state failures on (`M*.*.LFO.T`). A period scale
    /// is in bars, so in 4/4 a quarter note is 0.25 and a whole bar is 1 --
    /// asking for anything between has to come back as one of the divisions the
    /// meter actually has.
    ///
    ////////////////////////////////////////////////////////////////////////////

    auto const quarter(
        [](float const wanted) { return LFOImpl::snapPeriodScale(wanted, LFO::Quarter).first; });

    // Asking for a bar, a half and a quarter gets exactly those.
    CHECK(quarter(1.0f) == Catch::Approx(1.0f));
    CHECK(quarter(0.5f) == Catch::Approx(0.5f));
    CHECK(quarter(0.25f) == Catch::Approx(0.25f));

    // Anything in between lands on one of them rather than staying put.
    for (float const wanted : {0.3f, 0.4f, 0.6f, 0.8f})
    {
        CAPTURE(wanted);
        auto const snapped(quarter(wanted));
        auto const inBeats(snapped * 4);
        CHECK(inBeats == Catch::Approx(std::round(inBeats)).margin(1e-4));
    }

    // And the type it snapped to is reported, which is what the editor labels.
    CHECK(LFOImpl::snapPeriodScale(0.3f, LFO::Quarter).second == LFO::Quarter);
}

TEST_CASE("Triplet and dotted sync land somewhere quarter sync cannot", "[lfo]")
{
    ScopedHostTiming const timing;

    /// \note The control for the case above: three sync types that all snapped
    /// to the same grid would be one sync type with three names, and nothing had
    /// ever asked for more than the default.

    constexpr float wanted{0.3f};
    auto const asQuarter(LFOImpl::snapPeriodScale(wanted, LFO::Quarter));
    auto const asTriplet(LFOImpl::snapPeriodScale(wanted, LFO::Triplet));
    auto const asDotted(LFOImpl::snapPeriodScale(wanted, LFO::Dotted));

    CAPTURE(asQuarter.first, asTriplet.first, asDotted.first);
    CHECK(asTriplet.second == LFO::Triplet);
    CHECK(asDotted.second == LFO::Dotted);
    CHECK(asTriplet.first != asQuarter.first);
    CHECK(asDotted.first != asQuarter.first);

    /// \note `All` is the three of them offered together, so whatever it picks
    /// has to be one of the three -- and the nearest of them, which is what
    /// makes it useful rather than merely permissive.
    auto const asAll(LFOImpl::snapPeriodScale(wanted, LFO::All));
    auto const distance([](float const value) { return std::abs(value - wanted); });
    CHECK(std::min({distance(asQuarter.first), distance(asTriplet.first),
                    distance(asDotted.first)}) == Catch::Approx(distance(asAll.first)));
}

TEST_CASE("The snapped period follows the host's meter", "[lfo]")
{
    ScopedHostTiming const timing;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Where the meter first mattered, and the shortest form of it:
    /// `snapSyncedPeriodScale()` divides by `measureNumerator()` throughout, so a
    /// period scale is bars and the beats it can land on are the ones the meter
    /// has. The case below takes the same claim over four meters at once.
    ///
    ///   In 3/4 a bar is three beats, so one beat is a third of a bar and there
    /// is no such thing as the quarter-of-a-bar that 4/4 snaps to.
    ///
    ////////////////////////////////////////////////////////////////////////////
    {
        REQUIRE(LFOImpl::Timer::measureNumerator() == 4);
        CHECK(LFOImpl::snapPeriodScale(0.25f, LFO::Quarter).first == Catch::Approx(0.25f));
    }
    {
        /// Three beats to the bar, at the same tempo -- so the bar is 1.5 s.
        ScopedHostTiming const inThreeFour(1.5f, 3);
        REQUIRE(LFOImpl::Timer::measureNumerator() == 3);

        // One beat is a third of a bar now, and asking for a quarter gets it.
        auto const oneBeat(LFOImpl::snapPeriodScale(0.25f, LFO::Quarter).first);
        CAPTURE(oneBeat);
        CHECK(oneBeat == Catch::Approx(1.0f / 3).margin(1e-4));

        // ...and a whole bar is still a whole bar.
        CHECK(LFOImpl::snapPeriodScale(1.0f, LFO::Quarter).first == Catch::Approx(1.0f));
    }

    /// \note And 4/4 is back, because the 3/4 guard went out of scope with the
    /// block above it. These are process-wide statics; a case that left 3/4
    /// behind would change what every later case in the binary measures. See
    /// ScopedHostTiming.
    CHECK(LFOImpl::Timer::measureNumerator() == 4);
}

TEST_CASE("Three four, six eight and five four each snap to a grid of their own", "[lfo]")
{
    ScopedHostTiming const timing;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Issue #14 asks for a meter other than 4/4 somewhere in the suite;
    /// this is the half of it that is about the *grid*. `snapSyncedPeriodScale()`
    /// asks which whole numbers of beats divide the bar, so the meter decides the
    /// set of periods a synced LFO can hold at all -- and the case above says
    /// that of three four while leaving the impression that a meter is a meter.
    ///
    ///   The three below say it is not. Five four is the sharp one: five is
    /// prime, so a bar holds exactly two synced periods -- one beat and the whole
    /// bar -- and every request between them lands on one or the other. And the
    /// pair worth having next to each other is three four against six eight: a
    /// bar of each is six eighth notes, and only one of them can hold a half-bar
    /// period, which is what makes them two meters rather than one written twice.
    ///
    ////////////////////////////////////////////////////////////////////////////

    auto const gridIn([](std::uint8_t const beatsPerBar) {
        ScopedHostTiming const meter(barOf(beatsPerBar), beatsPerBar);
        REQUIRE(LFOImpl::Timer::measureNumerator() == beatsPerBar);
        return snapGridOfTheCurrentMeter();
    });

    auto const isTheGrid([](std::vector<float> const &grid, std::vector<float> const &expected) {
        INFO("reachable: " << row(grid) << "  --  expected: " << row(expected));
        REQUIRE(grid.size() == expected.size());
        for (std::size_t index(0); index < grid.size(); ++index)
            CHECK(grid[index] == Catch::Approx(expected[index]).margin(1e-4));
    });

    constexpr float sixth{1.0f / 6}, fifth{1.0f / 5}, third{1.0f / 3};

    // Four four, which everything else in the suite drives, for comparison.
    isTheGrid(gridIn(4), {0.25f, 0.5f, 1.0f});
    // Three: a beat or a bar, and nothing between them.
    isTheGrid(gridIn(3), {third, 1.0f});
    // Six: the richest of the four, and the only one with a half bar and a third.
    isTheGrid(gridIn(6), {sixth, third, 0.5f, 1.0f});
    // Five: prime, so a beat or a bar. Not an oversight -- there is no whole
    // number of beats between one and five that divides five.
    isTheGrid(gridIn(5), {fifth, 1.0f});

    /// \note And the same request read in each meter, which is the statement the
    /// four grids above are made of but is worth having on one line: asking for
    /// half a bar gets half a bar in four four and six eight, a third of one in
    /// three four, and a fifth in five four.
    auto const halfABarIn([](std::uint8_t const beatsPerBar) {
        ScopedHostTiming const meter(barOf(beatsPerBar), beatsPerBar);
        return LFOImpl::snapPeriodScale(0.5f, LFO::Quarter).first;
    });
    CHECK(halfABarIn(4) == Catch::Approx(0.5f));
    CHECK(halfABarIn(6) == Catch::Approx(0.5f));
    CHECK(halfABarIn(3) == Catch::Approx(third).margin(1e-4));
    CHECK(halfABarIn(5) == Catch::Approx(fifth).margin(1e-4));

    // Every guard above went out of scope with the statement that made it.
    CHECK(LFOImpl::Timer::measureNumerator() == 4);
}

TEST_CASE("A host that opens in five four is stating its meter rather than changing it", "[lfo]")
{
    ScopedHostTiming const timing;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The other half of issue #14, and the one it was filed about.
    /// `establishedChange()` reports no meter change on the first update after a
    /// reset, on the argument that there was nothing to change *from* -- the same
    /// argument the bar duration makes, and reasoned rather than measured because
    /// nothing in the suite drove a meter other than 4/4.
    ///
    ///   What it protects is what the tempo half protects. A synced period is
    /// resnapped when the numerator changes, so treating "the host has finally
    /// said five four" as a change would move a stored, host-visible parameter on
    /// the first block of every session that is not in four four -- which is the
    /// exact shape of the four `clap-cpp-validator` state failures the tempo half
    /// fixed, one meter over.
    ///
    ////////////////////////////////////////////////////////////////////////////

    LFOImpl::Timer timer;
    timer.reset();

    LFOImpl synced;
    synced.parameters().get<LFOImpl::SyncTypes>().setValue(LFO::Quarter);
    /// A quarter of a bar: on four four's grid, and on neither of the two meters
    /// below. So a resnap that should not happen is visible, and one that should
    /// has somewhere to go.
    synced.setPeriodScale(0.25f);

    /// \note And a free one alongside it, at the same period, told the same three
    /// things: its period is a duration and the host's meter is none of its
    /// business, in either direction. The case below says that of a meter
    /// *change*; nothing said it of the update that establishes one.
    LFOImpl free;
    free.parameters().get<LFOImpl::SyncTypes>().setValue(LFO::Free);
    free.setPeriodScale(0.25f);

    // The first thing this host ever says, and it is in five four.
    auto const established(timer.updatePositionAndTimingInformation(0, barOf(5), 5));
    CHECK_FALSE(established.measureNumeratorChanged());
    CHECK_FALSE(established.timingInfoChanged());

    synced.updateForNewTimingInformation(established);
    free.updateForNewTimingInformation(established);
    CHECK(synced.periodScale() == Catch::Approx(0.25f));
    CHECK(free.periodScale() == Catch::Approx(0.25f));

    // The same meter again is not a change either.
    auto const again(timer.updatePositionAndTimingInformation(1, barOf(5), 5));
    CHECK_FALSE(again.timingInfoChanged());
    synced.updateForNewTimingInformation(again);
    free.updateForNewTimingInformation(again);
    CHECK(synced.periodScale() == Catch::Approx(0.25f));
    CHECK(free.periodScale() == Catch::Approx(0.25f));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And the other direction, on the numerator *alone*: six eight over a
    /// bar of the same 2.5 seconds is 144 BPM, so the bar duration does not move
    /// and only the grid does. Written that way deliberately -- a meter change
    /// that also changed the tempo would leave `barDurationChanged()` true, and
    /// `updateForNewTimingInformation()` would then be resnapping for a reason
    /// this case could not tell apart from the one it is about.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const meterOnly(timer.updatePositionAndTimingInformation(2, barOf(5), 6));
    CHECK_FALSE(meterOnly.barDurationChanged());
    CHECK(meterOnly.measureNumeratorChanged());
    CHECK(meterOnly.timingInfoChanged());

    synced.updateForNewTimingInformation(meterOnly);
    free.updateForNewTimingInformation(meterOnly);
    CAPTURE(synced.periodScale(), free.periodScale());
    CHECK(synced.periodScale() == Catch::Approx(1.0f / 3).margin(1e-4));
    CHECK(free.periodScale() == Catch::Approx(0.25f));
}

////////////////////////////////////////////////////////////////////////////////
// The clock
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A meter change resnaps a synced LFO and leaves a free one alone", "[lfo]")
{
    ScopedHostTiming const timing;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `updateForNewTimingInformation()`, which is the other half of what
    /// the sync types are for and is called once per block. A synced LFO has to
    /// follow the host into a new meter -- its period is in beats, and the bar
    /// just changed length -- and a free one must not, its period being a
    /// duration and the host's meter none of its business.
    ///
    /// \note **The free half of this changed on 06.08.2026 and the line below is
    /// the change.** It used to read
    /// `freeBefore * (twoSeconds / 1.5f)` -- the period *rescaled*, so that
    /// `periodScale * barDuration` stayed constant and the LFO went on sounding
    /// at the same rate. Correct in what you heard and wrong in what it did to
    /// the number, which is host-visible and automatable: a tempo change moved a
    /// value the user had automated and the project had saved. A free period is
    /// measured against the reference bar now, so it sounds the same and does not
    /// move. See `LFOImpl::getValue` and the case below.
    ///
    ////////////////////////////////////////////////////////////////////////////
    LFOImpl::Timer timer;
    timer.reset();
    timer.updatePositionAndTimingInformation(0, twoSeconds, 4);

    LFOImpl synced;
    synced.parameters().get<LFOImpl::SyncTypes>().setValue(LFO::Quarter);
    synced.setPeriodScale(0.25f); // one beat of four

    LFOImpl free;
    free.parameters().get<LFOImpl::SyncTypes>().setValue(LFO::Free);
    free.setPeriodScale(0.25f);
    auto const freeBefore(free.periodScale());

    // The host moves to 3/4 at the same tempo.
    auto const change(timer.updatePositionAndTimingInformation(0, 1.5f, 3));
    REQUIRE(change.timingInfoChanged());

    synced.updateForNewTimingInformation(change);
    free.updateForNewTimingInformation(change);

    CAPTURE(synced.periodScale(), free.periodScale(), freeBefore);

    // The synced one moved onto the new meter's grid...
    CHECK(synced.periodScale() != Catch::Approx(0.25f));
    CHECK(synced.periodScale() == Catch::Approx(1.0f / 3).margin(1e-4));

    // ...and the free one did not move at all.
    CHECK(free.periodScale() == Catch::Approx(freeBefore));
}

TEST_CASE("A tempo change moves neither a free LFO's rate nor its parameter", "[lfo]")
{
    ScopedHostTiming const timing;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The pair that has to hold together, and the reason the 2011
    /// arrangement was not simply wrong: **the sounding rate has to survive a
    /// tempo change and so does the number**. Rescaling the parameter bought the
    /// first at the cost of the second. Measuring a free period against a bar
    /// that never changes length buys both.
    ///
    ///   Measured as a phase rather than asserted as an identity, because the
    /// identity is exactly what the implementation assumes and a test that
    /// restates it would pass however the clock were wired. Two LFOs, one free
    /// and one synced, over the same wall-clock interval either side of a tempo
    /// change: the free one has to have advanced by the same fraction of its
    /// period both times and the synced one must not have.
    ///
    ////////////////////////////////////////////////////////////////////////////

    /// \note A sawtooth, whose value *is* its normalised position -- so what is
    /// compared below is a phase and not a level.
    auto const phaseAfter(
        [](float const barDuration, float const seconds, LFO::SyncType const syncType) {
            LFOImpl lfo;
            lfo.parameters().get<LFOImpl::SyncTypes>().setValue(syncType);
            withWaveform(lfo, LFO::Sawtooth);
            lfo.setPeriodScale(0.5f); // one second at the reference tempo

            LFOImpl::Timer timer;
            timer.reset();
            timer.updatePositionAndTimingInformation(0, barDuration, 4);
            timer.updatePositionAndTimingInformation(seconds / barDuration, barDuration, 4);
            return lfo.getValue(timer);
        });

    constexpr float atNinety{4 * 60.0f / 90}; // a bar is 8/3 s rather than 2

    // Three quarters of a second of real time, at two different tempi.
    constexpr float seconds{0.75f};

    auto const freeAt120(phaseAfter(twoSeconds, seconds, LFO::Free));
    auto const freeAt90(phaseAfter(atNinety, seconds, LFO::Free));
    CAPTURE(freeAt120, freeAt90);

    // The free LFO is three quarters through a one second period, both times.
    CHECK(freeAt120 == Catch::Approx(0.75f).margin(1e-4));
    CHECK(freeAt90 == Catch::Approx(freeAt120).margin(1e-4));

    /// \note And the control, without which the case above would pass on an LFO
    /// that had simply stopped reading the clock: a synced LFO's rate *does*
    /// follow the tempo, so the same wall-clock interval is a different fraction
    /// of its period.
    auto const syncedAt120(phaseAfter(twoSeconds, seconds, LFO::Quarter));
    auto const syncedAt90(phaseAfter(atNinety, seconds, LFO::Quarter));
    CAPTURE(syncedAt120, syncedAt90);
    CHECK(syncedAt90 != Catch::Approx(syncedAt120).margin(1e-3));
}

TEST_CASE("The first block after a reset establishes the timing rather than changing it", "[lfo]")
{
    ScopedHostTiming const timing;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The 03.08.2026 fix, stated directly for the first time. A timer
    /// starts holding the assumed 120 BPM 4/4; comparing a host's real tempo
    /// against an assumption nobody chose and calling the difference a *change*
    /// rescaled every free-running LFO period on the first block of any session
    /// that was not at 120.
    ///
    ///   `[clap][lfo]`'s "Stop the LFO periods moving when the host states its
    /// tempo" covers this through the plugin. This is the unit underneath it,
    /// and it is where the distinction actually lives.
    ///
    ////////////////////////////////////////////////////////////////////////////
    LFOImpl::Timer timer;
    timer.reset();

    // 90 BPM 4/4: a bar is 8/3 seconds, which is not the assumption.
    constexpr float atNinety{4 * 60.0f / 90};
    auto const established(timer.updatePositionAndTimingInformation(0, atNinety, 4));
    CHECK_FALSE(established.timingInfoChanged());
    CHECK_FALSE(established.barDurationChanged());

    // The same timing again is still not a change.
    auto const again(timer.updatePositionAndTimingInformation(1, atNinety, 4));
    CHECK_FALSE(again.timingInfoChanged());

    // A real tempo change is.
    auto const changed(timer.updatePositionAndTimingInformation(2, twoSeconds, 4));
    CHECK(changed.barDurationChanged());
    CHECK(changed.timingInfoChanged());
}

TEST_CASE("Several LFOs on one clock stay independent", "[lfo]")
{
    ScopedHostTiming const timing;

    /// \note The todo's "several at once". They share a `Timer` -- one per
    /// engine, read by every module's every LFO -- and each carries its own
    /// waveform state, so a shared `state_` or a shared position would show up
    /// as two LFOs agreeing when they should not.
    LFOImpl::Timer timer;
    timer.reset();
    timer.updatePositionAndTimingInformation(0, twoSeconds, 4);

    LFOImpl sine, sawtooth, held, early;
    withWaveform(sine, LFO::Sine);
    withWaveform(sawtooth, LFO::Sawtooth);
    withWaveform(held, LFO::RandomHold);

    // The same waveform at opposite phases, which is the sharpest form of it.
    withWaveform(early, LFO::Sawtooth);
    early.setPhase(0.25f);

    std::vector<float> sines, sawtooths, helds, earlies;
    for (unsigned int step(0); step < 32; ++step)
    {
        timer.updatePositionAndTimingInformation(static_cast<float>(step) / 32, twoSeconds, 4);
        sines.push_back(sine.getValue(timer));
        sawtooths.push_back(sawtooth.getValue(timer));
        helds.push_back(held.getValue(timer));
        earlies.push_back(early.getValue(timer));
    }

    CHECK(sines != sawtooths);
    CHECK(sawtooths != helds);
    CHECK(sawtooths != earlies); // the phase offset really offset it

    // The held one held, next to two that did not.
    CHECK(std::ranges::count(helds, helds.front()) == static_cast<std::ptrdiff_t>(helds.size()));
    CHECK(std::ranges::count(sawtooths, sawtooths.front()) < 4);
}
