////////////////////////////////////////////////////////////////////////////////
///
/// presetLoading.cpp
/// -----------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "presetLoading.hpp"

#include "editorHost.hpp"
#include "spectrumWorxEditor.hpp"

#include "configuration/versionConfiguration.hpp" // MB_WARNING

#include "core/automatedModuleChain.hpp"
#include "core/host_interop/plugin2Host.hpp"
#include "core/modules/finalImplementations.hpp"
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/parameterID.hpp"
#include "core/spectrumWorxCore.hpp"
#include "core/threading/publish.hpp"

#include "gui/gui.hpp" // warningMessageBox()
#include "io/jucePath.hpp"

#include "le/math/conversion.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/spectrumworx/presetStorage.hpp"

#include "le/utility/assert.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace LE::SW::GUI
{
namespace
{
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Puts a whole GlobalParameters::Parameters into the engine, one
/// parameter at a time.
///
/// \note This was `SpectrumWorx::resetForGlobalParameters()`, and it is not the
/// same as assigning the struct: FFT size, overlap factor and window function
/// each reconfigure the engine, so a preset that changes one has to go through
/// `setGlobalParameter` rather than land in the field behind its back.
///
/// \note By which route depends on who owns the engine, which is the whole of
/// §2 rules 1 and 2 and is what this got wrong. A preset load runs on the main
/// thread and a user browsing presets with the transport rolling is the ordinary
/// case, so `setGlobalParameter` was writing the six parameters that `process()`
/// reads every block, from the other thread, with nothing between them. Thread
/// sanitizer names it as a write/read race on the parameter itself.
///
///   With audio running they go the way every other edit goes: applied to this
/// thread's copy and queued for the engine, which picks them up at the top of
/// its next block. That defers them, and the deferral is not new -- the chain
/// this same load publishes has always been queued the same way, and lands at
/// the same points (a block, a `params.flush()`, or `deactivate()`).
///
////////////////////////////////////////////////////////////////////////////////

struct GlobalParameterUpdater
{
    using result_type = void;

    EditorHost &host;

    template <class Parameter> result_type operator()(Parameter const &parameter) const
    {
        if (host.core().engineIsRunning())
        {
            host.editGlobalParameter(
                LE::Parameters::IndexOf<GlobalParameters::Parameters, Parameter>::value,
                Math::convert<float>(parameter.getValue()));
            return;
        }

        /// \note Nothing is processing, so the main thread owns the engine and
        /// may simply do it -- and has to: with no audio thread there is nothing
        /// to drain the queue, and this is the arrangement a session is restored
        /// in.
        LE_VERIFY((SpectrumWorxCore::setGlobalParameter<Parameter, SpectrumWorxCore>(
            host.core(), parameter.getValue())));
    }
}; // struct GlobalParameterUpdater

/// \brief Where a preset being loaded puts what it reads.
///
/// \note The 2016 original carried a target *program* index, because VST 2.4
/// gave a plugin 128 of them and the browser could load into one that was not
/// current -- whence `onlySetParameters()`, which meant "this program is not
/// live, so move the numbers and leave the engine alone". Neither CLAP nor the
/// harness has more than one program, so that case cannot arise and the answer
/// is a constant.
struct Loader
{
    EditorHost &host;
    /// Null when no window is open, which is the normal case for session state.
    SpectrumWorxEditor *pEditor;
    /// The preset browser's "ignore external samples" box.
    bool ignoreSampleFile;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Whether this pass is filling the main thread's Program rather than
    /// the engine's.
    ///
    /// \note Which is exactly what `onlySetParameters()` already meant, and the
    /// reason that branch is back rather than replaced. It was written for VST
    /// 2.4's 128 programs -- "this program is not live, so move the numbers and
    /// leave the engine alone" -- and the port retired it as a case that could no
    /// longer arise. It arises again for a different reason: there are two copies
    /// of one program now, and the main thread's is filled by a pass that
    /// reconfigures nothing, publishes nothing and loads no sample.
    ///
    ////////////////////////////////////////////////////////////////////////////
    bool mainThreadCopy;

    /// The chain reaches for this typedef; see presets.hpp's loadPreset().
    using Module = SpectrumWorxCore::Module;

    Program &program() const { return mainThreadCopy ? host.programMain() : host.core().program(); }
    GlobalParameters::Parameters &targetGlobalParameters() const { return program().parameters(); }
    AutomatedModuleChain &targetChain() const { return program().moduleChain(); }

    Host2PluginInteropControler::AutomationBlocker automationBlocker() const
    {
        return {host.core()};
    }

    /// \note The one point at which a preset load touches the engine; everything
    /// before it built a chain nothing else could see. See
    /// Threading::publishChain() and doc/tech/threading_model.md §5.
    void publishChain(AutomatedModuleChain &newChain) const
    {
        Threading::publishChain(host.core(), host.toEngine(), newChain);
    }

    /// \note The DSP half alone: the rack follows the chain, and
    /// `SpectrumWorxEditor::resyncModuleRack()` builds the strips once the chain
    /// is installed.
    SpectrumWorxCore::ModuleInitialiser moduleInitialiser() const
    {
        return host.core().moduleInitialiser();
    }

    bool onlySetParameters() const { return mainThreadCopy; }

    ////////////////////////////////////////////////////////////////////////////
    // The external audio file the preset names, if it names one.
    ////////////////////////////////////////////////////////////////////////////

    bool wantsSampleFile() const { return !ignoreSampleFile && !onlySetParameters(); }

    /// \note Wider than wantsSampleFile(): a patch loaded with "Ignore external
    /// audio" on still says where its side channel comes from, and the answer is
    /// then one of the two that is not a file. Only a load into a Program copy,
    /// which has no host to set anything on, wants none of this.
    bool wantsSideChain() const { return !onlySetParameters(); }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The sample is the one thing a preset can name that a preset failing
    /// to name did not clear. Every other parameter a file omits goes back to its
    /// default -- that is what makes loading a preset a statement about all of
    /// them rather than a merge -- and this returned early on an empty name, so
    /// the *previous* preset's audio file went on playing under the new one. It
    /// then went into the next `stateSave`, because `sampleFile_` is what that
    /// writes: a session that ended up naming a file the user had loaded two
    /// presets ago.
    ///
    ///   A named file that will not load lands in the same place for the same
    /// reason, and is worse: the load is a no-op, so the engine keeps the old
    /// sample and the session claims the new name. Cleared here rather than kept,
    /// which is the honest answer -- this preset does not use that audio, and
    /// nothing else the loader does leaves the previous preset's anything behind.
    ///
    /// \note And reported rather than shown. It used to raise a modal box from
    /// inside the load, which is a dialog nobody asked for in front of a host
    /// restoring a session. `PresetProblem` is where everything else wrong with a
    /// preset goes, and the caller decides: `GUI::loadPreset` folds it into the
    /// one summary a user who opened a preset gets, and `stateLoad` drops it.
    ///
    ////////////////////////////////////////////////////////////////////////////

    void setSample(std::string_view const sampleFileName) const
    {
        if (sampleFileName.empty())
        {
            host.setNewSample({});
            if (pEditor)
                pEditor->updateSampleNameAsync();
            return;
        }

        // Implementation note:
        //   Workaround for relative sample paths and Windows paths on OS X.
        //                                    (17.11.2011.) (Domagoj Saric)
        /// \note And the reason a factory sample is stored by bare name: that
        /// is the one spelling no separator can spoil, and Sample::load()
        /// resolves it against the embedded set when there is nothing on disk.
        ///
        // on the bytes, before they become a path: fs::path does not normalise
        // separators, so this is the only thing standing between a preset
        // written on the other platform and a path with the wrong ones in it
        std::string spelling(sampleFileName);
#ifdef _WIN32
        std::ranges::replace(spelling, '/', '\\');
#else
        std::ranges::replace(spelling, '\\', '/');
#endif // _WIN32

        /// \note utf8ToPath(), not `fs::path( std::string )`: what a preset
        /// carries is UTF-8 and the latter decodes with the active code page on
        /// Windows. \see io/jucePath.hpp.
        if (host.setNewSample(LE::IO::utf8ToPath(spelling)))
        {
            host.setNewSample({});
            reportPresetProblem(PresetProblem::SampleNotLoaded, sampleFileName);
        }

        if (pEditor)
            pEditor->updateSampleNameAsync();
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The audio file and the side-chain source together, because the
    /// second is not decidable without the first.
    ///
    /// \note **The sample is applied exactly as it always was**, independently of
    /// the source: a patch that names a file loads it and a patch that names none
    /// clears whatever the last one left, because a preset is a statement about
    /// everything it can name. What is new is only the source, which is then
    /// allowed to be `Main` or `Host` with a file still loaded and ready to be
    /// switched back to.
    ///
    /// \note **The migration.** A patch that records no source is a 2.x file, or
    /// a 3.0 file this build's predecessor wrote, and 2016's rule is recoverable
    /// exactly: the file wins if there is one, and otherwise `Input_mode`'s odd
    /// values are the ones that asked the host. That reproduces the old truth
    /// table row for row, including for the 84 shipped presets that name a
    /// carrier and the ten in `Sidechainables` that ask for a send.
    ///
    ///   `Ignore external audio` falls out of it rather than being a case: with
    /// the toggle on no sample is applied, so a patch that named one migrates to
    /// `Input_mode`'s answer instead of to `File`, which is what a user who asked
    /// not to be given somebody else's audio meant.
    ///
    /// \note A source of `File` with nothing loaded is not a state this leaves
    /// behind. A named file that will not decode is reported and cleared, and the
    /// source falls to `Main` -- so the selector never shows a file that is not
    /// there, and the engine never has a source it cannot honour.
    ///
    ////////////////////////////////////////////////////////////////////////////

    void setSideChain(std::string_view const recordedSource,
                      std::optional<unsigned int> const legacyInputMode,
                      std::string_view const sampleFileName) const
    {
        if (wantsSampleFile())
            setSample(sampleFileName);

        host.setSideChainSource(resolveSideChainSource(recordedSource, legacyInputMode,
                                                       !host.currentSampleFile().empty()));
    }

    bool setNewGlobalParameters(GlobalParameters::Parameters const &newParameters) const
    {
        LE::Parameters::forEach(newParameters, GlobalParameterUpdater{host});
        return true;
    }

    /// \note `syncedLFOFound` is ignored, and there is no problem kind for it: a
    /// host that reports no transport gets 120 BPM in four four, which is a
    /// defined answer and a common one -- the standalone is such a host. \see
    /// pluginTests.cpp's "With no transport the LFO clock is 120 BPM in four
    /// four", which pins the assumption.
    void moduleChainFinished(std::uint8_t const moduleCount, bool /*syncedLFOFound*/) const
    {
        if (pEditor)
            pEditor->setLastModulePosition(moduleCount);
    }
}; // struct Loader

struct Consumer
{
    EditorHost &host;
    SpectrumWorxEditor *pEditor;
    /// \see Loader::mainThreadCopy
    bool mainThreadCopy;

    using Module = Loader::Module;

    Loader presetLoader(bool const ignoreExternalSample) const
    {
        return {host, pEditor, ignoreExternalSample, mainThreadCopy};
    }

    Program &program() const { return mainThreadCopy ? host.programMain() : host.core().program(); }

    /// \note Silent on the main-thread pass: the host is told once, about the
    /// load as a whole, by the pass that reaches the engine.
    void notifyHostAboutPresetChangeBegin() const
    {
        if (!mainThreadCopy)
            host.automation().presetChangeBegin();
    }
    void notifyHostAboutPresetChangeEnd() const
    {
        if (!mainThreadCopy)
            host.automation().presetChangeEnd();
    }
}; // struct Consumer

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
///
/// \brief One dialog for a whole load, or none.
///
/// \note The preset layer counts now rather than raising a message box per
/// problem -- see PresetLoadReport. This is the caller that turns the count back
/// into something a user sees, and it is deliberately *this* caller: a user opened
/// a preset and is owed an answer. `SpectrumWorxCLAP::stateLoad` takes the same
/// report and says nothing, a session restore being nobody's business.
///
/// \note "Owed an answer" is not "owed a count". A missing parameter on its own
/// says nothing to a user: the effect grew that parameter after the preset was
/// written and the value defaulted, which is the format's forward compatibility
/// doing its job. A good fraction of the shipped banks raise one, and it is the only kind
/// any of them raises -- so this used to interrupt one factory preset in three
/// with a number nobody could act on. See PresetLoadReport::worthTellingTheUser()
/// and presetReportTests.cpp, which is what actually watches that total.
///
///   It is still *mentioned* when something else has gone wrong, because "two
/// effects are missing and so are forty parameters" is a fuller account of the
/// same event.
///
////////////////////////////////////////////////////////////////////////////////

void reportToTheUser(PresetLoadReport const &report)
{
    if (!report.worthTellingTheUser())
        return;

    juce::String message;
    if (report.failures)
        message << "The preset could not be read.\n";
    if (report.unknownEffects)
        message << static_cast<int>(report.unknownEffects)
                << " effect(s) in it are not in this build";
    if (report.unavailableEffects)
        message << static_cast<int>(report.unavailableEffects)
                << " effect(s) in it are not in this edition";
    if (report.unknownEffects || report.unavailableEffects)
        message << " (" << juce::String(report.firstDetail) << " and so on).\n";
    /// \note The one parameter case that is the user's business: the file carries
    /// a value and nothing in this build knows where to put it, so the preset
    /// will not sound the way it was saved. Named, because a name is the only
    /// thing that makes it reportable.
    if (report.unknownParameters)
        message << static_cast<int>(report.unknownParameters)
                << " value(s) in it could not be applied (" << juce::String(report.firstDetail)
                << ").\n";
    if (report.missingParameters)
        message << static_cast<int>(report.missingParameters)
                << " parameter(s) it does not mention were left at their defaults.\n";
    /// \note Named, for the same reason an unknown parameter is: the name is the
    /// only part of this a user can act on, and a moved audio file is something
    /// they can go and find.
    if (report.samplesNotLoaded)
        message << "The audio file it names could not be loaded ("
                << juce::String(report.firstDetail) << ").\n";
    if (report.modulesDropped)
        message << static_cast<int>(report.modulesDropped)
                << " module(s) in it did not fit this build's "
                << static_cast<int>(SW::Constants::maxNumberOfModules) << " slots.\n";

    GUI::warningMessageBox(MB_WARNING, message.toRawUTF8(), false);
}

bool loadPreset(EditorHost &host, SpectrumWorxEditor *const pEditor, char *const inMemoryPreset,
                bool const ignoreExternalSample, juce::String *const comment,
                char const *const presetName, DawExtraState const *const pDawExtraState)
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note A preset replaces every parameter, the whole chain and the sample,
    /// so anything a menu is standing over is about to be describing something
    /// that has gone -- the module combo boxes most of all, since the strips
    /// themselves are rebuilt.
    ///
    /// \note Only with a window open. `dismissAllActiveMenus()` is process-wide,
    /// and a session being restored into an instance that has no editor has no
    /// business closing a menu the user has open in a different one.
    ///
    ////////////////////////////////////////////////////////////////////////////
    if (pEditor)
        juce::PopupMenu::dismissAllActiveMenus();

    Consumer const consumer{host, pEditor, false};

    // Whatever a previous load left uncollected is not this load's.
    takePresetLoadReport();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The main thread's copy first, in a pass of its own.
    ///
    /// \note Two passes over one file rather than one pass and a clone, because
    /// the parser is the only thing that knows how to turn a preset into a
    /// chain: cloning would have to copy every module's parameters through the
    /// automated interface, which cannot reach the two an LFO does not export
    /// and quantises everything it can reach. A second parse is exact by
    /// construction and costs a parse of a file that was just read off disk.
    ///
    ///   `ParametersLoader` cannot be rewound -- `loadModuleChain()` asserts it
    /// has not already switched to module parameters -- so each pass builds its
    /// own over the same buffer.
    ///
    /// \note This one's report is dropped. It sees exactly the problems the pass
    /// below sees, and counting them twice would double every number a user is
    /// shown; `presetReportTests.cpp` is what watches those totals.
    ///
    ////////////////////////////////////////////////////////////////////////////
    {
        Consumer const mainThreadCopy{host, pEditor, true};
        SW::loadPreset(inMemoryPreset, true /*a sample is the engine's*/, nullptr, mainThreadCopy,
                       pDawExtraState);
        takePresetLoadReport();
    }

    /// \note The format layer speaks `std::string` -- it is below JUCE now, and
    /// a preset's comment is UTF-8 bytes whatever the interface's string type is.
    std::string commentBytes;
    consumer.notifyHostAboutPresetChangeBegin();
    bool const succeeded(SW::loadPreset(inMemoryPreset, ignoreExternalSample,
                                        comment ? &commentBytes : nullptr, consumer,
                                        pDawExtraState));
    if (comment)
        *comment =
            juce::String::fromUTF8(commentBytes.data(), static_cast<int>(commentBytes.size()));
    if (succeeded)
        copyPresetName(presetName, consumer.program().name());
    consumer.notifyHostAboutPresetChangeEnd();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And the rack follows the chain, said from here rather than left to
    /// the engine's echo.
    ///
    ///   The main-thread pass above replaced `programMain_`'s chain outright --
    /// `onlySetParameters()`, presets.hpp -- so by this line the editor's own
    /// chain *is* the preset's and every strip on screen belongs to the one
    /// before it. The engine's copy is a different matter: `publishChain()`
    /// queues it while audio is running, and the resync used to arrive only when
    /// the audio thread got round to installing it and echoed `ChainChanged`
    /// back. A host that is not calling `process()` never closes that loop, and
    /// the picture stays on the previous preset until something makes a block of
    /// audio happen.
    ///
    ///   Reported against the AudioUnit in Logic with the transport stopped, and
    /// not reproducible with the CLAP in Bitwig, whose engine runs regardless.
    /// The echo still arrives and still resyncs; it is a second, idempotent
    /// recomputation rather than the only one.
    ///
    /// \note Unconditional rather than gated on `succeeded`: a load that failed
    /// partway has still replaced this thread's chain, and a rack left pointing
    /// at the previous one is the worse of the two outcomes.
    ///
    /// \note Asynchronous, and it has to be -- the browser calls this from inside
    /// its list-box callback and goes on to touch its own widgets afterwards
    /// (PresetBrowser::presetSelectionChanged), while a resync destroys strips
    /// and moves the keyboard focus. \see refreshModuleRackAsync().
    ///
    ////////////////////////////////////////////////////////////////////////////
    if (pEditor)
        pEditor->refreshModuleRackAsync();

    /// \note Only with a window open. With none this is a session being restored,
    /// which nobody asked for and which may not even have a message thread yet.
    if (pEditor)
        reportToTheUser(takePresetLoadReport());

    return succeeded;
}

bool loadPreset(EditorHost &host, SpectrumWorxEditor *const pEditor, fs::path const &presetFile,
                bool const ignoreExternalSample, juce::String *const comment,
                char const *const presetName, DawExtraState const *const pDawExtraState)
{
    auto const presetData(readPresetFile(presetFile));
    if (!presetData)
        return false;
    return loadPreset(host, pEditor, presetData.get(), ignoreExternalSample, comment, presetName,
                      pDawExtraState);
}

} // namespace LE::SW::GUI
