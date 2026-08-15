////////////////////////////////////////////////////////////////////////////////
///
/// \file editorHost.hpp
/// --------------------
///
///   What the editor needs from whatever is hosting it.
///
///   The 2016 editor reached straight into the SpectrumWorx VST2/AU class:
/// effect() recovered it from the editor's own address, because the effect
/// owned the editor as a member. That class is gone, and the CLAP plugin cannot
/// take its place directly -- sw-impl links sw-gui, so sw-gui naming
/// SpectrumWorxCLAP would be a cycle.
///
///   So the dependency is inverted. Most of what the editor asked the effect
/// for was really the engine's, and is reached through core(); the rest -- the
/// side channel's sample file, presets, and the two persisted settings -- is
/// genuinely the host's, and is declared here.
///
/// \note Deliberately small, and deliberately not a home for anything the
/// engine already answers. Every function added here is one the editor cannot
/// be tested without a plugin behind it.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef editorHost_hpp__0C5A1E7B_9D34_4F82_A6E1_37B0C4D8F925
#define editorHost_hpp__0C5A1E7B_9D34_4F82_A6E1_37B0C4D8F925
//------------------------------------------------------------------------------
#include "core/threading/messages.hpp"
#include "core/threading/valueMailbox.hpp"

#include "le/utility/platformSpecifics.hpp"

#include <juce_core/juce_core.h>

/// `fs`, for the side channel's sample file. \see io/jucePath.hpp.
#include "filesystem/import.h"

namespace juce
{
class PopupMenu;
} // namespace juce

namespace LE::SW
{

class Program;
class SpectrumWorxCore;
class Plugin2HostInteropControler;
union ParameterID;

namespace GUI
{

class SpectrumWorxEditor;

////////////////////////////////////////////////////////////////////////////////
///
/// \class EditorHost
///
////////////////////////////////////////////////////////////////////////////////

class EditorHost
{
  public:
    /// The engine. Parameters, the module chain, the setup and the process lock
    /// all come from here.
    ///
    /// \note And the audio thread's, for as long as the plugin is activated. What
    /// the editor reads is `programMain()`; what is still legitimately taken from
    /// here is the engine's own -- the spectral setup and the LFO clock.
    virtual SpectrumWorxCore &core() = 0;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The Program the main thread owns, which is the one the editor
    /// draws and edits. `[main-thread]`
    ///
    /// \note Not `core().program()`, which belongs to the audio thread while the
    /// plugin is activated -- §2 rule 1. The editor walks this state constantly
    /// (`resyncModuleRack()`, `getIndexForModule()`, every strip holding a module
    /// out of it), and the module chain is a circular intrusive list the engine
    /// splices in `drainCommands()`. A walk that overlaps a splice crosses into
    /// another chain's root, which `isEnd()` does not recognise as an end.
    ///
    ////////////////////////////////////////////////////////////////////////////
    virtual Program &programMain() = 0;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief An edit, both halves of it: applied to the Program this thread owns
    /// and queued for the engine. `[main-thread]`
    ///
    /// \note One call rather than two, because leaving them to the caller is what
    /// lets them come apart -- a bare `toEngine().push()` moves the engine and not
    /// the copy `paramsValue` answers from.
    ///
    ////////////////////////////////////////////////////////////////////////////
    virtual void editParameter(ParameterID, float value) const = 0;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief One of the six global parameters, by its index in
    /// `GlobalParameters::Parameters` and **in its own units**. `[main-thread]`
    ///
    /// \note Which is what distinguishes it from `editParameter()`, whose value
    /// is in the units the host automation edge speaks -- and those are not the
    /// same numbers. `FullRangeAutomatedParameter` carries a power-of-two
    /// parameter as its *exponent*, so the FFT size crosses that edge as 11 and
    /// not as 2048, and an enumerated parameter crosses as its ordinal.
    ///
    ///   Everything on this side holds the value the user sees, so the
    /// conversion belongs at the boundary. It was at neither end: the settings
    /// page handed `queueGlobalParameter` a raw 2048, which the edge then read
    /// as an exponent far outside the six it has -- an assertion in a checked
    /// build and a meaningless size in a shipped one, on all three of the FFT
    /// size, the overlap factor and the window function.
    ///                                       (08.08.2026.) (SW port)
    ///
    /// \note Not virtual, and defined once in terms of `editParameter()` above:
    /// the conversion is a property of the protocol, not of whoever is hosting
    /// the editor, and three implementations of it would be three chances to
    /// disagree.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void editGlobalParameter(std::uint8_t index, float value) const;

