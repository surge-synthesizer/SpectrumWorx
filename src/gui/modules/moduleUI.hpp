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
/// \namespace ModuleKnobStyle
///
////////////////////////////////////////////////////////////////////////////////

/// \brief Everything a module knob is drawn from: five colours and the radii and
/// angles they are laid out at.
///
///   Until 14.08.2026 a module knob was a film strip -- 127 square frames
/// stacked into one sheet (skin files 03, 12, 63 and 64), picked by
/// `126 * proportion` and blitted. That is four assets for what is one drawing
/// at two sizes and two polarities, it quantises the value to 127 steps, and it
/// is the reason the knob could not follow the editor's zoom: the frames are
/// pixels. This is the same drawing as paint calls, so it resolves at whatever
/// the graphics context is and every number below is in one place.
///
///   The shape, from the middle out: a black cap, then the value wedge in the
/// skin's blue out to `wedgeRadius`, then a dome that ramps from `innerGradient`
/// where the cap ends to `outerGradient` at the rim. The wedge opens clockwise
/// from `-halfSweepDegrees` for a unipolar parameter and from twelve o'clock for
/// a bipolar one, and the cap grows with it -- which is what the artwork did,
/// and what keeps the blue a band of roughly even thickness rather than a
/// lengthening spike.
///
/// \note Radii are fractions of the knob's own radius, so they hold at both the
/// 51 px module knob and the 23 px shared one. The rim and the focus halo are in
/// pixels instead: they are hairlines at both sizes rather than something that
/// scales with them.
namespace ModuleKnobStyle
{
std::uint32_t constexpr innerGradient{0xFFB8B6B6};     ///< the dome where the cap ends
std::uint32_t constexpr outerGradient{0xFF0A0909};     ///< the dome at its rim
std::uint32_t constexpr centreFill{0xFF000000};        ///< the cap over the wedge's inside
std::uint32_t constexpr wedge{0xFF13B7EA};             ///< the skin blue
std::uint32_t constexpr selectedOuterEdge{0xFFFFFFFF}; ///< the focused knob's halo

float constexpr innerGradientRadius{0.26f}; ///< the dome holds innerGradient in to here
float constexpr wedgeRadius{0.717f};
float constexpr capRadiusClosed{0.22f}; ///< with the wedge shut
float constexpr capRadiusOpen{0.48f};   ///< with it fully open

/// \note The wedge's travel is KnobStyle::halfSweepDegrees, shared with the
/// editor knob's pointer -- see the note there.

float constexpr rimThickness{1.0f};  ///< px
float constexpr selectionGlow{2.0f}; ///< px, either side of the rim
} // namespace ModuleKnobStyle

/// \brief Draws a module knob into the square \p bounds.
///
/// \param normalisedValue where the value sits in its range, 0 to 1; for a
/// \p bipolar knob 0.5 is the centre the wedge opens from.
/// \param drawWedge false while an LFO drives the parameter, when the knob's own
/// value says nothing -- what the ModuleKnobLFOed bitmap used to be.
/// \param selected whether it has the keyboard focus.
void paintModuleKnob(juce::Graphics &, juce::Rectangle<float> bounds, float normalisedValue,
                     bool bipolar, bool drawWedge, bool selected);

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
    static constexpr unsigned int diameter{51};
    static constexpr unsigned int smallDiameter{23};

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

    void paint(juce::Graphics &) override;

  private: // Knob's menu interface
    juce::String parameterName() const override;
    juce::String parameterValueText() const override;
    ParameterID parameterID() const override;
    bool parameterEditable() const override;
    bool setParameterFromText(juce::String const &) override;
    void setParameterToDefault() override;
    void addParameterMenuEntries(juce::PopupMenu &) override;

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
    static unsigned int const marginForGlow = 4;
    static unsigned int const spaceForText = 18;
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

  protected: // ModuleControl interface.
    void focusChanged() { repaint(); }

    // Implementation note:
    //   We allow a smooth LFO range control for boolean parameters.
    //                                        (21.07.2011.) (Domagoj Saric)
    static double valueRangeQuantum() { return 0; }

    using BitmapButton::getTextFromValue;
    char const *getValueText() const { return getTextFromValue(getValue()); }

  public:
    typedef BitmapButton BaseWidget;

  private:
    void clicked() override;
}; // class ModuleLEDTextButton

////////////////////////////////////////////////////////////////////////////////
///
/// \class TriggerButton
///
////////////////////////////////////////////////////////////////////////////////

class TriggerButton : public BitmapButton, public ModuleControl<TriggerButton>
{
  protected:
    TriggerButton(juce::Component &parent, unsigned int x, unsigned int y);

  public:
    value_type getValue() const { return isDown(); }
    void setValue(param_type);

