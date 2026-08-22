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
#include "le/spectrumworx/sideChainSource.hpp"
#include "core/threading/valueMailbox.hpp"

#include "le/utility/platformSpecifics.hpp"

#include <atomic>
#include <cstdint>

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

////////////////////////////////////////////////////////////////////////////////
///
/// \struct PanelState
///
/// \brief Where the user was in the panel column: which of the two panels was
/// up, which tab the settings one was on, and where the browser was pointing.
///
/// \note Four answers rather than one because they are not exclusive -- a user
/// who leaves the settings panel up was somewhere in the browser before that,
/// and expects to be there again when they press PRESETS. \see issue #129.
///
/// \note The two enumerations cross a file, so they are streamed **by name**
/// rather than by ordinal, exactly as the preferences file's are: inserting a
/// value cannot then silently change what an existing session means, and what is
/// in the file can be grepped for in the source.
///
/// \note Neither of the browser's two answers is trusted on the way back in. A
/// folder is a path from a previous run that the user may have moved, and a bank
/// is a name a later build need not still ship; PresetBrowser::restoreLastPlace()
/// checks both and falls back to the top of the factory tree.
///
////////////////////////////////////////////////////////////////////////////////

struct PanelState
{
    enum class Panel : std::uint8_t
    {
        presets,
        settings
    };

    enum class PresetLocation : std::uint8_t
    {
        factory,
        user
    };

    /// \note The browser, which is what a plugin with no session opens on: it is
    /// what a user came for. \see SpectrumWorxEditor::openRememberedPanel().
    Panel panel{Panel::presets};

    /// \note An index and not a `SettingsPage`: that enumeration is the editor's
    /// and this layer is below it. What comes back out of a file is checked
    /// against the tabs this build has. \see SpectrumWorxEditor::showSettings().
    unsigned int settingsPage{0};

    PresetLocation presetLocation{PresetLocation::factory};
    juce::String presetBank; ///< when presetLocation is factory
    fs::path presetFolder;   ///< when presetLocation is user
}; // struct PanelState

////////////////////////////////////////////////////////////////////////////////
///
/// \struct LoadedPreset
///
/// \brief Which preset the plugin is playing, and whether it has been edited
/// since it arrived.
///
///   What the two Save buttons key on. Save As offers to write the edit
/// somewhere new; Save offers to write it back where the sound came from, which
/// is why the file is here rather than taken from whichever row the browser
/// happens to have selected -- a user may well have gone looking elsewhere
/// between loading a preset and deciding to keep their changes.
///
/// \note The plugin's rather than the browser's, for two reasons: the browser is
/// built and destroyed every time the panels are swapped, and this goes into the
/// session so that reopening a project finds the same preset name over the list
/// and the same buttons lit. \see issue #177.
///
/// \note `modified` is atomic because a host writing a parameter from its audio
/// thread sets it -- the same path that reaches `clap_host_state::mark_dirty`,
/// and the same reason that one defers. Nothing reads it but the message thread.
///                                           (22.08.2026.)
///
////////////////////////////////////////////////////////////////////////////////

struct LoadedPreset
{
    LoadedPreset() = default;
    LoadedPreset(LoadedPreset const &) = delete; // holds an atomic
    LoadedPreset &operator=(LoadedPreset const &) = delete;

    /// \brief Empty when nothing has been loaded: a fresh instance, or a session
    /// whose preset was renamed out from under it.
    juce::String name;

    PanelState::PresetLocation location{PanelState::PresetLocation::factory};
    juce::String bank; ///< when location is factory
    fs::path file;     ///< when location is user: what Save overwrites

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief What the browser's comment box holds.
    ///
    ///   Here rather than only in the file so that a comment typed against a
    /// *factory* preset is not lost: there is nothing in the binary to write it
    /// to, and it is still a note the user made about the sound they are
    /// playing. It goes into the session with everything else. \see issue #180.
    ///
    ////////////////////////////////////////////////////////////////////////////
    juce::String comment;

    std::atomic<bool> modified{false};

    /// \brief Whether Save has somewhere to write, which a factory preset and a
    /// fresh instance both do not.
    bool canBeOverwritten() const
    {
        return (location == PanelState::PresetLocation::user) && !file.empty();
    }

