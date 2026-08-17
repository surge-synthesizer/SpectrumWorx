////////////////////////////////////////////////////////////////////////////////
///
/// \file parameterTableTests.cpp
/// -----------------------------
///
///   Stage 7.0: a committed snapshot of the whole parameter table, taken before
/// the parameter system stops being a Boost.Fusion sequence.
///
///   Stage 7's characteristic failure mode is not a crash. It is silently
/// reordering, renaming or retyping a parameter -- which silently changes every
/// saved preset, because the 2016 preset schema serialises parameters *by name*
/// (stage 0.7) and the automation model addresses them *by index*. No audio test
/// catches that: the goldens drive the engine with parameters at their defaults
/// and would render identically with two of them swapped.
///
///   Two tables, from the two places the tuple is observable:
///
///   - `effect/...`, from Engine::ModuleParameters::parameterInfo(). This is the
///     per-effect table: type, range, default, name and unit, in the order the
///     LE_DEFINE_PARAMETERS sequence declared them. It is what presets bind to.
///   - `id/...`, from Plugin2HostPassiveInteropController, keyed by the packed
///     ParameterID a host actually sees. It covers what the first cannot: the
///     globals, the slot selectors and the seven exported LFO parameters.
///
///   Regenerate with SW_PARAMETER_TABLE_UPDATE=1 and read the diff. Every line
/// that moves is a preset that will not load the same way.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "goldens/engineHarness.hpp"

#include "core/host_interop/plugin2Host.hpp"
#include "core/modules/factory.hpp"
#include "core/parameterID.hpp"

/// \note Not sorted with the block above, and it matters: finalImplementations
/// names GUI::ModuleUI, which moduleDSPAndGUI is what defines.
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/modules/finalImplementations.hpp"

#include "le/parameters/runtimeInformation.hpp"
#include "le/spectrumworx/effects/configuration/constants.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/engine/moduleParameters.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace LE;
using namespace LE::SW;

namespace Effects = LE::SW::Effects;

using Controller = Plugin2HostPassiveInteropController;

std::string tablePath() { return std::string(SW_PARAMETER_DATA_DIR) + "/parameterTable.txt"; }

/// \note '|' separates the columns and parameter names contain spaces, so the
/// key gets underscores rather than the values getting quotes.
std::string sanitised(std::string_view const text)
{
    std::string result;
    for (auto const character : text)
        result += (character == ' ') ? '_' : character;
    return result;
}

/// Exactly, and identically on every platform: %g would print 0.1f as "0.1" and
/// hide a change in the last bit, which for a parameter default is exactly the
/// kind of change this file exists to catch.
std::string number(float const value)
{
    std::array<char, 64> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.9g", static_cast<double>(value));
    return buffer.data();
}

/// \note Units have a *significant* leading space -- " dB", not "dB" -- and are
/// the last column, so written bare they would put trailing whitespace on 534 of
/// these lines and leave the file one editor's save away from a diff nobody
/// meant. The brackets make both ends visible and neither strippable.
std::string bracketed(std::string_view const text) { return "[" + std::string(text) + "]"; }

char const *typeName(Parameters::RuntimeInformation::Type const type)
{
    switch (type)
    {
    case Parameters::RuntimeInformation::Boolean:
        return "bool";
    case Parameters::RuntimeInformation::Enumerated:
        return "enum";
    case Parameters::RuntimeInformation::FloatingPoint:
        return "float";
    case Parameters::RuntimeInformation::Integer:
        return "int";
    case Parameters::RuntimeInformation::Trigger:
        return "trigger";
    }
    return "?";
}

/// \brief An initialised engine with a Program, the same shape the goldens and
/// parameterModelTests use -- SpectrumWorxCore cannot be instantiated alone.
class Fixture
{
  public:
    Fixture()
    {
        engine_.setNumberOfChannels(2, 2);
        engine_.setSampleRate(48000);
        engine_.setBlockSize(512);
        REQUIRE(engine_.initialise());
    }

    std::int8_t insert(std::uint8_t const slot, std::int8_t const effectIndex)
    {
        return engine_.program()
            .moduleChain()
            .setParameter(slot, effectIndex, engine_.moduleInitialiser())
            .second;
    }

    Program const *program() const { return &const_cast<SWTest::Engine &>(engine_).program(); }

    Engine::ModuleParameters const &moduleInSlot(std::uint8_t const slot) const
    {
        Engine::ModuleParameters const *pFound{nullptr};
        std::uint8_t index{0};
        program()->moduleChain().forEach<Engine::ModuleParameters>(
            [&](Engine::ModuleParameters const &module) {
                if (index++ == slot)
                    pFound = &module;
            });
        REQUIRE(pFound != nullptr);
        return *pFound;
    }

    std::vector<Plugins::ParameterID> parameterIDs() const
    {
        std::vector<Plugins::ParameterID> ids(Controller::numberOfParameters(program()));
        Controller::getParameterIDs({ids.data(), ids.size()}, program());
        return ids;
    }

    std::string name(Plugins::ParameterID const id) const
    {
        std::array<char, 256> buffer{};
        Controller::getParameterName(ParameterID(id), {buffer.data(), buffer.size()}, program());
        return buffer.data();
    }

    std::string label(Plugins::ParameterID const id) const
    {
        std::array<char, 256> buffer{};
        Controller::getParameterLabel(ParameterID(id), {buffer.data(), buffer.size()}, program());
        return buffer.data();
    }

  private:
    SWTest::Engine engine_;
}; // class Fixture

/// One row: a key nothing else uses, and the tuple 7.0 asks for.
using Table = std::map<std::string, std::string>;

