//------------------------------------------------------------------------------
//...mrmlj...cleanup...
//------------------------------------------------------------------------------
#include "gui.hpp"

#include "core/host_interop/plugin2Host.hpp" //...mrmlj...only for Plugin2HostPassiveInteropController::ParameterLabelGetter...
#include "gui/editor/editorHost.hpp" // the host's half of a knob's menu
#include "gui/editor/spectrumWorxEditor.hpp"
#include "gui/preferences.hpp"

#include "le/spectrumworx/engine/setup.hpp"

/// print() and its inverse: what an editor knob draws inside its face, and what
/// its menu's type-in field reads back. \see EditorKnob::setParameterFromText().
#include "le/parameters/parser.hpp"

#include "le/utility/cstdint.hpp"
#include "le/utility/lexicalCast.hpp"
#include "le/utility/platformSpecifics.hpp"

#include "le/utility/assert.hpp"
#include "le/utility/ignoreUnused.hpp"
#include "le/utility/polymorphicDowncast.hpp"

#include <sst/plugininfra/paths.h>
#include <algorithm>
#include <filesystem>
#include <system_error>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string_view>
#include "le/utility/span.hpp"
//------------------------------------------------------------------------------
#ifdef _WIN32
extern "C" IMAGE_DOS_HEADER __ImageBase;
#endif // _WIN32

namespace LE::SW::GUI
{

std::uint8_t SkinLifetime::liveEditors_(0);

SkinLifetime::SkinLifetime()
{
    // the editor is built inside the shim's guiCreate() under its
    // MessageManagerLock, so this is the message thread and a plain counter does
    LE_ASSERT(isThisTheGUIThread() || !isGUIInitialised());

    if (liveEditors_++ != 0)
        return;

    JUCE_AUTORELEASEPOOL
    {
#if defined(_WIN32)
        juce::Process::setCurrentModuleInstanceHandle(&__ImageBase);
#endif // _WIN32
        // before the Theme, which takes its colours from the map in its
        // constructor; a later change goes through Theme::reloadColours()
        ColourMap::setPalette(preferences().palette());

        Theme::createSingleton();
        juce::LookAndFeel::setDefaultLookAndFeel(&Theme::singleton());
    }
}

SkinLifetime::~SkinLifetime()
{
    LE_ASSERT(isThisTheGUIThread());

    if (--liveEditors_ != 0)
        return;

    JUCE_AUTORELEASEPOOL
    {
        // before the Theme goes: JUCE asserts if the default LookAndFeel is
        // destroyed while still installed
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);

        // Implementation note:
        //   We must manually reset the animator otherwise its timer becomes
        // orphaned when the juce::InternalTimerThread singleton is destroyed (so
        // it thinks it is still running even though its parent
        // juce::InternalTimerThread has been destroyed).
        //                                    (15.12.2011.) (Domagoj Saric)
        juce::Desktop::getInstance().getAnimator().cancelAllAnimations(false);

#if defined(_WIN32)
        LE_ASSERT(juce::Process::getCurrentModuleInstanceHandle() == &__ImageBase);
#endif // _WIN32

        Theme::destroySingleton();
    }
}

/// \note Neither of the two boxes below blocks: JUCE 8 defaults
/// JUCE_MODAL_LOOPS_PERMITTED to 0, and a plugin has no business spinning a modal
/// loop inside a host's message thread. `showMessageBoxAsync` is safe to call
/// from any thread and simply posts.

namespace
{
/// \note A counter rather than a flag, so that a nested load is not a case worth
/// being wrong about. `[main-thread]` throughout.
unsigned int unattendedLoads{0};
} // anonymous namespace

UnattendedLoad::UnattendedLoad() { ++unattendedLoads; }
UnattendedLoad::~UnattendedLoad() { --unattendedLoads; }
bool UnattendedLoad::inProgress() { return unattendedLoads != 0; }

void warningMessageBox(std::string_view const title, std::string_view const message,
                       bool const /*canBlock*/)
{
    // the invariant, and for now only asserted. \see UnattendedLoad
    LE_ASSERT_MSG(!UnattendedLoad::inProgress(),
                  "A modal box in front of a host restoring a session.");

    //...mrmlj...canBlock no longer means anything and should come off the ~15
    //...mrmlj...call sites once they are ported.
    JUCE_AUTORELEASEPOOL
    {
        // data(), not begin(): MSVC's std::string_view iterator is a class type
        // and does not convert to the char const * juce::String wants
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               juce::String(title.data(), title.size()),
                                               juce::String(message.data(), message.size()));
    }
}

void warningMessageBox(std::string_view const title, std::string_view const message,
                       std::function<void()> onDismissed)
{
    LE_ASSERT_MSG(!UnattendedLoad::inProgress(),
                  "A modal box in front of a host restoring a session.");

    JUCE_AUTORELEASEPOOL
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, juce::String(title.data(), title.size()),
            juce::String(message.data(), message.size()), {}, nullptr,
            juce::ModalCallbackFunction::create([onDismissed = std::move(onDismissed)](int) {
                if (onDismissed)
                    onDismissed();
            }));
    }
}

