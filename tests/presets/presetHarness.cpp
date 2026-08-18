////////////////////////////////////////////////////////////////////////////////
///
/// presetHarness.cpp
/// -----------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "presetHarness.hpp"

#include "core/modules/factory.hpp"

/// \note Not sorted with the block above: finalImplementations names
/// GUI::ModuleUI, which moduleDSPAndGUI is what defines.
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/modules/finalImplementations.hpp"

#include "le/parameters/lfoImpl.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/engine/moduleParameters.hpp"

#include <array>
#include <cstdio>
#include <iomanip>
#include <locale>
#include <sstream>

namespace SWTest
{

using namespace LE;
using namespace LE::SW;

namespace Effects = LE::SW::Effects;

namespace
{
//------------------------------------------------------------------------------

/// \brief Pushes a whole GlobalParameters::Parameters through the engine's own
/// setter, one parameter at a time.
///
/// \note Not the same as assigning the struct: FFT size, overlap factor and
/// window function each reconfigure the engine, so a preset that changes one has
/// to go through setGlobalParameter rather than land in the field behind its
/// back.
struct GlobalParameterUpdater
{
    using result_type = void;

    SpectrumWorxCore &core;

    template <class Parameter> result_type operator()(Parameter const &parameter) const
    {
        LE_VERIFY((SpectrumWorxCore::setGlobalParameter<Parameter, SpectrumWorxCore>(
            core, parameter.getValue())));
    }
}; // struct GlobalParameterUpdater

/// \note Every LFO parameter, unconditionally, rather than only the enabled
/// ones. The saver writes only non-default LFO values (to keep a five-module
/// preset under the 4096 byte limit) and the loader resets the rest to their
/// defaults -- so "what a preset does not say" is as much part of the loaded
/// state as what it does, and a parser that dropped an attribute shows up here
/// as a default appearing.
struct LFODumper
{
    using result_type = void;

    std::string &out;

    template <class Parameter> result_type operator()(Parameter const &parameter) const
    {
        out += ' ';
        out += LE::Parameters::streamingName<Parameter>();
        out += '=';
        out += number(static_cast<float>(parameter.getValue()));
    }
}; // struct LFODumper

/// \note Streaming names here and in LFODumper, for the reason dumpModule()
/// gives at length: what this hashes has to move when a preset loads
/// differently and stay put when a label is retitled. The globals are where it
/// bites -- they are the only parameters whose display name and streaming name
/// differ -- so relabelling `FFT size` to `FFT Size` moved all eight fixture
/// digests while every preset went on loading identically.
///                                       (18.08.2026.) (SW port)
struct GlobalDumper
{
    using result_type = void;

    std::string &out;

