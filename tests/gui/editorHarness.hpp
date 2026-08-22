////////////////////////////////////////////////////////////////////////////////
///
/// \file editorHarness.hpp
/// -----------------------
///
///   One plugin's worth of everything a SpectrumWorxEditor reaches into, with no
/// host and no plugin format under it. The editors are constructed directly
/// rather than through the CLAP shim, as tools/show-ui does: what these cases
/// test is our own bookkeeping, and the shim's half is stood in for by the
/// ScopedJuceInitialiser_GUI that HostSideJuce holds.
///
/// \note Header rather than a .cpp because every member is inline and the whole
/// thing is 60 lines; a second translation unit would be more build than
/// harness. It was twoInstanceTests.cpp's anonymous namespace until a second
/// file needed an editor.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef editorHarness_hpp__4D0F2A57_9C4E_4B2C_9D71_5B0E2F6A3C18
#define editorHarness_hpp__4D0F2A57_9C4E_4B2C_9D71_5B0E2F6A3C18
//------------------------------------------------------------------------------
#include "goldens/engineHarness.hpp"

#include "core/host_interop/plugin2Host.hpp"
#include "core/host_interop/programWrite.hpp"
#include "core/threading/publish.hpp"

#include "le/plugins/clap/tag.hpp"

#include "gui/editor/editorHost.hpp"
#include "gui/editor/spectrumWorxEditor.hpp"
#include "gui/resources.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#if JUCE_LINUX || JUCE_BSD
////////////////////////////////////////////////////////////////////////////////
///
/// \note Declared rather than included. <X11/Xlib.h> reaches a consumer of
/// juce_gui_basics only behind JUCE_GUI_BASICS_INCLUDE_XHEADERS, and turning
/// that on here would put X11's macros -- None, Status, Bool, Success, Complex
/// -- into every translation unit that includes this harness, all of which also
/// compile Catch2 and our own headers. Three functions and one opaque type is
/// the whole of what aWindowCanBeMade() below needs, and libX11 is already on
/// this target's link line through JUCE.
///
////////////////////////////////////////////////////////////////////////////////
extern "C"
{
    struct _XDisplay;
    _XDisplay *XOpenDisplay(char const *displayName);
    unsigned long XInternAtom(_XDisplay *display, char const *name, int onlyIfExists);
    int XCloseDisplay(_XDisplay *display);
} // extern "C"
#endif // JUCE_LINUX || JUCE_BSD

namespace SWTest
{
using namespace LE;
using namespace LE::SW;

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Whether a desktop window can be made here at all.
///
/// \note X11 only, and it is not the same question as "is there a display".
/// `xvfb-run` gives a window server and no *window manager*, and JUCE cannot
/// make a window on one: `XWindowSystem::createWindow` writes the WM_PROTOCOLS
/// property unguarded --
///
///     xchangeProperty( windowH, atoms.protocols, XA_ATOM, 32, atoms.protocolList, 2 );
///
/// -- and `atoms.protocols` is `getIfExists( display, "WM_PROTOCOLS" )`, an atom
/// that only a window manager ever interns. With none running it is None, the
/// property written is 0, and the server answers BadAtom -- on which Xlib's
/// default handler calls `exit( 1 )`, killing the case where it stands. The
/// leak-detector output that follows in a CI log is that exit, not a second bug.
///
///   So the atom is asked for the way JUCE asks for it, and its absence is the
/// signal. macOS and Windows have no such hole and run these cases normally.
///
/// \note **Any** case that puts a component on the desktop needs this, not only
/// the ones that are about focus -- and a menu is the easy one to forget,
/// because nothing in the case says "window". A GUI::PopupMenu is a desktop
/// window (\see PopupMenu::showAt(), which names no parent component), so a case
/// that so much as opens one dies here without this guard. \see issue #145's
/// sibling, the knob's parameter menu, which is parented and does not.
///
////////////////////////////////////////////////////////////////////////////////
inline bool aWindowCanBeMade()
{
#if JUCE_LINUX || JUCE_BSD
    /// \note Our own connection rather than JUCE's, which is not open yet the
    /// first time this is asked. Atoms belong to the server rather than to a
    /// connection, so the answer is the one JUCE will get.
    auto *const pDisplay(XOpenDisplay(nullptr));
    if (!pDisplay)
        return false; // No window server either.
    auto const protocols(XInternAtom(pDisplay, "WM_PROTOCOLS", 1 /* only if it exists */));
    XCloseDisplay(pDisplay);
    return protocols != 0; // ...which is None.
#else
    return true;
#endif // JUCE_LINUX || JUCE_BSD
}

/// \brief What a case says when there is no window to put a component on.
constexpr char noWindow[]{
    "No window manager: JUCE cannot put a component on the desktop here, and these cases need a "
    "real window to drive the keyboard with."};

/// \note Every one of these is a notification travelling plugin -> host, and
/// there is no host.
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

/// One plugin's worth of everything an editor reaches into.
class Instance final : public GUI::EditorHost
{
  public:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \param setUpEngine false for an engine that has never been given a
    /// sample rate, which is what a plugin looks like before the host activates
    /// it -- and the one state in which a case may *load a preset*.
    ///
    /// \note Loading reaches SpectrumWorxCore::engineSetup(), which asserts
    /// unless the engine is already running at the FFT size the preset asks
    /// for. Re-setting it up is the CLAP layer's job and there is no CLAP layer
    /// here, so what makes the assertion legal is its third term: storage
    /// factors that are not complete. \see spectrumWorxCore.cpp.
    ///
    /// \note show-ui's EditorPage takes the same argument as a sample rate of
    /// zero, for the same reason.
    ///
    ////////////////////////////////////////////////////////////////////////////