void warningOkCancelBox(TCHAR const *const title, TCHAR const *const question,
                        std::function<void(bool)> onResult)
{
    JUCE_AUTORELEASEPOOL
    {
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon, juce::String(title), juce::String(question), {},
            {}, nullptr,
            juce::ModalCallbackFunction::create([onResult = std::move(onResult)](int const result) {
                if (onResult)
                    onResult(result == 1);
            }));
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// Global paths
// ------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   The user's presets are the one thing that has to be somewhere a user can
/// find and back up: `~/Documents/SpectrumWorx` and its platform equivalents,
/// from `sst::plugininfra::paths`, which honours XDG on Linux. The skin and the
/// factory presets are compiled into the binary.
///
/// \note **Answered on demand, not initialised.** A function-local static is
/// computed on first use and is thread-safe by the language rather than by
/// convention, so there is no "too early" for a getter to be called at and no
/// initialisation step to forget.
///
/// \note **And no conversion at all.** `sst::plugininfra::paths` answers with an
/// `fs::path` and that is what this hands back. Converting to `juce::String` here
/// is what put a mojibake Documents folder on a `ja_JP.UTF-8` desktop: with
/// `-fno-char8_t` propagating from sst-plugininfra, `std::u8string` *is*
/// `std::string`, so the overload that decodes UTF-8 does not exist to be chosen
/// and the plain `char const *` one widens every byte to a code point. The five
/// edges that genuinely need a JUCE string convert in io/jucePath.hpp.
/// \see issue #28.

fs::path const &rootPath()
{
    static fs::path const path(sst::plugininfra::paths::bestDocumentsVendorFolderPathFor(
        "Surge Synth Team", "SpectrumWorx"));
    return path;
}

/// \note By reference, and const: this is where the user's presets live. Where
/// the browser last was is its own business -- see PresetBrowser::Place.
fs::path const &presetsFolder()
{
    static fs::path const folder(rootPath() / "Presets");
    return folder;
}

/// \note Separate from presetsFolder(), and called when the browser opens rather
/// than from the getter: asking where the presets go should not leave a directory
/// in someone's Documents, but a browser that opens on a folder which is not
/// there shows nothing and offers no way to make one.
///
/// \note A failure is neither asserted nor reported. A plugin does not always get
/// to write to the user's Documents folder -- an AUv2 under macOS app sandboxing,
/// a locked-down home directory, a CI container -- and none of those is a
/// programming error or a reason not to open the editor.

bool createUserPresetsFolder()
{
    auto const &folder(presetsFolder());

    // the std::error_code overloads throughout, never the throwing ones: this
    // runs behind the CLAP C entry points, where an escaping exception is
    // undefined behaviour rather than a false
    std::error_code error;
    if (std::filesystem::is_directory(folder, error))
        return true;

    // create_directories(), plural: the root above it may not be there either
    return std::filesystem::create_directories(folder, error) && !error;
}

void paintImage(juce::Graphics &graphics, Artwork const &artwork)
{
    paintImage(graphics, artwork, 0, 0);
}

void paintImage(juce::Graphics &graphics, Artwork const &artwork, int const x, int const y)
{
    artwork.draw(graphics, x, y);
}

void setSizeFromImage(juce::Component &component, Artwork const &artwork)
{
    component.setSize(artwork.getWidth(), artwork.getHeight());
}

void addToParentAndShow(juce::Component &parent, juce::Component &childToBe)
{
    parent.addAndMakeVisible(&childToBe);
}

void fadeOutComponent(juce::Component &component, float const finalAlpha,
                      unsigned int const duration, bool const useProxyComponent)
{
    // nothing to animate with no displays, and asking is fatal rather than
    // merely pointless: JUCE's proxy component dereferences
    // getDisplayForRect(), which is null when the display list is empty, and the
    // catch below cannot help with that. Reachable on any headless box
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        return;

    try
    {
        juce::Point<int> const centre(component.getBounds().getCentre());
        juce::Desktop::getInstance().getAnimator().animateComponent(
            &component, juce::Rectangle<int>(centre.getX(), centre.getY(), 0, 0), finalAlpha,
            duration, useProxyComponent, 0, 0);
    }
    catch (...)
    {
    }
}

/// \note Both ask JUCE rather than counting our own editors, which is a different
/// question: the shim's MessageManager runs for the whole of a plugin's life,
/// window or no window. `getInstanceWithoutCreating()` is the call that does not
/// bring one into existence to answer.
LE_NOINLINE bool isThisTheGUIThread()
{
    auto const *const pMessageManager(juce::MessageManager::getInstanceWithoutCreating());
    return pMessageManager && pMessageManager->isThisTheMessageThread();
}

bool isGUIInitialised() { return juce::MessageManager::getInstanceWithoutCreating() != nullptr; }

float displayScale()
{
    auto const &desktop(juce::Desktop::getInstance());
    auto const scale(desktop.getGlobalScaleFactor());
#ifndef NDEBUG
    for (auto const &display : desktop.getDisplays().displays)
        LE_ASSERT(display.scale == scale);
#endif // NDEBUG
    return scale;
}

namespace Detail
{
void setName(juce::Component &widget, juce::String const &newName)
{
    widget.juce::Component::setName(newName);
}
LE_NOINLINE void setName(juce::Component &widget, char const *const newName)
{
    setName(widget, juce::String(newName));
}

bool hasDirectFocus(juce::Component const &widget)
{
    bool const result(&widget == widget.getCurrentlyFocusedComponent());
    LE_ASSERT(result == widget.hasKeyboardFocus(false));
    return result;
}

bool hasFocus(juce::Component const &widget)
{
    bool const result(hasDirectFocus(widget) ||
                      isParentOf(widget, widget.getCurrentlyFocusedComponent()));
    LE_ASSERT(result == widget.hasKeyboardFocus(true));
    return result;
}

bool isParentOf(juce::Component const &parent, juce::Component const &possibleChild)
{
    juce::Component *pParent(possibleChild.getParentComponent());
    while (pParent)
    {
        if (pParent == &parent)
            return true;
        pParent = pParent->getParentComponent();
    }
    return false;
}

bool isParentOf(juce::Component const &parent, juce::Component const *pPossibleChild)
{
    return pPossibleChild && isParentOf(parent, *pPossibleChild);
}
} // namespace Detail

DrawableText::DrawableText(char const *const text, unsigned int const x, unsigned int const y,
                           unsigned int const width, unsigned int const height,
                           juce::Justification const justification, juce::Font const &font)
{
    glyphs_.addFittedText(font, text, Math::convert<float>(x), Math::convert<float>(y),
                          Math::convert<float>(width), Math::convert<float>(height), justification,
                          1);
}

juce::Font DrawableText::defaultFont()
{
    juce::Font font(Theme::singleton().Theme::getPopupMenuFont());
    font.setHeight(17);
    return font;
}

void PanelBackground::paint(juce::Graphics &graphics)
{
    auto const bounds(getLocalBounds().toFloat());
    if (which_ == Browser)
        PanelPainter::paintPresetBrowser(graphics, bounds);
    else
        PanelPainter::paintSettingsPage(graphics, bounds);
}

void PanelBackground::setSizeFromPanel()
{
    setSize(PanelPainter::width, (which_ == Browser) ? PanelPainter::presetBrowserHeight
                                                     : PanelPainter::settingsPageHeight);
}

////////////////////////////////////////////////////////////////////////////////
//
// PaintedButton
// -------------
//
////////////////////////////////////////////////////////////////////////////////

PaintedButton::PaintedButton(juce::Component &parent, juce::String const &text, int const width,
                             int const height, bool const toggled, Glow const glow)
    : glow_(glow)
{
    setButtonText(text);

    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    setSize(width, height);
    setClickingTogglesState(toggled);

    addToParentAndShow(parent, *this);
}

/// \note A transparency layer rather than a colour with an alpha in it: what is
/// being faded is a drawing of half a dozen fills, and fading each separately
/// would show their overlaps.
void PaintedButton::paintButton(juce::Graphics &graphics, bool isMouseOverButton,
                                bool const isButtonDown)
{
    if (!isEnabled())
        isMouseOverButton = false;

    auto opacity(isMouseOverButton ? PointerFeedback::over : PointerFeedback::normal);
    if (!isEnabled())
        opacity *= PointerFeedback::disabled;

    bool const fade(opacity < 1.0f);
    if (fade)
        graphics.beginTransparencyLayer(opacity);

    bool const alsoLit((glow_ == Glow::whenHovered) ? isMouseOverButton : getToggleState());

    ButtonPainter::paint(graphics, getLocalBounds().toFloat(), ButtonPainter::Rectangular,
                         isEnabled() && (isButtonDown || alsoLit), getButtonText());

    if (fade)
        graphics.endTransparencyLayer();
}

// juce::ComboBox is not customisable enough for this skin, so the items live
// here and a juce::PopupMenu is built from them at the moment of showing

PopupMenu::PopupMenu() : menuHeight_(0), menuWidth_(0) {}

void PopupMenu::addItem(ItemID const newItemId, char const *const newItemText,
                        Artwork const *const icon, bool const enabled)
{
    addItem(newItemId, newItemText, newItemText, icon, enabled);
}

void PopupMenu::addItem(ItemID const newItemId, char const *const newItemText,
                        char const *const shortText, Artwork const *const icon, bool const enabled)
{
    juce::String text(newItemText);
    // the *menu's* width, from the full text, whichever of the two the widget
    // goes on to show
    updateDimensionsForNewItem(text);
    items_.push_back({newItemId, std::move(text), shortText, icon, enabled, false, false, nullptr});
}

void PopupMenu::addSubMenu(PopupMenu &subMenu, char const *const name)
{
    LE_ASSERT(name);
    juce::String text(name);
    updateDimensionsForNewItem(text);
    items_.push_back({0, text, text, nullptr, true, false, false, &subMenu});
}

void PopupMenu::addSectionHeader(char const *const title)
{
    LE_ASSERT(title);
    juce::String text(title);
    updateDimensionsForNewItem(text);
    items_.push_back({0, text, text, nullptr, false, true, false, nullptr});
}

/// \note Nothing added to the measured height: a rule is a few pixels and the
/// height only centres showCenteredAtRight().
void PopupMenu::addSeparator()
{
    items_.push_back({0, {}, {}, nullptr, false, false, true, nullptr});
}

void PopupMenu::updateDimensionsForNewItem(juce::String const &itemText)
{
    int idealWidth, idealHeight;
    Theme::singleton().Theme::getIdealPopupMenuItemSize(itemText, false, -1, idealWidth,
                                                        idealHeight);
    menuWidth_ = std::max<unsigned int>(menuWidth_, idealWidth + 6);
    menuHeight_ += idealHeight;
}

/// \note juce::PopupMenu reserves 0 for "the user dismissed the menu", so the IDs
/// handed to it are ours plus one -- which keeps the whole ID space usable.
namespace
{
constexpr int toJuceID(PopupMenu::ItemID const id) { return static_cast<int>(id) + 1; }
constexpr PopupMenu::ItemID fromJuceID(int const id)
{
    return static_cast<PopupMenu::ItemID>(id - 1);
}
} // anonymous namespace

juce::PopupMenu PopupMenu::build(int const tickedIndex) const
{
    juce::PopupMenu menu;
    for (int index(0); index < static_cast<int>(items_.size()); ++index)
    {
        auto const &item(items_[static_cast<std::size_t>(index)]);
        if (item.isSeparator)
            menu.addSeparator();
        else if (item.isSectionHeader)
            menu.addSectionHeader(item.text);
        else if (item.pSubMenu)
            menu.addSubMenu(item.text, item.pSubMenu->build(item.pSubMenu->tickedIndex_), true);
        else if (auto icon(item.icon ? item.icon->drawableCopy() : nullptr); icon != nullptr)
        {
            // A vector icon goes in as a Drawable, so the menu draws it at its
            // own resolution rather than at the size the skin was authored for.
            menu.addItem(toJuceID(item.id), item.text, item.enabled, index == tickedIndex,
                         std::move(icon));
        }
        else
            menu.addItem(toJuceID(item.id), item.text, item.enabled, index == tickedIndex,
                         item.icon ? item.icon->image() : juce::Image());
    }
    return menu;
}

/// \note The 1 x 1 area is what gets the menu on the right side rather than
/// under the owner: juce::PopupMenu puts it below whatever it is given.
void PopupMenu::showCenteredAtRight(juce::Component const &owner, OnChosen onChosen) const
{
    showAt(owner, {owner.getWidth() + 9, (owner.getHeight() / 2) - (menuHeight_ / 2), 1, 1},
           std::move(onChosen));
}

void PopupMenu::showCenteredBelow(juce::Component const &owner, OnChosen onChosen) const
{
    int const width(owner.getWidth());
    int const overhang(menuWidth_ - width);
    showAt(owner, {(overhang > 0) ? -(overhang / 2) : 0, 0, width, owner.getHeight()},
           std::move(onChosen));
}

void PopupMenu::showAtScreenPosition(juce::Component const &owner,
                                     juce::Point<int> const screenPosition, OnChosen onChosen) const
{
    juce::Point<int> const position(owner.getLocalPoint(nullptr, screenPosition));
    showAt(owner, {position.getX(), position.getY(), 1, 1}, std::move(onChosen));
}

void PopupMenu::showAt(juce::Component const &owner, juce::Rectangle<int> const area,
                       OnChosen onChosen) const
{
    menuActive_ = true;
    // `this` in the callback: a menu can outlive what opened it -- the host can
    // close the editor while it is down -- but not the menu object itself, every
    // caller owning its menu as a member. \see ~SpectrumWorxEditor()
    build(tickedIndex_)
        .showMenuAsync(juce::PopupMenu::Options()
                           // the cast because Options holds a plain pointer; it
                           // only ever reads the component
                           .withTargetComponent(const_cast<juce::Component *>(&owner))
                           // after withTargetComponent(), which overwrites the
                           // area with the owner's own bounds
                           .withTargetScreenArea(owner.localAreaToGlobal(area))
                           .withMinimumWidth(area.getWidth()),
                       [this, onChosen = std::move(onChosen)](int const chosenID) {
                           menuActive_ = false;
                           if (onChosen)
                               onChosen(chosenID ? OptionalID(fromJuceID(chosenID)) : std::nullopt);
                       });
}

void PopupMenu::clear()
{
    items_.clear();
    tickedIndex_ = -1;
    menuHeight_ = 0;
    menuWidth_ = 0;
}

unsigned int PopupMenu::numberOfItems() const { return static_cast<unsigned int>(items_.size()); }

juce::String const &PopupMenu::getItemText(unsigned int const itemIndex) const
{
    return items_[itemIndex].text;
}

juce::String const &PopupMenu::getItemShortText(unsigned int const itemIndex) const
{
    return items_[itemIndex].shortText;
}

PopupMenuWithSelection::PopupMenuWithSelection() : currentSelection_(0), currentSelectionID_(0) {}

unsigned int PopupMenuWithSelection::getSelectedIndex() const
{
    LE_ASSERT(hasValidSelection());
    return static_cast<unsigned int>(currentSelection_);
}

unsigned int PopupMenuWithSelection::indexForID(unsigned int const id) const
{
    for (unsigned int index(0); index < numberOfItems(); ++index)
        if (items()[index].id == id)
            return index;
    LE_UNREACHABLE_CODE();
}

void PopupMenuWithSelection::setSelectedIndex(unsigned int const newSelectionIndex)
{
    updateSelection(newSelectionIndex);
    currentSelectionID_ = items()[newSelectionIndex].id + 1;
}

unsigned int PopupMenuWithSelection::getSelectedID() const
{
    LE_ASSERT(hasValidSelection());
    return currentSelectionID_ - 1;
}

void PopupMenuWithSelection::setSelectedID(unsigned int const newSelectionID)
{
    currentSelectionID_ = newSelectionID + 1;
    updateSelection(indexForID(newSelectionID));
}

juce::String const &PopupMenuWithSelection::getSelectedItemText() const
{
    return getItemText(static_cast<unsigned int>(currentSelection_));
}

juce::String const &PopupMenuWithSelection::getSelectedItemShortText() const
{
    return getItemShortText(static_cast<unsigned int>(currentSelection_));
}

Artwork const *PopupMenuWithSelection::getSelectedItemIcon() const
{
    // a reference into our own storage, valid for as long as the item is
    return items()[static_cast<std::size_t>(currentSelection_)].icon;
}

void PopupMenuWithSelection::updateSelection(unsigned int const newSelectionIndex)
{
    currentSelection_ = static_cast<int>(newSelectionIndex);
    tickedIndex_ = currentSelection_;
}

void PopupMenuWithSelection::clear()
{
    PopupMenu::clear();
    currentSelection_ = 0;
    currentSelectionID_ = 0;
}

bool PopupMenuWithSelection::hasValidSelection() const
{
    return (currentSelectionID_ != 0) &&
           (static_cast<unsigned int>(currentSelection_) < numberOfItems());
}

bool PopupMenuWithSelection::handleNewSelection(OptionalID const &chosenMenuEntryID)
{
    if (chosenMenuEntryID.has_value())
    {
        currentSelectionID_ = *chosenMenuEntryID + 1;
        updateSelection(indexForID(*chosenMenuEntryID));
        return true;
    }
    return false;
}

void PopupMenuWithSelection::showCenteredAtRight(juce::Component const &owner,
                                                 OnSelection onSelection)
{
    PopupMenu::showCenteredAtRight(
        owner, [this, onSelection = std::move(onSelection)](OptionalID const &chosen) {
            onSelection(handleNewSelection(chosen));
        });
}

void PopupMenuWithSelection::showCenteredBelow(juce::Component const &owner,
                                               OnSelection onSelection)
{
    PopupMenu::showCenteredBelow(
        owner, [this, onSelection = std::move(onSelection)](OptionalID const &chosen) {
            onSelection(handleNewSelection(chosen));
        });
}

ComboBox::ComboBox(juce::Component &parent, FrameStyle const &frame, int const width,
                   int const height)
    : frame_(frame), boxHeight_(height)
{
    setSize(width, height);
    addToParentAndShow(parent, *this);
}

/// \note `textMargin` either side: the background is a rounded rectangle, so a
/// smaller margin is spent on the curve and a long selection reads as touching
/// both ends.
void ComboBox::paint(juce::Graphics &graphics)
{
    // the rim is the whole of "this box is the selected one" -- white rather
    // than the skin's blue, and halfway between the two for one merely under the
    // pointer -- and the halo is under all three, so the box does not jump size
    auto const accent(ColourMap::getColour(ColourMap::Accent));
    auto const white(ColourMap::getColour(ColourMap::FocusHalo));
    auto const rim(showsAsSelected()  ? white
                   : showsAsHovered() ? accent.interpolatedWith(white, hoverStrength)
                                      : accent);

    FramePainter::paint(graphics,
                        juce::Rectangle<float>(0, 0, static_cast<float>(getWidth()),
                                               static_cast<float>(boxHeight_)),
                        frame_, rim, ColourMap::getColour(ColourMap::ComboBackground),
                        1.0f /*halo*/);

    graphics.setColour(ColourMap::getColour(ColourMap::Text));
    graphics.setFont(Theme::singleton().labelFont());
    // the short reading, which for all but a handful of values is the only one
    // there is. \see PopupMenu::addItem()
    graphics.drawFittedText(getSelectedItemShortText(), textMargin, 2, getWidth() - 2 * textMargin,
                            boxHeight_ - 2, juce::Justification::centred, 1, 0.1f);
}

/// \note \p onValueChanged runs later, on the message thread, and only if the
/// menu actually opened. The SafePointer is the point: a menu can outlive the
/// widget that opened it, the host being free to close the editor while it is
/// down.
void ComboBox::showMenu(std::function<void(bool)> onValueChanged)
{
    //...mrmlj...temporary workaround for the temporary zero padding workaround...
    if (!isEnabled())
        return;

    if (menuActive())
        return;

    showCenteredBelow(*this, [self = juce::Component::SafePointer<ComboBox>(this),
                              onValueChanged = std::move(onValueChanged)](bool const valueChanged) {
        if (!self)
            return;
        if (valueChanged)
        {
            self->grabKeyboardFocus();
            self->repaint();
        }
        if (onValueChanged)
            onValueChanged(valueChanged);
    });
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Five notches to a row, which is juce::ComboBox's own calibration
/// (`mouseWheelAccumulator += wheel.deltaY * 5.0f`). A mouse wheel sends about
/// 0.2 per detent and a trackpad a great many smaller deltas, so this is one
/// row per detent and a readable gesture on a trackpad.
///
/// \note Not while the menu is down. The menu scrolls itself, the box under it
/// is not what the pointer is over, and changing the selection from beneath an
/// open list is how a user ends up with a value nobody picked.
///
////////////////////////////////////////////////////////////////////////////////

void ComboBox::mouseWheelMove(juce::MouseEvent const &event, juce::MouseWheelDetails const &wheel)
{
    if (!isEnabled() || menuActive() || !hasValidSelection() || (numberOfItems() < 2) ||
        event.mods.isAnyMouseButtonDown())
    {
        juce::Component::mouseWheelMove(event, wheel);
        return;
    }

    constexpr float notchesPerRow{5};

    auto const travel(wheel.deltaY * (wheel.isReversed ? -1.0f : +1.0f) * notchesPerRow);

    // a reversal starts again rather than paying off what the other direction
    // left behind: otherwise one notch down and one notch up leaves the box a
    // row from where it started
    if ((travel * wheelTravel_) < 0)
        wheelTravel_ = 0;

    wheelTravel_ += travel;

    int rows{0};
    while (wheelTravel_ > 1.0f)
    {
        wheelTravel_ -= 1.0f;
        ++rows; // away from the user is down the list
    }
    while (wheelTravel_ < -1.0f)
    {
        wheelTravel_ += 1.0f;
        --rows;
    }

    if (rows == 0)
        return;

    auto const row(rowReachedBy(rows));
    if (row == getSelectedIndex())
        return;

    setSelectedIndex(row);
    selectionScrolled();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Steps a row at a time rather than adding \p rows to the index, because a
/// row a user cannot land on by clicking -- a disabled value, a section header, a
/// separator -- is not one the wheel may land on either.
///
/// \note And it stops at the ends rather than wrapping. A wheel has no sense of
/// where a list begins, so wrapping turns "keep scrolling" into "start over
/// somewhere else", which is not what the gesture means.
///
////////////////////////////////////////////////////////////////////////////////

unsigned int ComboBox::rowReachedBy(int rows) const
{
    auto const last(static_cast<int>(numberOfItems()) - 1);
    auto row(static_cast<int>(getSelectedIndex()));
    auto reached(row);

    auto const step(rows < 0 ? -1 : +1);
    for (auto remaining(std::abs(rows)); remaining > 0; --remaining)
    {
        row += step;
        while ((row >= 0) && (row <= last) && !isSelectableRow(static_cast<unsigned int>(row)))
            row += step;
        if ((row < 0) || (row > last))
            break;
        reached = row;
    }

    return static_cast<unsigned int>(reached);
}

bool ComboBox::isSelectableRow(unsigned int const row) const
{
    auto const &item(items()[row]);
    return item.enabled && !item.isSectionHeader && !item.isSeparator;
}

LE_NOINLINE void ComboBox::setSelectedID(unsigned int const newSelectionID)
{
    PopupMenuWithSelection::setSelectedID(newSelectionID);
    repaint();
}

void ComboBox::setSelectedIndex(unsigned int const newSelectionIndex)
{
    PopupMenuWithSelection::setSelectedIndex(newSelectionIndex);
    repaint();
}

CapsuleButton::CapsuleButton(juce::Component &parent, CapsuleStyle const &style, int const width,
                             int const height, bool const litWhenOn)
    : pStyle_(&style), litWhenOn_(litWhenOn)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    setSize(width, height);
    setClickingTogglesState(true);

    addToParentAndShow(parent, *this);
}

/// \note \see PointerFeedback, which is what every button in the editor dims by.
void CapsuleButton::paintCapsule(juce::Graphics &graphics, juce::Rectangle<int> const bounds,
                                 bool isMouseOverButton, bool const isButtonDown)
{
    if (!isEnabled())
        isMouseOverButton = false;

    auto opacity(isMouseOverButton ? PointerFeedback::over : PointerFeedback::normal);
    if (!isEnabled())
        opacity *= PointerFeedback::disabled;

    bool const fade(opacity < 1.0f);
    if (fade)
        graphics.beginTransparencyLayer(opacity);

    CapsulePainter::paint(graphics, bounds.toFloat(), *pStyle_,
                          (isButtonDown || getToggleState()) == litWhenOn_);

    if (fade)
        graphics.endTransparencyLayer();
}

void CapsuleButton::paintButton(juce::Graphics &graphics, bool const isMouseOverButton,
                                bool const isButtonDown)
{
    paintCapsule(graphics, getLocalBounds(), isMouseOverButton, isButtonDown);
}

namespace
{
/// The shape is drawn, then filled again in the tint where the pointer is on it.
void withPointerTint(juce::Button const &button, juce::Graphics &graphics,
                     bool const isMouseOverButton, juce::Colour const tint,
                     std::function<void(juce::Colour)> const &fill)
{
    if (button.isEnabled() && isMouseOverButton && !tint.isTransparent())
        fill(tint);
}
} // anonymous namespace

ArrowButton::ArrowButton(juce::Component &parent, int const width, int const height,
                         bool const fadeFromBase, ColourMap::Name const tintWhenOver)
    : fadeFromBase_(fadeFromBase), tintWhenOver_(tintWhenOver)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    setSize(width, height);
    setClickingTogglesState(false);

    addToParentAndShow(parent, *this);
}

void ArrowButton::paintArrow(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                             bool const isMouseOver)
{
    ArrowPainter::paint(graphics, bounds, fadeFromBase_);
    withPointerTint(*this, graphics, isMouseOver, ColourMap::getColour(tintWhenOver_),
                    [&](juce::Colour const tint) { ArrowPainter::tint(graphics, bounds, tint); });
}

void ArrowButton::paintButton(juce::Graphics &graphics, bool const isMouseOverButton,
                              bool const /*isButtonDown*/)
{
    paintArrow(graphics, getLocalBounds().toFloat(), isMouseOverButton);
}

EjectButton::EjectButton(juce::Component &parent)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    setSize(EjectStyle::widgetWidth, EjectStyle::widgetHeight);
    setClickingTogglesState(false);

    addToParentAndShow(parent, *this);
}

