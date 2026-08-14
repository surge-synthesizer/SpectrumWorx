////////////////////////////////////////////////////////////////////////////////
///
/// \file presetReportTests.cpp
/// ---------------------------
///
///   What the shipped presets have to say for themselves.
///
///   **Loading a factory preset must tell the user nothing.** That is the whole
/// of this file, and it is a stronger claim than "no dialog appears": what makes
/// it safe to suppress the one thing all 303 banks do raise is that the total is
/// pinned here, so a parameter that goes missing for a *bad* reason -- a rename,
/// a dropped streaming name, a reader that stopped recognising an element --
/// moves the number and reddens.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "presets/presetHarness.hpp"

#include "core/modules/finalImplementations.hpp"
#include "core/modules/moduleDSPAndGUI.hpp"

#include "le/spectrumworx/presetStorage.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
////////////////////////////////////////////////////////////////////////////////
///
/// \brief What every shipped preset reported, in one pass.
///
/// \note The *default* collector, deliberately, because that is the one
/// `GUI::loadPreset` reads when it decides whether to raise a dialog. Installing
/// a reporter of this file's own would measure something else.
///
////////////////////////////////////////////////////////////////////////////////
struct FactoryReport
{
    unsigned int presets{0};
    unsigned int presetsWorthTellingTheUser{0};
    unsigned int missingParameters{0};
    unsigned int unknownParameters{0};
    unsigned int unknownEffects{0};
    unsigned int unavailableEffects{0};
    unsigned int failures{0};
    std::vector<std::string> offenders; ///< presets with anything user-facing
};

FactoryReport loadEveryFactoryPreset()
{
    FactoryReport summary;

    std::filesystem::path const banks(SW_PRESET_DATA_DIR);
    REQUIRE(std::filesystem::is_directory(banks));

    LE::SW::takePresetLoadReport(); // whatever an earlier case left

    for (auto const &file : std::filesystem::recursive_directory_iterator(banks))
    {
        if (file.path().extension() != ".swp")
            continue;

        /// \note One engine per preset, as presetCorpusTests.cpp does: loading B
        /// on top of A is a merge, and it is not what this case is about.
        SWTest::Engine engine;
        engine.setNumberOfChannels(2, 2);
        engine.setSampleRate(48000);
        engine.setBlockSize(512);
        REQUIRE(engine.initialise());

        auto const preset(LE::SW::readPresetFile(file.path()));
        REQUIRE(preset);

        REQUIRE(LE::SW::loadPreset(preset.get(), true /*ignore external samples*/, nullptr,
                                   SWTest::PresetConsumer{engine}));
        ++summary.presets;

        auto const report(LE::SW::takePresetLoadReport());
        summary.missingParameters += report.missingParameters;
        summary.unknownParameters += report.unknownParameters;
        summary.unknownEffects += report.unknownEffects;
        summary.unavailableEffects += report.unavailableEffects;
        summary.failures += report.failures;

        if (report.worthTellingTheUser())
        {
            ++summary.presetsWorthTellingTheUser;
            summary.offenders.push_back(file.path().string() + " (" + report.firstDetail + ")");
        }
    }

    return summary;
}
} // anonymous namespace

TEST_CASE("Loading a factory preset tells the user nothing", "[presets][report]")
{
    auto const summary(loadEveryFactoryPreset());

    REQUIRE(summary.presets >= 288);

    for (auto const &offender : summary.offenders)
        UNSCOPED_INFO("raised a dialog: " << offender);

    // The shipped content is ours. A preset naming an effect this build does not
    // have, or failing to parse, is a fault in what we ship rather than news for
    // the user -- so none of it may reach them, and none of it is here to.
    CHECK(summary.presetsWorthTellingTheUser == 0);
    CHECK(summary.unknownParameters == 0);
    CHECK(summary.unknownEffects == 0);
    CHECK(summary.unavailableEffects == 0);
    CHECK(summary.failures == 0);
}

