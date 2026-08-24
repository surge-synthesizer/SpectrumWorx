////////////////////////////////////////////////////////////////////////////////
///
/// \file moduleUI.hpp
/// ------------------
///
/// Module UI related functionality.
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef moduleUI_hpp__8228E5F3_535E_4B08_9AD0_072C9fA7AD93
#define moduleUI_hpp__8228E5F3_535E_4B08_9AD0_072C9fA7AD93
//------------------------------------------------------------------------------
#include "gui/gui.hpp"
#include "gui/painters/moduleKnobPainter.hpp"
#include "gui/painters/moduleStripPainter.hpp"
#include "gui/modules/moduleControl.hpp"
#include "gui/modules/moduleWidgets.hpp"

#include "le/utility/intrusivePtr.hpp"

#include "le/math/conversion.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/boolean/tag.hpp"
#include "le/parameters/enumerated/tag.hpp"
#include "le/parameters/powerOfTwo/tag.hpp"
#include "le/parameters/trigger/tag.hpp"
#include "le/parameters/symmetric/tag.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/parameters/printer_fwd.hpp"
#include "le/utility/cstdint.hpp"
#include "le/utility/platformSpecifics.hpp"
#include "le/utility/tchar.hpp"

#include "le/utility/polymorphicDowncast.hpp"

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace LE::SW
{

class Module;
class ModuleGUI;

namespace Effects
{
namespace Detail
{
struct EmptyParameters;
}

namespace BaseParameters
{
class Bypass;
class Gain;
class Wet;
class StartFrequency;
class StopFrequency;
} // namespace BaseParameters
} // namespace Effects

namespace Engine
{
class Setup;
}

namespace GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class ModuleKnob
///
////////////////////////////////////////////////////////////////////////////////

class ModuleUI;

class ModuleKnob : public Knob, public ModuleControl<ModuleKnob>
{
  protected:
    ModuleKnob(juce::Component &parent, unsigned int x, unsigned int y);

  private: // Knob
    /// \note The ModuleControl half, which is where a module control's menu is
    /// answered. \see issue #93.
    ParameterMenu &parameterMenu() override { return static_cast<ModuleControl &>(*this); }

  public:
#pragma warning(push)
#pragma warning(                                                                                   \
    disable                                                                                        \
    : 4480) // Nonstandard extension used: specifying underlying type for enum 'SW::Effects::PhaseVocoderShared::pitchShiftAndScale::TransientBins'.
    enum Quantization : std::uint8_t
    {
        Fixed,
        FrequencyInHertz,
        TimeInMilliseconds
    };
#pragma warning(pop)

    /// Which end the value wedge opens from: the left stop, or twelve o'clock.
    enum Polarity : std::uint8_t
    {
        Unipolar,
        Bipolar
    };

    /// The two sizes a module knob comes in: the one in a module's panel, and
    /// the smaller gain and wet pair in the shared controls above the rack.
    /// These were the film strips' frame widths.
    ///
    /// \note constexpr rather than the `static unsigned int const` the two
    /// margins below are: that form is odr-usable and so wants an out-of-line
    /// definition, which is what the `#ifdef __GNUC__` after spaceForText is.
    static constexpr unsigned int diameter{77};
    static constexpr unsigned int smallDiameter{35};

  private:
    using Hertz = LE::Parameters::UnitString<" Hz">;
    using Millisecond = LE::Parameters::UnitString<" ms">;

    template <typename Unit> struct QuantizationImpl;

  public:
    template <class Parameter>
    struct QuantizationFor : QuantizationImpl<typename LE::Parameters::Detail::GetTraitDefaulted<
                                 LE::Parameters::Traits::Tag::Unit, typename Parameter::Traits,
                                 typename Parameter::Defaults>::type>
    {
    }; // struct QuantizationFor

    void setupForParameter(Polarity, unsigned int knobDiameter, Quantization quantizationType,
                           std::uint8_t quantizationStep);

  private: // juce::Component overrides
    void mouseDrag(juce::MouseEvent const &) noexcept override;

    void valueChanged() noexcept override;

    /// \brief The press and the release, which is what a host gesture brackets.
    /// \see issue #188.
    ///@{
    void startedDragging() noexcept override;
    void stoppedDragging() noexcept override;
    ///@}

    void paint(juce::Graphics &) override;

  public:
    /// \brief The circle, not the widget: the margin the focus halo needs and
    /// the strip below it that carries the caption are the module's, not the
    /// knob's. \see Knob::isOnKnobFace(), issue #92.
    bool isOnKnobFace(juce::Point<int>) const override;

  protected: // ModuleControl interface.
    void lfoStateChanged();

    void updateForEngineSetupChanges(Engine::Setup const &);

    void moduleControlActivated();
    void moduleControlDeactivated();

    double valueRangeMinimum() const { return getMinimum(); }
    double valueRangeMaximum() const { return getMaximum(); }
    double valueRangeQuantum() const { return getInterval(); }

    static bool const mouseClickCanGrabFocus = true;

  public:
    typedef Knob BaseWidget;

  private:
    void syncMouseWheelAndLFOState();

  private:
    Quantization quantization_;
    Polarity polarity_;
    unsigned int diameter_;

  private:
    static unsigned int const marginForGlow = 6;
    static unsigned int const spaceForText = 27;
}; // class ModuleKnob

