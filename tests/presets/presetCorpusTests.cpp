////////////////////////////////////////////////////////////////////////////////
///
/// \file presetCorpusTests.cpp
/// ---------------------------
///
///   Every factory preset, loaded into a real engine, snapshotted.
///
///   Stage 8's "done when" says an unmodified 2016-era preset file still loads.
/// There are 303 of them committed under assets/presets, written between 2009
/// and 2016 by a plugin that no longer exists, and they are the only sample of
/// the format nobody can rewrite. This walks all of them.
///
///   It exists to be the backstop for 8.1, which replaces the XML parser. A
/// parser swap that loses an attribute, mangles an entity or reads a float one
/// ulp differently changes what a preset sounds like and nothing else in the
/// suite would notice: the goldens render effects at their *defaults*, and the
/// parameter table snapshot never opens a file.
///
///   One row per preset rather than one per parameter -- the full dump is
/// ~8000 lines, and a diff nobody can read is a diff nobody reads. The row
/// carries the module count and the effect names in the clear, so the common
/// failures (a preset losing a module, or loading the wrong effect) name
/// themselves; everything finer is behind a hash of the canonical dump.
/// SW_PRESET_DUMP=<substring> prints those dumps in full, which is the first
/// thing to reach for when a hash moves.
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

std::string snapshotPath() { return std::string(SW_PRESET_SNAPSHOT_DIR) + "/presetCorpus.txt"; }

//------------------------------------------------------------------------------
// The corpus
//------------------------------------------------------------------------------

/// `<bank>/<preset>`, so the key survives a change of checkout path and sorts
/// the way the browser shows them.
std::vector<std::pair<std::string, std::filesystem::path>> corpus()
{
    std::filesystem::path const root(SW_PRESET_DATA_DIR);
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

/// \note One engine per preset, not one reused across all 303. Loading a preset
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
        *pRewritten = savePreset({}, {}, engine.program());

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
    file << "# SpectrumWorx factory preset corpus -- generated, do not hand edit.\n"
            "# Regenerate with SW_PRESET_CORPUS_UPDATE=1 ./sw-tests \"[preset-corpus]\"\n"
            "#\n"
            "# <bank>/<preset> | <modules> | <effects> | <parameters> | <missing> | <digest>\n"
            "#     what loading that file into a fresh engine produces. <missing> counts\n"
            "#     parameters the effect has and the preset never mentions -- normal for\n"
            "#     a 2009-2011 file against a 2016 effect, and a number that must not\n"
            "#     grow. The digest is FNV-1a over the full parameter dump, values at six\n"
            "#     significant figures; SW_PRESET_DUMP=<substring> prints those dumps.\n";
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

TEST_CASE("Every factory preset loads and produces the committed state", "[preset-corpus]")
{
    auto const files(corpus());
    INFO("preset directory " << SW_PRESET_DATA_DIR);
    REQUIRE(files.size() >= 288); // 288 as committed; an empty sweep is a failure, not a pass

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
        INFO("preset " << key);
        REQUIRE(found != table.end()); // a preset that disappeared
        CHECK(found->second == row);
    }

    for (auto const &[key, row] : table)
    {
        INFO("preset " << key << " = " << row);
        CHECK(expected.find(key) != expected.end()); // a preset that appeared
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
/// be the row the snapshot above committed.
///
///   This is the claim the format change rests on: that 3.0 carries everything
/// 2.x carried. It is a different question from "does the new writer round-trip"
/// -- presetRoundTripTests answers that, from parameters this build chose -- and
/// a stronger one, because the input is fifteen years of files nobody can
/// rewrite, holding shapes the round-trip fixtures do not think to produce:
/// parameters absent because the effect grew them later, LFOs from before sync
/// types existed, values written by a printer that is gone.
///
///   It shares the committed table rather than one of its own, and that is the
/// point: two paths into the same 303 digests, so a translation that quietly
/// dropped an attribute cannot agree with the direct read.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Every factory preset survives translation into the 3.0 format", "[preset-corpus]")
{
    auto const files(corpus());
    REQUIRE(files.size() >= 288);

    auto const expected(readTable());
    REQUIRE_FALSE(expected.empty());

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
        /// parse. 25 of these 303 do need one on the way *in*; none may need one
        /// on the way out.
        INFO("rewritten as:\n" << rewritten);
        CHECK(rewritten.find("Format=\"3\"") != std::string::npos);
        CHECK(rewritten.find("<p n=") != std::string::npos);

        std::vector<char> parseBuffer(rewritten.begin(), rewritten.end());
        parseBuffer.push_back('\0');

        bool translated{false};
        auto const reloaded(loadBuffer(std::move(parseBuffer), translated));
        REQUIRE(translated);

        /// \note The parameters a 2011 file never mentioned are still not
        /// mentioned after a translation -- the writer writes what the *engine*
        /// holds, so they come back as defaults and are no longer missing. That
        /// is the one column that legitimately differs, and comparing the rest
        /// against the committed row is what says nothing else did.
        std::array<char, 32> digestText{};
        std::snprintf(digestText.data(), digestText.size(), "%016llx",
                      static_cast<unsigned long long>(digest(reloaded.text)));

        auto const found(expected.find(key));
        REQUIRE(found != expected.end());

        auto const row(std::to_string(reloaded.modules) + " | " + reloaded.effects + " | " +
                       std::to_string(reloaded.parameters) + " | " +
                       std::to_string(legacy.missing) + " | " + digestText.data());
        CHECK(found->second == row);
        CHECK(reloaded.missing == 0); // a file this build wrote cannot be missing a parameter
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
    REQUIRE(files.size() >= 288);

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
    /// contributes no presets to it, so all three of them could go missing from
    /// the listing with the total still reading 303 -- which is exactly what
    /// happened. 110 of these presets were in the binary, byte-for-byte correct,
    /// and unreachable from the browser, because banks() named
    /// `Martin Walker/Gamma Shift` and never `Martin Walker`.
    ///                                       (10.08.2026.) (SW port)
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

        auto const embedded(FactoryPresets::load(bank, name));
        REQUIRE(embedded); // a preset on disk that is not in the binary

        std::ifstream file(path, std::ios::binary);
        std::string const onDisk((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());

        /// \note The embedded copy is terminated by load(); the file may or may
        /// not be -- 193 of the 303 end in a NUL and 110 do not -- so the
        /// comparison is over the file's own length.
        CHECK(std::string_view(embedded.get(), onDisk.size()) == onDisk);
    }

    /// \note And that a bank which is not there says so, rather than answering
    /// with an empty list that reads the same as an empty bank.
    CHECK_FALSE(FactoryPresets::isBank("No Bank"));
    CHECK(FactoryPresets::presets("No Bank").empty());
    CHECK_FALSE(FactoryPresets::load("Echoes", "No Preset"));
}
