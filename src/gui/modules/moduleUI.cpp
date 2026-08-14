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

#include "le/parameters/lfo.hpp"
#include "le/parameters/printer.hpp"
#include "le/parameters/uiElements.hpp"
#include "le/spectrumworx/engine/setup.hpp"
#include "le/utility/platformSpecifics.hpp"

#include "le/utility/assert.hpp"
#include "le/utility/polymorphicDowncast.hpp"

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
    setSize(resourceArtwork<ModuleComboOn>().getWidth(), getHeight() + 2);
}

void ModuleLEDTextButton::clicked() { moduleParameterChanged(); }

void ModuleLEDTextButton::mouseDown(juce::MouseEvent const &event)
{
    if (!hasDirectFocus())
    {
        grabKeyboardFocus();
    }
    else if (!isLFOEnabled())
    {
        LEDTextButton::mouseDown(event);
    }
}

void ModuleLEDTextButton::paintButton(juce::Graphics &g, bool const isMouseOverButton,
                                      bool const isButtonDown)
{
    if (hasDirectFocus())
        paintImage(g, resourceArtwork<ModuleComboOn>(), 0, -1);
    g.setOrigin(3, 1);
    LEDTextButton::paintButton(g, isMouseOverButton, isButtonDown);
}

TriggerButton::TriggerButton(juce::Component &parent, unsigned int const x, unsigned int const y)
    : BitmapButton(parent, resourceArtwork<TriggerBtnOn>(), resourceArtwork<TriggerBtnOff>(),
                   juce::Colours::transparentWhite, false)
{
    setName(control().name());

    setBounds(x, y, ModuleUI::width, getHeight() + 13);

    setTriggeredOnMouseDown(true);

    addToParentAndShow(parent, *this);
}

void TriggerButton::setValue(param_type const newValue)
{
    setState(newValue ? buttonDown : buttonNormal);
}

void TriggerButton::mouseDown(juce::MouseEvent const &e)
{
    if (!hasDirectFocus())
    {
        grabKeyboardFocus();
    }
    else if (!isLFOEnabled())
    {
        BitmapButton::mouseDown(e);
        moduleParameterChanged();
    }
}

void TriggerButton::mouseUp(juce::MouseEvent const &e) noexcept
{
    if (!isLFOEnabled())
    {
        BitmapButton::mouseUp(e);
        moduleParameterChanged();
    }
}

void TriggerButton::paintButton(juce::Graphics &graphics, bool const isMouseOverButton,
                                bool const isButtonDown)
{
    unsigned int const imageWidth(51);
    unsigned int const imageHeight(51);
    LE_ASSERT(currentArtwork().getWidth() == imageWidth);
    LE_ASSERT(currentArtwork().getHeight() == imageHeight);
    Detail::paintTextButton(*this, graphics, 0, imageHeight + 2, (ModuleUI::width - imageWidth) / 2,
                            0, isMouseOverButton, isButtonDown);
    if (this->hasDirectFocus())
    {
        paintImage(graphics, resourceArtwork<ModuleKnobSelected>(),
                   (ModuleUI::width - imageWidth) / 2 - 1, -1);
    }
}

ModuleKnob::ModuleKnob(juce::Component &parent, unsigned int const x, unsigned int const y)
    : Knob(parent, x, y, marginForGlow * 2,
           std::max<unsigned int>(marginForGlow * 2, spaceForText)),
      pImageStrip_(nullptr)
{
    //...mrmlj...LE_ASSERT( imageStrip.getHeight() / numberOfKnobSubbitmaps == 50 );
    //...mrmlj...LE_ASSERT( imageStrip.getWidth ()                          == 50 );

    setScrollWheelEnabled(false);
}

