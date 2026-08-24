////////////////////////////////////////////////////////////////////////////////
//
// ---
// GUI
// ---
//
//   The widget vocabulary the editor is built from, and the process-wide bits
// of it: the skin's lifetime, the palette, the paths and the message boxes.
//
// Copyright (c) 2009 - 2016. Little Endian Ltd.
// SPDX-License-Identifier: GPL-3.0-or-later
//
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef gui_hpp__3B545D5D_569F_4056_BEA8_159015782EC9
#define gui_hpp__3B545D5D_569F_4056_BEA8_159015782EC9
//------------------------------------------------------------------------------
#include "le/parameters/enumerated/tag.hpp"
#include "le/parameters/powerOfTwo/tag.hpp"
#include "le/utility/cstdint.hpp"
#include "le/utility/platformSpecifics.hpp"
#include "le/utility/rvalueReferences.hpp"
#include "le/utility/tchar.hpp"

#include <optional>
#include <string_view>
#include "le/utility/span.hpp"

#include "le/utility/intrusivePtr.hpp"

/// A knob's right button menu names the parameter it belongs to, and the host's
/// half of that menu is addressed by ID. \see Knob::parameterID().
#include "core/parameterID.hpp"

#include "painters/arrowPainter.hpp"
#include "painters/backgroundPainter.hpp"
#include "painters/buttonPainter.hpp"
#include "painters/capsulePainter.hpp"
#include "painters/ejectPainter.hpp"
#include "painters/editorKnobPainter.hpp"
#include "painters/framePainter.hpp"
#include "painters/glyphPainter.hpp"
#include "painters/knobPainter.hpp"
#include "painters/panelPainter.hpp"

#include "resources.hpp"
#include "theme.hpp"

/// `fs`, for the two path getters below. \see io/jucePath.hpp for the conversion
/// to and from JUCE's own file type, which is what the editor's edges want.
#include "filesystem/import.h"

/// \note Individual JUCE 8 headers have no include guards and open
/// `namespace juce {` mid-file; they may only be reached through the module
/// umbrella header.
#include <juce_gui_basics/juce_gui_basics.h>

#if defined(_WIN32)
#include "windows.h"
#elif defined(__APPLE__)
#ifdef __LP64__
typedef unsigned int ATSFontContainerRef;
#else
typedef unsigned long ATSFontContainerRef;
#endif // __LP64__
#endif // _WIN32

namespace LE
{

namespace Parameters
{
template <class Parameter> struct DiscreteValues;
template <class Parameter> struct Name;
/// \note Declared rather than included, as the two above are: the definition is
/// in uiElements.hpp and reaches this header's one caller
/// (fillComboBoxForParameter) through the translation unit that instantiates it.
template <class Parameter>
constexpr typename DiscreteValues<Parameter>::Strings const &shortValueStrings();
/// \note The same, for the order the rows come in. \see MenuOrder.
template <class Parameter> constexpr typename DiscreteValues<Parameter>::Order const &menuOrder();
} // namespace Parameters

namespace SW
{

namespace Engine
{
class Setup;
}
class SpectrumWorx;

namespace GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class SkinLifetime
///
/// \brief Keeps the skin alive for as long as at least one editor is.
///
///   The Theme owns two typefaces, the slider thumb and the default LookAndFeel
/// registration, and none of that may outlive the JUCE it was made under. So the
/// first editor builds it and the last one to go takes it down. A base class, and
/// the first one, so that it is constructed before the editor's widgets and
/// destroyed after them.
///
/// \note **JUCE's own lifetime is not this class's**, and must not be: the CLAP
/// shim holds a `ScopedJuceInitialiser_GUI` for the life of the plugin, and the
/// init counter, the shutdown list and the MessageManager singleton are one per
/// binary -- shared by every instance the host has loaded. Shutting JUCE down
/// from here would delete the message loop another instance is running on.
///
////////////////////////////////////////////////////////////////////////////////

class SkinLifetime
{
  protected:
    SkinLifetime();
    ~SkinLifetime();

    SkinLifetime(SkinLifetime const &) = delete; // makes non-copyable
    SkinLifetime &operator=(SkinLifetime const &) = delete;

