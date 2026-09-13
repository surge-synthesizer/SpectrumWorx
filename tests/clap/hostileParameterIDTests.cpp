////////////////////////////////////////////////////////////////////////////////
///
/// \file hostileParameterIDTests.cpp
/// ---------------------------------
///
///   What the plugin does with a clap_id it never advertised.
///
///   A clap_id is 32 bits of host-supplied data and ParameterID decodes all of
/// them: a discriminator in the top byte and up to three indices below it. Those
/// indices reach `invokeFunctorOnIndexedParameter`, whose jump tables are
/// `cases[index]` guarded by nothing but an LE_ASSERT -- which is absent from
/// a release build, so an out-of-range index is an
/// out-of-bounds read and then an indirect call through whatever it found.
///
///   isValidParamId() is the one place all four host entry points funnel
/// through -- paramsValue, paramsValueToText, paramsTextToValue on the main
/// thread and handleEvent on the audio thread -- and everything downstream is
/// written assuming it happened. It checked the discriminator and none of the
/// indices.
///
///   A stale automation lane in an old project, a host rescanning against a
/// parameter list that has changed, or a validator sweeping the id space all
/// produce one of these.
///
/// \note The second half is as important as the first: a rule that rejects an id
/// the plugin *does* advertise silently drops a host's automation. So every id
/// paramsInfo hands out is required to be accepted, which is what stops the
/// bound from being tightened past the truth.
///
/// See doc/tech/parameter_system.md and core/parameterID.hpp.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "clap/testHost.hpp"

#include "configuration/constants.hpp"
#include "core/host_interop/parameters.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <set>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace SWTest;

constexpr double sampleRate{48000};
constexpr std::uint32_t blockSize{256};

using LE::SW::Constants::maxNumberOfModules;
using LE::SW::Constants::maxNumberOfParametersPerModule;
using LE::SW::ParameterCounts::lfoExportedParameters;

constexpr unsigned globalParameters{LE::SW::GlobalParameters::Parameters::static_size};

/// \note An LFO drives every module parameter except the first (Bypass), which is
/// why this is one less than the parameters a module has. \see parameterIDFromIndex.
constexpr unsigned lfoModuleParameters{maxNumberOfParametersPerModule - 1u};

/// The three index bytes under the discriminator, packed as ParameterID lays
/// them out: the type is the top byte and the low byte is a padding field for
/// every type except the LFO's.
clap_id rawID(unsigned const type, unsigned const high, unsigned const middle, unsigned const low)
{
    return static_cast<clap_id>((type << 24) | (high << 16) | (middle << 8) | low);
}

struct HostileID
{
    clap_id id;
    char const *why;
}; // struct HostileID

/// Every field of every type, one step past what the model can address.
std::vector<HostileID> hostileIDs()
{
    return {
        // The discriminator itself, which was the only thing checked.
        {rawID(4, 0, 0, 0), "type past LFOParameter"},
        {rawID(255, 0, 0, 0), "type at the top of the byte"},

        // Global: one index, and two padding bytes that must be zero or two
        // different clap_ids name one parameter.
        {rawID(0, globalParameters, 0, 0), "global index one past the last"},
        {rawID(0, 255, 0, 0), "global index at the top of the byte"},
        {rawID(0, 0, 1, 0), "global with a non-zero padding byte"},
        {rawID(0, 0, 0, 1), "global with a non-zero padding byte"},

        // Module chain: the slot selector.
        {rawID(1, maxNumberOfModules, 0, 0), "slot one past the last"},
        {rawID(1, 255, 0, 0), "slot at the top of the byte"},
        {rawID(1, 0, 0, 1), "slot selector with a non-zero padding byte"},

        // Module parameter: slot and parameter.
        {rawID(2, maxNumberOfModules, 0, 0), "module parameter in a slot past the last"},
        {rawID(2, 0, maxNumberOfParametersPerModule, 0), "module parameter past the last"},
        {rawID(2, 0, 255, 0), "module parameter at the top of the byte"},
        {rawID(2, 0, 0, 1), "module parameter with a non-zero padding byte"},

        // LFO: slot, the parameter it drives, and which of its own.
        {rawID(3, maxNumberOfModules, 0, 0), "LFO in a slot past the last"},
        {rawID(3, 0, lfoModuleParameters, 0), "LFO on a parameter that has none"},
        {rawID(3, 0, 255, 0), "LFO parameter index at the top of the byte"},
        {rawID(3, 0, 0, lfoExportedParameters), "LFO sub-parameter past the last"},
        {rawID(3, 0, 0, 255), "LFO sub-parameter at the top of the byte"},
    };
}