void ModuleKnob::setupForParameter(Artwork const &imageStrip, Quantization const quantizationType,
                                   std::uint8_t const quantizationStep)
{
    auto const &info(control().info());
    Knob::setupForParameter(info.name, imageStrip, info.default_);
    //LE_ASSERT( !isLFOEnabled() ); //...mrmlj...when turning the GUI on or off...
    setDoubleClickReturnValue(!isLFOEnabled(), info.default_);
    quantization_ = quantizationType;
    pImageStrip_ = &imageStrip;
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

/// \note `mouseDown` was `Knob::mouseDown( event ); setEnabled( !isLFOEnabled() )`
/// and `mouseUp` put the enabled flag back, with "in order for the base class to
/// handle the mouseDown() event, the control has to be disabled afterwards"
/// (09.12.2011.) over it. What that bought was one thing --
/// `juce::Slider::mouseDrag` is gated on `isEnabled()` -- and what it cost was
/// the focus: `Component::setEnabled( false )` hands the keyboard focus to the
/// parent, so pressing a knob whose LFO was on deactivated the control and took
/// its own LFO display off the screen, on the way to tripping
/// `ModuleControlImpl::focusLost`'s assertion.
///
///   Blocking the one gesture instead is also what the other two already do:
/// `lfoStateChanged()` keys `setScrollWheelEnabled()` and
/// `setDoubleClickReturnValue()` on the same question. This is the drag.
///                                           (03.08.2026.) (SW port)
void ModuleKnob::mouseDrag(juce::MouseEvent const &event) noexcept
{
    if (isLFOEnabled())
        return;
    Knob::mouseDrag(event);
}

void ModuleKnob::paint(juce::Graphics &graphics)
{
    unsigned int const imageWidth(pImageStrip_->getWidth());
    unsigned int const imageHeight(imageWidth);

    if (!control().isLFOEnabled() || shouldUpdateLFOControl(control()))
        Knob::paintFilmStrip(*pImageStrip_, marginForGlow, marginForGlow, graphics);
    else
        paintImage(graphics, resourceArtwork<ModuleKnobLFOed>(), marginForGlow, marginForGlow);
    if (this->hasDirectFocus())
    {
        Artwork const &selection(imageWidth < 51 ? resourceArtwork<SmallModuleKnobSelected>()
                                                 : resourceArtwork<ModuleKnobSelected>());
        LE_ASSERT(selection.getWidth() == selection.getHeight());
        LE_ASSERT(unsigned(selection.getWidth()) == imageWidth + 2);
        unsigned int const selectionWidth(imageWidth + 2);
        unsigned int const xy(marginForGlow - (selectionWidth - imageWidth) / 2);
        paintImage(graphics, selection, xy, xy);
    }

    graphics.setColour(juce::Colours::lightgrey);
    {
        juce::Font font(Theme::singleton().whiteFont());
        font.setHeight(10);
        graphics.setFont(font);
    }
    graphics.drawFittedText(getName(), 0, imageHeight + marginForGlow + (marginForGlow / 2),
                            getWidth(), 12, juce::Justification::horizontallyCentred, 2, 0.6f);
}

void ModuleKnob::valueChanged() noexcept
{
    LE_ASSERT(isMouseOverOrDragging());
    moduleParameterChanged();
}

void ModuleKnob::lfoStateChanged()
{
    /// \note JUCE 8 split the out-parameter off into isDoubleClickReturnEnabled();
    /// getDoubleClickReturnValue() now just returns the value, which is all this
    /// wanted -- the flag it had to pass a variable for was discarded.
    ///                                       (28.07.2026.) (SW port)
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
    ///                                       (29.07.2026.) (SW port)
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

void ModuleKnob::moduleControlActivated() { syncMouseWheelAndLFOState(); }
void ModuleKnob::moduleControlDeactivated() { setScrollWheelEnabled(false); }
void ModuleKnob::syncMouseWheelAndLFOState() { setScrollWheelEnabled(!isLFOEnabled()); }

#ifdef __GNUC__ //...mrmlj... GCC 4.6, Clang 2.8-3.2
unsigned int const ModuleKnob::spaceForText /* = 18*/;
#endif // __GNUC__

DiscreteParameter::DiscreteParameter(juce::Component &parent, unsigned int const x,
                                     unsigned int const y)
    : ComboBox(parent, resourceArtwork<ModuleCombo>(), resourceArtwork<ModuleComboOn>())
{
    setName(control().name());
    DiscreteParameter::setTopLeftPosition(x, y);
    LE_ASSERT(control().info().default_ == 0);
}

void DiscreteParameter::mouseDown(juce::MouseEvent const &)
{
    if (!hasDirectFocus())
    {
        grabKeyboardFocus();
    }
    else if (!isLFOEnabled())
    {
        /// \note The menu is asynchronous now, so the notification happens in
        /// the callback rather than on the next line. The SafePointer matters:
        /// a module can be ejected while its menu is down.
        ///                                   (28.07.2026.) (SW port)
        ComboBox::showMenu([self = juce::Component::SafePointer<DiscreteParameter>(this)](
                               bool const valueChanged) {
            if (self && valueChanged)
                self->moduleParameterChanged();
        });
    }
}

void DiscreteParameter::focusChanged() { repaint(); }

#pragma warning(push)
#pragma warning(disable : 4355) // 'this' used in base member initializer list.

ModuleUI::ModuleUI(SpectrumWorxEditor &editor, LE::Utility::IntrusivePtr<SW::Module> pModule,
                   std::uint8_t const slotIndex)
    : editor_(editor), pModule_(std::move(pModule)),
      bypass_(*this, resourceArtwork<ModuleMuted>(), resourceArtwork<ModuleOn>()),
      eject_(*this, resourceArtwork<Eject>(), resourceArtwork<Eject>(),
             juce::Colours::darkgrey.withAlpha(0.4f))
{
    LE_ASSERT(isThisTheGUIThread() ||
              juce::MessageManager::getInstance()->currentThreadHasLockedMessageManager());
    LE_ASSERT(pModule_);

    LE_ASSERT(resourceArtwork<ModuleBgSelected>().getWidth() ==
              resourceArtwork<ModuleBg>().getWidth());
    LE_ASSERT(resourceArtwork<ModuleBgSelected>().getHeight() ==
              resourceArtwork<ModuleBg>().getHeight());
    LE_ASSERT(resourceArtwork<ModuleBgSelected>().getWidth() == width);
    LE_ASSERT(resourceArtwork<ModuleBgSelected>().getHeight() == height);

    setSize(width, height);

    bypass_.setTopLeftPosition((ModuleUI::width / 2) - (bypass_.getWidth() / 2),
                               ModuleUI::height - 26 - resourceArtwork<ModuleOn>().getHeight());

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

    ////////////////////////////////////////////////////////////////////////////
    // The effect's own controls, and everything Module::createGUI() used to do
    // after building them.
    ////////////////////////////////////////////////////////////////////////////

    /// \note Parented before the effect's controls are built, and invisible until
    /// the caller shows it. Several of them walk `getParentComponent()` up to the
    /// editor as they are constructed -- `SpectrumWorxEditor::fromChild()` -- and
    /// a strip that is not in the hierarchy yet has nothing to walk. The old
    /// createGUI() did the same thing with an `editor.addChildComponent()` under
    /// `#ifndef NDEBUG`, for the same reason and only in a checked build.
    ///                                       (02.08.2026.) (SW port)
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
    ///                                       (02.08.2026.) (SW port)
    slot_ = slotIndex;
    std::uint16_t const myHorizontalOffset(horizontalOffset + slotIndex * (width + distance));
    setTopLeftPosition(myHorizontalOffset, verticalOffset);
}

void ModuleUI::paint(juce::Graphics &graphics)
{
    bool const isActive(selected());
    graphics.setOpacity(isActive ? 1.0f : 0.5f);
    paintImage(graphics,
               isActive ? resourceArtwork<ModuleBgSelected>() : resourceArtwork<ModuleBg>());
    graphics.setColour(Theme::singleton().blueColour());
    graphics.drawHorizontalLine(nameRule, static_cast<float>(ModuleUI::border),
                                Math::convert<float>(getWidth() - ModuleUI::border));

    graphics.setFont(Theme::singleton().whiteFont());
    graphics.drawFittedText(getName(), 3, nameRule, width - 6, 28, juce::Justification::centred, 3,
                            0.6f);
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

void ModuleUI::focusLost(FocusChangeType)
{
    // Implementation note:
    //   If only transferring focus to a subcontrol or to the shared controls do
    // not deactivate.
    //                                        (14.11.2011.) (Domagoj Saric)
    if (hasFocus() || editor().sharedModuleControlsActiveAndFocused())
        return;

    //...mrmlj...rethink this focus changing logic and assumptions
    //LE_ASSERT( selected() );
    if (selected())
        deactivate();
}

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
    return (Theme::settings().moduleUIMouseOverReaction == Theme::WhenParentOrNothingSelected) &&
           ModuleControlBase::noModuleOrModuleControlFocused(editor());
}

namespace
{
auto const bypassIndex = LE::Parameters::IndexOf<Effects::BaseParameters::Parameters,
                                                 Effects::BaseParameters::Bypass>::value;

void setParameterControl(ModuleControlBase &control, float const parameterValue,
                         ModuleUI::ParameterChangeSource const source)
{
    if ((source != ModuleUI::LFOValue) || shouldUpdateLFOControl(control))
    {
        control.setValue(parameterValue);
    }
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
        editor().updateModuleParameterAndNotifyHost(*this, bypassIndex, value);
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
        ///                                   (02.08.2026.) (SW port)
        auto *const pActiveControl(editor().activeControl());
        if (pActiveControl && (this == &pActiveControl->moduleUI()))
            editor().moduleControlDectivated(*pActiveControl);
        editor().removeModule(*this);
    }
}

/// \note Was a `polymorphicDowncast` of `getParentComponent()`, with an assertion
/// that there was one. See the note on the constructor: the region is written to
/// before it is parented, so the editor cannot be recovered that way.
///                                           (02.08.2026.) (SW port)
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
    : parent(parent), yOffset(14),
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
    state.yOffset += widget.getHeight() + 6;
}

template <>
ModuleWidgetHolder<TriggerButton>::ModuleWidgetHolder(ModuleWidgetConstructionState &state)
    : widget(state.parent, state.parent, 0, state.yOffset + 4, state.parameterIndex++)
{
    state.yOffset += widget.getHeight() + 6;
}

template <>
ModuleWidgetHolder<DiscreteParameter>::ModuleWidgetHolder(ModuleWidgetConstructionState &state)
    : widget(state.parent, state.parent, ModuleUI::border, state.yOffset += 4,
             state.parameterIndex++)
{
    state.yOffset += widget.getHeight() + 4;
}

template <>
ModuleWidgetHolder<ModuleKnob>::ModuleWidgetHolder(ModuleWidgetConstructionState &state)
    : widget(state.parent, state.parent, ModuleUI::border, state.yOffset, state.parameterIndex++)
{
    state.yOffset += widget.getHeight();
    //...mrmlj...
    LE_ASSERT(resourceArtwork<ModuleKnobStrip>().getWidth() == 51);
    state.yOffset += 51;
}

} // namespace Detail

} // namespace LE::SW::GUI