  private:
    /// \note Editors are created and destroyed on the message thread, and only
    /// there, so a plain counter is enough -- the constructor asserts it.
    static std::uint8_t liveEditors_;
}; // class SkinLifetime

/// \note An Artwork rather than a juce::Image, and there is no juce::Image
/// overload on purpose: a widget holding a bitmap can only ever draw at the size
/// the skin was authored for, which is invisible at 100 % and soft once the
/// editor is zoomed.
void paintImage(juce::Graphics &, Artwork const &);
void paintImage(juce::Graphics &, Artwork const &, int x, int y);

void setSizeFromImage(juce::Component &, Artwork const &);

class SpectrumWorxEditor;

bool isThisTheGUIThread();
bool isGUIInitialised();

float displayScale();

////////////////////////////////////////////////////////////////////////////////
// Global paths.
////////////////////////////////////////////////////////////////////////////////

/// \note **`fs::path`, not `juce::File`**: `sst::plugininfra::paths` answers
/// with an `fs::path`, and converting it here is what put a mojibake Documents
/// folder on a `ja_JP.UTF-8` desktop. The platform's bytes are handed on
/// untouched, and whoever needs a `juce::String` converts at its own edge,
/// through io/jucePath.hpp. \see issue #28.

/// The user's SpectrumWorx folder: `~/Documents/SpectrumWorx` or the platform
/// equivalent. Nothing is created by asking.
fs::path const &rootPath();

/// \brief The browser's most-recently-used preset folder, which starts at the
/// user's preset directory and which the browser writes back when it closes.
/// \note Const: this is where the user's presets live, not where the browser last
/// was -- it anchors `goToParent()` and is what `createUserPresetsFolder()`
/// makes. Where the browser was is `PresetBrowser::Place`.
fs::path const &presetsFolder();

/// \brief Creates the user preset directory if it is not there.
/// \note Not done by presetsFolder(); see the note at the definition.
bool createUserPresetsFolder();

////////////////////////////////////////////////////////////////////////////////
/// \internal
/// \class WidgetBase
////////////////////////////////////////////////////////////////////////////////

namespace Detail
{
void setName(juce::Component &widget, juce::String const &newName);
void setName(juce::Component &widget, char const *const newName);

bool hasDirectFocus(juce::Component const &);
bool hasFocus(juce::Component const &);

bool isParentOf(juce::Component const &parent, juce::Component const &possibleChild);
bool isParentOf(juce::Component const &parent, juce::Component const *pPossibleChild);
} // namespace Detail

template <class BaseComponent = juce::Component> class WidgetBase : public BaseComponent
{
  protected:
    WidgetBase() : BaseComponent(juce::String()) {}
    LE_NOINLINE WidgetBase(char const *const componentName) : BaseComponent(juce::String())
    {
        setName(componentName);
    }
    WidgetBase(juce::String const &componentName) : BaseComponent(componentName) {}
    LE_FORCEINLINE ~WidgetBase() {}

  public:
    void setName(char const *const newName) { Detail::setName(*this, newName); }
    using BaseComponent::setName;

    void setVisible() { BaseComponent::setVisible(true); }
    void setInvisible() { BaseComponent::setVisible(false); }
    void setIsVisible(bool const isVisible) { BaseComponent::setVisible(isVisible); }

    void setEnabled(bool const isEnabled) { BaseComponent::setEnabled(isEnabled); }

    bool hasDirectFocus() const { return Detail::hasDirectFocus(*this); }
    bool hasFocus() const { return Detail::hasFocus(*this); }

    bool isParentOf(juce::Component const &control) const
    {
        return Detail::isParentOf(*this, control);
    }

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

    /// \note Required, not decorative: declaring the placement pair above puts
    /// `operator delete` in class scope, and lookup for the one the *deleting
    /// destructor* needs stops there rather than falling back to the global.
    /// Every widget here has a virtual destructor.
    void *operator new(std::size_t const count) { return ::operator new(count); }
    void operator delete(void *const pObject) { return ::operator delete(pObject); }

  private:
    using BaseComponent::isParentOf;
    using BaseComponent::setVisible;
}; // class WidgetBase

void warningMessageBox(std::string_view title, std::string_view message, bool canBlock);

////////////////////////////////////////////////////////////////////////////////
///
/// \class UnattendedLoad
///
/// \brief Marks a load nobody asked for -- a host restoring a session -- for as
/// long as one of these is alive.
///
/// \note It exists to be asserted against, and `warningMessageBox` is where.
/// Nothing may put a modal dialog in front of a user during a session restore:
/// they did not ask for the load, the answer they would have to give is about a
/// project they may not have finished opening, and a host that restores state
/// before showing any window has nowhere to put the box in the first place.
///
///   An assert rather than a refusal, because what is *missing* is a place for
/// such a message to go. `PresetProblem` is that place for everything the preset
/// layer runs into. \see issue #12.
///
////////////////////////////////////////////////////////////////////////////////

class UnattendedLoad
{
  public:
    UnattendedLoad();
    ~UnattendedLoad();

    UnattendedLoad(UnattendedLoad const &) = delete; // makes non-copyable

    /// \brief Whether any is in progress. `[main-thread]`
    static bool inProgress();
}; // class UnattendedLoad

/// \note The answer arrives in a callback and cannot arrive any other way: JUCE 8
/// defaults JUCE_MODAL_LOOPS_PERMITTED to 0, so showOkCancelBox returns at once.
void warningOkCancelBox(TCHAR const *title, TCHAR const *question,
                        std::function<void(bool)> onResult);

void addToParentAndShow(juce::Component &parent, juce::Component &childToBe);

void fadeOutComponent(juce::Component &, float finalAlpha, unsigned int duration,
                      bool useProxyComponent);

namespace Detail
{
template <class GUIHolder, class Functor>
class Message final : public juce::MessageManager::MessageBase
{
  public:
    Message(GUIHolder &guiHolder, Functor &&functor)
        : pGUIHolder_(&guiHolder), functor_(std::move(functor))
    {
    }

  private:
    void messageCallback() final
    {
        if (pGUIHolder_->gui())
            if (!functor_(*pGUIHolder_->gui()))
                this->post();
    }

  private:
    LE::Utility::IntrusivePtr<GUIHolder> const pGUIHolder_;
    Functor functor_;
}; // class Message

/// \note Message above reference-counts the object that owns the widget and
/// asks it for gui() at delivery time -- which is how a Module tracks its
/// ModuleUI. The editor has no such owner: the host's window holds it. So this
/// one tracks the widget itself through a SafePointer, which JUCE nulls when
/// the component is destroyed.
template <class Component, class Functor>
class MessageToComponent final : public juce::MessageManager::MessageBase
{
  public:
    MessageToComponent(Component &component, Functor &&functor)
        : pComponent_(&component), functor_(std::move(functor))
    {
    }

  private:
    void messageCallback() final
    {
        if (pComponent_)
            if (!functor_(*pComponent_))
                this->post();
    }

  private:
    juce::Component::SafePointer<Component> const pComponent_;
    Functor functor_;
}; // class MessageToComponent

template <class Functor> class MessageDirect final : public juce::MessageManager::MessageBase
{
  public:
    MessageDirect(Functor &&functor) : functor_(std::move(functor)) {}

  private:
    void messageCallback() final { functor_(); }

  private:
    Functor const functor_;
}; // class MessageDirect

inline void postMessage(juce::MessageManager::MessageBase *LE_RESTRICT const pMessage)
{
    if (pMessage)
        pMessage->post();
}
} // namespace Detail

template <class GUIHolder, class Functor> void postMessage(GUIHolder &guiHolder, Functor &&functor)
{
    Detail::postMessage(new (std::nothrow)
                            Detail::Message<GUIHolder, Functor>(guiHolder, std::move(functor)));
}

/// \note Same contract as postMessage(): the functor returning false means
/// "not yet", and the message re-posts itself.
template <class Component, class Functor>
void postMessageToComponent(Component &component, Functor &&functor)
{
    Detail::postMessage(new (std::nothrow) Detail::MessageToComponent<Component, Functor>(
        component, std::move(functor)));
}

template <class Functor> void postMessage(Functor &&functor)
{
    Detail::postMessage(new (std::nothrow) Detail::MessageDirect<Functor>(std::move(functor)));
}

template <class GUIHolder, class Functor>
void postOrExecuteMessage(GUIHolder &guiHolder, Functor &&functor)
{
    if (isThisTheGUIThread() && functor(*guiHolder.gui()))
        return;
    postMessage(/*std::forward<GUIHolder>*/ (guiHolder), std::move(functor));
}

////////////////////////////////////////////////////////////////////////////////
///
/// \class DrawableText
///
////////////////////////////////////////////////////////////////////////////////

/// \note Holds a GlyphArrangement rather than deriving from one, JUCE 8 marking
/// GlyphArrangement final.
class DrawableText
{
  public:
    DrawableText(char const *text, unsigned int x, unsigned int y, unsigned int width,
                 unsigned int height, juce::Justification = juce::Justification::centredLeft,
                 juce::Font const &font = defaultFont());

