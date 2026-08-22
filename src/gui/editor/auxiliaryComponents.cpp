////////////////////////////////////////////////////////////////////////////////
///
/// auxiliaryComponents.cpp
/// -----------------------
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "auxiliaryComponents.hpp"

#include "core/modules/moduleDSPAndGUI.hpp"
#include "spectrumWorxEditor.hpp"

#include "le/math/math.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/parameters/printer.hpp"
#include "le/spectrumworx/effects/baseParameters.hpp"
#include "le/spectrumworx/engine/setup.hpp"
#include "le/utility/parentFromMember.hpp"

namespace LE
{

namespace Parameters
{
class LFOImpl;
}
namespace SW::GUI
{

using Effects::BaseParameters::Bypass;
using Effects::BaseParameters::Gain;
using Effects::BaseParameters::Parameters;
using Effects::BaseParameters::StartFrequency;
using Effects::BaseParameters::StopFrequency;
using Effects::BaseParameters::Wet;

using LE::Parameters::IndexOf;
using LE::Parameters::LFOImpl;

namespace Constants
{
static std::uint8_t const startFrequencyIndex =
    IndexOf<Parameters, StartFrequency>::value - 1 /*Bypass*/;
static std::uint8_t const stopFrequencyIndex =
    IndexOf<Parameters, StopFrequency>::value - 1 /*Bypass*/;
static std::uint8_t const invalidIndex = static_cast<std::uint8_t>(-1);
static std::uint8_t const startFrequencyThumbIndex = 1;
static std::uint8_t const stopFrequencyThumbIndex = 2;
/// \note "No thumb", and -1 rather than invalidIndex: these are thumb indices
/// rather than parameter indices, JUCE spells the absent one -1, and
/// verifyThumbAndParameterIndicies() below switches on -1.
static int const noThumb = -1;
} // namespace Constants

#pragma warning(push)
#pragma warning(disable : 4355) // 'this' used in base member initializer list.

SharedModuleControls::SharedModuleControls()
    : /// \note In order to enable a simple parameter-to-control (index) mapping,
      /// the control widgets must be added to the parent SharedModuleControls
      /// component in the correct order.
      ///                                       (11.02.2014.) (Domagoj Saric)
      gain_(*this, *editor().selectedModule(), 8, 6,
            IndexOf<Parameters, Gain>::value -
                1), //...mrmlj...ModuleControlBase::moduleParameterIndex() excludes bypass...
      wet_(*this, *editor().selectedModule(), 113, 6, IndexOf<Parameters, Wet>::value - 1)
{
    gain_.setupForParameter(ModuleKnob::Bipolar, ModuleKnob::smallDiameter, ModuleKnob::Fixed,
                            Gain::discreteValueDistance);
    wet_.setupForParameter(ModuleKnob::Unipolar, ModuleKnob::smallDiameter, ModuleKnob::Fixed,
                           Wet ::discreteValueDistance);

    addToParentAndShow(*this, frequencyRange_);

    setBounds(116, 120, 174, 119);
    addToParentAndShow(editor().mainArea(), *this);

    updateForEngineSetupChanges(editor().engineSetup());
}

#pragma warning(pop)

void SharedModuleControls::updateForEngineSetupChanges(Engine::Setup const &setup)
{
    frequencyRange_.FrequencyRange::updateForEngineSetupChanges(setup);
    /// \note Gain and Wet are assumed to be non quantized.
    ///                                       (13.02.2014.) (Domagoj Saric)
}

void SharedModuleControls::updateForActiveModule()
{
    LE_ASSERT(editor().selectedModule());

    ModuleUI &moduleUI(*editor().selectedModule());

    gain_.reassignTo(moduleUI);
    wet_.reassignTo(moduleUI);
    frequencyRange_.reassignTo(moduleUI);

    auto const &module(moduleUI.module());

    gain_.ModuleKnob::setValue(module.getBaseParameter(IndexOf<Parameters, Gain>::value));
    wet_.ModuleKnob::setValue(module.getBaseParameter(IndexOf<Parameters, Wet>::value));

    frequencyRange_.setStartValue(
        module.getBaseParameter(IndexOf<Parameters, StartFrequency>::value));
    frequencyRange_.setStopValue(
        module.getBaseParameter(IndexOf<Parameters, StopFrequency>::value));
}

ModuleControlBase &SharedModuleControls::controlForParameter(std::uint8_t const parameterIndex)
{
    using LE::Parameters::IndexOf;
    using namespace Effects::BaseParameters;
    typedef Effects::BaseParameters::Parameters BaseParams;
    switch (parameterIndex)
    {
    case IndexOf<BaseParams, Gain>::value:
        return gain_;
    case IndexOf<BaseParams, Wet>::value:
        return wet_;
    case IndexOf<BaseParams, StartFrequency>::value:
        return frequencyRange_.startControl();
    case IndexOf<BaseParams, StopFrequency>::value:
        return frequencyRange_.stopControl();

    case IndexOf<BaseParams, Bypass>::value:
        LE_UNREACHABLE_CODE();
        LE_DEFAULT_CASE_UNREACHABLE();
    }
}

/// \note Checked rather than asserted. Losing focus is something JUCE does to
/// these controls on the way *out* -- a strip being dropped moves the focus, and
/// the editor's record of what is selected is cleared before that -- so "there
/// is a selected module" is exactly what does not hold here. In a shipped build
/// the assertion was not there and the null was dereferenced.
void SharedModuleControls::focusLost(FocusChangeType)
{
    /// \note Not while a menu is up. These two knobs each have a right button
    /// menu with a type-in field in it, the field takes the keyboard, and
    /// deactivating the module from here destroys *these controls* -- so the
    /// knob the open menu belongs to would be freed under it.
    /// \see the note on ModuleControlImpl::focusLost().
    if (isCurrentlyBlockedByAnotherModalComponent())
        return;

    auto *const pModuleUI(editor().selectedModule());
    if (pModuleUI && !pModuleUI->hasFocus())
        pModuleUI->deactivate();
}

void SharedModuleControls::focusOfChildComponentChanged(FocusChangeType const type)
{
    if (!hasFocus())
        SharedModuleControls::focusLost(type);
}

SpectrumWorxEditor &SharedModuleControls::editor()
{
    return Utility::ParentFromOptionalMember<SpectrumWorxEditor, SharedModuleControls,
                                             &SpectrumWorxEditor::sharedModuleControls_, false>()(
        *this);
}

SpectrumWorxEditor const &SharedModuleControls::editor() const
{
    return const_cast<SharedModuleControls &>(*this).editor();
}

#pragma warning(push)
#pragma warning(disable : 4355) // 'this' used in base member initializer list.

/// \note `parent().editor()` and not `editor()`: this control's own editor() is
/// `ModuleControlBase`'s, which goes through `pModuleUI_` -- the member this
/// initialiser list is setting. `parent()` is pointer arithmetic from a member
/// back to its owner and needs nothing constructed.
SharedModuleControls::FrequencyRange::FrequencyRange()
    : ModuleControlBase(Constants::invalidIndex, *parent().editor().selectedModule()),
      parameterIndexForInternalWriteAccess_(Constants::invalidIndex),
      selectedThumb_(Constants::noThumb)
{
    /// \note The removeValueListeners() call that was here went with the fork's
    /// Slider::valueListener(). See the note in the Knob constructor.

    setWantsKeyboardFocus(true);
    setSliderStyle(juce::Slider::TwoValueHorizontal);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 15, 18);
    setBounds(3, 54, 162, 51);

