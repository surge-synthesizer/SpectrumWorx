////////////////////////////////////////////////////////////////////////////////
///
/// \file spectrumWorxEditor.hpp
/// ----------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef spectrumWorxEditor_hpp__3D67D57C_4EAA_4263_8FA1_C8CA61C7A539
#define spectrumWorxEditor_hpp__3D67D57C_4EAA_4263_8FA1_C8CA61C7A539
//------------------------------------------------------------------------------
#include "core/host_interop/parameters.hpp"
#include "core/parameterID.hpp"
#include "gui/about.hpp"
#include "gui/gui.hpp"
#include "gui/editor/auxiliaryComponents.hpp"
#include "gui/editor/moduleMenuHolder.hpp"
#include "gui/preset_browser/presetBrowser.hpp"

#include "le/parameters/lfoImpl.hpp" //...mrmlj...member typedefs...
#include "le/parameters/parametersUtilities.hpp"
#include "le/utility/cstdint.hpp"
#include "le/utility/platformSpecifics.hpp"

#include <juce_gui_basics/juce_gui_basics.h>
#include "le/utility/intrusivePtr.hpp"

#include <array>
#include <memory>
#include <utility>
#include <optional>
//------------------------------------------------------------------------------
namespace boost
{
template <class T> class intrusive_ptr;
}

