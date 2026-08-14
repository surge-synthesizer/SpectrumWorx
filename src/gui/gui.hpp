////////////////////////////////////////////////////////////////////////////////
//
// ---
// GUI
// ---
//
//   SpectrumWorx GUI implementation (based on our patched JUCE GIT tip).
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

#include "resources.hpp"
#include "theme.hpp"

/// `fs`, for the two path getters below. \see io/jucePath.hpp for the conversion
/// to and from JUCE's own file type, which is what the editor's edges want.
#include "filesystem/import.h"

/// \note Individual JUCE 8 headers have no include guards and open
/// `namespace juce {` mid-file; they may only be reached through the module
/// umbrella header. The old "juce/..." prefix was the deleted fork's layout.
///                                       (28.07.2026.) (SW port)
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

/// \note `enum ResourceBitmaps` and `resourceBitmap()` used to be declared here
/// too, numbered with multi-character literals ('01') and read off disk. Both
/// now come from resources.hpp, which numbers them as plain integers, reads them
/// out of the binary and can release the cache. The two spellings coexisted only
/// while this header did not compile; the name sets were identical, so nothing
/// at a call site changes.
///                                       (28.07.2026.) (SW port)

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
/// \note This was `ReferenceCountedGUIInitializationGuard`, and it did far more
/// than its replacement: it called `juce::initialiseJuce_GUI()` and
/// `juce::shutdownJuce_GUI()` **directly**, outside the `numScopedInitInstances`
/// counter that `juce::ScopedJuceInitialiser_GUI` uses
/// (juce_MessageManager.cpp:465-477). The CLAP shim holds one of those for the
/// life of the plugin (clap_juce_shim_impl.cpp:115,235), so closing an editor ran
/// `DeletedAtShutdown::deleteAll()` and `MessageManager::deleteInstance()` under a
/// shim that still held a live reference -- and the counter, the shutdown list and
/// the MessageManager singleton are one per binary, which is to say shared by
/// every instance of this plugin the host has loaded. One instance closing its
/// window deleted the message loop another was running on.
///
///   It also called `setCurrentThreadAsMessageThread()`, which is not a plugin's
/// to say, and threw an exception refusing to open a second editor at all. JUCE's
/// lifetime is the shim's; this keeps only what is ours.
///                                           (02.08.2026.) (SW port)
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

/// \note These take an Artwork rather than a juce::Image, and there is no
/// juce::Image overload on purpose. A widget holding a bitmap can only ever
/// draw at the size the skin was authored for; holding an Artwork, it draws a
/// converted file as a vector and an unconverted one exactly as before. Making
/// it the only spelling is what stops the raster path growing back -- the
/// symptom is invisible at 100 % and only shows as soft artwork once the
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

/// \note The two mapPathsFile() overloads went with the boost::mmap dependency
/// stage 2 removed. They mapped the `SpectrumWorx.paths` file the 2016 installer
/// wrote, to find the skin and the most-recently-used presets folder; the skin
/// is compiled in now (resources.hpp), so the only thing left worth keeping is
/// the MRU folder, and that is an ordinary user-data path.
///                                       (28.07.2026.) (SW port)

/// \note `initializePaths()`, `havePathsBeenInitialised()` and `resourcesPath()`
/// stood here. The first two were a two-phase initialisation whose initialiser
/// nothing called -- see gui.cpp -- and the third was declared and never
/// defined. These two answer on demand.
///                                       (31.07.2026.) (SW port)

/// \note **`fs::path`, not `juce::File`**, and that is what closed issue #28.
/// `sst::plugininfra::paths` answers with an `fs::path`; this used to convert it
/// into a `juce::File`, and getting that conversion wrong created a mojibake
/// Documents folder on a `ja_JP.UTF-8` desktop. There is no conversion here any
/// more -- the bytes the platform gave us are handed on untouched, and whoever
/// needs them as a `juce::String` converts at the edge that needs it, through
/// io/jucePath.hpp.
///                                       (09.08.2026.) (SW port)

/// The user's SpectrumWorx folder: `~/Documents/SpectrumWorx` or the platform
/// equivalent. Nothing is created by asking.
fs::path const &rootPath();

/// \brief The browser's most-recently-used preset folder, which starts at the
/// user's preset directory and which the browser writes back when it closes.
/// \note Const: this is where the user's presets live, not where the browser
/// last was. The browser used to assign to it, which moved the anchor
/// `goToParent()` stops at and the folder `createUserPresetsFolder()` makes;
/// where it was is its own business now (`PresetBrowser::Place`).
fs::path const &presetsFolder();