    void draw(juce::Graphics &graphics) const { glyphs_.draw(graphics); }

    static juce::Font defaultFont();

  private:
    juce::GlyphArrangement glyphs_;
}; // class DrawableText

////////////////////////////////////////////////////////////////////////////////
///
/// \namespace PointerFeedback
///
////////////////////////////////////////////////////////////////////////////////

/// \brief How much a button dims to say the pointer is on it, or that it cannot
/// be pressed at all.
///
/// \note Here rather than per button, so that every button in the editor dims by
/// the same amount and does so together.
namespace PointerFeedback
{
float constexpr normal{1.00f};
float constexpr over{0.88f};

/// As juce::LookAndFeel_V2::drawImageButton dimmed a disabled one.
float constexpr disabled{0.30f};
} // namespace PointerFeedback

////////////////////////////////////////////////////////////////////////////////
///
/// \class PaintedButton
///
////////////////////////////////////////////////////////////////////////////////

/// \brief A caption on a ButtonPainter pill: Presets and Settings, and the preset
/// browser's Save, Save as and Delete.
///
/// \note Held down *or* toggled on is what "selected" means here.
class PaintedButton : public WidgetBase<juce::Button>
{
  public:
    PaintedButton(juce::Component &parent, juce::String const &text, int width, int height,
                  bool toggled = true);

  private:
    void paintButton(juce::Graphics &, bool isMouseOverButton, bool isButtonDown) override;
}; // class PaintedButton

////////////////////////////////////////////////////////////////////////////////
///
/// \class PopupMenu
///
////////////////////////////////////////////////////////////////////////////////

/// \note The items live here and a juce::PopupMenu is built from them at the
/// moment of showing, so JUCE's layout stays JUCE's business and
/// getSelectedItemText() and getSelectedItemIcon() return references into
/// storage we own rather than into menu internals.
class PopupMenu
{
  public:
    PopupMenu(PopupMenu const &) = delete; // makes non-copyable
    PopupMenu &operator=(PopupMenu const &) = delete;

    using ItemID = std::uint32_t;
    using OptionalID = std::optional<ItemID>;
    /// Called on the message thread when the menu closes; no value if dismissed.
    using OnChosen = std::function<void(OptionalID)>;

  public:
    PopupMenu();
    ~PopupMenu() {}

    void addItem(ItemID, char const *newItemText, Artwork const *icon = nullptr,
                 bool enabled = true);
    /// \brief An entry the menu lists as \p newItemText and the widget showing
    /// the selection reads as \p shortText.
    ///
    /// \note The menu is as wide as its longest line and a module strip's combo
    /// box is sixty pixels, so the two cannot always be the same string. Every
    /// other reading of the value -- the host's, the knob menu's, a preset's --
    /// stays the full one. \see LE::Parameters::ShortValues.
    void addItem(ItemID, char const *newItemText, char const *shortText,
                 Artwork const *icon = nullptr, bool enabled = true);
    void addSubMenu(PopupMenu &, char const *name);
    void addSectionHeader(char const *title);
    void addSeparator();

    void clear();

    unsigned int numberOfItems() const;
    juce::String const &getItemText(unsigned int itemIndex) const;
    /// \brief The same entry as the widget reads it, which is its full text
    /// unless addItem() was given a shorter one.
    juce::String const &getItemShortText(unsigned int itemIndex) const;

    /// \note Asynchronous, JUCE 8 defaulting JUCE_MODAL_LOOPS_PERMITTED to 0.
    void showCenteredAtRight(juce::Component const &, OnChosen) const;
    void showCenteredBelow(juce::Component const &, OnChosen) const;
    /// \brief With its corner at \p screenPosition, which is what a right-click
    /// menu wants: it belongs to the point that was clicked, not to a widget.
    /// \p owner is what it is drawn beside all the same. \see showAt().
    void showAtScreenPosition(juce::Component const &owner, juce::Point<int> screenPosition,
                              OnChosen) const;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief True from the moment *this* menu is shown until its callback has
    /// run.
    ///
    /// \note Per menu rather than per process: all three readers ask about a menu
    /// they own -- a combo box's, the LFO type's, the sample area's -- and a
    /// shared flag would let one editor's open menu silence another's button.
    /// `mutable` because showing a menu changes nothing about what it *is*.
    ///
    ////////////////////////////////////////////////////////////////////////////
    bool menuActive() const { return menuActive_; }

  protected:
    struct Item
    {
        ItemID id;
        juce::String text;
        /// What a widget showing this selection reads; equal to `text` unless a
        /// shorter one was given. \see addItem().
        juce::String shortText;
        Artwork const *icon;
        bool enabled;
        bool isSectionHeader;
        bool isSeparator;
        /// Non-owning: a sub-menu is a member of the owning widget and outlives
        /// the menu.
        PopupMenu const *pSubMenu;
    }; // struct Item

    std::vector<Item> const &items() const { return items_; }

  private:
    void updateDimensionsForNewItem(juce::String const &itemText);

    /// Builds the JUCE menu from items_, ticking \p tickedIndex if in range.
    juce::PopupMenu build(int tickedIndex) const;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Shows the menu beside \p area, which is in \p owner's coordinates.
    ///
    /// \note \p owner is not there only for the geometry. A menu is its own
    /// desktop window and inherits nothing from what opened it: JUCE works its
    /// scale out from the component the menu names as its target, so a menu that
    /// names none is drawn at 1:1 beside an editor drawn at the user's zoom.
    ///
    /// \note And the area is in the owner's coordinates rather than the screen's
    /// for the same reason: every offset here is a skin pixel, and the screen
    /// stopped being measured in those the moment the editor took a zoom.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void showAt(juce::Component const &owner, juce::Rectangle<int> area, OnChosen) const;

