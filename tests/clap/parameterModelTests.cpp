////////////////////////////////////////////////////////////////////////////////
///
/// \file parameterModelTests.cpp
/// -----------------------------
///
///   The dynamic parameter model, which is the part of this port with the most
/// novel risk and the reason the plan chose CLAP: the parameter list is a
/// function of which effect currently sits in which of the module slots, and
/// the host has to be told when it changes.
///
///   These drive Plugin2HostPassiveInteropController's statics directly rather
/// than through clap_plugin_params. That layer is 5.5; this is the thing
/// underneath it, and proving it here means that when the CLAP extension
/// misbehaves the question is only ever about the extension.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "goldens/engineHarness.hpp"

#include "core/host_interop/plugin2Host.hpp"
#include "core/modules/factory.hpp"
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/modules/finalImplementations.hpp"
#include "core/parameterID.hpp"

#include "le/spectrumworx/effects/configuration/effectsList.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace LE;
using namespace LE::SW;

using Controller = Plugin2HostPassiveInteropController;

/// \brief An initialised engine with a Program, ready to have effects put into
/// its slots. The same shape the goldens use -- SpectrumWorxCore cannot be
/// instantiated on its own.
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

    /// Puts effect \p effectIndex into slot \p slot, and returns what actually
    /// landed there.
    std::int8_t insert(std::uint8_t const slot, std::int8_t const effectIndex)
    {
        return engine_.program()
            .moduleChain()
            .setParameter(slot, effectIndex, engine_.moduleInitialiser())
            .second;
    }

    Program const *program() const { return &const_cast<SWTest::Engine &>(engine_).program(); }

    std::uint16_t parameterCount() const { return Controller::numberOfParameters(program()); }

    std::vector<Plugins::ParameterID> parameterIDs() const
    {
        std::vector<Plugins::ParameterID> ids(parameterCount());
        Controller::getParameterIDs({ids.data(), ids.size()}, program());
        return ids;
    }

    /// \brief How many IDs getParameterIDs will write, counted the way it
    /// writes them -- one per module parameter, plus lfoExportedParameters for
    /// every module parameter but Bypass -- in arithmetic wide enough to hold
    /// the answer.
    ///
    /// \note Deliberately not measured by handing getParameterIDs an oversized
    /// buffer and finding the high-water mark: it asserts the buffer is exactly
    /// the right size, and this codebase's assert handler raises SIGINT.
    std::size_t idsThatWillBeWritten() const
    {
        std::size_t total{exportedGlobals() + Constants::maxNumberOfModules};
        program()->moduleChain().forEach<Engine::ModuleParameters>(
            [&total](Engine::ModuleParameters const &module) {
                std::size_t const n(module.numberOfParameters());
                total += n + (n - 1) * ParameterCounts::lfoExportedParameters;
            });
        return total;
    }

    /// The largest per-module LFO parameter count in the current program: the
    /// quantity the counting code has to find a type big enough for.
    std::size_t largestModuleLFOCount() const
    {
        std::size_t largest{0};
        program()->moduleChain().forEach<Engine::ModuleParameters>(
            [&largest](Engine::ModuleParameters const &module) {
                std::size_t const n(module.numberOfParameters());
                largest = std::max(largest, (n - 1) * ParameterCounts::lfoExportedParameters);
            });
        return largest;
    }

    static std::size_t exportedGlobals() { return Program::Parameters::static_size; }

    std::string name(Plugins::ParameterID const id) const
    {
        std::array<char, 256> buffer{};
        Controller::getParameterName(ParameterID(id), {buffer.data(), buffer.size()}, program());
        return std::string(buffer.data());
    }

    std::string label(Plugins::ParameterID const id) const
    {
        std::array<char, 256> buffer{};
        Controller::getParameterLabel(ParameterID(id), {buffer.data(), buffer.size()}, program());
        return std::string(buffer.data());
    }

    /// The names of every parameter belonging to one module slot.
    std::vector<std::string> namesInSlot(std::uint8_t const slot) const
    {
        std::vector<std::string> names;
        for (auto const id : parameterIDs())
        {
            ParameterID const parameterID(id);
            if ((parameterID.type() == ParameterID::ModuleParameter) &&
                (parameterID.value._.module.moduleIndex == slot))
                names.push_back(name(id));
        }
        return names;
    }

  private:
    SWTest::Engine engine_;
}; // class Fixture

/// \note No fixture: a null program is what selects the arms that answer for a
/// slot nothing is loaded in, and handing one over takes the other branch.
std::string nameWithoutProgram(ParameterID const parameterID)
{
    std::array<char, 256> buffer{};
    Controller::getParameterName(parameterID, {buffer.data(), buffer.size()}, nullptr);
    return std::string(buffer.data());
}

ParameterID moduleParameterID(std::uint8_t const slot, std::uint8_t const parameter)
{
    ParameterID parameterID;
    parameterID.value.type = ParameterID::ModuleParameter;
    parameterID.value._.module = {ParameterID::Zero, parameter, slot};
    return parameterID;
}