/// \brief Creates the user preset directory if it is not there.
/// \note Not done by presetsFolder(); see the note at the definition.
bool createUserPresetsFolder();

/// \note makeFSRefFromPath() went with it: FSRef is Carbon, which was never
/// ported to arm64. Its one caller was external_audio/sampleMac.cpp, which
/// stage 5.0 replaces with juce::AudioFormatManager.

////////////////////////////////////////////////////////////////////////////////
///
/// Theme
///
////////////////////////////////////////////////////////////////////////////////

/// \note Theme moved to theme.hpp, where it is a LookAndFeel_V2 (not a
/// LookAndFeel, which is abstract in JUCE 8, and not a LookAndFeel_V4, which
/// would paint linear sliders whole and silently drop the skinned thumb).
///
///   These two were static members of Theme purely because they read
/// Theme::settings(). They ask about a ModuleControlBase and a ModuleUI, which
/// a LookAndFeel has no business knowing about, and being members is what kept
/// Theme from being separable at all. aModuleControlNeedsLFOUpdate went with
/// them: it had no callers.
///                                       (28.07.2026.) (SW port)

class ModuleControlBase;
class ModuleUI;

bool shouldUpdateLFOControl(ModuleControlBase const &);

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

    /// \note Not a Clang workaround, which is how this was gated: declaring the
    /// placement pair above puts `operator delete` in class scope, and lookup for
    /// the one the *deleting destructor* needs stops there rather than falling
    /// back to `::operator delete`. Every widget here has a virtual destructor
    /// (juce::Component does), so a usable single-argument form is required, and
    /// GCC says so as plainly as Clang does. Only MSVC let it pass.
    ///                                       (29.07.2026.) (SW port)
    void *operator new(std::size_t const count) { return ::operator new(count); }
    void operator delete(void *const pObject) { return ::operator delete(pObject); }

  private:
    using BaseComponent::isParentOf;
    using BaseComponent::setVisible;
}; // class WidgetBase

/// \note `OwnedWindowBase` / `OwnedWindow<>` stood here: ~500 lines that made
/// the preset browser and the settings panel separate top-level desktop windows
/// owned by the editor's native one, kept in position by a process-wide
/// `SetWindowsHookEx` on Windows and `[NSWindow addChildWindow:]` on macOS.
/// **Stage 6.4** deleted them. Both panels are ordinary child components of the
/// editor now, sharing one overlay rectangle over the module strips --
/// `SpectrumWorxEditor::openOverlay()` is where that lives.
///                                           (01.08.2026.) (SW port)

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
/// layer runs into, and `GUI::loadPreset` still raises its one summary from
/// inside a load whenever a window happens to be open -- which for a session
/// restore is a window that was open for some other reason.
/// \see issue #12, "A load problem has nowhere to go but a modal box".
///                                           (08.08.2026.) (SW port)
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

/// \note Was `bool warningOkCancelBox(title, question)`, answered synchronously.
/// It cannot be: JUCE 8 defaults JUCE_MODAL_LOOPS_PERMITTED to 0, so
/// showOkCancelBox returns immediately and delivers the answer to a callback.
/// The two callers that want an answer are both in the preset browser's save
/// path, and inverting them is that file's job.
///                                       (28.07.2026.) (SW port)
void warningOkCancelBox(TCHAR const *title, TCHAR const *question,
                        std::function<void(bool)> onResult);

void addToParentAndShow(juce::Component &parent, juce::Component &childToBe);

void fadeOutComponent(juce::Component &, float finalAlpha, unsigned int duration,
                      bool useProxyComponent);

/// \note `class Lock : private juce::MessageManagerLock` stood here. It had no
/// call sites anywhere in `src/`, `tests/` or `tools/`, and its one line of body
/// was an assertion about the reference count SkinLifetime no longer keeps -- so
/// it either had to be rewritten or removed, and nothing calls it.
///                                           (02.08.2026.) (SW port)

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

/// \note Holds a GlyphArrangement rather than deriving from one: JUCE 8 marks
/// GlyphArrangement final. Only the constructor and draw() were ever used, so
/// forwarding the one is the whole of the change.
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
/// \class BitmapButton
///
////////////////////////////////////////////////////////////////////////////////

