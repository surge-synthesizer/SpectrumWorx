////////////////////////////////////////////////////////////////////////////////
///
/// \file presetRoundTripTests.cpp
/// ------------------------------
///
///   Save a preset, load it back, and require that nothing moved.
///
///   The other half of stage 8's "done when": the corpus test proves the reader
/// still reads what the 2016 writer wrote, and this proves the writer and the
/// reader still agree with each other. Neither implies the other, and 8.1
/// replaces both at once.
///
///   Every effect in turn, with every parameter driven off its default first --
/// a round-trip of defaults is the one that passes even when the format writes
/// nothing at all. LFOs go on too: they are a nested element rather than an
/// attribute, and the saver deliberately omits any LFO value that is still at
/// its default, so "what was left out" is part of what has to round-trip.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "presets/presetHarness.hpp"
#include "utility/localeHarness.hpp"

#include "core/modules/factory.hpp"

/// \note Not sorted with the block above: finalImplementations names
/// GUI::ModuleUI, which moduleDSPAndGUI is what defines.
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/modules/finalImplementations.hpp"

#include "le/parameters/lfoImpl.hpp"
#include "le/parameters/runtimeInformation.hpp"
#include "le/spectrumworx/effects/configuration/constants.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/engine/moduleParameters.hpp"
#include "le/spectrumworx/presetStorage.hpp"
#include "le/spectrumworx/presets.hpp"

#include <filesystem>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace LE;
using namespace LE::SW;

namespace Effects = LE::SW::Effects;

using ModuleParameters = LE::SW::Engine::ModuleParameters;
using LFO = LE::Parameters::LFOImpl;

using SWTest::dump;
using SWTest::PresetConsumer;
using SWTest::ScopedProblemCounter;

/// \note `generousBuffer`, a 1 MiB `std::vector<char>` every save was handed,
/// stood here. It was needed because `savePreset` wrote into a caller's span and
/// the shipping caller's was 4096 bytes -- which five TuneWorx modules breach,
/// as the 2016 sources record. `savePreset` returns a `std::string` now, so
/// there is no size to be generous about and no path left where a preset is too
/// large to save.
///                                           (02.08.2026.) (SW port)

/// \brief The first `<p n="…">` anywhere under \p element, or null.
TiXmlElement const *findParameter(TiXmlElement const &element, std::string_view const parameterName)
{
    for (auto const *pChild(element.FirstChildElement()); pChild;
         pChild = pChild->NextSiblingElement())
    {
        auto const *const pName(pChild->Attribute("n"));
        if (pName && (std::string_view(pChild->Value()) == "p") &&
            (std::string_view(pName) == parameterName))
            return pChild;
        if (auto const *const pFound = findParameter(*pChild, parameterName))
            return pFound;
    }
    return nullptr;
}

/// \brief The value 3.0 recorded for \p parameterName, read back out of the
/// document rather than matched as text.
///
/// \note Depth-first from the root and the first match wins, which is what these
/// single-module fixtures want. The globals come first in the document and share
/// no name with a module parameter -- In, Out, Mix, FFT size, Overlap factor,
/// Window type against Gain, Wet and the rest -- so there is nothing here to
/// disambiguate yet. A two-module preset would need the module element named.
float savedParameterValue(std::string const &saved, char const *const parameterName)
{
    TiXmlDocument document;
    document.Parse(saved.c_str());
    REQUIRE_FALSE(document.Error());

    auto const *const pRoot(document.RootElement());
    REQUIRE(pRoot != nullptr);

    auto const *const pParameter(findParameter(*pRoot, parameterName));
    REQUIRE(pParameter != nullptr);

    double value{0};
    REQUIRE(pParameter->QueryDoubleAttribute("v", &value) == TIXML_SUCCESS);
    return static_cast<float>(value);
}

/// \note A class rather than a function returning one: SWTest::Engine points
/// SpectrumWorxCore at a Program it holds by value, so it can be neither copied
/// nor moved without the pointer dangling.
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

    SWTest::Engine &engine() { return engine_; }

  private:
    SWTest::Engine engine_;
}; // class Fixture

//------------------------------------------------------------------------------
// Driving everything off its default
//------------------------------------------------------------------------------

