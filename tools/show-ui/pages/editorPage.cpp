////////////////////////////////////////////////////////////////////////////////
///
/// \file editorPage.cpp
/// --------------------
///
///   The whole editor, with an engine under it and no plugin.
///
///   This is the page that matters: everything else here shows a layer, and
/// this shows whether the layers add up. The editor's constructor builds three
/// knobs, a module menu, a gradient, the slot widgets and whatever the module
/// chain already holds, and any one of those can assert or throw. Rendering it
/// offscreen catches that without a DAW, which is the only way to catch it at
/// all in a sandbox with no window server.
///
/// \note The EditorHost below is the honest minimum: a SpectrumWorxCore for the
/// engine half and a Plugin2HostInteropControler whose notifications go
/// nowhere, because there is no host to notify.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "../page.hpp"

#include "core/host_interop/plugin2Host.hpp"
#include "core/host_interop/programWrite.hpp"
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/threading/publish.hpp"

#include "le/plugins/clap/tag.hpp"
#include "core/spectrumWorxCore.hpp"
#include "gui/editor/editorHost.hpp"
#include "core/automatedModuleChain.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/factoryPresets.hpp"
#include "gui/editor/spectrumWorxEditor.hpp"
#include "gui/editor/zoomedEditor.hpp"
#include "gui/theme.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace LE;
using namespace LE::SW;

/// The engine, which cannot be instantiated on its own -- SpectrumWorxCore's
/// constructor is protected and Engine::Processor downcasts to it.
class HarnessEngine final : public SpectrumWorxCore
{
  public:
    /// \param sampleRate zero to leave the engine unset up, which is the state a
    /// plugin is in between its constructor and activate() -- and the state a
    /// standalone restores a session in. A knob whose range quantises to a step
    /// time or a bin width has neither to work from then; see
    /// ModuleKnob::updateForEngineSetupChanges.
    explicit HarnessEngine(float const sampleRate)
    {
        setProgram(program_);
        if (sampleRate <= 0)
            return;

        setNumberOfChannels(2, 2);
        setSampleRate(sampleRate);
        setBlockSize(512);
        initialise();
    }

  private:
    Program program_;
}; // class HarnessEngine

/// \note Every one of these is a notification travelling plugin -> host, and
/// there is no host. They are pure virtual rather than optional because a real
/// plugin format needs all of them; see spectrumWorxCLAP.hpp for the CLAP ones.
class SilentNotifications final : public Plugin2HostInteropControler
{
  private:
    void automatedParameterBeginEdit(ParameterID) const override {}
    void automatedParameterEndEdit(ParameterID) const override {}
    void gestureBegin(char const *) const override {}
    void gestureEnd() const override {}
    void automatedParameterChanged(ParameterID, ParameterValueForAutomation) const override {}
    void moduleChanged(std::uint8_t, Module const *) const override {}
    bool parameterListChanged() const override { return true; }
    void presetChangeBegin() const override {}
    void presetChangeEnd() const override {}
    bool latencyChanged() override { return true; }

}; // class SilentNotifications

class HarnessHost final : public GUI::EditorHost
{
  public:
    explicit HarnessHost(float const sampleRate) : engine_(sampleRate) {}

    SpectrumWorxCore &core() override { return engine_; }
    Plugin2HostInteropControler &automation() override { return notifications_; }

    /// \note One Program, because this harness never activates anything: with
    /// nothing processing the main thread owns the engine outright, which is what
    /// the second copy exists to avoid needing. \see EditorHost::programMain().
    Program &programMain() override { return engine_.program(); }

    void editParameter(ParameterID const parameterID, float const value) const override
    {
        setParameterIn<LE::Plugins::Protocol::CLAP>(const_cast<HarnessEngine &>(engine_).program(),
                                                    parameterID, value);
        toEngine_.push(Threading::setBaseParameter(parameterID.binaryValue, value));
    }

    bool editSlot(std::uint8_t const slot, std::int8_t const effectIndex) override
    {
        auto *const pModule(Threading::createModuleForSlot(engine_, effectIndex, slot));
        if ((effectIndex != AutomatedModuleChain::noModule) && !pModule)
            return false;
        Threading::publishSlot(engine_, toEngine_, slot, effectIndex, pModule);
        return true;
    }

    void editModuleMove(std::uint8_t const from, std::uint8_t const to) override
    {
        Threading::publishModuleMove(engine_, toEngine_, from, to);
    }

    /// \note Unchecked, like everything else here: nothing drains this queue and
    /// the page is a still image. The plugin's own implementation counts a
    /// refusal -- see `SpectrumWorxCLAP::pushed()`.
    void publishUnexportedLFOParameter(std::uint8_t const moduleIndex,
                                       std::uint8_t const moduleParameterIndex,
                                       std::uint8_t const lfoParameterIndex,
                                       float const value) override
    {
        toEngine_.push(Threading::setUnexportedLFOParameter(moduleIndex, moduleParameterIndex,
                                                            lfoParameterIndex, value));
    }