template <typename Unit> struct ModuleKnob::QuantizationImpl
{
    static Quantization const value = Fixed;
};
template <> struct ModuleKnob::QuantizationImpl<ModuleKnob::Hertz>
{
    static Quantization const value = FrequencyInHertz;
};
template <> struct ModuleKnob::QuantizationImpl<ModuleKnob::Millisecond>
{
    static Quantization const value = TimeInMilliseconds;
};

////////////////////////////////////////////////////////////////////////////////
///
/// \class ModuleLEDTextButton
///
////////////////////////////////////////////////////////////////////////////////

class ModuleLEDTextButton : public LEDTextButton, public ModuleControl<ModuleLEDTextButton>
{
  protected:
    ModuleLEDTextButton(juce::Component &parent, unsigned int x, unsigned int y);

  private: // juce::Component overrides
    void mouseDown(juce::MouseEvent const &) override;
    void paintButton(juce::Graphics &, bool isMouseOverButton, bool isButtonDown) override;

  private: // ParameterMenu
    /// \note \see TriggerButton::parameterAcceptsText(), which carries the reason
    /// all three of the non-knob controls answer no.
    bool parameterAcceptsText() const override { return false; }

  protected: // ModuleControl interface.
    // Implementation note:
    //   We allow a smooth LFO range control for boolean parameters.
    //                                        (21.07.2011.) (Domagoj Saric)
    static double valueRangeQuantum() { return 0; }

    /// The two words a boolean reads as, which BitmapButton lent this while
    /// there was a bitmap to hold.
    static char const *getTextFromValue(unsigned int const booleanValue)
    {
        static char const *const strings[] = {"off", "on"};
        return strings[booleanValue];
    }
    char const *getValueText() const { return getTextFromValue(getValue()); }

  public:
    typedef CapsuleButton BaseWidget;

  private:
    void clicked() override;
}; // class ModuleLEDTextButton

////////////////////////////////////////////////////////////////////////////////
///
/// \class TriggerButton
///
////////////////////////////////////////////////////////////////////////////////

/// \note Was a BitmapButton, holding skin files 13 and 14 -- which were the
/// module knob's dome with a cap that turns blue. It paints itself now, from
/// KnobPainter and TriggerButtonStyle, and what is left of that class here is
/// the two words a boolean reads as.
class TriggerButton : public WidgetBase<juce::Button>, public ModuleControl<TriggerButton>
{
  public: // ModuleUI control traits
    /// \note BitmapButton's, which this inherited while it was one. A trigger
    /// is a boolean and reads its own value off the mouse rather than off a
    /// toggle state, so getValue() below is its own; the range is not.
    typedef bool value_type;
    typedef value_type param_type;

    static value_type valueRangeMinimum() { return false; }
    static value_type valueRangeMaximum() { return true; }

