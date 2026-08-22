////////////////////////////////////////////////////////////////////////////////
///
/// \file presetReportTests.cpp
/// ---------------------------
///
///   What the shipped presets have to say for themselves.
///
///   **Loading a factory preset must tell the user nothing.** That is the whole
/// of this file, and it is a stronger claim than "no dialog appears": what makes
/// it safe to suppress the one thing the banks do raise is that the total is
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
        REQUIRE(static_cast<bool>(preset));

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

    REQUIRE(summary.presets > 0); // an empty sweep is a failure, not a pass

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

////////////////////////////////////////////////////////////////////////////////
///
/// \note "What the factory presets do not mention is exactly this much" stood
/// here until 14.08.2026, pinning `missingParameters` across the banks at 98.
///
///   Its argument was that suppressing `MissingParameter` is only safe while the
/// total is watched, because a parameter can go missing for a *bad* reason -- a
/// rename, a dropped streaming name, a reader that stopped recognising an
/// element -- and that looks identical from inside the loader.
///
///   The argument is right and the number was the wrong instrument for it. It
/// counts something about the shipped content, so every preset added or removed
/// moves it, and it moves in the same direction and by the same amounts as a
/// real fault. Loosening it to `<=` does not help: the commonest edit is adding
/// an old preset, which raises it.
///
///   What actually catches the rename is the other side of the ledger, and it is
/// already asserted above and stated directly below: a value the preset carries
/// that nothing can place is an `unknownParameter`, and the case above holds
/// that at zero across every shipped file. A rename produces one of those for
/// every preset that named the parameter -- which is the loud failure the count
/// was standing in for.
///
////////////////////////////////////////////////////////////////////////////////

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
    REQUIRE(static_cast<bool>(fixture));

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
