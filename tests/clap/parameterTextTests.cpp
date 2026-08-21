////////////////////////////////////////////////////////////////////////////////
///
/// \file parameterTextTests.cpp
/// ----------------------------
///
///   `text_to_value`, held to `value_to_text` as its inverse.
///
///   A round trip is the only honest way to test this pair, and the reason is
/// the bug that made the plugin decline to parse anything at all for a week: the
/// first implementation ran `strtod` over the text and returned the result as if
/// display units were storage units, which reads back an input gain of 0.001 as
/// -60 -- a number in the right *shape* and the wrong *units*. Nothing that
/// checks either direction on its own catches that. Printing a value, reading it
/// back, and printing it again does, for every parameter at once, without a
/// table of expected strings that would rot.
///
/// \note The re-print, rather than a tolerance on the value. "Within the
/// display's own precision" is exactly what a re-print asserts, and it needs no
/// per-parameter idea of how many decimal places that is: a gain shown at one
/// decimal place in dB and an FFT size shown as an integer are the same
/// assertion here.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "testHost.hpp"

#include "le/spectrumworx/effects/configuration/constants.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace SWTest;

namespace Effects = LE::SW::Effects;

/// What the host would show for \p id right now.
std::string displayOf(clap_plugin const &plugin, clap_plugin_params const &params, clap_id const id)
{
    double value{0};
    REQUIRE(params.get_value(&plugin, id, &value));

    std::array<char, CLAP_NAME_SIZE> text{};
    REQUIRE(params.value_to_text(&plugin, id, value, text.data(), text.size()));
    return text.data();
}