    /// \brief Puts \p effectIndex in \p slot, in both copies. `[main-thread]`
    /// \return false when the effect is not in this build, which is the one
    /// failure a caller can still be told about synchronously.
    virtual bool editSlot(std::uint8_t slot, std::int8_t effectIndex) = 0;

    /// \brief Moves the module in \p from to \p to, in both copies. `[main-thread]`
    virtual void editModuleMove(std::uint8_t from, std::uint8_t to) = 0;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Hands the engine one of the two LFO parameters that have no
    /// `ParameterID` -- Waveform and SyncTypes -- addressed by index.
    /// `[main-thread]`
    ///
    /// \note Only the engine's half: the caller has the parameter's type and has
    /// already written its own copy, which is what `editParameter` does through
    /// the protocol for the five that a host can see. Here so that the push is
    /// checked where every other push is, rather than being the one bare
    /// `toEngine().push()` left in the interface -- see the note on
    /// `editParameter`, which is the same argument.
    ///
    ////////////////////////////////////////////////////////////////////////////
    virtual void publishUnexportedLFOParameter(std::uint8_t moduleIndex,
                                               std::uint8_t moduleParameterIndex,
                                               std::uint8_t lfoParameterIndex, float value) = 0;

    /// The other direction: telling the host that the user moved something.
    /// Gestures, automation notifications and module chain changes.
    virtual Plugin2HostInteropControler &automation() = 0;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Appends the host's own entries for \p parameter to \p menu -- its
    /// automation lanes, its MIDI learn, whatever it has -- when a knob's right
    /// button menu is being built. `[main-thread]`
    ///
    /// \note `clap_host_context_menu`, which is the only route a plugin has to
    /// them, and it is optional: a host that does not implement it adds nothing
    /// and the menu ends where the plugin's own entries do.
    ///
    /// \note Here rather than in the widget layer because a `clap_host *` is the
    /// one thing on the far side of this interface that the editor cannot reach
    /// through `core()` -- which is the test this file's header sets.
    ///
    ////////////////////////////////////////////////////////////////////////////
    virtual void addHostParameterEntries(ParameterID parameter, juce::PopupMenu &menu) const = 0;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The protocol, as the editor sees it: a queue to ask the engine for
    /// something, and a mailbox to read what it is currently doing.
    ///
    /// \note These belong to the plugin and not to the editor, because the host
    /// reads parameters through `clap_plugin_params` with the window shut. The
    /// editor is a *user* of them, which is why they arrive through this
    /// interface rather than through the editor's constructor.
    ///
    /// \note `const` on both, and a `const &` on the mailbox: the editor never
    /// owns either, and it only ever reads the second. Pushing to a queue is a
    /// mutation of the queue and not of the host, which is why the first is
    /// const-qualified and hands back a non-const queue.
    ///
    /// \see core/threading/messages.hpp, doc/tech/threading_model.md §3.
    ///
    ////////////////////////////////////////////////////////////////////////////

    virtual Threading::ToEngineQueue &toEngine() const = 0;
    virtual Threading::ValueMailbox const &modulatedValues() const = 0;