    // Implementation note:
    //   See the note for the LFODisplay range control in
    // SpectrumWorxEditor.cpp.
    //                                        (07.10.2011.) (Domagoj Saric)
    setRange(StartFrequency::minimum(), StopFrequency::maximum(), 0);
    setStartValue(StartFrequency::minimum());
    setStopValue(StopFrequency ::maximum());
    //...mrmlj...setSkewFactor( ... );
}

#pragma warning(pop)

void SharedModuleControls::FrequencyRange::setValue(float const value)
{
    LE_ASSERT(canUseWriteAccessIndex());
    switch (parameterIndexForInternalWriteAccess_)
    {
    case Constants::startFrequencyIndex:
        setStartValue(value);
        break;
    case Constants::stopFrequencyIndex:
        setStopValue(value);
        break;
        LE_DEFAULT_CASE_UNREACHABLE();
    }
}

float SharedModuleControls::FrequencyRange::getValue() const
{
    //...mrmlj...see the comment for parameterIndexForInternalWriteAccess_...
    LE_ASSERT(isThisTheGUIThread());
    switch (moduleParameterIndex())
    {
    case Constants::startFrequencyIndex:
        return getStartValue();
    case Constants::stopFrequencyIndex:
        return getStopValue();
        LE_DEFAULT_CASE_UNREACHABLE();
    }
}

void SharedModuleControls::FrequencyRange::updateForEngineSetupChanges(
    Engine::Setup const &engineSetup)
{
    typedef FrequencyRange::param_type value_type;
    value_type const nyquist(engineSetup.sampleRate<value_type>() / 2);
    value_type const intervalForFrequencyKnobs(engineSetup.frequencyRangePerBin<value_type>() /
                                               nyquist);

    using namespace Effects::BaseParameters;
    this->setRange(StartFrequency::minimum(), StartFrequency::maximum(), intervalForFrequencyKnobs);
    this->setSkewFactorFromMidPoint(4000.0f / nyquist);
}

ModuleControlBase &SharedModuleControls::FrequencyRange::startControl()
{
    parameterIndexForInternalWriteAccess_ = Constants::startFrequencyIndex;
    return *this;
}

ModuleControlBase &SharedModuleControls::FrequencyRange::stopControl()
{
    parameterIndexForInternalWriteAccess_ = Constants::stopFrequencyIndex;
    return *this;
}

void SharedModuleControls::FrequencyRange::focusGained(juce::Component::FocusChangeType)
{
    reportActiveControl();
}
void SharedModuleControls::FrequencyRange::focusLost(juce::Component::FocusChangeType)
{
    reportInactiveControl();
}

void SharedModuleControls::FrequencyRange::mouseEnter(juce::MouseEvent const &event) noexcept
{
    updateSliderSelection(event);
    juce::Slider::mouseEnter(event);
}

void SharedModuleControls::FrequencyRange::mouseExit(juce::MouseEvent const &event) noexcept
{
    reportInactiveControl();
    juce::Slider::mouseExit(event);
}

void SharedModuleControls::FrequencyRange::mouseDown(juce::MouseEvent const &event) noexcept
{
    //...mrmlj...LE_ASSERT( hasFocus() == this->isActive() );
    updateSliderSelection(event);
    verifyThumbAndParameterIndicies();
    juce::Slider::mouseDown(event);
    verifyThumbAndParameterIndicies();
}

/// \note Only the drag needs blocking, and disabling a focused control instead
/// would make JUCE deactivate it. \see ModuleKnob::mouseDown().
void SharedModuleControls::FrequencyRange::mouseDrag(juce::MouseEvent const &event) noexcept
{
    if ((selectedThumb_ != Constants::noThumb) && lfo().enabled())
        return;
    juce::Slider::mouseDrag(event);
}

void SharedModuleControls::FrequencyRange::mouseMove(juce::MouseEvent const &event) noexcept
{
    updateSliderSelection(event);
    juce::Slider::mouseMove(event);
}

void SharedModuleControls::FrequencyRange::paint(juce::Graphics &g)
{
    juce::Slider::paint(g);
    g.setFont(DrawableText::defaultFont());
    g.setColour(ColourMap::getColour(ColourMap::Text));
    g.drawSingleLineText("Frequency Range", 17, 48);
}

void SharedModuleControls::FrequencyRange::valueChanged() noexcept
{
    LE_ASSERT(editor().selectedModule());
    LE_ASSERT(&this->module() == &editor().selectedModule()->module());
    /// \note juce::Slider might have updated the active thumb within its
    /// juce::Slider::mouseDown() handler before calling valueChanged().
    ///                                       (12.02.2014.) (Domagoj Saric)
    if (this->moduleParameterIndex() != thumbToParameterIndex())
        reportActiveControl();
    verifyThumbAndParameterIndicies();

    moduleParameterChanged();
}

/// \note The control's own module, which is what this always returned. It read
/// it out of `editor().selectedModule()` first and then asserted the two were the
/// same -- so a shipped build dereferenced a pointer that is null whenever
/// nothing is selected, to obtain something it already had. `pointsInto()`'s note
/// says when that is: the editor forgets what is selected before the strip goes.
LFOImpl &SharedModuleControls::FrequencyRange::lfo()
{
    auto &controlModule(this->module());
    LE_ASSERT(editor().selectedModule() == nullptr ||
              (&editor().selectedModule()->module() == &controlModule));
    return controlModule.baseLFO(activeParameterIndex());
}

void SharedModuleControls::FrequencyRange::reportActiveControl()
{
    LE_ASSERT(&parent().editor() == &this->editor());

    std::uint8_t const currentParameterIndex(moduleParameterIndex());
    std::uint8_t newParameterIndex;
    char const *pName;
    switch (selectedThumb_)
    {
        using namespace Constants;
        using LE::Parameters::Name;
    case startFrequencyThumbIndex:
        newParameterIndex = startFrequencyIndex;
        pName = Name<StartFrequency>::string_;
        break;
    case stopFrequencyThumbIndex:
        newParameterIndex = stopFrequencyIndex;
        pName = Name<StopFrequency>::string_;
        break;
    default:
        return;
    }

    if ((editor().activeControl() == this) && (currentParameterIndex != newParameterIndex))
    {
        /// \note DIRTY HACK:
        /// If FrequencyRange is already the active control widget
        /// ModuleControlBase::reportActiveControl() will skip activation even
        /// if FrequencyRange has internally changed the parameter it maps to.
        /// As a quick workaround we clear the active control in order to force
        /// reactivation (and updating of the editor window).
        ///                                   (12.02.2014.) (Domagoj Saric)
        clearActiveControl();
    }

    reassignTo(newParameterIndex);
    setName(pName);
    if (!ModuleControlBase::reportActiveControl(StartFrequency::minimum(),
                                                StartFrequency::maximum(), 0))
        reassignTo(currentParameterIndex);
    //...mrmlj...resetting the name should not be necessary
}

void SharedModuleControls::FrequencyRange::reportInactiveControl()
{
    if (ModuleControlBase::reportInactiveControl())
    {
        using Constants::invalidIndex;
        selectedThumb_ = Constants::noThumb;
        reassignTo(invalidIndex);
        parameterIndexForInternalWriteAccess_ = invalidIndex;
        repaint();
    }
}

std::uint8_t SharedModuleControls::FrequencyRange::activeParameterIndex() const
{
    verifyThumbAndParameterIndicies();
    std::uint8_t const indexFromThumb(thumbToParameterIndex());
    std::uint8_t const indexFromControl(this->moduleParameterIndex());
    LE_ASSUME(indexFromThumb == indexFromControl);
    return indexFromControl;
}

void SharedModuleControls::FrequencyRange::updateSliderSelection(juce::MouseEvent const &event)
{
    if (editor().activeControl() && !this->isActive())
    {
        selectedThumb_ = Constants::noThumb;
        return;
    }

    int const mousePos(event.x);

    unsigned int const startPosDistance(
        Math::abs(Math::convert<int>(getPositionOfValue(getMinValue())) - mousePos));
    unsigned int const stopPosDistance(
        Math::abs(Math::convert<int>(getPositionOfValue(getMaxValue())) - mousePos));

    using namespace Constants;
    int const newSliderSelection((startPosDistance < stopPosDistance) ? startFrequencyThumbIndex
                                                                      : stopFrequencyThumbIndex);
    bool const activeControlChanged(newSliderSelection != selectedThumb_);

    if (activeControlChanged)
    {
        selectedThumb_ = newSliderSelection;
        reportActiveControl();
        repaint();
    }
}

std::uint8_t SharedModuleControls::FrequencyRange::thumbToParameterIndex() const
{
    return static_cast<std::uint8_t>(selectedThumb_ - 1 + Constants::startFrequencyIndex);
}

void SharedModuleControls::FrequencyRange::verifyThumbAndParameterIndicies() const
{
#ifndef NDEBUG
    std::uint8_t expectedParameterIndex;
    switch (selectedThumb_)
    {
    case +1:
        expectedParameterIndex = Constants::startFrequencyIndex;
        break;
    case +2:
        expectedParameterIndex = Constants::stopFrequencyIndex;
        break;
    case -1:
        expectedParameterIndex = Constants::invalidIndex;
        break;
        LE_DEFAULT_CASE_UNREACHABLE();
    }
    std::uint8_t const actualParameterIndex(this->moduleParameterIndex());
    LE_ASSERT_MSG(expectedParameterIndex == actualParameterIndex,
                  "Thumb and parameter indicies out of sync.");
#endif // NDEBUG
}

/// \note `!isThisTheGUIThread() ||` stood in front of this, and it is what the
/// old model looked like written down as a condition: "somebody other than the
/// interface is writing this widget, so the write is one of ours". That somebody
/// was the audio thread, driving a two-thumbed range slider from inside
/// `preProcess()`. Nothing but the interface writes a widget now, so the term is
/// dead -- and it was never a check, it was a description.
///
///   What is left is the honest half: a preset load pushes values into the
/// controls, and that is when a write without a mouse behind it is expected.
bool SharedModuleControls::FrequencyRange::canUseWriteAccessIndex() const
{
    LE_ASSERT(isThisTheGUIThread());
    //...mrmlj...see the comment for parameterIndexForInternalWriteAccess_...
    return editor().presetLoadingInProgress() &&
           (parameterIndexForInternalWriteAccess_ != Constants::invalidIndex);
}

SharedModuleControls &SharedModuleControls::FrequencyRange::parent()
{
    return Utility::ParentFromMember<SharedModuleControls, FrequencyRange,
                                     &SharedModuleControls::frequencyRange_>()(*this);
}

} // namespace SW::GUI

} // namespace LE