  protected:
    TriggerButton(juce::Component &parent, unsigned int x, unsigned int y);

  public:
    value_type getValue() const { return isDown(); }
    void setValue(param_type);

  protected: // ModuleControl interface.
    void lfoStateChanged() { setValue(false); }

    // Implementation note:
    //   We allow a smooth LFO range control for boolean parameters.
    //                                        (21.07.2011.) (Domagoj Saric)
    static double valueRangeQuantum() { return 0; }

    static char const *getTextFromValue(unsigned int const booleanValue)
    {
        static char const *const strings[] = {"off", "on"};
        return strings[booleanValue];
    }
    char const *getValueText() const { return getTextFromValue(getValue()); }

  public:
    typedef TriggerButton BaseWidget;

    /// \brief Whether \p position is on the round face rather than on the strip
    /// showing through around it. \see isOnRoundFace(), Knob::isOnKnobFace() and
    /// issue #92 -- a trigger is the same widget as a knob in a different shape,
    /// a circle with its caption underneath and margin either side.
    bool isOnFace(juce::Point<int> position) const;

  private: // juce::Component overrides
    void mouseDown(juce::MouseEvent const &) override;
    void mouseUp(juce::MouseEvent const &) noexcept override;

  private: // ParameterMenu
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note **A knob is the only module control you type a value into.**
    ///
    ///   A trigger is an event rather than a number and there is no text that
    /// says "fire"; a boolean and an enumeration are *chosen* rather than typed,
    /// and for both of them the choice is already on the widget -- an LED you
    /// press, a list you drop. What issue #93 asked for on these three is the
    /// host's own entries, which is what they now have; the field a knob offers
    /// would be a second and worse way to do what the widget does.
    ///
    ////////////////////////////////////////////////////////////////////////////

    bool parameterAcceptsText() const override { return false; }

    void paintButton(juce::Graphics &, bool isMouseOverButton, bool isButtonDown) override;

  private:
    /// Where paintButton() puts the face: centred across the strip, at the top.
    juce::Rectangle<int> faceBounds() const;
}; // class TriggerButton

////////////////////////////////////////////////////////////////////////////////
///
/// \class DiscreteParameter
///
/// \brief Module UI widget for parameters with special discrete values.
///
////////////////////////////////////////////////////////////////////////////////

class DiscreteParameter : public ComboBox, public ModuleControl<DiscreteParameter>
{
  protected:
    DiscreteParameter(juce::Component &parent, unsigned int x, unsigned int y);

  private:
    void mouseDown(juce::MouseEvent const &) override;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Refused while the LFO is on, which is the condition the menu opens
    /// under and the one ModuleKnob turns its own wheel off for: the LFO owns
    /// the value, so a row the user scrolled to would be overwritten by the next
    /// sweep. \see syncMouseWheelAndLFOState() and issue #124.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void mouseWheelMove(juce::MouseEvent const &, juce::MouseWheelDetails const &) override;

    /// \note The same call the menu's own callback makes.
    void selectionScrolled() override;

  private: // ParameterMenu
    /// \note \see TriggerButton::parameterAcceptsText(). What a combo box has
    /// instead is its values, which is the next line.
    bool parameterAcceptsText() const override { return false; }

    void addParameterValueEntries(juce::PopupMenu &) override;

  private: // ComboBox
    /// \note The selection rather than the keyboard, as on every other module
    /// control. \see ModuleControlBase::isActive() and issue #139.
    bool showsAsSelected() const override { return control().isActive(); }

  protected: // ModuleControl interface.
    juce::String const &getTextFromValue(value_type const valueIndex) const
    {
        return getItemText(valueIndex);
    }
    juce::String const &getValueText() const { return getSelectedItemText(); }

    double valueRangeMaximum() const { return Math::convert<double>(numberOfItems() - 1); }

  public:
    typedef ComboBox BaseWidget;

  private:
    static unsigned int const horizontalMargin = 12;
    static unsigned int const textHeight = 17;
}; // class DiscreteParameter

////////////////////////////////////////////////////////////////////////////////
///
/// \class ModuleUI
///
////////////////////////////////////////////////////////////////////////////////

