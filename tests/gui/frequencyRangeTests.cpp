////////////////////////////////////////////////////////////////////////////////
///
/// frequencyRangeTests.cpp
/// -----------------------
///
///   The shared Frequency Range slider, and what the pointer does to it.
///
///   It was the one module control a hover did nothing to: it tracked the mouse
/// only while nothing else was selected, so a user who had clicked a knob could
/// not see either frequency's LFO without giving up the knob. Issue #220 makes
/// it behave as the knobs beside it do -- the thumb under the pointer is marked,
/// and lends the LFO strip its own LFO for as long as the pointer is there when
/// the user has asked for the preview.
///
/// \note No window anywhere here, as in moduleHoverTests.cpp, whose rule this
/// file is the frequency range's half of: hovering marks, clicking selects, and
/// marking never becomes selecting. What a click gives is asked for directly,
/// there being no keyboard to give it.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/editorHarness.hpp"

/// \note Before anything that names SW::Module, as elsewhere here: the module
/// chain downcasts a node to it.
#include "core/modules/moduleDSPAndGUI.hpp"

#include "gui/editor/auxiliaryComponents.hpp"
#include "gui/modules/moduleControl.hpp"
#include "gui/modules/moduleUI.hpp"
#include "gui/preferences.hpp"
#include "gui/theme.hpp"

#include "le/parameters/lfoImpl.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/spectrumworx/effects/baseParameters.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

namespace GUI = LE::SW::GUI;

using FrequencyRange = GUI::SharedModuleControls::FrequencyRange;

/// juce::Slider numbers a two-value slider's thumbs 1 and 2, and -1 for none.
constexpr int lowerThumb{1}, upperThumb{2}, noThumb{-1};

/// \brief The LFO-able index of one of the two frequencies, which is what
/// `ModuleControlBase::moduleParameterIndex()` answers with -- Bypass excluded.
template <class Parameter> std::uint8_t frequencyIndex()
{
    using namespace LE::SW::Effects::BaseParameters;
    return static_cast<std::uint8_t>(LE::Parameters::IndexOf<Parameters, Parameter>::value - 1);
}

/// \note Hand-built and handed straight to the component, as everywhere else
/// here, and carrying no button: a hover is not a press.
juce::MouseEvent pointerAt(juce::Component &component, float const x)
{
    juce::Point<float> const position(x, static_cast<float>(component.getHeight()) / 2);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
                            juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component,
                            &component, juce::Time(), position, juce::Time(), 1, false);
}

/// \brief \p widget as an image, which is what a user sees. \see
/// moduleDragTests.cpp.
juce::Image rendered(juce::Component &widget)
{
    juce::Image image(juce::Image::ARGB, widget.getWidth(), widget.getHeight(), true);
    juce::Graphics graphics(image);
    widget.paintEntireComponent(graphics, true);
    return image;
}

