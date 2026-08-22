////////////////////////////////////////////////////////////////////////////////
///
/// \file presetCorpusTests.cpp
/// ---------------------------
///
///   The preset format, held to two things: that a handful of **frozen
/// fixtures** load to exactly the state they have always loaded to, and that
/// **every shipped preset** survives the 2.x -> 3.0 translation unchanged.
///
///   Stage 8's "done when" says an unmodified 2016-era preset file still loads.
/// The files committed under assets/presets were written between 2009 and 2016
/// by a plugin that no longer exists, and they are the only sample of the format
/// nobody can rewrite.
///
///   This exists to be the backstop for 8.1, which replaces the XML parser. A
/// parser swap that loses an attribute, mangles an entity or reads a float one
/// ulp differently changes what a preset sounds like and nothing else in the
/// suite would notice: the goldens render effects at their *defaults*, and the
/// parameter table snapshot never opens a file.
///
////////////////////////////////////////////////////////////////////////////////
///
/// \note **The digests are over `data/fixtures`, not over the shipping banks.**
/// Until 14.08.2026 there was a committed row per shipped preset in
/// `presetCorpus.txt`, and it worked exactly as long as nobody touched the
/// content. Adding a preset was a failure, deleting one was a failure, and
/// re-voicing one was a failure, all three reported the same way as a parser
/// regression. The banks are now edited as ordinary work, so a snapshot of them
/// says more about the last person to open the browser than about this tree.
///
///   The eight files under `data/fixtures` are copies, frozen deliberately, and
/// chosen for the shapes a parser can break rather than for how they sound:
///
///   | Fixture | The shape it holds |
///   |---|---|
///   | `Autotune/Aeolian` | `<1>`..`<12>`, TuneWorx's semitones -- an element name no conforming parser reads, so this one only loads through `repairLegacyElementNames()` |
///   | `Gamma Shift/Bojangles` | the other repair shape: `PVD start`, `Imploder (pvd)`, `PVD stop` -- parenthesised effect names |
///   | `Echoes/Jumbo Jet` | four Freqverbs, four parameters the 2011 file never mentions (`HF absorb`, added later) |
///   | `ESS/Once Upon A Time` | two modules, one missing parameter |
///   | `Echoes/Great Escape` | three modules, Frecho's distance and a Gain |
///   | `Voices/Robokid` | LFOs: `sync="0"` with a `T=` period, on six parameters across three modules |
///   | `Overt Dynamics/Stalactite Rock Steady` | a full five-module chain |
///   | `Gamma Shift/Bird Song` | five modules, 38 parameters, the widest dump here |
///
///   They may be added to. They may not be edited: a fixture that is refreshed
/// to make a test pass is a fixture that has stopped testing anything.
///
////////////////////////////////////////////////////////////////////////////////
///
///   One row per fixture rather than one per parameter -- a diff nobody can read
/// is a diff nobody reads. The row carries the module count and the effect names
/// in the clear, so the common failures (a preset losing a module, or loading
/// the wrong effect) name themselves; everything finer is behind a hash of the
/// canonical dump. SW_PRESET_DUMP=<substring> prints those dumps in full, which
/// is the first thing to reach for when a hash moves.
///
///   Values are formatted to six significant figures before hashing. They come
/// from decimal text in the file by way of strtof, so they are the same number
/// everywhere; six figures is enough to catch a parameter that moved and not so
/// many that a legitimate difference in the last bit of a conversion turns the
/// file red on another platform.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "presets/presetHarness.hpp"

#include "core/modules/factory.hpp"

/// \note Not sorted with the block above: finalImplementations names
/// GUI::ModuleUI, which moduleDSPAndGUI is what defines. loadPreset() is a
/// template over the consumer and downcasts a chain node to SW::Module, so this
/// translation unit needs the complete type.
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/modules/finalImplementations.hpp"

#include "le/spectrumworx/factoryPresets.hpp"
#include "le/spectrumworx/presetStorage.hpp"
#include "le/spectrumworx/presets.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace LE;
using namespace LE::SW;

using SWTest::digest;
using SWTest::dump;
using SWTest::Loaded;
using SWTest::PresetConsumer;
using SWTest::ScopedProblemCounter;

std::string snapshotPath() { return std::string(SW_PRESET_SNAPSHOT_DIR) + "/presetFixtures.txt"; }