void EjectButton::paintButton(juce::Graphics &graphics, bool const isMouseOverButton,
                              bool const /*isButtonDown*/)
{
    auto const bounds(getLocalBounds().toFloat());
    EjectPainter::paint(graphics, bounds);
    withPointerTint(*this, graphics, isMouseOverButton,
                    ColourMap::getColour(ColourMap::MouseOverShade),
                    [&](juce::Colour const tint) { EjectPainter::tint(graphics, bounds, tint); });
}

namespace
{
/// \brief How wide the widget holding \p glyph is; they are all one height.
int glyphWidgetWidth(GlyphButton::Glyph const glyph)
{
    switch (glyph)
    {
    case GlyphButton::Glyph::FolderUp:
        return GlyphStyle::upWidgetWidth;
    case GlyphButton::Glyph::User:
        return GlyphStyle::userWidgetWidth;
    case GlyphButton::Glyph::JogPrevious:
    case GlyphButton::Glyph::JogNext:
        return GlyphStyle::jogWidgetWidth;
    case GlyphButton::Glyph::Lock:
        return GlyphStyle::lockWidgetWidth;
    }
    LE_UNREACHABLE_CODE();
}
} // anonymous namespace

GlyphButton::GlyphButton(juce::Component &parent, Glyph const glyph, bool const toggles)
    : glyph_(glyph)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    setSize(glyphWidgetWidth(glyph),
            (glyph == Glyph::Lock) ? GlyphStyle::lockWidgetHeight : GlyphStyle::rowHeight);
    setClickingTogglesState(toggles);

    addToParentAndShow(parent, *this);
}