/// \brief Writes \p value the way a host does, and lets the main thread catch up.
///
/// \note Through the event queue rather than through anything private, because
/// what is being checked is that a parsed value is one the plugin will actually
/// take: a number that cannot be written back is not an answer to
/// "what value displays as this".
void write(ActivePlugin const &plugin, clap_id const id, double const value)
{
    OneParameterEvent const event(id, value);
    plugin.flush(&*event);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Prints \p id, reads the print back, writes what it read, and prints
/// again. The two prints must agree.
///
/// \returns whether the plugin agreed to parse its own display at all. Every
/// parameter should -- `text_to_value` may decline text in general, but its own
/// display is not text in general -- and the callers assert it; the bool is what
/// lets the failure name the parameter rather than the helper.
///
////////////////////////////////////////////////////////////////////////////////

bool roundTrips(ActivePlugin const &plugin, clap_plugin_params const &params, clap_id const id)
{
    auto const before(displayOf(*plugin, params, id));

    double parsed{0};
    if (!params.text_to_value(&*plugin, id, before.c_str(), &parsed))
        return false;

    // A value a host would refuse to carry is not a value.
    REQUIRE(parsed == parsed); // NaN
    CHECK(parsed >= -1e9);
    CHECK(parsed <= 1e9);

    write(plugin, id, parsed);

    CHECK(displayOf(*plugin, params, id) == before);
    return true;
}

/// What the host would show for \p id if it held \p value -- without writing it.
std::string displayOf(clap_plugin const &plugin, clap_plugin_params const &params, clap_id const id,
                      double const value)
{
    std::array<char, CLAP_NAME_SIZE> text{};
    REQUIRE(params.value_to_text(&plugin, id, value, text.data(), text.size()));
    return text.data();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief `roundTrips` for a value the parameter does *not* hold: print \p value,
/// read the print back, print that. The two prints must agree.
///
/// \note Nothing is written, which is the whole point -- a printer that ignored
/// its argument and answered about the parameter's own value would pass the
/// written round trip and fail this one for every value but the current one.
///
////////////////////////////////////////////////////////////////////////////////

void roundTripsUnwritten(ActivePlugin const &plugin, clap_plugin_params const &params,
                         clap_id const id, double const value)
{
    auto const shown(displayOf(*plugin, params, id, value));
    INFO("asked about " << value << ", shows '" << shown << "'");

    double parsed{0};
    REQUIRE(params.text_to_value(&*plugin, id, shown.c_str(), &parsed));
    CHECK(displayOf(*plugin, params, id, parsed) == shown);
}

/// Every parameter of slot \p slot -- its own and its LFOs' -- in id order.
std::vector<clap_param_info> slotParameters(clap_plugin const &plugin,
                                            clap_plugin_params const &params, unsigned const slot)
{
    std::vector<clap_param_info> mine;
    std::string const path("Module " + std::to_string(slot + 1));
    for (auto const &info : allParameterInfo(plugin, params))
        if ((path == info.module) || (path + "/LFO" == info.module))
            mine.push_back(info);
    return mine;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("Every parameter reads back what it displays", "[clap][text]")
{
    Entry const entry;
    ActivePlugin plugin(48000, 512);
    auto const &params(parameters(*plugin));

    ////////////////////////////////////////////////////////////////////////////
    // The whole fixed list, on an instance with nothing in any slot: the globals
    // and the five slot selectors are real, and every other one of the ~1500 is
    // a parameter no effect owns.
    //
    // Every one of them, and no exception for the absent ones, because
    // clap-validator's param-conversions allows no exception either: text_to_value
    // has to work for all the automatable parameters or for none, and every ID
    // here is automatable so that a host's lane survives an effect swap. What an
    // absent parameter displays as, and reads back from, is "N/A".
    ////////////////////////////////////////////////////////////////////////////

    /// \note Which parameters those are is read off the *id*, not off a flag.
    /// CLAP_PARAM_IS_HIDDEN used to mark them and is no longer set on anything
    /// (see paramsInfo), and asking the type is the better oracle anyway: on an
    /// instance with nothing in any slot, exactly the module and LFO parameters
    /// are the ones no effect owns, and the globals and the five slot selectors
    /// are the ones that are real.
    std::uint32_t absent{0}, present{0};
    for (auto const &info : allParameterInfo(*plugin, params))
    {
        INFO("parameter '" << info.name << "' in '" << info.module << "'");
        CHECK(roundTrips(plugin, params, info.id));

        bool const ownedByNoEffect(isNormalisedType(info.id));
        CHECK(displayOf(*plugin, params, info.id).starts_with("N/A") == ownedByNoEffect);
        (ownedByNoEffect ? absent : present)++;
    }

    // Both arms exercised, which is what makes the CHECKs above mean something.
    CHECK(present > 0);
    CHECK(absent > 0);
}

TEST_CASE("Every effect's own parameters read back what they display", "[clap][text]")
{
    // The coverage that matters: the base parameters are five, and the other
    // ~400 belong to the 57 effects and are reached only by putting each one in
    // a slot. Every display transform in the tree except the globals' is down
    // here -- the two frequencies' Hz, the ExImPloder gate's "-inf dB", every
    // enumerated parameter's value strings.
    Entry const entry;
    ActivePlugin plugin(48000, 512);
    auto const &params(parameters(*plugin));

    auto const slotSelector(parameterID(moduleChainType, 0));

    for (std::uint8_t effect(0); effect < Effects::Constants::numberOfEffects; ++effect)
    {
        INFO("effect " << unsigned(effect) << " '" << Effects::effectName(effect) << "'");
        write(plugin, slotSelector, effect);

        // The slot selector itself: its display is the effect's title.
        CHECK(displayOf(*plugin, params, slotSelector) == Effects::effectName(effect));
        CHECK(roundTrips(plugin, params, slotSelector));

        for (auto const &info : slotParameters(*plugin, params, 0))
        {
            INFO("parameter '" << info.name << "'");
            CHECK(roundTrips(plugin, params, info.id));
        }
    }
}

TEST_CASE("Text that is not a value is declined rather than guessed", "[clap][text]")
{
    // The whole reason parse() answers std::optional. strtod answers 0 for all
    // of these, and 0 is a value every one of these parameters can hold -- so a
    // parser that cannot say "no" silently writes one.
    Entry const entry;
    ActivePlugin plugin(48000, 512);
    auto const &params(parameters(*plugin));

    auto const inputGain(parameterID(globalType, 0));

    for (char const *const nonsense : {"", "  ", "off", "N/A", "dB", "nan", "-nan", "+"})
    {
        INFO("'" << nonsense << "'");
        double value{-1};
        CHECK_FALSE(params.text_to_value(&*plugin, inputGain, nonsense, &value));
    }

    // And an enumerated parameter is not an index: the window type displays a
    // word, so a number is not one of its values.
    auto const windowType(parameterID(globalType, 5));
    REQUIRE(std::string(displayOf(*plugin, params, windowType)) == "Hann");
    double value{-1};
    CHECK_FALSE(params.text_to_value(&*plugin, windowType, "3", &value));
    CHECK(params.text_to_value(&*plugin, windowType, "Blackman", &value));
    CHECK(value == 2);

    /// \note There is no third global to check here. `Input mode` was one for a
    /// day, on 17.08.2026, and is not a parameter at all now: what feeds the side
    /// channel is the audio-file selector's answer rather than an automatable
    /// value, and it streams beside the sample. \see issue #113.
}

TEST_CASE("A typed value is read in display units, not storage units", "[clap][text]")
{
    // The 2016 regression, named: the input gain is stored as a linear 0.001..2
    // and shown in dB, and the implementation this replaces returned the dB
    // number as the stored value. -60 is not in 0.001..2, and what a host then
    // read back was "nandB".
    Entry const entry;
    ActivePlugin plugin(48000, 512);
    auto const &params(parameters(*plugin));

    auto const inputGain(parameterID(globalType, 0));

    double value{0};
    REQUIRE(params.text_to_value(&*plugin, inputGain, "-60dB", &value));
    CHECK(value > 0.0009);
    CHECK(value < 0.0011);

    // The unit is optional, and a space before it is too.
    double withSpace{0}, without{0};
    REQUIRE(params.text_to_value(&*plugin, inputGain, "-6 dB", &withSpace));
    REQUIRE(params.text_to_value(&*plugin, inputGain, "-6", &without));
    CHECK(withSpace == without);

    // Unity gain, both ways round.
    double unity{0};
    REQUIRE(params.text_to_value(&*plugin, inputGain, "0", &unity));
    CHECK(unity > 0.999);
    CHECK(unity < 1.001);

    write(plugin, inputGain, unity);
    CHECK(displayOf(*plugin, params, inputGain) == "0.0 dB");
}

TEST_CASE("A typed value out of range is refused, not clamped", "[clap][text]")
{
    ////////////////////////////////////////////////////////////////////////////
    // Refused, because clamping answers a question nobody asked: "what value of
    // the input gain displays as 999 dB" has no answer when the gain stops at
    // +6, and a host handed the maximum has been told the text was understood --
    // so it commits the edit and a typo silently becomes a real parameter change.
    //
    // The line between "out of range" and "the maximum, printed rounded and read
    // back" is measured rather than assumed: parse() prints the bound with this
    // parameter's own printer and reads it back, and accepts anything no further
    // out than that. \see Parameters::Detail::displayRounding.
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;
    ActivePlugin plugin(48000, 512);
    auto const &params(parameters(*plugin));

    auto const inputGain(parameterID(globalType, 0));

    clap_param_info info{};
    for (auto const &candidate : allParameterInfo(*plugin, params))
        if (candidate.id == inputGain)
            info = candidate;
    REQUIRE(info.max_value > info.min_value);

    double value{-1};
    for (char const *const outOfRange : {"999 dB", "-999 dB", "inf", "-inf", "40 dB"})
    {
        INFO("'" << outOfRange << "'");
        CHECK_FALSE(params.text_to_value(&*plugin, inputGain, outOfRange, &value));
    }

    // The bounds themselves, as the plugin prints them, are inside. That is the
    // half of the contract a refusal-only rule would break: the maximum shows as
    // "6.0 dB", and 6 dB is 1.9953 -- a shade under 2.0 here, but a parameter whose
    // maximum rounds the other way would read back *past* its own maximum.
    for (double const bound : {info.min_value, info.max_value})
    {
        write(plugin, inputGain, bound);
        auto const shown(displayOf(*plugin, params, inputGain));
        INFO("bound " << bound << " shows as '" << shown << "'");
        double readBack{-1};
        REQUIRE(params.text_to_value(&*plugin, inputGain, shown.c_str(), &readBack));
        CHECK(readBack >= info.min_value);
        CHECK(readBack <= info.max_value);
    }
}

TEST_CASE("A slot selector reads back the effect a user names", "[clap][text]")
{
    Entry const entry;
    ActivePlugin plugin(48000, 512);
    auto const &params(parameters(*plugin));

    /// \note Slot 0, not slot 1. The chain is a list rather than an array -- see
    /// ModuleChainBase::module(), which walks and stops at the end -- so filling
    /// "slot 1" while slot 0 is empty puts the effect in slot 0 and leaves slot
    /// 1's selector reading "<empty>".
    auto const slotSelector(parameterID(moduleChainType, 0));

    // Empty is a value the selector holds and displays, so it has to read back.
    REQUIRE(displayOf(*plugin, params, slotSelector) == "<empty>");
    double empty{0};
    REQUIRE(params.text_to_value(&*plugin, slotSelector, "<empty>", &empty));
    CHECK(empty == -1);

    double named{0};
    REQUIRE(params.text_to_value(&*plugin, slotSelector, Effects::effectName(3), &named));
    CHECK(named == 3);

    write(plugin, slotSelector, named);
    CHECK(displayOf(*plugin, params, slotSelector) == Effects::effectName(3));

    // A title no effect has is not an effect.
    double unknown{0};
    CHECK_FALSE(params.text_to_value(&*plugin, slotSelector, "Not An Effect", &unknown));
}

TEST_CASE("value_to_text answers about the value it is asked about", "[clap][text]")
{
    ////////////////////////////////////////////////////////////////////////////
    // What a host's automation lane actually asks. This ignored its argument and
    // rendered the parameter's own value until 09.08.2026, so every tooltip over
    // every lane in every host read the same number wherever the pointer was.
    //
    // Named values rather than a sweep, because the failure this replaces was
    // not "the number is a bit off" -- it was "the number is the wrong one
    // entirely", which one contrast pins.
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;
    ActivePlugin plugin(48000, 512);
    auto const &params(parameters(*plugin));

    auto const inputGain(parameterID(globalType, 0));

    // The gain sits at unity and is asked about something else.
    REQUIRE(displayOf(*plugin, params, inputGain) == "0.0 dB");

    double minusSix{0};
    REQUIRE(params.text_to_value(&*plugin, inputGain, "-6dB", &minusSix));
    CHECK(displayOf(*plugin, params, inputGain, minusSix) == "-6.0 dB");

    // And asking did not move it.
    CHECK(displayOf(*plugin, params, inputGain) == "0.0 dB");

    // An enumerated global: the answer is a different name, not a number.
    auto const windowType(parameterID(globalType, 5));
    REQUIRE(displayOf(*plugin, params, windowType) == "Hann");
    CHECK(displayOf(*plugin, params, windowType, 2) == "Blackman");
    CHECK(displayOf(*plugin, params, windowType) == "Hann");

    // A slot selector reads as the effect the *value* names.
    auto const slotSelector(parameterID(moduleChainType, 0));
    REQUIRE(displayOf(*plugin, params, slotSelector) == "<empty>");
    CHECK(displayOf(*plugin, params, slotSelector, 3) == Effects::effectName(3));
}

TEST_CASE("Every parameter answers about a value it does not hold", "[clap][text]")
{
    ////////////////////////////////////////////////////////////////////////////
    // The whole list, and a checked build is half the point: the reason the
    // argument was ignored was that rendering it default-constructed a
    // `Parameter` to hold it, and a detached parameter with a dynamic range
    // asserts on construction. An LFO's period is one -- so is every module
    // parameter, through the LFO bounds that print in its units -- and this case
    // walks all of them with a value that is not the current one.
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;
    ActivePlugin plugin(48000, 512);
    auto const &params(parameters(*plugin));

    // Something in slot 0, so the module and LFO parameters are real ones.
    write(plugin, parameterID(moduleChainType, 0), 0);

    for (auto const &info : allParameterInfo(*plugin, params))
    {
        INFO("parameter '" << info.name << "' in '" << info.module << "'");

        double held{0};
        REQUIRE(params.get_value(&*plugin, info.id, &held));

        auto const before(displayOf(*plugin, params, info.id));

        for (double const asked :
             {info.min_value, info.max_value, (info.min_value + info.max_value) / 2})
            roundTripsUnwritten(plugin, params, info.id, asked);

        // Asking about the value it holds is the same question as asking it.
        CHECK(displayOf(*plugin, params, info.id, held) == before);
        // And none of the asking was an edit.
        CHECK(displayOf(*plugin, params, info.id) == before);
    }
}

TEST_CASE("An LFO's bounds read as the two ends of what they modulate", "[clap][text]")
{
    ////////////////////////////////////////////////////////////////////////////
    // A bound is a 0..1 position across the parameter it modulates and is shown
    // in *that* parameter's units, so its two ends have to read as that
    // parameter's two ends -- which is the one place the edge a value arrives on
    // is not the edge it is printed on. \see ParameterValueStringGetter's LFO arm.
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;
    ActivePlugin plugin(48000, 512);
    auto const &params(parameters(*plugin));

    write(plugin, parameterID(moduleChainType, 0), 0);

    std::uint32_t checked{0};
    for (auto const &info : slotParameters(*plugin, params, 0))
    {
        std::string const name(info.name);
        std::string const suffix(" - LFO Range Min");
        if (!name.ends_with(suffix))
            continue;

        // The module parameter this LFO modulates: an LFO parameter is named
        // for it. \see ParameterNameGetter's LFO arm.
        std::string const modulated(name.substr(0, name.size() - suffix.size()));
        clap_param_info target{};
        for (auto const &candidate : slotParameters(*plugin, params, 0))
        {
            if (modulated == candidate.name)
                target = candidate;
        }
        if (target.name[0] == '\0')
            continue;

        INFO("'" << name << "' modulating '" << target.name << "'");
        CHECK(displayOf(*plugin, params, info.id, 0) ==
              displayOf(*plugin, params, target.id, target.min_value));
        CHECK(displayOf(*plugin, params, info.id, 1) ==
              displayOf(*plugin, params, target.id, target.max_value));
        ++checked;
    }
    CHECK(checked > 0);
}
