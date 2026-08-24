////////////////////////////////////////////////////////////////////////////////
///
/// moduleUI.cpp
/// ------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "moduleUI.hpp"

#include "core/modules/moduleDSPAndGUI.hpp"
#include "gui/editor/spectrumWorxEditor.hpp"
#include "gui/preferences.hpp"

#include "le/parameters/lfo.hpp"
#include "le/parameters/printer.hpp"
#include "le/parameters/uiElements.hpp"
#include "le/spectrumworx/engine/setup.hpp"
#include "le/utility/platformSpecifics.hpp"

#include "le/utility/assert.hpp"
#include "le/utility/polymorphicDowncast.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace LE::SW::GUI
{

ModuleLEDTextButton::ModuleLEDTextButton(juce::Component &parent, unsigned int const x,
                                         unsigned int const y)
    : LEDTextButton(parent, x, y, nullptr)
{
    setName(control().name());
    //...mrmlj...for temporary test selection...
    setSize(moduleComboWidth, getHeight() + 2);
}

void ModuleLEDTextButton::clicked() { moduleParameterChanged(); }

////////////////////////////////////////////////////////////////////////////////
///
/// \note **Both, rather than either/or.** A module control has to be the selected
/// one before it may be changed -- that is what puts its LFO on screen -- and a
/// button that spent its first press on the selection would need two clicks to
/// toggle once, which is not how a button behaves anywhere else.
///
///   Nothing about selection needs the press thrown away. `grabKeyboardFocus()`
/// delivers `focusGained` synchronously, which is where
/// `ModuleControlImpl::reportActiveControl()` runs, so by the time the button's
/// own handler is reached this control is already the active one and
/// `moduleParameterChanged()`'s `LE_ASSERT( isActive() )` holds.
///
/// \note And only if the focus was actually taken. `Component::takeKeyboardFocus`
/// is the window manager's to refuse -- see the note on `tookTheKeyboard()` in
/// tests/gui/moduleControlFocusTests.cpp -- and a control that is not selected
/// must not publish a value, so a refusal leaves the press selecting and nothing
/// else.
///
/// \note The SafePointer guards the gap rather than a known crash: taking focus
/// runs the *previous* holder's `focusLost`, and that reaches the editor, which
/// retires an LFO strip and can drop the shared controls. This widget is a
/// module's own and never one of those, so there is nothing to catch today.
///
////////////////////////////////////////////////////////////////////////////////

void ModuleLEDTextButton::mouseDown(juce::MouseEvent const &event)
{
    /// \note The right button raises the parameter's menu rather than toggling
    /// anything, exactly as it does on a knob -- and it does not take the
    /// selection on the way, also as on a knob: nothing the menu offers needs
    /// this control to be the active one. \see issue #93.
    if (event.mods.isPopupMenu())
        return showParameterMenu(event);

    if (!hasDirectFocus())
    {
        juce::Component::SafePointer<juce::Component> const self(this);
        grabKeyboardFocus();
        if (!self || !hasDirectFocus())
            return;
    }

    if (!isLFOEnabled())
        LEDTextButton::mouseDown(event);
}

void ModuleLEDTextButton::paintButton(juce::Graphics &g, bool const isMouseOverButton,
                                      bool const isButtonDown)
{
    /// \note A combo box's selected background, borrowed: this button stands
    /// where one does and says it is the selected control the same way. It was
    /// `paintImage( ModuleComboOn )` while that was a file.
    if (control().isActive())
        FramePainter::paint(g,
                            juce::Rectangle<float>(0, -1, static_cast<float>(moduleComboWidth),
                                                   static_cast<float>(moduleComboHeight)),
                            moduleComboFrame, ColourMap::getColour(ColourMap::FocusHalo),
                            ColourMap::getColour(ColourMap::ComboBackground), true /*halo*/);
    g.setOrigin(3, 1);
    LEDTextButton::paintButton(g, isMouseOverButton, isButtonDown);
}

TriggerButton::TriggerButton(juce::Component &parent, unsigned int const x, unsigned int const y)
{
    setName(control().name());

    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setClickingTogglesState(false);
    addToParentAndShow(parent, *this);

    // the extra height is the caption's room under the face. \see paintButton()
    setBounds(x, y, ModuleUI::width, TriggerButtonStyle::diameter + 20);

    setTriggeredOnMouseDown(true);

    addToParentAndShow(parent, *this);
}

void TriggerButton::setValue(param_type const newValue)
{
    setState(newValue ? buttonDown : buttonNormal);
}

/// \note The artwork is square and holds a circle, and paintButton() centres it
/// across the strip at the top. The thirteen pixels under it are the caption.
juce::Rectangle<int> TriggerButton::faceBounds() const
{
    auto const face(static_cast<int>(TriggerButtonStyle::diameter));
    return {(ModuleUI::width - face) / 2, 0, face, face};
}

bool TriggerButton::isOnFace(juce::Point<int> const position) const
{
    return isOnRoundFace(faceBounds(), position);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The same either/or, and the same fix: a trigger that fires on the
/// second press is a trigger that did not fire when it was pressed. \see
/// ModuleLEDTextButton::mouseDown() above for why taking the focus first is
/// enough to make this control the active one.
///
/// \note **The right button does not fire a trigger**, exactly as it does not
/// move a knob, and off the face it belongs to the strip behind it -- which for a
/// trigger is most of the widget: eight pixels either side of the circle, the
/// caption under it and the four corners of the artwork. Freeze's two are the
/// ones this was noticed on. On the face there is nothing for it to raise yet: a
/// trigger has no parameter menu of its own, where a knob has. \see issue #92 and
/// Knob::mouseDown().
///
////////////////////////////////////////////////////////////////////////////////

void TriggerButton::mouseDown(juce::MouseEvent const &e)
{
    if (e.mods.isPopupMenu())
    {
        if (isOnFace(e.getPosition()))
            showParameterMenu(e);
        else
            passMousePressToParent(*this, e);
        return;
    }

    if (!hasDirectFocus())
    {
        juce::Component::SafePointer<juce::Component> const self(this);
        grabKeyboardFocus();
        if (!self || !hasDirectFocus())
            return;
    }

    if (!isLFOEnabled())
    {
        juce::Button::mouseDown(e);
        moduleParameterChanged();
    }
}

/// \note The release is guarded too, and not only for symmetry: the press is
/// what fires (setTriggeredOnMouseDown), so all an unguarded release adds is a
/// second moduleParameterChanged() -- an edit published to the engine and the
/// host for a trigger that was never pulled.
void TriggerButton::mouseUp(juce::MouseEvent const &e) noexcept
{
    if (e.mods.isPopupMenu())
        return;

    if (!isLFOEnabled())
    {
        juce::Button::mouseUp(e);
        moduleParameterChanged();
    }
}

/// \note The caption sits above the face in the paint order and below it on the
/// screen, which is what Detail::paintTextButton() did for this and still does
/// for an LED button. It is spelled out here because the face is no longer an
/// Artwork to hand that function.
void TriggerButton::paintButton(juce::Graphics &graphics, bool const isMouseOverButton,
                                bool const isButtonDown)
{
    auto const face(faceBounds());

    graphics.setColour(ColourMap::getColour(ColourMap::Text));
    graphics.setFont(DrawableText::defaultFont());
    graphics.drawFittedText(getName(), 0, face.getHeight() + 3, getWidth(), 17,
                            juce::Justification::horizontallyCentred, 1);

    bool const fade(isMouseOverButton && !isButtonDown);
    if (fade)
        graphics.beginTransparencyLayer(PointerFeedback::over);

    paintTriggerButton(graphics, face.toFloat(), isButtonDown || getToggleState());

    if (fade)
        graphics.endTransparencyLayer();

    if (control().isActive())
        KnobPainter::paintFocusRing(graphics, face.getCentre().toFloat(),
                                    static_cast<float>(face.getWidth()) / 2);
}

ModuleKnob::ModuleKnob(juce::Component &parent, unsigned int const x, unsigned int const y)
    : Knob(parent, x, y, marginForGlow * 2,
           std::max<unsigned int>(marginForGlow * 2, spaceForText)),
      polarity_(Unipolar), diameter_(diameter)
{
    setScrollWheelEnabled(false);
}

void ModuleKnob::setupForParameter(Polarity const polarity, unsigned int const knobDiameter,
                                   Quantization const quantizationType,
                                   std::uint8_t const quantizationStep)
{
    auto const &info(control().info());
    Knob::setupForParameter(info.name, knobDiameter, info.default_);
    //LE_ASSERT( !isLFOEnabled() ); //...mrmlj...when turning the GUI on or off...
    setDoubleClickReturnValue(!isLFOEnabled(), info.default_);
    quantization_ = quantizationType;
    polarity_ = polarity;
    diameter_ = knobDiameter;
    switch (quantization_)
    {
    case Fixed:
        setRange(info.minimum, info.maximum, quantizationStep);
        break;
    case FrequencyInHertz:
        LE_ASSERT(quantizationStep == 1);
        break;
    case TimeInMilliseconds:
        LE_ASSERT(quantizationStep == 0 || quantizationStep == 1);
        break;
        LE_DEFAULT_CASE_UNREACHABLE();
    }
}

/// \note The drag is blocked here rather than by disabling the control:
/// `Component::setEnabled( false )` hands the keyboard focus to the parent, so
/// pressing a knob whose LFO is on would deactivate the control and take its own
/// LFO display off the screen.
///
///   Blocking the one gesture is also what the other two do:
/// `lfoStateChanged()` keys `setScrollWheelEnabled()` and
/// `setDoubleClickReturnValue()` on the same question. This is the drag.
void ModuleKnob::mouseDrag(juce::MouseEvent const &event) noexcept
{
    if (isLFOEnabled())
        return;
    Knob::mouseDrag(event);
}

void ModuleKnob::paint(juce::Graphics &graphics)
{
    // valueToProportionOfLength() rather than getNormalisedValue(), so a skewed
    // range points where the artwork does
    auto const value(static_cast<float>(juce::Slider::valueToProportionOfLength(Knob::getValue())));

    paintModuleKnob(
        graphics,
        juce::Rectangle<float>(static_cast<float>(marginForGlow), static_cast<float>(marginForGlow),
                               static_cast<float>(diameter_), static_cast<float>(diameter_)),
        value, polarity_ == Bipolar, control().isActive());

    graphics.setColour(ColourMap::getColour(ColourMap::Text));
    {
        juce::Font font(Theme::singleton().labelFont());
        font.setHeight(15);
        graphics.setFont(font);
    }
    graphics.drawFittedText(
        getName(), ModuleUI::textMargin / 2, diameter_ + marginForGlow + (marginForGlow / 2),
        getWidth() - ModuleUI::textMargin, 18, juce::Justification::horizontallyCentred, 2, 0.6f);
}

void ModuleKnob::valueChanged() noexcept
{
    LE_ASSERT(isMouseOverOrDragging());
    moduleParameterChanged();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note juce::Slider calls these from mouseDown and mouseUp, which is exactly
/// where the host's gesture belongs: the drag is one edit however many values it
/// passes through. `EditorKnob` brackets the global knobs the same way.
///
/// \note Guarded on the LFO, as every other gesture on this widget is -- the
/// wheel, the double click and mouseDrag() all stand aside when the LFO owns the
/// value. A begin/end pair around an edit the widget refuses to make would be a
/// parameter the host could learn but the mouse cannot move.
///
////////////////////////////////////////////////////////////////////////////////

void ModuleKnob::startedDragging() noexcept
{
    Knob::startedDragging();
    if (!isLFOEnabled())
        beginGesture();
}

void ModuleKnob::stoppedDragging() noexcept
{
    if (control().gestureIsOpen())
        endGesture();
    Knob::stoppedDragging();
}

void ModuleKnob::lfoStateChanged()
{
    /// \note JUCE 8 split the out-parameter off into isDoubleClickReturnEnabled();
    /// getDoubleClickReturnValue() now just returns the value, which is all this
    /// wanted -- the flag it had to pass a variable for was discarded.
    double const defaultValue(getDoubleClickReturnValue());
    setDoubleClickReturnValue(!isLFOEnabled(), defaultValue);
    syncMouseWheelAndLFOState();
}

void ModuleKnob::updateForEngineSetupChanges(Engine::Setup const &engineSetup)
{
    ModuleKnob::param_type quantization;
    switch (quantization_)
    {
    case Fixed:
        return;
    case FrequencyInHertz:
        quantization = engineSetup.frequencyRangePerBin<ModuleKnob::param_type>();
        break;
    case TimeInMilliseconds:
        quantization = engineSetup.stepTime() * 1000;
        break;
        LE_DEFAULT_CASE_UNREACHABLE();
    }
    ParameterInfo const &parameterInfo(control().info());
    param_type const minimum(parameterInfo.minimum);
    param_type const maximum(parameterInfo.maximum);

    /// \note There is nothing to quantise against until the engine has been set
    /// up: with no sample rate there is no step time and no bin width, so
    /// stepTime() is zero and every assumption below is false. That is reachable
    /// rather than theoretical -- a session restored before activate() builds its
    /// module GUIs against an empty Setup, which is the "quantization > 0"
    /// assertion a standalone hits on startup -- and it is an ordering, not an
    /// error. SpectrumWorxEditor::updateForEngineSetupChanges() re-ranges every
    /// module once a real setup exists, which activate() now asks it to do.
    ///
    ///   A quantum as coarse as the parameter's whole range is the same problem
    /// from the other end, and it is what a large FFT size at a low sample rate
    /// produces for a parameter measured in milliseconds. Leaving the range alone
    /// beats deriving one whose minimum has been rounded up past its maximum.
    if ((quantization <= 0) || (quantization >= maximum))
        return;

    using namespace Math::PositiveFloats;
    LE_ASSUME(minimum >= 0);
    LE_ASSUME(maximum > 0);
    LE_ASSUME(quantization > 0);
    LE_ASSUME(maximum > quantization);
    bool const quantumAsMinimum((minimum < quantization) && (minimum != 0));
    double const adjustedMinimum(
        quantumAsMinimum ? quantization
                         : Math::convert<param_type>(ceil(minimum / quantization)) * quantization);
    double const adjustedMaximum(Math::convert<param_type>(floor(maximum / quantization)) *
                                 quantization);
    LE_ASSERT(adjustedMinimum >= minimum);
    LE_ASSERT(adjustedMaximum <=
              maximum + 250 * std::numeric_limits<float>::epsilon()); //...mrmlj...
    setRange(adjustedMinimum, adjustedMaximum, quantization);
}

////////////////////////////////////////////////////////////////////////////////
//
// ModuleKnob -- the right button's menu
// -------------------------------------
//
////////////////////////////////////////////////////////////////////////////////

/// \note getName(), which setupForParameter() took from the same
/// RuntimeInformation, and *not* the host's name for it: that reads "M3.Wet",
/// and the strip the knob is standing in already says which module this is.
/// \note The circle paint() draws, to the pixel: `marginForGlow` in from the top
/// left and `diameter_` across. Everything else the widget covers -- the glow
/// margin, and the eighteen pixels of caption below -- is the module strip
/// showing through.
bool ModuleKnob::isOnKnobFace(juce::Point<int> const position) const
{
    auto const margin(static_cast<int>(marginForGlow));
    auto const size(static_cast<int>(diameter_));
    return isOnRoundFace({margin, margin, size, size}, position);
}

void ModuleKnob::moduleControlActivated() { syncMouseWheelAndLFOState(); }
void ModuleKnob::moduleControlDeactivated() { setScrollWheelEnabled(false); }
void ModuleKnob::syncMouseWheelAndLFOState() { setScrollWheelEnabled(!isLFOEnabled()); }

#ifdef __GNUC__ //...mrmlj... GCC 4.6, Clang 2.8-3.2
unsigned int const ModuleKnob::spaceForText /* = 27*/;
#endif // __GNUC__

DiscreteParameter::DiscreteParameter(juce::Component &parent, unsigned int const x,
                                     unsigned int const y)
    : ComboBox(parent, moduleComboFrame, moduleComboWidth, moduleComboHeight)
{
    setName(control().name());
    DiscreteParameter::setTopLeftPosition(x, y);

    // no assertion that the default is zero: an enumerated parameter may name
    // any of its values, and a box showing one its parameter does not hold is
    // settled where the rows exist to select among. \see WidgetInitialiser::setup()
}

/// \note The third of the three, and the one that was missed the first time --
/// \see ModuleLEDTextButton::mouseDown() above for why taking the focus is
/// enough to make this control the active one. A combo box is the same complaint
/// as a button in a different shape: the first press selected the control and
/// swallowed itself, so opening the menu took two clicks. \see issue #65.

////////////////////////////////////////////////////////////////////////////////
///
/// \note **Both buttons, and the same menu.** The parameter's menu *is* the list
/// of values, plus the name over it, the LFO switch, the way back to the default
/// and whatever the host adds -- so there is nothing the plain list offered that
/// this does not, and no reason for the two buttons to answer differently. \see
/// issue #93 and addParameterValueEntries().
///
/// \note Which also settles what an LFO'd combo box does with a press. It used
/// to do nothing at all, so the one control that could turn the LFO back off was
/// the knob the LFO was not on; the menu comes up either way now, with the
/// values disabled and the switch live.
///
/// \note The left button takes the selection first and the right does not, which
/// is what each already did and what a knob does: a press that is going to
/// change the value has to make this the active control, and one that is only
/// going to open a menu about it does not.
///
////////////////////////////////////////////////////////////////////////////////

void DiscreteParameter::mouseDown(juce::MouseEvent const &event)
{
    if (!event.mods.isPopupMenu() && !hasDirectFocus())
    {
        juce::Component::SafePointer<juce::Component> const self(this);
        grabKeyboardFocus();
        if (!self || !hasDirectFocus())
            return;
    }

    showParameterMenu(event);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Takes the selection first, exactly as mouseDown() above does and for
/// the same reason: `moduleParameterChanged()` asserts that the control being
/// changed is the selected one, which is what puts its LFO on screen. A wheel
/// that edited a control while a different module stayed selected would be the
/// state those assertions exist to catch.
///
/// \note Which means a wheel needs a window, since focus does. That is a real
/// cost and it is paid where the click already pays it. \see
/// tests/gui/moduleControlFocusTests.cpp.
///
////////////////////////////////////////////////////////////////////////////////

void DiscreteParameter::mouseWheelMove(juce::MouseEvent const &event,
                                       juce::MouseWheelDetails const &wheel)
{
    if (!hasDirectFocus())
    {
        juce::Component::SafePointer<juce::Component> const self(this);
        grabKeyboardFocus();
        if (!self || !hasDirectFocus())
            return;
    }

    if (isLFOEnabled())
        return;

    ComboBox::mouseWheelMove(event, wheel);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The rows the box itself lists, in the order it lists them, ticked where
/// the selection is. So the right button reads name, values, then whatever the
/// host adds -- which is what the left button offers *plus* the host's entries,
/// rather than a second way of doing something the widget already does. \see
/// issue #93.
///
/// \note Disabled rather than hidden while an LFO owns the parameter, exactly as
/// "Reset to default value" is: the user can still read what the value is, and
/// choosing one would be overwritten by the next sweep.
///
/// \note `publishValue()` rather than `moduleParameterChanged()`, for the reason
/// ModuleControlBase::setValueFromText() gives: the latter asserts the mouse is
/// on the widget, and it is on the menu.
///
////////////////////////////////////////////////////////////////////////////////

void DiscreteParameter::addParameterValueEntries(juce::PopupMenu &menu)
{
    if (!hasValidSelection())
        return;

    auto const selected(getSelectedIndex());
    for (unsigned int row(0); row < numberOfItems(); ++row)
    {
        menu.addItem(getItemText(row), /*isEnabled*/ !isLFOEnabled(), /*isTicked*/ row == selected,
                     [pThis = juce::Component::SafePointer<DiscreteParameter>(this), row] {
                         if (!pThis)
                             return;
                         pThis->setSelectedIndex(row);
                         pThis->control().publishValue();
                     });
    }
}

void DiscreteParameter::selectionScrolled() { moduleParameterChanged(); }

#pragma warning(push)
#pragma warning(disable : 4355) // 'this' used in base member initializer list.

ModuleUI::ModuleUI(SpectrumWorxEditor &editor, LE::Utility::IntrusivePtr<SW::Module> pModule,
                   std::uint8_t const slotIndex)
    : editor_(editor), pModule_(std::move(pModule)), bypass_(*this), eject_(*this)
{
    LE_ASSERT(isThisTheGUIThread() ||
              juce::MessageManager::getInstance()->currentThreadHasLockedMessageManager());
    LE_ASSERT(pModule_);

    setSize(width, height);

    bypass_.setTopLeftPosition((ModuleUI::width / 2) - (bypass_.getWidth() / 2),
                               ModuleUI::height - 39 - bypassWidgetHeight);

    eject_.setTopLeftPosition((ModuleUI::width - eject_.getWidth()) / 2, -3);

    bypass_.addListener(this);
    eject_.addListener(this);

    setMouseClickGrabsKeyboardFocus(true);
    setWantsKeyboardFocus(true);

    //...mrmlj...for testing...
    //juce::Desktop::getInstance().getAnimator().animateComponent
    //(
    //    this,
    //    juce::Rectangle<int>
    //    (
    //        myHorizontalOffset, verticalOffset,
    //        width             , height
    //    ),
    //    0, 200, false, 0, 0
    //);

    LE_ASSERT_MSG(unsigned(this->getNumChildComponents()) == baseWidgets,
                  "Unexpected number of child widgets before the effect's own controls.");

    // the effect's own controls

    /// \note Parented before the effect's controls are built, and invisible until
    /// the caller shows it. Several of them walk `getParentComponent()` up to the
    /// editor as they are constructed -- `SpectrumWorxEditor::fromChild()` -- and
    /// a strip that is not in the hierarchy yet has nothing to walk. The old
    /// createGUI() did the same thing with an `editor.addChildComponent()` under
    /// `#ifndef NDEBUG`, for the same reason and only in a checked build.
    editor_.mainArea().addChildComponent(this);

    pWidgets_ = createModuleWidgets(module().effectTypeIndex(), *this);
    LE_ASSERT_MSG(pWidgets_ != nullptr, "No widgets for this effect index.");

    LE_ASSERT_MSG(getNumChildComponents() ==
                      (baseWidgets + module().numberOfEffectSpecificParameters()),
                  "Unexpected number of child widgets at end of ModuleUI constructor.");

    updateForEngineSetupChanges(editor_.engineSetup());

    /// \note A strip is never selected at the moment it is built, so its shared
    /// parameter controls are not showing and do not need updating.
    ///                                       (07.02.2014.) (Domagoj Saric)
    LE_ASSERT(!selected());
    setBypass(module().bypass());

    auto const effectParameters(module().numberOfEffectSpecificParameters());
    for (std::uint8_t parameter(0); parameter < effectParameters; ++parameter)
        setEffectParameter(parameter, module().getEffectParameter(parameter), AutomationOrPreset);

    moveToSlot(slotIndex);
}

#pragma warning(pop)

ModuleUI::~ModuleUI()
{
    LE_ASSERT(isThisTheGUIThread() ||
              juce::MessageManager::getInstance()->currentThreadHasLockedMessageManager());

    if (selected())
    {
        //...mrmlj...
        //LE_ASSERT( hasFocus() || editor()./*...mrmlj...sharedModuleControls().hasFocus()*/ sharedModuleControlsActive() );

        // Implementation note:
        //   Unforunately moveKeyboardFocusToSibling() does not just select the
        // next ModuleUI, if any, but also its 'first' control which is
        // undesired so we simply do nothing for now.
        //                                    (08.07.2011.) (Domagoj Saric)
        //moveKeyboardFocusToSibling( false );

        this->setWantsKeyboardFocus(false);
        editor().moduleDeactivated();
        editor().pSelectedModule_ = nullptr;
    }
    else
    {
        LE_ASSERT(!hasFocus());
    }

    fadeOutComponent(*this, 0, 600, true);
}

void ModuleUI::setUpForEffect(char const *const effectName, char const *const effectDescription)
{
    LE_ASSERT(getName().isEmpty());
    LE_ASSERT(description_.isEmpty());
    setName(effectName);
    description_ = effectDescription;
}

void ModuleUI::moveToSlot(std::uint8_t const slotIndex)
{
    /// \note The assignment is new, and its absence was invisible for as long as
    /// every caller recovered the slot from `getX()` instead -- `slot_` sat at
    /// the zero it was constructed with while `slot()` was only ever read by an
    /// assertion. It is the answer now: the rack is what the user asked for and
    /// the chain is what is playing, so a strip has to know its own place
    /// without asking the chain.
    slot_ = slotIndex;
    std::uint16_t const myHorizontalOffset(horizontalOffset + slotIndex * (width + distance));
    setTopLeftPosition(myHorizontalOffset, verticalOffset);
}

void ModuleUI::paint(juce::Graphics &graphics)
{
    paintModuleStrip(graphics, getLocalBounds().toFloat(), selected());
    graphics.setColour(ColourMap::getColour(ColourMap::Accent));
    graphics.drawHorizontalLine(nameRule, static_cast<float>(ModuleUI::border),
                                Math::convert<float>(getWidth() - ModuleUI::border));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Two lines, not three, a 4 px margin either side, and 0.8 rather than
    /// 0.6 as the least this may condense to. \see issue #76.
    ///
    ///   The three interact, which is why they moved together. drawFittedText
    /// spends its budget on *narrowing* before it spends it on wrapping: at 0.6
    /// every title in the skin fits on one line, so the third line only ever
    /// appeared for a name too long even for that -- "Pitch Follower (pvd)" --
    /// and the rest were condensed to the strip's full width instead. Raising
    /// the floor to 0.8 is what makes a long name break rather than squash,
    /// which is the two rows the issue asks for; the margin is what keeps
    /// whichever way it lands off the rounded border.
    ///
    ////////////////////////////////////////////////////////////////////////////
    graphics.setFont(Theme::singleton().labelFont());
    graphics.drawFittedText(getName(), textMargin, nameRule - 1, width - 2 * textMargin, 42,
                            juce::Justification::centred, 2, 0.75f);
}

/// \note It was every pixel no control happened to cover, which made a drag of
/// the gaps between knobs: a slip off a knob moved the module instead of the
/// value. The eject button's own bottom edge rather than a constant, so a skin
/// that resizes the `X` moves the band with it.
bool ModuleUI::isDragHandle(juce::Point<int> const position) const
{
    if (!getLocalBounds().contains(position))
        return false;
    return (position.getY() < eject_.getBottom()) || (position.getY() >= nameRule);
}

/// \note The open flat hand, not the pointing finger: the finger is the web-link
/// cursor and means "click this".
void ModuleUI::updateCursorFor(juce::Point<int> const position)
{
    setMouseCursor(isDragHandle(position) ? juce::MouseCursor::DraggingHandCursor
                                          : juce::MouseCursor::NormalCursor);
}

/// \note Wherever in the strip it lands, handles included: the drag is the left
/// button's and this is the right one's, so the two never compete for a pixel.
void ModuleUI::mouseDown(juce::MouseEvent const &event)
{
    if (event.mods.isPopupMenu())
        editor().showEffectMenuAt(event.getScreenPosition());
}

void ModuleUI::mouseDrag(juce::MouseEvent const &event)
{
    /// \note Where the mouse went *down*, not where it is: a drag that began on a
    /// handle stays a drag wherever it is carried, and one that began anywhere
    /// else never becomes one.
    if (event.mods.isLeftButtonDown() && isDragHandle(event.getMouseDownPosition()))
        editor().moduleDrag(*this, event);
}

void ModuleUI::mouseUp(juce::MouseEvent const &event) noexcept
{
    if (isDragHandle(event.getMouseDownPosition()))
        editor().moduleDragEnd(*this, event);
}

void ModuleUI::mouseEnter(juce::MouseEvent const &event)
{
    updateCursorFor(event.getPosition());

    if (selectionTracksMouseMovements())
        activate();
}

void ModuleUI::mouseMove(juce::MouseEvent const &event) { updateCursorFor(event.getPosition()); }

void ModuleUI::mouseExit(juce::MouseEvent const &event) noexcept
{
    /// \note In some strange cases (e.g. while a ComboBox drop down menu is
    /// open and the mouse is moved over a module) JUCE will call mouseExit()
    /// without first calling mouseEnter().
    ///                                       (24.05.2012.) (Domagoj Saric)
    if (!editor().selectedModule())
        return;

    if (selectionTracksMouseMovements() &&
        !juce::Rectangle<int>(0, 0, width, height).contains(event.x, event.y))
        deactivate();
}

void ModuleUI::focusGained(FocusChangeType)
{
    activate();
    LE_ASSERT(selected());
}

/// \note Deliberately nothing, for the reason ModuleControlImpl::focusLost()
/// gives: a strip stops being the selected one when another is selected or when
/// it is destroyed, and not because the keyboard went to the preset pane. The
/// three guards this used to need -- focus moving to a subcontrol, to the shared
/// controls, or to a menu one of those opened -- were all ways of saying "that is
/// not really a loss", and there is no loss to qualify any more.
void ModuleUI::focusLost(FocusChangeType) {}

void ModuleUI::focusOfChildComponentChanged(FocusChangeType const changeType)
{
    if (hasFocus())
        ModuleUI::focusGained(changeType);
    else
        ModuleUI::focusLost(changeType);
}

void ModuleUI::activate()
{
    LE_ASSERT(hasFocus() || selectionTracksMouseMovements());
    if (this->selected())
        return;

    /// \note A module change retires the control selection, where a control
    /// change within one module does not. The LFO strip would otherwise name the
    /// module the user has just left while the shared controls and the header
    /// name the one they are on. \see issue #139.
    if (auto *const pControl = editor().activeControl(); pControl && !pControl->pointsInto(*this))
        pControl->deselect();

    // Implementation note:
    //   If the previously active module wasn't actually focused but the shared
    // controls it will not deactivate (and thus repaint) itself in the
    // focusLost() handler so a repaint must be forced here.
    //                                        (14.11.2011.) (Domagoj Saric)
    if (editor().selectedModule())
        editor().selectedModule()->repaint();

    editor().pSelectedModule_ = this;
    editor().moduleActivated();
    repaint();
}

void ModuleUI::deactivate()
{
    LE_ASSERT(selected());
    LE_ASSERT(!hasFocus());

    editor().moduleDeactivated();
    editor().pSelectedModule_ = nullptr;
    repaint();
}

bool ModuleUI::selectionTracksMouseMovements() const
{
    return (preferences().moduleUIMouseOverReaction() ==
            Preferences::WhenParentOrNothingSelected) &&
           ModuleControlBase::noModuleOrModuleControlFocused(editor());
}

namespace
{
auto const bypassIndex = LE::Parameters::IndexOf<Effects::BaseParameters::Parameters,
                                                 Effects::BaseParameters::Bypass>::value;

void setParameterControl(ModuleControlBase &control, float const parameterValue,
                         ModuleUI::ParameterChangeSource const source)
{
    control.setValue(parameterValue);
    if ((source == ModuleUI::AutomationOrPreset) && control.isActive())
    {
        SpectrumWorxEditor::fromChild(control.widget()).updateActiveControlValue();
    }
}
} // anonymous namespace

void ModuleUI::setBaseParameter(std::uint8_t const sharedParameterIndex, float const parameterValue,
                                ParameterChangeSource const source)
{
    if (sharedParameterIndex == bypassIndex)
    {
        LE_ASSUME(source == AutomationOrPreset);
        setBypass(Math::convert<bool>(parameterValue));
    }
    else
    {
        if (selected())
            setParameterControl(sharedControls().controlForParameter(sharedParameterIndex),
                                parameterValue, source);
    }
}

void ModuleUI::setEffectParameter(std::uint8_t const effectParameterIndex,
                                  float const parameterValue, ParameterChangeSource const source)
{
    setParameterControl(effectSpecificParameterControl(effectParameterIndex), parameterValue,
                        source);
}

void ModuleUI::setParameter(std::uint8_t const parameterIndex, float const parameterValue,
                            ParameterChangeSource const source)
{
    if (parameterIndex < Effects::BaseParameters::Parameters::static_size)
        setBaseParameter(parameterIndex, parameterValue, source);
    else
        setEffectParameter(Engine::ModuleParameters::effectSpecificParameterIndex(parameterIndex),
                           parameterValue, source);
}

void ModuleUI::setBypass(bool const bypass) { bypass_.setValue(bypass); }

////////////////////////////////////////////////////////////////////////////////
//
// ModuleUI::BypassButton
// ----------------------
//
////////////////////////////////////////////////////////////////////////////////

ModuleUI::BypassButton::BypassButton(ModuleUI &parent)
    : CapsuleButton(parent, bypassCapsule, bypassWidgetWidth, bypassWidgetHeight,
                    false /*lit when on*/),
      parent_(parent)
{
}

void ModuleUI::BypassButton::mouseDown(juce::MouseEvent const &event)
{
    if (event.mods.isPopupMenu())
        return showParameterMenu(event);
    CapsuleButton::mouseDown(event);
}

juce::String ModuleUI::BypassButton::parameterName() const
{
    return parent_.module().parameterInfo(bypassIndex).name;
}

/// \note The two words a boolean reads as. Not the menu's -- the row below is,
/// `parameterAcceptsText()` being false -- but this is what the question means.
juce::String ModuleUI::BypassButton::parameterValueText() const
{
    return getValue() ? "On" : "Off";
}

/// \note "Bypassed" rather than the parameter's own name, which the header above
/// it already carries: a row beside a checkmark reads as a state.
void ModuleUI::BypassButton::addParameterValueEntries(juce::PopupMenu &menu)
{
    menu.addItem("Bypassed", /*isEnabled*/ true, /*isTicked*/ getToggleState(),
                 [pThis = juce::Component::SafePointer<BypassButton>(this)] {
                     if (pThis)
                         pThis->setToggleState(!pThis->getToggleState(),
                                               juce::sendNotificationSync);
                 });
}

ParameterID ModuleUI::BypassButton::parameterID() const
{
    return parent_.editor().moduleParameterID(parent_.module(), bypassIndex);
}

/// \note Through the widget, which is the path a press takes: the notification
/// is what tells the engine and the host.
void ModuleUI::BypassButton::setParameterToDefault()
{
    setToggleState(parent_.module().parameterInfo(bypassIndex).default_ != 0,
                   juce::sendNotificationSync);
}

void ModuleUI::updateForEngineSetupChanges(Engine::Setup const &engineSetup)
{
    /// \note SharedModuleControls are updated in/by
    /// SpectrumWorxEditor::updateForEngineSetupChanges().
    ///                                       (13.02.2014.) (Domagoj Saric)
    std::uint8_t const numberOfControls(module().numberOfEffectSpecificParameters());
    for (std::uint8_t parameterIndex(0); parameterIndex < numberOfControls; ++parameterIndex)
    {
        effectSpecificParameterControl(parameterIndex).updateForEngineSetupChanges(engineSetup);
    }
}

void ModuleUI::updateLFOParameter(std::uint8_t const parameterIndex,
                                  std::uint8_t const lfoParameterIndex,
                                  Plugins::AutomatedParameterValue const value)
{
    //...mrmlj...value unused - updated from the LFO...
    editor().updateLFO(*this, parameterIndex, lfoParameterIndex, value);
}

void ModuleUI::buttonClicked(juce::Button *LE_RESTRICT const pButton)
{
    if (pButton == &bypass_)
    {
        float const value(Math::convert<float>(bypass_.getValue()));
        // a click, so a whole gesture of its own -- there is no drag to hold one
        editor().updateModuleParameterAndNotifyHost(*this, bypassIndex, value, true);
    }
    else
    {
        LE_ASSERT(pButton == &eject_);
        //...mrmlj...investigate why this doesn't work when placed inside the ModuleUI destructor...
        /// \note Answered, and it is still true: JUCE moves the focus when a
        /// strip is destroyed and delivers the loss to whichever control had it,
        /// which re-enters the editor through a control that is going. What has
        /// changed is that the strip is no longer destroyed inside this call, so
        /// this deactivation is now only half the job -- see
        /// SpectrumWorxEditor::detachFrom(), which does the other half where the
        /// strip actually dies. This one stays because the module is still in the
        /// chain here, which is what ending the host's gesture needs.
        auto *const pActiveControl(editor().activeControl());
        if (pActiveControl && (this == &pActiveControl->moduleUI()))
            editor().moduleControlDectivated(*pActiveControl);
        editor().removeModule(*this);
    }
}

/// \note Was a `polymorphicDowncast` of `getParentComponent()`, with an assertion
/// that there was one. See the note on the constructor: the region is written to
/// before it is parented, so the editor cannot be recovered that way.
SpectrumWorxEditor &ModuleUI::editor() { return editor_; }

SpectrumWorxEditor const &ModuleUI::editor() const { return editor_; }

bool ModuleUI::selected() const { return this == editor().selectedModule(); }

SharedModuleControls &ModuleUI::sharedControls()
{
    LE_ASSERT_MSG(selected(), "Inactive modules do not have an active shared controls UI.");
    return editor().sharedModuleControls();
}

ModuleControlBase &ModuleUI::effectSpecificParameterControl(std::uint8_t const parameterIndex)
{
    std::uint8_t const actualChildIndex(parameterIndex + baseWidgets);
    LE_ASSERT_MSG(actualChildIndex < unsigned(this->getNumChildComponents()),
                  "Parameter index out of range.");
    juce::Component *LE_RESTRICT const pWidget(this->getChildComponent(actualChildIndex));
    LE_ASSUME(pWidget);
    return ModuleControlBase::controlForWidget(*pWidget);
}
ModuleControlBase const &
ModuleUI::effectSpecificParameterControl(std::uint8_t const parameterIndex) const
{
    return const_cast<ModuleUI &>(*this).effectSpecificParameterControl(parameterIndex);
}

ModuleUI::Module &ModuleUI::module()
{
    LE_ASSERT(pModule_.get() != nullptr);
    return *pModule_;
}
ModuleUI::Module const &ModuleUI::module() const { return const_cast<ModuleUI &>(*this).module(); }

namespace Detail
{
ModuleWidgetConstructionState::ModuleWidgetConstructionState(ModuleUI &parent)
    : parent(parent), yOffset(21),
      parameterIndex(Engine::ModuleParameters::numberOfLFOBaseParameters)
{
}

EmptyWidgets::EmptyWidgets(ModuleWidgetConstructionState const &state)
{
    LE_ASSERT_MSG(state.yOffset < static_cast<unsigned int>(state.parent.getHeight()),
                  "You added more parameters/controls to the effect than can fit into its UI");
    (void)state;
}

template <>
ModuleWidgetHolder<ModuleLEDTextButton>::ModuleWidgetHolder(ModuleWidgetConstructionState &state)
    : widget(state.parent, state.parent, ModuleUI::border, state.yOffset, state.parameterIndex++)
{
    state.yOffset += widget.getHeight() + 9;
}

template <>
ModuleWidgetHolder<TriggerButton>::ModuleWidgetHolder(ModuleWidgetConstructionState &state)
    : widget(state.parent, state.parent, 0, state.yOffset + 6, state.parameterIndex++)
{
    state.yOffset += widget.getHeight() + 9;
}

template <>
ModuleWidgetHolder<DiscreteParameter>::ModuleWidgetHolder(ModuleWidgetConstructionState &state)
    : widget(state.parent, state.parent, ModuleUI::border, state.yOffset += 6,
             state.parameterIndex++)
{
    state.yOffset += widget.getHeight() + 6;
}

template <>
ModuleWidgetHolder<ModuleKnob>::ModuleWidgetHolder(ModuleWidgetConstructionState &state)
    : widget(state.parent, state.parent, ModuleUI::border, state.yOffset, state.parameterIndex++)
{
    // getHeight() is still only the margin here: setupForParameter() adds the
    // face, and it has not run yet.
    state.yOffset += widget.getHeight() + ModuleKnob::diameter;
}

} // namespace Detail

} // namespace LE::SW::GUI