namespace Detail
{
template <typename ParameterTag> struct WidgetForParameterAux
{
    typedef ModuleKnob type;
};
template <> struct WidgetForParameterAux<Parameters::BooleanParameterTag>
{
    typedef ModuleLEDTextButton type;
};
template <> struct WidgetForParameterAux<Parameters::EnumeratedParameterTag>
{
    typedef DiscreteParameter type;
};
template <> struct WidgetForParameterAux<Parameters::TriggerParameterTag>
{
    typedef TriggerButton type;
};

template <class Parameter>
using WidgetForParameter = WidgetForParameterAux<typename Parameter::Tag>;
} // namespace Detail

class SharedModuleControls;
class SpectrumWorxEditor;

class ModuleUI final : public WidgetBase<>, private juce::Button::Listener
{
  public:
    enum ParameterChangeSource
    {
        AutomationOrPreset,
        LFOValue
    }; // enum ParameterChangeSource

    void setBaseParameter(std::uint8_t sharedParameterIndex, float parameterValue,
                          ParameterChangeSource);
    void setEffectParameter(std::uint8_t effectParameterIndex, float parameterValue,
                            ParameterChangeSource);
    void setParameter(std::uint8_t parameterIndex, float parameterValue, ParameterChangeSource);

    void setBypass(bool);

    void updateForEngineSetupChanges(Engine::Setup const &);

    void updateLFOParameter(std::uint8_t parameterIndex, std::uint8_t lfoParameterIndex,
                            float /*Parameters::RuntimeInformation::value_type*/ value);

  public:
    juce::String const &description() const { return description_; }

    SpectrumWorxEditor &editor();
    SpectrumWorxEditor const &editor() const;
    SharedModuleControls &sharedControls();

    typedef SW::Module Module;

    /// \note Was `Module::fromGUI( *this )` -- pointer arithmetic from the
    /// region back to the module that held it as a member. The region holds the
    /// module now, the other way round.
    Module &module();
    Module const &module() const;

    /// \brief Which slot this strip is drawn in, as last set by moveToSlot().
    std::uint8_t slot() const { return slot_; }

    /// \brief Whether \p position -- in this strip's coordinates -- is somewhere
    /// it can be picked up by: the row the eject `X` is in, or the name under the
    /// blue rule. Public because it is also what the cursor says.
    bool isDragHandle(juce::Point<int> position) const;

    /// \note Which module is selected is the *editor's*, not the process's: two
    /// instances sharing one pointer would let the second to select a module
    /// deselect the first's, and one closing would leave the other holding a
    /// pointer into freed storage.
    bool selected() const;

  public:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief One module's strip: its controls, and where they are on screen.
    ///
    /// \note The module is held by *reference count*, a region having to outlive
    /// the chain dropping the module -- a host can empty a slot from its own
    /// panel while the window is open. The other way round, the module could not
    /// be destroyed until the message thread had taken the region down, and the
    /// thread dropping the last reference can be the audio one.
    ///
    /// \note The editor is held rather than recovered from the component
    /// hierarchy: the constructor writes every parameter into the widgets before
    /// the region is parented.
    ///
    ////////////////////////////////////////////////////////////////////////////

    ModuleUI(SpectrumWorxEditor &, LE::Utility::IntrusivePtr<SW::Module>, std::uint8_t slotIndex);
    ~ModuleUI();

    void setUpForEffect(char const *effectName, char const *effectDescription);

    void moveToSlot(std::uint8_t slotIndex);

    ModuleControlBase &effectSpecificParameterControl(std::uint8_t parameterIndex);
    ModuleControlBase const &effectSpecificParameterControl(std::uint8_t parameterIndex) const;

  private:
    friend class SpectrumWorxEditor;
    friend class SharedModuleControls; //...mrmlj...
    void activate();
    void deactivate();
    bool selectionTracksMouseMovements() const;

  private:
    /// \brief A hand over a drag handle and the arrow everywhere else. \see
    /// isDragHandle().
    void updateCursorFor(juce::Point<int> position);

