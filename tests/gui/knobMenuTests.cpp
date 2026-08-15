////////////////////////////////////////////////////////////////////////////////
///
/// knobMenuTests.cpp
/// -----------------
///
///   The two things a knob's right button menu does that are not JUCE's: reading
/// a typed value back into the parameter, and turning the LFO on.
///
/// \note The menu itself is deliberately not driven here, for the reason
/// lfoDisplayTests.cpp gives about the LFO waveform popup: a menu is a modal
/// desktop window and a test binary has no message loop to answer one with. What
/// is covered is everything underneath it -- the two routes the items call --
/// which is where all of the logic is. `Knob::showParameterMenu()` itself only
/// assembles them.
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

#include "core/parameterID.hpp"
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

using LE::SW::ParameterID;
using LE::SW::GUI::ModuleControlBase;
using LE::SW::Threading::ToEngine;

constexpr std::uint8_t
    enabledIndex(LE::Parameters::IndexOf<LE::Parameters::LFOImpl::Parameters,
                                         LE::Parameters::LFOImpl::Enabled>::value);

/// \brief The first effect-specific control of \p moduleUI that is a knob.
///
/// \note By parameter index rather than by walking the children, as
/// moduleControlFocusTests.cpp does and for the same reason: the widget storage
/// is a compile-time chain of one base class per parameter, so there is no
/// runtime list to iterate.
ModuleControlBase *firstKnob(LE::SW::GUI::ModuleUI &moduleUI)
{
    auto const parameters(moduleUI.module().numberOfEffectSpecificParameters());
    for (std::uint8_t index(0); index < parameters; ++index)
    {
        auto &control(moduleUI.effectSpecificParameterControl(index));
        if (dynamic_cast<LE::SW::GUI::ModuleKnob *>(&control.widget()) != nullptr)
            return &control;
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

/// The last `SetBaseParameter` in \p messages naming \p parameterID.
std::optional<ToEngine> lastEditOf(std::vector<ToEngine> const &messages,
                                   ParameterID const parameterID)
{
    std::optional<ToEngine> found;
    for (auto const &message : messages)
        if ((message.kind == ToEngine::Kind::SetBaseParameter) &&
            (message.setBaseParameter.parameterID == parameterID.binaryValue))
            found = message;
    return found;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief An editor with a module in slot 0 and its first knob's control to
/// hand, selected so that the LFO strip is up.
///
////////////////////////////////////////////////////////////////////////////////

class KnobUnderTest
{
  public:
    explicit KnobUnderTest(SWTest::Instance &instance)
    {
        instance.openEditor();
        auto &editor(instance.editor());
        editor.addUserAddedModule(0);
        editor.resyncModuleRack();

        auto *const pModuleUI(editor.regionInSlot(0));
        REQUIRE(pModuleUI != nullptr);
        pControl_ = firstKnob(*pModuleUI);
        REQUIRE(pControl_ != nullptr);

        /// \note The range the LFO strip's two-value bound slider is laid out
        /// over; an LFO's bounds are normalised, so the knob's own units do not
        /// come into it. \see lfoDisplayTests.cpp, which sets it up the same way.
        editor.moduleControlActivated(*pControl_, 0.0, 1.0, 0.0);

        // Whatever building the strip and the panel queued is not a case's.
        drain(instance.toEngine());
    }

    ModuleControlBase &control() const { return *pControl_; }
    juce::Slider &knob() const { return dynamic_cast<juce::Slider &>(pControl_->widget()); }

    /// The ID the host knows this knob's LFO switch by.
    ParameterID lfoEnabledID() const
    {
        ParameterID parameterID;
        parameterID.value.type = ParameterID::LFOParameter;
        parameterID.value._.lfo = {enabledIndex, pControl_->moduleParameterIndex(),
                                   /*moduleIndex*/ 0};
        return parameterID;
    }

  private:
    ModuleControlBase *pControl_{nullptr};
}; // class KnobUnderTest

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("A knob reads back the value string it prints", "[gui][modules][menu]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Which is the whole contract of the type-in field: whatever the knob
    /// is currently showing is what the field starts out holding, so the user who
    /// opens the menu and presses return has to land back on the value they were
    /// already on. The two halves are `getValueString()` and `parseValueString()`,
    /// and until 15.08.2026 only the first had a caller in the interface.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    KnobUnderTest const knob(instance);
    auto &control(knob.control());

    auto const value(control.getValue());
    auto const parsed(control.parseValueString(control.getValueText()));

    REQUIRE(parsed.has_value());
    /// \note Against the knob's own step and not against an epsilon: a value is
    /// *printed* rounded, so what comes back is the nearest value of this
    /// parameter that displays as that text -- which is the right answer and need
    /// not be bit-identical. \see LE::Parameters::displayRounding().
    auto const quantum(std::max(knob.knob().getInterval(),
                                (knob.knob().getMaximum() - knob.knob().getMinimum()) / 1000));
    CHECK(std::abs(*parsed - value) <= quantum);
}

TEST_CASE("A knob refuses text no value of it displays as", "[gui][modules][menu]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Refused rather than clamped, which is why parseValueString() answers
    /// an optional at all: a knob handed a clamped value has told the user their
    /// typo was understood and has committed a parameter change they did not ask
    /// for. Nothing is what puts the field back where it was.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    KnobUnderTest const knob(instance);
    auto &control(knob.control());

    CHECK_FALSE(control.parseValueString("").has_value());
    CHECK_FALSE(control.parseValueString("not a number").has_value());
    /// \note Far outside every module parameter's range and, unlike the two
    /// above, a perfectly good number -- so this is the range check rather than
    /// the parse.
    CHECK_FALSE(control.parseValueString("1e9").has_value());
}

TEST_CASE("The menu's LFO switch moves both copies and the host", "[gui][modules][lfo][menu]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `LFO::Enabled` is an exported parameter, so turning it on is an edit
    /// like any other and has to reach the engine as well as the copy this thread
    /// draws from -- which is the trap lfoDisplayTests.cpp was written around.
    /// The menu entry and the LFO strip's own switch are one implementation for
    /// exactly that reason.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    KnobUnderTest const knob(instance);
    auto &control(knob.control());
    auto &editor(instance.editor());

    REQUIRE_FALSE(control.isLFOEnabled());

    editor.setLFOEnabled(control, true);

    CHECK(control.isLFOEnabled()); // the main thread's copy
    {
        auto const queued(lastEditOf(drain(instance.toEngine()), knob.lfoEnabledID()));
        REQUIRE(queued.has_value()); // ...and the engine's
        CHECK(queued->setBaseParameter.value == 1.0f);
    }

    /// \note Not a side issue: `lfoStateChanged()` is what re-keys the two
    /// gestures that would otherwise move a value the LFO owns, and the menu
    /// route has to do it as well as the strip's switch does.
    CHECK_FALSE(knob.knob().isScrollWheelEnabled());
    CHECK_FALSE(knob.knob().isDoubleClickReturnEnabled());

    editor.setLFOEnabled(control, false);

    CHECK_FALSE(control.isLFOEnabled());
    {
        auto const queued(lastEditOf(drain(instance.toEngine()), knob.lfoEnabledID()));
        REQUIRE(queued.has_value());
        CHECK(queued->setBaseParameter.value == 0.0f);
    }
    CHECK(knob.knob().isScrollWheelEnabled());
    CHECK(knob.knob().isDoubleClickReturnEnabled());
}
