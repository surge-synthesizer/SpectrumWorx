////////////////////////////////////////////////////////////////////////////////
///
/// \file moduleControl.hpp
/// -----------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef moduleControl_hpp__F2848AE1_5E1F_4FB9_81AB_541E17F40DD2
#define moduleControl_hpp__F2848AE1_5E1F_4FB9_81AB_541E17F40DD2
//------------------------------------------------------------------------------
/// `ParameterMenu`, which every module control is one of. \see issue #93.
#include "gui/gui.hpp"

#include "core/parameterID.hpp"
#include "le/math/conversion.hpp"
#include "le/utility/cstdint.hpp"

#include <optional>

/// \note This header names juce::Component and juce::Slider throughout but used
/// to rely on whoever included it having pulled JUCE in first. That worked while
/// there was exactly one includer.
#include <juce_gui_basics/juce_gui_basics.h>

namespace LE
{

namespace Parameters
{
class LFOImpl;
struct RuntimeInformation;
} // namespace Parameters

namespace SW
{

class Module;
class ModuleGUI;

namespace Engine
{
class Setup;
}

namespace GUI
{

class SpectrumWorxEditor;

class ModuleControlBase;
template <class ImplWidget> class ModuleControlImpl;

////////////////////////////////////////////////////////////////////////////////
///
/// \note The parameter menu is answered here and not by each widget, and that is
/// the whole of issue #93. Every control on a module strip is a widget standing
/// for one automatable parameter, and `ModuleControlBase` *is* that parameter as
/// the widget sees it -- its name, its reading, the value a typed string means,
/// its default and its LFO. So the seven questions have one set of answers, and
/// a knob, an LED, a trigger and a combo box all get the menu by being module
/// controls rather than by being knobs.
///
////////////////////////////////////////////////////////////////////////////////

template <class ImplWidget> class ModuleControl : public ParameterMenu
{
  private:
    using Impl = ModuleControlImpl<ImplWidget>;

  private: // ParameterMenu
    ////////////////////////////////////////////////////////////////////////////
    /// \note Every one of these forwards to the control, which is where the
    /// answers are: a widget's shape has nothing to do with what its parameter
    /// is called or what typing into it means. \see ModuleControlBase.
    ////////////////////////////////////////////////////////////////////////////
    juce::Component &menuOwner() override { return control().widget(); }

    juce::String parameterName() const override { return control().parameterMenuName(); }
    juce::String parameterValueText() const override { return control().getValueText(); }
    ParameterID parameterID() const override { return control().parameterMenuID(); }

    bool parameterEditable() const override { return !isLFOEnabled(); }

    bool setParameterFromText(juce::String const &text) override
    {
        return control().setValueFromText(text);
    }
    void setParameterToDefault() override { control().setValueToDefault(); }
    void addParameterMenuEntries(juce::PopupMenu &menu) override
    {
        control().addLFOMenuEntry(menu);
    }

  protected: // Utility functions
    bool isLFOEnabled() const
    {
        return impl().ModuleControlBase::isLFOEnabled();
    } //...mrmlj...try to access the effect-specific lfo directly...
    void moduleParameterChanged() { impl().ModuleControlBase::moduleParameterChanged(); }
    void beginGesture() { impl().ModuleControlBase::beginGesture(); }
    void endGesture() { impl().ModuleControlBase::endGesture(); }

    ModuleControlBase &control()
    { /*LE_ASSERT( &ModuleControlBase::controlForWidget( impl() ) == &impl() );*/ return impl(); }
    /// \note The const half, for the widget's own const questions -- what this
    /// parameter is called and what it currently reads as.
    ModuleControlBase const &control() const { return impl(); }

  protected: // Default implementations for the module control interface
    static bool const mouseClickCanGrabFocus = false;

  protected: // Default implementations for the module control interface callbacks
    static void lfoStateChanged() {};

    static void moduleControlActivated() {}
    static void moduleControlDeactivated() {}

    static void updateForEngineSetupChanges(Engine::Setup const &) {}

  private:
    Impl &impl() { return static_cast<Impl &>(*this); }
    Impl const &impl() const { return static_cast<Impl const &>(*this); }
}; // class ModuleControl

class ModuleUI;

////////////////////////////////////////////////////////////////////////////////
///
/// \class ModuleControlBase
///
////////////////////////////////////////////////////////////////////////////////

class ModuleControlBase
{
  protected:
    ModuleControlBase(std::uint8_t const parameterIndex, ModuleUI &moduleUI)
        : parameterIndex_(parameterIndex), pModuleUI_(&moduleUI)
    {
    }