/// \brief A value inside \p info's range that is not its default.
///
/// \note Discrete parameters step to the neighbour rather than to a fraction of
/// the range: a boolean has two values and 37% of the way along is not one of
/// them, and an enumerated parameter that quantises back to its default would
/// make this test pass without having tested anything.
float offDefault(Parameters::RuntimeInformation const &info)
{
    using Type = Parameters::RuntimeInformation::Type;

    switch (info.type)
    {
    case Type::Boolean:
    case Type::Enumerated:
    case Type::Integer:
    {
        auto const stepUp(info.default_ + 1);
        return (stepUp <= info.maximum) ? stepUp : (info.default_ - 1);
    }

    case Type::FloatingPoint:
    {
        /// \note Snapped to four decimals, which is all the format carries --
        /// the 2016 writer's floats are "1.0000" and "100.0000", and
        /// lexical_cast still writes exactly that. Phasevolution's Period was
        /// the one parameter whose 0.371-of-range landed on 1.85563 and came
        /// back 1.8556: the format's resolution, not a fault. A test value the
        /// format cannot hold would test the rounding rather than the trip.
        auto const snap([](float const value) { return std::round(value * 1.0e4f) / 1.0e4f; });

        auto const candidate(snap(info.minimum + 0.371f * (info.maximum - info.minimum)));
        return (candidate != info.default_)
                   ? candidate
                   : snap(info.minimum + 0.617f * (info.maximum - info.minimum));
    }

    /// \note Left alone. A trigger consumes its value when read, so "set it and
    /// see whether it survives a save" has no meaning for one.
    case Type::Trigger:
        break;
    }
    return info.default_;
}

/// \brief Sets an LFO parameter to \p fraction of the way along its own range.
///
/// \note Off the parameter's declared range rather than off a number chosen
/// here. Guessing produced two asserts in a row: the LFO's Phase is a fraction
/// of a period over +/- 0.5, and the global gains are linear multipliers over
/// 0.001 .. 2, so "-40 degrees" and "-3.5 dB" are both simply out of range. The
/// range is right there in the traits; ask it.
template <class Parameter> void setFraction(LFO &lfo, float const fraction)
{
    constexpr auto minimum(static_cast<float>(Parameter::unscaledMinimum) /
                           Parameter::rangeValuesDenominator);
    constexpr auto maximum(static_cast<float>(Parameter::unscaledMaximum) /
                           Parameter::rangeValuesDenominator);

    lfo.parameters().template get<Parameter>().setValue(
        static_cast<typename Parameter::param_type>(minimum + fraction * (maximum - minimum)));
}

/// \note SyncTypes is left alone on purpose: the saver writes it
/// unconditionally *because* leaving it out makes a preset read as
/// pre-synced-LFO, and PeriodScale is snapped against it on the way back in, so
/// changing either here would test the snapping rather than the round-trip.
///
/// \note `enabled` defaults to false, and the settings still round-trip: an LFO
/// is written as a nested element with the parameter's value as its text, and
/// the saver omits any LFO value still at its default -- so what is being
/// exercised is the element path and the omissions, not whether the LFO runs.
/// See the second test case for what enabling one does.
void driveLFO(LFO &lfo, unsigned int const seed, bool const enabled = false)
{
    lfo.parameters().get<LFO::Enabled>().setValue(enabled);
    setFraction<LFO::Phase>(lfo, 0.25f + 0.05f * static_cast<float>(seed % 5));
    setFraction<LFO::LowerBound>(lfo, 0.125f);
    setFraction<LFO::UpperBound>(lfo, 0.875f);
    setFraction<LFO::Waveform>(lfo, 0.5f);
}

void driveModule(ModuleParameters &module, unsigned int const seed)
{
    auto const baseParameters(ModuleParameters::numberOfBaseParameters);

    module.setBaseParameter(0, 1.0f); // Bypass

    for (std::uint8_t index(1); index < module.numberOfParameters(); ++index)
    {
        auto const value(offDefault(module.parameterInfo(index)));
        if (index < baseParameters)
            module.setBaseParameter(index, value);
        else
            module.setEffectParameter(static_cast<std::uint8_t>(index - baseParameters), value);
    }

    /// \note Every other LFO-able parameter, not all of them: a preset with an
    /// LFO on all forty of TuneWorx's parameters is a large document, and what
    /// is being tested is that an LFO survives beside a parameter that has none.
    for (std::uint8_t index(0); index < module.numberOfLFOControledParameters(); index += 2)
        driveLFO(module.lfo(index), seed + index);
}

