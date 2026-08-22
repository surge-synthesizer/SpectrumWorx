////////////////////////////////////////////////////////////////////////////////
///
/// moduleControlFocusTests.cpp
/// ---------------------------
///
///   What happens to a module control's keyboard focus when the control is
/// disabled under it -- which is what clicking a knob whose LFO is on does.
///
///   `ModuleKnob::mouseDown` calls `setEnabled( !isLFOEnabled() )` so that
/// juce::Slider's own drag handling cannot move a value the LFO owns. JUCE's
/// `Component::setEnabled( false )` sets the disabled flag *and then*, if the
/// component has keyboard focus, hands that focus to the parent -- and
/// `getWantsKeyboardFocus()` is `wantsKeyboardFocusFlag && !isDisabledFlag`
/// (juce_Component.cpp), so by the time our `focusLost()` runs the answer is
/// already false. There is no `--render` page for that: it needs a click, and a
/// click needs focus, which needs a peer.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/editorHarness.hpp"

/// \note Before anything that names SW::Module: the module chain downcasts a
/// node to it, and this is the header with the complete type.
#include "core/modules/moduleDSPAndGUI.hpp"

#include "gui/editor/auxiliaryComponents.hpp"
#include "gui/modules/moduleControl.hpp"
#include "gui/modules/moduleUI.hpp"
#include "gui/preferences.hpp" // hideCursorOnKnobDrag, for the fourth LFO gesture

#include "le/parameters/lfoImpl.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/spectrumworx/effects/baseParameters.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>
//------------------------------------------------------------------------------
namespace
{
using namespace LE;
using namespace LE::SW;

/// \brief What a case says when the screen will not give the window the keyboard.
/// \note The other half, SWTest::noWindow, is shared: every case that puts a
/// component on the desktop needs it, and this file is no longer the only one.
constexpr char keyboardRefused[]{
    "The window server did not hand this window the keyboard -- a locked screen, an unattended "
    "session, or focus-stealing prevention. These cases can only test what focus does if they "
    "are given it."};

/// \brief The editor, on the desktop, so that JUCE will hand out keyboard focus.
///
/// \note `Component::grabKeyboardFocusInternal` returns early on `!isShowing()`,
/// and `isShowing()` walks up to a peer -- so an editor that is merely
/// constructed can never be focused, which is why `sw-show-ui --render` trips
/// JUCE's own `jassert( isShowing() || isOnDesktop() )` and renders anyway. A
/// window is the only way to drive focus, and if this machine has no window
/// server the cases below skip rather than fail.
class DesktopEditor
{
  public:
    explicit DesktopEditor(SWTest::Instance &instance) : instance_(instance)
    {
        instance_.openEditor();
        editor().setVisible();
        editor().addToDesktop(0);
        editor().toFront(true);
    }

    /// \note The editor is destroyed while it is still on the desktop, which is
    /// the order a host closes a window in: the view goes away with the editor
    /// rather than before it. juce::Component's destructor calls
    /// removeFromDesktop() itself.
    ~DesktopEditor() { instance_.closeEditor(); }

    GUI::SpectrumWorxEditor &editor() const { return instance_.editor(); }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Whether this window was actually given the keyboard.
    ///
    /// \note It asks, rather than inspecting, and the difference is the whole
    /// point. This read `isShowing()`, which answers "is there a window on a
    /// screen" -- but `Component::takeKeyboardFocus` wants more than that:
    ///
    ///     peer->grabFocus();
    ///     if (! peer->isFocused() || currentlyFocusedComponent == this)
    ///         return;                       // focus silently NOT taken
    ///
    /// -- and whether the peer ends up focused is the window manager's to
    /// decide. A locked screen is the case that found this: the window is made,
    /// mapped and showing, `isShowing()` is true, and the lock screen keeps the
    /// keyboard, so every `hasKeyboardFocus()` below is false and the cases fail
    /// on a machine that is working correctly. Focus-stealing prevention and an
    /// unattended session do the same thing.
    ///
    ///   So the question is put the way the cases put it -- grab, then look --
    /// and a no is a skip rather than a failure. The grab is not a side effect
    /// worth hiding from: every case here starts by taking focus anyway.
    ///
    ////////////////////////////////////////////////////////////////////////////
    bool tookTheKeyboard() const
    {
        if (!editor().isShowing())
            return false;
        editor().grabKeyboardFocus();
        return editor().hasKeyboardFocus(true);
    }

