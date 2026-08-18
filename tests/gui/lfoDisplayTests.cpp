////////////////////////////////////////////////////////////////////////////////
///
/// lfoDisplayTests.cpp
/// -------------------
///
///   The LFO panel's own edits, and whether they reach the audio thread.
///
///   The panel had no test of any kind, and the shape of what it hid is worth
/// stating: **every widget on it wrote the LFO the strip holds, and that stopped
/// being the engine's LFO when the editor was bound to `programMain_`.** Five of
/// the seven sub-parameters have a `ParameterID` and go through
/// `EditorHost::editParameter`, which moves both copies. The two that do not --
/// Waveform and SyncTypes -- take `ToEngine::SetUnexportedLFOParameter`, and the
/// N/T/D buttons were not converted with the waveform popup: they called
/// `LFO::addSyncType()` straight onto the main thread's module, so a sync-mode
/// change moved the display and the saved state and nothing anybody could hear.
///
///   So what these cases assert is not "the parameter changed" -- it did, that
/// was never the bug -- but **that a message was queued for the engine**. The
/// harness's `toEngine()` is a real ring, which makes that an ordinary read.
///
/// \note No window and no peer, unlike moduleControlFocusTests.cpp: nothing here
/// needs focus. `moduleControlActivated()` is public and is what builds the
/// panel, and a button is driven by `setToggleState( …, sendNotificationSync )`
/// rather than by a synthesised click -- `setClickingTogglesState( true )` means
/// a real click does exactly that and then notifies.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/editorHarness.hpp"

/// \note Before anything that names SW::Module, as elsewhere: the module chain
/// downcasts a node to it and this is the header with the complete type.
#include "core/modules/moduleDSPAndGUI.hpp"

#include "core/threading/messages.hpp"
#include "gui/modules/moduleControl.hpp"
#include "gui/modules/moduleUI.hpp"

#include "le/parameters/lfoImpl.hpp"
#include "le/parameters/parametersUtilities.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using LE::Parameters::LFO;
using LE::Parameters::LFOImpl;
using LE::SW::GUI::ModuleControlBase;
using LE::SW::Threading::ToEngine;

constexpr std::uint8_t
    syncTypesIndex(LE::Parameters::IndexOf<LFOImpl::Parameters, LFOImpl::SyncTypes>::value);

/// \brief The first module control in \p component's subtree, which is a knob on
/// the module strip and therefore something an LFO can be attached to.
ModuleControlBase *firstControl(juce::Component &component)
{
    for (auto *const pChild : component.getChildren())
    {
        if (auto *const pControl = dynamic_cast<ModuleControlBase *>(pChild))
            return pControl;
        if (auto *const pFound = firstControl(*pChild))
            return pFound;
    }
    return nullptr;
}

/// \brief The button named \p name anywhere under \p component.
///
/// \note By name because that is what `GUI::TextButton` sets from its label, and
/// because the panel's widgets are private members of a private nested class --
/// reaching them through the component tree is what a click does anyway.
juce::Button *buttonNamed(juce::Component &component, juce::StringRef const name)
{
    for (auto *const pChild : component.getChildren())
    {
        if (auto *const pButton = dynamic_cast<juce::Button *>(pChild);
            pButton && (pButton->getName() == name))
            return pButton;
        if (auto *const pFound = buttonNamed(*pChild, name))
            return pFound;
    }
    return nullptr;
}

/// Everything the interface has queued for the engine, drained.
std::vector<ToEngine> drain(LE::SW::Threading::ToEngineQueue &queue)
{
    std::vector<ToEngine> messages;
    ToEngine message;
    while (queue.pop(message))
        messages.push_back(message);
    return messages;
}

