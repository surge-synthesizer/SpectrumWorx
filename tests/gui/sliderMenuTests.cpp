////////////////////////////////////////////////////////////////////////////////
///
/// sliderMenuTests.cpp
/// -------------------
///
///   The last of issue #93: the sliders. A knob, an LED, a trigger and a combo
/// box got the parameter's right button menu on 21.08.2026 by being module
/// controls; the four sliders are not, and the two-thumbed ones stand for two
/// parameters rather than one.
///
/// \note The menu's contents are not driven here, for the reason knobMenuTests.cpp
/// gives: a test binary has no message loop to answer a modal component with.
/// What is covered is what the widget answers *with* -- which parameter, what it
/// reads as, what typing into it does -- and that a right press does not move the
/// value on its way to raising one.
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
#include "gui/editor/auxiliaryComponents.hpp"
#include "gui/modules/moduleControl.hpp"
#include "gui/preferences.hpp"
#include "gui/modules/moduleUI.hpp"

#include "le/parameters/lfoImpl.hpp"
#include "le/parameters/parametersUtilities.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using LE::SW::ParameterID;
using LE::SW::GUI::ModuleControlBase;
using LE::SW::GUI::SpectrumWorxEditor;
using LFO = LE::Parameters::LFOImpl;

template <class Parameter>
constexpr std::uint8_t lfoIndex(LE::Parameters::IndexOf<LFO::Parameters, Parameter>::value);

/// \note Hand-built and handed straight to the component, as everywhere else
/// here. \see the note on eventOver() in moduleControlFocusTests.cpp.
juce::MouseEvent pressAt(juce::Component &component, float const x, int const button)
{
    juce::Point<float> const position(x, static_cast<float>(component.getHeight()) / 2);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
                            juce::ModifierKeys(button), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component,
                            &component, juce::Time(), position, juce::Time(), 1, false);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief An editor with a module in slot 0 and its first control selected, so
/// that the LFO strip is up and its three sliders are laid out.
///
////////////////////////////////////////////////////////////////////////////////

class StripUnderTest
{
  public:
    explicit StripUnderTest(SWTest::Instance &instance)
    {
        instance.openEditor();
        auto &editor(instance.editor());
        editor.addUserAddedModule(0);
        editor.resyncModuleRack();

        auto *const pModuleUI(editor.regionInSlot(0));
        REQUIRE(pModuleUI != nullptr);

        /// \note The strip has to be *selected* and not merely have a control
        /// activated: SharedModuleControls -- which the frequency range lives on
        /// -- is built by moduleActivated(), and a mouse entering the region is
        /// the public way in. The preference that gates it is Never by default.
        /// \see ModuleUI::selectionTracksMouseMovements().
        LE::SW::GUI::preferences().setModuleUIMouseOverReaction(
            LE::SW::GUI::Preferences::WhenParentOrNothingSelected);
        static_cast<juce::Component &>(*pModuleUI).mouseEnter(pressAt(*pModuleUI, 1, 0));
        REQUIRE(editor.selectedModule() == pModuleUI);

        pControl_ = &pModuleUI->effectSpecificParameterControl(0);

        /// \note The range the two-value bound slider is laid out over; an LFO's
        /// bounds are normalised. \see lfoDisplayTests.cpp, which sets it up so.
        editor.moduleControlActivated(*pControl_, 0.0, 1.0, 0.0);

        pStrip_ = editor.lfoDisplay();
        REQUIRE(pStrip_ != nullptr);
    }

    SpectrumWorxEditor::LFODisplay &strip() const { return *pStrip_; }
    ModuleControlBase &control() const { return *pControl_; }

    /// The ID this strip's LFO parameter \p lfoParameterIndex is known by.
    ParameterID idFor(std::uint8_t const lfoParameterIndex) const
    {
        ParameterID parameterID;
        parameterID.value.type = ParameterID::LFOParameter;
        parameterID.value._.lfo = {lfoParameterIndex, pControl_->moduleParameterIndex(),
                                   /*moduleIndex*/ 0};
        return parameterID;
    }