ParameterID lfoParameterID(std::uint8_t const slot, std::uint8_t const parameter,
                           std::uint8_t const lfoParameter)
{
    ParameterID parameterID;
    parameterID.value.type = ParameterID::LFOParameter;
    parameterID.value._.lfo = {lfoParameter, parameter, slot};
    return parameterID;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("An empty chain still reports the global parameters", "[parameters]")
{
    Fixture fixture;

    auto const count(fixture.parameterCount());
    CHECK(count > 0);

    // Every ID the model hands out has to be one the model recognises.
    auto const ids(fixture.parameterIDs());
    REQUIRE(ids.size() == count);
    for (auto const id : ids)
    {
        ParameterID const parameterID(id);
        CHECK(parameterID.type() <= ParameterID::LFOParameter);
        CHECK_FALSE(fixture.name(id).empty());
    }
}

TEST_CASE("numberOfParameters agrees with getParameterIDs for every effect", "[parameters]")
{
    // The two are a contract: a host allocates from the first and hands the
    // buffer to the second. They were written independently and have to be
    // checked against each other for every shape of program, not just an empty
    // one -- an empty chain hides every per-module disagreement there is.
    Fixture fixture;

    {
        INFO("empty chain");
        CHECK(fixture.idsThatWillBeWritten() == fixture.parameterCount());
    }

    for (std::int8_t effect(0);
         effect < static_cast<std::int8_t>(Effects::Constants::numberOfEffects); ++effect)
    {
        INFO("effect index " << static_cast<int>(effect));
        INFO("largest per-module LFO count " << fixture.largestModuleLFOCount());
        REQUIRE(fixture.insert(0, effect) == effect);
        CHECK(fixture.idsThatWillBeWritten() == fixture.parameterCount());
    }
}

TEST_CASE("Inserting an effect grows the parameter list", "[parameters]")
{
    Fixture fixture;

    auto const emptyCount(fixture.parameterCount());
    REQUIRE(fixture.insert(0, 0) == 0);
    auto const filledCount(fixture.parameterCount());

    CHECK(filledCount > emptyCount);
    CHECK(fixture.parameterIDs().size() == filledCount);
}

TEST_CASE("Swapping a slot's effect renames that slot's parameters", "[parameters]")
{
    // This is stage 5's headline claim, and the reason the plan rejected
    // AudioProcessorParameter: the names are a function of the program, and
    // asking again after a swap has to give different answers.
    Fixture fixture;

    REQUIRE(fixture.insert(0, 0) == 0);
    auto const firstNames(fixture.namesInSlot(0));
    REQUIRE_FALSE(firstNames.empty());

    // Find a second effect whose parameter names differ from the first's. Not
    // every pair does -- Bypass leads most of them -- so this searches rather
    // than assuming effect 1 will do.
    bool foundADifference{false};
    for (std::int8_t effect(1);
         effect < static_cast<std::int8_t>(Effects::Constants::numberOfEffects); ++effect)
    {
        REQUIRE(fixture.insert(0, effect) == effect);
        if (fixture.namesInSlot(0) != firstNames)
        {
            foundADifference = true;
            break;
        }
    }
    CHECK(foundADifference);
}

TEST_CASE("Every effect in the list can be put in a slot and named", "[parameters]")
{
    // The sweep the goldens do for audio, done for metadata: a null name or an
    // out-of-range parameter count here is a host-visible corruption, and it is
    // exactly the kind of thing an effect-specific parameter table gets wrong.
    Fixture fixture;

    for (std::int8_t effect(0);
         effect < static_cast<std::int8_t>(Effects::Constants::numberOfEffects); ++effect)
    {
        INFO("effect index " << static_cast<int>(effect));
        REQUIRE(fixture.insert(0, effect) == effect);

        auto const ids(fixture.parameterIDs());
        REQUIRE(ids.size() == fixture.parameterCount());

        for (auto const id : ids)
        {
            auto const parameterName(fixture.name(id));
            CHECK_FALSE(parameterName.empty());
            // getParameterName writes into a fixed buffer; a name that fills it
            // means something ran off the end of a table.
            CHECK(parameterName.size() < 255);
            // Labels are legitimately empty for unitless parameters, so only
            // the length is checked.
            CHECK(fixture.label(id).size() < 255);
        }
    }
}

TEST_CASE("Clearing a slot returns the parameter list to its empty size", "[parameters]")
{
    Fixture fixture;

    auto const emptyCount(fixture.parameterCount());

    REQUIRE(fixture.insert(0, 0) == 0);
    CHECK(fixture.parameterCount() > emptyCount);

    REQUIRE(fixture.insert(0, noModule) == noModule);
    CHECK(fixture.parameterCount() == emptyCount);
}

TEST_CASE("An LFO with no program to ask is named after the parameter it drives", "[parameters]")
{
    // Two arms answer this, and they have to agree on which parameter an LFO
    // belongs to: one composes onto the module parameter's own name, the other
    // -- reached only with no program -- prints the position itself. The second
    // said "the base block is five" in terms of the exported LFO parameter
    // count, which stopped being five with issue #159.
    for (std::uint8_t slot(0); slot < Constants::maxNumberOfModules; ++slot)
    {
        for (std::uint8_t parameter(0); parameter < Constants::maxNumberOfParametersPerModule - 1u;
             ++parameter)
        {
            auto const driven(nameWithoutProgram(moduleParameterID(slot, parameter + 1u)));
            auto const expected(driven + " - LFO ");
            auto const composed(nameWithoutProgram(lfoParameterID(slot, parameter, 0)));

            INFO("got \"" << composed << "\", expected it to start \"" << expected << '"');
            CHECK(composed.compare(0, expected.size(), expected) == 0);
        }
    }
}