    // Implementation note:
    //   ModuleControls do not need to explicitly deactivate themselves in the
    // destructor because all the info for the corresponding ModuleUI will be
    // erased from the main window when the parent ModuleUI deactivates in its
    // destructor.
    //                                        (14.11.2011.) (Domagoj Saric)

  public:
    virtual void setValue(float) = 0;
    virtual float getValue() const = 0;

    virtual void lfoStateChanged() = 0;

    /// \brief Tells this control it is no longer the selected one.
    ///
    /// \note Asked of the control that is *losing* the selection, by the one
    /// taking it. Losing the keyboard no longer does it. \see issue #139.
    virtual void deselect() = 0;

    virtual void updateForEngineSetupChanges(Engine::Setup const &) {}

    // Implementation note:
    //   The names (and signatures) of these two functions were chosen to match
    // the existing virtual functions of the Knob widget class (in order to
    // reuse them more easily).
    //                                        (07.07.2011.) (Domagoj Saric)
    ///
    /// \note `getValueString( nullptr )`, which formatted the *engine's* value.
    /// The interface's edits are queued now, so the engine may not have applied
    /// the last one yet and the readout would lag a drag by a block -- or for
    /// ever, with a host that is not processing. The widget's own value is what is
    /// on screen and is what the caption should say.
    juce::String getValueText() const
    {
        auto const value(getValue());
        return getValueString(&value);
    }
    juce::String getTextFromValue(float const value) const { return getValueString(&value); }

    juce::String getValueString(float const *LE_RESTRICT pValue) const;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief getValueString() backwards: the value this parameter would have to
    /// hold for it to read as \p text, in the widget's own units -- or nothing at
    /// all, when no value of it does.
    ///
    /// \note Nothing rather than a clamp, which is the whole reason it is an
    /// optional: a knob handed a clamped value has told the user their typo was
    /// understood. \see LE::Parameters::parse().
    ///
    ////////////////////////////////////////////////////////////////////////////
    std::optional<float> parseValueString(juce::String const &text) const;

    Parameters::RuntimeInformation const &info() const;
    char const *name() const;

    //...mrmlj...excludes non-lfoable parameters (Bypass)...
    std::uint8_t moduleParameterIndex() const { return parameterIndex_; }

    /// \brief Whether this is the selected control -- the one the LFO strip is
    /// showing, and the one wearing the halo. Not the keyboard. \see issue #139.
    bool isActive() const;

    bool isLFOEnabled() const;

    void moduleParameterChanged();

    /// \brief The widget's value, into the Program and out to the host --
    /// moduleParameterChanged() without the assertions that say the user has
    /// this control under the mouse right now. \see the definition.
    void publishValue();

    /// \brief What a control does when the pointer leaves it. \see the definition.
    void mouseLeft();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The host's automation gesture for this parameter, held open across
    /// a drag. \see issue #188 and SpectrumWorxEditor::moduleControlGestureBegin().
    ///
    /// \note Only a drag needs these. Every other edit is one value with nothing
    /// either side of it, and `publishValue()` gives it a gesture of its own.
    ///
    ////////////////////////////////////////////////////////////////////////////
    ///@{
    void beginGesture();
    void endGesture();
    bool gestureIsOpen() const { return gestureOpen_; }