  protected:
    /// Which entry build() should tick. PopupMenuWithSelection drives it; a
    /// plain PopupMenu leaves it at -1.
    int tickedIndex_{-1};

  private:
    std::vector<Item> items_;

    unsigned short menuHeight_;
    unsigned short menuWidth_;

    mutable bool menuActive_{false};
}; // class PopupMenu

////////////////////////////////////////////////////////////////////////////////
///
/// \class PopupMenuWithSelection
///
////////////////////////////////////////////////////////////////////////////////

class PopupMenuWithSelection : public PopupMenu
{
  public:
    /// True if the selection changed.
    using OnSelection = std::function<void(bool)>;

  public:
    PopupMenuWithSelection();

    unsigned int getSelectedID() const;
    void setSelectedID(unsigned int newSelectionID);

    unsigned int getSelectedIndex() const;
    void setSelectedIndex(unsigned int newSelectionIndex);

    juce::String const &getSelectedItemText() const;
    /// \brief The selection as the widget reads it. \see getItemShortText().
    juce::String const &getSelectedItemShortText() const;
    Artwork const *getSelectedItemIcon() const;

    void showCenteredAtRight(juce::Component const &, OnSelection);
    void showCenteredBelow(juce::Component const &, OnSelection);

    void clear();

    bool hasValidSelection() const;

  private:
    bool handleNewSelection(OptionalID const &chosenMenuEntryID);
    void updateSelection(unsigned int newSelectionIndex);
    unsigned int indexForID(unsigned int id) const;

  private:
    int currentSelection_;
    /// 0 means "nothing chosen yet"; a real ID is stored as id + 1 so that 0 is
    /// a legal item ID, juce::PopupMenu reserving it for "dismissed".
    unsigned int currentSelectionID_;
}; // class PopupMenuWithSelection

////////////////////////////////////////////////////////////////////////////////
///
/// \name The two combo boxes
///
///   The same rounded box at two sizes, its hairline blue at rest and white when
/// the box has the keyboard focus.
///
/// \note Two styles rather than one: the two sizes disagree about their corner
/// radius by a pixel and a third and about their insets by half a pixel, and
/// averaging them would move two controls to no purpose.
///
////////////////////////////////////////////////////////////////////////////////
///@{
/// The widget each is drawn in, which was its artwork's size.
int constexpr moduleComboWidth{90};
int constexpr moduleComboHeight{27};
int constexpr settingsComboWidth{225};
int constexpr settingsComboHeight{32};

FrameStyle constexpr moduleComboFrame{
    /* insets */ 2.595f,
    3.345f,
    2.175f,
    /* corner */ 8.46f,
    /* rim    */ RuleStyle::thickness,
    /* halo   */ 5u,
    0.0242f,
    0.0120f,
};

FrameStyle constexpr settingsComboFrame{
    /* insets */ 3.0f,
    4.41f,
    5.16f,
    /* corner */ 10.395f,
    /* rim    */ RuleStyle::thickness,
    /* halo   */ 5u,
    0.0197f,
    0.0147f,
};
///@}

////////////////////////////////////////////////////////////////////////////////
///
/// \class ComboBox
///
////////////////////////////////////////////////////////////////////////////////

class ComboBox : public WidgetBase<>, public PopupMenuWithSelection
{
  public: // ModuleUI control traits
    typedef unsigned int value_type;
    typedef value_type param_type;

    static value_type valueRangeMinimum() { return 0; }
    value_type valueRangeMaximum() const { return numberOfItems(); }
    static value_type valueRangeQuantum() { return 1; }

    value_type getValue() const { return getSelectedID(); }
    void setValue(param_type const newValue) { setSelectedID(newValue); }

  public:
    /// What the selected item's text keeps clear of the rounded background's
    /// ends. \see paint().
    static int constexpr textMargin{9};

    void setSelectedID(unsigned int newSelectionID);
    void setSelectedIndex(unsigned int newSelectionIndex);

  protected:
    /// \note \p height is the box's own, which for a TitledComboBox is not the
    /// widget's: that one is fifteen pixels taller and paints its title in the
    /// difference.
    ComboBox(juce::Component &parent, FrameStyle const &, int width, int height);

    void showMenu(std::function<void(bool)> onValueChanged);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief What a wheel notch has just done, for whoever the selection
    /// belongs to.
    ///
    ///   Called after the row has already moved, and only when it did. Doing
    /// nothing is the right default: a box nobody has wired to a parameter
    /// changes its own display and nothing else, which is what it does when the
    /// menu is used too.
    ///
    /// \note The same seam `showMenu()`'s callback is, in the shape a wheel
    /// needs: nothing here outlives the widget, so it is a virtual rather than a
    /// SafePointer and a std::function.
    ///
    ////////////////////////////////////////////////////////////////////////////

    virtual void selectionScrolled() {}

    /// \brief Whether the box draws as the selected one -- the keyboard, unless
    /// whoever owns it has another answer. \see DiscreteParameter.
    virtual bool showsAsSelected() const { return hasDirectFocus(); }

  protected: // juce::Component overrides
    void paint(juce::Graphics &) override;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Steps the selection: **a notch away from the user is the next row
    /// down the list**, which is the *later* value.
    ///
    /// \note Deliberately not a list's convention, where scrolling away moves
    /// toward the top. A module strip's box is a parameter editor in a row of
    /// them with knobs either side, and away from the user raises a knob.
    ///
    ////////////////////////////////////////////////////////////////////////////

    void mouseWheelMove(juce::MouseEvent const &, juce::MouseWheelDetails const &) override;

  private:
    /// The row \p rows away from the selected one that a user could have picked
    /// off the menu, or the selected one if there is none.
    unsigned int rowReachedBy(int rows) const;

    /// Whether \p row is one a click could land on.
    bool isSelectableRow(unsigned int row) const;

  private:
    FrameStyle const &frame_;
    int const boxHeight_;

