////////////////////////////////////////////////////////////////////////////////
///
/// \file presetHarness.hpp
/// -----------------------
///
///   Driving the preset format with no editor, no host and no message thread.
///
///   Two things every preset test needs: something for presets.hpp's
/// loadPreset() to write through, and a canonical dump of what an engine holds
/// afterwards, so that "did this round-trip" and "did the parser change" are
/// both a string comparison.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef presetHarness_hpp__7B21D640_35CE_4A98_B0E7_9F42D8C51A63
#define presetHarness_hpp__7B21D640_35CE_4A98_B0E7_9F42D8C51A63
//------------------------------------------------------------------------------
#include "goldens/engineHarness.hpp"

#include "core/automatedModuleChain.hpp"
#include "core/spectrumWorxCore.hpp"

#include "le/spectrumworx/presets.hpp"

#include <cstdint>
#include <string>

namespace SWTest
{

using LE::SW::PresetProblem;
using LE::SW::PresetProblemReporter;

////////////////////////////////////////////////////////////////////////////////
///
/// \struct PresetConsumer
///
/// \brief Where a preset being loaded puts what it reads, with nothing above
/// the engine to put it in.
///
/// \note The editor-free half of GUI::loadPreset()'s consumer
/// (presetLoading.cpp). Deliberately a separate copy rather than a shared base:
/// the two members that differ -- the module initialiser and what happens when
/// the chain is finished -- are the two that exist only to build a UI region and
/// move a slot marker, and a test that needed an editor could not run here at
/// all.
///
////////////////////////////////////////////////////////////////////////////////

struct PresetLoader
{
    Engine &engine;

    /// The chain reaches for this typedef; see presets.hpp's loadPreset().
    using Module = LE::SW::SpectrumWorxCore::Module;

    LE::SW::Program &program() const { return engine.program(); }
    LE::SW::GlobalParameters::Parameters &targetGlobalParameters() const
    {
        return program().parameters();
    }
    LE::SW::AutomatedModuleChain &targetChain() const { return program().moduleChain(); }

    LE::SW::Host2PluginInteropControler::AutomationBlocker automationBlocker() const
    {
        return {engine};
    }
    /// \note No queue behind it, because the harness engine is never running:
    /// Threading::publishChain() therefore installs it here and now, and
    /// `newChain` comes back holding what it displaced. See
    /// doc/tech/threading_model.md §5.
    void publishChain(LE::SW::AutomatedModuleChain &newChain) const
    {
        engine.swapModuleChain(newChain);
    }
    LE::SW::SpectrumWorxCore::ModuleInitialiser moduleInitialiser() const
    {
        return engine.moduleInitialiser();
    }

    static bool onlySetParameters() { return false; }

    /// \note The external audio file, declined: the harness engine has no
    /// loader behind it, and what these tests are about is the parameters. A
    /// preset that names a sample therefore loads without it, which is the same
    /// answer the browser's "ignore external samples" box gives.
    static bool wantsSampleFile() { return false; }
    static void setSample(std::string_view) {}

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The side chain's source, which this harness records rather than
    /// applies -- there is no host under it to load a file on.
    ///
    /// \note Recorded all the same, because the *migration* is a claim about the
    /// format rather than about the plugin: a 2.x file's `Input_mode` becoming
    /// `Main` or `Host` is exactly the sort of thing presetCorpusTests exists to
    /// pin. \see doc/tech/sidechain-approach.md.
    ///
    ////////////////////////////////////////////////////////////////////////////
    static bool wantsSideChain() { return true; }
    void setSideChain(std::string_view recordedSource, std::optional<unsigned int> legacyInputMode,
                      std::string_view sampleFileName) const;

    bool setNewGlobalParameters(LE::SW::GlobalParameters::Parameters const &) const;

    static void moduleChainFinished(std::uint8_t /*moduleCount*/, bool /*syncedLFOFound*/) {}
}; // struct PresetLoader

struct PresetConsumer
{
    Engine &engine;

    using Module = PresetLoader::Module;

    PresetLoader presetLoader(bool /*ignoreExternalSample*/) const { return {engine}; }

    LE::SW::Program &program() const { return engine.program(); }

    static void notifyHostAboutPresetChangeBegin() {}
    static void notifyHostAboutPresetChangeEnd() {}
}; // struct PresetConsumer

////////////////////////////////////////////////////////////////////////////////
// Counting what the loader complains about
////////////////////////////////////////////////////////////////////////////////

/// \note A file-scope counter rather than something threaded through, because
/// PresetProblemReporter is a plain function pointer -- see the note on it.
/// These tests are single threaded and load one preset at a time.
struct PresetProblems
{
    unsigned int missingParameter{0};
    unsigned int unknownEffect{0};
    unsigned int other{0};

    unsigned int total() const { return missingParameter + unknownEffect + other; }
};

PresetProblems const &presetProblems();
void clearPresetProblems();

/// \brief Installs a counting reporter for as long as it is alive, so that a
/// preset load raises numbers instead of a juce::AlertWindow per problem in a
/// process with no message thread -- 809 leaked AsyncUpdaters, the first time.
class ScopedProblemCounter
{
  public:
    ScopedProblemCounter();
    ~ScopedProblemCounter();

    ScopedProblemCounter(ScopedProblemCounter const &) = delete; // makes non-copyable
    ScopedProblemCounter &operator=(ScopedProblemCounter const &) = delete;

  private:
    PresetProblemReporter const previous_;
}; // class ScopedProblemCounter

////////////////////////////////////////////////////////////////////////////////
// The canonical dump
////////////////////////////////////////////////////////////////////////////////

/// \brief Six significant figures.
///
/// \note Values come from decimal text in the file by way of strtof, so they are
/// the same number everywhere; six figures is enough to catch a parameter that
/// moved and not so many that a difference in the last bit of a conversion turns
/// a fixture red on another platform.
std::string number(float value);

/// What one preset turned into: the dump, plus the facts worth reading in the
/// clear rather than through a hash.
struct Loaded
{
    std::string text;
    std::string effects{"-"}; ///< the chain, in slot order
    unsigned int modules{0};
    unsigned int parameters{0};
    unsigned int missing{0}; ///< parameters the preset never mentioned
};

Loaded dump(Engine &);

/// FNV-1a, 64 bit, over the dump text.
std::uint64_t digest(std::string_view text);

} // namespace SWTest

#endif // presetHarness_hpp