    /// \brief Whether this edit has to bracket itself -- true unless a drag has
    /// already opened a gesture for this very parameter.
    bool needsOwnGesture() const;
    ///@}

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \name What the right button's menu asks of a module control
    ///
    ///   The three answers that are not one-liners over the getters above.
    /// Written here rather than in `ModuleControl<>` because each of them needs
    /// the editor, and a template in a header that the widgets include cannot
    /// have it. \see ParameterMenu and issue #93.
    ///
    ////////////////////////////////////////////////////////////////////////////
    ///@{
    /// Which parameter this is, as the host knows it.
    ParameterID parameterMenuID() const;

    /// \brief The section header: which module strip this parameter is on, then
    /// what it is called. \see issue #203.
    juce::String parameterMenuName() const;

    /// \return false for text no value of this parameter displays as, which
    /// leaves the parameter where it was.
    bool setValueFromText(juce::String const &);

    void setValueToDefault();

    /// The LFO switch, which is every module control's and not only a knob's.
    void addLFOMenuEntry(juce::PopupMenu &);
    ///@}

    using Module = SW::Module;

    using LFO = Parameters::LFOImpl;

    LFO &lfo();
    LFO const &lfo() const { return const_cast<ModuleControlBase &>(*this).lfo(); }
    Module &module();
    Module const &module() const { return const_cast<ModuleControlBase &>(*this).module(); }
    juce::Component &widget();
    juce::Component const &widget() const
    {
        return const_cast<ModuleControlBase &>(*this).widget();
    }
    ModuleUI &moduleUI();
    ModuleUI const &moduleUI() const { return const_cast<ModuleControlBase &>(*this).moduleUI(); }
    SpectrumWorxEditor &editor() const;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Whether this control's back-pointer is into \p region.
    ///
    /// \note A comparison and not a dereference, which is the point: it is asked
    /// while \p region is being torn down, of controls that may already be
    /// pointing at freed memory. `moduleUI()` would assert and then hand out a
    /// reference to it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    bool pointsInto(ModuleUI const &region) const { return pModuleUI_ == &region; }

    static ModuleControlBase &controlForWidget(juce::Component &);

    static bool isActive(juce::Component const &);

    bool noModuleOrModuleControlFocused() const; //...mrmlj...
    static bool noModuleOrModuleControlFocused(SpectrumWorxEditor const &);

  protected:
    bool reportActiveControl(double minimum, double maximum, double interval);
    bool reportInactiveControl();

    void configureControl(bool mouseClickCanGrabFocus);

  private:
    /// \note DIRTY HACKS:
    /// See the note in the
    /// SharedModuleControls::FrequencyRange::reportActiveControl() member
    /// function.
    ///                                       (12.02.2014.) (Domagoj Saric)
    friend class SharedModuleControls;
    void clearActiveControl();

    void reassignTo(ModuleUI &);
    void reassignTo(std::uint8_t parameterIndex);

    bool isASharedModuleControl() const;

  private:
    bool tryActivateControl() const;

  private:
    std::uint8_t parameterIndex_;
    ModuleUI *LE_RESTRICT pModuleUI_;

    /// \note The widget's, not the editor's: two controls can be mid-gesture at
    /// once only if two mice are, but this says which *parameter* is held open
    /// and that is this control's own. The ID is kept rather than recomputed
    /// because the frequency range can change which of its two parameters it
    /// stands for while the mouse is still down.
    ///@{
    bool gestureOpen_{false};
    ParameterID gestureID_;
    ///@}

    /// \note Which control is active is the *editor's* -- see
    /// SpectrumWorxEditor::pActiveControl_ -- so that two instances of the plugin
    /// in one host do not share an answer.
}; // class ModuleControlBase

////////////////////////////////////////////////////////////////////////////////
///
/// \class ModuleControlImpl
///
////////////////////////////////////////////////////////////////////////////////

#pragma warning(push)
#pragma warning(disable : 4355) // 'this' used in base member initializer list.

template <class ImplWidget>
class ModuleControlImpl final : public ModuleControlBase, public ImplWidget
{
  public:
    using BaseWidget = typename ImplWidget::BaseWidget;