/// \note Globals too. The FFT size and overlap factor reconfigure the engine
/// rather than only moving a number, so a preset that carries them is the case
/// where the loader has to go through setGlobalParameter -- and the one where
/// getting it wrong is silent.
/// \note The gains are linear multipliers over 0.001 .. 2.0 and the mix is
/// 0 .. 1, not decibels and not a percentage -- whatever their names say. The
/// committed presets agree: `In="1.0000" Out="1.0000" Mix="1.0000"` is unity.
void driveGlobals(SWTest::Engine &engine)
{
    using namespace LE::SW::GlobalParameters;
    engine.set<FFTSize>(1024);
    engine.set<OverlapFactor>(8);
    engine.set<InputGain>(0.7f);
    engine.set<OutputGain>(1.5f);
    engine.set<MixPercentage>(0.625f);
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("A saved preset loads back as itself", "[preset-roundtrip]")
{
    for (std::uint8_t effect(0); effect < Effects::Constants::numberOfEffects; ++effect)
    {
        INFO("effect " << unsigned(effect) << " (" << Effects::effectName(effect) << ')');

        std::string saved;
        std::string original;
        {
            Fixture fixture;
            auto &engine(fixture.engine());
            driveGlobals(engine);

            REQUIRE(
                engine.program()
                    .moduleChain()
                    .setParameter(0, static_cast<std::int8_t>(effect), engine.moduleInitialiser())
                    .second == static_cast<std::int8_t>(effect));

            engine.program().moduleChain().forEach<ModuleParameters>(
                [&](ModuleParameters const &module) {
                    driveModule(const_cast<ModuleParameters &>(module), effect);
                });

            original = dump(engine).text;

            saved = savePreset({}, engine.sideChainSource(), "round trip", engine.program());
            REQUIRE_FALSE(saved.empty());
        }

        INFO("saved preset:\n" << saved);

        Fixture reloaded;
        auto &engine(reloaded.engine());
        std::vector<char> parseBuffer(saved.begin(), saved.end());
        parseBuffer.push_back('\0'); // the parse is destructive and wants a terminator

        SWTest::clearPresetProblems();
        {
            ScopedProblemCounter const counting;
            REQUIRE(LE::SW::loadPreset(parseBuffer.data(), true, nullptr, PresetConsumer{engine}));
        }

        /// \note A preset this build wrote a moment ago must not be missing a
        /// parameter this build knows about. This is what named the TuneWorx
        /// problem: twenty one of its parameters serialised under the
        /// placeholder name "N/A", so it wrote twenty one `<N/A>` elements --
        /// not a legal XML name -- and reading one back failed the parse.
        CHECK(SWTest::presetProblems().missingParameter == 0);
        CHECK(SWTest::presetProblems().unknownEffect == 0);

        CHECK(dump(engine).text == original);
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// An enabled LFO
// --------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note This is the one thing a preset deliberately does *not* restore, and it
/// took a failing round-trip to notice. The saver writes an LFO-controlled
/// parameter's value as the element's text; the loader
/// (ParametersLoader::getLFOParameterValue) reads the LFO first and, if it comes
/// back enabled, returns nothing rather than the value -- because the LFO is
/// what drives that parameter from now on, so the stored value is a snapshot of
/// a moving thing.
///
///   Pinned here rather than worked around silently, because it is exactly the
/// sort of asymmetry a parser rewrite can lose in either direction: stop writing
/// the value, or start applying it.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Saving while an LFO is running stores the setting, not the sweep",
          "[preset-roundtrip][lfo]")
{
    // The concrete cost of there having been no unmodulated value:
    // savePresetParameters() wrote getBaseParameter(), which the LFO overwrote
    // every block, so a preset saved with an LFO running froze that LFO's
    // instantaneous output as the parameter's value -- and loading it back read
    // that as the user's setting. Revert the unmodulated write in
    // ModuleParameters::setBaseParameter and this goes red.
    constexpr std::uint8_t gain{1}; // the first base parameter with an LFO
    constexpr std::uint32_t blockSize{512};
    constexpr std::uint8_t channels{2};

    Fixture fixture;
    auto &engine(fixture.engine());

    REQUIRE(engine.program().moduleChain().setParameter(0, 0, engine.moduleInitialiser()).second ==
            0);

    float setValue{0};
    engine.program().moduleChain().forEach<ModuleParameters>(
        [&](ModuleParameters const &constModule) {
            auto &module(const_cast<ModuleParameters &>(constModule));
            setValue = module.setBaseParameter(gain, offDefault(module.parameterInfo(gain)));
            driveLFO(module.baseLFO(gain - 1), 0, true /*enabled*/);
        });
    REQUIRE(setValue != 0);

    // Enough blocks for the LFO clock to move well off position zero.
    std::vector<std::vector<float>> input(channels, std::vector<float>(blockSize, 0.0f));
    std::vector<std::vector<float>> output(channels, std::vector<float>(blockSize, 0.0f));
    std::vector<float const *> inputPointers{input[0].data(), input[1].data()};
    std::vector<float *> outputPointers{output[0].data(), output[1].data()};
    for (unsigned block(0); block < 64; ++block)
        engine.process(inputPointers.data(), inputPointers.data(), outputPointers.data(), 1.0f,
                       blockSize);

    // The LFO really did move the live parameter -- otherwise the case below
    // would pass for the wrong reason.
    float liveValue{0};
    engine.program().moduleChain().forEach<ModuleParameters>(
        [&](ModuleParameters const &module) { liveValue = module.getBaseParameter(gain); });
    REQUIRE(liveValue != setValue);

    auto const saved(savePreset({}, engine.sideChainSource(), "lfo running", engine.program()));
    REQUIRE_FALSE(saved.empty());

    INFO("saved preset:\n" << saved);
    CHECK(savedParameterValue(saved, "Gain") == Catch::Approx(setValue));
}

TEST_CASE("A parameter under an enabled LFO takes its value from the LFO", "[preset-roundtrip]")
{
    /// Gain is base parameter 1, the first with an LFO of its own.
    constexpr std::uint8_t gain{1};

    std::string saved;
    float drivenValue{0};
    {
        Fixture fixture;
        auto &engine(fixture.engine());

        REQUIRE(
            engine.program().moduleChain().setParameter(0, 0, engine.moduleInitialiser()).second ==
            0);

        engine.program().moduleChain().forEach<ModuleParameters>(
            [&](ModuleParameters const &constModule) {
                auto &module(const_cast<ModuleParameters &>(constModule));
                drivenValue = module.setBaseParameter(gain, offDefault(module.parameterInfo(gain)));
                driveLFO(module.baseLFO(gain - 1), 0, true /*enabled*/);
            });

        REQUIRE(drivenValue != 0);

        saved = savePreset({}, engine.sideChainSource(), "lfo", engine.program());
        REQUIRE_FALSE(saved.empty());
    }

    /// The value is in the file -- it is what is not applied.
    ///
    /// \note Read out of the document rather than searched for as a substring.
    /// It was `saved.find( SWTest::number( drivenValue ) )`, which asked the file
    /// to spell the number the way the *dump* spells it -- six significant
    /// figures -- and 3.0 writes nine, so `-5.16` became `-5.15999985` and the
    /// search stopped finding anything. Comparing the parsed value asks the
    /// question the test means and survives the next change of precision too.
    INFO("saved preset:\n" << saved);
    CHECK(savedParameterValue(saved, "Gain") == Catch::Approx(drivenValue));

    Fixture reloaded;
    auto &engine(reloaded.engine());
    std::vector<char> parseBuffer(saved.begin(), saved.end());
    parseBuffer.push_back('\0');

    SWTest::clearPresetProblems();
    {
        ScopedProblemCounter const counting;
        REQUIRE(LE::SW::loadPreset(parseBuffer.data(), true, nullptr, PresetConsumer{engine}));
    }

    engine.program().moduleChain().forEach<ModuleParameters>([&](ModuleParameters const &module) {
        CHECK(module.getBaseParameter(gain) == module.parameterInfo(gain).default_);
        CHECK(module.baseLFO(gain - 1).enabled());
        // ...while the LFO's own settings did come back.
        CHECK(module.baseLFO(gain - 1).parameters().get<LFO::LowerBound>().getValue() == 0.125f);
        CHECK(module.baseLFO(gain - 1).parameters().get<LFO::UpperBound>().getValue() == 0.875f);
    });
}

////////////////////////////////////////////////////////////////////////////////
//
// Through a file
// --------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note Stage 8's other "done when" is that a user's save and load round-trips.
/// Everything above round-trips through a buffer; this is the layer under it --
/// writePresetFile() and readPresetFile() -- and nothing else covers it.
/// The bug it exists to catch is a truncated or unterminated write, which the
/// buffer path cannot have.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A preset saved to a file loads back from it", "[preset-roundtrip]")
{
    std::filesystem::path const directory(std::filesystem::path(SW_TEST_OUTPUT_DIR) / "presets");
    std::filesystem::create_directories(directory);
    REQUIRE(std::filesystem::is_directory(directory));

    /// \note A name with a space and a parenthesis in it, because those are what
    /// the shipped banks are full of and what CMakeRC's unescaped command line
    /// tripped over.
    auto const file(directory / "round trip (1).swp");
    std::filesystem::remove(file);

    std::string original;
    {
        Fixture fixture;
        auto &engine(fixture.engine());
        driveGlobals(engine);

        REQUIRE(
            engine.program().moduleChain().setParameter(0, 3, engine.moduleInitialiser()).second ==
            3);

        engine.program().moduleChain().forEach<ModuleParameters>(
            [&](ModuleParameters const &module) {
                driveModule(const_cast<ModuleParameters &>(module), 3);
            });

        original = dump(engine).text;

        auto const preset(
            savePreset({}, engine.sideChainSource(), "through a file", engine.program()));
        REQUIRE(
            writePresetFile(file, preset.c_str(), static_cast<unsigned int>(preset.size() + 1)));
    }

    REQUIRE(std::filesystem::is_regular_file(file));
    REQUIRE(std::filesystem::file_size(file) > 0);

    auto const contents(readPresetFile(file));
    REQUIRE(contents);

    Fixture reloaded;
    auto &engine(reloaded.engine());

    SWTest::clearPresetProblems();
    {
        ScopedProblemCounter const counting;
        REQUIRE(LE::SW::loadPreset(contents.get(), true, nullptr, PresetConsumer{engine}));
    }
    CHECK(SWTest::presetProblems().missingParameter == 0);
    CHECK(dump(engine).text == original);

    /// \note And the comment, which is the one thing in a preset that is neither
    /// a parameter nor an effect and so is in none of the dumps above.
    std::string comment;
    Fixture commentOnly;
    REQUIRE(LE::SW::loadPreset(readPresetFile(file).get(), true, &comment,
                               PresetConsumer{commentOnly.engine()}));
    CHECK(comment == "through a file");

    std::filesystem::remove(file);
}

////////////////////////////////////////////////////////////////////////////////
//
// The host's locale
// -----------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The writing half of what presetFileTests.cpp checks on the reading
/// side, and the half that leaves the damage on disk. A comma-decimal host made
/// the writer spell every fraction with a comma -- `In="0,7000"` -- so the file
/// was unreadable by the same plugin on any other machine, and unreadable by
/// this one after the host's locale changed.
///
///   Saved under the locale and read back *outside* it, because a writer and a
/// reader that are wrong in the same direction agree with each other. The
/// comparison that catches this is against text produced by neither.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A preset saved under the host's locale is a preset anywhere else",
          "[preset-roundtrip][locale]")
{
    constexpr std::int8_t effect{3};

    std::string original;
    std::string saved;
    {
        SWTest::CommaDecimalHost const host;
        if (!host)
            SKIP("No comma-decimal locale is installed on this machine.");

        Fixture fixture;
        auto &engine(fixture.engine());
        driveGlobals(engine);

        REQUIRE(engine.program()
                    .moduleChain()
                    .setParameter(0, effect, engine.moduleInitialiser())
                    .second == effect);

        engine.program().moduleChain().forEach<ModuleParameters>(
            [&](ModuleParameters const &module) {
                driveModule(const_cast<ModuleParameters &>(module), effect);
            });

        original = dump(engine).text;
        saved = savePreset({}, engine.sideChainSource(), "written abroad", engine.program());
        REQUIRE_FALSE(saved.empty());
    }

    INFO("saved preset:\n" << saved);

    /// The direct statement of it, before any of it is parsed.
    CHECK(saved.find(',') == std::string::npos);

    Fixture reloaded;
    auto &engine(reloaded.engine());
    std::vector<char> parseBuffer(saved.begin(), saved.end());
    parseBuffer.push_back('\0');

    SWTest::clearPresetProblems();
    {
        ScopedProblemCounter const counting;
        REQUIRE(LE::SW::loadPreset(parseBuffer.data(), true, nullptr, PresetConsumer{engine}));
    }
    CHECK(SWTest::presetProblems().missingParameter == 0);
    CHECK(dump(engine).text == original);
}

////////////////////////////////////////////////////////////////////////////////
//
// The committed 3.0 fixture
// -------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   Everything above writes a preset and reads it back, so the writer and the
/// reader agree by construction -- including if they are both wrong in the same
/// direction. A change that renamed `<p>` to `<param>` on both sides would pass
/// every one of them and orphan every file already saved.
///
///   So: a file, hand written, never regenerated, read by a test that does not
/// run the writer at all. If this fails, the grammar moved.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The committed 3.0 fixture loads without the writer's help", "[preset-roundtrip]")
{
    std::filesystem::path const fixtureFile(std::filesystem::path(SW_PRESET_SNAPSHOT_DIR) /
                                            "format3.swp");
    REQUIRE(std::filesystem::is_regular_file(fixtureFile));

    auto const contents(readPresetFile(fixtureFile));
    REQUIRE(contents);

    Fixture fixture;
    auto &engine(fixture.engine());

    std::string comment;
    SWTest::clearPresetProblems();
    {
        ScopedProblemCounter const counting;
        REQUIRE(LE::SW::loadPreset(contents.get(), true, &comment, PresetConsumer{engine}));
    }
    CHECK(SWTest::presetProblems().total() == 0);
    CHECK(std::string_view(comment).starts_with("A hand-written 3.0 fixture"));

    /// \note The globals, read off the `<Global>` block. `MixPercentage` runs
    /// 0..1, not 0..100 -- the name and the "%" unit are both about how it is
    /// *displayed*. Worth stating because the fixture was written with 75 in it
    /// first and this test is how that was found out.
    auto const &globals(engine.program().parameters());
    CHECK(globals.get<GlobalParameters::InputGain>().getValue() == Catch::Approx(0.5f));
    CHECK(globals.get<GlobalParameters::OutputGain>().getValue() == Catch::Approx(1.25f));
    CHECK(globals.get<GlobalParameters::MixPercentage>().getValue() == Catch::Approx(0.75f));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The side chain's source, and the fixture is built so that only one
    /// reading of it passes. `main` is not the default -- that is `host` -- so a
    /// build that failed to find the element would answer `host`. And the fixture
    /// *also* carries `Input mode="1"`, which migrates to `host`, so a build that
    /// preferred the legacy key over the recorded source would answer `host` too.
    /// `main` is only reachable by reading `Side chain source` and letting it win.
    /// \see issue #113 and doc/tech/sidechain-approach.md.
    ///
    ////////////////////////////////////////////////////////////////////////////
    CHECK(engine.sideChainSource() == SideChainSource::Main);

    /// Two modules, in document order, named by their streaming names.
    std::vector<std::string> effects;
    std::vector<float> gains;
    float strength{0};
    bool lfoEnabled{false};
    engine.program().moduleChain().forEach<ModuleParameters>([&](ModuleParameters const &module) {
        effects.emplace_back(Effects::effectStreamingName(module.effectTypeIndex()));
        gains.push_back(module.getBaseParameter(1 /*Gain*/));
        if (module.numberOfParameters() > 7)
        {
            lfoEnabled |= module.lfo(5 - ModuleParameters::numberOfNonLFOBaseParameters).enabled();
            strength = module.getEffectParameter(2 /*Strength*/);
        }
    });

    REQUIRE(effects.size() == 2);
    CHECK(effects[0] == "Pitch Shifter");
    CHECK(effects[1] == "Ah-ah");
    CHECK(gains[0] == Catch::Approx(-6.5f));

    /// \note Ah-ah's Strength is 9999 in the fixture and its range is +/-24 dB,
    /// so what comes back is the default. A value outside a parameter's range is
    /// dropped silently -- not clamped, not reported, and not counted as a
    /// missing parameter, because the file did name it. Pinned here because
    /// nothing else drives that branch and because "silently" is the part worth
    /// knowing.
    CHECK(strength == Catch::Approx(0.0f));

    /// \note The second module's Center carries a full LFO attribute set, in the
    /// same seven attributes 2.x used and on the `<p>` element rather than
    /// beside it. That the LFO comes back enabled is what says the writer's
    /// change of element did not quietly cost the reader its settings.
    CHECK(lfoEnabled);

    /// \note And `<dawExtraState>` is in the fixture with an element inside it
    /// that means nothing here. A preset reader installs no hook, so it must
    /// walk straight past -- neither reading it nor treating it as a module.
    CHECK(effects.size() == 2);
}