    /// \note Real, and nobody drains them: this harness renders a still image, so
    /// whatever the editor asks the engine for stays in the queue.
    Threading::ToEngineQueue &toEngine() const override { return toEngine_; }
    Threading::ValueMailbox const &modulatedValues() const override { return values_; }

    /// \note Nothing: there is no CLAP host behind this harness, which is the
    /// same situation as a host without `clap_host_context_menu`.
    void addHostParameterEntries(ParameterID, juce::PopupMenu &) const override {}

    void editorOpened(GUI::SpectrumWorxEditor &) override {}
    void editorClosed(GUI::SpectrumWorxEditor &) override {}

    /// \note Granted, as a host that honours `request_resize` does, so that this
    /// page renders the arrangement a user gets rather than the fallback. The
    /// page follows the editor's size; there is no window under either.
    bool requestEditorSize(int, int) override { return true; }

    /// \note The external audio file, declined: the harness has no engine to
    /// feed one to and renders a still image, so the sample area draws its empty
    /// state. Loading one is what sampleTests.cpp covers.
    fs::path currentSampleFile() const override { return {}; }
    char const *setNewSample(fs::path const &) override { return nullptr; }
    bool isSampleLoadInProgress() const override { return false; }
    void registerSampleLoadedListener(GUI::SpectrumWorxEditor &) override {}
    void deregisterSampleLoadedListener(GUI::SpectrumWorxEditor const &) override {}

    bool completelyDisableIOChanges() const override { return false; }

  private:
    HarnessEngine engine_;
    SilentNotifications notifications_;
    mutable Threading::ToEngineQueue toEngine_;
    Threading::ValueMailbox values_;
}; // class HarnessHost

/// Owns the host so that it outlives the editor reaching into it.
class EditorPage final : public juce::Component
{
  public:
    /// \param sampleRate zero for an engine that has not been set up, which is
    /// what a session restored before activate() sees. See HarnessEngine.
    /// \param openPresetBrowser opens the browser as the presets button does.
    /// \param settingsPage a tab index to open the settings panel on, or -1.
    EditorPage(bool const withModuleInFirstSlot, float const sampleRate,
               bool const openPresetBrowser = false, int const settingsPage = -1)
        : host_(sampleRate)
    {
        /// \note No setLookAndFeel() here, unlike the other pages: the editor's
        /// own ReferenceCountedGUIInitializationGuard makes Theme the default
        /// LookAndFeel (gui.cpp), and a second reference to it outlives that
        /// guard -- which JUCE asserts on when the singleton goes.
        /// \note Wrapped, so this page shows the editor at the size the
        /// plugin does. The zoom lives in ZoomedEditor rather than in the
        /// editor, so the test harness -- which builds a bare
        /// SpectrumWorxEditor -- still renders at 1:1.
        zoomed_ = std::make_unique<GUI::ZoomedEditor>(
            std::make_unique<GUI::SpectrumWorxEditor>(host_, panelPlacement()));
        editor_ = &zoomed_->editor();
        addAndMakeVisible(*zoomed_);
        setSize(zoomed_->getWidth(), zoomed_->getHeight());

        if (withModuleInFirstSlot)
        {
            fillFirstSlot();
            showModuleDrop();
        }

        if (openPresetBrowser)
        {
            /// \note SW_SHOW_UI_PRESET_BANK opens the browser inside a factory
            /// bank rather than at its root, so a single page can be swept over
            /// the eighteen of them from a shell loop -- the same idea as
            /// SW_SHOW_UI_EFFECT above.
            if (auto const *const bank = std::getenv("SW_SHOW_UI_PRESET_BANK"))
            {
                editor_->showFactoryBank(bank);

                /// \note SW_SHOW_UI_PRESET then *loads* it, which is what
                /// selecting a row does and is the half of the browser that
                /// reaches the engine: new global parameters, a new module
                /// chain, and a UI region built for each of them.
                if (auto const *const preset = std::getenv("SW_SHOW_UI_PRESET"))
                    loadFactoryPreset(bank, preset);
                else if (std::getenv("SW_SHOW_UI_PRESET_SWEEP"))
                    sweepFactoryPresets();
            }
            else
                editor_->showPresetBrowser(true);
        }

        if (settingsPage >= 0)
        {
            /// \note Through the browser first, deliberately. The two panels
            /// share one rectangle and openOverlay() asserts that only one of
            /// them is ever up; going straight to the settings would never
            /// exercise that, and swapping between them is what a user does.
            /// What this page renders is therefore also the evidence that the
            /// browser went away.
            ///                               (01.08.2026.) (SW port)
            editor_->showPresetBrowser(true);
            editor_->showSettings(static_cast<unsigned int>(settingsPage));
        }
    }