  private: // JUCE Component overrides.
    void paint(juce::Graphics &) override;

    /// \note Only the right button, which opens the effect menu; the left one is
    /// the drag, and that starts in mouseDrag().
    void mouseDown(juce::MouseEvent const &) override;
    void mouseDrag(juce::MouseEvent const &) override;
    void mouseEnter(juce::MouseEvent const &) override;
    void mouseExit(juce::MouseEvent const &) noexcept override;
    /// \note Only the cursor. JUCE reveals it right after delivering this, which
    /// is what lets one component carry two.
    void mouseMove(juce::MouseEvent const &) override;
    void mouseUp(juce::MouseEvent const &) noexcept override;

    void focusGained(FocusChangeType) override;
    void focusLost(FocusChangeType) override;
    void focusOfChildComponentChanged(FocusChangeType) override;

  private: // JUCE ButtonListener overrides.
    void buttonClicked(juce::Button *) override;

  public:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The blue pill at the foot of a strip: the module's Bypass, with
    /// the menu every other automatable parameter carries.
    ///
    /// \note Not a ModuleControl, which is how the rest of a strip's widgets get
    /// the menu. Those stand for an LFO-able parameter and Bypass is the one
    /// that is not -- it is the parameter the LFO-able index is counted *past*.
    /// \see issue #93.
    ///
    ////////////////////////////////////////////////////////////////////////////

    class BypassButton final : public CapsuleButton, public ParameterMenu
    {
      public:
        explicit BypassButton(ModuleUI &parent);

      private: // juce::Component overrides
        void mouseDown(juce::MouseEvent const &) override;

      private: // ParameterMenu
        juce::Component &menuOwner() override { return *this; }

        juce::String parameterName() const override;
        juce::String parameterValueText() const override;
        ParameterID parameterID() const override;

        /// \note \see TriggerButton::parameterAcceptsText(), which carries the
        /// reason a boolean offers no field: the other of its two values is a
        /// press away on the widget itself.
        bool parameterAcceptsText() const override { return false; }
        bool setParameterFromText(juce::String const &) override { return false; }
        void setParameterToDefault() override;

        /// \note The pill's own state, where a knob has its field: one ticked
        /// row, which is what a boolean's values are.
        void addParameterValueEntries(juce::PopupMenu &) override;

      private:
        ModuleUI &parent_;
    }; // class BypassButton

  private:
    /// \note First, and a reference: the widgets below are built in the
    /// constructor body and reach through it.
    SpectrumWorxEditor &editor_;

    /// The module this strip is for, kept alive for as long as the strip is.
    LE::Utility::IntrusivePtr<SW::Module> pModule_;

    BypassButton bypass_;
    EjectButton eject_;

    /// \note After the two buttons: the effect's own controls are children added
    /// in order, and `effectSpecificParameterControl()` indexes past `baseWidgets`
    /// of them. Destroyed before them, which is what the declaration order gives.
    std::unique_ptr<ModuleWidgets> pWidgets_;

    juce::String description_;

    std::uint8_t slot_{0};

  public:
    /// \note All std::uint16_t, including the four that fit in a byte. These are
    /// one coordinate system and horizontalOffset left the range of a
    /// std::uint8_t when the skin was rescaled to 845 x 564; a set of geometry
    /// constants where the type varies by how big the number happens to be is a
    /// trap the next rescale falls into.
    static std::uint16_t const horizontalOffset = 320;
    static std::uint16_t const verticalOffset = 14;
    static std::uint16_t const height = 537;
    static std::uint16_t const width = 102;
    static std::uint16_t const distance = 0;
    static std::uint16_t const border = 6;

    /// \brief What text keeps clear of a strip's left and right edges.
    /// \see issue #76, and paint(), which is the one thing wide enough to need it.
    static std::uint16_t const textMargin = 8;

    /// \brief The blue rule across the bottom of a strip: the effect's controls
    /// are above it and its name is below.
    ///
    /// \note Was `height - 30` written out in paint(), twice. It is a boundary two
    /// things now depend on -- what is drawn, and where the strip can be picked up
    /// -- so it is a name.
    static std::uint16_t const nameRule = height - 45;