  private:
    SWTest::Instance &instance_;
}; // class DesktopEditor

/// \brief A left-button event over \p component, \p offset from its centre. The
/// press is at the centre either way, which is what a slider measures a drag
/// against.
///
/// \note Hand-built and handed straight to `Component::mouseDown()` and friends,
/// which is *half* a mouse: JUCE tracks a gesture in the MouseInputSource, and
/// nothing here touches that, so `isMouseOverOrDragging()` stays false
/// throughout. That is enough for everything below, and the whole mouse is not
/// available: driving `ComponentPeer::handleMouseEvent` -- the entry point the
/// window server itself uses -- was tried and never reaches the component,
/// because `Desktop::findComponentAt` ends at `NSViewComponentPeer::contains`,
/// which is an NSView hit test, and a plain test binary is not an app whose
/// views the window server will hit-test. See the second case for what that
/// costs.
juce::MouseEvent eventOver(juce::Component &component, juce::Point<float> const offset,
                           bool const dragged)
{
    auto const centre(component.getLocalBounds().getCentre().toFloat());
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre + offset,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, &component, &component, juce::Time(), centre,
                            juce::Time(), 1, dragged);
}

/// Press, drag, release. The drag is upwards, because the knobs are
/// RotaryVerticalDrag, and far enough to clear setMouseDragSensitivity( 800 ).
void dragKnob(juce::Component &knob)
{
    auto const up(juce::Point<float>(0.0f, -200.0f));
    knob.mouseDown(eventOver(knob, {}, false));
    knob.mouseDrag(eventOver(knob, up, true));
    knob.mouseUp(eventOver(knob, up, true));
}

/// \brief The first effect-specific control of \p moduleUI that is a knob.
///
/// \note By parameter index rather than by walking the children: the widget
/// storage is a compile-time chain of one base class per parameter, so there is
/// no runtime list of controls to iterate.
template <typename Widget> GUI::ModuleControlBase *firstControlOfType(GUI::ModuleUI &moduleUI)
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

GUI::ModuleControlBase *firstKnob(GUI::ModuleUI &moduleUI)
{
    return firstControlOfType<GUI::ModuleKnob>(moduleUI);
}

/// \brief The one strip of \p effectName, in slot 0, with nothing selected in it.
GUI::ModuleUI &stripFor(GUI::SpectrumWorxEditor &editor, char const *const effectName)
{
    auto const effect(SWTest::effectByStreamingName(effectName));
    editor.addUserAddedModule(static_cast<std::uint8_t>(effect));
    editor.resyncModuleRack();
    auto *const pModuleUI(editor.regionInSlot(0));
    REQUIRE(pModuleUI != nullptr);
    return *pModuleUI;
}

/// One press and release over the centre of \p widget.
void clickOnce(juce::Component &widget)
{
    widget.mouseDown(eventOver(widget, {}, false));
    widget.mouseUp(eventOver(widget, {}, false));
}

/// \brief The Gain/Wet/frequency-range strip, found in the tree rather than
/// asked for: the editor hands it out to ModuleUI alone.
GUI::SharedModuleControls *sharedControlsUnder(juce::Component &component)
{
    for (auto *const pChild : component.getChildren())
    {
        if (auto *const pShared = dynamic_cast<GUI::SharedModuleControls *>(pChild))
            return pShared;
        if (auto *const pFound = sharedControlsUnder(*pChild))
            return pFound;
    }
    return nullptr;
}