/// \note Was a juce::ImageButton, which can only hold a juce::Image and so
/// pinned every button to 1x no matter what its artwork was. It holds two
/// Artworks now and paints them itself, which is all it took for a converted
/// file to reach the screen as a vector -- juce::ImageButton's own painting was
/// four lines of LookAndFeel_V2::drawImageButton, reproduced in paintButton().
///                                       (06.08.2026.) (SW port)
class BitmapButton : public WidgetBase<juce::Button>
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
    BitmapButton(juce::Component &parent, Artwork const &on, Artwork const &off,
                 juce::Colour const &overlayColourWhenOver = defaultOverOverlay(),
                 bool toggled = true);

  public:
    /// Whichever of the two the button is showing: `on` while held down or
    /// toggled on, `off` otherwise. There is no separate hover artwork -- JUCE
    /// fell back to the normal image for that, and so does this.
    Artwork const &currentArtwork() const;

    void paintButton(juce::Graphics &, bool isMouseOverButton, bool isButtonDown) override;

    static juce::Colour const &normalOverlay();
    static juce::Colour const &downOverlay();
    static juce::Colour const &defaultOverOverlay();

    static float normalOpacity() { return 1.00f; }
    static float downOpacity() { return 1.00f; }
    static float overOpacity() { return 0.88f; }

  protected:
    static char const *getTextFromValue(unsigned int const booleanValue)
    {
        static char const *const strings[] = {"off", "on"};
        return strings[booleanValue];
    }

  private:
    Artwork const *pOn_{nullptr};
    Artwork const *pOff_{nullptr};
    juce::Colour overOverlay_;

    using juce::Button::getToggleStateValue;
}; // class BitmapButton

////////////////////////////////////////////////////////////////////////////////
///
/// \class PopupMenu
///
////////////////////////////////////////////////////////////////////////////////

/// \note This used to hold a juce::PopupMenu and reach into its private `items`
/// array through a shadow copy of JUCE's internal item struct -- to tick an
/// entry, to read an entry's text or icon, and to map an ID to an index. The
/// 2016 comment justified that by saying rebuilding the menu whenever the
/// selection changed was too slow.
///
///   That judgement was made against 2010 hardware and a menu of a few dozen
/// entries, and it is not true now: the items live here, and a juce::PopupMenu
/// is built from them at the moment of showing. JUCE's layout is then nobody's
/// business but JUCE's, getSelectedItemText() and getSelectedItemIcon() return
/// references into storage we own rather than into menu internals, and the
/// whole class survives a JUCE upgrade.
///                                       (28.07.2026.) (SW port)
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
    void addSubMenu(PopupMenu &, char const *name);
    void addSectionHeader(char const *title);
    void addSeparator();

    void clear();

    unsigned int numberOfItems() const;
    juce::String const &getItemText(unsigned int itemIndex) const;

    /// \note Asynchronous. JUCE 8 defaults JUCE_MODAL_LOOPS_PERMITTED to 0, and
    /// showMenu() -- which blocked until the user chose -- is gone with it.
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
    /// \note It was a process-wide `static bool`, so one editor's open menu
    /// silenced the equivalent button in every other instance in the host. All
    /// three readers ask about a menu they own -- a combo box's own, the LFO
    /// type's, the sample area's -- so per menu is both correct and what they
    /// meant. `mutable` because showing a menu changes nothing about what the
    /// menu *is*, which is why showAt() is const.
    ///                                       (02.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    bool menuActive() const { return menuActive_; }

  protected:
    struct Item
    {
        ItemID id;
        juce::String text;
        Artwork const *icon;
        bool enabled;
        bool isSectionHeader;
        bool isSeparator;
        /// Non-owning, as it was when this held a juce::PopupMenu: sub-menus are
        /// members of the owning widget and outlive the menu.
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
    /// names none is drawn at 1:1 beside an editor drawn at 1.5.
    ///
    /// \note And the area is in the owner's coordinates rather than the screen's
    /// for the same reason: every offset here is a skin pixel, and the screen
    /// stopped being measured in those the moment the editor took a zoom.
    ///                                       (14.08.2026.) (SW port)
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
    /// a legal item ID. This replaces the old mangling into the top byte, which
    /// existed because juce::PopupMenu reserves 0 for "dismissed".
    unsigned int currentSelectionID_;
}; // class PopupMenuWithSelection

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
    void setSelectedID(unsigned int newSelectionID);
    void setSelectedIndex(unsigned int newSelectionIndex);

  protected:
    ComboBox(juce::Component &parent, Artwork const &normalBackground,
             Artwork const &selectedBackground);

    void showMenu(std::function<void(bool)> onValueChanged);

  protected: // juce::Component overrides
    void paint(juce::Graphics &) override;

  private:
    Artwork const &normalBackground_;
    Artwork const &selectedBackground_;
}; // class ComboBox

