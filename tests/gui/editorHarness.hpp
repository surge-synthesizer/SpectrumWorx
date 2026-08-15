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
///                                           (03.08.2026.) (SW port)
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

namespace SWTest
{
using namespace LE;
using namespace LE::SW;

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
    Instance()
    {
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

    std::vector<juce::Point<int>> requestedSizes;
    bool grantResizes{true};

    fs::path currentSampleFile() const override { return {}; }
    char const *setNewSample(fs::path const &) override { return nullptr; }
    bool isSampleLoadInProgress() const override { return false; }
    void registerSampleLoadedListener(GUI::SpectrumWorxEditor &) override {}
    void deregisterSampleLoadedListener(GUI::SpectrumWorxEditor const &) override {}

    bool completelyDisableIOChanges() const override { return false; }

  private:
    Engine engine_;
    SilentNotifications notifications_;
    mutable Threading::ToEngineQueue toEngine_;
    Threading::ValueMailbox values_;
    std::unique_ptr<GUI::SpectrumWorxEditor> pEditor_;
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
