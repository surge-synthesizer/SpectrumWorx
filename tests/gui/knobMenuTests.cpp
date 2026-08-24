////////////////////////////////////////////////////////////////////////////////
///
/// knobMenuTests.cpp
/// -----------------
///
///   The two things a module control's right button menu does that are not
/// JUCE's: reading a typed value back into the parameter, and turning the LFO
/// on. And, since issue #93, that every control has one -- an LED, a trigger and
/// a combo box as much as a knob.
///
/// \note The menu's *contents* are deliberately not driven here, for the reason
/// lfoDisplayTests.cpp gives about the LFO waveform popup: a test binary has no
/// message loop to answer a modal component with. What is covered is everything
/// underneath it -- the two routes the items call -- which is where all of the
/// logic is. `ParameterMenu::showParameterMenu()` itself only assembles them.
///
/// \note That one is *raised* is a different question and is checkable, because
/// this menu is not a desktop window: it names the editor as its parent so that
/// the type-in field can take the keyboard, so it is an ordinary modal child and
/// `getNumCurrentlyModalComponents()` counts it.
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

#include "le/parameters/lfo.hpp"
#include "le/parameters/lfoImpl.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <ranges>
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

/// \brief The first effect-specific control of \p moduleUI of type \p Widget.
///
/// \note By parameter index rather than by walking the children, as
/// moduleControlFocusTests.cpp does and for the same reason: the widget storage
/// is a compile-time chain of one base class per parameter, so there is no
/// runtime list to iterate.
template <typename Widget> ModuleControlBase *firstControlOfType(LE::SW::GUI::ModuleUI &moduleUI)
{
    auto const parameters(moduleUI.module().numberOfEffectSpecificParameters());
    for (std::uint8_t index(0); index < parameters; ++index)
    {
        auto &control(moduleUI.effectSpecificParameterControl(index));
        if (dynamic_cast<Widget *>(&control.widget()) != nullptr)
            return &control;
    }
    return nullptr;
}

ModuleControlBase *firstKnob(LE::SW::GUI::ModuleUI &moduleUI)
{
    return firstControlOfType<LE::SW::GUI::ModuleKnob>(moduleUI);
}

/// \brief A right-button press over \p position, in \p component's coordinates.
///
/// \note Hand-built and handed straight to `Component::mouseDown()`, which is
/// half a mouse -- \see the note on `eventOver()` in moduleControlFocusTests.cpp.
/// Enough for the one question here, which is what the widget does with a press
/// it is given.
juce::MouseEvent rightPressAt(juce::Component &component, juce::Point<int> const position)
{
    auto const point(position.toFloat());
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                            juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 1.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, &component, &component, juce::Time(), point,
                            juce::Time(), 1, false);
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

/// \brief Every direct child of \p parent that is a button standing for a
/// parameter, in the order they were added.
std::vector<juce::Button *> parameterButtons(juce::Component &parent)
{
    std::vector<juce::Button *> found;
    for (auto *const pChild : parent.getChildren())
        if (auto *const pButton = dynamic_cast<juce::Button *>(pChild);
            pButton && (dynamic_cast<LE::SW::GUI::ParameterMenu *>(pChild) != nullptr))
            found.push_back(pButton);
    return found;
}

