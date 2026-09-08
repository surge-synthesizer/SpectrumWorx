////////////////////////////////////////////////////////////////////////////////
///
/// midiConsumersTests.cpp
/// ----------------------
///
///   Which effects consume notes, and how that is decided.
///
///   The mechanism is the point. An effect declares that it reads the note
/// status by taking it as a third setup() parameter, and that signature is the
/// only declaration of the fact -- the rule effect_contract.md §1.8 arrived at
/// for the side chain after a `usesSideChannel` constant spent ten years naming
/// seven effects the engine treated as fifteen. So what is asserted here is that
/// the *derivation* discriminates, and that the table it produces is derived
/// rather than written down.
///
/// \note The engine reaching a consumer's setup() with the notes in it is not
/// asserted here, because no shipped effect consumes them and a second
/// `if constexpr` written in a test would be exactly the second answer this
/// design exists to avoid. The true branch of the engine's dispatch is therefore
/// never instantiated -- which is a compile error the moment an effect declares
/// the third parameter, not a silent wrong answer, because the two forms are
/// mutually exclusive. \see doc/tech/midi-input.md §7.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/configuration/constants.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/effects/configuration/midiConsumingEffects.hpp"
#include "le/spectrumworx/effects/consumesMIDI.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
namespace Effects = LE::SW::Effects;
namespace Engine = LE::SW::Engine;

using Effects::IndexRange;

struct Noteless
{
    void setup(IndexRange const &, Engine::Setup const &) {}
};

struct Consumer
{
    void setup(IndexRange const &, Engine::Setup const &, Engine::MIDINoteStatus const &) {}
};

/// \note Both forms may be static -- GainImpl's are -- and a trait that saw only
/// the non-static ones would name the wrong set.
struct StaticNoteless
{
    static void setup(IndexRange const &, Engine::Setup const &) {}
};

struct StaticConsumer
{
    static void setup(IndexRange const &, Engine::Setup const &, Engine::MIDINoteStatus const &) {}
};

/// \note A third parameter of the wrong type is *not* a consumer. It is also not
/// a silent one: neither form is then invocable, so the engine's dispatch fails
/// to compile rather than selecting the noteless form and never running.
struct MistypedThirdParameter
{
    void setup(IndexRange const &, Engine::Setup const &, int) {}
};

/// \note By value *is* a consumer: a const reference binds to a value parameter
/// and the copy is correct, merely wasteful -- 256 bytes per module per frame.
/// Asserted rather than forbidden because the contract is "takes the status",
/// not "spells it the way the engine hands it over", and a concept that matched
/// one exact signature would reject the static and by-value forms an effect is
/// otherwise free to use.
struct ByValue
{
    void setup(IndexRange const &, Engine::Setup const &, Engine::MIDINoteStatus) {}
};

std::vector<std::string_view> consumerNames()
{
    std::vector<std::string_view> names;
    for (std::uint8_t index(0); index < Effects::Constants::numberOfEffects; ++index)
        if (Effects::effectConsumesMIDI[index])
            names.emplace_back(Effects::effectName(index));
    return names;
}
} // anonymous namespace

TEST_CASE("The setup() signature is what declares a note consumer", "[effects][midi]")
{
    static_assert(!Effects::ConsumesMIDI<Noteless>);
    static_assert(Effects::ConsumesMIDI<Consumer>);

    static_assert(!Effects::ConsumesMIDI<StaticNoteless>);
    static_assert(Effects::ConsumesMIDI<StaticConsumer>);

    static_assert(!Effects::ConsumesMIDI<MistypedThirdParameter>);
    static_assert(Effects::ConsumesMIDI<ByValue>);
}

TEST_CASE("The consumer table covers every effect", "[effects][midi]")
{
    static_assert(Effects::effectConsumesMIDI.size() == Effects::Constants::numberOfEffects);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The count is spelt out rather than derived from the same array the
/// production code reads, because a test that recomputed it would agree with
/// itself whatever the effects did. Changing it is the deliberate act of
/// admitting a new consumer, and the failure names which effect grew one.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("No shipped effect consumes notes yet", "[effects][midi]")
{
    auto const names(consumerNames());

    std::string listed;
    for (auto const &name : names)
    {
        if (!listed.empty())
            listed += ", ";
        listed += name;
    }
    INFO("consumers: " << listed);

    CHECK(names.empty());
    CHECK(Effects::numberOfMIDIConsumingEffects() == 0);
}