/// \brief The per-effect table, from the engine's own runtime metadata.
///
/// \note Every effect goes into slot 0 in turn rather than one per slot: a
/// module's parameter table is a property of the effect, and putting them all
/// through the same slot means a difference between two rows cannot be a
/// difference between two slots.
///
/// \note Keyed by the effect's *streaming* name, the same as
/// streamingNameTests.cpp keys its `param/` rows and for the same reason: a
/// retitled effect must not drag its parameter rows with it. It carried the
/// title until 17.08.2026, and renaming two effects moved 64 rows that had
/// nothing to say about either. The title is not lost -- `streamingNames.txt`
/// records it, beside the streaming name it moved against.
void collectEffectTables(Table &table)
{
    Fixture fixture;
    for (std::uint8_t effect(0); effect < Effects::Constants::numberOfEffects; ++effect)
    {
        REQUIRE(fixture.insert(0, static_cast<std::int8_t>(effect)) ==
                static_cast<std::int8_t>(effect));

        auto const &module(fixture.moduleInSlot(0));
        std::string const effectKey(sanitised(Effects::effectStreamingName(effect)));

        for (std::uint8_t index(0); index < module.numberOfParameters(); ++index)
        {
            auto const &info(module.parameterInfo(index));

            std::string row;
            row += typeName(info.type);
            row += " | " + number(info.minimum);
            row += " | " + number(info.maximum);
            row += " | " + number(info.default_);
            row += " | ";
            row += info.name;
            row += " | " + bracketed(info.unit);

            if (info.enumeratedValueStrings)
            {
                row += " | ";
                for (auto value(static_cast<int>(info.minimum));
                     value <= static_cast<int>(info.maximum); ++value)
                {
                    if (value != static_cast<int>(info.minimum))
                        row += ';';
                    row += info.enumeratedValueStrings[value];
                }
            }

            std::array<char, 32> indexText{};
            std::snprintf(indexText.data(), indexText.size(), "%02u", index);
            table.emplace("effect/" + effectKey + "/" + indexText.data(), row);
        }
    }
}

/// \brief The host-visible table, keyed by the packed ParameterID.
///
/// \note One fixed program -- effects 0..4, one per slot -- rather than a sweep.
/// The names of a module's parameters are the previous table's business; what
/// this one pins is the *identifier space*: which id a parameter has, what its
/// module path implies, and the naming of the seven exported LFO parameters,
/// none of which the per-effect table can see.
void collectIdentifiedTable(Table &table)
{
    Fixture fixture;
    for (std::uint8_t slot(0); slot < Constants::maxNumberOfModules; ++slot)
        REQUIRE(fixture.insert(slot, static_cast<std::int8_t>(slot)) ==
                static_cast<std::int8_t>(slot));

    for (auto const id : fixture.parameterIDs())
    {
        std::array<char, 32> key{};
        std::snprintf(key.data(), key.size(), "id/%08x", static_cast<unsigned>(id.value));
        table.emplace(key.data(), fixture.name(id) + " | " + bracketed(fixture.label(id)));
    }
}

Table currentTable()
{
    Table table;
    collectEffectTables(table);
    collectIdentifiedTable(table);
    return table;
}

Table readTable()
{
    Table table;
    std::ifstream stream(tablePath());
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
    std::ofstream file(tablePath(), std::ios::trunc);
    file
        << "# SpectrumWorx parameter table -- generated, do not hand edit.\n"
           "# Regenerate with SW_PARAMETER_TABLE_UPDATE=1 ./sw-plugin-tests \"[parameter-table]\"\n"
           "#\n"
           "# effect/<streaming name>/<index> | type | min | max | default | name | [unit]\n"
           "#                                 [| enums]\n"
           "#     the per-effect parameter table, in declaration order. Presets\n"
           "#     serialise by name and automation addresses by index, so a row that\n"
           "#     moves is a preset that loads differently. The key is the effect's\n"
           "#     streaming name rather than its title -- which is why a few read\n"
           "#     '(pvd)' where the interface says '(PV)' -- so that retitling an\n"
           "#     effect does not drag its parameter rows with it. streamingNames.txt\n"
           "#     is where the two names are recorded side by side.\n"
           "#\n"
           "# id/<parameterID> | name | [unit]\n"
           "#     the same parameters as a host sees them, plus the globals, the slot\n"
           "#     selectors and the exported LFO parameters.\n";
    for (auto const &[key, row] : table)
        file << key << " | " << row << '\n';
}

bool updateRequested()
{
    auto const *const requested(std::getenv("SW_PARAMETER_TABLE_UPDATE"));
    return requested && (*requested != '\0') && (*requested != '0');
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("The parameter table matches the committed snapshot", "[parameter-table]")
{
    auto const table(currentTable());
    REQUIRE_FALSE(table.empty());

    if (updateRequested())
    {
        writeTable(table);
        WARN("SW_PARAMETER_TABLE_UPDATE was set: " << table.size()
                                                   << " rows rewritten. Read the "
                                                      "diff before committing it.");
        return;
    }

    auto const expected(readTable());
    REQUIRE_FALSE(expected.empty()); // an absent or empty file is a failure, not a pass

    // Reported per row rather than as one container comparison: "1348 rows
    // differ" is not a diagnosis, and the first differing row usually is.
    for (auto const &[key, row] : expected)
    {
        auto const found(table.find(key));
        INFO("parameter " << key);
        REQUIRE(found != table.end()); // a parameter that disappeared
        CHECK(found->second == row);
    }

    for (auto const &[key, row] : table)
    {
        INFO("parameter " << key << " = " << row);
        CHECK(expected.find(key) != expected.end()); // a parameter that appeared
    }

    CHECK(table.size() == expected.size());
}