/// The shared Gain knob -- the one the report's focus was in.
juce::Component &sharedGain(GUI::SharedModuleControls &shared)
{
    namespace Base = Effects::BaseParameters;
    return shared.controlForParameter(LE::Parameters::IndexOf<Base::Parameters, Base::Gain>::value)
        .widget();
}

/// \brief One wheel notch over \p widget, positive being away from the user.
///
/// \note 0.3 rather than 1: GUI::ComboBox counts five notches to a row, and a
/// notch is about what a wheel detent sends. \see
/// tests/gui/discreteParameterTests.cpp, which is where what the wheel *does* to
/// a list is pinned; what this file adds is the half that needs a window.
void scrollOnce(juce::Component &widget, float const deltaY)
{
    juce::MouseWheelDetails wheel{};
    wheel.deltaX = 0;
    wheel.deltaY = deltaY;
    wheel.isReversed = false;
    wheel.isSmooth = false;
    wheel.isInertial = false;

    /// \note Its own event rather than eventOver()'s, which carries the left
    /// button: a wheel with a button held is a drag as far as JUCE is concerned
    /// and GUI::ComboBox declines it, exactly as juce::Slider does.
    auto const centre(widget.getLocalBounds().getCentre().toFloat());
    juce::MouseEvent const event(juce::Desktop::getInstance().getMainMouseSource(), centre,
                                 juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &widget,
                                 &widget, juce::Time(), centre, juce::Time(), 1, false);
    widget.mouseWheelMove(event, wheel);
}
} // anonymous namespace

TEST_CASE("Dragging a knob the LFO owns moves nothing and deselects nothing", "[gui][modules][lfo]")
{
    SWTest::HostSideJuce const juceIsUp;

    if (!SWTest::aWindowCanBeMade())
        SKIP(SWTest::noWindow);

    SWTest::Instance instance;
    DesktopEditor const window(instance);
    if (!window.tookTheKeyboard())
        SKIP(keyboardRefused);

    auto &editor(window.editor());
    editor.addUserAddedModule(0);
    editor.resyncModuleRack();
    auto *const pModuleUI(editor.regionInSlot(0));
    REQUIRE(pModuleUI != nullptr);

    auto *const pControl(firstKnob(*pModuleUI));
    REQUIRE(pControl != nullptr);
    auto &control(*pControl);
    auto &knob(control.widget());

    // The LFO, on, the way the LFO display's switch sets it.
    control.lfo().parameters().set<Parameters::LFOImpl::Enabled>(true);
    REQUIRE(control.isLFOEnabled());

    auto const valueBefore(control.getValue());

    // The click's own half: ModuleKnob::mouseClickCanGrabFocus is true, so a
    // press on a knob takes the keyboard focus, and taking it is what makes the
    // control the selected one -- which is what puts its LFO on screen.
    knob.grabKeyboardFocus();
    REQUIRE(knob.hasKeyboardFocus(false));
    REQUIRE(editor.activeControl() == &control);

    // The reported case. Against the previous implementation mouseDown disabled
    // the knob, so JUCE handed that focus straight back to the parent and
    // ModuleControlImpl::focusLost's `LE_ASSERT( getWantsKeyboardFocus() )`
    // fired -- SIGINT in a debug build.
    dragKnob(knob);

    // The requirement that disabling was there to meet: the LFO owns this value
    // and the mouse may not move it.
    CHECK(control.getValue() == valueBefore);

    // ...and the three things disabling cost. The control is still live, still
    // focused, and still the selected one -- so its LFO display is still on
    // screen, which is the whole reason for pressing a modulated knob.
    CHECK(knob.isEnabled());
    CHECK(knob.hasKeyboardFocus(false));
    CHECK(editor.activeControl() == &control);
}