/// \note Dimmed by a transparency layer rather than by an alpha on the colour,
/// for the reason PaintedButton gives: the up arrow is two overlapping shapes and
/// fading each separately would show where they meet.
void GlyphButton::paintButton(juce::Graphics &graphics, bool isMouseOverButton,
                              bool const isButtonDown)
{
    if (!isEnabled())
        isMouseOverButton = false;

    auto opacity(isMouseOverButton ? PointerFeedback::over : PointerFeedback::normal);
    if (!isEnabled())
        opacity *= PointerFeedback::disabled;

    bool const fade(opacity < 1.0f);
    if (fade)
        graphics.beginTransparencyLayer(opacity);

    auto const bounds(getLocalBounds().toFloat());
    auto const colour(ColourMap::getColour(
        isEnabled() && (isButtonDown || getToggleState()) ? ColourMap::Accent : ColourMap::Text));

    switch (glyph_)
    {
    case Glyph::FolderUp:
        GlyphPainter::paintFolderUp(graphics, bounds, colour);
        break;
    case Glyph::User:
        GlyphPainter::paintUser(graphics, bounds, colour);
        break;
    case Glyph::JogPrevious:
        GlyphPainter::paintJog(graphics, bounds, false /*points left*/, colour);
        break;
    case Glyph::JogNext:
        GlyphPainter::paintJog(graphics, bounds, true /*points right*/, colour);
        break;
    case Glyph::Lock:
        GlyphPainter::paintLock(graphics, bounds, colour);
        break;
    }

    if (fade)
        graphics.endTransparencyLayer();
}