    template <class Parameter> result_type operator()(Parameter const &parameter) const
    {
        out += "global ";
        out += LE::Parameters::streamingName<Parameter>();
        out += " = ";
        out += number(static_cast<float>(parameter.getValue()));
        out += '\n';
    }
}; // struct GlobalDumper

/// \note `SWTest::Engine` is the harness's own, so the engine namespace has to
/// be spelled in full here.
using ModuleParameters = LE::SW::Engine::ModuleParameters;

void dumpModule(std::string &out, std::uint8_t const slot, ModuleParameters const &module)
{

    /// \note Streaming names throughout, not display names. This dump is hashed
    /// into presetCorpus.txt, whose contract is "a row that moves is a preset
    /// that loads differently" -- and retitling an effect or relabelling a knob
    /// is precisely a change that does *not* load differently. Naming the
    /// display strings here would have made every such rename a 303-row diff
    /// that says nothing. They are the same strings for all but one parameter
    /// today, so what this buys is not readability but the right sensitivity.
    ///                                       (01.08.2026.) (SW port)
    out += "module " + std::to_string(slot) + " = " +
           Effects::effectStreamingName(module.effectTypeIndex()) + '\n';
    out += "  bypass = " + std::string(module.bypass() ? "1" : "0") + '\n';

    auto const baseParameters(ModuleParameters::numberOfBaseParameters);

    for (std::uint8_t index(0); index < module.numberOfParameters(); ++index)
    {
        bool const isBase(index < baseParameters);
        auto const value(
            isBase ? module.getBaseParameter(index)
                   : module.getEffectParameter(static_cast<std::uint8_t>(index - baseParameters)));

        out += "  ";
        out += module.parameterInfo(index).streamingName;
        out += " = " + number(value);

        /// \note Bypass is parameter 0 and is the one base parameter with no
        /// LFO; the LFO array is indexed from the one after it.
        if (index >= ModuleParameters::numberOfNonLFOBaseParameters)
        {
            out += " |";
            LE::Parameters::forEach(module
                                        .lfo(static_cast<std::uint8_t>(
                                            index - ModuleParameters::numberOfNonLFOBaseParameters))
                                        .parameters(),
                                    LFODumper{out});
        }
        out += '\n';
    }
}

PresetProblems problems;

void countProblem(PresetProblem const problem, std::string_view const /*detail*/)
{
    switch (problem)
    {
    case PresetProblem::MissingParameter:
        ++problems.missingParameter;
        return;
    case PresetProblem::UnknownEffect:
    case PresetProblem::EffectNotAvailable:
        ++problems.unknownEffect;
        return;
    default:
        ++problems.other;
        return;
    }
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

void PresetLoader::setSideChain(std::string_view const recordedSource,
                                std::optional<unsigned int> const legacyInputMode,
                                std::string_view const sampleFileName) const
{
    /// \note `false` for "have a sample": wantsSampleFile() declines them, so a
    /// preset that names one loads without it -- which is exactly the arrangement
    /// the browser's "Ignore external audio" produces, and therefore exactly the
    /// migration arm those files take there too.
    LE::Utility::ignoreUnused(sampleFileName);
    engine.setSideChainSource(
        LE::SW::resolveSideChainSource(recordedSource, legacyInputMode, false));
}

bool PresetLoader::setNewGlobalParameters(
    LE::SW::GlobalParameters::Parameters const &newParameters) const
{
    LE::Parameters::forEach(newParameters, GlobalParameterUpdater{engine});
    return true;
}

PresetProblems const &presetProblems() { return problems; }
void clearPresetProblems() { problems = {}; }

ScopedProblemCounter::ScopedProblemCounter() : previous_(setPresetProblemReporter(&countProblem)) {}
ScopedProblemCounter::~ScopedProblemCounter() { setPresetProblemReporter(previous_); }

////////////////////////////////////////////////////////////////////////////////
///
/// \note Imbued, for the same reason the plugin's own writer is: this text is
/// what `presetCorpus.txt` hashes, and `snprintf` spells the point whichever way
/// the machine's locale says. So every committed digest was a statement about
/// the locale of the machine that generated it -- 303 rows that would all move
/// on a German developer's checkout, saying "these presets load differently"
/// about presets that load identically.
///
////////////////////////////////////////////////////////////////////////////////

std::string number(float const value)
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(6) << static_cast<double>(value);
    return text.str();
}

Loaded dump(Engine &engine)
{
    Loaded loaded;

    LE::Parameters::forEach(engine.program().parameters(), GlobalDumper{loaded.text});

    engine.program().moduleChain().forEach<ModuleParameters>([&](ModuleParameters const &module) {
        if (loaded.modules == 0)
            loaded.effects.clear();
        else
            loaded.effects += ',';
        loaded.effects += Effects::effectStreamingName(module.effectTypeIndex());

        loaded.parameters += module.numberOfParameters();
        dumpModule(loaded.text, static_cast<std::uint8_t>(loaded.modules), module);
        ++loaded.modules;
    });

    loaded.text += "modules = " + std::to_string(loaded.modules) + '\n';

    /// \note In the dump, so that the 2.x migration is inside what
    /// presetFixtures.txt hashes -- a shipped preset whose `Input_mode` stopped
    /// being read would move its row rather than passing quietly.
    loaded.text += std::string("side chain source = ") + toString(engine.sideChainSource()) + '\n';
    return loaded;
}

std::uint64_t digest(std::string_view const text)
{
    std::uint64_t hash{0xcbf29ce484222325ull};
    for (auto const character : text)
    {
        hash ^= static_cast<unsigned char>(character);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

} // namespace SWTest