    /// \note Accumulated because a trackpad sends a great many small deltas
    /// where a wheel sends one large one, and a row per delta would make the
    /// first unusable. juce::ComboBox carries the same member for the same
    /// reason.
    float wheelTravel_{0};
}; // class ComboBox

////////////////////////////////////////////////////////////////////////////////
///
/// \class PanelBackground
///
////////////////////////////////////////////////////////////////////////////////

/// \brief A widget whose whole job is the panel behind everything on it.
///
/// \note There are only two, and both are drawn rather than blitted.
/// \see panelPainter.hpp.
class PanelBackground : public WidgetBase<>
{
  public:
    enum Which
    {
        Browser,     ///< the preset browser, rounded on all four corners
        SettingsPage ///< a settings page, square where its tabs meet it
    };

  protected:
    PanelBackground(Which const which) : which_(which) {}

    /// The size this panel is drawn at, which the caller usually wants too.
    void setSizeFromPanel();

  protected: // juce::Component overrides
    void paint(juce::Graphics &) override;

  private:
    Which const which_;
}; // class PanelBackground

////////////////////////////////////////////////////////////////////////////////
///
/// \class LEDTextButton
///
////////////////////////////////////////////////////////////////////////////////

/// \brief A capsule that lights up, and nothing else.
///
/// \note The control traits below are how a module strip's bypass reports a
/// boolean. \see CapsulePainter for the drawing.
class CapsuleButton : public WidgetBase<juce::Button>
{
  public: // ModuleUI control traits
    typedef bool value_type;
    typedef value_type param_type;

    static value_type valueRangeMinimum() { return false; }
    static value_type valueRangeMaximum() { return true; }
    static value_type valueRangeQuantum() { return true - false; }

    value_type getValue() const { return getToggleState(); }
    void setValue(param_type const newValue)
    {
        setToggleState(newValue, juce::dontSendNotification);
    }

  public:
    /// \param litWhenOn false where the toggle state is the *opposite* of what
    /// the capsule shows. A module strip's bypass is the one: its state is
    /// "bypassed" and its capsule says "running".
    CapsuleButton(juce::Component &parent, CapsuleStyle const &, int width, int height,
                  bool litWhenOn = true);

  protected:
    /// \brief Draws this button's capsule into \p bounds, at whatever the mouse
    /// has made of its opacity.
    void paintCapsule(juce::Graphics &, juce::Rectangle<int> bounds, bool isMouseOverButton,
                      bool isButtonDown);

  protected: // juce::Component overrides
    void paintButton(juce::Graphics &, bool isMouseOverButton, bool isButtonDown) override;

  private:
    CapsuleStyle const *pStyle_;
    bool litWhenOn_;

    using juce::Button::getToggleStateValue;
}; // class CapsuleButton

////////////////////////////////////////////////////////////////////////////////
///
/// \class ArrowButton
///
////////////////////////////////////////////////////////////////////////////////

/// \brief A triangle pointing right, which is three of the editor's buttons:
/// the one that adds an effect, and the two that step to the next of something.
///
/// \note No second state, only a tint under the pointer. \see ArrowPainter.
class ArrowButton : public WidgetBase<juce::Button>
{
  public:
    ArrowButton(juce::Component &parent, int width, int height, bool fadeFromBase,
                ColourMap::Name tintWhenOver);

  protected:
    /// \brief The triangle and its pointer tint in \p bounds rather than filling
    /// the widget, for a button that is more than the arrow drawn on it.
    void paintArrow(juce::Graphics &, juce::Rectangle<float> bounds, bool isMouseOver);

  private: // juce::Component overrides
    void paintButton(juce::Graphics &, bool isMouseOverButton, bool isButtonDown) override;

  private:
    bool const fadeFromBase_;
    /// \note The name rather than the colour, so that the tint follows the
    /// palette. A widget that holds a juce::Colour holds the palette that was
    /// current when it was built. \see ColourMap::setPalette().
    ColourMap::Name const tintWhenOver_;
}; // class ArrowButton

////////////////////////////////////////////////////////////////////////////////
///
/// \class EjectButton
///
////////////////////////////////////////////////////////////////////////////////

/// \brief The tongue at the top of a module strip that takes the effect out.
class EjectButton : public WidgetBase<juce::Button>
{
  public:
    EjectButton(juce::Component &parent);

  private: // juce::Component overrides
    void paintButton(juce::Graphics &, bool isMouseOverButton, bool isButtonDown) override;
}; // class EjectButton

////////////////////////////////////////////////////////////////////////////////
///
/// \class GlyphButton
///
////////////////////////////////////////////////////////////////////////////////

/// \brief A mark that stands for a word, and says what it is by its colour.
///
///   The preset browser's navigation row: up a folder, the user's own presets,
/// and the two halves of the preset jog. \see GlyphPainter for the drawings and
/// for the sizes, which are each glyph's own.
///
/// \note White when off and the accent when on, so a toggling glyph needs nothing
/// said about it at the call site and one that does not toggle is white for its
/// whole life. Disabled and moused-over are PointerFeedback's.
class GlyphButton : public WidgetBase<juce::Button>
{
  public:
    enum struct Glyph
    {
        FolderUp,
        User,
        JogPrevious,
        JogNext,
        /// \note Not in that row: this one is the editor's, beside the
        /// sidechain source. \see BackgroundPainter::sideChainLockBounds().
        Lock
    };

    /// \param toggles false for the three that are actions rather than states.
    GlyphButton(juce::Component &parent, Glyph, bool toggles = false);

  private: // juce::Component overrides
    void paintButton(juce::Graphics &, bool isMouseOverButton, bool isButtonDown) override;

  private:
    Glyph const glyph_;
}; // class GlyphButton

/// \brief One of those with a caption beside it.
class LEDTextButton : public CapsuleButton
{
  public:
    LEDTextButton(juce::Component &parent, unsigned int x, unsigned int y, char const *text);

    /// What the capsule takes out of the widget, the caption having the rest.
    static int constexpr ledWidth{38};
    static int constexpr ledHeight{21};

  protected: // juce::Component overrides
    void paintButton(juce::Graphics &, bool isMouseOverButton, bool isButtonDown) override;
}; // class LEDTextButton

////////////////////////////////////////////////////////////////////////////////
///
/// \class TextButton
///
////////////////////////////////////////////////////////////////////////////////

class TextButton : public WidgetBase<juce::Button>
{
  public:
    TextButton(juce::Component &parent, unsigned int x, unsigned int y, char const *text);