/// \brief The blue pill at the foot of \p moduleUI, which is the one capsule on
/// a strip -- the eject X beside it is a shape of its own.
juce::Component &bypassPill(LE::SW::GUI::ModuleUI &moduleUI)
{
    for (auto *const pChild : moduleUI.getChildren())
        if (auto *const pPill = dynamic_cast<LE::SW::GUI::CapsuleButton *>(pChild))
            return *pPill;
    FAIL("The strip has no bypass button.");
    return moduleUI;
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

TEST_CASE("The knob's face is the circle, not the box it is drawn in", "[gui][modules][menu]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note What decides whether a right press raises the parameter's menu or is
    /// handed to the strip behind it. A module knob's widget is eight pixels wider
    /// and eighteen pixels taller than its circle -- the margin the focus halo
    /// needs, and the row the caption is drawn in -- and all of that reads as the
    /// module's background rather than as the knob. \see issue #92.
    ///
    /// \note The geometry rather than the forwarding: sending the press itself
    /// would open one of the two menus, and a menu is what a test binary with no
    /// message loop cannot answer. This is the whole of the decision either way.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    KnobUnderTest const knob(instance);

    auto &widget(dynamic_cast<LE::SW::GUI::Knob &>(knob.control().widget()));
    auto const bounds(widget.getLocalBounds());

    CHECK(widget.isOnKnobFace(bounds.getCentre()));

    // The caption, which is what the issue was reported about.
    CHECK_FALSE(widget.isOnKnobFace({bounds.getCentreX(), bounds.getBottom() - 1}));

    // ...and the four corners, which are inside the rectangle and outside the
    // circle.
    for (auto const corner : {bounds.getTopLeft(), bounds.getTopRight(), bounds.getBottomLeft(),
                              bounds.getBottomRight()})
        CHECK_FALSE(widget.isOnKnobFace(corner));
}

TEST_CASE("A trigger button is a circle in a box too", "[gui][modules][menu]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The same widget in a different shape, and the one the knob fix
    /// missed: Freeze's two triggers are 51 px circles at the top of a 68 x 64
    /// box, so eight pixels either side, the caption row under them and the four
    /// corners of the artwork are all the module strip showing through.
    ///
    /// \note And the right button does not fire the trigger, on the face or off
    /// it -- a knob does not move on the right button either, and a right click
    /// that freezes the audio while the user is reaching for a menu is the
    /// gesture this is about. \see issue #92.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    instance.openEditor();
    auto &editor(instance.editor());

    auto const freeze(LE::SW::Effects::effectIndex("Freeze"));
    REQUIRE(freeze >= 0);
    editor.addUserAddedModule(static_cast<std::uint8_t>(freeze));
    editor.resyncModuleRack();

    auto *const pModuleUI(editor.regionInSlot(0));
    REQUIRE(pModuleUI != nullptr);
    auto *const pControl(firstControlOfType<LE::SW::GUI::TriggerButton>(*pModuleUI));
    REQUIRE(pControl != nullptr);

    auto &button(dynamic_cast<LE::SW::GUI::TriggerButton &>(pControl->widget()));
    auto const bounds(button.getLocalBounds());

    // The circle is at the top of the box, so its centre is not the box's.
    CHECK(button.isOnFace({bounds.getCentreX(), bounds.getWidth() / 2}));

    CHECK_FALSE(button.isOnFace({bounds.getCentreX(), bounds.getBottom() - 1})); // the caption
    CHECK_FALSE(button.isOnFace(bounds.getTopLeft())); // beside the circle
    CHECK_FALSE(button.isOnFace(bounds.getTopRight()));

    // Whatever building the strip queued is not this case's.
    drain(instance.toEngine());

    /// \note Through the base, because the overrides are private -- which is how
    /// moduleControlFocusTests.cpp drives a widget too. The call is virtual
    /// either way.
    juce::Component &component(button);
    component.mouseDown(rightPressAt(button, {bounds.getCentreX(), bounds.getWidth() / 2}));
    component.mouseUp(rightPressAt(button, {bounds.getCentreX(), bounds.getWidth() / 2}));

    CHECK(pControl->getValue() == 0.0f);
    CHECK(drain(instance.toEngine()).empty());
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

////////////////////////////////////////////////////////////////////////////////
///
/// \note Issue #93. "As long as a parameter is exposed to host, we should get
/// the RMB context menu" -- and three of the four module control shapes had
/// none, because the menu was a knob's rather than a parameter's.
///
/// \note Tune Worx by name, because it is the effect the report names and the
/// reason it is the one: thirteen parameters, and **not one of them is a knob**.
/// A combo box for the key and twelve LEDs for the semitones, so a user wanting
/// their host's own entries on any of them had nowhere to right-click.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Every module control raises its parameter's menu", "[gui][modules][menu]")
{
    SWTest::HostSideJuce const juceIsUp;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief How many items each of \p effect's controls put in its menu.
    ///
    /// \note An instance each, because `addUserAddedModule()` fills the *next*
    /// free slot: a second effect asked for on one editor leaves the first in
    /// slot 0, and the case then measures the same thirteen widgets twice. Which
    /// is what it did until the widget types were printed out.
    ///
    ////////////////////////////////////////////////////////////////////////////

    auto const menusOf([](char const *const effect) {
        SWTest::Instance instance;
        instance.openEditor();
        auto &editor(instance.editor());

        editor.addUserAddedModule(static_cast<std::uint8_t>(SWTest::effectByStreamingName(effect)));
        editor.resyncModuleRack();

        auto *const pModuleUI(editor.regionInSlot(0));
        REQUIRE(pModuleUI != nullptr);

        auto const parameters(pModuleUI->module().numberOfEffectSpecificParameters());
        REQUIRE(parameters > 0);

        std::vector<int> items;
        for (std::uint8_t index(0); index < parameters; ++index)
        {
            auto &widget(pModuleUI->effectSpecificParameterControl(index).widget());
            CAPTURE(effect, unsigned(index));

            REQUIRE(juce::Component::getNumCurrentlyModalComponents() == 0);

            /// \note The centre, which for the two round widgets -- a knob and a
            /// trigger -- is the only part of them the menu belongs to. Off the
            /// face the press is the module strip's. \see issue #92.
            auto const centre(widget.getLocalBounds().getCentre());
            widget.mouseDown(rightPressAt(widget, centre));

            ////////////////////////////////////////////////////////////////////
            ///
            /// \note And it is the *parameter's* menu rather than any menu,
            /// which for a combo box is a real distinction: the right button
            /// used to drop its list of values, and that is modal too. The
            /// parameter menu names the editor as its parent -- so that the
            /// type-in field can take the keyboard -- and a list of values does
            /// not, being a desktop window of its own. Whose child it is is
            /// therefore which menu it is.
            ///
            ////////////////////////////////////////////////////////////////////
            REQUIRE(juce::Component::getNumCurrentlyModalComponents() == 1);
            auto *const pMenu(juce::Component::getCurrentlyModalComponent(0));
            REQUIRE(pMenu != nullptr);
            REQUIRE(static_cast<juce::Component const &>(editor).isParentOf(pMenu));

            items.push_back(pMenu->getNumChildComponents());
            juce::PopupMenu::dismissAllActiveMenus();
        }
        return items;
    });

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Counting the items is how a headless run tells the menus apart.
    /// Reading them needs a message loop; counting them does not, and what each
    /// widget shape adds to the four every control has is exactly countable.
    ///
    ///   The four are the parameter's name, the rule under it, "Enable LFO" and
    /// "Reset to default value". The host adds none here, having none to add --
    /// and the rule that would have gone above them is not drawn, JUCE dropping
    /// a separator with nothing after it.
    ///
    ////////////////////////////////////////////////////////////////////////////

    constexpr int shared{4};

    /// \note What a middle section costs: a rule of its own, plus its rows. The
    /// section is what a value may be *set to* -- a field to type into, or the
    /// list of choices -- and a trigger and an LED have neither, so their menus
    /// close up to the four above rather than drawing two rules in a row.
    constexpr int rule{1};

    ////////////////////////////////////////////////////////////////////////////
    /// \note Tune Worx: a combo box for the key and twelve LEDs for the
    /// semitones, and not one knob -- which is why it is the effect the report
    /// names. The LEDs add nothing: a boolean is one press away and there is no
    /// text to type at it. The combo box adds its twelve keys, because an
    /// enumerated parameter is *chosen*, and a user who right-clicks one wants
    /// the list they would otherwise left-click for.
    ////////////////////////////////////////////////////////////////////////////
    auto const tuneWorx(menusOf("TuneWorx"));
    CHECK(tuneWorx.size() == 13);
    CHECK(std::ranges::count(tuneWorx, shared) == 12);
    CHECK(std::ranges::count(tuneWorx, shared + rule + 12) == 1);

    ////////////////////////////////////////////////////////////////////////////
    /// \note Freeze for the other two shapes -- a trigger and a knob in one
    /// effect. A knob is the only module control with a field to type a value
    /// into, so it is the only one of the three with a middle section at all.
    ////////////////////////////////////////////////////////////////////////////
    auto const freeze(menusOf("Freeze"));
    REQUIRE(freeze.size() == 3);
    CHECK(std::ranges::count(freeze, shared) == 2);
    CHECK(std::ranges::count(freeze, shared + rule + 1) == 1);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The three widgets issue #93 was reopened for, and the fourth that goes
/// with them. Every control on a module strip got the menu by being a
/// `ModuleControl`; these four are not, each for its own reason -- Bypass is the
/// parameter a module control's index is counted *past*, and the LFO strip's
/// buttons stand for the LFO's own sub-parameters rather than for a module's.
/// So the one part of the plugin a user cannot MIDI-learn was the part that is
/// nothing but switches.
///
/// \note The waveform well was not in the report, and is here because the answer
/// it gave was the one a combo box gave before #93 closed the first time: a right
/// press dropped the list of values and the host got no say. It raises the menu
/// now, with those values in it.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A module's bypass and the LFO strip's switches raise their menus too",
          "[gui][modules][lfo][menu]")
{
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    KnobUnderTest const knob(instance);
    auto &editor(instance.editor());

    auto *const pModuleUI(editor.regionInSlot(0));
    REQUIRE(pModuleUI != nullptr);
    auto *const pStrip(editor.lfoDisplay());
    REQUIRE(pStrip != nullptr);

    std::vector<juce::Component *> widgets{&bypassPill(*pModuleUI)};
    for (auto *const pButton : parameterButtons(*pStrip))
        widgets.push_back(pButton);

    // the pill, the LFO's own switch, N, T, D and the waveform well
    REQUIRE(widgets.size() == 6);

    std::vector<int> items;
    for (auto *const pWidget : widgets)
    {
        CAPTURE(pWidget->getName());
        REQUIRE(juce::Component::getNumCurrentlyModalComponents() == 0);

        pWidget->mouseDown(rightPressAt(*pWidget, pWidget->getLocalBounds().getCentre()));

        /// \note And the *parameter's* menu rather than any menu, which for the
        /// waveform is a real distinction: the list it used to drop is modal too.
        /// This one names the editor as its parent, being where the type-in field
        /// would have to take the keyboard from. \see the case above.
        REQUIRE(juce::Component::getNumCurrentlyModalComponents() == 1);
        auto *const pMenu(juce::Component::getCurrentlyModalComponent(0));
        REQUIRE(pMenu != nullptr);
        CHECK(static_cast<juce::Component const &>(editor).isParentOf(pMenu));

        items.push_back(pMenu->getNumChildComponents());
        juce::PopupMenu::dismissAllActiveMenus();
    }

    ////////////////////////////////////////////////////////////////////////////
    /// \note Three items are shared: the parameter's name, the rule under it and
    /// "Reset to default value". What a middle section adds is a rule of its own
    /// plus its rows -- and every one of these six has one, where a knob has a
    /// field to type into.
    ///
    ///   The two booleans -- the module's Bypass and the LFO's own switch --
    /// carry their state as one ticked row. Each sync button carries the four
    /// the mask can be, which is one more than the strip offers: Free is on a
    /// button only as the lit one pressed again. The waveform carries its eleven
    /// marks, which is the list a right press used to drop on its own.
    ////////////////////////////////////////////////////////////////////////////
    constexpr int shared{3};
    constexpr int rule{1};
    CHECK(std::ranges::count(items, shared + rule + 1) == 2);
    CHECK(std::ranges::count(items, shared + rule + 4) == 3);
    CHECK(std::ranges::count(items, shared + rule + LE::Parameters::LFO::NumberOfWaveforms) == 1);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The knob's menu is the one menu in the skin with a parent component,
/// and a parented juce::PopupMenu is the only one JUCE paints
/// LookAndFeel::drawResizableFrame() over -- a hard square in translucent black,
/// drawn after the children and so over the rounded background the theme had
/// just drawn. It read as a straight-edged rect showing through every corner.
/// \see issue #145 and Theme::drawResizableFrame().
///
/// \note Measured at the corner because that is the only place the two shapes
/// disagree: a round corner leaves the pixel unpainted and a square one does
/// not. No colour is named -- what is asserted is that *nothing* is there.
///
////////////////////////////////////////////////////////////////////////////////
TEST_CASE("The knob's menu keeps its rounded corners", "[gui][modules][menu]")
{
    /// \note The square branch of drawPopupMenuBackground(), where a corner is
    /// meant to be filled. \see issue #149.
    if (!juce::Desktop::canUseSemiTransparentWindows())
        return;

    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    instance.openEditor();
    auto &editor(instance.editor());

    editor.addUserAddedModule(static_cast<std::uint8_t>(SWTest::effectByStreamingName("Freeze")));
    editor.resyncModuleRack();

    auto *const pModuleUI(editor.regionInSlot(0));
    REQUIRE(pModuleUI != nullptr);

    auto &widget(pModuleUI->effectSpecificParameterControl(0).widget());
    widget.mouseDown(rightPressAt(widget, widget.getLocalBounds().getCentre()));

    REQUIRE(juce::Component::getNumCurrentlyModalComponents() == 1);
    auto *const pMenu(juce::Component::getCurrentlyModalComponent(0));
    REQUIRE(pMenu != nullptr);
    REQUIRE(static_cast<juce::Component const &>(editor).isParentOf(pMenu));

    auto const menu(pMenu->createComponentSnapshot(pMenu->getLocalBounds(), false, 1.0f));
    REQUIRE(menu.isValid());

    /// The four corners are outside the arc, so nothing may have painted them.
    CHECK(menu.getPixelAt(0, 0).isTransparent());
    CHECK(menu.getPixelAt(menu.getWidth() - 1, 0).isTransparent());
    CHECK(menu.getPixelAt(0, menu.getHeight() - 1).isTransparent());
    CHECK(menu.getPixelAt(menu.getWidth() - 1, menu.getHeight() - 1).isTransparent());

    /// \note And the menu was drawn at all, so that a background that stopped
    /// painting entirely could not pass the four above.
    CHECK(menu.getPixelAt(menu.getWidth() / 2, menu.getHeight() / 2).isOpaque());

    juce::PopupMenu::dismissAllActiveMenus();
}