LEDTextButton::LEDTextButton(juce::Component &parent, unsigned int const x, unsigned int const y,
                             char const *const text)
    : CapsuleButton(parent, ledCapsule, ledWidth, ledHeight)
{
    setName(text);

    setBounds(x, y,
              ledWidth +
                  juce::GlyphArrangement::getStringWidthInt(DrawableText::defaultFont(), getName()),
              ledHeight);
}

/// \note The caption sits at three: seventeen pixels of line box in a twenty-one
/// pixel widget is what centres it, in the frame ModuleLEDTextButton draws as
/// well as in the bare widget.
void LEDTextButton::paintButton(juce::Graphics &g, bool const isMouseOverButton,
                                bool const isButtonDown)
{
    g.setColour(ColourMap::getColour(ColourMap::Text));
    g.setFont(DrawableText::defaultFont());
    g.drawFittedText(getName(), ledWidth, 3, getWidth() - ledWidth, 19,
                     juce::Justification::horizontallyCentred, 1);

    paintCapsule(g, {0, 1, ledWidth, ledHeight}, isMouseOverButton, isButtonDown);
}

TextButton::TextButton(juce::Component &parent, unsigned int const x, unsigned int const y,
                       char const *const text)
{
    setName(text);

    juce::Font font(Theme::singleton().labelFont());
    font.setHeight(static_cast<float>(height));

    setBounds(x, y, juce::GlyphArrangement::getStringWidthInt(font, getName()), height);

    setClickingTogglesState(true);

    addToParentAndShow(parent, *this);
}

void TextButton::paintButton(juce::Graphics &g, bool const isMouseOverButton, bool /*isButtonDown*/)
{
#define INTEGER_ALPHA(alpha) static_cast<unsigned char>(alpha * 255)
    static unsigned char const alphas[2]
                                     [2] = /* [not toggled, toggled] [not mouse over, mouse over] */
        {{INTEGER_ALPHA(0.5), INTEGER_ALPHA(0.8)}, {INTEGER_ALPHA(1.0), INTEGER_ALPHA(0.85)}};
#undef INTEGER_ALPHA

    unsigned char const alpha(alphas[getToggleState()][isMouseOverButton]);

    juce::Font font(Theme::singleton().labelFont());
    font.setHeight(static_cast<float>(height));

    g.setColour(ColourMap::getColour(ColourMap::Accent).withAlpha(alpha));
    g.setFont(font);
    g.drawSingleLineText(getName(), 0, height);
}

////////////////////////////////////////////////////////////////////////////////
//
// FineDrag
// --------
//
////////////////////////////////////////////////////////////////////////////////

void FineDrag::keepDragLinear(juce::Slider &slider)
{
    // the trailing false is userCanPressKeyToSwapMode, off so that every drag is
    // the plain linear one adjust() can reason about: JUCE's default swaps
    // command, control and alt into a velocity-based drag that responds to the
    // mouse's speed rather than its distance. The first three arguments are
    // JUCE's own defaults, there being no setter for the fourth alone
    slider.setVelocityModeParameters(1.0, 1, 0.0, false);
}

void FineDrag::begin(float const anchor) noexcept
{
    start_ = last_ = anchor;
    travel_ = 0;
}

float FineDrag::adjust(float const position, bool const fine) noexcept
{
    travel_ += (position - last_) / (fine ? ratio : 1.0f);
    last_ = position;
    return start_ + travel_;
}

juce::MouseEvent linkThumbsOnAlt(juce::MouseEvent const &event)
{
    auto const modifiers(event.mods.isAltDown()
                             ? event.mods.withFlags(juce::ModifierKeys::shiftModifier)
                             : event.mods.withoutFlags(juce::ModifierKeys::shiftModifier));

    return {event.source,
            event.position,
            modifiers,
            event.pressure,
            event.orientation,
            event.rotation,
            event.tiltX,
            event.tiltY,
            event.eventComponent,
            event.originalComponent,
            event.eventTime,
            event.mouseDownPosition,
            event.mouseDownTime,
            event.getNumberOfClicks(),
            event.mouseWasDraggedSinceMouseDown()};
}

juce::MouseEvent refinedDrag(FineDrag &drag, juce::MouseEvent const &event)
{
    return linkThumbsOnAlt(event).withNewPosition(juce::Point<float>(
        drag.adjust(event.position.x, event.mods.isShiftDown()), event.position.y));
}

////////////////////////////////////////////////////////////////////////////////
//
// HorizontalSlider
// ----------------
//
////////////////////////////////////////////////////////////////////////////////

HorizontalSlider::HorizontalSlider()
{
    setSliderStyle(LinearHorizontal);
    setTextBoxStyle(NoTextBox, true, 0, 0);
    FineDrag::keepDragLinear(*this);
}

void HorizontalSlider::mouseDown(juce::MouseEvent const &event)
{
    fine_.begin(event.position.x);
    juce::Slider::mouseDown(linkThumbsOnAlt(event));
}

void HorizontalSlider::mouseDrag(juce::MouseEvent const &event)
{
    juce::Slider::mouseDrag(refinedDrag(fine_, event));
}

