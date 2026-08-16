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

#include "gui/modules/moduleControl.hpp"
#include "gui/modules/moduleUI.hpp"

#include "le/parameters/lfoImpl.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>
//------------------------------------------------------------------------------
#if JUCE_LINUX || JUCE_BSD
////////////////////////////////////////////////////////////////////////////////
///
/// \note Declared rather than included. <X11/Xlib.h> reaches a consumer of
/// juce_gui_basics only behind JUCE_GUI_BASICS_INCLUDE_XHEADERS, and turning
/// that on here would put X11's macros -- None, Status, Bool, Success, Complex
/// -- into a translation unit that also compiles Catch2 and our own headers.
/// Three functions and one opaque type is the whole of what aWindowCanBeMade()
/// below needs, and libX11 is already on this target's link line through JUCE.
///                                           (05.08.2026.) (SW port)
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
//------------------------------------------------------------------------------
namespace
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
///                                           (05.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////
bool aWindowCanBeMade()
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

/// \brief What a case says when it cannot run, of which there are two: no window
/// to put on a screen, and a window the screen will not give the keyboard to.
/// Shared, because both cases below can hit either and a skip should say which.
constexpr char noWindow[]{
    "No window manager: JUCE cannot put a component on the desktop here, and these cases need a "
    "real window to drive the keyboard with."};
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
    ///                                       (06.08.2026.) (SW port)
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
///                                           (03.08.2026.) (SW port)
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
    auto const effect(Effects::effectIndex(effectName));
    REQUIRE(effect >= 0);
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
} // anonymous namespace

TEST_CASE("Dragging a knob the LFO owns moves nothing and deselects nothing", "[gui][modules][lfo]")
{
    SWTest::HostSideJuce const juceIsUp;

    if (!aWindowCanBeMade())
        SKIP(noWindow);

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

    if (!aWindowCanBeMade())
        SKIP(noWindow);

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

    auto const valueBefore(control.getValue());

    control.lfo().parameters().set<Parameters::LFOImpl::Enabled>(true);
    control.lfoStateChanged();

    CHECK(!knob.isScrollWheelEnabled());
    CHECK(!knob.isDoubleClickReturnEnabled());
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

    if (!aWindowCanBeMade())
        SKIP(noWindow);

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

    if (!aWindowCanBeMade())
        SKIP(noWindow);

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

    if (!aWindowCanBeMade())
        SKIP(noWindow);

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
    /// \note `menuActive()` rather than a screenshot: the menu is asynchronous
    /// and there is no message loop in a test binary, but the flag is set where
    /// the menu is shown, which is the half this case is about. \see
    /// PopupMenu::menuActive().
    ///                                       (17.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    if (!aWindowCanBeMade())
        SKIP(noWindow);

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
    REQUIRE(!comboBox.menuActive());

    comboBox.mouseDown(eventOver(comboBox, {}, false));

    // It selected -- which is what puts the control's LFO on screen...
    CHECK(editor.activeControl() == &control);
    // ...and the menu is up, which used to need a second press.
    CHECK(comboBox.menuActive());

    /// \note Before the editor goes. Its destructor dismisses menus itself, but
    /// a menu left up here would outlive the case rather than the editor.
    juce::PopupMenu::dismissAllActiveMenus();
}