  public:
    ModuleControlImpl(juce::Component &parent, ModuleUI &moduleUI, std::uint16_t const x,
                      std::uint16_t const y, std::uint8_t const parameterIndex)
        : ModuleControlBase(parameterIndex, moduleUI), ImplWidget(parent, x, y)
    {
        configureControl(ImplWidget::mouseClickCanGrabFocus);
    }

    using ModuleControlBase::lfo;
    using ModuleControlBase::moduleParameterChanged;

    using ImplWidget::getValue;
    using ImplWidget::setValue;

    void setValue(float const value) override
    {
        return ImplWidget::setValue(Math::convert<typename BaseWidget::param_type>(value));
    }
    float getValue() const override { return Math::convert<float>(ImplWidget::getValue()); }

    void updateForEngineSetupChanges(Engine::Setup const &engineSetup) override
    {
        ImplWidget::updateForEngineSetupChanges(engineSetup);
    }

  private:
    void lfoStateChanged() override { ImplWidget::lfoStateChanged(); }

    void reportActiveControl()
    {
        bool const activated(ModuleControlBase::reportActiveControl(
            ImplWidget::valueRangeMinimum(), ImplWidget::valueRangeMaximum(),
            ImplWidget::valueRangeQuantum()));
        if (activated)
            ImplWidget::moduleControlActivated();
    }

    void reportInactiveControl()
    {
        bool const deactivated(ModuleControlBase::reportInactiveControl());
        if (deactivated)
            ImplWidget::moduleControlDeactivated();
    }

    /// \note The same thing asked from outside. `moduleControlDeactivated()` is
    /// the widget's own and resolves in this template, so the editor cannot reach
    /// it through a ModuleControlBase -- and it has to, now that the control
    /// losing the selection is told by the one taking it. \see issue #139.
    void deselect() override { reportInactiveControl(); }

    using ModuleControlBase::isActive;

  private:
    virtual void focusGained(juce::Component::FocusChangeType) override
    {
        LE_ASSERT(this->getWantsKeyboardFocus());
        reportActiveControl();
    }
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note **Deliberately nothing.** Losing the keyboard is not losing the
    /// selection: this used to retire the LFO strip, on the assumption that the
    /// focus was on its way to another module control. It goes to a preset row, a
    /// settings box, a host's automation panel or another application at least as
    /// often, and each of those wiped the strip the user had just set up. The
    /// selection is handed over by whoever takes it instead.
    /// \see reportActiveControl() and issue #139.
    ///
    /// \note Which is why the halo is not drawn on the focus either -- it says
    /// which control the LFO strip is showing, and a control keeps that when the
    /// keyboard leaves. \see ModuleControlBase::isActive().
    ///
    /// \note Which retires three guards with it. There is no longer a tear-down
    /// to keep away from an open menu -- the knob's own right button menu takes
    /// the keyboard for its type-in field, and used to deselect the module and
    /// destroy the shared controls the knob might itself be one of. Nor is there
    /// a disabled-control case to excuse: `getWantsKeyboardFocus()` reads
    /// `wantsKeyboardFocusFlag && !isDisabledFlag`, so a control disabled while
    /// focused arrived here reading "does not want focus", and nothing asks any
    /// more.
    ///
    ////////////////////////////////////////////////////////////////////////////
    virtual void focusLost(juce::Component::FocusChangeType) noexcept override {}

    virtual void mouseEnter(juce::MouseEvent const &) override { reportActiveControl(); }
    virtual void mouseExit(juce::MouseEvent const &) noexcept override { mouseLeft(); }
}; // class ModuleControlImpl

#pragma warning(pop)

} // namespace GUI

} // namespace SW

} // namespace LE

#endif // moduleControl_hpp