Knob::Knob(juce::Component &parent, unsigned int const x, unsigned int const y,
           unsigned int const xMargin, unsigned int const yMargin)
{
    setBounds(x, y, xMargin, yMargin);
    //setTooltip             ( title                 );
    setSliderStyle(RotaryVerticalDrag);
    setTextBoxStyle(NoTextBox, true, 0, 0);
    //setPopupDisplayEnabled ( true, 0               ); //...mrmlj...for testing...
    // no setPopupMenuEnabled(): the right button raises ParameterMenu's
    setMouseDragSensitivity(coarseDragPixels());

    FineDrag::keepDragLinear(*this);

    addToParentAndShow(parent, *this);
}

void Knob::setupForParameter(char const *const title, unsigned int const diameter,
                             param_type const defaultValue)
{
    setName(title);

    // The margins the constructor stashed in the bounds: a Knob is built before
    // it knows how big its face is, so setBounds() carries them until here.
    unsigned int const xMargin(getWidth());
    unsigned int const yMargin(getHeight());

    setSize(diameter + xMargin, diameter + yMargin);

    setDoubleClickReturnValue(true, defaultValue);
}

bool Knob::hidesCursorWhileDragging() const
{
    return parameterMenu().parameterEditable() && preferences().hideCursorOnKnobDrag();
}

void Knob::startedDragging() noexcept
{
    if (!hidesCursorWhileDragging())
        return;

    LE_ASSERT(juce::Desktop::getInstance().getNumMouseSources() == 1);
    // by value: getMainMouseSource() returns a prvalue, and
    // enableUnboundedMouseMovement() is const, so a copy does the same work
    auto mouseSource(juce::Desktop::getInstance().getMainMouseSource());

    // compared by value, not by address: MouseInputSource is a handle around a
    // pimpl, so the local copy's address never equals anything Desktop owns
    [[maybe_unused]] auto const *const pDraggingSource(
        juce::Desktop::getInstance().getDraggingMouseSource(0));
    LE_ASSERT(!pDraggingSource || //...mrmlj...double click...
              (*pDraggingSource == mouseSource));

    /// \note setMouseCursor( juce::MouseCursor::NoCursor ) and
    /// enableUnboundedMouseMovement() result in a black box under VMWare.
    ///                                       (10.07.2012.) (Domagoj Saric)
    mouseSource.enableUnboundedMouseMovement(true, false);
    LE_ASSERT(mouseSource.canDoUnboundedMovement());
}

void Knob::stoppedDragging() noexcept
{
    LE_ASSERT(juce::Desktop::getInstance().getNumMouseSources() == 1);
    //juce::MouseInputSource & mouseSource( juce::Desktop::getInstance().getMainMouseSource() );
    // http://www.rawmaterialsoftware.com/viewtopic.php?f=2&t=5628&hilit=enableUnboundedMouseMovement
    //mouseSource.enableUnboundedMouseMovement( false, !preferences().hideCursorOnKnobDrag() );

    //...mrmlj...neither of these works/helps because
    //...mrmlj...enableUnboundedMouseMovement() seems to handle it
    //...mrmlj...automatically (but imprecisely)...
    //juce::Desktop::setMousePosition( juce::Desktop::getLastMouseDownPosition() );
    //juce::Desktop::setMousePosition( this->localPointToGlobal( this->getBounds().getCentre() ) );
}

////////////////////////////////////////////////////////////////////////////////
//
// ParameterMenu::ValueTypein
// --------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief The "type a value here" line of a parameter's menu: a juce::TextEditor
/// living inside a menu item.
///
/// \note `CustomComponent( false )` -- not triggered automatically -- because a
/// click inside the field must land in the field. `triggerMenuItem()` dismisses
/// it when the user commits or gives up.
///
/// \note The widget is held through a SafePointer and every use is guarded. A
/// menu is asynchronous, and while ~SpectrumWorxEditor dismisses whatever is
/// open, the deferred grab below can still find itself running against a widget
/// that has gone.
///
////////////////////////////////////////////////////////////////////////////////

class ParameterMenu::ValueTypein final : public juce::PopupMenu::CustomComponent,
                                         private juce::TextEditor::Listener
{
  public:
    explicit ValueTypein(ParameterMenu &parameter)
        : juce::PopupMenu::CustomComponent(/*isTriggeredAutomatically*/ false),
          parameter_(&parameter), widget_(&parameter.menuOwner())
    {
        editor_.setWantsKeyboardFocus(true);
        editor_.setIndents(6, 0);
        editor_.setJustification(juce::Justification::centredLeft);
        editor_.setFont(Theme::singleton().Theme::getPopupMenuFont());
        editor_.addListener(this);
        addAndMakeVisible(editor_);
    }

  private: // juce::PopupMenu::CustomComponent overrides
    void getIdealSize(int &idealWidth, int &idealHeight) override
    {
        idealWidth = fieldWidth;
        idealHeight = fieldHeight;
    }

    void resized() override { editor_.setBounds(getLocalBounds().reduced(6, 3)); }

    /// \note Deferred by a message rather than done here: the item is made
    /// visible while the menu is still laying itself out and before its window is
    /// on screen, and a component that grabs the keyboard then does not keep it.
    void visibilityChanged() override
    {
        if (!isVisible())
            return;

        juce::Component::SafePointer<ValueTypein> pThis(this);
        juce::MessageManager::callAsync([pThis] {
            if (!pThis || !pThis->isVisible())
                return;
            pThis->editor_.setText(pThis->widget_ ? pThis->parameter_->parameterValueText()
                                                  : juce::String(),
                                   juce::dontSendNotification);
            pThis->editor_.grabKeyboardFocus();
            pThis->editor_.selectAll();
        });
    }

  private: // juce::TextEditor::Listener overrides
    /// \note The typo is not committed and not clamped: setParameterFromText()
    /// answers false for text no value of this parameter displays as, and the
    /// menu simply closes with the parameter where it was.
    void textEditorReturnKeyPressed(juce::TextEditor &typedInto) override
    {
        if (widget_)
            parameter_->setParameterFromText(typedInto.getText());
        triggerMenuItem();
    }
    void textEditorEscapeKeyPressed(juce::TextEditor &) override { triggerMenuItem(); }

  private:
    static int constexpr fieldWidth{210};
    static int constexpr fieldHeight{33};

  private:
    /// \note The two are one object, and the SafePointer is what says whether it
    /// is still there: a ParameterMenu is a mix-in with no lifetime of its own,
    /// so there is nothing to hold weakly but the widget it is part of.
    ParameterMenu *const parameter_;
    juce::Component::SafePointer<juce::Component> widget_;

    juce::TextEditor editor_;
}; // class ParameterMenu::ValueTypein

void ParameterMenu::showParameterMenu(juce::MouseEvent const &event, bool const skipSetToDefault)
{
    auto &widget(menuOwner());

    bool const editable(parameterEditable());

    juce::PopupMenu menu;

    menu.addSectionHeader(parameterName());

    menu.addSeparator();

    if (editable && parameterAcceptsText())
    {
        // the result ID is unused -- the field dismisses the menu itself -- but
        // it may not be zero, which juce::PopupMenu reserves for "dismissed"
        menu.addCustomItem(1, std::make_unique<ValueTypein>(*this));
    }

    addParameterValueEntries(menu);

    menu.addSeparator();

    addParameterMenuEntries(menu);

    if (!skipSetToDefault)
    {
        menu.addItem("Reset to default value", editable, /*isTicked*/ false,
                     [this, pWidget = juce::Component::SafePointer<juce::Component>(&widget)] {
                         if (pWidget)
                             setParameterToDefault();
                     });
    }

    menu.addSeparator();

    auto &editor(SpectrumWorxEditor::fromChild(widget));

    editor.editorHost().addHostParameterEntries(parameterID(), menu);

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withParentComponent(&editor)
                           .withTargetComponent(&widget)
                           .withTargetScreenArea(widget.localAreaToGlobal(
                               juce::Rectangle<int>(event.x, event.y, 1, 1))),
                       [pWidget = juce::Component::SafePointer<juce::Component>(&widget)](int) {
                           if (pWidget && pWidget->getWantsKeyboardFocus() && pWidget->isShowing())
                               pWidget->grabKeyboardFocus();
                       });
}