//------------------------------------------------------------------------------
// The corpus
//------------------------------------------------------------------------------

/// `<bank>/<preset>`, so the key survives a change of checkout path and sorts
/// the way the browser shows them.
std::vector<std::pair<std::string, std::filesystem::path>>
presetsUnder(std::filesystem::path const &root)
{
    std::vector<std::pair<std::string, std::filesystem::path>> found;

    std::error_code error;
    for (auto const &entry : std::filesystem::recursive_directory_iterator(root, error))
    {
        if (!entry.is_regular_file() || (entry.path().extension() != ".swp"))
            continue;
        found.emplace_back(std::filesystem::relative(entry.path(), root, error).generic_string(),
                           entry.path());
    }

    std::ranges::sort(found, {}, &std::pair<std::string, std::filesystem::path>::first);
    return found;
}

/// Everything the plugin ships, which is edited as ordinary work.
std::vector<std::pair<std::string, std::filesystem::path>> corpus()
{
    return presetsUnder(SW_PRESET_DATA_DIR);
}

/// The frozen copies the digests are over. \see the note at the top of the file.
std::vector<std::pair<std::string, std::filesystem::path>> fixtures()
{
    return presetsUnder(SW_PRESET_FIXTURE_DIR);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Every directory under the preset root that has a preset somewhere
/// below it, sorted, relative to the root -- which is what banks() must answer
/// with, row for row.
///
/// \note Ancestors included, and that is the whole point of it. `Martin Walker`
/// holds no preset of its own, only `Martin Walker/Gamma Shift`, and a listing
/// that leaves the parent out leaves the browser no row to open the child by.
///
////////////////////////////////////////////////////////////////////////////////

std::vector<std::string> bankDirectories()
{
    std::set<std::string> found;
    for (auto const &[key, path] : corpus())
        for (auto separator(key.find('/')); separator != std::string::npos;
             separator = key.find('/', separator + 1))
            found.insert(key.substr(0, separator));
    return {found.begin(), found.end()};
}

/// \note One engine per preset, never one reused across the sweep. Loading a preset
/// *merges* into the current chain -- loadModuleChain() looks for a module
/// already holding the same effect and moves it across rather than building a
/// new one -- so a reused engine would make every row depend on the row before
/// it, and a snapshot in which row 200 changes when row 199 does is not a
/// snapshot of row 200.
/// \brief Loads \p data into a fresh engine and dumps it, optionally handing
/// back what this build would write that engine out as.
Loaded loadBuffer(std::vector<char> data, bool &succeeded, std::string *const pRewritten = nullptr)
{
    SWTest::Engine engine;
    engine.setNumberOfChannels(2, 2);
    engine.setSampleRate(48000);
    engine.setBlockSize(512);
    REQUIRE(engine.initialise());

    succeeded = false;

    SWTest::clearPresetProblems();
    {
        ScopedProblemCounter const counting;
        if (!LE::SW::loadPreset(data.data(), true /*ignore external samples*/, nullptr,
                                PresetConsumer{engine}))
            return {};
    }

    /// \note No effect a preset names may be unknown -- the 57 shipped are the
    /// 57 these banks were written against, and one going missing is a build
    /// that silently dropped an effect rather than a preset that is wrong.
    CHECK(SWTest::presetProblems().unknownEffect == 0);

    if (pRewritten)
        *pRewritten = savePreset({}, engine.sideChainSource(), {}, engine.program());

    succeeded = true;
    auto loaded(dump(engine));
    loaded.missing = SWTest::presetProblems().missingParameter;
    return loaded;
}

std::vector<char> readPreset(std::filesystem::path const &file)
{
    auto const presetData(readPresetFile(file));
    if (!presetData)
        return {};
    std::string_view const text(presetData.get());
    std::vector<char> buffer(text.begin(), text.end());
    buffer.push_back('\0'); // the parse is destructive and wants a terminator
    return buffer;
}

Loaded load(std::filesystem::path const &file, bool &succeeded, std::string *const pRewritten)
{
    auto data(readPreset(file));
    if (data.empty())
    {
        succeeded = false;
        return {};
    }
    return loadBuffer(std::move(data), succeeded, pRewritten);
}

using Table = std::map<std::string, std::string>;

Table readTable()
{
    Table table;
    std::ifstream stream(snapshotPath());
    std::string line;
    while (std::getline(stream, line))
    {
        if (line.empty() || (line.front() == '#'))
            continue;
        auto const separator(line.find(" | "));
        REQUIRE(separator != std::string::npos);
        table.emplace(line.substr(0, separator), line.substr(separator + 3));
    }
    return table;
}

void writeTable(Table const &table)
{
    std::ofstream file(snapshotPath(), std::ios::trunc);
    file << "# SpectrumWorx frozen preset fixtures -- generated, do not hand edit.\n"
            "# Regenerate with SW_PRESET_CORPUS_UPDATE=1 ./sw-dsp-tests \"[preset-corpus]\"\n"
            "#\n"
            "#   Over tests/presets/data/fixtures, NOT over assets/presets. The shipped\n"
            "#   banks are edited as ordinary work -- presets get added, removed and\n"
            "#   re-voiced -- so a digest per shipped preset reported content changes\n"
            "#   and parser regressions the same way, which made it useless for the\n"
            "#   second. These eight files are frozen copies and never change.\n"
            "#\n"
            "#   So: regenerating this file is only ever correct when a fixture has been\n"
            "#   *added*. If a row moved, the reader changed, and that is the finding.\n"
            "#\n"
            "# <bank>/<preset> | <modules> | <effects> | <parameters> | <missing> | <digest>\n"
            "#     what loading that file into a fresh engine produces. <missing> counts\n"
            "#     parameters the effect has and the preset never mentions -- normal for\n"
            "#     a 2009-2011 file against a 2016 effect. The digest is FNV-1a over the\n"
            "#     full parameter dump, values at six significant figures;\n"
            "#     SW_PRESET_DUMP=<substring> prints those dumps.\n";
    for (auto const &[key, row] : table)
        file << key << " | " << row << '\n';
}

bool environmentFlag(char const *const name)
{
    auto const *const value(std::getenv(name));
    return value && (*value != '\0') && (*value != '0');
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("Every frozen fixture loads and produces the committed state", "[preset-corpus]")
{
    auto const files(fixtures());
    INFO("fixture directory " << SW_PRESET_FIXTURE_DIR);
    REQUIRE_FALSE(files.empty()); // an empty sweep is a failure, not a pass

    auto const *const dumpFilter(std::getenv("SW_PRESET_DUMP"));

    Table table;
    for (auto const &[key, path] : files)
    {
        INFO("preset " << key);

        bool succeeded{false};
        auto const loaded(load(path, succeeded, nullptr));
        REQUIRE(succeeded); // stage 8: an unmodified 2016-era preset file still loads

        if (dumpFilter && (key.find(dumpFilter) != std::string::npos))
            WARN(key << ":\n" << loaded.text);

        std::array<char, 32> digestText{};
        std::snprintf(digestText.data(), digestText.size(), "%016llx",
                      static_cast<unsigned long long>(digest(loaded.text)));

        table.emplace(key, std::to_string(loaded.modules) + " | " + loaded.effects + " | " +
                               std::to_string(loaded.parameters) + " | " +
                               std::to_string(loaded.missing) + " | " + digestText.data());
    }

    if (environmentFlag("SW_PRESET_CORPUS_UPDATE"))
    {
        writeTable(table);
        WARN("SW_PRESET_CORPUS_UPDATE was set: " << table.size()
                                                 << " rows rewritten. Read the diff before "
                                                    "committing it.");
        return;
    }

    auto const expected(readTable());
    REQUIRE_FALSE(expected.empty()); // an absent or empty file is a failure, not a pass

    for (auto const &[key, row] : expected)
    {
        auto const found(table.find(key));
        INFO("fixture " << key);
        REQUIRE(found != table.end()); // a fixture that disappeared
        CHECK(found->second == row);
    }

    /// \note A fixture with no row is a failure rather than a warning, unlike
    /// anything about the shipping banks: these files are added deliberately and
    /// adding one means generating its row in the same commit. Eight of them is
    /// not a set anybody adds to by accident.
    for (auto const &[key, row] : table)
    {
        INFO("fixture " << key << " = " << row);
        CHECK(expected.find(key) != expected.end()); // a fixture with no committed row
    }

    CHECK(table.size() == expected.size());
}

////////////////////////////////////////////////////////////////////////////////
//
// The translation
// ---------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   Every factory preset, read through the 2.x reader, written out by the 3.0
/// writer, and read back through the 3.0 reader. What comes out the far end must
/// be what went in.
///
///   This is the claim the format change rests on: that 3.0 carries everything
/// 2.x carried. It is a different question from "does the new writer round-trip"
/// -- presetRoundTripTests answers that, from parameters this build chose -- and
/// a stronger one, because the input is fifteen years of files nobody can
/// rewrite, holding shapes the round-trip fixtures do not think to produce:
/// parameters absent because the effect grew them later, LFOs from before sync
/// types existed, values written by a printer that is gone.
///
/// \note **Self-comparing**, and it did not used to be: both halves were checked
/// against the committed row for that preset, which meant the whole shipping
/// bank had to hold still for this case to be green. It compares the two dumps
/// against *each other* now, so the banks may be edited freely and every file in
/// them -- including one added this morning -- is still put through both
/// readers and the writer.
///
///   What that gives up is a reader change that alters both paths *identically*:
/// the direct read and the re-read would move together and agree. That is what
/// the frozen fixtures above are for, and it is why they are a separate case
/// over files that never change rather than a floor on this one.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Every factory preset survives translation into the 3.0 format", "[preset-corpus]")
{
    auto const files(corpus());
    REQUIRE_FALSE(files.empty());

    for (auto const &[key, path] : files)
    {
        INFO("preset " << key);

        bool succeeded{false};
        std::string rewritten;
        auto const legacy(load(path, succeeded, &rewritten));
        REQUIRE(succeeded);
        REQUIRE_FALSE(rewritten.empty());

        /// \note Both facts about the file this build would now write: that it
        /// says which grammar it is in, and that it needs no repair pass to
        /// parse. A good few of the shipped files do need one on the way *in*;
        /// none may need one on the way out.
        INFO("rewritten as:\n" << rewritten);
        CHECK(rewritten.find("Format=\"3\"") != std::string::npos);
        CHECK(rewritten.find("<p n=") != std::string::npos);

        std::vector<char> parseBuffer(rewritten.begin(), rewritten.end());
        parseBuffer.push_back('\0');

        bool translated{false};
        auto const reloaded(loadBuffer(std::move(parseBuffer), translated));
        REQUIRE(translated);

        // The chain first, in the clear: a module lost in translation, or an
        // effect that came back as a different one, names itself here.
        CHECK(reloaded.modules == legacy.modules);
        CHECK(reloaded.effects == legacy.effects);
        CHECK(reloaded.parameters == legacy.parameters);

        /// \note And then every parameter of every module, which is the claim.
        /// Compared as text rather than as a digest: these two dumps are in the
        /// same process and a difference is worth printing, where the fixture
        /// case compares against a file and wants one short row.
        CHECK(reloaded.text == legacy.text);

        /// \note The parameters a 2011 file never mentioned are still not
        /// mentioned after a translation -- the writer writes what the *engine*
        /// holds, so they come back as defaults and are no longer missing. That
        /// is the one thing that legitimately differs between the two dumps.
        CHECK(reloaded.missing == 0); // a file this build wrote cannot be missing a parameter
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// What the shipped files say about their side chain
// -------------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   Every shipped preset carries `Input_mode`, and until 18.08.2026 nothing read
/// it: the 2016 parameter behind it was compiled out and then deleted, so every
/// one of them had been recording an intention the plugin had no way to act on. It is
/// not a parameter here and never will be again -- bus topology is not a setting
/// -- but it is exactly enough to recover what those files *meant*, which is
/// which of the three sources feeds their side channel. \see
/// doc/tech/sidechain-approach.md and issue #113.
///
///   In this harness no sample is ever applied (`wantsSampleFile()` declines
/// them, as the browser's "Ignore external audio" does), so every preset takes
/// the migration's second arm and `Input_mode` decides alone. The bank named
/// `Sidechainables` is the ten that asked for a host send; everything else is
/// self side chain. A build that read the attribute wrongly -- wrong key, an
/// off-by-one, odd-vs-even inverted -- would either lose all ten or gain the
/// other 278.
///
/// \note What this cannot show is the arm those ten take *in the plugin*, where
/// a sample is applied and the file wins: all ten name a carrier chosen to match.
/// `tests/external_audio/sampleFeedTests.cpp` is where that half lives.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("An old preset's input mode becomes the source it always meant",
          "[preset-corpus][side-chain][issue-113]")
{
    auto const files(corpus());
    REQUIRE_FALSE(files.empty());

    std::vector<std::string> fromHost;
    for (auto const &[key, path] : files)
    {
        INFO("preset " << key);

        bool succeeded{false};
        auto const loaded(load(path, succeeded, nullptr));
        REQUIRE(succeeded);

        /// \note One or the other and never neither, and never `file`: nothing
        /// here loads a sample, so a `file` row would be a migration that had
        /// invented a source it cannot honour.
        auto const readsHost(loaded.text.find("side chain source = host\n") != std::string::npos);
        CHECK((readsHost || (loaded.text.find("side chain source = main\n") != std::string::npos)));

        if (readsHost)
            fromHost.push_back(key);
    }

    /// \note The bank rather than the count, so that a preset added to or removed
    /// from `Sidechainables` reads as ordinary content work and a preset
    /// *elsewhere* that starts asking for a host send reads as a finding.
    REQUIRE_FALSE(fromHost.empty());
    for (auto const &key : fromHost)
        CHECK(std::string_view(key).starts_with("Sidechainables/"));

    /// ...and the whole bank, not part of it.
    std::size_t inTheBank{0};
    for (auto const &[key, path] : files)
        inTheBank += std::string_view(key).starts_with("Sidechainables/");
    CHECK(fromHost.size() == inTheBank);
}

////////////////////////////////////////////////////////////////////////////////
//
// What a 2.x file leaves out
// --------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note Issue #15 moved the Octaver's `Low pass` from 350 Hz to its maximum,
/// and the objection to moving it was that a 2011 preset would then sound
/// different. It does not, and the reason is the first half below: the 2.x
/// writer emits **every** parameter an effect has, at its default or not --
/// `Gain 0`, `Start frequency 0` and `Stop frequency 1` are written out in every
/// shipped file -- so a preset naming an Octaver names its cutoff too and
/// the compiled default is never reached for.
///
///   Hand-built rather than taken from the corpus because **no factory preset
/// uses the Octaver at all**, which is exactly why the claim had nothing
/// holding it: there was no file anywhere in the tree to contradict "2011
/// presets skip their defaults" with.
///
///   The second half is the same claim from the other side, and it is the only
/// route by which moving a default can move an old preset: a parameter the file
/// genuinely does not carry -- one the effect grew afterwards -- takes *today's*
/// default rather than the one in force when the file was written. That is
/// `PresetProblem::MissingParameter`, it is ordinary (a good fraction of the
/// shipped banks raise one; \see PresetLoadReport), and it is what a table of prior defaults
/// would exist to change. Nothing needs one yet, so this records the behaviour
/// rather than working around it.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A 2.x preset carries its own defaults", "[preset-corpus][issue-15]")
{
    /// The 2011 grammar, by hand: the element name is the mangled parameter
    /// name, the value is the element's text, and the module element is the
    /// mangled effect title. \see streaming_format.md §4.2.
    auto const octaverPreset([](std::string_view const cutoffElement) {
        std::string preset(
            "<SpectrumWorxPreset Version=\"2.6\" LastModified=\"15.12.2011 15:35\" Comment=\"\">\n"
            "\t<Global In=\"1.0000\" Out=\"1.0000\" Mix=\"1.0000\" FFT_size=\"2048\" "
            "Overlap_factor=\"4\" Window_type=\"0\" Input_mode=\"0\"/>\n"
            "\t<Modules>\n"
            "\t\t<Octaver Bypass=\"0\">\n"
            "\t\t\t<Gain sync=\"1\">0.0000</Gain>\n"
            "\t\t\t<Wet sync=\"1\">100.0000</Wet>\n"
            "\t\t\t<Start_frequency sync=\"1\">0.0000</Start_frequency>\n"
            "\t\t\t<Stop_frequency sync=\"1\">1.0000</Stop_frequency>\n"
            "\t\t\t<Octave_1 sync=\"1\">3</Octave_1>\n"
            "\t\t\t<Gain_1 sync=\"1\">0.0000</Gain_1>\n"
            "\t\t\t<Octave_2 sync=\"1\">2</Octave_2>\n"
            "\t\t\t<Gain_2 sync=\"1\">0.0000</Gain_2>\n");
        preset += cutoffElement;
        preset += "\t\t</Octaver>\n"
                  "\t</Modules>\n"
                  "</SpectrumWorxPreset>\n";
        std::vector<char> buffer(preset.begin(), preset.end());
        buffer.push_back('\0'); // the parse is destructive and wants a terminator
        return buffer;
    });

    SECTION("a cutoff it wrote survives a default that has moved out from under it")
    {
        bool succeeded{false};
        auto const loaded(loadBuffer(
            octaverPreset("\t\t\t<Low_pass sync=\"1\">350.0000</Low_pass>\n"), succeeded));
        REQUIRE(succeeded);
        INFO(loaded.text);

        CHECK(loaded.text.find("\n  Low pass = 350 |") != std::string::npos);
        CHECK(loaded.missing == 0);
    }

    SECTION("a cutoff it never wrote takes today's default")
    {
        bool succeeded{false};
        auto const loaded(loadBuffer(octaverPreset(""), succeeded));
        REQUIRE(succeeded);
        INFO(loaded.text);

        CHECK(loaded.text.find("\n  Low pass = 16000 |") != std::string::npos);
        CHECK(loaded.missing == 1);
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// The embedded copy
// -----------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note Stage 8.2 puts the banks in the binary so that a plugin which was
/// copied rather than installed still has them. What that is worth depends
/// entirely on the embedded bytes being the committed bytes, and a resource
/// library built from a glob is exactly the thing that silently ships fourteen
/// banks out of fifteen. So: byte-for-byte, both directions.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The embedded factory banks are the committed files", "[preset-corpus]")
{
    auto const files(corpus());
    REQUIRE_FALSE(files.empty());

    std::size_t embeddedCount{0};
    for (auto const &bank : FactoryPresets::banks())
    {
        INFO("bank " << bank);
        CHECK(FactoryPresets::isBank(bank));
        embeddedCount += FactoryPresets::presets(bank).size();
    }
    CHECK(embeddedCount == files.size()); // no bank quietly left out of the glob

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And the directories themselves, set against set. The count above
    /// cannot see this one: a bank that holds nothing but a sub-folder
    /// contributes no presets to it, so every such bank could go missing from the
    /// listing with the total unchanged -- leaving its presets in the binary,
    /// byte-for-byte correct, and unreachable from the browser because banks()
    /// named the sub-folder and never its parent.
    ///
    ////////////////////////////////////////////////////////////////////////////
    CHECK(FactoryPresets::banks() == bankDirectories());

    for (auto const &[key, path] : files)
    {
        INFO("preset " << key);

        auto const separator(key.find('/'));
        REQUIRE(separator != std::string::npos);
        auto const bank(key.substr(0, separator));
        auto const name(key.substr(separator + 1, key.size() - separator - 1 - 4 /*".swp"*/));

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \note `static_cast<bool>` rather than the bare handle, and it is not
        /// style. `InMemoryPreset` is a `std::unique_ptr<char[]>`; C++20 gave
        /// `unique_ptr` an `operator<<` that streams `get()`, which for this
        /// one is a `char *` and so streams as a **C string**. Catch2 stringifies
        /// the expression when an assertion fails, so a *null* handle -- the only
        /// case this line exists to report -- sent `strlen( nullptr )` into the
        /// failure path and the run died with SIGSEGV before printing which
        /// preset it was. The assertion segfaulted exactly when it was right.
        ///
        ///   Found by adding a preset to a bank without re-running CMake, which
        /// is the ordinary way to meet it: the resource library is built from a
        /// glob at configure time, so the file is on disk and not yet in the
        /// binary. The other nine sites in tests/ were the same shape.
        ///
        ////////////////////////////////////////////////////////////////////////
        auto const embedded(FactoryPresets::load(bank, name));
        // a preset on disk that is not in the binary -- re-run CMake, then look
        REQUIRE(static_cast<bool>(embedded));

        std::ifstream file(path, std::ios::binary);
        std::string const onDisk((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());

        /// \note The embedded copy is terminated by load(); the file may or may
        /// not be -- most committed files carry one and some do not -- so the
        /// comparison is over the file's own length.
        CHECK(std::string_view(embedded.get(), onDisk.size()) == onDisk);
    }

    /// \note And that a bank which is not there says so, rather than answering
    /// with an empty list that reads the same as an empty bank.
    CHECK_FALSE(FactoryPresets::isBank("No Bank"));
    CHECK(FactoryPresets::presets("No Bank").empty());
    CHECK_FALSE(FactoryPresets::load("Echoes", "No Preset"));
}