  private:
    void paintButton(juce::Graphics &, bool isMouseOverButton, bool isButtonDown) override;

    static unsigned int const height = 17;
}; // class TextButton

////////////////////////////////////////////////////////////////////////////////
///
/// \name Round controls, and the strip behind them
///
///   A module knob and a trigger button are both circles, drawn in a rectangle
/// with room for a caption underneath. Everything in that rectangle which is not
/// the circle reads as the module strip showing through, so a right press there
/// is a question for the strip -- "replace this effect" -- and not for the
/// control.
///
////////////////////////////////////////////////////////////////////////////////
///@{

/// \brief Whether \p position is inside the circle inscribed in \p face, both in
/// the widget's own coordinates.
bool isOnRoundFace(juce::Rectangle<int> face, juce::Point<int> position);

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Hands \p event to \p widget's parent, in the parent's coordinates.
///
/// \note The parent is asked directly rather than by making the widget
/// transparent to the mouse: which button is down is not something `hitTest()`
/// can see, and a control that let the left button through could not be operated
/// at all.
///
/// \note A parent with nothing to say about the press does nothing with it --
/// the panel the shared gain and wet pair stand on is not a module strip and
/// offers no menu -- which is also what pressing that panel itself does.
///
////////////////////////////////////////////////////////////////////////////////

void passMousePressToParent(juce::Component &widget, juce::MouseEvent const &event);
///@}

////////////////////////////////////////////////////////////////////////////////
///
/// \class ParameterMenu
///
/// \brief The right button's menu on a widget that stands for a parameter.
///
///   The parameter's name, somewhere to type a value, the way back to the
/// default, whatever the widget itself adds, and then whatever the host has to
/// add -- which is the menu the rest of the Surge Synth Team's plugins put on a
/// parameter.
///
/// \note A mix-in rather than something Knob owns: a knob is not the only widget
/// standing for an automatable parameter. Tune Worx has none at all -- a combo
/// box and twelve LEDs.
///
/// \note Every question below is virtual rather than a constructor argument
/// because each is one only the concrete widget can answer, and two of them --
/// the value and whether it may be edited -- change while the widget lives.
///
////////////////////////////////////////////////////////////////////////////////

class ParameterMenu
{
  public:
    /// \brief Raises the menu at \p event, over the widget this is mixed into.
    ///
    /// \note Public because the widget that raises it is not always the class
    /// that answers for it: Knob::mouseDown() reaches whichever of its two
    /// subclasses carries the parameter.
    void showParameterMenu(juce::MouseEvent const &, bool const skipSetToDefault = false);

    ////////////////////////////////////////////////////////////////////////////
    /// \brief Whether the widget's own value is worth editing at all.
    ///
    /// \note False while an LFO drives the parameter: what is heard then is the
    /// LFO's output and the widget's value is overwritten from under any edit.
    /// The gestures that could already move the widget are blocked on the same
    /// question, so the type-in and the default follow them rather than becoming
    /// another answer.
    ///
    /// \note Public because a knob asks it about itself for a second reason:
    /// whether there is a drag worth hiding the cursor for.
    /// \see Knob::hidesCursorWhileDragging().
    ////////////////////////////////////////////////////////////////////////////
    virtual bool parameterEditable() const { return true; }

  protected:
    ~ParameterMenu() = default;

    /// \brief The widget, for JUCE. This is a mix-in and not a Component of its
    /// own, and a menu needs one to hang off, to place itself against and to
    /// hand the keyboard back to.
    virtual juce::Component &menuOwner() = 0;

    /// The section header: what this parameter is called.
    virtual juce::String parameterName() const = 0;
    /// What the type-in field starts out holding, unit and all.
    virtual juce::String parameterValueText() const = 0;
    /// Which parameter this is, for the host's own entries.
    virtual ParameterID parameterID() const = 0;

    ////////////////////////////////////////////////////////////////////////////
    /// \brief Whether typing a value into it means anything.
    ///
    /// \note False for a trigger, which is an event rather than a number: there
    /// is no text that says "fire". Everything else in this plugin has a
    /// reading, enumerated parameters included -- their value strings are what
    /// `Parameters::parse` reads.
    ////////////////////////////////////////////////////////////////////////////
    virtual bool parameterAcceptsText() const { return true; }

    /// \return false when \p text is not a value this parameter can hold, which
    /// leaves the parameter where it was. \see LE::Parameters::parse().
    virtual bool setParameterFromText(juce::String const &text) = 0;
    virtual void setParameterToDefault() = 0;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The parameter's *values*, where a knob has its type-in field.
    ///
    ///   An enumerated parameter is chosen rather than typed, and what a user
    /// right-clicking a combo box wants is the list they would otherwise have to
    /// left-click for -- so the menu reads name, values, then whatever the host
    /// adds. \see DiscreteParameter::addParameterValueEntries().
    ///
    /// \note Empty for everything else. A knob's values are a continuum and a
    /// button's are two, one of which is a press away.
    ///
    ////////////////////////////////////////////////////////////////////////////

    virtual void addParameterValueEntries(juce::PopupMenu &) {}

    /// Entries of the widget's own, under "Set to Default": a module control's
    /// LFO switch.
    virtual void addParameterMenuEntries(juce::PopupMenu &) {}

  private:
    /// The type-in field, as a menu item. \see gui.cpp.
    class ValueTypein;
}; // class ParameterMenu

/// \brief Shift-refines a drag by feeding juce::Slider a position rather than a
/// new sensitivity -- rescaling mid-drag would rescale the travel already made
/// and jump the value.
class FineDrag
{
  public:
    /// \brief What shift divides the travel by.
    ///
    /// \note Shift because that is the key the rest of the Surge Synth Team's
    /// plugins use; command is not free, sst-jucegui quantizing a drag to the
    /// parameter's step with it.
    ///
    /// \note Four rather than sst-jucegui's ten, this dividing an already brisk
    /// 600 px: ten would put a fine sweep past even referenceDragPixels.
    static constexpr float ratio{4};

    /// Kills the velocity drag command, control and alt otherwise swap into.
    static void keepDragLinear(juce::Slider &);

    void begin(float anchor) noexcept;