TEST_CASE("An LFO switches every gesture that would move the knob under it", "[gui][modules][lfo]")
{
    // \note The other side of the case above, and it is the switches rather than
    // a second drag. Moving an unmodulated knob is what proves the first case
    // blocks something -- but a value only moves through
    // ModuleKnob::valueChanged(), which asserts isMouseOverOrDragging(), and no
    // synthesised event sets that (see the note on eventOver). What can be said
    // without a real mouse is that the three gestures agree with each other: the
    // drag joined the wheel and the double click, which is the whole change.
    SWTest::HostSideJuce const juceIsUp;

    if (!SWTest::aWindowCanBeMade())
        SKIP(SWTest::noWindow);

    SWTest::Instance instance;
    DesktopEditor const window(instance);
    if (!window.tookTheKeyboard())
        SKIP(keyboardRefused);

    auto &editor(window.editor());
    editor.addUserAddedModule(0);
    editor.resyncModuleRack();
    auto *const pModuleUI(editor.regionInSlot(0));
    REQUIRE(pModuleUI != nullptr);

    auto *const pControl(firstKnob(*pModuleUI));
    REQUIRE(pControl != nullptr);
    auto &control(*pControl);
    auto &knob(dynamic_cast<juce::Slider &>(control.widget()));

    REQUIRE(!control.isLFOEnabled());

    // \note Selecting it first: the wheel is off on an unselected knob whatever
    // the LFO says -- moduleControlDeactivated() switches it off so that
    // scrolling the rack does not move whatever it passes over -- and
    // moduleControlActivated() is what asks syncMouseWheelAndLFOState().
    knob.grabKeyboardFocus();
    REQUIRE(editor.activeControl() == &control);

    CHECK(knob.isScrollWheelEnabled());
    CHECK(knob.isDoubleClickReturnEnabled());

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The fourth gesture, and it was the odd one out until 16.08.2026.
    /// Pressing a knob hands the mouse over to it -- cursor hidden, movement
    /// unbounded -- and that was asked of the preference alone, not of whether
    /// there was a drag to hand it over *for*. On an LFO'd knob there is not:
    /// `mouseDrag` returns at the top and the value cannot move. So the cursor
    /// went invisible for a gesture that did nothing, and JUCE put it back inside
    /// the knob's bounds on release rather than where the press had been --
    /// which is the jump in issue #82.
    ///
    /// \note The preference is *set* rather than assumed, and put back after.
    /// `GUI::preferences()` is one object for the process, so whether it happens
    /// to be on here depends on which cases ran first -- and the knob answers
    /// `false` when it is off for a reason that has nothing to do with the LFO,
    /// which would leave this passing without testing anything. Asserting it
    /// first caught exactly that: the case passed alone and failed in the full
    /// suite, behind preferencesTests.cpp.
    ///
    ////////////////////////////////////////////////////////////////////////////
    struct HideCursorPreference
    {
        bool const previous{GUI::preferences().hideCursorOnKnobDrag()};
        HideCursorPreference() { GUI::preferences().setHideCursorOnKnobDrag(true); }
        ~HideCursorPreference() { GUI::preferences().setHideCursorOnKnobDrag(previous); }
    } const hideCursor;

    REQUIRE(GUI::preferences().hideCursorOnKnobDrag());
    auto &asKnob(dynamic_cast<GUI::Knob &>(control.widget()));
    CHECK(asKnob.hidesCursorWhileDragging());

    auto const valueBefore(control.getValue());

    control.lfo().parameters().set<Parameters::LFOImpl::Enabled>(true);
    control.lfoStateChanged();

    CHECK(!knob.isScrollWheelEnabled());
    CHECK(!knob.isDoubleClickReturnEnabled());
    CHECK(!asKnob.hidesCursorWhileDragging());
    // ...and the third, which used to be spelt setEnabled( false ) inside
    // mouseDown and is now a return at the top of mouseDrag.
    dragKnob(knob);
    CHECK(control.getValue() == valueBefore);
    CHECK(knob.isEnabled());
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note T3.2. The LFO display and the shared module controls are children of
/// the *editor*, not of the strip, and each holds a raw `ModuleUI *` into one.
/// `detachFrom()` is what drops them before the strip is freed -- and it used to
/// decide by asking the editor which control was *active* and which module was
/// *selected*, rather than asking those two widgets what they were pointing at.
///
///   The two are not the same the moment a control is deactivated: deactivation
/// clears the editor's records and leaves the widgets alive, deliberately, so
/// that moving between controls does not destroy and rebuild them. So a control
/// that lost focus before its module was removed left both widgets behind,
/// pointing into freed memory, still parented and still painted --
/// `ModuleKnob::paint` reads `moduleUI().pModule_` straight through the hole.
///
///   This is the 608f0773 bug class over again: a guard keyed on the wrong
/// pointer. An `-fsanitize=address` build reports the paint below as a
/// heap-use-after-free on a freed `ModuleUI`.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A strip removed after its control was deactivated leaves nothing pointing at it",
          "[gui][modules][lfo]")
{
    SWTest::HostSideJuce const juceIsUp;

    if (!SWTest::aWindowCanBeMade())
        SKIP(SWTest::noWindow);

    SWTest::Instance instance;
    DesktopEditor const window(instance);
    if (!window.tookTheKeyboard())
        SKIP(keyboardRefused);

    auto &editor(window.editor());

    // Two modules, so that removing one leaves a rack to go on painting.
    editor.addUserAddedModule(0);
    editor.addUserAddedModule(0);
    editor.resyncModuleRack();
    auto *const pFirstStrip(editor.regionInSlot(0));
    REQUIRE(pFirstStrip != nullptr);
    REQUIRE(editor.regionInSlot(1) != nullptr);

    // Selecting a control is what builds the LFO display and points the shared
    // controls at this strip.
    auto *const pControl(firstKnob(*pFirstStrip));
    REQUIRE(pControl != nullptr);
    pControl->widget().grabKeyboardFocus();
    REQUIRE(pControl->widget().hasKeyboardFocus(false));
    REQUIRE(editor.activeControl() == pControl);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And then deselecting it, which is the step that made the old guards
    /// answer no. The editor forgets which control was active and which module
    /// was selected; the widgets pointing into the strip are only *disabled*,
    /// their destruction deferred in case the user is on their way to another
    /// control.
    ///
    ////////////////////////////////////////////////////////////////////////////
    editor.grabKeyboardFocus();
    REQUIRE(editor.activeControl() == nullptr);

    // The strip goes. detachFrom() runs from in here.
    editor.removeModule(*pFirstStrip);
    editor.resyncModuleRack();
    CHECK(editor.regionInSlot(1) == nullptr);

    /// \note And the paint that used to read the freed strip. Into an image
    /// rather than to a screen, which is what makes it something a test can do
    /// at all -- and it reaches the same `paint()` on the same components.
    juce::Image canvas(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
    {
        juce::Graphics graphics(canvas);
        editor.paintEntireComponent(graphics, true);
    }

    CHECK(editor.regionInSlot(0) != nullptr);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The shared controls' own version of the case above, and it crashes
/// rather than reads freed memory. `detachFrom()` destroys them while the
/// keyboard focus is still *inside* them, and JUCE answers a focused component
/// going away by handing the focus to the next thing that will take it -- which,
/// two strips down from the editor, is another `ModuleUI`. That is a synchronous
/// `focusGained` -> `activate()` -> `moduleActivated()`, from inside
/// `std::optional::reset()`.
///
///   And `reset()` destroys the value *before* it clears the engaged flag
/// (libc++ `__optional_destruct_base::reset`), so `sharedModuleControls_` still
/// answers `has_value()` at that moment: `moduleActivated()` takes the else
/// branch, calls `updateForActiveModule()`, and writes `gain_` -- whose
/// `juce::Slider` destructor has already run and nulled its Pimpl.
///
///     0  juce::Slider::Pimpl::setValue
///     1  SharedModuleControls::updateForActiveModule
///     2  SpectrumWorxEditor::moduleActivated
///     3  ModuleUI::focusGained
///     ...
///     8  juce::Component::removeChildComponent
///     9  juce::Component::~Component
///    10  SpectrumWorxEditor::detachFrom
///    11  SpectrumWorxEditor::resyncModuleRack
///
/// \note Two strips, and the *second* one removed: with one strip there is no
/// other `ModuleUI` for the focus to land on and nothing re-enters.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Removing the strip the shared controls are focused in activates nothing",
          "[gui][modules][lfo]")
{
    SWTest::HostSideJuce const juceIsUp;

    if (!SWTest::aWindowCanBeMade())
        SKIP(SWTest::noWindow);

    SWTest::Instance instance;
    DesktopEditor const window(instance);
    if (!window.tookTheKeyboard())
        SKIP(keyboardRefused);

    auto &editor(window.editor());

    editor.addUserAddedModule(0);
    editor.addUserAddedModule(0);
    editor.resyncModuleRack();
    auto *const pSecondStrip(editor.regionInSlot(1));
    REQUIRE(editor.regionInSlot(0) != nullptr);
    REQUIRE(pSecondStrip != nullptr);

    // Selecting a control in the second strip is what builds the shared controls
    // and points them at it.
    auto *const pControl(firstKnob(*pSecondStrip));
    REQUIRE(pControl != nullptr);
    pControl->widget().grabKeyboardFocus();
    REQUIRE(editor.sharedModuleControlsActive());

    // ...and then the click on Gain, which moves the focus out of the strip and
    // into the shared controls while leaving the strip selected.
    auto *const pShared(sharedControlsUnder(editor));
    REQUIRE(pShared != nullptr);
    auto &gain(sharedGain(*pShared));
    gain.grabKeyboardFocus();
    REQUIRE(gain.hasKeyboardFocus(false));
    REQUIRE(editor.sharedModuleControlsActiveAndFocused());
    REQUIRE(editor.selectedModule() == pSecondStrip);

    // The eject button's path, and the crash.
    editor.removeModule(*pSecondStrip);
    editor.resyncModuleRack();

    CHECK(editor.regionInSlot(1) == nullptr);
    CHECK(editor.regionInSlot(0) != nullptr);

    // Nothing was activated on the way out: the strip that was selected is gone
    // and no other one was made selected in its place.
    CHECK(editor.selectedModule() == nullptr);
    CHECK(editor.activeControl() == nullptr);
    CHECK(!editor.sharedModuleControlsActive());

    /// \note And the rack still paints, which is what the case above pins for
    /// the deactivated-first path.
    juce::Image canvas(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
    {
        juce::Graphics graphics(canvas);
        editor.paintEntireComponent(graphics, true);
    }
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Issue #65. A module control has to be selected before it may be
/// changed, and taking the selection used to be the whole of the first press --
/// so a button took two clicks to toggle once and a trigger fired on the second.
/// Both cases below press exactly once.
///
/// \note They need a real window for the same reason the two above do: the
/// selection *is* the keyboard focus, and JUCE will not hand focus to a
/// component with no peer.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("One press on a module button selects it and toggles it", "[gui][modules]")
{
    SWTest::HostSideJuce const juceIsUp;

    if (!SWTest::aWindowCanBeMade())
        SKIP(SWTest::noWindow);

    SWTest::Instance instance;
    DesktopEditor const window(instance);
    if (!window.tookTheKeyboard())
        SKIP(keyboardRefused);

    auto &editor(window.editor());
    // TuneWorx, because its twelve semitone toggles are the LED buttons the
    // report was about -- \see the Boolean arm of Detail::WidgetForParameterAux.
    auto &moduleUI(stripFor(editor, "TuneWorx"));

    auto *const pControl(firstControlOfType<GUI::ModuleLEDTextButton>(moduleUI));
    REQUIRE(pControl != nullptr);
    auto &control(*pControl);
    auto &button(control.widget());

    REQUIRE(editor.activeControl() != &control);
    auto const valueBefore(control.getValue());

    clickOnce(button);

    // It selected -- which is what puts the control's LFO on screen...
    CHECK(editor.activeControl() == &control);
    CHECK(button.hasKeyboardFocus(false));
    // ...and it toggled, which is the half that used to need a second press.
    CHECK(control.getValue() != valueBefore);
}

TEST_CASE("One press on a module trigger selects it and fires it", "[gui][modules]")
{
    SWTest::HostSideJuce const juceIsUp;

    if (!SWTest::aWindowCanBeMade())
        SKIP(SWTest::noWindow);

    SWTest::Instance instance;
    DesktopEditor const window(instance);
    if (!window.tookTheKeyboard())
        SKIP(keyboardRefused);

    auto &editor(window.editor());
    // Freeze, whose two triggers are "Freeze" and "Melt": a trigger that fires
    // on the second press is one that did nothing when it was pressed.
    auto &moduleUI(stripFor(editor, "Freeze"));

    auto *const pControl(firstControlOfType<GUI::TriggerButton>(moduleUI));
    REQUIRE(pControl != nullptr);
    auto &control(*pControl);
    auto &button(control.widget());

    REQUIRE(editor.activeControl() != &control);
    REQUIRE(control.getValue() == 0);

    /// \note The press alone, with no release: TriggerButton is
    /// `setTriggeredOnMouseDown( true )`, so that is the whole gesture.
    button.mouseDown(eventOver(button, {}, false));

    CHECK(editor.activeControl() == &control);
    CHECK(control.getValue() != 0);
}

TEST_CASE("One press on a module combo box selects it and opens its menu", "[gui][modules]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The third widget, and the one the first pass at issue #65 missed:
    /// `Detail::WidgetForParameterAux` maps Boolean to an LED, Trigger to a
    /// button and **Enumerated to a combo box**, and only the first two were
    /// fixed. A combo box is the same complaint in a different shape -- the
    /// first press selected the control and swallowed itself, so the menu took
    /// two clicks.
    ///
    /// \note The modal component count rather than a screenshot: the menu is
    /// asynchronous and there is no message loop in a test binary, but the
    /// component is made and entered into a modal state where the menu is shown,
    /// which is the half this case is about.
    ///
    /// \note It read `ComboBox::menuActive()` until 21.08.2026, and the flag is
    /// no longer set because the box no longer drops a bare list of values: both
    /// buttons raise the *parameter's* menu, which is that list plus the LFO
    /// switch, the default and the host's entries. \see
    /// DiscreteParameter::mouseDown() and issue #93. What the case claims has not
    /// moved -- one press, selected and a menu up -- only what answers it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    if (!SWTest::aWindowCanBeMade())
        SKIP(SWTest::noWindow);

    SWTest::Instance instance;
    DesktopEditor const window(instance);
    if (!window.tookTheKeyboard())
        SKIP(keyboardRefused);

    auto &editor(window.editor());
    // Swappah, whose Target and Swap order are the enumerations the report named.
    auto &moduleUI(stripFor(editor, "Swappah"));

    auto *const pControl(firstControlOfType<GUI::DiscreteParameter>(moduleUI));
    REQUIRE(pControl != nullptr);
    auto &control(*pControl);
    auto &comboBox(dynamic_cast<GUI::ComboBox &>(control.widget()));

    REQUIRE(editor.activeControl() != &control);
    REQUIRE(juce::Component::getNumCurrentlyModalComponents() == 0);

    comboBox.mouseDown(eventOver(comboBox, {}, false));

    // It selected -- which is what puts the control's LFO on screen...
    CHECK(editor.activeControl() == &control);
    // ...and the menu is up, which used to need a second press.
    CHECK(juce::Component::getNumCurrentlyModalComponents() == 1);

    /// \note And it is the parameter's menu rather than a bare list of values.
    /// The parameter menu names the editor as its parent, so that the type-in
    /// field a knob has can take the keyboard; a list of values is a desktop
    /// window of its own. Whose child it is is which menu it is.
    auto *const pMenu(juce::Component::getCurrentlyModalComponent(0));
    REQUIRE(pMenu != nullptr);
    CHECK(static_cast<juce::Component const &>(editor).isParentOf(pMenu));

    /// \note Before the editor goes. Its destructor dismisses menus itself, but
    /// a menu left up here would outlive the case rather than the editor.
    juce::PopupMenu::dismissAllActiveMenus();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Issue #124's other half. A module strip's combo box takes the module
/// selection before it will move, exactly as a press on it does -- otherwise a
/// wheel would edit one module's parameter while a different one stayed
/// selected, which is the state `moduleParameterChanged()`'s assertions exist to
/// catch.
///
/// \note Which is why the case is here rather than beside the other wheel cases:
/// focus needs a window, and this file is where the window is.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A wheel over a module combo box selects it and steps it", "[gui][modules][combo]")
{
    SWTest::HostSideJuce const juceIsUp;

    if (!SWTest::aWindowCanBeMade())
        SKIP(SWTest::noWindow);

    SWTest::Instance instance;
    DesktopEditor const window(instance);
    if (!window.tookTheKeyboard())
        SKIP(keyboardRefused);

    auto &editor(window.editor());
    auto &moduleUI(stripFor(editor, "Swappah"));

    auto *const pControl(firstControlOfType<GUI::DiscreteParameter>(moduleUI));
    REQUIRE(pControl != nullptr);
    auto &control(*pControl);
    auto &comboBox(dynamic_cast<GUI::ComboBox &>(control.widget()));

    REQUIRE(editor.activeControl() != &control);

    /// \note Put at the top of the list first, and the reason is the point of
    /// `MenuOrder`: a row is not a value. Swappah's Mode is declared Both,
    /// Magnitudes, Phases and *listed* Magnitudes, Phases, Both, so its default
    /// of zero is the **last** row -- and a step down the list from there is
    /// correctly refused. Starting at a known row is what makes this a case
    /// about the wheel rather than about which parameter it landed on.
    comboBox.setSelectedIndex(0);
    auto const first(comboBox.getValue());

    /// \note Away from the user, which is down the list. \see
    /// ComboBox::mouseWheelMove() and issue #124.
    scrollOnce(comboBox, +0.3f);

    // It selected, which is what puts the control's LFO on screen...
    CHECK(editor.activeControl() == &control);
    // ...and the row moved, without a menu having been opened at all.
    CHECK(comboBox.getValue() != first);
    CHECK_FALSE(comboBox.menuActive());

    // And back where it started, which is the gesture a user actually makes.
    scrollOnce(comboBox, -0.3f);
    CHECK(comboBox.getValue() == first);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The LFO owns the value, so a row the wheel moved to would be overwritten
/// by the next sweep. The same guard the menu is behind, and the same one
/// ModuleKnob turns its own wheel off with. \see syncMouseWheelAndLFOState().
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A wheel over a combo box the LFO owns moves nothing", "[gui][modules][lfo][combo]")
{
    SWTest::HostSideJuce const juceIsUp;

    if (!SWTest::aWindowCanBeMade())
        SKIP(SWTest::noWindow);

    SWTest::Instance instance;
    DesktopEditor const window(instance);
    if (!window.tookTheKeyboard())
        SKIP(keyboardRefused);

    auto &editor(window.editor());
    auto &moduleUI(stripFor(editor, "Swappah"));

    auto *const pControl(firstControlOfType<GUI::DiscreteParameter>(moduleUI));
    REQUIRE(pControl != nullptr);
    auto &control(*pControl);
    auto &comboBox(dynamic_cast<GUI::ComboBox &>(control.widget()));

    control.lfo().parameters().set<Parameters::LFOImpl::Enabled>(true);
    REQUIRE(control.isLFOEnabled());

    auto const opened(comboBox.getValue());
    scrollOnce(comboBox, -0.3f);
    CHECK(comboBox.getValue() == opened);
}