    /// \note How the plugin comes to know about the editor at all. Called from
    /// the editor's constructor and destructor, on the UI thread -- opened()
    /// last, so nothing reaches a half-built editor, and closed() first, so
    /// nothing reaches a dying one.
    virtual void editorOpened(SpectrumWorxEditor &) = 0;
    virtual void editorClosed(SpectrumWorxEditor &) = 0;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Asks whoever owns the window to make it \p width x \p height.
    /// `[main-thread]`
    ///
    /// \return whether it will be. A refusal is not a failure and must not be
    /// treated as one: the editor lays its panel out over the module strips
    /// instead. \see SpectrumWorxEditor::PanelPlacement.
    ///
    /// \note `clap_host_gui::request_resize`, which is the only way a plugin can
    /// change the size of a window it does not own -- and the reason this is on
    /// the interface rather than something the editor does for itself. Nothing
    /// here says the editor may be dragged bigger: `can_resize` stays false and
    /// this is a request for one particular size.
    ///
    ////////////////////////////////////////////////////////////////////////////
    virtual bool requestEditorSize(int width, int height) = 0;

    ////////////////////////////////////////////////////////////////////////////
    // The side channel's sample: the external audio file that feeds it in place
    // of the host's side chain port.
    //
    // \note The listener registration is the 2016 shape and the last three are
    // vestigial while a load is synchronous -- isSampleLoadInProgress() is what
    // the editor asks before it registers, and today it is always false. Kept
    // whole because they are the interface a deferred load would come back
    // through, and the note on SpectrumWorxCLAP::setNewSample says what such a
    // load would need first.
    ////////////////////////////////////////////////////////////////////////////

    /// \note `fs::path`, and an empty one is "no sample". `juce::File` stood here
    /// and made this interface the reason `Sample` and the CLAP carried JUCE's
    /// file type at all; the conversion to one now happens at the two file
    /// choosers that genuinely need it. \see io/jucePath.hpp.
    ///                                       (09.08.2026.) (SW port)
    virtual fs::path currentSampleFile() const = 0;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Loads \p file as the side channel's source; an empty path clears
    /// it.
    ///
    /// \return the reason it could not, or null. **The caller decides what to do
    /// about that**, which is the whole point of it being returned rather than
    /// shown: a user who picked a file from the menu gets a dialog, and a preset
    /// naming an audio file that is not on this machine reports a preset problem
    /// like every other thing wrong with a preset. This used to raise the dialog
    /// itself, so restoring a session -- which nobody asked for, possibly with no
    /// window yet -- stopped the host with a modal box.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    virtual char const *setNewSample(fs::path const &) = 0;
    virtual bool isSampleLoadInProgress() const = 0;
    virtual void registerSampleLoadedListener(SpectrumWorxEditor &) = 0;
    virtual void deregisterSampleLoadedListener(SpectrumWorxEditor const &) = 0;

    /// \note Presets were two more virtuals here, and are neither. Everything
    /// loading one needs is reachable through core() and automation(), so it is
    /// one free function over this interface -- presetLoading.hpp -- rather than
    /// something each plugin format reimplements. The program's name comes off
    /// the Program itself.

    ////////////////////////////////////////////////////////////////////////////
    // Settings.
    ////////////////////////////////////////////////////////////////////////////

    /// \note Was runningAsAU(): Audio Units negotiate their channel layout with
    /// the host, so the plugin must not offer the user a way to change it.
    virtual bool completelyDisableIOChanges() const = 0;

    /// \note `shouldLoadLastSessionOnStartup()` was a pair here, reaching a flag
    /// nothing ever read: the checkbox on the interface page stored it and no
    /// session was ever reloaded from it. Restoring what was open is the host's
    /// job and every one of them does it.
    ///                                       (14.08.2026.) (SW port)

  protected:
    /// Not deleted through this.
    ~EditorHost() = default;
}; // class EditorHost

} // namespace GUI

} // namespace LE::SW

#endif // editorHost_hpp