    /// \p position scaled by the ratio each segment was covered at.
    float adjust(float position, bool fine) noexcept;

  private:
    float start_{0};
    float last_{0};
    float travel_{0};
}; // class FineDrag

/// \p event with alt standing in for the shift juce::Slider reads as "link a
/// two-value slider's thumbs", and shift cleared so it only ever means fine.
juce::MouseEvent linkThumbsOnAlt(juce::MouseEvent const &);

/// The above, plus the horizontal position \p drag has earned so far.
juce::MouseEvent refinedDrag(FineDrag &drag, juce::MouseEvent const &);

/// \brief A slider that drags like a knob: shift refines, alt links a two-value
/// slider's thumbs, and no modifier means velocity. \see issue #167.
class HorizontalSlider : public juce::Slider
{
  public:
    HorizontalSlider();

  protected:
    void mouseDown(juce::MouseEvent const &) override;
    void mouseDrag(juce::MouseEvent const &) override;

  private:
    FineDrag fine_;
}; // class HorizontalSlider

////////////////////////////////////////////////////////////////////////////////
///
/// \class Knob
///
////////////////////////////////////////////////////////////////////////////////

class Knob : public WidgetBase<juce::Slider>
{
  public:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Whichever subclass carries this knob's parameter.
    ///
    /// \note A hook rather than a base, because the two subclasses reach a
    /// ParameterMenu by two different routes: the editor's own knobs answer for
    /// themselves, and a module's answers through the ModuleControlBase every
    /// widget on a strip shares. One ParameterMenu subobject either way, which
    /// inheriting it here would not give.
    ///
    ////////////////////////////////////////////////////////////////////////////
    virtual ParameterMenu &parameterMenu() = 0;
    ParameterMenu const &parameterMenu() const { return const_cast<Knob &>(*this).parameterMenu(); }

  public:
    typedef double value_type;
    typedef float param_type;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \name How fast a knob follows the mouse
    ///
    /// \note One number to turn, and it is deliberately not a preference: the
    /// question these answer is whether the plugin tracks the mouse the way the
    /// rest of the platform does, which is not something to ask a user. If a
    /// platform wants a different answer, this is where it says so.
    ///
    ////////////////////////////////////////////////////////////////////////////
    ///@{
    /// \brief The vertical travel a whole range took in the 2.x plugin, kept as
    /// the reference so the sensitivity below reads as a multiple of that feel.
    static constexpr float referenceDragPixels{1200};

    /// \brief The multiple of that travel a knob covers. One is the 2.x feel, at
    /// which a 16" laptop screen is not tall enough for a whole range.
    static constexpr float dragSensitivity{2};

    /// \brief The travel a whole range takes, as JUCE counts it: 600 px, and
    /// FineDrag::ratio times that with shift held.
    static constexpr int coarseDragPixels()
    {
        return static_cast<int>(referenceDragPixels / dragSensitivity);
    }
    ///@}

    value_type getValue() const { return static_cast<value_type>(juce::Slider::getValue()); }
    void setValue(param_type);

    param_type getNormalisedValue() const;

    /// \note \p diameter, a knob that paints itself having no film strip to
    /// measure off.
    void setupForParameter(char const *title, unsigned int diameter, param_type defaultValue);

  protected:
    Knob(juce::Component &parent, unsigned int x, unsigned int y, unsigned int xMargin,
         unsigned int yMargin);

  public:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Whether \p position, in this widget's coordinates, is on the knob
    /// itself rather than on the space around it.
    ///
    ///   A right press that is not gets handed to the parent instead of raising
    /// the parameter menu: a module knob is a circle in a rectangle with its
    /// caption underneath, and the caption reads as part of the strip behind it.
    /// ModuleKnob::isOnKnobFace() is the only override.
    ///
    /// \note The default is "all of it", which is what the editor's three main
    /// knobs want: they have no margin, nothing behind them offers a menu, and
    /// the four corners outside their circle are as much part of the knob as the
    /// middle is.
    ///
    /// \note Public for the same reason ModuleUI::isDragHandle() is: it is the
    /// only part of this a test can put a number on.
    ///
    ////////////////////////////////////////////////////////////////////////////
    virtual bool isOnKnobFace(juce::Point<int> position) const;

  public:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Whether pressing this knob hands the mouse to the knob -- cursor
    /// hidden, movement unbounded -- for the duration of the drag.
    ///
    /// \note Two questions: the preference is the user's, and
    /// `parameterEditable()` is whether there is a drag to hand the mouse over
    /// *for*. An LFO'd knob answers no to the second -- `ModuleKnob::mouseDrag`
    /// returns at the top -- and hiding the cursor for a gesture that cannot move
    /// anything leaves JUCE to put it back inside the knob's bounds on release
    /// rather than where the user pressed.
    ///
    /// \note Public and const because it is the only part of this a test can
    /// reach: JUCE gates the mode on a real button being down, so a synthesised
    /// press never turns it on.
    ///
    ////////////////////////////////////////////////////////////////////////////
    bool hidesCursorWhileDragging() const;

  protected: // juce::Component overrides
    ////////////////////////////////////////////////////////////////////////////
    /// \note All three, and not only the press. juce::Slider::mouseDown is what
    /// clears `useDragEvents`, so returning from it early -- which is what
    /// showing our own menu instead does -- would leave the flag set from the
    /// last real drag and let a right-drag move the value.
    ////////////////////////////////////////////////////////////////////////////
    void mouseDown(juce::MouseEvent const &) override;
    void mouseDrag(juce::MouseEvent const &) override;
    void mouseUp(juce::MouseEvent const &) override;

    ////////////////////////////////////////////////////////////////////////////
    /// \note Swallowed rather than forwarded, so that shift may be pressed in
    /// the middle of a drag. \see fineAdjusted().
    ////////////////////////////////////////////////////////////////////////////
    void modifierKeysChanged(juce::ModifierKeys const &) override;

    void startedDragging() noexcept override;
    void stoppedDragging() noexcept override;

  private:
    /// \brief \p event with the vertical position shift has earned so far.
    ///
    /// \note Vertical only, because the style is RotaryVerticalDrag and
    /// `handleAbsoluteDrag` reads nothing else.
    juce::MouseEvent fineAdjusted(juce::MouseEvent const &event);

  private:
    FineDrag fine_;