    static std::uint8_t const baseWidgets = 2;

  private:
    static ModuleUI *pSelectedModule_;
}; // class ModuleUI

namespace Detail ///< \internal
{
////////////////////////////////////////////////////////////////////////////
///
/// \class ModuleWidgetConstructionState
///
////////////////////////////////////////////////////////////////////////////

struct ModuleWidgetConstructionState
{
  public:
    ModuleWidgetConstructionState(ModuleUI &parent);

    ModuleUI &parent;
    mutable std::uint16_t yOffset;
    mutable std::uint8_t parameterIndex;

  private:
    ModuleWidgetConstructionState(ModuleWidgetConstructionState const &);
    void operator=(ModuleWidgetConstructionState const &);
}; // struct ModuleWidgetConstructionState

template <class Widget> struct ModuleWidgetHolder
{
    ModuleWidgetHolder(ModuleWidgetConstructionState &);

    ModuleControlImpl<Widget> widget;
}; // struct ModuleWidgetHolder

/// \note Was `#ifdef __clang__ //...mrmlj...ambiguity compilation errors...`, and
/// the ambiguity is real rather than a Clang quirk. `WidgetsStorage` below folds
/// one base class per parameter onto the chain, so an effect with two parameters
/// of the same widget type — and most have several knobs — inherits
/// `ModuleWidgetHolder<ModuleKnob>` twice. Converting to a base that appears
/// twice is ambiguous, full stop; MSVC accepted it (hence the 4584 suppression
/// on WidgetsStorage) and resolved to whichever it saw first. Interposing a
/// holder keyed on the *parameter* makes every base distinct, which is the fix
/// for all three compilers rather than for one.
template <typename Parameter>
struct ParameterWidgetHolder : ModuleWidgetHolder<typename WidgetForParameter<Parameter>::type>
{
    ParameterWidgetHolder(ModuleWidgetConstructionState &state)
        : ModuleWidgetHolder<typename WidgetForParameter<Parameter>::type>(state)
    {
    }
};

template <typename Parameter> struct ParameterWidget
{
    typedef ParameterWidgetHolder<Parameter> type;
}; // struct ParameterWidget

////////////////////////////////////////////////////////////////////////////
///
/// \class WidgetInitialiser
///
////////////////////////////////////////////////////////////////////////////

struct WidgetInitialiser
{
    template <class Parameter, class Widget> static void setup(Widget const &) {}

    template <class Parameter> static void setup(ModuleControlImpl<DiscreteParameter> &comboBox)
    {
        fillComboBoxForParameter<Parameter>(comboBox);

        /// \note And onto the parameter's default, which the two fillers cannot
        /// do for themselves: one leaves the box on value zero and the other on
        /// its first power of two, both of which were the answer while zero was
        /// the only default an enumerated parameter could have. \see issue #163.
        comboBox.DiscreteParameter::setValue(Parameter::default_());
    }

    template <class Parameter> static void setup(ModuleControlImpl<ModuleKnob> &knob)
    {
        knob.setupForParameter(
            std::is_base_of<LE::Parameters::SymmetricParameterTag, typename Parameter::Tag>::value
                ? ModuleKnob::Bipolar
                : ModuleKnob::Unipolar,
            ModuleKnob::diameter, ModuleKnob::QuantizationFor<Parameter>::value,
            Parameter::discreteValueDistance);
    }
}; // struct WidgetInitialiser

////////////////////////////////////////////////////////////////////////////
///
/// \class EmptyWidgets
///
////////////////////////////////////////////////////////////////////////////

struct EmptyWidgets
{
    EmptyWidgets(ModuleWidgetConstructionState const &);
    static void setup(WidgetInitialiser const &) {}