  private:
    SpectrumWorxEditor::LFODisplay *pStrip_{nullptr};
    ModuleControlBase *pControl_{nullptr};
}; // class StripUnderTest

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("Every LFO slider names the parameter it stands for", "[gui][lfo][menu]")
{
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    StripUnderTest const strip(instance);

    CHECK(strip.strip().period().lfoParameterIndex() == lfoIndex<LFO::PeriodScale>);
    CHECK(strip.strip().phase().lfoParameterIndex() == lfoIndex<LFO::Phase>);

    CHECK(strip.strip().period().parameterID().binaryValue ==
          strip.idFor(lfoIndex<LFO::PeriodScale>).binaryValue);
    CHECK(strip.strip().phase().parameterID().binaryValue ==
          strip.idFor(lfoIndex<LFO::Phase>).binaryValue);

    /// \note The module parameter's own name in front of the LFO's -- and the
    /// module in front of that, \see issue #203 -- so that a menu opened over the
    /// strip says which knob of which strip its LFO belongs to.
    CHECK(strip.strip().phase().parameterName() ==
          "Module 1 - " + juce::String(strip.control().name()) + " - LFO Phase");
}

////////////////////////////////////////////////////////////////////////////////
/// \note The rule the issue names: "if you are below low show low, if you are
/// above high show high, if you are between use closest" -- which is the nearest
/// thumb in all three cases.
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The LFO range slider's menu is about the thumb pressed", "[gui][lfo][menu]")
{
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    StripUnderTest const strip(instance);

    auto &range(strip.strip().range());

    range.setMinValue(0.25, juce::dontSendNotification);
    range.setMaxValue(0.75, juce::dontSendNotification);

    auto const low(range.getPositionOfValue(0.25));
    auto const high(range.getPositionOfValue(0.75));

    /// \note notePressAt() rather than a right press, which would raise a real
    /// modal menu -- \see the note at the top of this file. It is the step
    /// mouseDown() takes before it opens one, and the whole of the decision.
    auto const pressedAt([&](float const position) {
        range.notePressAt(position);
        return range.lfoParameterIndex();
    });

    CHECK(pressedAt(low) == lfoIndex<LFO::LowerBound>);
    CHECK(pressedAt(high) == lfoIndex<LFO::UpperBound>);

    // Outside the band on either side.
    CHECK(pressedAt(low - 20) == lfoIndex<LFO::LowerBound>);
    CHECK(pressedAt(high + 20) == lfoIndex<LFO::UpperBound>);

    // ...and inside it, where the answer is the nearer thumb.
    CHECK(pressedAt(low + (high - low) * 0.4f) == lfoIndex<LFO::LowerBound>);
    CHECK(pressedAt(high - (high - low) * 0.4f) == lfoIndex<LFO::UpperBound>);

    range.notePressAt(low);
    CHECK(range.parameterID().binaryValue == strip.idFor(lfoIndex<LFO::LowerBound>).binaryValue);
}

////////////////////////////////////////////////////////////////////////////////
/// \note A right press raises the menu; it does not also drag the slider under
/// it. The knob has said so since issue #92 and the sliders did not.
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The right button does not move a slider", "[gui][lfo][menu]")
{
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    StripUnderTest const strip(instance);

    auto &phase(strip.strip().phase());
    auto &widget(static_cast<juce::Component &>(phase));

    phase.setValue(0.0, juce::dontSendNotification);
    auto const before(phase.getValue());

    constexpr int rightButton{juce::ModifierKeys::rightButtonModifier};
    widget.mouseDrag(pressAt(phase, static_cast<float>(phase.getWidth()) - 1, rightButton));

    CHECK(phase.getValue() == Catch::Approx(before));
}