  private:
    using juce::Slider::getMaxValueObject;
    using juce::Slider::getMinValueObject;
    using juce::Slider::getValueObject;
}; // class Knob

////////////////////////////////////////////////////////////////////////////////
///
/// \class EditorKnob
///
////////////////////////////////////////////////////////////////////////////////

class EditorKnob final : public Knob, public ParameterMenu
{
  public:
    static constexpr unsigned int diameter{83};

    EditorKnob(SpectrumWorxEditor &parent, unsigned int x, unsigned int y);

    void setupForParameter(std::uint8_t parameterIndex, param_type minimumValue,
                           param_type maximumValue, param_type defaultValue);

  private: // juce::Component overrides
    void paint(juce::Graphics &) override;
    void valueChanged() noexcept override;

    void startedDragging() noexcept override;
    void stoppedDragging() noexcept override;

  private: // Knob
    ParameterMenu &parameterMenu() override { return *this; }

  private: // ParameterMenu
    juce::Component &menuOwner() override { return *this; }
    juce::String parameterName() const override;
    juce::String parameterValueText() const override;
    ParameterID parameterID() const override;
    bool setParameterFromText(juce::String const &) override;
    void setParameterToDefault() override;

    /// \brief A value from somewhere other than a drag, bracketed in a gesture
    /// of its own so that a host records it as one edit rather than as a jump.
    void setParameterValue(double newValue);

  private:
    /// \note const, and still handing back a non-const editor: fromChild() takes
    /// a `Component const &` and answers the editor it belongs to, and the const
    /// half of the menu interface above has to be able to ask.
    SpectrumWorxEditor &editor() const;

  private:
    std::uint8_t parameterIndex_;
}; // class EditorKnob

////////////////////////////////////////////////////////////////////////////////
///
/// \class TitledComboBox
///
////////////////////////////////////////////////////////////////////////////////

class TitledComboBox : public ComboBox
{
  public:
    TitledComboBox(juce::Component &parent, unsigned int x, unsigned int y, char const *title);

  private:
    void mouseDown(juce::MouseEvent const &) override;
    void paint(juce::Graphics &) override;

    /// \note The same call the menu's own callback makes.
    void selectionScrolled() override;

  private:
    DrawableText const title_;
}; // class TitledComboBox

// Implementation note:
//   The following fillComboBoxForParameter<>() implementation supports only
// enumerated and power-of-two parameters and uses their internal knowledge
// (that they do not use a DisplayValueTransformer and how they are printed,
// a simple 'lexical_cast' for power-of-two parameters or a direct fetch of
// a string from the DiscreteValues<>::strings[] array) in order to slightly
// reduce compile time and runtime overhead. In case it becomes needed again, a
// generic solution was used up to revision 4636.
//                                            (15.07.2011.) (Domagoj Saric)

namespace Detail
{
void addPowerOfTwoValueStringsToComboBox(unsigned int firstValue, unsigned int lastValue,
                                         ComboBox &comboBox);

/// \note Not LE_RESTRICT const: DiscreteValues::strings is a std::array now, and
/// a restrict-qualified element type is not something a template argument can
/// carry.
///
/// \note Two lists of the same length: what the menu lists each value under and
/// what the box reads once it is chosen. They are the same array for every
/// parameter that has not been given abbreviations.
///
/// \note Both are indexed by *value*, and \p menuOrder says which value each row
/// holds -- declaration order for almost every parameter, and its own list for
/// the ones given a MenuOrder. An item carries its value as its ID, so nothing
/// downstream reads a row number.
void addEnumeratedParameterValueStringsToComboBox(LE::Utility::Span<char const *const> strings,
                                                  LE::Utility::Span<char const *const> shortStrings,
                                                  LE::Utility::Span<std::uint8_t const> menuOrder,
                                                  ComboBox &comboBox);

template <class Parameter>
void fillComboBoxForParameter(ComboBox &comboBox, LE::Parameters::PowerOfTwoParameterTag)
{
    addPowerOfTwoValueStringsToComboBox(Parameter::minimum(), Parameter::maximum(), comboBox);
}

template <class Parameter>
void fillComboBoxForParameter(ComboBox &comboBox, LE::Parameters::EnumeratedParameterTag)
{
    addEnumeratedParameterValueStringsToComboBox(
        LE::Utility::makeSpan(LE::Parameters::DiscreteValues<Parameter>::strings),
        LE::Utility::makeSpan(LE::Parameters::shortValueStrings<Parameter>()),
        LE::Utility::makeSpan(LE::Parameters::menuOrder<Parameter>()), comboBox);
}
} // namespace Detail

template <class Parameter> void fillComboBoxForParameter(ComboBox &comboBox)
{
    Detail::fillComboBoxForParameter<Parameter>(comboBox, typename Parameter::Tag());
}

////////////////////////////////////////////////////////////////////////////////
///
/// \class DiscreteParameterComboBox
///
///   A wrapper for the TitledComboBox widget class that helps reduce verbosity/
/// boiler plate code by automatically calling name<Parameter>() and
/// fillComboBoxForParameter<Parameter>.
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//  It contains a TitledComboBox instance rather than inheriting from
// TitledComboBox in order to 'emulate' __declspec( novtable ) for non-MSVC
// compilers. This requires the various helper operators and the -> syntax.
//  Rather than making the whole class a template only the constructor is
// templatized so that headers that use the class do not have to also know about
// the actual parameters for which a DiscreteParameterComboBox will be created.
//                                            (18.07.2011.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

struct DiscreteParameterComboBox
{
    template <class Parameter>
    DiscreteParameterComboBox(juce::Component &parent, unsigned int const x, unsigned int const y,
                              Parameter const * = 0)
        : comboBox_(parent, x, y, LE::Parameters::Name<Parameter>::string_)
    {
        fillComboBoxForParameter<Parameter>(comboBox_);
    }

    TitledComboBox *operator->() { return &comboBox_; }
    TitledComboBox const *operator&() const { return &comboBox_; }
    operator TitledComboBox &() { return comboBox_; }
    operator TitledComboBox const &() const { return comboBox_; }

    TitledComboBox comboBox_;
}; // struct DiscreteParameterComboBox

} // namespace GUI

} // namespace SW

} // namespace LE

#endif // gui_hpp