/// Every id the plugin tells the host about.
std::set<clap_id> advertisedIDs(clap_plugin const &plugin)
{
    auto const &params(parameters(plugin));
    std::set<clap_id> ids;
    for (auto const &info : allParameterInfo(plugin, params))
        ids.insert(info.id);
    return ids;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("A parameter id the plugin never advertised is declined", "[clap][parameters][hostile]")
{
    Entry const entry;

    ActivePlugin plugin(sampleRate, blockSize);
    auto const &params(parameters(*plugin));
    auto const advertised(advertisedIDs(*plugin));

    for (auto const &hostile : hostileIDs())
    {
        INFO(hostile.why << " -- id 0x" << std::hex << hostile.id);

        // If one of these ever becomes a real parameter the case is wrong rather
        // than the plugin, and this is what says so.
        REQUIRE(advertised.find(hostile.id) == advertised.end());

        double value{-1};
        CHECK(!params.get_value(&*plugin, hostile.id, &value));

        char display[64]{};
        CHECK(!params.value_to_text(&*plugin, hostile.id, 0.5, display, sizeof(display)));

        double parsed{-1};
        CHECK(!params.text_to_value(&*plugin, hostile.id, "0.5", &parsed));
    }
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The audio-thread half, and the one that matters most: handleEvent()
/// reaches the same decode from inside process(), where the indirect call the
/// jump table makes would be through whatever the out-of-bounds read found.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A parameter event naming an id the plugin never advertised is dropped",
          "[clap][parameters][hostile]")
{
    Entry const entry;

    ActivePlugin plugin(sampleRate, blockSize);
    auto const &params(parameters(*plugin));

    std::vector<float> leftIn(blockSize, 0), rightIn(blockSize, 0);
    std::vector<float> leftOut(blockSize, 0), rightOut(blockSize, 0);

    // What the six global parameters read before anything hostile arrives.
    std::vector<double> before;
    for (unsigned index(0); index < globalParameters; ++index)
    {
        double value{0};
        REQUIRE(params.get_value(&*plugin, rawID(0, index, 0, 0), &value));
        before.push_back(value);
    }

    for (auto const &hostile : hostileIDs())
    {
        INFO(hostile.why << " -- id 0x" << std::hex << hostile.id);

        OneParameterEvent const event(hostile.id, 0.75);
        plugin.process(leftIn, rightIn, leftOut, rightOut, nullptr, &*event);
        plugin.pumpMainThread();
    }

    // Nothing the host can address moved, which is the observable half of "the
    // event was dropped rather than applied somewhere".
    for (unsigned index(0); index < globalParameters; ++index)
    {
        double value{0};
        REQUIRE(params.get_value(&*plugin, rawID(0, index, 0, 0), &value));
        CHECK(value == before[index]);
    }
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note And the other direction. Rejecting too much is the failure mode a
/// tightened bound has, and it is invisible: a host's automation lane for a real
/// parameter simply stops answering.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Every parameter id the plugin advertises is accepted", "[clap][parameters][hostile]")
{
    Entry const entry;

    ActivePlugin plugin(sampleRate, blockSize);
    auto const &params(parameters(*plugin));

    auto const advertised(advertisedIDs(*plugin));
    REQUIRE(advertised.size() == LE::SW::ParameterCounts::maxNumberOfParameters);

    for (auto const id : advertised)
    {
        INFO("id 0x" << std::hex << id);

        double value{-1};
        CHECK(params.get_value(&*plugin, id, &value));

        char display[64]{};
        CHECK(params.value_to_text(&*plugin, id, value, display, sizeof(display)));
    }
}
