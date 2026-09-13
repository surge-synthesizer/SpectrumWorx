////////////////////////////////////////////////////////////////////////////////
///
/// \file parameterLayoutTests.cpp
/// ------------------------------
///
///   How a Parameters container is laid out is a contract, and until stage 7.2
/// moved the container into a template nothing checked it.
///
///   ModuleDSP::getEffectParameterPtr() reaches a parameter by adding a
/// std::uint8_t offset to the module, and opens with
/// LE_ASSERT( pParameterOffsets_[ 0 ] == 0 ) -- an assert that does not survive
/// a release build. Two properties keep it true,
/// and neither is visible in the parameter table snapshot or in any golden,
/// because both would fail as silently as reading the wrong bytes:
///
///   - the first parameter is at offset zero. std::tuple storage would break
///     this on libc++, which lays its elements out in reverse.
///   - the parameters are laid out flat, so the offsets fit in a std::uint8_t.
///     Tune Worx declares two dozen booleans; a recursively nested holder would
///     align each of them to the widest parameter still to come and turn
///     twenty-four bytes into ninety-six.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "goldens/engineHarness.hpp"

#include "core/modules/factory.hpp"

/// \note Not sorted with the block above, and it matters: finalImplementations
/// names GUI::ModuleUI, which moduleDSPAndGUI is what defines.
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/modules/finalImplementations.hpp"

#include "le/parameters/parameterList.hpp"
#include "le/parameters/runtimeInformation.hpp"
#include "le/spectrumworx/effects/configuration/constants.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/effects/octaver/octaver.hpp"
#include "le/spectrumworx/engine/moduleParameters.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace LE::SW;

/// Stand-ins for the two parameter shapes that decide a container's layout: a
/// byte and a word. ParameterList only ever needs the type to be distinct.
template <int n> struct Flag
{
    bool value_;
};
template <int n> struct Real
{
    float value_;
};

template <class List, class Parameter> std::ptrdiff_t offsetOf(List const &list)
{
    return reinterpret_cast<char const *>(&list.template get<Parameter>()) -
           reinterpret_cast<char const *>(&list);
}

/// \brief An initialised engine with a Program, the same shape the goldens and
/// parameterTableTests use -- SpectrumWorxCore cannot be instantiated alone.
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

    Engine::ModuleParameters &moduleInSlot(std::uint8_t const slot)
    {
        Engine::ModuleParameters *pFound{nullptr};
        std::uint8_t index{0};
        engine_.program().moduleChain().forEach<Engine::ModuleParameters>(
            [&](Engine::ModuleParameters const &module) {
                if (index++ == slot)
                    pFound = &const_cast<Engine::ModuleParameters &>(module);
            });
        REQUIRE(pFound != nullptr);
        return *pFound;
    }

  private:
    SWTest::Engine engine_;
}; // class Fixture

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("The first parameter of a list is the list", "[parameters][layout]")
{
    LE::Parameters::ParameterList<Real<0>, Real<1>, Flag<0>> const list{};
    CHECK(offsetOf<decltype(list), Real<0>>(list) == 0);
}

TEST_CASE("A run of byte-sized parameters stays a run of bytes", "[parameters][layout]")
{
    // The Tune Worx shape: a word, two dozen booleans, and a word after them --
    // the arrangement that tells a flat layout apart from a nested one.
    using List = LE::Parameters::ParameterList<
        Real<0>, Flag<0>, Flag<1>, Flag<2>, Flag<3>, Flag<4>, Flag<5>, Flag<6>, Flag<7>, Flag<8>,
        Flag<9>, Flag<10>, Flag<11>, Flag<12>, Flag<13>, Flag<14>, Flag<15>, Flag<16>, Flag<17>,
        Flag<18>, Flag<19>, Flag<20>, Flag<21>, Flag<22>, Flag<23>, Real<1>>;
    List const list{};

    CHECK(offsetOf<List, Real<0>>(list) == 0);

    // Every boolean one byte after the last, starting right after the word.
    CHECK(offsetOf<List, Flag<0>>(list) == 4);
    CHECK(offsetOf<List, Flag<23>>(list) == 4 + 23);

    // A nested holder would give each of those booleans a word: 4 + 24 * 4 + 4.
    CHECK(sizeof(List) < 100u);
    CHECK(offsetOf<List, Real<1>>(list) < 256);
}