    explicit Instance(bool const setUpEngine = true)
    {
        if (!setUpEngine)
            return;

        engine_.setNumberOfChannels(2, 2);
        engine_.setSampleRate(48000);
        engine_.setBlockSize(512);
        REQUIRE(engine_.initialise());
    }

    using PanelPlacement = GUI::SpectrumWorxEditor::PanelPlacement;

    void openEditor(PanelPlacement const placement = GUI::SpectrumWorxEditor::defaultPanelPlacement)
    {
        pEditor_ = std::make_unique<GUI::SpectrumWorxEditor>(*this, placement);
    }
    void closeEditor() { pEditor_.reset(); }
    GUI::SpectrumWorxEditor &editor() const
    {
        REQUIRE(pEditor_ != nullptr);
        return *pEditor_;
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The side chain's source, held and handed back.
    ///
    /// \note No sample loader under this harness, so `File` is not reachable
    /// through it -- what the editor cases are about is that the selector *shows*
    /// the source and that picking one reaches the host. \see
    /// tests/external_audio/sampleFeedTests.cpp for what each source does to the
    /// audio.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SideChainSource sideChainSource() const override { return sideChainSource_; }
    void setSideChainSource(SideChainSource const source) override { sideChainSource_ = source; }

    SpectrumWorxCore &core() override { return engine_; }
    Plugin2HostInteropControler &automation() override { return notifications_; }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The engine's own Program, which here is also the main thread's.
    ///
    /// \note Nothing is ever activated in this harness, so `engineIsRunning()` is
    /// false and this thread owns the engine outright --
    /// `currentThreadMayMutateEngineState()` says so. One Program is therefore
    /// the honest arrangement rather than a shortcut: the second copy exists to
    /// keep the main thread off state an audio thread owns, and there is no audio
    /// thread here.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Program &programMain() override { return engine_.program(); }

    Program &mutableProgram() const { return const_cast<Engine &>(engine_).program(); }

    /// \brief The engine itself, typed, so a case can stand in for the audio
    /// thread and apply a spectral parameter. \see SWTest::Engine::set().
    Engine &engine() { return engine_; }

    /// \note Applied outright, for the same reason: with nothing processing,
    /// `Threading::publish*()` would apply them here anyway.
    void editParameter(ParameterID const parameterID, float const value) const override
    {
        setParameterIn<LE::Plugins::Protocol::CLAP>(mutableProgram(), parameterID, value);
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

    /// \note Unchecked here on purpose: nothing drains this harness's queue, so a
    /// full one is a finding about the case rather than about the plugin. The
    /// plugin's own implementation counts the drop -- see
    /// `SpectrumWorxCLAP::pushed()`.
    void publishUnexportedLFOParameter(std::uint8_t const moduleIndex,
                                       std::uint8_t const moduleParameterIndex,
                                       std::uint8_t const lfoParameterIndex,
                                       float const value) override
    {
        toEngine_.push(Threading::setUnexportedLFOParameter(moduleIndex, moduleParameterIndex,
                                                            lfoParameterIndex, value));
    }

    /// \note Real ones, and nobody drains them: what the editor asks for goes
    /// into the queue and stays there. These cases are about the interface side,
    /// and a queue that fills would be a finding rather than a nuisance.
    Threading::ToEngineQueue &toEngine() const override { return toEngine_; }
    Threading::ValueMailbox const &modulatedValues() const override { return values_; }

    /// \note Nothing, which is also what a host with no `clap_host_context_menu`
    /// contributes: the knob's menu ends where the plugin's own entries do.
    void addHostParameterEntries(ParameterID, juce::PopupMenu &) const override {}

    void editorOpened(GUI::SpectrumWorxEditor &) override {}
    void editorClosed(GUI::SpectrumWorxEditor &) override {}

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief `clap_host_gui::request_resize`, recorded and answered.
    ///
    /// \note Answering *no* is the interesting host and not a broken one -- the
    /// extension is optional and a host may refuse any particular size -- so
    /// `grantResizes` is what a case sets to make one, and the recorded sizes are
    /// what says the editor asked at all rather than just resizing itself behind
    /// a window that did not move.
    ///
    /// \note Nothing sets `grantResizes` as of 14.08.2026: what the editor did
    /// about a refusal is about to change under user-controlled zoom, so the two
    /// cases that pinned it have gone. \see overlayPanelTests.cpp.
    ///
    ////////////////////////////////////////////////////////////////////////////

    bool requestEditorSize(int const width, int const height) override
    {
        requestedSizes.emplace_back(width, height);
        return grantResizes;
    }

    /// \note Recorded separately from requestedSizes, because the distinction is
    /// the point: this one has no answer and cannot be refused. \see
    /// EditorHost::editorSizeChanged().
    void editorSizeChanged(int const width, int const height) override
    {
        announcedSizes.emplace_back(width, height);
    }

    std::vector<juce::Point<int>> requestedSizes;
    std::vector<juce::Point<int>> announcedSizes;
    bool grantResizes{true};

    fs::path currentSampleFile() const override { return {}; }
    char const *setNewSample(fs::path const &) override { return nullptr; }
    bool isSampleLoadInProgress() const override { return false; }
    void registerSampleLoadedListener(GUI::SpectrumWorxEditor &) override {}
    void deregisterSampleLoadedListener(GUI::SpectrumWorxEditor const &) override {}

    bool completelyDisableIOChanges() const override { return false; }

    /// \note Held by the instance and not by the editor, which is what the
    /// plugin does with it and what makes a case that closes and reopens the
    /// window measure the right thing. \see SpectrumWorxCLAP::sessionState().
    ///
    GUI::PanelState &panelState() override { return panelState_; }

    /// \note The same reasoning: the plugin holds this and a case that wants to
    /// drive the Save buttons has to be able to set it. \see issue #177.
    GUI::LoadedPreset &loadedPreset() override { return loadedPreset_; }

  private:
    Engine engine_;
    SilentNotifications notifications_;
    mutable Threading::ToEngineQueue toEngine_;
    Threading::ValueMailbox values_;
    std::unique_ptr<GUI::SpectrumWorxEditor> pEditor_;
    SideChainSource sideChainSource_{defaultSideChainSource};
    GUI::PanelState panelState_;
    GUI::LoadedPreset loadedPreset_;
}; // class Instance

/// \brief JUCE, owned the way the shim owns it: one reference held across
/// everything the case does.
struct HostSideJuce
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    /// The skin caches juce::Images, and no juce::Image may outlive JUCE.
    ~HostSideJuce() { GUI::releaseCachedResources(); }
}; // struct HostSideJuce

} // namespace SWTest

#endif // editorHarness_hpp