////////////////////////////////////////////////////////////////////////////////
/// \note What the type-in field promises: whatever the slider is showing is what
/// the field starts out holding, so opening the menu and pressing return lands
/// back on the value that was already there. \see knobMenuTests.cpp's first case.
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("An LFO slider reads back the value string it prints", "[gui][lfo][menu]")
{
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    StripUnderTest const strip(instance);

    SECTION("the phase, in degrees")
    {
        auto &phase(strip.strip().phase());
        phase.setValue(0.25, juce::dontSendNotification);

        REQUIRE(phase.parameterValueText() == "90.0\xc2\xb0");
        REQUIRE(phase.setParameterFromText(phase.parameterValueText()));
        CHECK(phase.getValue() == Catch::Approx(0.25));
    }

    SECTION("the period, on whichever grid it is snapped to")
    {
        auto &period(strip.strip().period());
        auto const text(period.parameterValueText());

        REQUIRE(text.isNotEmpty());
        REQUIRE(period.setParameterFromText(text));
        CHECK(period.parameterValueText() == text);
    }

    SECTION("a bound, in the units of the parameter it modulates")
    {
        auto &range(strip.strip().range());
        range.setMinValue(0.25, juce::dontSendNotification);

        range.notePressAt(range.getPositionOfValue(0.25));
        REQUIRE(range.lfoParameterIndex() == lfoIndex<LFO::LowerBound>);

        /// \note Text in and the same text out, rather than a value comparison: a
        /// bound is printed in the modulated parameter's units and therefore
        /// rounded to what that parameter displays. \see knobMenuTests.cpp's
        /// first case, which says the same thing about a knob.
        auto const text(range.parameterValueText());
        REQUIRE(range.setParameterFromText(text));
        CHECK(range.parameterValueText() == text);
    }
}

TEST_CASE("An LFO slider refuses text no value of it displays as", "[gui][lfo][menu]")
{
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    StripUnderTest const strip(instance);

    CHECK_FALSE(strip.strip().phase().setParameterFromText(""));
    CHECK_FALSE(strip.strip().phase().setParameterFromText("not a number"));
    CHECK_FALSE(strip.strip().period().setParameterFromText("not a period"));
}

