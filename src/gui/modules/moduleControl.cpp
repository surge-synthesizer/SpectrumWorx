////////////////////////////////////////////////////////////////////////////////
///
/// module.cpp
/// ----------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "moduleControl.hpp"

#include "core/modules/automatedModuleImpl.inl" //...mrmlj...

#include "gui/editor/spectrumWorxEditor.hpp"
#include "gui/modules/moduleUI.hpp"
#include "gui/preferences.hpp"

#include "le/parameters/lfo.hpp"
#include "le/parameters/parser.hpp"
#include "le/parameters/printer.hpp"

#include "core/modules/moduleDSPAndGUI.hpp"

#include "le/utility/polymorphicDowncast.hpp"

#include <juce_gui_basics/juce_gui_basics.h>
#include "le/utility/span.hpp"

namespace LE::SW::GUI
{

// Implementation note:
//   Because no two windows can have focus at the same time and because JUCE
// does not support multithreaded GUI code, it is safe to use a static here even
// if there are multiple effect editor instances open.
//                                            (07.07.2011.) (Domagoj Saric)
/// \todo Verify this on the Mac.
///                                           (07.07.2011.) (Domagoj Saric)
/// \note Verified, and the answer is no -- but not for the reason the note was
/// worried about. Focus really is exclusive; *lifetime* is not. Two editors
/// shared one pointer, so closing one left the other holding a control that had
/// been freed, and `reportInactiveControl()` would then call
/// `editor().moduleControlDectivated()` on it. It is
/// SpectrumWorxEditor::pActiveControl_ now, one per editor.

bool ModuleControlBase::isActive() const { return this == editor().activeControl(); }

bool ModuleControlBase::tryActivateControl() const
{
    if (isActive())
        return false;

    if (juce::Component::getNumCurrentlyModalComponents() != 0)
        return false;

    Preferences::ModuleUIMouseOverReaction const desiredReaction(
        preferences().moduleUIMouseOverReaction());
    juce::Component const *const pFocusedComponent(juce::Component::getCurrentlyFocusedComponent());

    bool const nothingFocused(noModuleOrModuleControlFocused());
    bool const parentFocused(pFocusedComponent == &moduleUI());
    bool const parentOrNothingFocused(parentFocused |
                                      nothingFocused); // Intentional bitwise or as an optimization.
    bool const controlFocused(pFocusedComponent == &widget());
    if ((controlFocused) ||
        (desiredReaction == Preferences::WhenParentOrNothingSelected && parentOrNothingFocused) ||
        (desiredReaction == Preferences::WhenParentModuleSelected && parentFocused))
        return true;
    else
        return false;
}

bool ModuleControlBase::reportActiveControl(double const minimum, double const maximum,
                                            double const interval)
{
    if (tryActivateControl())
    {
        /// \note The control is marked as activated only after the editor
        /// has been informed (and the LFO GUI created) to avoid race condition
        /// crashes when a user activates a control that is being automated (and
        /// SpectrumWorxEditor::updateActiveControlValue() gets called as a
        /// response to setParameter() before the LFO gets created).
        ///                                   (25.04.2013.) (Domagoj Saric)
        editor().moduleControlActivated(*this, minimum, maximum, interval);
        editor().pActiveControl_ = this;
        return true;
    }
    return false;
}

bool ModuleControlBase::reportInactiveControl() const
{
    if (isActive() && !Detail::hasDirectFocus(widget()))
    {
        editor().moduleControlDectivated(*this);
        editor().pActiveControl_ = nullptr;
        return true;
    }
    else
        return false;
}

////////////////////////////////////////////////////////////////////////////////
//
// ModuleControlBase -- the right button's menu
// --------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   These were ModuleKnob's, and were the reason issue #93 existed: a knob is
/// not the only widget standing for an automatable parameter, and the three
/// that are not knobs -- an LED, a trigger and a combo box -- had no menu at
/// all. Tune Worx is entirely made of the first and the last of those, so the
/// one effect where a user most wants a host's own entries was the one effect
/// that offered none.
///
////////////////////////////////////////////////////////////////////////////////

ParameterID ModuleControlBase::parameterMenuID() const { return editor().moduleControlID(*this); }

/// \note `setValue()` rather than a notifying form, and `publishValue()` rather
/// than `moduleParameterChanged()`: the latter asserts the mouse is on the
/// widget, and it is on the menu. \see publishValue().
bool ModuleControlBase::setValueFromText(juce::String const &text)
{
    auto const value(parseValueString(text));
    if (!value)
        return false;

    setValue(*value);
    publishValue();
    widget().repaint();
    return true;
}

void ModuleControlBase::setValueToDefault()
{
    setValue(info().default_);
    publishValue();
    widget().repaint();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The LFO strip's own switch is the other way in, and both end up in
/// SpectrumWorxEditor::setLFOEnabled(): turning an LFO on is an edit of an
/// exported parameter, so it has to reach the engine and the host and not just
/// the copy this thread draws from.
///
/// \note Offered whether or not the strip is up. A control is reachable with no
/// LFO display on screen -- the shared gain and wet pair above the rack are the
/// obvious case -- and there is nothing about the switch that needs one.
///
/// \note The tick is read here, when the menu is built; the toggle re-reads when
/// it is chosen. The two can only disagree if the host moved the LFO while the
/// menu was open, and then the fresh answer is the right one.
///
////////////////////////////////////////////////////////////////////////////////

void ModuleControlBase::addLFOMenuEntry(juce::PopupMenu &menu)
{
    menu.addItem("Enable LFO", /*isEnabled*/ true, /*isTicked*/ isLFOEnabled(),
                 [pWidget = juce::Component::SafePointer<juce::Component>(&widget())] {
                     if (!pWidget)
                         return;
                     auto &control(ModuleControlBase::controlForWidget(*pWidget));
                     control.editor().setLFOEnabled(control, !control.isLFOEnabled());
                 });
}

void ModuleControlBase::moduleParameterChanged()
{
    LE_ASSERT(!editor().selectedModule() || editor().selectedModule() == &moduleUI());
    LE_ASSERT(noModuleOrModuleControlFocused() || Detail::hasDirectFocus(widget()) ||
              moduleUI().hasDirectFocus());
    LE_ASSERT(isActive());
    LE_ASSERT(!isLFOEnabled());

    publishValue();
}

////////////////////////////////////////////////////////////////////////////////
//
// ModuleControlBase::publishValue()
// ---------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note Split out of moduleParameterChanged() above, which is this and the four
/// assertions that say the user is turning the control at this moment. A value
/// typed into the knob's right button menu is the same edit made from outside
/// that: the type-in field takes the keyboard, so the knob loses it, so the
/// control deactivates -- and `isActive()` is false by the time the user presses
/// return on a value they are quite deliberately setting.
///
////////////////////////////////////////////////////////////////////////////////

void ModuleControlBase::publishValue()
{
    /// \note Checkout revision 7493 or earlier for (more templated) code
    /// that directly accessed the effect's parameters.
    ///                                       (12.03.2013.) (Domagoj Saric)
    SpectrumWorxEditor &editor(this->editor());
    std::uint8_t const parameterIndex(moduleParameterIndex() + 1); // Bypass
    editor.updateModuleParameterAndNotifyHost(moduleUI(), parameterIndex, getValue());
    editor.updateActiveControlValue();
}

void ModuleControlBase::configureControl(bool const mouseClickCanGrabFocus)
{
    widget().setWantsKeyboardFocus(true);
    widget().setMouseClickGrabsKeyboardFocus(mouseClickCanGrabFocus);
}

bool ModuleControlBase::noModuleOrModuleControlFocused(SpectrumWorxEditor const &editor)
{
    // Implementation note:
    //   We ignore focused widgets on auxiliary windows.
    //                                        (07.07.2011.) (Domagoj Saric)
    juce::Component const *const pFocusedComponent(juce::Component::getCurrentlyFocusedComponent());
    return (pFocusedComponent == nullptr) || (pFocusedComponent == &editor) ||
           (!editor.isParentOf(*pFocusedComponent));
}

bool ModuleControlBase::noModuleOrModuleControlFocused() const
{
    return noModuleOrModuleControlFocused(editor());
}

ModuleControlBase::Module &ModuleControlBase::module() { return moduleUI().module(); }

ModuleUI &ModuleControlBase::moduleUI()
{
    // Implementation note:
    //   Shared module controls are not children of ModuleUI instances so we
    // cannot just return
    // *LE::Utility::polymorphicDowncast<ModuleUI *>( widget().getParentComponent() ).
    //                                        (04.10.2011.) (Domagoj Saric)
    LE_ASSERT(pModuleUI_);
    return *pModuleUI_;
}

/// \note Was `polymorphicDowncast<SpectrumWorxEditor *>( moduleUI().getParentComponent() )`,
/// so it could only be asked once the module's region had been parented -- and
/// `Module::createGUI` writes every parameter into the widgets before that
/// happens. ModuleUI holds its editor now.
SpectrumWorxEditor &ModuleControlBase::editor() const
{
    /// \note The const_cast is what the previous body got for free: JUCE's
    /// `getParentComponent() const` hands back a non-const `Component *`.
    return const_cast<ModuleUI &>(moduleUI()).editor();
}

LE::Parameters::LFOImpl &ModuleControlBase::lfo() { return module().lfo(moduleParameterIndex()); }
LE::Parameters::RuntimeInformation const &ModuleControlBase::info() const
{
    return module().parameterInfo(moduleParameterIndex() + 1);
}
char const *ModuleControlBase::name() const { return info().name; }

bool ModuleControlBase::isActive(juce::Component const &widget)
{
    /// \note Recovers the editor from the widget rather than asking a static
    /// which control is active, so that the question is answered by the editor
    /// the widget belongs to. The caller is Theme's painting, which has a widget
    /// and nothing else.
    auto const *const pControl(SpectrumWorxEditor::fromChild(widget).activeControl());
    return pControl && (&widget == &pControl->widget());
}

juce::String ModuleControlBase::getValueString(float const *LE_RESTRICT const pValue) const
{
    std::array<char, 20> buffer;
    using Printer = Engine::ModuleParameters::ParameterPrinter;
    Printer const printer = {
        pValue ? std::optional<LE::Plugins::AutomatedParameterValue>(*pValue) : std::nullopt,
        Printer::Unchanged,
        {LE::Utility::makeSpan(&buffer[0], buffer.size()), moduleUI().editor().engineSetup()}};
    std::uint8_t const parameterIndex(moduleParameterIndex() + 1 /*Bypass*/);
    char const *const pValueString(module().getParameterValueString(parameterIndex, printer));
    char const *const pUnit(Automation::getParameterUnit(parameterIndex, &module()));
    juce::String result(pValueString);
    result.appendCharPointer(juce::CharPointer_ASCII(pUnit));
    return result;
}

/// \note The same parser `clap_plugin_params::text_to_value` runs, over the same
/// module: a value typed into a knob's menu and a value typed into the host's
/// generic panel are one question, and there is one answer to it.
std::optional<float> ModuleControlBase::parseValueString(juce::String const &text) const
{
    LE::Parameters::ParameterValueParser const parser{text.toRawUTF8(),
                                                      moduleUI().editor().engineSetup()};
    std::uint8_t const parameterIndex(moduleParameterIndex() + 1 /*Bypass*/);
    return Automation::parseParameterValue(parameterIndex, parser, module());
}

namespace
{
// Cheap cross-casting between widget and ModuleControlBase instances
class ControlWidgetBridge : public ModuleControlBase, public WidgetBase<>
{
  public:
    /// \note The dynamic_cast assertions are the checks that say something --
    /// that this really is the other half of the same object.
    static ModuleControlBase &asControl(juce::Component &widget)
    {
        ModuleControlBase &control(static_cast<ControlWidgetBridge &>(widget));
        LE_ASSERT_MSG(&control == dynamic_cast<ModuleControlBase *>(&widget),
                      "Widget is not a module control.");
        return control;
    }

    static juce::Component &asWidget(ModuleControlBase &control)
    {
        juce::Component &widget(static_cast<ControlWidgetBridge &>(control));
        LE_ASSERT_MSG(&widget == dynamic_cast<juce::Component *>(&control),
                      "Module control detached from its widget.");
        return widget;
    }

  private:
    ControlWidgetBridge();
    ~ControlWidgetBridge();
}; // class ControlWidgetBridge
} // anonymous namespace

juce::Component &ModuleControlBase::widget() { return ControlWidgetBridge::asWidget(*this); }
ModuleControlBase &ModuleControlBase::controlForWidget(juce::Component &widget)
{
    return ControlWidgetBridge::asControl(widget);
}

bool ModuleControlBase::isLFOEnabled() const { return lfo().enabled(); }

void ModuleControlBase::reassignTo(ModuleUI &moduleUI)
{
    LE_ASSERT_MSG(isASharedModuleControl(),
                  "This functionality is meant only for SharedModuleControls where the "
                  "controls are not actually owned by the ModuleUI and can thus be "
                  "assigned to different module UIs.");
    pModuleUI_ = &moduleUI;
}

void ModuleControlBase::reassignTo(std::uint8_t const parameterIndex)
{
    LE_ASSERT_MSG(isASharedModuleControl(),
                  "This functionality is meant only for SharedModuleControls where the "
                  "frequency control can change the parameter it maps to.");
    parameterIndex_ = parameterIndex;
}

void ModuleControlBase::clearActiveControl()
{
    LE_ASSERT_MSG(isASharedModuleControl(),
                  "This functionality is meant only for the "
                  "SharedModuleControls::FrequencyRange class where the same "
                  "ModuleControlBase instance can map to a different logical control.");
    editor().pActiveControl_ = nullptr;
}

bool ModuleControlBase::isASharedModuleControl() const
{
    /// \note Shared module control widgets are not children of the
    /// corresponding ModuleUI instance (but rather of the singleton
    /// SharedModuleControls instance).
    ///                                       (12.02.2014.) (Domagoj Saric)
    return pModuleUI_ != widget().getParentComponent();
}

} // namespace LE::SW::GUI