    /// \note The editor goes before the host it holds a reference to.
    ~EditorPage() override { zoomed_.reset(); }

    void resized() override { zoomed_->setTopLeftPosition(0, 0); }

    /// \note The page is whatever size the editor is, and the editor changes
    /// size: opening a panel asks its host for a column and takes one here,
    /// because this harness grants every request. Without this the render is
    /// cropped to 563 px and the column -- the thing the page exists to show --
    /// is the part that falls off.
    void childBoundsChanged(juce::Component *const pChild) override
    {
        if (pChild == zoomed_.get())
            setSize(zoomed_->getWidth(), zoomed_->getHeight());
    }

  private:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief SW_SHOW_UI_PANEL_PLACEMENT, which is the whole reason a still image
    /// is worth anything here: the three arrangements are three pictures, and
    /// which of them is right is a question for an eye and not a test.
    ///
    /// \note At construction rather than through the setter, because that is what
    /// a plugin does and because `alwaysVisible` takes its column there.
    ///
    ////////////////////////////////////////////////////////////////////////////
    static GUI::SpectrumWorxEditor::PanelPlacement panelPlacement()
    {
        using Placement = GUI::SpectrumWorxEditor::PanelPlacement;

        auto const *const requested(std::getenv("SW_SHOW_UI_PANEL_PLACEMENT"));
        if (!requested)
            return GUI::SpectrumWorxEditor::defaultPanelPlacement;

        std::fprintf(stderr, "sw-show-ui: panel placement %s\n", requested);
        if (std::strcmp(requested, "overlay") == 0)
            return Placement::overlay;
        if (std::strcmp(requested, "always-visible") == 0)
            return Placement::alwaysVisible;
        LE_ASSERT_MSG(std::strcmp(requested, "expand-contract") == 0,
                      "SW_SHOW_UI_PANEL_PLACEMENT: overlay, expand-contract or always-visible.");
        return Placement::expandContract;
    }

    void loadFactoryPreset(juce::String const &bank, juce::String const &preset)
    {
        std::fprintf(stderr, "sw-show-ui: loading %s / %s\n", bank.toRawUTF8(), preset.toRawUTF8());

        auto presetData(FactoryPresets::load(bank.toStdString(), preset.toStdString()));
        LE_ASSERT_MSG(presetData, "No such factory preset.");
        if (!presetData)
            return;

        juce::String comment;
        LE_VERIFY(editor_->loadPreset(presetData.get(), true, comment, preset));
    }

    /// \brief Every factory preset, into the same editor, one after another.
    ///
    /// \note One editor rather than one each, deliberately: loading a preset
    /// *merges* into the current module chain -- loadModuleChain() moves a module
    /// already holding the same effect across rather than building a new one --
    /// so the second load is a different code path from the first, and it is the
    /// one a user takes.
    void sweepFactoryPresets()
    {
        for (auto const &bank : FactoryPresets::banks())
            for (auto const &preset : FactoryPresets::presets(bank))
                loadFactoryPreset(bank.c_str(), preset.c_str());
    }