////////////////////////////////////////////////////////////////////////////////
///
/// \class BackgroundImage
///
////////////////////////////////////////////////////////////////////////////////

class BackgroundImage : public WidgetBase<>
{
  protected:
    BackgroundImage(Artwork const &artwork) : pArtwork_(&artwork) {};

    Artwork const &image() const;
    void setImage(Artwork const &);

  protected: // juce::Component overrides
    void paint(juce::Graphics &) override;

  private:
    Artwork const *pArtwork_;
}; // class BackgroundImage

////////////////////////////////////////////////////////////////////////////////
///
/// \class LEDTextButton
///
////////////////////////////////////////////////////////////////////////////////

namespace Detail
{
void paintTextButton(BitmapButton const &, juce::Graphics &, unsigned int textX, unsigned int textY,
                     unsigned int imageX, unsigned int imageY, bool isMouseOverButton,
                     bool isButtonDown);
} // namespace Detail

class LEDTextButton : public BitmapButton
{
  public:
    LEDTextButton(juce::Component &parent, unsigned int x, unsigned int y, char const *text);

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

    static unsigned int const height = 11;
}; // class TextButton

////////////////////////////////////////////////////////////////////////////////
///
/// \class Knob
///
////////////////////////////////////////////////////////////////////////////////

class Knob : public WidgetBase<juce::Slider>
{
  public:
    typedef double value_type;
    typedef float param_type;

    value_type getValue() const { return static_cast<value_type>(juce::Slider::getValue()); }
    void setValue(param_type);

    param_type getNormalisedValue() const;

    void setupForParameter(char const *title, Artwork const &filmStripToSizeFor,
                           param_type defaultValue);

  protected:
    Knob(juce::Component &parent, unsigned int x, unsigned int y, unsigned int xMargin,
         unsigned int yMargin);

  public:
    /// \note removeValueListeners() went with the fork's Slider::valueListener().
    /// See the note in the Knob constructor.

  protected:
    /// \note Named paintFilmStrip() rather than being a second paint(). As
    /// paint() it hid juce::Slider::paint( Graphics & ) -- the virtual JUCE
    /// calls -- from every derived knob, which both compilers report and which
    /// a `#pragma clang diagnostic ignored "-Woverloaded-virtual"` used to
    /// silence on one of them. The two do different things to different
    /// arguments, and the derived knobs already had to qualify the call to say
    /// which they meant; the name now says it for them.
    ///                                       (05.08.2026.) (SW port)
    void paintFilmStrip(Artwork const &filmStrip, unsigned int xMargin, unsigned int yMargin,
                        juce::Graphics &);

  protected: // juce::Component overrides
    void startedDragging() noexcept override;
    /// \note Was declared and defined only under !NDEBUG, but
    /// EditorKnob::stoppedDragging calls it unconditionally -- an undefined
    /// symbol in every release build. The body is nothing but an assertion, so
    /// it costs nothing to exist: LE_ASSERT compiles away on its own.
    ///                                       (28.07.2026.) (SW port)
    void stoppedDragging() noexcept override;

  private:
    using juce::Slider::getMaxValueObject;
    using juce::Slider::getMinValueObject;
    using juce::Slider::getValueObject;

  protected:
    static unsigned int const numberOfKnobSubbitmaps = 127;
}; // class Knob

////////////////////////////////////////////////////////////////////////////////
///
/// \class EditorKnob
///
////////////////////////////////////////////////////////////////////////////////

class EditorKnob final : public Knob
{
  public:
    EditorKnob(SpectrumWorxEditor &parent, unsigned int x, unsigned int y);

    void setupForParameter(std::uint8_t parameterIndex, param_type minimumValue,
                           param_type maximumValue, param_type defaultValue);

  private: // juce::Component overrides
    void paint(juce::Graphics &) override;
    void valueChanged() noexcept override;

    void startedDragging() noexcept override;
    void stoppedDragging() noexcept override;

  private:
    SpectrumWorxEditor &editor();

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
void addEnumeratedParameterValueStringsToComboBox(LE::Utility::Span<char const *const> strings,
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
        LE::Utility::makeSpan(LE::Parameters::DiscreteValues<Parameter>::strings), comboBox);
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
