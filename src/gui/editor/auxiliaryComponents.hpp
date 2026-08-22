////////////////////////////////////////////////////////////////////////////////
///
/// \file auxiliaryComponents.hpp
/// -----------------------------
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef auxiliaryComponents_hpp__66FD7192_4423_4467_8D99_99BF0FD87CB3
#define auxiliaryComponents_hpp__66FD7192_4423_4467_8D99_99BF0FD87CB3
//------------------------------------------------------------------------------
#include "gui/modules/moduleControl.hpp"
#include "gui/modules/moduleUI.hpp"

#include "le/utility/cstdint.hpp"
#include "le/utility/platformSpecifics.hpp"

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
/// \internal
/// \class SharedModuleControls
////////////////////////////////////////////////////////////////////////////////

class SharedModuleControls : public WidgetBase<>
{
  private:
    typedef ModuleControlImpl<ModuleKnob> Knob; //...mrmlj...are these logically "module knobs"?

  public:
    ////////////////////////////////////////////////////////////////////////////
    /// \note A ParameterMenu like every other module control, and the one that
    /// had to say which of its two parameters the menu is about: whichever thumb
    /// the press was nearest. \see issue #93.
    ////////////////////////////////////////////////////////////////////////////

    class FrequencyRange final : public ModuleControlBase,
                                 public WidgetBase<juce::Slider>,
                                 public SliderWithSelectedThumb,
                                 public ParameterMenu
    {
      public:
        FrequencyRange();

        /// \note Which thumb this control currently stands for -- set by hover
        /// as well as by dragging, which is why it is not
        /// juce::Slider::getThumbBeingDragged(). See selectedThumb_.
        int selectedThumb() const override { return selectedThumb_; }

      public: // module control traits
        typedef float value_type;
        typedef float param_type;

        value_type getStartValue() const
        {
            return static_cast<value_type>(juce::Slider::getMinValue());
        }
        void setStartValue(param_type const newValue)
        {
            juce::Slider::setMinValue(static_cast<value_type>(newValue), juce::dontSendNotification,
                                      false);
        }

        value_type getStopValue() const
        {
            return static_cast<value_type>(juce::Slider::getMaxValue());
        }
        void setStopValue(param_type const newValue)
        {
            juce::Slider::setMaxValue(static_cast<value_type>(newValue), juce::dontSendNotification,
                                      false);
        }

        void updateForEngineSetupChanges(Engine::Setup const &) override;

        ModuleControlBase &startControl();
        ModuleControlBase &stopControl();

      protected: // ModuleControlBase overrides
        void lfoStateChanged() override {}

        void setValue(float value) override;
        float getValue() const override;

      private: // ParameterMenu
        /// \note Every one of these is the module control's own answer, for the
        /// thumb selectedThumb_ names. \see ModuleControl<>, which is the same
        /// forwarding for a widget that stands for one parameter rather than two.
        juce::Component &menuOwner() override { return *this; }

        juce::String parameterName() const override { return name(); }
        juce::String parameterValueText() const override { return getValueText(); }
        ParameterID parameterID() const override { return parameterMenuID(); }

        bool parameterEditable() const override { return !isLFOEnabled(); }

        bool setParameterFromText(juce::String const &text) override
        {
            return selectedControl().setValueFromText(text);
        }
        void setParameterToDefault() override { selectedControl().setValueToDefault(); }
        void addParameterMenuEntries(juce::PopupMenu &menu) override { addLFOMenuEntry(menu); }

        /// \brief This control pointed at the thumb's parameter, which is what an
        /// edit needs. \see parameterIndexForInternalWriteAccess_.
        ModuleControlBase &selectedControl();

      private:
        void focusGained(juce::Component::FocusChangeType) override;
        void focusLost(juce::Component::FocusChangeType) override;

        void mouseDown(juce::MouseEvent const &) noexcept override;
        void mouseDrag(juce::MouseEvent const &) noexcept override;

        void mouseEnter(juce::MouseEvent const &) noexcept override;
        void mouseExit(juce::MouseEvent const &) noexcept override;

        void mouseMove(juce::MouseEvent const &) noexcept override;

        void valueChanged() noexcept override;

        void paint(juce::Graphics &) override;

      private:
        void reportActiveControl();
        void reportInactiveControl();

        void updateSliderSelection(juce::MouseEvent const &);

        std::uint8_t activeParameterIndex() const;
        std::uint8_t thumbToParameterIndex() const;
        void verifyThumbAndParameterIndicies() const;

        LFO &lfo();
        LFO const &lfo() const { return const_cast<FrequencyRange &>(*this).lfo(); }

        SharedModuleControls &parent();

      private:
        /// \note DIRTY HACK:
        /// Because the (single) FrequencyRange widget/control actually maps to
        /// two module parameters we need a way to discriminate between the two,
        /// that is to detect which parameter the widget is currently
        /// representing. The real problem is the fact that a FrequencyRange
        /// instance may need to map to both parameters at the same time: in the
        /// GUI thread the user is accessing the StartFrequency parameter while
        /// an LFO is changing the StopFrequency parameter from the processing
        /// thread.
        /// The first solution was to have two ModuleControl instances both
        /// mapping to the same widget (FrequencyRange) but this became no
        /// longer doable after the module widget/control code was refactored
        /// to allow a one-to-one static cross-casting between juce::Component
        /// and GUI::ModuleControlBase classes (see the implementation of the
        /// ModuleControlBase::controlForWidget() member function).
        /// The current solution uses the fact that concurrent access will
        /// always happen through different parts of the API:
        /// - GUI code will call ModuleControlBase::getValue() and change the
        /// "value"/position of the control directly through/from OS/JUCE events
        /// - internal/automation/preset/LFO code will use
        /// ModuleControlBase::setValue() and never call
        /// ModuleControlBase::getValue().
        /// This is why FrequencyRange::getValue() uses the
        /// ModuleControlBase::moduleParameterIndex() member functions while
        /// setValue() uses the below parameterIndexForInternalWriteAccess_
        /// data member.
        ///                                   (12.02.2014.) (Domagoj Saric)
        std::uint8_t parameterIndexForInternalWriteAccess_;
        bool canUseWriteAccessIndex() const;

        /// \note The 2016 code kept this in juce::Slider::sliderBeingDragged,
        /// which was protected then and is private now -- and which JUCE clears
        /// the moment a drag ends, whereas this must survive a hover. Ours.
        int selectedThumb_;

        /// Shift-drag, as on every other slider. \see HorizontalSlider.
        FineDrag fine_;
    }; // class FrequencyRange

  public:
    SharedModuleControls();

    void updateForEngineSetupChanges(Engine::Setup const &);

    void updateForActiveModule();

    ModuleControlBase &controlForParameter(std::uint8_t parameterIndex);

    /// For a headless run that presses it. \see issue #93.
    FrequencyRange &frequencyRange() { return frequencyRange_; }

    /// \brief Whether these controls are pointing into \p region.
    ///
    /// \note All three are reassigned together by `updateForActiveModule()`, so
    /// one of them is the answer for all of them.
    /// \see ModuleControlBase::pointsInto().
    bool pointsInto(ModuleUI const &region) const { return gain_.pointsInto(region); }

  private:
    SpectrumWorxEditor &editor();
    SpectrumWorxEditor const &editor() const;

  private: // JUCE Component overrides.
    void focusLost(FocusChangeType) override;
    void focusOfChildComponentChanged(FocusChangeType) override;

  private:
    Knob gain_;
    Knob wet_;
    FrequencyRange frequencyRange_;
}; // class SharedModuleControls

} // namespace LE::SW::GUI

#endif // auxiliaryComponents_hpp