TEST_CASE("IndexOf answers for a parameter that is not in the list", "[parameters][layout]")
{
    using List = LE::Parameters::ParameterList<Real<0>, Flag<0>>;
    CHECK(List::IndexOf<Real<0>>::value == 0);
    CHECK(List::IndexOf<Flag<0>>::value == 1);
    CHECK(List::IndexOf<Real<9>>::value == List::static_size);
}

TEST_CASE("Every effect parameter has storage of its own", "[parameters][layout]")
{
    ///   Writes one parameter through the offset table and reads every parameter
    /// back through it. An offset that is wrong by any amount lands on another
    /// parameter, on the padding between two, or outside the container -- and
    /// the first of those is the one no other test would notice.
    Fixture fixture;

    for (std::uint8_t effect(0); effect < Effects::Constants::numberOfEffects; ++effect)
    {
        REQUIRE(fixture.insert(0, static_cast<std::int8_t>(effect)) ==
                static_cast<std::int8_t>(effect));

        auto &module(fixture.moduleInSlot(0));
        INFO("effect " << Effects::effectName(effect));

        ///   Trigger parameters are left out of this. They are momentary rather
        /// than stored -- Freeze's two and the Convolver's Grab IR do not read
        /// back what was written to them, by design -- so they answer no
        /// question about where a parameter lives. Everything else does.
        std::vector<std::uint8_t> stored;
        for (std::uint8_t index(0); index < module.numberOfEffectSpecificParameters(); ++index)
            if (module.effectSpecificParameterInfo(index).type !=
                LE::Parameters::RuntimeInformation::Trigger)
                stored.push_back(index);

        std::vector<float> baseline;
        for (auto const index : stored)
            baseline.push_back(module.getEffectParameter(index));

        for (std::size_t which(0); which < stored.size(); ++which)
        {
            auto const index(stored[which]);
            auto const &info(module.effectSpecificParameterInfo(index));
            if (info.minimum == info.maximum)
                continue; // nothing else this one can be set to

            auto const other((baseline[which] == info.minimum) ? info.maximum : info.minimum);
            INFO("parameter " << static_cast<unsigned>(index) << " (" << info.name << ") set to "
                              << other);
            module.setEffectParameter(index, other);

            CHECK(module.getEffectParameter(index) != baseline[which]);
            for (std::size_t bystander(0); bystander < stored.size(); ++bystander)
            {
                if (bystander == which)
                    continue;
                INFO("bystander " << static_cast<unsigned>(stored[bystander]) << " ("
                                  << module.effectSpecificParameterInfo(stored[bystander]).name
                                  << ")");
                CHECK(module.getEffectParameter(stored[bystander]) == baseline[bystander]);
            }

            module.setEffectParameter(index, baseline[which]);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Issue #163. An enumerated parameter's default was the constant zero --
/// `EnumeratedParameterTraits::default_()` returned a literal -- so the only
/// value one could rest at was whichever happened to be declared first.
///
/// \note Three claims, and the third is the one worth having: a parameter given
/// a default rests at it, one not given one still rests at its first value, and
/// the default a *host* is told is the same number. `clap_param_info` reads
/// `default_()` through the same traits, so a mechanism that moved the object
/// and not the metadata would show up as a DAW's "reset to default" disagreeing
/// with the plugin's own.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("An enumerated parameter can rest at a value other than its first",
          "[parameters][layout]")
{
    using LE::SW::Effects::Octaver;

    // The one the issue asked for: two octaves that are not the same octave, the
    // nearer one first.
    CHECK(Octaver::Octave1().getValue() == Octaver::Octave1::Down1);
    CHECK(Octaver::Octave2().getValue() == Octaver::Octave2::Down2);

    // Which is the *default*, not merely what a fresh object happens to hold.
    CHECK(Octaver::Octave1::default_() == Octaver::Octave1::Down1);
    CHECK(Octaver::Octave2::default_() == Octaver::Octave2::Down2);

    // And the plain macro is unchanged: the first value, as it always was.
    CHECK(Octaver::Octave2::default_() == Octaver::Octave2::minimum());

    /// \note Neither of them moved its range, which is the mistake this shape
    /// avoids: the default is a third template argument rather than a shifted
    /// minimum, so what a preset's stored value means does not move with it.
    CHECK(Octaver::Octave1::minimum() == 0);
    CHECK(Octaver::Octave1::maximum() == 4);
}
