////////////////////////////////////////////////////////////////////////////////
///
/// sliderDragTests.cpp
/// -------------------
///
///   How a slider follows the mouse: shift refines, alt links a two-value
/// slider's thumbs, and nothing drops the drag into JUCE's velocity mode.
/// \see issue #167.
///
/// \note The widget alone, with no editor under it -- what is being asserted is
/// the arithmetic between a mouse position and a value, which is the same on
/// every slider in the plugin because they all take it from GUI::FineDrag.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/gui.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using LE::SW::GUI::FineDrag;
using LE::SW::GUI::HorizontalSlider;

constexpr int sliderWidth{200};
constexpr float dragDistance{40};

/// \note Hand-built and handed straight to the component, as everywhere else
/// here. \see the note over moduleControlFocusTests.cpp's eventOver().
juce::MouseEvent eventAt(juce::Component &component, float const x, float const pressX,
                         int const modifiers, bool const dragged)
{
    juce::Point<float> const position(x, static_cast<float>(component.getHeight()) / 2);
    juce::Point<float> const pressed(pressX, position.y);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | modifiers),
                            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component, juce::Time(),
                            pressed, juce::Time(), 1, dragged);
}

/// \brief Presses at \p from, drags to \p from + \p by, releases.
/// \returns how far the value moved.
double drag(juce::Slider &slider, float const from, float const by, int const modifiers = 0)
{
    auto const before(slider.getValue());
    slider.mouseDown(eventAt(slider, from, from, modifiers, false));
    slider.mouseDrag(eventAt(slider, from + by, from, modifiers, true));
    slider.mouseUp(eventAt(slider, from + by, from, modifiers, true));
    return slider.getValue() - before;
}

/// A slider big enough to have a track, in the unit range.
struct Fixture
{
    Fixture()
    {
        slider.setSize(sliderWidth, 20);
        slider.setRange(0.0, 1.0);
        slider.setValue(0.5);
    }

    /// \note Before the slider, so that it outlives it: a juce::Component wants
    /// a message manager to repaint against. \see juce_Component.cpp:1658.
    juce::ScopedJuceInitialiser_GUI const juceIsUp;
    HorizontalSlider slider;
};

/// The same with two thumbs, well inside the range so that neither can clamp.
struct RangeFixture
{
    RangeFixture()
    {
        slider.setSize(sliderWidth, 20);
        slider.setSliderStyle(juce::Slider::TwoValueHorizontal);
        slider.setRange(0.0, 1.0);
        /// \note The upper thumb first: juce::Slider clamps a new minimum
        /// against the maximum, which starts out at zero along with it.
        slider.setMaxValue(high, juce::dontSendNotification);
        slider.setMinValue(low, juce::dontSendNotification);
    }

    static constexpr double low{0.2};
    static constexpr double high{0.5};

    juce::ScopedJuceInitialiser_GUI const juceIsUp;
    HorizontalSlider slider;
};

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("A slider drag follows the mouse", "[gui][slider]")
{
    Fixture fixture;
    auto const moved(drag(fixture.slider, sliderWidth / 2.0f, dragDistance));
    CHECK(moved > 0);
}

TEST_CASE("Shift makes a slider drag four times finer", "[gui][slider]")
{
    Fixture coarse, fine;

    auto const wholeDrag(drag(coarse.slider, sliderWidth / 2.0f, dragDistance));
    auto const fineDrag(
        drag(fine.slider, sliderWidth / 2.0f, dragDistance, juce::ModifierKeys::shiftModifier));

    CHECK(fineDrag == Catch::Approx(wholeDrag / FineDrag::ratio).epsilon(0.001));
}

////////////////////////////////////////////////////////////////////////////////
/// \note The point of feeding juce::Slider a position rather than a sensitivity:
/// what shift refines is the travel made while it was down, and the travel
/// before it went down keeps the value it already earned.
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Shift pressed mid-drag does not jump a slider", "[gui][slider]")
{
    Fixture fixture;
    auto &slider(fixture.slider);
    /// \note Through juce::Component, whose mouse handlers are public. \see
    /// discreteParameterTests.cpp, which reaches a combo box the same way.
    auto &widget(static_cast<juce::Component &>(slider));
    auto const start(sliderWidth / 2.0f);

    widget.mouseDown(eventAt(slider, start, start, 0, false));
    widget.mouseDrag(eventAt(slider, start + dragDistance, start, 0, true));
    auto const afterCoarse(slider.getValue());

    widget.mouseDrag(
        eventAt(slider, start + dragDistance, start, juce::ModifierKeys::shiftModifier, true));
    CHECK(slider.getValue() == Catch::Approx(afterCoarse));

    widget.mouseDrag(
        eventAt(slider, start + 2 * dragDistance, start, juce::ModifierKeys::shiftModifier, true));
    auto const refined(slider.getValue() - afterCoarse);

    widget.mouseUp(eventAt(slider, start + 2 * dragDistance, start, 0, true));

    Fixture reference;
    auto const wholeDrag(drag(reference.slider, start, dragDistance));
    CHECK(refined == Catch::Approx(wholeDrag / FineDrag::ratio).epsilon(0.001));
}

////////////////////////////////////////////////////////////////////////////////
/// \note The regression guard for what this issue was reported as: command gave
/// finer increments on the AU, because JUCE reads command, control and alt as
/// "switch to velocity mode", whose response is the mouse's speed rather than
/// its distance. \see FineDrag::keepDragLinear().
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("No modifier drops a slider into velocity mode", "[gui][slider]")
{
    Fixture plain;
    auto const expected(drag(plain.slider, sliderWidth / 2.0f, dragDistance));

    for (auto const modifier : {juce::ModifierKeys::commandModifier,
                                juce::ModifierKeys::ctrlModifier, juce::ModifierKeys::altModifier})
    {
        Fixture fixture;
        CHECK(drag(fixture.slider, sliderWidth / 2.0f, dragDistance, modifier) ==
              Catch::Approx(expected));
    }
}

////////////////////////////////////////////////////////////////////////////////
/// \note juce::Slider puts this on shift, which is fine adjustment here.
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Alt drags a two-value slider's thumbs together", "[gui][slider]")
{
    SECTION("alt keeps the span")
    {
        RangeFixture fixture;
        auto &slider(fixture.slider);
        drag(slider, slider.getPositionOfValue(RangeFixture::low), dragDistance,
             juce::ModifierKeys::altModifier);

        CHECK(slider.getMinValue() > RangeFixture::low);
        CHECK(slider.getMaxValue() - slider.getMinValue() ==
              Catch::Approx(RangeFixture::high - RangeFixture::low).epsilon(0.001));
    }

    SECTION("shift does not")
    {
        RangeFixture fixture;
        auto &slider(fixture.slider);
        drag(slider, slider.getPositionOfValue(RangeFixture::low), dragDistance,
             juce::ModifierKeys::shiftModifier);

        CHECK(slider.getMinValue() > RangeFixture::low);
        CHECK(slider.getMaxValue() == Catch::Approx(RangeFixture::high));
    }
}