  protected: // ModuleControl interface.
    void lfoStateChanged() { setValue(false); }
    void focusChanged() { repaint(); }

    // Implementation note:
    //   We allow a smooth LFO range control for boolean parameters.
    //                                        (21.07.2011.) (Domagoj Saric)
    static double valueRangeQuantum() { return 0; }

    using BitmapButton::getTextFromValue;
    char const *getValueText() const { return getTextFromValue(getValue()); }

  public:
    typedef TriggerButton BaseWidget;

  private: // juce::Component overrides
    void mouseDown(juce::MouseEvent const &) override;
    void mouseUp(juce::MouseEvent const &) noexcept override;

    void paintButton(juce::Graphics &, bool isMouseOverButton, bool isButtonDown) override;
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

  protected: // ModuleControl interface.
    void focusChanged();

    juce::String const &getTextFromValue(value_type const valueIndex) const
    {
        return getItemText(valueIndex);
    }
    juce::String const &getValueText() const { return getSelectedItemText(); }

    double valueRangeMaximum() const { return Math::convert<double>(numberOfItems() - 1); }

  public:
    typedef ComboBox BaseWidget;

  private:
    static unsigned int const horizontalMargin = 8;
    static unsigned int const textHeight = 11;
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

    /// \note Was `static ModuleUI *selectedModule()` over a file-scope pointer,
    /// with a 2011 note arguing that a static was safe "even if there are multiple
    /// effect editor instances open" because no two windows can have focus at
    /// once, and a `\todo Verify this on the Mac` under it. Focus is not the
    /// question: two instances shared one pointer, so the second editor to select
    /// a module silently deselected the first one's, and an editor closing left
    /// the other holding a pointer into freed storage. It is the editor's now.
    ///                                       (02.08.2026.) (SW port)
    bool selected() const;

  public:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief One module's strip: its controls, and where they are on screen.
    ///
    /// \note The module is held by *reference count*. A region has to outlive the
    /// chain dropping the module -- a host can empty a slot from its own panel
    /// while the window is open -- and the alternative was what used to be here:
    /// the module owned the region as a member, so the module could not be
    /// destroyed until the message thread had run `destroyGUI()`, which is why
    /// `intrusive_ptr_release_deleter` posted a message from whichever thread
    /// dropped the last reference. That thread can be the audio one.
    ///
    /// \note The editor is held rather than recovered from the component
    /// hierarchy: the constructor writes every parameter into the widgets before
    /// the region is parented.
    ///                                       (02.08.2026.) (SW port)
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

  private:
    /// \note First, and a reference: the widgets below are built in the
    /// constructor body and reach through it.
    SpectrumWorxEditor &editor_;

    /// The module this strip is for, kept alive for as long as the strip is.
    LE::Utility::IntrusivePtr<SW::Module> pModule_;

    BitmapButton bypass_;
    BitmapButton eject_;

    /// \note After the two buttons: the effect's own controls are children added
    /// in order, and `effectSpecificParameterControl()` indexes past `baseWidgets`
    /// of them. Destroyed before them, which is what the declaration order gives.
    std::unique_ptr<ModuleWidgets> pWidgets_;

    juce::String description_;

    std::uint8_t slot_{0};

  public:
    static std::uint8_t const horizontalOffset = 213;
    static std::uint8_t const verticalOffset = 9;
    static std::uint16_t const height = 358;
    static std::uint8_t const width = 68;
    static std::uint8_t const distance = 0;
    static std::uint8_t const border = 4;

    /// \brief What text keeps clear of a strip's left and right edges.
    /// \see issue #76, and paint(), which is the one thing wide enough to need it.
    static std::uint8_t const textMargin = 4;

    /// \brief The blue rule across the bottom of a strip: the effect's controls
    /// are above it and its name is below.
    ///
    /// \note Was `height - 30` written out in paint(), twice. It is a boundary two
    /// things now depend on -- what is drawn, and where the strip can be picked up
    /// -- so it is a name.
    static std::uint16_t const nameRule = height - 30;

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
///                                           (29.07.2026.) (SW port)
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
//                                        (30.07.2026.) (SW port)
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

/// \note `template <class Interface> struct ParameterWidgetsVTable` stood here:
/// a pair of function pointers, planted in every module at construction, that
/// built and destroyed that effect's widgets. It existed because the *module*
/// owned the widgets, so the only place that knew which effect's storage it was
/// holding was the module itself.
///
///   The region owns them now and picks the instantiation by effect index --
/// moduleWidgets.hpp -- so there is nothing left to plant.
///                                           (02.08.2026.) (SW port)

} // namespace GUI

} // namespace LE::SW

#endif // moduleUI_hpp