TEST_CASE("What the factory presets do not mention is exactly this much",
          "[presets][report][fixture]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The number that makes suppressing `MissingParameter` safe.
    ///
    ///   All 98 of these are the same thing: an effect that grew a parameter
    /// after the preset was written. Freqverb gained `HF absorb` (104 presets --
    /// every one that uses it); two presets predate a `Phase`.
    ///
    ///   The value defaults, which is what the format is for. What would *not*
    /// be fine is this number going **up**: that is a parameter the reader
    /// stopped recognising -- a rename, a changed streaming name, a broken
    /// element -- and it looks identical from inside the loader while silently
    /// throwing away shipped preset data. Same discipline as the corpus digests:
    /// if it moves, something is wrong, and refreshing it is not the fix.
    ///                                       (02.08.2026.) (SW port)
    ///
    ///   Was 722. The other 616 were TuneWorx: twelve per-semitone bypasses, the
    /// vibrato section, the pitch range, the retune time and the tuning
    /// direction, over 28 presets at 22 parameters each -- parameters the 2016
    /// plugin never had either, restored to it by a build that stopped defining
    /// LE_SIMPLE_TUNEWORX. Removing them took the count down, which is the safe
    /// direction: every one of the 28 presets now mentions every parameter its
    /// effect has. \see tuneWorx.hpp.
    ///                                       (11.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    constexpr unsigned int knownMissingParameters{98};

    auto const summary(loadEveryFactoryPreset());

    INFO("across " << summary.presets << " presets");
    CHECK(summary.missingParameters == knownMissingParameters);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The other side of the ledger, and what makes the silence above safe.
///
///   Reading a preset is one lookup per parameter the *effect* has, so an element
/// the effect does not have a parameter for was never visited: a renamed
/// parameter, a changed streaming name, an element the reader stopped
/// recognising, all silently discarded. The preset would then not sound the way
/// it was saved and nothing anywhere would say so -- while the count that *would*
/// have moved, `missingParameters`, is exactly the one no longer shown.
///
///   So the loader compares what it took out of a module against what was in it.
/// This case renames one element in the committed 3.0 fixture, which is the same
/// thing a rename in the source would do to every preset ever saved.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A value the preset carries and nothing can place is an error", "[presets][report]")
{
    auto const fixture(
        LE::SW::readPresetFile(std::filesystem::path(SW_PRESET_SNAPSHOT_DIR) / "format3.swp"));
    REQUIRE(fixture);

    std::string preset(fixture.get());
    REQUIRE(preset.find("n=\"Semitones\"") != std::string::npos);
    preset.replace(preset.find("n=\"Semitones\""), std::strlen("n=\"Semitones\""),
                   "n=\"Semitonez\"");

    SWTest::Engine engine;
    engine.setNumberOfChannels(2, 2);
    engine.setSampleRate(48000);
    engine.setBlockSize(512);
    REQUIRE(engine.initialise());

    LE::SW::takePresetLoadReport();
    std::vector<char> buffer(preset.begin(), preset.end());
    buffer.push_back('\0'); // the parse is destructive and wants a terminator
    REQUIRE(LE::SW::loadPreset(buffer.data(), true, nullptr, SWTest::PresetConsumer{engine}));

    auto const report(LE::SW::takePresetLoadReport());

    // Both halves: the effect asked for a parameter that is not there, and the
    // file carried one nothing asked for. Only the second is the user's business.
    CHECK(report.missingParameters == 1);
    CHECK(report.unknownParameters == 1);
    CHECK(report.worthTellingTheUser());

    // ...and the unchanged fixture says nothing at all.
    LE::SW::takePresetLoadReport();
    SWTest::Engine untouched;
    untouched.setNumberOfChannels(2, 2);
    untouched.setSampleRate(48000);
    untouched.setBlockSize(512);
    REQUIRE(untouched.initialise());

    std::vector<char> original(fixture.get(), fixture.get() + preset.size());
    original.push_back('\0');
    REQUIRE(LE::SW::loadPreset(original.data(), true, nullptr, SWTest::PresetConsumer{untouched}));
    CHECK_FALSE(LE::SW::takePresetLoadReport().worthTellingTheUser());
}