bool isOnRoundFace(juce::Rectangle<int> const face, juce::Point<int> const position)
{
    LE_ASSERT_MSG(face.getWidth() == face.getHeight(), "Not a circle.");
    auto const radius(face.getWidth() / 2.0f);
    juce::Point<float> const centre(static_cast<float>(face.getX()) + radius,
                                    static_cast<float>(face.getY()) + radius);
    return centre.getDistanceFrom(position.toFloat()) <= radius;
}

void passMousePressToParent(juce::Component &widget, juce::MouseEvent const &event)
{
    if (auto *const pParent = widget.getParentComponent())
        pParent->mouseDown(event.getEventRelativeTo(pParent));
}

bool Knob::isOnKnobFace(juce::Point<int>) const { return true; }

////////////////////////////////////////////////////////////////////////////////
///
/// \note The right button off the knob's face is *forwarded* rather than
/// swallowed: the widget is a rectangle around a circle with room for a caption
/// under it, so it covers a good deal of the module strip it stands on, and the
/// strip's own menu has to stay reachable there. \see TriggerButton::mouseDown(),
/// which is the same widget in a different shape.
///
////////////////////////////////////////////////////////////////////////////////

void Knob::mouseDown(juce::MouseEvent const &event)
{
    if (event.mods.isPopupMenu())
    {
        if (isOnKnobFace(event.getPosition()))
            return parameterMenu().showParameterMenu(event);
        return passMousePressToParent(*this, event);
    }

    fine_.begin(event.position.y);

    juce::Slider::mouseDown(event);
}

juce::MouseEvent Knob::fineAdjusted(juce::MouseEvent const &event)
{
    return event.withNewPosition(juce::Point<float>(
        event.position.x, fine_.adjust(event.position.y, event.mods.isShiftDown())));
}

void Knob::mouseDrag(juce::MouseEvent const &event)
{
    if (event.mods.isPopupMenu())
        return;
    juce::Slider::mouseDrag(fineAdjusted(event));
}

/// \note The cursor goes back where it was pressed, overwriting juce::Slider's
/// own attempt: `restoreMouseIfHidden` constrains its answer to the knob's screen
/// bounds, so a drag longer than the eighty-odd pixels a knob is tall leaves the
/// cursor pinned to an edge. Where the press was is what the gesture means -- the
/// mouse was taken away for the duration and is being handed back.

void Knob::mouseUp(juce::MouseEvent const &event)
{
    if (event.mods.isPopupMenu())
        return;

    juce::Slider::mouseUp(event);

    if (hidesCursorWhileDragging())
        juce::Desktop::getInstance().getMainMouseSource().setScreenPosition(
            event.getMouseDownScreenPosition().toFloat());
}

/// \note Deliberately empty. juce::Slider::modifierKeysChanged calls
/// `restoreMouseIfHidden()` for every modifier change that is not its own
/// velocity swap, which puts the cursor back in the middle of the knob and ends
/// the unbounded movement -- so pressing shift part way through a drag would
/// break the gesture the key is there to refine. Nothing is lost by dropping it:
/// juce::Component's own only forwards to the parent.

void Knob::modifierKeysChanged(juce::ModifierKeys const &) {}

void LE_NOINLINE Knob::setValue(param_type const newValue)
{
#ifndef NDEBUG
    {
        // Implementation note:
        //   A simple
        // LE_ASSERT( Math::isValueInRange( static_cast<value_type>( newValue ), getMinimum(), getMaximum() ) );
        // assertion would sometimes falsely fail for knobs with
        // quantization-adjusted ranges.
        //                                    (05.05.2011.) (Domagoj Saric)
        Engine::Setup const &engineSetup(SpectrumWorxEditor::fromChild(*this).engineSetup());
        Knob::value_type const maxQuantizationAdjustment(std::max(
            engineSetup.frequencyRangePerBin<Knob::param_type>(), engineSetup.stepTime() * 1000));
        auto const minimum(getMinimum());
        auto const maximum(getMaximum());
        LE_ASSERT_MSG(Math::isValueInRange(static_cast<value_type>(newValue),
                                           minimum - maxQuantizationAdjustment,
                                           maximum + maxQuantizationAdjustment),
                      "Knob value out of range");
    }
#endif // NDEBUG
    juce::Slider::setValue(static_cast<value_type>(newValue), juce::dontSendNotification);
}

Knob::param_type Knob::getNormalisedValue() const
{
    Knob::param_type const fullRangeValue(static_cast<param_type>(getValue()));
    Knob::param_type const minimumValue(static_cast<param_type>(getMinimum()));
    Knob::param_type const maximumValue(static_cast<param_type>(getMaximum()));

    return Math::convertLinearRange<Knob::param_type, 0, 1, 1, Knob::param_type>(
        fullRangeValue, minimumValue, maximumValue);
}