    /// \brief Points this at \p presetName, wherever it came from, and calls it
    /// unedited. \see PresetBrowser::presetSelectionChanged().
    void loaded(juce::String const &presetName, PanelState::PresetLocation const from)
    {
        name = presetName;
        location = from;
        comment.clear();
        modified.store(false, std::memory_order_relaxed);
    }
}; // struct LoadedPreset

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
    ///
    /// \brief Says the editor *is* now \p width x \p height, and that the window
    /// around it has to follow. `[main-thread]`
    ///
    ///   An announcement, where requestEditorSize() above is a negotiation, and
    /// the difference is whether there is a fallback. A host that will not widen
    /// its window for the preset browser gets the browser over the module strips
    /// instead, so asking first is right there. A user who picks 200 % gets 200
    /// %; there is nothing else to give them, so the editor changes and this
    /// carries the consequence outwards.
    ///
    /// \note And it must not stop at a refusal: `clap-wrapper`'s macOS standalone
    /// resizes its window and **returns false** from `request_resize`, so a
    /// refusal does not even mean the window stayed put. Leaving the shim's
    /// components at the old size while the window has the new one shows up as
    /// the editor sliding up or down it, a JUCE peer inside a foreign NSView
    /// being anchored at Cocoa's bottom left.
    ///
    ////////////////////////////////////////////////////////////////////////////
    virtual void editorSizeChanged(int width, int height) = 0;

    ////////////////////////////////////////////////////////////////////////////
    // The side channel's sample: the external audio file that feeds it in place
    // of the host's side chain port.
    //
    // \note The last three are vestigial while a load is synchronous:
    // isSampleLoadInProgress() is what the editor asks before it registers, and
    // it is always false. Kept whole as the interface a deferred load would come
    // back through.
    ////////////////////////////////////////////////////////////////////////////

    /// \note `fs::path`, and an empty one is "no sample". Naming `juce::File`
    /// here is what would make `Sample` and the CLAP carry JUCE's file type; the
    /// conversion belongs at the two file choosers. \see io/jucePath.hpp.
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
    ///
    ////////////////////////////////////////////////////////////////////////////
    virtual char const *setNewSample(fs::path const &) = 0;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief What feeds the side channel, which is the audio-file selector's
    /// answer and travels with the patch. \see sideChainSource.hpp.
    ///
    /// \note A pair of plain accessors rather than a parameter, because "load
    /// this file" and "take the host's port" are one act and one selection. The
    /// setter publishes to the audio thread; `SpectrumWorxCLAP` sends it in the
    /// same command as a sample swap so that the two can never be seen apart.
    ///
    ////////////////////////////////////////////////////////////////////////////
    virtual SideChainSource sideChainSource() const = 0;
    virtual void setSideChainSource(SideChainSource) = 0;

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

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Where the user was in the panel column. `[main-thread]`
    ///
    /// \note Here rather than on the editor because nothing on the editor lives
    /// long enough to hold it: a panel is destroyed every time the other one is
    /// opened -- which is the report in issue #129 -- and the editor every time
    /// the window shuts. It is the *session's* answer rather than this user's,
    /// so it goes into the DAW extra state and not into the preferences file:
    /// two projects may reasonably have been left in two different places.
    /// \see SpectrumWorxCLAP::sessionState() and streaming_format.md §4.4.
    ///
    /// \note A reference rather than a getter and a setter per field. It is one
    /// thread's own scratch state, every member of it is written by the widget
    /// that owns that part of the panel, and the alternative is eight accessors
    /// that say nothing the struct does not.
    ///
    ////////////////////////////////////////////////////////////////////////////

    virtual PanelState &panelState() = 0;
    PanelState const &panelState() const { return const_cast<EditorHost &>(*this).panelState(); }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Says the session has changed in a way that is not a parameter.
    ///
    /// \note The preset comment is the one that needs this. Everything else the
    /// editor can change goes out through `editParameter`, which tells the host
    /// on the way past; a comment reaches no parameter and no engine and would
    /// otherwise be a change a host never hears about -- so a project closed
    /// after typing one would be offered as unmodified. \see issue #180.
    ///
    ////////////////////////////////////////////////////////////////////////////
    virtual void markStateModified() const = 0;

    /// \brief The preset the plugin is playing. \see LoadedPreset and issue #177.
    virtual LoadedPreset &loadedPreset() = 0;
    LoadedPreset const &loadedPreset() const
    {
        return const_cast<EditorHost &>(*this).loadedPreset();
    }

    /// \note `shouldLoadLastSessionOnStartup()` was a pair here, reaching a flag
    /// nothing ever read: the checkbox on the interface page stored it and no
    /// session was ever reloaded from it. Restoring what was open is the host's
    /// job and every one of them does it.

  protected:
    /// Not deleted through this.
    ~EditorHost() = default;
}; // class EditorHost

} // namespace GUI

} // namespace LE::SW

#endif // editorHost_hpp