////////////////////////////////////////////////////////////////////////////////
/// \note The other two-thumbed slider, and the one that was already a module
/// control: it needed the mix-in and the thumb rule, not the answers.
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The frequency range's menu is about the thumb pressed", "[gui][modules][menu]")
{
    ////////////////////////////////////////////////////////////////////////////
    /// \note The other two-thumbed slider, and the one that was already a module
    /// control: it needed the mix-in and the thumb rule, not the answers.
    ///
    /// \note No control activated here, unlike every case above: the frequency
    /// range only tracks the mouse while nothing else is the active control --
    /// \see FrequencyRange::updateSliderSelection() -- so selecting the module is
    /// the whole of the setup it wants.
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    instance.openEditor();
    auto &editor(instance.editor());
    editor.addUserAddedModule(0);
    editor.resyncModuleRack();

    auto *const pModuleUI(editor.regionInSlot(0));
    REQUIRE(pModuleUI != nullptr);

    LE::SW::GUI::preferences().setModuleUIMouseOverReaction(
        LE::SW::GUI::Preferences::WhenParentOrNothingSelected);
    static_cast<juce::Component &>(*pModuleUI).mouseEnter(pressAt(*pModuleUI, 1, 0));
    REQUIRE(editor.selectedModule() == pModuleUI);
    REQUIRE(editor.activeControl() == nullptr);

    auto &range(editor.sharedModuleControls().frequencyRange());
    auto &widget(static_cast<juce::Component &>(range));

    /// \note juce::Slider numbers a two-value slider's thumbs 1 and 2, and the
    /// module parameter indices skip Bypass. \see auxiliaryComponents.cpp.
    using namespace LE::SW::Effects::BaseParameters;
    constexpr int lowerThumb{1}, upperThumb{2};
    auto const startIndex(
        static_cast<std::uint8_t>(LE::Parameters::IndexOf<Parameters, StartFrequency>::value - 1));
    auto const stopIndex(
        static_cast<std::uint8_t>(LE::Parameters::IndexOf<Parameters, StopFrequency>::value - 1));

    auto const low(range.getPositionOfValue(range.getMinValue()));
    auto const high(range.getPositionOfValue(range.getMaxValue()));

    /// \note mouseMove rather than a right press, which would raise a real modal
    /// menu -- \see the note at the top of this file. It is the same
    /// updateSliderSelection() the press makes before it opens one.
    widget.mouseMove(pressAt(range, low - 20, 0));
    CHECK(range.selectedThumb() == lowerThumb);
    CHECK(range.moduleParameterIndex() == startIndex);

    widget.mouseMove(pressAt(range, high + 20, 0));
    CHECK(range.selectedThumb() == upperThumb);
    CHECK(range.moduleParameterIndex() == stopIndex);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Issue #203, and it is the case the one above deliberately does not set
/// up: a control *is* selected. The frequency range stops tracking the mouse
/// then -- that is what keeps a sweep across the rack from taking the LFO strip
/// away from whatever the user clicked -- and a press on it used to inherit that
/// refusal, so the slider stood for no parameter at all. `moduleParameterIndex()`
/// spells that as an index one past the end, and `+ 1 /*Bypass*/` wraps it to
/// zero: every answer the menu gave -- the header, the identifier the host's own
/// entries key on, and what "Reset to default value" would have written -- was
/// about the module's Bypass.
///
/// \note The preference is put back to its default here, which is the harder
/// half: with `Never` the press may not take the selection, and what the menu is
/// about must not depend on whether it did.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The frequency range's menu is about the thumb pressed while a knob is selected",
          "[gui][modules][menu]")
{
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    instance.openEditor();
    auto &editor(instance.editor());
    editor.addUserAddedModule(0);
    editor.resyncModuleRack();

    auto *const pModuleUI(editor.regionInSlot(0));
    REQUIRE(pModuleUI != nullptr);

    LE::SW::GUI::preferences().setModuleUIMouseOverReaction(
        LE::SW::GUI::Preferences::WhenParentOrNothingSelected);
    static_cast<juce::Component &>(*pModuleUI).mouseEnter(pressAt(*pModuleUI, 1, 0));
    REQUIRE(editor.selectedModule() == pModuleUI);

    /// \note A knob taking the selection the way the pointer gives it, rather
    /// than `moduleControlActivated()` -- which builds the strip without making
    /// anything the *active* control, and it is that pointer the frequency range
    /// asks about.
    auto &knob(pModuleUI->effectSpecificParameterControl(0));
    static_cast<juce::Component &>(knob.widget()).mouseEnter(pressAt(knob.widget(), 1, 0));
    REQUIRE(editor.activeControl() == &knob);

    auto &range(editor.sharedModuleControls().frequencyRange());
    auto &widget(static_cast<juce::Component &>(range));

    LE::SW::GUI::preferences().setModuleUIMouseOverReaction(LE::SW::GUI::Preferences::Never);

    // the sweep that leaves the slider standing for nothing
    widget.mouseEnter(pressAt(range, 1, 0));
    REQUIRE(range.selectedThumb() == -1);

    using namespace LE::SW::Effects::BaseParameters;
    auto const startIndex(
        static_cast<std::uint8_t>(LE::Parameters::IndexOf<Parameters, StartFrequency>::value - 1));
    auto const stopIndex(
        static_cast<std::uint8_t>(LE::Parameters::IndexOf<Parameters, StopFrequency>::value - 1));

    auto const idFor([&](std::uint8_t const moduleParameterIndex) {
        return editor.moduleParameterID(knob.module(), moduleParameterIndex).binaryValue;
    });

    auto const low(static_cast<int>(range.getPositionOfValue(range.getMinValue())));
    auto const high(static_cast<int>(range.getPositionOfValue(range.getMaxValue())));

    /// \note notePressAt() rather than a right press, which would raise a real
    /// modal menu -- \see the note at the top of this file. It is the step
    /// mouseDown() takes before it opens one, and the whole of the decision.
    range.notePressAt(low - 20);
    CHECK(range.moduleParameterIndex() == startIndex);
    CHECK(juce::String(range.name()) == "Start Frequency");
    CHECK(range.parameterMenuName() == "Module 1 - Start Frequency");
    CHECK(range.parameterMenuID().binaryValue ==
          idFor(LE::Parameters::IndexOf<Parameters, StartFrequency>::value));

    range.notePressAt(high + 20);
    CHECK(range.moduleParameterIndex() == stopIndex);
    CHECK(juce::String(range.name()) == "Stop Frequency");
    CHECK(range.parameterMenuID().binaryValue ==
          idFor(LE::Parameters::IndexOf<Parameters, StopFrequency>::value));

    // ...and the knob kept the halo throughout, the preference saying the pointer
    // may not take it
    CHECK(editor.activeControl() == &knob);
}