EditorKnob::EditorKnob(SpectrumWorxEditor &parent, unsigned int const x, unsigned int const y)
    : Knob(parent.mainArea(), x, y, 0, 0), parameterIndex_(0)
{
    setScrollWheelEnabled(true);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void EditorKnob::setupForParameter(std::uint8_t const parameterIndex, param_type const minimumValue,
                                   param_type const maximumValue, param_type const defaultValue)
{
    Knob::setupForParameter(nullptr, diameter, defaultValue);

    parameterIndex_ = parameterIndex;

    setRange(minimumValue, maximumValue, 0);
}

namespace
{
#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.
struct ParameterPrinter
{
    typedef char const *result_type;
    template <class Parameter> result_type operator()() const
    {
        return LE::Parameters::print<Parameter>(value, engineSetup, buffer);
    }
    Engine::Setup const &engineSetup;
    float const value;
    LE::Parameters::PrintBuffer const buffer;
}; // struct ParameterPrinter
#pragma warning(pop)
} // namespace

/// \note One place rather than two that could drift: the menu's type-in field
/// starts out holding exactly what the knob's face is showing.
juce::String EditorKnob::parameterValueText() const
{
    //...mrmlj...ugh...
    std::array<char, 20> valueString;
    ParameterPrinter const printer = {editor().engineSetup(), static_cast<float>(getValue()),
                                      LE::Utility::makeSpan(&valueString[0], valueString.size())};
    using LE::Parameters::IndexOf;
    using namespace GlobalParameters;
    typedef GlobalParameters::Parameters GlobalParams;
    switch (parameterIndex_)
    {
    case IndexOf<GlobalParams, InputGain>::value:
        printer.operator()<InputGain>();
        break;
    case IndexOf<GlobalParams, OutputGain>::value:
        printer.operator()<OutputGain>();
        break;
    case IndexOf<GlobalParams, MixPercentage>::value:
        printer.operator()<MixPercentage>();
        break;
        LE_DEFAULT_CASE_UNREACHABLE();
    }
    //...mrmlj...assumes global parameters are static...
    char const *const pUnit(Plugin2HostPassiveInteropController::ParameterLabelGetter()(
        parameterID().value._.global, nullptr));

    return juce::String(&valueString[0]) + pUnit;
}

void EditorKnob::paint(juce::Graphics &graphics)
{
    // valueToProportionOfLength() rather than getNormalisedValue(), so a skewed
    // range -- which the two gains have -- points where the artwork does
    paintEditorKnob(graphics, juce::Rectangle<float>(0, 0, diameter, diameter),
                    static_cast<float>(juce::Slider::valueToProportionOfLength(Knob::getValue())));

    // a main knob shows its value inside its own face
    graphics.setColour(ColourMap::getColour(ColourMap::Text));
    {
        juce::Font font(Theme::singleton().labelFont());
        font.setHeight(17);
        font.setBold(true);
        graphics.setFont(font);
    }

    graphics.drawFittedText(parameterValueText(), 18, 24, 48, 36, juce::Justification::centred, 1,
                            0.1f);
}

ParameterID EditorKnob::parameterID() const
{
    ParameterID parameterID;
    parameterID.value.type = ParameterID::GlobalParameter;
    parameterID.value._.global.index = parameterIndex_;
    return parameterID;
}

/// \note Asked of the same place the host asks, rather than off the widget:
/// Knob::setupForParameter() is handed a null title for these three, because a
/// main knob prints its value inside its own face and has never had a caption.
juce::String EditorKnob::parameterName() const
{
    std::array<char, 64> name{};
    Plugin2HostPassiveInteropController::getParameterName(
        parameterID(), LE::Utility::makeSpan(&name[0], name.size()), nullptr);
    return juce::String(&name[0]);
}

bool EditorKnob::setParameterFromText(juce::String const &text)
{
    using LE::Parameters::IndexOf;
    using namespace GlobalParameters;
    typedef GlobalParameters::Parameters GlobalParams;

    auto const &engineSetup(editor().engineSetup());
    char const *const string(text.toRawUTF8());

    LE::Parameters::ParsedValue value;
    switch (parameterIndex_)
    {
    case IndexOf<GlobalParams, InputGain>::value:
        value = LE::Parameters::parse<InputGain>(string, engineSetup);
        break;
    case IndexOf<GlobalParams, OutputGain>::value:
        value = LE::Parameters::parse<OutputGain>(string, engineSetup);
        break;
    case IndexOf<GlobalParams, MixPercentage>::value:
        value = LE::Parameters::parse<MixPercentage>(string, engineSetup);
        break;
        LE_DEFAULT_CASE_UNREACHABLE();
    }

    if (!value)
        return false;

    setParameterValue(*value);
    return true;
}

/// \note getDoubleClickReturnValue() is the default Knob::setupForParameter()
/// was given, so the menu entry and the double click cannot disagree about what
/// "default" means.
void EditorKnob::setParameterToDefault() { setParameterValue(getDoubleClickReturnValue()); }

/// \note Bracketed in a gesture of its own, which a drag gets from
/// started/stoppedDragging(): without it the host sees a parameter jump with no
/// gesture around it, which some record as automation and some ignore.
///
/// \note sendNotificationSync, so that valueChanged() -- and through it the queue
/// and the host -- runs before this returns, exactly as it does mid-drag.

void EditorKnob::setParameterValue(double const newValue)
{
    auto &editor(this->editor());
    editor.mainKnobDragStarted(parameterIndex_);
    juce::Slider::setValue(newValue, juce::sendNotificationSync);
    editor.mainKnobDragStopped(parameterIndex_);
    repaint();
}

/// \note EditorKnob::valueChanged() lives in spectrumWorxEditor.cpp: it is the
/// only thing here that instantiates
/// SpectrumWorxEditor::globalParameterChanged<>, which needs the complete plugin
/// type. Everything else in this file needs the editor declared, not defined.

void EditorKnob::startedDragging() noexcept
{
    Knob::startedDragging();
    editor().mainKnobDragStarted(parameterIndex_);
}

void EditorKnob::stoppedDragging() noexcept
{
    editor().mainKnobDragStopped(parameterIndex_);
    Knob::stoppedDragging();
}

/// \note fromChild() rather than a downcast of the parent, which is the main
/// area rather than the editor.
SpectrumWorxEditor &EditorKnob::editor() const { return SpectrumWorxEditor::fromChild(*this); }

TitledComboBox::TitledComboBox(juce::Component &parent, unsigned int const x, unsigned int const y,
                               char const *const title)
    : ComboBox(parent, settingsComboFrame, settingsComboWidth, settingsComboHeight),
      title_(title, 6, 0, getWidth() - 12, 20, juce::Justification::left)
{
    TitledComboBox::setBounds(x, y, getWidth(), getHeight() + 23);
}

void TitledComboBox::paint(juce::Graphics &graphics)
{
    if (!hasValidSelection())
        return;
    graphics.setOrigin(0, +18);
    ComboBox::paint(graphics);
    graphics.setOrigin(0, -18);
    title_.draw(graphics);
}

void TitledComboBox::mouseDown(juce::MouseEvent const &)
{
    ComboBox::showMenu(
        [self = juce::Component::SafePointer<TitledComboBox>(this)](bool const valueChanged) {
            if (self && valueChanged)
                //...mrmlj...move...editor/settings specific...
                SpectrumWorxEditor::Settings::comboBoxValueChanged(*self);
        });
}

void TitledComboBox::selectionScrolled()
{
    //...mrmlj...move...editor/settings specific...
    SpectrumWorxEditor::Settings::comboBoxValueChanged(*this);
}

TitledTextBox::TitledTextBox(juce::Component &parent, unsigned int const x, unsigned int const y,
                             char const *const title, int const maximumLength)
    : title_(title, 6, 0, settingsComboWidth - 12, 20, juce::Justification::left)
{
    setName(title);
    setSize(settingsComboWidth, settingsComboHeight + 23);
    addToParentAndShow(parent, *this);

    // the frame is this widget's, so the editor brings none of its own
    editor_.setMultiLine(false);
    editor_.setReturnKeyStartsNewLine(false);
    editor_.setPopupMenuEnabled(true);
    editor_.setBorder({});
    editor_.setIndents(0, 0);
    editor_.setJustification(juce::Justification::centred);
    editor_.setFont(Theme::singleton().labelFont());
    editor_.setInputRestrictions(maximumLength);
    editor_.setColour(juce::TextEditor::backgroundColourId,
                      ColourMap::getColour(ColourMap::Transparent));
    editor_.setColour(juce::TextEditor::textColourId, ColourMap::getColour(ColourMap::Text));
    editor_.addListener(this);

    // ComboBox::textMargin either side, so a typed name sits where a chosen
    // one does
    editor_.setBounds(ComboBox::textMargin, boxTop + 2,
                      settingsComboWidth - 2 * ComboBox::textMargin, settingsComboHeight - 4);
    addToParentAndShow(*this, editor_);

    setBounds(static_cast<int>(x), static_cast<int>(y), getWidth(), getHeight());
}

juce::String TitledTextBox::text() const { return editor_.getText(); }

void TitledTextBox::setText(juce::String const &newText)
{
    editor_.setText(newText, juce::dontSendNotification);
}

void TitledTextBox::edited()
{
    if (onEdit)
        onEdit(text());
}

void TitledTextBox::commit()
{
    if (onCommit)
        onCommit();
}

void TitledTextBox::paint(juce::Graphics &graphics)
{
    auto const accent(ColourMap::getColour(ColourMap::Accent));
    auto const rim(hasFocus() ? ColourMap::getColour(ColourMap::FocusHalo) : accent);

    FramePainter::paint(graphics,
                        juce::Rectangle<float>(0, static_cast<float>(boxTop),
                                               static_cast<float>(settingsComboWidth),
                                               static_cast<float>(settingsComboHeight)),
                        settingsComboFrame, rim, ColourMap::getColour(ColourMap::ComboBackground),
                        1.0f /*halo*/);

    // the title takes the context's colour, and the frame left its own
    graphics.setColour(ColourMap::getColour(ColourMap::Text));
    title_.draw(graphics);
}

namespace Detail
{
void addPowerOfTwoValueStringsToComboBox(unsigned int const firstValue,
                                         unsigned int const lastValue, ComboBox &comboBox)
{
    LE_ASSERT_MSG(comboBox.numberOfItems() == 0, "ComboBox already filled.");
    std::array<char, 20> buffer;
    unsigned int value(firstValue);
    while (value <= lastValue)
    {
        Utility::lexical_cast(value, buffer);
        comboBox.addItem(value, &buffer[0]);
        value *= 2;
    }
    comboBox.setValue(firstValue);
}

void addEnumeratedParameterValueStringsToComboBox(LE::Utility::Span<char const *const> strings,
                                                  LE::Utility::Span<char const *const> shortStrings,
                                                  LE::Utility::Span<std::uint8_t const> menuOrder,
                                                  ComboBox &comboBox)
{
    LE_ASSERT_MSG(comboBox.numberOfItems() == 0, "ComboBox already filled.");
    LE_ASSERT(strings.size() == shortStrings.size());
    LE_ASSERT(strings.size() == menuOrder.size());
    for (auto const parameterValue : menuOrder)
        comboBox.addItem(parameterValue, strings[parameterValue], shortStrings[parameterValue]);
    comboBox.setValue(0);
}
} // namespace Detail

} // namespace LE::SW::GUI