/// Whether two renders of the same widget disagree anywhere.
bool differ(juce::Image const &left, juce::Image const &right)
{
    for (int y(0); y < left.getHeight(); ++y)
        for (int x(0); x < left.getWidth(); ++x)
            if (left.getPixelAt(x, y) != right.getPixelAt(x, y))
                return true;
    return false;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief An editor with one effect in the first slot and that strip selected,
/// which is what builds the shared controls the frequency range lives on.
///
////////////////////////////////////////////////////////////////////////////////

class RangeUnderTest
{
  public:
    explicit RangeUnderTest(SWTest::Instance &instance) : editor_(instance.editor())
    {
        editor_.addUserAddedModule(0);
        editor_.resyncModuleRack();

        pStrip_ = editor_.regionInSlot(0);
        REQUIRE(pStrip_ != nullptr);

        /// \note Selected, and not merely a control of it activated:
        /// SharedModuleControls is built by moduleActivated(). The pointer does
        /// not select a strip and a headless case has no keyboard to give it, so
        /// it is asked directly. \see sliderMenuTests.cpp.
        pStrip_->activate();
        REQUIRE(editor_.selectedModule() == pStrip_);

        pRange_ = &editor_.sharedModuleControls().frequencyRange();
    }

    FrequencyRange &range() const { return *pRange_; }

    /// \note The same object through the base, whose `select()` is public: the
    /// frequency range answers the module control interface protected.
    GUI::ModuleControlBase &control() const { return *pRange_; }

    juce::Component &widget() const { return *pRange_; }
    GUI::ModuleControlBase &knob() const { return pStrip_->effectSpecificParameterControl(0); }
    LE::SW::Module &module() const { return pStrip_->module(); }

    /// Where the two thumbs are, in the widget's own coordinates.
    ///@{
    float lowThumb() const { return pRange_->getPositionOfValue(pRange_->getMinValue()); }
    float highThumb() const { return pRange_->getPositionOfValue(pRange_->getMaxValue()); }
    ///@}

    void pointerEnters(float const x) const { widget().mouseEnter(pointerAt(widget(), x)); }
    void pointerMovesTo(float const x) const { widget().mouseMove(pointerAt(widget(), x)); }
    void pointerLeaves() const { widget().mouseExit(pointerAt(widget(), lowThumb())); }

  private:
    GUI::SpectrumWorxEditor &editor_;
    GUI::ModuleUI *pStrip_{nullptr};
    FrequencyRange *pRange_{nullptr};
}; // class RangeUnderTest

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("The pointer marks the frequency thumb it is nearest", "[gui][modules][hover]")
{
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    instance.openEditor();
    RangeUnderTest const range(instance);
    auto &editor(instance.editor());

    //   A knob holding the selection, which is the case the frequency range used
    // to refuse to track the mouse in at all.
    range.knob().select();
    REQUIRE(editor.activeControl() == &range.knob());

    range.pointerEnters(range.lowThumb());

    CHECK(editor.hoveredControl() == &range.range());
    CHECK(range.range().selectedThumb() == lowerThumb);
    CHECK(range.range().moduleParameterIndex() ==
          frequencyIndex<LE::SW::Effects::BaseParameters::StartFrequency>());

    // ...and the knob kept the ring: the pointer may not take it.
    CHECK(editor.activeControl() == &range.knob());
}

TEST_CASE("The pointer moving across the frequency range changes the thumb it marks",
          "[gui][modules][hover]")
{
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    instance.openEditor();
    RangeUnderTest const range(instance);

    range.knob().select();

    range.pointerEnters(range.lowThumb());
    REQUIRE(range.range().selectedThumb() == lowerThumb);

    range.pointerMovesTo(range.highThumb());

    CHECK(range.range().selectedThumb() == upperThumb);
    CHECK(range.range().moduleParameterIndex() ==
          frequencyIndex<LE::SW::Effects::BaseParameters::StopFrequency>());
}

TEST_CASE("A hover lends the LFO strip the frequency thumb's own LFO", "[gui][modules][hover][lfo]")
{
    SWTest::HostSideJuce const juceIsUp;
    GUI::preferences().setPreviewLFOOnHover(true);

    SWTest::Instance instance;
    instance.openEditor();
    RangeUnderTest const range(instance);
    auto &editor(instance.editor());

    range.knob().select();
    REQUIRE(editor.displayedControl() == &range.knob());

    ///   Two bounds apart, so that what the strip is showing says which of the
    /// two parameters it is showing it for.
    using namespace LE::SW::Effects::BaseParameters;
    auto &startLFO(range.module().baseLFO(frequencyIndex<StartFrequency>()));
    auto &stopLFO(range.module().baseLFO(frequencyIndex<StopFrequency>()));
    startLFO.setLowerBound(0.1f);
    stopLFO.setLowerBound(0.8f);

    range.pointerEnters(range.lowThumb());

    // The strip follows the pointer...
    CHECK(editor.displayedControl() == &range.range());
    REQUIRE(editor.lfoDisplay() != nullptr);
    auto const showingStart(editor.lfoDisplay()->range().getMinValue());

    // ...and follows the thumb within the one widget, which is the harder half.
    range.pointerMovesTo(range.highThumb());
    CHECK(editor.lfoDisplay()->range().getMinValue() != showingStart);

    range.pointerLeaves();

    // ...and gives it back.
    CHECK(editor.displayedControl() == &range.knob());
    CHECK(editor.activeControl() == &range.knob());
}

TEST_CASE("With the preview off a hover marks the frequency thumb and no more",
          "[gui][modules][hover][lfo]")
{
    SWTest::HostSideJuce const juceIsUp;
    GUI::preferences().setPreviewLFOOnHover(false);

    SWTest::Instance instance;
    instance.openEditor();
    RangeUnderTest const range(instance);
    auto &editor(instance.editor());

    range.knob().select();

    range.pointerEnters(range.lowThumb());

    CHECK(range.range().selectedThumb() == lowerThumb);
    CHECK(editor.displayedControl() == &range.knob());

    GUI::preferences().setPreviewLFOOnHover(true);
}

TEST_CASE("The mark leaves the frequency range with the pointer", "[gui][modules][hover]")
{
    ///   A marked thumb is what the menu, the readout and the halo are about, so
    /// it may not outlive the pointer that chose it -- the slider would be left
    /// wearing a halo with nothing under the mouse.
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    instance.openEditor();
    RangeUnderTest const range(instance);
    auto &editor(instance.editor());

    range.knob().select();
    range.pointerEnters(range.lowThumb());
    REQUIRE(range.range().selectedThumb() == lowerThumb);

    range.pointerLeaves();

    CHECK(editor.hoveredControl() == nullptr);
    CHECK(range.range().selectedThumb() == noThumb);
}

TEST_CASE("A pressed frequency thumb keeps the selection when the pointer leaves",
          "[gui][modules][hover]")
{
    ///   The other half of the same rule: a thumb a *press* chose stays chosen,
    /// the slider being the selected control and the strip still showing it.
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    instance.openEditor();
    RangeUnderTest const range(instance);
    auto &editor(instance.editor());

    //   select() is the click's own step with the keyboard left out, and a thumb
    // has to be chosen first: it is the one control select() cannot make up an
    // answer for.
    range.range().notePressAt(static_cast<int>(range.highThumb()));
    range.control().select();

    REQUIRE(editor.activeControl() == &range.range());
    REQUIRE(range.range().selectedThumb() == upperThumb);

    range.pointerEnters(range.highThumb());
    range.pointerLeaves();

    CHECK(range.range().selectedThumb() == upperThumb);
    CHECK(editor.activeControl() == &range.range());
    CHECK(editor.displayedControl() == &range.range());
}

TEST_CASE("Sliding from one selected frequency thumb to the other keeps the selection",
          "[gui][modules][hover]")
{
    ///   The move the pointer is allowed to make on a selected control: which of
    /// the two parameters this one widget stands for. The selection stays here --
    /// it is the thumb that moved -- and the widget has to say so itself, the
    /// keyboard being somewhere else entirely by the time a user reaches for the
    /// other end of the band.
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    instance.openEditor();
    RangeUnderTest const range(instance);
    auto &editor(instance.editor());

    range.range().notePressAt(static_cast<int>(range.lowThumb()));
    range.control().select();
    REQUIRE(editor.activeControl() == &range.range());

    range.pointerEnters(range.lowThumb());
    range.pointerMovesTo(range.highThumb());

    CHECK(editor.activeControl() == &range.range());
    CHECK(editor.displayedControl() == &range.range());
    CHECK(range.range().moduleParameterIndex() ==
          frequencyIndex<LE::SW::Effects::BaseParameters::StopFrequency>());
}

////////////////////////////////////////////////////////////////////////////////
//
// What it draws
// -------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   How the halo and the bead *look* is checked by looking --
/// `sw-show-ui --render editor-module SW_SHOW_UI_HIGHLIGHT=1` puts a marked
/// thumb beside a selected one. What is below is the thing an eye is bad at:
/// noticing nothing at all when a binding has died.
///
/// \note Nothing here names a colour or a size, for the reason
/// knobPaintingTests.cpp gives: those are decisions, and a case that pinned one
/// would fail the first time somebody made the halo brighter.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The frequency range draws the thumb the pointer marks", "[gui][modules][hover]")
{
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    instance.openEditor();
    RangeUnderTest const range(instance);

    range.knob().select();
    auto const unmarked(rendered(range.widget()));

    range.pointerEnters(range.lowThumb());

    CHECK(differ(unmarked, rendered(range.widget())));
}

TEST_CASE("A selected frequency thumb is not drawn as a marked one", "[gui][modules][hover]")
{
    ///   Both wear the halo the knobs beside them wear, and the selection wears
    /// it louder -- which is the whole of what tells them apart now that the
    /// bead no longer changes size. \see issue #210 and issue #220.
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    instance.openEditor();
    RangeUnderTest const range(instance);

    range.knob().select();
    range.pointerEnters(range.lowThumb());
    auto const marked(rendered(range.widget()));

    range.pointerLeaves();
    range.range().notePressAt(static_cast<int>(range.lowThumb()));
    range.control().select();

    CHECK(differ(marked, rendered(range.widget())));
}

TEST_CASE("The frequency range's thumbs are chunkier than an LFO slider's", "[gui][modules]")
{
    ///   Because they are drawn among the module knobs and stand for parameters
    /// of their own, which the LFO strip's sliders do not. \see issue #220, and
    /// SliderWithSelectedThumb, which is where the two part company.
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    instance.openEditor();
    RangeUnderTest const range(instance);
    auto &editor(instance.editor());

    range.knob().select();
    REQUIRE(editor.lfoDisplay() != nullptr);

    auto &theme(GUI::Theme::singleton());
    CHECK(theme.getSliderThumbRadius(range.range()) >
          theme.getSliderThumbRadius(editor.lfoDisplay()->range()));
}