    static void *operator new(std::size_t const count, void *LE_RESTRICT const pStorage)
    {
        (void)count;
        LE_ASSUME(pStorage);
        return pStorage;
    }
    static void operator delete(void *LE_RESTRICT const /*pObject*/,
                                void *LE_RESTRICT const /*pStorage*/)
    {
    }
}; // struct EmptyWidgets

////////////////////////////////////////////////////////////////////////////
///
/// \class WidgetsStorage
///
////////////////////////////////////////////////////////////////////////////

#pragma warning(push)
#pragma warning(disable : 4584) // base-class <> is already a base-class of WidgetsStorage
template <typename PreviousWidgets, typename Parameter>
struct WidgetsStorage : PreviousWidgets, ParameterWidget<Parameter>::type
{
    WidgetsStorage(ModuleWidgetConstructionState &state)
        : PreviousWidgets(state), ParameterWidget<Parameter>::type(state)
    {
    }

    void setup(WidgetInitialiser const &initialiser)
    {
        PreviousWidgets::setup(initialiser);
        initialiser.setup<Parameter>(ParameterWidget<Parameter>::type::widget);
    }
}; // struct WidgetsStorage
#pragma warning(pop)

////////////////////////////////////////////////////////////////////////////
///
/// \struct FoldWidgets
/// \internal
/// \brief One WidgetsStorage per parameter, each deriving from the last.
///
////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   Was boost::mpl::fold< Parameters, EmptyWidgets, WidgetsStorage<_1, _2> >
// over the Fusion-adapted parameter container. The placeholder expression is
// the only thing MPL was contributing; the traversal is a left fold over the
// indices the container already knows about.
////////////////////////////////////////////////////////////////////////////

template <class Accumulated, class Parameters, std::size_t index,
          bool done = (index == Parameters::static_size)>
struct FoldWidgets
{
    using type = typename FoldWidgets<
        WidgetsStorage<Accumulated, LE::Parameters::ParameterAt<Parameters, index>>, Parameters,
        index + 1>::type;
};

template <class Accumulated, class Parameters, std::size_t index>
struct FoldWidgets<Accumulated, Parameters, index, true>
{
    using type = Accumulated;
};
} // namespace Detail

////////////////////////////////////////////////////////////////////////////////
///
/// \class ParameterWidgets
///
////////////////////////////////////////////////////////////////////////////////

template <class ParametersParam> class ParameterWidgets
{
  public:
    typedef ParametersParam Parameters;

    using Container = typename Detail::FoldWidgets<Detail::EmptyWidgets, Parameters, 0>::type;

  public:
#ifndef NDEBUG
    ParameterWidgets() : constructed_(false) {}
    ~ParameterWidgets() { LE_ASSERT(!constructed_); }
#endif // NDEBUG

    void construct(ModuleUI &parent)
    {
        LE_ASSERT(!constructed_);
        doConstruct(parent);
        container().setup(Detail::WidgetInitialiser());
#ifndef NDEBUG
        constructed_ = true;
#endif // NDEBUG
    }

    void destroy()
    {
        LE_ASSERT(constructed_);
        container().~Container();
#ifndef NDEBUG
        constructed_ = false;
#endif // NDEBUG
    }

  private:
    void doConstruct(ModuleUI &parent)
    {
        Detail::ModuleWidgetConstructionState constructionState(parent);
        LE_ASSUME(&parameterWidgetsStorage_);
        Container *const pContainer(new (&parameterWidgetsStorage_) Container(constructionState));
        LE_ASSUME(pContainer);
    }

    Container &container()
    {
        Container *LE_RESTRICT const pContainer(
            &reinterpret_cast<Container &>(parameterWidgetsStorage_));
        LE_ASSUME(pContainer);
        return *pContainer;
    }

  private:
    typedef
        typename std::aligned_storage<sizeof(Container), std::alignment_of<Container>::value>::type
            ParameterWidgetsStorage;
    ParameterWidgetsStorage parameterWidgetsStorage_;

#ifndef NDEBUG
    bool constructed_;
#endif // NDEBUG
}; // class ParameterWidgets

template <> class ParameterWidgets<Effects::Detail::EmptyParameters>
{
  public:
    static void construct(ModuleUI &) {}
    static void destroy() {}
}; // class ParameterWidgets<EmptyParameters>

} // namespace GUI

} // namespace LE::SW

#endif // moduleUI_hpp