namespace LE::SW
{

class Module;
class ModuleGUI;
class SpectrumWorxCore;
class Plugin2HostInteropControler;

class AutomatedModuleChain;

class Program;

namespace GUI
{

class EditorHost;

////////////////////////////////////////////////////////////////////////////////
///
/// \class SpectrumWorxEditor
///
/// \brief The editor: the skin, the module rack, and the two panels that share
/// the column beside them.
///
////////////////////////////////////////////////////////////////////////////////

class SpectrumWorxEditor final : private SkinLifetime,
                                 public WidgetBase<>,
                                 public juce::DragAndDropContainer,
                                 private juce::Button::Listener,
                                 private juce::Timer
{
  public:
    /// \note constexpr rather than `static ... const`: these are compared against
    /// and passed by reference outside this class, and an in-class initialiser
    /// with no out-of-line definition is not something that can be odr-used.
    static constexpr unsigned short estimatedWidth{845};

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The editor's height, which is the artwork's.
    ///
    ///   The artwork is 845 x 564 and every offset in this editor is a pixel
    /// position in it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    static constexpr unsigned short artworkHeight{564};

    static constexpr unsigned short estimatedHeight{artworkHeight};

    // the zoom is deliberately not here: the editor keeps its skin-pixel bounds
    // and ZoomedEditor holds the transform and reports the scaled size, so
    // exactly one component scales and exactly one answers for size

  public: //...mrmlj...VST 2.4 editor dummy implementation...
    static bool setKnobMode(int) { return false; }

    static bool onKeyDown(char, int, int) { return false; }
    static bool onKeyUp(char, int, int) { return false; }

    static void idle() {}

  public:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief What opening the preset browser or the settings panel does to the
    /// editor.
    ///
    ///   Both panels are 287 x 545 and the editor's artwork is 845 x 564. Its
    /// left column is 320 px wide and every pixel of it is spoken for -- the
    /// in/out/mix knobs, the module-info and LFO column, and the two buttons that
    /// open these panels, which a panel must not cover or there is no way to shut
    /// it again. So a panel either covers the module strips or the editor grows a
    /// column for it, and this says which. Only one panel is ever open either
    /// way: there is one panel-sized rectangle, wherever it is. \see overlayX and
    /// panelColumnX below for the two places it can be.
    ///
    /// \note The column is on the **left** and the skin moves right to make room
    /// for it. \see MainArea, which is what moves.
    ///
    ////////////////////////////////////////////////////////////////////////////

    enum class PanelPlacement : std::uint8_t
    {
        /// Over the module strips, in an editor that never changes size. What
        /// every host can do, and what a live rack vanishes behind.
        overlay,

        /// The editor asks its host for a wider window while a panel is up and
        /// gives the space back when it shuts, so nothing is covered. Needs a
        /// host that honours `clap_host_gui::request_resize`; one that refuses
        /// leaves the panel over the strips, as `overlay`.
        expandContract,

        /// The column is always there and always holds a panel -- the preset
        /// browser when the user has asked for nothing else, so that the plugin
        /// opens on the thing they most often came for. \see openRestingPanel().
        alwaysVisible
    }; // enum class PanelPlacement

    /// \note A per-editor property rather than a build-time constant: this is the
    /// sort of thing that ends up on the settings panel's interface page, and a
    /// test has to be able to drive all three arrangements against one build.
    static constexpr PanelPlacement defaultPanelPlacement{PanelPlacement::alwaysVisible};

  public:
    /// \note The placement is a constructor argument and not only a setter
    /// because `alwaysVisible` has to be in force before the shim reads the
    /// editor's size -- see the constructor.
    explicit SpectrumWorxEditor(EditorHost &, PanelPlacement = defaultPanelPlacement);
    ~SpectrumWorxEditor();

  public:
    static SpectrumWorxEditor &fromChild(juce::Component const &);
    static SpectrumWorxEditor &fromPresetBrowser(PresetBrowser &);

    Engine::Setup const &engineSetup() const;
    AutomatedModuleChain &moduleChain();
    AutomatedModuleChain const &moduleChain() const;

    bool loadPreset(fs::path const &, bool ignoreExternalSample, juce::String &comment,
                    juce::String const &presetName);
    /// \note A factory preset has no file; it comes out of the binary.
    bool loadPreset(char *inMemoryPreset, bool ignoreExternalSample, juce::String &comment,
                    juce::String const &presetName);
    void savePreset(fs::path const &, bool ignoreExternalSample, juce::String const &comment) const;
    char const *currentProgramName() const;

    /// \brief Whether the user has locked the sidechain source against presets.
    /// \see ignoreExternalSample_, and the two calls in PresetBrowser.
    bool ignoreExternalSample() const;

    bool presetLoadingInProgress() const;

  public:
    /// \note Was the SpectrumWorx VST2/AU class, recovered from this editor's
    /// own address. It is the engine now: every one of these calls asked the
    /// effect for something the engine owns.
    SpectrumWorxCore &effect();
    SpectrumWorxCore const &effect() const;

    /// The rest -- sample, presets, settings -- which only a plugin can answer.
    EditorHost &editorHost() const { return editorHost_; }

  private:
    using Module = SW::Module;

    /// \note The whole of what the editor wants from the plugin in this
    /// direction.
    using Host = Plugin2HostInteropControler;
    Host &host();
    Host const &host() const;

    Program &program();
    Program const &program() const;

    SpectrumWorxCore &moduleChainOwner() { return effect(); }
    SpectrumWorxCore const &moduleChainOwner() const { return effect(); }

  private:
  public: //...mrmlj...FMOD...
    /// \note Workarounds for Clang to force lazy template instantiations so
    /// that this header does not require a full definition of the SpectrumWorx
    /// class (when Host and Effect are in fact SpectrumWorx).
    ///                                       (02.07.2014.) (Domagoj Saric)
    template <class Parameter, class Host>
    static void globalParameterChanged(Host &host, typename Parameter::value_type const value,
                                       bool const asDiscreteGesture)
    {
        host.template globalParameterChanged<Parameter>(Parameter(value), asDiscreteGesture);
    }
    template <class Parameter, class Effect>
    static bool setGlobalParameter(Effect &effect, typename Parameter::value_type const value)
    {
        return Effect::template setGlobalParameter<Parameter>(effect, value);
    }

  public: // for EditorKnob
    void mainKnobDragStarted(std::uint8_t parameterIndex) const;
    void mainKnobDragStopped(std::uint8_t parameterIndex) const;

    /// \name The three knobs above the rack
    ///
    /// \note Read-only, and public so a headless run can read what they are
    /// *showing*, which is not the same question as what the program holds.
    /// \see issue #91.
    ///@{
    EditorKnob const &inKnob() const { return in_; }
    EditorKnob const &outKnob() const { return out_; }
    EditorKnob const &mixKnob() const { return mix_; }
    ///@}

    /// \note Queued, and applied by the engine where every other parameter
    /// change is: the FFT size and the overlap factor reallocate the whole
    /// spectral working set, which is not the message thread's to do.
    ///
    /// \note The `bool` is not the engine's answer -- nothing can give one
    /// synchronously. It comes back as a `ToUI::BaseParameterChanged`, and until
    /// then the interface believes itself.

    template <class Parameter>
    bool globalParameterChanged(typename Parameter::value_type const value,
                                bool const asDiscreteGesture)
    {
        queueGlobalParameter(
            LE::Parameters::IndexOf<GlobalParameters::Parameters, Parameter>::value,
            Math::convert<float>(value));
        this->globalParameterChanged<Parameter>(host(), Parameter(value), asDiscreteGesture);
        return true;
    }

    template <class Parameter> bool setGlobalParameter(typename Parameter::value_type const value)
    {
        queueGlobalParameter(
            LE::Parameters::IndexOf<GlobalParameters::Parameters, Parameter>::value,
            Math::convert<float>(value));
        return true;
    }

  private:
    /// \brief One global parameter, by its index in GlobalParameters::Parameters,
    /// in its own units. Non-template so that the header does not need the
    /// protocol.
    void queueGlobalParameter(std::uint8_t index, float value) const;

  public:
  private:
    template <class Parameter> void updateGlobalParameterWidget();

  public:
    void updateActiveControlValue();

    void updateSampleName();
    void updateSampleNameAsync();

    void updateForGlobalParameterChange();

    void updateForEngineSetupChanges();

    void updateForNewTimingInfo();
    void updateLFO(ModuleUI const &, std::uint8_t parameterIndex, std::uint8_t lfoParameterIndex,
                   /*LFO::AutomatedParameterValue*/ float value);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief A parameter that something other than this editor moved -- host
    /// automation, or a preset. `[main-thread]`
    ///
    /// \note Arrives as a `ToUI::BaseParameterChanged` off the ring, so that no
    /// widget is written from the thread the change came in on.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void parameterChangedElsewhere(ParameterID, float value);

    void moduleActivated();
    void moduleDeactivated();
    void moduleControlActivated(ModuleControlBase &, double minimum, double maximum,
                                double interval);
    void moduleControlDectivated(ModuleControlBase const &);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Which module strip is selected, and which of its controls the mouse
    /// or the keyboard is on. One of each, per editor.
    ///
    /// \note Per editor rather than per process: two instances of the plugin in
    /// one host each have a window, and a static would let one deselect the
    /// other's module and outlive the other's storage.
    ///
    ////////////////////////////////////////////////////////////////////////////

    ModuleUI *selectedModule() const { return pSelectedModule_; }
    ModuleControlBase *activeControl() const { return pActiveControl_; }

    ParameterID moduleControlID(ModuleControlBase const &) const;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Turns \p control's LFO on or off. `[main-thread]`
    ///
    /// \note One implementation with two ways in: the LFO strip's own switch,
    /// and the knob's right button menu. `LFO::Enabled` is an exported
    /// parameter, so this is an edit like any other -- both copies and the host
    /// -- and not a flag the interface may set for itself.
    ///
    /// \note Takes the control rather than working off `activeControl()`: a
    /// menu's callback runs whenever the user chose, and what was current when
    /// it opened need not still be.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void setLFOEnabled(ModuleControlBase &control, bool enable);

    bool sharedModuleControlsActive() const { return sharedModuleControls_.has_value(); }
    bool sharedModuleControlsActiveAndFocused() const
    {
        return sharedModuleControlsActive() && sharedModuleControls_->hasFocus();
    }

    void updateModuleParameterAndNotifyHost(ModuleUI &, std::uint8_t moduleParameterIndex,
                                            float parameterValue) const;

    void destroyChainGUIs();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The module strips, which this editor owns.
    ///
    ///   A strip holds a counted reference to its module, so the module cannot be
    /// destroyed while it is on screen and no thread but this one ever touches a
    /// widget.
    ///
    /// \note Found by module rather than by slot: there are at most five and the
    /// search is a pointer comparison, where an array indexed by slot would have
    /// to be reordered every time a module is dragged or removed.
    ///
    ////////////////////////////////////////////////////////////////////////////

    /// \brief Builds \p module's strip at \p slotIndex, or moves the one it
    /// already has.
    void createModuleRegion(LE::Utility::IntrusivePtr<Module> const &, std::uint8_t slotIndex);

    ModuleUI *regionFor(Module const &);
    ModuleUI *regionInSlot(std::uint8_t slotIndex);
    ModuleUI *regionInRackSlot(std::uint8_t slotIndex);

    /// \brief What the *rack* shows in \p slotIndex, which is what the user
    /// asked for; the chain may not have caught up. \see the definition.
    std::int8_t effectInRackSlot(std::uint8_t slotIndex) const;

  public:
    /// \brief Makes the rack a function of the chain: drops strips whose module
    /// has gone, builds strips for modules that have none, and puts every one of
    /// them where the chain says. \see the definition.
    void resyncModuleRack();

    /// \brief The same, on the next turn of the message loop. See the definition
    /// for why it cannot always be immediate.
    void refreshModuleRackAsync();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief How many times refreshModuleRackAsync() has been called.
    ///
    /// \note For tests, and the only seam that will do: the refresh travels as a
    /// `juce::MessageManager::MessageBase` and a headless run has no message loop
    /// to deliver it, so a test that runs the resync by hand cannot otherwise
    /// tell "asked and not delivered" from "never asked".
    ///
    ////////////////////////////////////////////////////////////////////////////
    std::uint32_t rackResyncRequests() const { return rackResyncRequests_; }

    /// \brief Draws whatever the LFOs have done since the last sweep.
    ///
    /// \note What `timerCallback()` does, at `modulationRefreshHz`. Public so a
    /// headless test can be the clock.
    void pumpModulatedValues();

    /// \brief Takes the palette again if it has moved, and repaints everything.
    ///
    /// \note Public for the same reason pumpModulatedValues() is: a headless test
    /// has to be the clock.
    void applyPaletteIfChanged();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Redraws the settings panel's engine information if the engine has
    /// caught up with what the user asked for.
    ///
    /// \returns whether it repainted, which is the seam a test has: a headless
    /// render repaints everything it is given whether or not anything marked it
    /// dirty, so a picture cannot tell "asked" from "never asked" and this can.
    ///
    /// \note Public for the same reason applyPaletteIfChanged() is.
    ///
    ////////////////////////////////////////////////////////////////////////////
    bool updateEngineInformationIfChanged();

    /// \brief What the Interface page's colour scheme box does: remembers it
    /// and hands it to the map. `[main-thread]`
    void setPalette(ColourMap::Palette);

    /// \brief Lets go of a strip's controls before it is destroyed, so that
    /// JUCE's focus handling cannot re-enter through a control that is going.
    void detachFrom(ModuleUI &);

  private:
    /// \brief Disables the LFO display and posts its destruction. Shared by
    /// moduleControlDectivated() and detachFrom().
    void retireLFODisplay();

    /// \see rackResyncRequests().
    std::uint32_t rackResyncRequests_{0};

    /// \brief The slot addUserAddedModule() wants focused once its strip exists,
    /// or noSlotAwaitingFocus. \see addUserAddedModule().
    static constexpr std::uint8_t noSlotAwaitingFocus{0xFF};
    std::uint8_t slotAwaitingFocus_{noSlotAwaitingFocus};

  public:
    /// \note Both resync the rack afterwards, because both are reached from the
    /// host's side as well as from this editor's -- `updateGUIForChangedModule`
    /// in host2PluginImpl.inl calls them when a *host* fills or empties a slot,
    /// and nothing else would then take the strip down.
    void moduleRemoved()
    {
        setLastModulePosition(nextAvailableModuleSlot_ - 1);
        refreshModuleRackAsync();
    }
    void moduleAdded()
    {
        setLastModulePosition(nextAvailableModuleSlot_ + 1);
        refreshModuleRackAsync();
    }

  public: //...mrmlj...needed at end of preset loading...
    void setLastModulePosition(std::uint8_t slotIndex);

    /// \see PanelPlacement, declared above the constructor that takes one.
    PanelPlacement panelPlacement() const { return panelPlacement_; }
    void panelPlacement(PanelPlacement);

    /// \brief Whether the panel has a column of its own rather than the module
    /// strips' rectangle: `alwaysVisible`, or `expandContract` with a panel up
    /// and a host that agreed to the resize.
    bool panelHasOwnColumn() const { return panelHasOwnColumn_; }

    ////////////////////////////////////////////////////////////////////////////
    // Where a panel goes, in either arrangement.
    ////////////////////////////////////////////////////////////////////////////

    /// \note overlayX is the module strips' right edge less overlayWidth, and
    /// the .cpp static_asserts it against ModuleUI's own constants rather than
    /// this header taking a dependency on moduleUI.hpp for three numbers.
    static constexpr unsigned short overlayWidth{287};
    static constexpr unsigned short overlayHeight{545};
    static constexpr unsigned short overlayX{543};
    static constexpr unsigned short overlayY{(artworkHeight - overlayHeight) / 2};

    /// The skin's right margin, the module strips ending at overlayX + overlayWidth.
    static constexpr unsigned short panelMargin{estimatedWidth - (overlayX + overlayWidth)};

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The editor with a column of its own for the panel, and where the
    /// two things in it go: the panel against the left edge, the skin to the
    /// right of it, each with the margin the strips already have.
    ///
    /// \note The column is on the left and the skin is what moves, so the panel a
    /// user opens the plugin for is not at the far edge of the window.
    /// \see mainArea().
    ///
    ////////////////////////////////////////////////////////////////////////////
    static constexpr unsigned short panelColumnX{panelMargin};
    static constexpr unsigned short mainAreaX{panelColumnX + overlayWidth};
    static constexpr unsigned short expandedWidth{mainAreaX + estimatedWidth};

  private:
    void newSampleFileSelected(fs::path const &);

  public:
    /// \brief One of the two sources that is not a file, which is what the sample
    /// menu's first two entries pick. Public because the menu itself is not
    /// reachable headlessly -- opening one needs a message loop -- so this is
    /// where a test stands in for a click.
    void sideChainSourceSelected(SideChainSource);

  private:
    /// \brief Parents \p panel to the editor, on top, wherever the placement puts
    /// it.
    void showPanel(juce::Component &panel);

    /// Whichever of the two panels is up, or nullptr.
    juce::Component *currentPanel();

    /// \brief Puts whichever panel is up where the placement says, and gives the
    /// editor the width that needs. The one place the three modes differ.
    void layOutPanels();

    /// \brief Grows the editor by a panel column or gives it back, asking the
    /// host to follow. \return whether the panel gets a column of its own.
    bool setPanelColumnVisible(bool wanted);

    /// \brief Drops both panels and un-toggles both buttons -- and, in
    /// `alwaysVisible`, opens the resting one in their place.
    void hidePanels();

    /// \brief What `alwaysVisible` leaves in the column when the user has asked
    /// for nothing: the preset browser. \see the definition.
    void openRestingPanel();

    /// \brief The same, with the panel this session was last left on rather than
    /// the browser. \see the definition, which has why they are two functions.
    void openRememberedPanel();

    void updateSettings();

    void updateMainKnobs();

  private:
    /// \brief Which ColourMap::generation() this editor was last painted for.
    ///
    /// \note Seeded with the current one rather than with zero, so that an
    /// editor opened into a session that has already changed palette does not
    /// spend its first tick reloading colours it was just built with.
    std::uint32_t palette_{ColourMap::generation()};

  private: // JUCE Component overrides.
    /// \note Only what is outside the skin: the panel column's chrome. The skin
    /// itself is MainArea's. \see the definition.
    void paint(juce::Graphics &) override;
    void parentHierarchyChanged() override;

  private: // JUCE ButtonListener overrides.
    void buttonClicked(juce::Button *) override;

  private: // juce::Timer
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Sweeps the modulated-value mailbox and repaints what moved.
    ///
    ///   Thirty times a second, which is what a knob animation needs and about
    /// one fiftieth of the rate the audio thread produces those values at. The
    /// mailbox coalesces, so a slow sweep costs nothing but smoothness.
    ///
    /// \note The audio thread writes the mailbox and this reads it, so the thread
    /// that draws a knob is the one allowed to.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void timerCallback() override;

    static constexpr int modulationRefreshHz{30};

  public:
    /// \brief What the add-module menu calls when an entry is chosen.
    ///
    /// \note Public so that sw-show-ui can drive it: adding a module is five
    /// steps -- create, build the region, take focus, select, notify -- and only
    /// the first two are reachable through the module chain.
    void addUserAddedModule(std::uint8_t effectIndex);

    /// \brief togglePresetBrowser() with the button taken out of it.
    ///
    /// \note Public for the same reason as addUserAddedModule(): the presets
    /// button is private and its handler recovers the editor *from* the button,
    /// neither of which a headless render has.
    void showPresetBrowser(bool show);

    /// \brief The browser, or nothing when the panel is not showing one. Public
    /// for the same reason showPresetBrowser() is. \see issue #177.
    PresetBrowser *presetBrowser() { return presetBrowser_ ? &*presetBrowser_ : nullptr; }

    /// \brief One of the timer's steps: the Save buttons follow a flag any
    /// parameter write can set. Public for the same reason
    /// updateEngineInformationIfChanged() is -- juce::Timer is a private base, so
    /// this is how a headless run drives a tick. \see issue #177.
    void updateSaveButtonsIfShowing();

    /// \brief Opens the browser on a factory bank, as double-clicking into one
    /// does. `tools/show-ui` only; see showPresetBrowser().
    void showFactoryBank(juce::String const &bank);

    /// \brief Opens the settings panel on \p pageIndexToActivate, as clicking
    /// the logo does. Public for the same reason showPresetBrowser() is: the
    /// button is private and its handler recovers the editor from it.
    void showSettings(unsigned int pageIndexToActivate);

    /// \brief Opens it on whichever tab this session was last left on, which is
    /// what the settings button does. \see EditorHost::settingsPage().
    void showSettings();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Shows this editor at \p zoomPercent and remembers the choice.
    ///
    ///   Three things in one call, and they have to happen in this order:
    /// the preference is written first, because it is what everything else
    /// reads; then the wrapper is re-transformed and re-sized; then the host is
    /// asked for a window that matches.
    ///
    /// \note This editor, not every open one. The preference is process-wide and
    /// a second instance picks it up when its editor is next built, an editor
    /// reading these once, when it is made.
    ///
    /// \note A bare editor -- what the tests and a 1:1 render build -- has no
    /// ZoomedEditor over it, so there is nothing to transform; the preference is
    /// still written.
    ///
    /// \note A host that refuses the resize leaves its window at the old size
    /// with the editor drawn at the new scale until it is next opened, at which
    /// point `guiGetSize` reports the zoomed size and it comes right. Refusing to
    /// zoom instead would leave the user a control that silently does nothing.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void setZoom(unsigned int zoomPercent);

  private:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Asks for \p effectIndex in \p slotIndex.
    ///
    /// \note An ask rather than the change itself: the chain belongs to the audio
    /// thread, so this builds the module here and hands it over, and the strip
    /// follows when the engine reports the chain changed.
    ///
    /// \note Which is why no module comes back. A caller that needs to do
    /// something to the one it asked for -- addUserAddedModule() takes focus on
    /// it -- says so through slotAwaitingFocus_, and resyncModuleRack() answers.
    ///
    ////////////////////////////////////////////////////////////////////////////
    /// \return whether the module could be built at all -- an effect this build
    /// does not have is still a synchronous failure.
    bool setModuleInSlot(std::uint8_t slotIndex, std::int8_t effectIndex);

    void setActiveModuleName(juce::String const &newName);
    void setActiveControlName(juce::String const &newName);
    void setActiveControlValue(juce::String const &newValue);

    void updateSampleName(juce::String const &);
    void setSampleLoadingStatus();

    void setDefaultFocusHandling();

    static void togglePresetBrowser(juce::Button const &);

  private:
    enum String
    {
        activeModuleName = 0,
        activeModuleDescription,
        activeControlName = activeModuleDescription,
        activeControlValue,
        currentSampleName,

        numberOfStrings
    };

    juce::String &string(String const stringID) { return strings_[stringID]; }

    void updateString(String, unsigned int stringVerticalOffset, unsigned int stringHeight,
                      juce::String const &);

  public:
    /// \brief What the eject button calls. Public for the same reason
    /// addUserAddedModule() is: removing a module is five steps -- shuffle the
    /// strips left, decrement the slot marker, empty the slot, tell the host, drop
    /// the strip -- and only the middle one is reachable through the chain.
    void removeModule(ModuleUI &);

  private:
    friend class ModuleUI;
    void moduleDrag(ModuleUI &, juce::MouseEvent const &);
    void moduleDragEnd(ModuleUI &, juce::MouseEvent const &);

  public:
    /// \note Public for the same reason regionInSlot() is: a headless run
    /// presses what is on it. \see issue #93.
    SharedModuleControls &sharedModuleControls() { return *sharedModuleControls_; }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The skin, and every widget laid out in its pixels.
    ///
    ///   Everything but a panel is parented here rather than to the editor, so
    /// the skin's 845 x 564 coordinate system is this component's and no offset in
    /// it has to know whether there is a column. The editor slides this right by
    /// mainAreaX when there is one and paints the column's chrome itself.
    ///
    ////////////////////////////////////////////////////////////////////////////

    class MainArea final : public WidgetBase<>
    {
      public:
        MainArea();

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \brief The logo's hit area, in this component's coordinates -- which
        /// are the skin's.
        ///
        /// \note Named rather than a literal inside `mouseDown()` because
        /// overlayPanelTests.cpp clicks the centre of it, so redrawing the skin
        /// costs one edit here rather than two.
        ///
        /// \note A function rather than a constant because juce::Rectangle's
        /// constructor is not constexpr, and a namespace-scope object would be
        /// runtime-initialised for no reason.
        ///
        ////////////////////////////////////////////////////////////////////////

        static juce::Rectangle<int> logoArea() { return {12, 290, 51, 63}; }

      private: // JUCE Component overrides.
        void paint(juce::Graphics &) override;
        /// \brief The logo, which opens the About page. \see the definition.
        void mouseDown(juce::MouseEvent const &) override;

      private:
        SpectrumWorxEditor &editor();
        SpectrumWorxEditor const &editor() const;
    }; // class MainArea

    /// \brief Where every widget but a panel lives. \see MainArea.
    ///
    /// \note Public because a strip, the shared controls and the LFO display are
    /// all built outside this class and parent themselves to it -- and because a
    /// test that means to click the skin has to click this rather than the editor.
    MainArea &mainArea() { return mainArea_; }
    MainArea const &mainArea() const { return mainArea_; }

  private:
    ////////////////////////////////////////////////////////////////////////////
    /// \internal
    /// \class ModuleMenuButton
    ////////////////////////////////////////////////////////////////////////////

    class ModuleMenuButton final : public ArrowButton
    {
      public:
        ModuleMenuButton(SpectrumWorxEditor &parent);
        void moveToSlot(std::uint8_t slotIndex);

      private: // JUCE component overrides.
        void clicked() override;
    }; // class ModuleMenuButton

    ////////////////////////////////////////////////////////////////////////////
    /// \internal
    /// \class DropIndicator
    ///
    /// \brief Where a dragged strip would land: over the strip it would change
    /// places with, or in the gap it would be inserted into.
    ///
    /// \note One component for both, because only one of the two can be showing
    /// and because the two are answers to the same question -- which is asked
    /// once, by moduleDropAt(), and drawn here.
    ///
    ////////////////////////////////////////////////////////////////////////////

    class DropIndicator final : public WidgetBase<>
    {
      public:
        explicit DropIndicator(juce::Component &parent);

        /// \brief Over the strip in \p slotIndex, which a drop would exchange the
        /// dragged one with.
        void showSwap(std::uint8_t slotIndex);

        /// \brief In the gap before \p gapIndex, 0 to maxNumberOfModules, which a
        /// drop would shift the strips apart at.
        void showInsert(std::uint8_t gapIndex);

        void hide() { setInvisible(); }

        /// The insert line's width -- even, so that it straddles the gap -- and
        /// how far past the strips it runs at each end. \see showInsert().
        static constexpr int lineWidth{9};
        static constexpr int lineOverrun{8};

      private: // JUCE component overrides.
        void paint(juce::Graphics &) override;

      private:
        bool insert_{false};
    }; // class DropIndicator

  public:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief What letting go of a dragged strip somewhere would do.
    ///
    /// \note A value rather than a state the drag keeps, so the question is asked
    /// the same way twice: moduleDrag() asks it to draw the indicator and
    /// moduleDragEnd() asks it again to act. What the user is shown and what
    /// happens are then the same function of the same point.
    ///
    ////////////////////////////////////////////////////////////////////////////

    struct ModuleDrop
    {
        enum Action : std::uint8_t
        {
            /// Outside the drop zone, or a drop that would not move anything.
            nothing,
            /// Exchange the dragged strip with the one in `slot`.
            swap,
            /// Put the dragged strip in the gap `slot`, shifting the rest along.
            insert
        }; // enum Action

        Action action{nothing};

        /// A slot index for `swap`; a gap index, 0 to the number of strips, for
        /// `insert`.
        std::uint8_t slot{0};
    }; // struct ModuleDrop

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Where the strip a drag is carrying would be: \p pointer, in the
    /// skin's coordinates, less where in the strip it was \p grabbedAt, plus half
    /// a strip.
    ///
    ///   Which is the column the eject `X` is drawn in, that marker being centred
    /// on the strip -- and it is what a drop is aimed with rather than the
    /// pointer. \see the definition.
    ///
    /// \note Public and static so that a test can compose it with moduleDropAt():
    /// the two agreeing is a question about the pair rather than about either.
    ///
    ////////////////////////////////////////////////////////////////////////////
    static juce::Point<int> draggedStripCentre(juce::Point<int> pointer,
                                               juce::Point<int> grabbedAt);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief What a drop with the dragged strip's middle at \p position -- in
    /// the skin's coordinates, which are mainArea()'s -- would do to the strip in
    /// \p sourceSlot. \see draggedStripCentre() for what \p position is.
    ///
    /// \note Public so that a test can ask it: driving this by mouse needs a real
    /// drag, which needs a window and a `MouseInputSource` that a synthesised
    /// event does not touch.
    ///
    ////////////////////////////////////////////////////////////////////////////
    ModuleDrop moduleDropAt(std::uint8_t sourceSlot, juce::Point<int> position) const;

    /// \brief Shows what \p drop would do -- a filled target strip for a swap, a
    /// line in a gap for an insert -- or takes the indication away.
    ///
    /// \note Public for the same reason as moduleDropAt(): the only other way to
    /// this is a live JUCE drag.
    void showModuleDrop(ModuleDrop drop);

    /// \brief Carries \p drop out on the strip in \p sourceSlot: the rack first,
    /// then the engine, then the host. Public for the same reason as
    /// moduleDropAt(). \see the definition.
    void applyModuleDrop(std::uint8_t sourceSlot, ModuleDrop drop);

  private:
    /// \brief The above, with the two coordinate conversions a mouse event needs.
    juce::Point<int> draggedStripCentre(ModuleUI const &, juce::MouseEvent const &) const;

    /// \brief Exchanges the strips in two slots, which is two moves. \see the
    /// definition.
    void swapModuleSlots(std::uint8_t a, std::uint8_t b);

    /// \brief Moves the strip in \p from to \p to, shifting everything between.
    void moveModuleSlot(std::uint8_t from, std::uint8_t to);

  public:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief What choosing an effect from the right-click menu would do, which
    /// depends on where the menu was opened. \see effectMenuTargetAt().
    ///
    ////////////////////////////////////////////////////////////////////////////

    struct EffectMenuTarget
    {
        enum Action : std::uint8_t
        {
            /// Somewhere the menu is not offered at all.
            none,
            /// Take over `slot`, keeping the rack the length it is.
            replace,
            /// Go into the gap `slot`, shifting the rest along.
            insert,
            /// Go on the end, which is what the add-module button does.
            append
        }; // enum Action

        Action action{none};

        /// A slot index for `replace`; a gap index for `insert`; unused for the
        /// other two.
        std::uint8_t slot{0};
    }; // struct EffectMenuTarget

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief What a right-click at \p position -- in the skin's coordinates,
    /// which are mainArea()'s -- offers to do. \see the definition.
    ///
    /// \note Public for the same reason moduleDropAt() is: which of three things
    /// a point in the rack means is geometry, and a headless run can ask it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    EffectMenuTarget effectMenuTargetAt(juce::Point<int> position) const;

    /// \brief What the menu is headed with, which is how the user is told which
    /// of the three they are about to do -- and, for a replacement, which strip
    /// it would be done to. \see the definition.
    juce::String effectMenuHeader(EffectMenuTarget) const;

    /// \brief Opens the effect menu for whatever is under \p screenPosition, or
    /// nothing if that is not somewhere it is offered.
    void showEffectMenuAt(juce::Point<int> screenPosition);

    /// \brief Puts \p effectIndex where \p target says. Public for the same
    /// reason applyModuleDrop() is: the only other way here is an open menu.
    void applyEffectMenuChoice(EffectMenuTarget target, std::uint8_t effectIndex);

  private:
    /// \brief The callback that hands what was chosen to applyEffectMenuChoice(),
    /// shared by the two places the menu is opened from.
    PopupMenu::OnChosen effectMenuCallback(EffectMenuTarget target);

    /// \brief Swaps the effect in \p slot for \p effectIndex, in place.
    void replaceModuleInSlot(std::uint8_t slot, std::uint8_t effectIndex);

    /// \brief Puts \p effectIndex in the gap \p gap. \see the definition.
    void insertModuleAtGap(std::uint8_t gap, std::uint8_t effectIndex);

  public:
    ////////////////////////////////////////////////////////////////////////////
    /// \internal
    /// \class SampleArea
    ///
    /// \brief The "Sidechain Source" strip: what the side channel is being fed
    /// from, and the click that changes it.
    ////////////////////////////////////////////////////////////////////////////

    class SampleArea : public WidgetBase
    {
      public:
        SampleArea();

      private: // JUCE Component overrides.
        void mouseUp(juce::MouseEvent const &) override;

      private:
        SpectrumWorxEditor &editor();

        void browseForFile();

        /// \note Outlives the async file dialog it launches.
        std::unique_ptr<juce::FileChooser> fileChooser_;

        /// \note Likewise the async menu: showCenteredBelow() returns before the
        /// user has chosen, and the items have to still be there when they do.
        PopupMenu menu_;
    }; // class SampleArea

    ////////////////////////////////////////////////////////////////////////////
    /// \internal
    /// \class LFODisplay
    ////////////////////////////////////////////////////////////////////////////

    class LFODisplay : public WidgetBase,
                       private juce::Button::Listener,
                       private juce::Slider::Listener
    {
      public: //...mrmlj...
        using AsyncSlider = HorizontalSlider;
        using LFO = LE::Parameters::LFOImpl;

        ////////////////////////////////////////////////////////////////////////
        /// \class ParameterSlider
        ///
        /// \brief One of the LFO's own parameters, with the right button's menu
        /// on it -- the same four sections a knob raises. \see issue #93.
        ////////////////////////////////////////////////////////////////////////

        class ParameterSlider : public AsyncSlider, public ParameterMenu
        {
          public:
            ParameterSlider(LFODisplay &parent, std::uint8_t lfoParameterIndex);

            ////////////////////////////////////////////////////////////////////
            ///
            /// \name What this slider stands for
            ///
            ///   Public for the reason ModuleControlBase's own four are: which
            /// parameter a widget is currently speaking for, what it reads as and
            /// what typing into it means are questions about the widget rather
            /// than about the menu that happens to ask them.
            ///
            ////////////////////////////////////////////////////////////////////
            ///@{
            /// \brief The range slider carries two and answers with the thumb the
            /// press was nearest.
            virtual std::uint8_t lfoParameterIndex() const { return lfoParameterIndex_; }

            /// \brief Where a press lands the menu, without raising one: the
            /// step mouseDown() takes before it does. Public for the same reason
            /// effectMenuTargetAt() is -- which of two parameters a point in the
            /// widget means is geometry, and a headless run can ask it.
            void notePressAt(float const position) { pressPosition_ = position; }

            juce::String parameterName() const override;
            juce::String parameterValueText() const override;
            ParameterID parameterID() const override;
            bool setParameterFromText(juce::String const &) override;
            void setParameterToDefault() override;
            ///@}

          protected:
            LFODisplay &parent() { return parent_; }
            LFODisplay const &parent() const { return parent_; }

            /// Where the last press landed, which is what that thumb is chosen by.
            float pressPosition() const { return pressPosition_; }

          private: // JUCE component overrides.
            void mouseDown(juce::MouseEvent const &) override;
            void mouseDrag(juce::MouseEvent const &) override;

          private: // ParameterMenu
            juce::Component &menuOwner() override { return *this; }

          private:
            LFODisplay &parent_;
            std::uint8_t const lfoParameterIndex_;
            float pressPosition_{0};
        }; // class ParameterSlider

        /// The LFO's two bounds on one slider.
        class RangeSlider final : public ParameterSlider
        {
          public:
            explicit RangeSlider(LFODisplay &parent);

          public:
            std::uint8_t lfoParameterIndex() const override;
        }; // class RangeSlider

        class Period final : public ParameterSlider
        {
          public:
            explicit Period(LFODisplay &parent);

            double milliseconds() const;

            LFO::SyncType lastSyncType() const { return lastSyncType_; }

          private: // JUCE component overrides.
            friend class LFODisplay;
            double snapValue(double attemptedValue, DragMode) override;

          private:
            LFO::SyncType lastSyncType_;
        }; // class Period

      public:
        LFODisplay();
        ~LFODisplay();

        void setupForControl(ModuleControlBase &, double minimum, double maximum, double interval);

        /// \brief Whether this strip is showing \p control.
        bool isFor(ModuleControlBase const &control) const { return pModuleControl_ == &control; }

        /// \brief Puts the switch back in step with the LFO, for a change made
        /// somewhere other than by pressing it. \see setLFOEnabled().
        void resyncEnabledSwitch();

        void updateForNewTimingInfo();
        void updateForChangedParameters(ModuleUI const &, std::uint8_t parameterIndex,
                                        std::uint8_t lfoParameterIndex,
                                        /*Plugins::AutomatedParameterValue*/ float);

        LFO const &lfo() const { return const_cast<LFODisplay &>(*this).lfo(); }
        ModuleControlBase const &control() const
        {
            return const_cast<LFODisplay &>(*this).control();
        }
        Period const &period() const { return period_; }

        /// The three sliders, for a case that presses one. \see issue #93.
        Period &period() { return period_; }
        ParameterSlider &phase() { return phase_; }
        RangeSlider &range() { return range_; }

      private: // JUCE component overrides.
        void paint(juce::Graphics &) override;

        void buttonClicked(juce::Button *) override;
        void sliderValueChanged(juce::Slider *) noexcept override;

      private:
        void updateAllControls();
        void updateAutomatableControls();
        void updatePeriodControl();
        void updateRangeControl();
        void updateSnapControls();
        void updateLFOAndHostFromPeriodControl();

        void automatedParameterChanged(std::uint8_t lfoParameterIndex, float parameterValue) const;

        /// \brief One LFO parameter, queued rather than written.
        ///
        /// \note The two past `lfoExportedParameters` -- Waveform and SyncTypes
        /// -- have no ParameterID and so no route through the parameter queue.
        /// They take `ToEngine::SetUnexportedLFOParameter` instead, addressed by
        /// index. **Everything that edits an LFO has to come through here**, or
        /// the edit reaches the main thread's copy and never the engine.
        template <class LFOParameter, typename T>
        void updateParameterAndNotifyHost(T const widgetValue)
        {
            using namespace LE::Parameters;
            using value_type = typename LFOParameter::value_type;
            auto const parameterValue(Math::convert<value_type>(widgetValue));
            auto const parameterIndex(IndexOf<LFO::Parameters, LFOParameter>::value);
            auto const internalValue(Math::convert<float>(parameterValue));

            //...mrmlj...fmod/separated DSP-GUI...
            if (parameterIndex >= ParameterCounts::lfoExportedParameters)
            {
                // both copies, as everywhere else: lfo() is the main thread's
                // own module, and the queue leg is what the engine hears. The
                // host is told nothing, there being no ParameterID for it
                lfo().parameters().set<LFOParameter>(parameterValue);
                queueUnexportedLFOParameter(parameterIndex, internalValue);
                return;
            }

            queueLFOParameter(parameterIndex, internalValue);
            automatedParameterChanged(parameterIndex, internalValue);
        }

        /// \brief The queued half of the above, non-template so that this header
        /// does not need the protocol.
        void queueLFOParameter(std::uint8_t lfoParameterIndex, float value) const;

        /// \brief The same for the two an LFO does not export, which have no
        /// ParameterID and so travel by index. \see ToEngine::SetUnexportedLFOParameter
        void queueUnexportedLFOParameter(std::uint8_t lfoParameterIndex, float value) const;

        void verifyGUIAndLFOConsistency() const;

        std::uint8_t moduleIndex() const;

        SpectrumWorxEditor &editor();
        SpectrumWorxEditor const &editor() const;

        LE_NOINLINE ModuleControlBase::LFO &lfo() { return control().lfo(); }
        ModuleControlBase &control()
        {
            LE_ASSERT(pModuleControl_);
            return *pModuleControl_;
        }

      public:
        /// \brief Whether this display is set up for a control in \p region.
        /// \see ModuleControlBase::pointsInto().
        bool pointsInto(ModuleUI const &region) const
        {
            return pModuleControl_ && pModuleControl_->pointsInto(region);
        }

      private:
        CapsuleButton switch_;
        TextButton quarter_;
        TextButton triplet_;
        TextButton dotted_;
        ArrowButton typeArrow_;
        Period period_;
        ParameterSlider phase_;
        RangeSlider range_;

        PopupMenuWithSelection type_;

        ModuleControlBase *pModuleControl_;

        static unsigned int const width = 174;

        typedef juce::Component LFODisplay::*ComponentPtr;
        static ComponentPtr const componentsToDisableKeyboardGrabingFor[];
    }; // class LFODisplay

  public:
    /// \brief The LFO strip, or nothing when no control is selected. Public for
    /// the same reason regionInSlot() is: a headless run can press what is on it.
    LFODisplay *lfoDisplay() { return lfoDisplay_ ? &*lfoDisplay_ : nullptr; }

    ////////////////////////////////////////////////////////////////////////////
    /// \internal
    /// \class Settings
    ////////////////////////////////////////////////////////////////////////////

    class Settings : public juce::TabbedComponent, private juce::Button::Listener
    {
      public:
        Settings();
        ~Settings();

        void updateEnginePage();

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \brief Redraws the four engine information lines if what they say has
        /// moved.
        ///
        /// \note Polled rather than pushed: the numbers come out of
        /// `Engine::Setup`, which is rebuilt on whichever thread owns the engine
        /// some time after the user picks an FFT size, and nothing marks a pixel
        /// of this page dirty when it happens.
        ///
        ////////////////////////////////////////////////////////////////////////

        /// \returns whether it repainted.
        bool updateEngineInformation();

        static void comboBoxValueChanged(ComboBox const &);

        SpectrumWorxEditor &editor();

      private:
        void refillFrameSize(Engine::Setup const &);

      private: // JUCE component overrides.
        /// \note The strip the tabs stand in, and nothing else -- the pages are
        /// children and paint themselves. \see PanelPainter::paintTabStrip().
        void paint(juce::Graphics &graphics) override
        {
            PanelPainter::paintTabStrip(graphics, getLocalBounds().toFloat());
        }

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \brief TabbedComponent's, and then the tab bar put where the panel
        /// wants it rather than in its corner.
        ///
        ///   A tab lines up with the frame below it -- PanelPainter::fieldInset
        /// is the line both stand on -- and the row of them is centred in the
        /// dark between the top of the panel and the top of that frame. JUCE
        /// puts the bar flush against the top left, which is a pixel in from an
        /// edge nothing else on either panel measures from. \see issue #134.
        ///
        /// \note And in front of the page, which it overlaps: `changeCallback`
        /// brings the page forward every time a tab is pressed, so this is the
        /// only place the order can be settled.
        ///
        ////////////////////////////////////////////////////////////////////////
        void resized() override
        {
            juce::TabbedComponent::resized();

            auto const strip(ButtonStyle::tabHeight + PanelPainter::settingsFrameTop);
            auto const left(juce::roundToInt(PanelPainter::fieldInset - ButtonStyle::tabSideInset));
            auto const top(juce::roundToInt((strip - ButtonStyle::tabHeight) / 2));

            auto &bar(getTabbedButtonBar());
            bar.setBounds(left, top, getWidth() - left, ButtonStyle::tabHeight);
            bar.toFront(false);
        }

        juce::TabBarButton *createTabButton(juce::String const &tabName, int tabIndex) override;

        /// \note Every route to a tab goes through here -- a press on the bar,
        /// and setCurrentTabIndex() whether or not it was asked to send the
        /// change message -- so it is the one place the session's answer can be
        /// written.
        void currentTabChanged(int newCurrentTabIndex, juce::String const &newTabName) override;

      private: // JUCE ButtonListener overrides.
        void buttonClicked(juce::Button *) override;

      private:
        class EnginePage : public PanelBackground
        {
          public:
            EnginePage();

            /// \brief Rebuilds the four lines under the combo boxes from \p setup.
            /// \returns whether any of them changed, which is what says whether
            /// the page needs repainting. \see Settings::updateEngineInformation().
            bool setEngineInformation(Engine::Setup const &setup);

          private: // JUCE component overrides.
            void paint(juce::Graphics &) override;

          private:
            juce::String engineQuality_;
            juce::String frequencyResolution_;
            juce::String timeResolution_;
            juce::String latency_;
        }; // class EnginePage

        class InterfacePage : public PanelBackground
        {
          public:
            InterfacePage();

            TitledComboBox const &paletteComboBox() const { return palette_; }
            TitledComboBox const &mouseOverComboBox() const { return moduleUIMouseOverReaction_; }
            TitledComboBox const &lfoUpdateComboBox() const { return lfoUpdateBehaviour_; }

          private: // JUCE component overrides.
            void paint(juce::Graphics &) override;

          public:
            TitledComboBox const &zoomComboBox() const { return zoom_; }

          private:
            friend class Settings;
            /// \note Zoom first and the colour scheme under it, those being the
            /// two a user reaches for. Each control draws its own title, so the
            /// order is a layout decision rather than an artwork one.
            TitledComboBox zoom_;
            TitledComboBox palette_;
            TitledComboBox moduleUIMouseOverReaction_;
            TitledComboBox lfoUpdateBehaviour_;
            LEDTextButton hideCursorOnKnobDrag_;
        }; // class InterfacePage

        EnginePage enginePage_;
        InterfacePage interfacePage_;
        AboutPage aboutPage_;

        DiscreteParameterComboBox fftSize_;
        DiscreteParameterComboBox overlapFactor_;
        DiscreteParameterComboBox windowFunction_;

      public:
        static std::uint16_t const xMargin = 30;
        static std::uint16_t const yMargin = 30;
        static std::uint16_t const yStep = 68;
    }; // class Settings

    /// Tab indices into Settings, in addTab() order.
    enum SettingsPage : unsigned int
    {
        enginePageIndex = 0,
        interfacePageIndex,
        aboutPageIndex,
        numberOfSettingsPages
    };

  private:
    friend class ModuleControlBase;
    /// \note Written by ModuleUI::activate()/deactivate() and by
    /// ModuleControlBase::report{Active,Inactive}Control(), and nowhere else.
    ModuleUI *pSelectedModule_{nullptr};
    ModuleControlBase *pActiveControl_{nullptr};

  private:
    /// \note First member, and a reference: everything below is built in the
    /// constructor body and reaches through it.
    EditorHost &editorHost_;

    /// \note Second, because the constructor reads it: an alwaysVisible editor
    /// takes its column before anything below is built. panelHasOwnColumn_ is not
    /// derivable from it -- `expandContract` with no panel up and `expandContract`
    /// against a host that refused the resize are both "no column", and only the
    /// second says so.
    PanelPlacement panelPlacement_;
    bool panelHasOwnColumn_{false};

    std::uint8_t nextAvailableModuleSlot_;

    /// \note Before every widget below, all of which parent themselves to it.
    MainArea mainArea_;

    EditorKnob in_, out_, mix_;

    /// \note Not const: its heading is set per use. \see menuWithHeader().
    ModuleMenuHolder moduleMenu_;
    ModuleMenuButton moduleMenuButton_;
    DropIndicator dropIndicator_;

    SampleArea sampleArea_;

    PaintedButton preset_;
    PaintedButton settingsButton_;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief "Ignore external audio": whether a preset gets to bring an audio
    /// file with it, in either direction.
    ///
    /// \note It says something about the sidechain source, so it sits beside the
    /// sidechain source -- and here it outlives the preset browser, which is
    /// built and destroyed with the panel. \see issue #44.
    ///
    ////////////////////////////////////////////////////////////////////////////

    GlyphButton ignoreExternalSample_;

    // Optional/auxiliary components
    friend class SharedModuleControls;
    std::optional<SharedModuleControls> sharedModuleControls_;
    std::optional<LFODisplay> lfoDisplay_;
    std::optional<PresetBrowser> presetBrowser_;
    std::optional<Settings> settings_;

    /// \note Deliberately not indexed by slot; see createModuleRegion().
    std::array<std::unique_ptr<ModuleUI>, SW::Constants::maxNumberOfModules> moduleRegions_;

    std::array<juce::String, numberOfStrings> strings_;

}; // class SpectrumWorxEditor

} // namespace GUI

} // namespace LE::SW

#endif // spectrumWorxEditor_hpp