    /// \brief Adds an effect exactly as choosing it from the module menu does.
    ///
    /// \note Through the editor's own entry point rather than through the module
    /// chain, because adding a module is five steps and only the first two are
    /// reachable from the chain: create, build the region, take focus, select,
    /// notify the host. Driving the chain directly renders a perfectly good
    /// picture while testing none of the last three, which is where the bugs have
    /// been.
    ///
    /// \note SW_SHOW_UI_EFFECT picks which effect, so a single page can be swept
    /// over the whole list from a shell loop. Building a module's widgets is
    /// per-effect work -- every widget type an effect uses is a template
    /// instantiation of its own, and the ranges are quantised against the engine
    /// setup per parameter -- so "one effect renders" says very little about the
    /// other fifty-six.
    void fillFirstSlot()
    {
        std::uint8_t effectIndex{0};
        if (auto const *const requested = std::getenv("SW_SHOW_UI_EFFECT"))
            effectIndex = static_cast<std::uint8_t>(std::atoi(requested));

        std::fprintf(stderr, "sw-show-ui: slot 1 <- effect %u (%s)\n", unsigned(effectIndex),
                     Effects::effectName(effectIndex));

        editor_->addUserAddedModule(effectIndex);

        auto const pModule(host_.core().moduleChain().module(0));
        LE_ASSERT_MSG(pModule, "The harness could not create a module.");

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \note The posted resync, run by hand -- `addUserAddedModule` ends in
        /// `refreshModuleRackAsync()` and an offscreen render turns no message
        /// loop, so without this the strip is never built.
        ///
        ///   Which is what it was doing until 03.08.2026: this page rendered an
        /// editor with a highlighted empty slot and no module in it, and the
        /// giveaway was that all 57 effects produced *byte-identical* PNGs. So
        /// the page that exists to prove a module's widgets can be built was
        /// building none of them, and "editor-module renders" meant "the empty
        /// editor renders, again". `tests/clap/pluginTests.cpp` had the same
        /// call for the same reason and this page never got it.
        ///                                   (03.08.2026.) (SW port)
        ///
        ////////////////////////////////////////////////////////////////////////
        editor_->resyncModuleRack();
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief SW_SHOW_UI_MODULE_DROP -- what dragging a strip over the rack
    /// looks like. "swap:<slot>" fills the strip a drop would change places
    /// with; "insert:<gap>" draws the line a drop would open the rack at.
    ///
    /// \note Which of the two indications is right is a question for an eye.
    /// tests/gui/moduleDragTests.cpp can say that a swap fills its target and an
    /// insert draws a sliver between two, and cannot say whether either reads as
    /// an invitation to let go of the mouse.
    ///
    /// \note Through showModuleDrop() rather than a real drag, for the same
    /// reason that file gives: a JUCE drag needs a window, and this page renders
    /// offscreen.
    ///                                       (14.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    void showModuleDrop()
    {
        auto const *const requested(std::getenv("SW_SHOW_UI_MODULE_DROP"));
        if (!requested)
            return;

        auto const *const pIndex(std::strchr(requested, ':'));
        LE_ASSERT_MSG(pIndex, "SW_SHOW_UI_MODULE_DROP: swap:<slot> or insert:<gap>.");
        if (!pIndex)
            return;

        // Something to drop between: the page fills one slot, this fills two more.
        for (std::uint8_t effect(1); effect < 3; ++effect)
            editor_->addUserAddedModule(effect);
        editor_->resyncModuleRack();

        using Drop = GUI::SpectrumWorxEditor::ModuleDrop;
        bool const swap(std::strncmp(requested, "swap", 4) == 0);
        std::fprintf(stderr, "sw-show-ui: %s at %s\n", swap ? "swap" : "insert", pIndex + 1);
        editor_->showModuleDrop(
            {swap ? Drop::swap : Drop::insert, static_cast<std::uint8_t>(std::atoi(pIndex + 1))});
    }

    HarnessHost host_;
    std::unique_ptr<GUI::ZoomedEditor> zoomed_;
    GUI::SpectrumWorxEditor *editor_{nullptr};
}; // class EditorPage

SWShowUI::PageRegistration const registration{
    "editor", "the whole editor, over a headless engine",
    [] { return std::unique_ptr<juce::Component>(std::make_unique<EditorPage>(false, 48000)); }};

SWShowUI::PageRegistration const registrationWithModule{
    "editor-module", "the editor with an effect in the first slot",
    [] { return std::unique_ptr<juce::Component>(std::make_unique<EditorPage>(true, 48000)); }};

/// \note The order a standalone starts in, and the one that broke: a session is
/// restored -- modules created and their UI built -- before activate() has given
/// the engine a sample rate, so a knob whose range quantises to a step time has
/// nothing to quantise against.
SWShowUI::PageRegistration const registrationBeforeSetup{
    "editor-module-cold", "an effect added before the engine has a sample rate",
    [] { return std::unique_ptr<juce::Component>(std::make_unique<EditorPage>(true, 0)); }};

/// \note The presets button, which had no headless coverage at all until it
/// asserted on its first real press -- `presetsFolder()` was half of a two-phase
/// initialisation whose initialiser nothing called. Constructing the browser is
/// most of what that button does: it reads six skin bitmaps, builds a list box
/// and a comment editor, asks where the user's presets are and lists them.
SWShowUI::PageRegistration const registrationWithPresetBrowser{
    "editor-presets", "the editor with the preset browser open", [] {
        return std::unique_ptr<juce::Component>(
            std::make_unique<EditorPage>(true, 48000, true /*preset browser*/));
    }};

/// \note The settings panel had no headless coverage at all -- it was a separate
/// desktop window until 6.4 and nothing offscreen could ever see it. It shares
/// the preset browser's rectangle now, so this page is also what says the two of
/// them land in the same place. Its three tabs are one bitmap page each; the
/// interface tab is the interesting one, being the only page with live widgets
/// on it. `SW_SHOW_UI_SETTINGS_PAGE` picks a tab, as the other env switches here
/// pick an effect or a bank.
SWShowUI::PageRegistration const registrationWithSettings{
    "editor-settings", "the editor with the settings panel open", [] {
        int page{0};
        if (auto const *const requested = std::getenv("SW_SHOW_UI_SETTINGS_PAGE"))
            page = std::atoi(requested);
        return std::unique_ptr<juce::Component>(
            std::make_unique<EditorPage>(true, 48000, false, page));
    }};

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------