/// The last `SetUnexportedLFOParameter` in \p messages naming \p lfoParameterIndex.
std::optional<ToEngine> lastUnexportedEdit(std::vector<ToEngine> const &messages,
                                           std::uint8_t const lfoParameterIndex)
{
    std::optional<ToEngine> found;
    for (auto const &message : messages)
        if ((message.kind == ToEngine::Kind::SetUnexportedLFOParameter) &&
            (message.setUnexportedLFOParameter.lfoParameterIndex == lfoParameterIndex))
            found = message;
    return found;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief An editor with a module in slot 0 and its first control's LFO panel up.
///
////////////////////////////////////////////////////////////////////////////////

class PanelUnderTest
{
  public:
    explicit PanelUnderTest(SWTest::Instance &instance) : instance_(instance)
    {
        instance.openEditor();
        auto &editor(instance.editor());
        editor.addUserAddedModule(0);
        editor.resyncModuleRack();

        auto *const pModuleUI(editor.regionInSlot(0));
        REQUIRE(pModuleUI != nullptr);
        pControl_ = firstControl(*pModuleUI);
        REQUIRE(pControl_ != nullptr);

        /// \note The range the panel's two-value bound slider is laid out over.
        /// An LFO's bounds are normalised, so the control's own units do not come
        /// into it and neither does the effect in the slot.
        editor.moduleControlActivated(*pControl_, 0.0, 1.0, 0.0);

        // Whatever building the strip and the panel queued is not this case's.
        drain(instance.toEngine());
    }

    ModuleControlBase &control() const { return *pControl_; }
    LFO &lfo() const { return pControl_->lfo(); }

    /// \brief Clicks the panel's \p name button, the way a mouse would.
    juce::Button &click(juce::StringRef const name) const
    {
        auto &button(this->button(name));
        button.setToggleState(!button.getToggleState(), juce::sendNotificationSync);
        return button;
    }

    juce::Button &button(juce::StringRef const name) const
    {
        auto *const pButton(buttonNamed(instance_.editor(), name));
        REQUIRE(pButton != nullptr);
        return *pButton;
    }

    bool lit(juce::StringRef const name) const { return button(name).getToggleState(); }

  private:
    SWTest::Instance &instance_;
    ModuleControlBase *pControl_{nullptr};
}; // class PanelUnderTest

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("A sync mode set in the interface is queued for the engine", "[gui][lfo]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The regression, and the reason it was invisible: nothing in the
    /// suite had ever read the *engine* side of an LFO after a UI edit. The
    /// display, `paramsValue`, `stateSave` and the preset writer all answer from
    /// the main thread's copy, so every existing case agreed with a change the
    /// audio thread never received.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    PanelUnderTest const panel(instance);

    // Quarter is the default, so turning it off is the smallest real edit.
    REQUIRE(panel.lfo().hasEnabledSync(LFO::Quarter));
    panel.click(" N ");

    CHECK_FALSE(panel.lfo().hasEnabledSync(LFO::Quarter)); // ...the half that worked
    CHECK(panel.lfo().syncTypes() == LFO::Free);

    auto const queued(lastUnexportedEdit(drain(instance.toEngine()), syncTypesIndex));
    REQUIRE(queued.has_value()); // ...and the half that did not
    /// \note The mask travels as a float, which is what the message carries for
    /// every unexported sub-parameter; these are small exact integers in one.
    CHECK(queued->setUnexportedLFOParameter.value == static_cast<float>(LFO::Free));
    CHECK(queued->setUnexportedLFOParameter.moduleIndex == 0);
}

TEST_CASE("N, T and D are one choice rather than three toggles", "[gui][lfo]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Issue #111. `SyncType` is a bit mask -- Quarter|Triplet|Dotted,
    /// `Free` being none of them -- and `snapSyncedPeriod()` picks whichever of
    /// the enabled grids lands nearest to the current period. With more than one
    /// enabled that is the quarter grid nearly everywhere, so lighting T or D on
    /// top of N read as "the button does nothing". The panel now selects one
    /// grid at a time.
    ///
    ///   What crosses to the engine is still the whole mask and not a bit: the
    /// engine applies what it is given rather than merging it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    PanelUnderTest const panel(instance);

    REQUIRE(panel.lit(" N ")); // Quarter is the default.

    panel.click(" T ");
    {
        auto const queued(lastUnexportedEdit(drain(instance.toEngine()), syncTypesIndex));
        REQUIRE(queued.has_value());
        CHECK(queued->setUnexportedLFOParameter.value == static_cast<float>(LFO::Triplet));
        CHECK(panel.lfo().syncTypes() == LFO::Triplet);
        CHECK_FALSE(panel.lit(" N ")); // ...and the one that was lit went out
        CHECK(panel.lit(" T "));
        CHECK_FALSE(panel.lit(" D "));
    }

    panel.click(" D ");
    {
        auto const queued(lastUnexportedEdit(drain(instance.toEngine()), syncTypesIndex));
        REQUIRE(queued.has_value());
        CHECK(queued->setUnexportedLFOParameter.value == static_cast<float>(LFO::Dotted));
        CHECK(panel.lfo().syncTypes() == LFO::Dotted);
        CHECK_FALSE(panel.lit(" T "));
        CHECK(panel.lit(" D "));
    }

    // Clicking the lit one is how Free is reached, as it always was.
    panel.click(" D ");
    {
        auto const queued(lastUnexportedEdit(drain(instance.toEngine()), syncTypesIndex));
        REQUIRE(queued.has_value());
        CHECK(queued->setUnexportedLFOParameter.value == static_cast<float>(LFO::Free));
        CHECK(panel.lfo().syncTypes() == LFO::Free);
        CHECK_FALSE(panel.lit(" N "));
        CHECK_FALSE(panel.lit(" T "));
        CHECK_FALSE(panel.lit(" D "));
    }
}

/// \note The waveform, which takes the same route, is deliberately **not**
/// covered here. Its only entry point is the popup -- `type_.showCenteredAtRight`
/// with a callback -- and a menu is one of the things a headless editor cannot
/// drive. What guards it is that both go through the one
/// `updateParameterAndNotifyHost<>`, which the two cases above exercise; a
/// waveform case would need a real menu and would be measuring JUCE.
