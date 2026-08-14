//------------------------------------------------------------------------------
//...mrmlj...cleanup...
//------------------------------------------------------------------------------
#include "gui.hpp"

#include "core/host_interop/plugin2Host.hpp" //...mrmlj...only for Plugin2HostPassiveInteropController::ParameterLabelGetter...
#include "gui/editor/spectrumWorxEditor.hpp"

#include "le/spectrumworx/engine/setup.hpp"
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

////////////////////////////////////////////////////////////////////////////////
//
// SkinLifetime
// ------------
//
//   What is left of ReferenceCountedGUIInitializationGuard once JUCE's lifetime
// is the shim's. The header has the whole account of what went and why.
//
////////////////////////////////////////////////////////////////////////////////

std::uint8_t SkinLifetime::liveEditors_(0);

SkinLifetime::SkinLifetime()
{
    /// \note The editor is built inside the shim's guiCreate(), under its
    /// MessageManagerLock, so this is the message thread -- which is what makes a
    /// plain counter enough. sw-show-ui reaches here from its own message thread.
    LE_ASSERT(isThisTheGUIThread() || !isGUIInitialised());

    if (liveEditors_++ != 0)
        return;

    JUCE_AUTORELEASEPOOL
    {
#if defined(_WIN32)
        juce::Process::setCurrentModuleInstanceHandle(&__ImageBase);
#endif // _WIN32
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
        /// \note Before the Theme goes: JUCE asserts if the default LookAndFeel
        /// is destroyed while still installed.
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);

        // Implementation note:
        //   We must manually reset the animator otherwise its timer becomes
        // orphaned when the juce::InternalTimerThread singleton is destroyed (so
        // it thinks it is still running even though its parent
        // juce::InternalTimerThread has been destroyed).
        //                                    (15.12.2011.) (Domagoj Saric)
        // \note The stopTimer() that followed is unreachable in JUCE 8 --
        // ComponentAnimator inherits Timer privately -- and the
        // InternalTimerThread it was defending against no longer exists.
        //                                    (28.07.2026.) (SW port)
        juce::Desktop::getInstance().getAnimator().cancelAllAnimations(false);

#if defined(_WIN32)
        LE_ASSERT(juce::Process::getCurrentModuleInstanceHandle() == &__ImageBase);
#endif // _WIN32

        Theme::destroySingleton();
    }

    /// \note The twenty `runDispatchLoopUntil(1)` calls that stood here are gone
    /// with the shutdown they were defending. They existed so that a queued
    /// creation of the *next* editor could not run between the reference check
    /// and `shutdownJuce_GUI()`; nothing is torn down here that a queued message
    /// could want. They were also inside `#if defined(__APPLE__) && !__LP64__`,
    /// so they had not run on any 64 bit build since 2013.
    ///                                       (02.08.2026.) (SW port)
}

////////////////////////////////////////////////////////////////////////////////
//
// warningMessageBox()
// -------------------
//
//    A thread safe nothrow implementation that can be safely called whenever
// and wherever from.
//
////////////////////////////////////////////////////////////////////////////////

/// \note Both of these were synchronous. JUCE 8 defaults JUCE_MODAL_LOOPS_PERMITTED
/// to 0, and a plugin has no business spinning a modal loop inside a host's
/// message thread anyway, so neither blocks now.
///
///   showNativeDialogBox is gone from JUCE 8 outright, and the
/// isGUIInitialised() / isThisTheGUIThread() dance that chose between it and the
/// JUCE box went with it: showMessageBoxAsync is safe to call from anywhere and
/// simply posts.
///                                       (28.07.2026.) (SW port)

namespace
{
/// \note One counter, not a flag: `[main-thread]` throughout, and a nested load
/// is not a case worth being wrong about.
unsigned int unattendedLoads{0};
} // anonymous namespace

UnattendedLoad::UnattendedLoad() { ++unattendedLoads; }
UnattendedLoad::~UnattendedLoad() { --unattendedLoads; }
bool UnattendedLoad::inProgress() { return unattendedLoads != 0; }

void warningMessageBox(std::string_view const title, std::string_view const message,
                       bool const /*canBlock*/)
{
    /// \note The invariant, and for now only asserted. \see UnattendedLoad.
    LE_ASSERT_MSG(!UnattendedLoad::inProgress(),
                  "A modal box in front of a host restoring a session.");

    //...mrmlj...canBlock no longer means anything and should come off the ~15
    //...mrmlj...call sites once they are ported.
    JUCE_AUTORELEASEPOOL
    {
        /// \note data(), not begin(): a std::string_view iterator is a `char
        /// const *` in libc++ and libstdc++, so juce::String's (pointer, length)
        /// constructor took it by accident. MSVC's is a class type, and the two
        /// failed conversions then collapsed the call into a third error saying
        /// showMessageBoxAsync does not take one argument.
        ///                                   (30.07.2026.) (SW port)
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               juce::String(title.data(), title.size()),
                                               juce::String(message.data(), message.size()));
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

/// \note `maxPathLength`, `path_t` and `getBinaryPath()` stood here and are
/// deleted. They existed only to locate the `SpectrumWorx.paths` file the note
/// below describes, and nothing had called them since 6.3 removed the two
/// `mapPathsFile()` overloads that did — the note said as much and then left the
/// helper behind. It was not harmless: `maxPathLength` had a `_WIN32` arm and an
/// `__APPLE__` arm and no third one, so on Linux it was a declaration with no
/// initialiser, and `path_t` was an array of `TCHAR`. `swDLLAddress`, whose only
/// writer was `getBinaryPath`, went with it — nothing ever read it.
///                                       (29.07.2026.) (SW port)

////////////////////////////////////////////////////////////////////////////////
//
// Global paths
// ------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note What used to live here: the plugin found its skin, its presets and its
/// documentation by mmapping a `SpectrumWorx.paths` file that the 2016 installer
/// wrote next to the binary. The skin is compiled into the binary now
/// (resources.hpp), the factory presets are too (factoryPresets.hpp), and the
/// installer is gone -- so the file, the two mapPathsFile() overloads that read
/// and rewrote it, and the on-disk resourceBitmap() that used it are all gone
/// with it.
///
/// \note What is left is the *user's* presets, which are the one thing that
/// genuinely has to be somewhere a user can find and back up. That is
/// `~/Documents/SpectrumWorx` and its platform equivalents, from
/// `sst::plugininfra::paths` -- the same answer Surge gives, arrived at by the
/// same code, rather than by the `userApplicationDataDirectory` that stood here
/// as a placeholder. On Linux it honours XDG.
///
/// \note **Answered on demand, not initialised.** There was an `initializePaths()`
/// beside these, and a `havePathsBeenInitialised()`, and an assert in each getter
/// saying "Not initialized." -- and nothing called the initialiser. Its only
/// caller had been the 2016 VST2/AU plugin class, which the CLAP replaced; the
/// scaffolding outlived it and went unnoticed for as long as
/// `presetBrowser.cpp` was in no target. The moment stage 8 put it in one,
/// pressing the presets button hit that assert.
///
///   So there is no initialisation step to forget. A function-local static is
/// computed on first use and is thread-safe by the language rather than by
/// convention, and the getters cannot be called too early because there is no
/// "too early".
///                                       (31.07.2026.) (SW port)
///
/// \note **No conversion at all, which is how issue #28 was closed.** This read
/// `juce::File( juce::String::fromUTF8( ... .u8string().c_str() ) )`, and the
/// `fromUTF8` was load bearing: sst-plugininfra's `filesystem` target carries
/// `-fno-char8_t` in its INTERFACE compile options and it propagates here, so
/// `std::u8string` *is* `std::string`, the `String( char8_t const * )` overload
/// that decodes UTF-8 does not exist to be chosen, and the plain `char const *`
/// constructor -- which widens every *byte* to a code point through
/// `CharPointer_ASCII` -- is what the call lands on instead.
///
///   ASCII is a fixed point of that widening, which is why it survived to a
/// release: it takes a home directory whose localised Documents folder is not
/// ASCII. Under `ja_JP.UTF-8` the XDG answer came back as its own mojibake --
/// `e3 83 89 ...` in, `c3 a3 c2 83 c2 89 ...` out -- which matched no directory
/// that existed, so the plugin helpfully created that one instead. The XDG lookup
/// is byte-exact and was never at fault; only the conversion was.
///
///   So there is no conversion here to get wrong. `sst::plugininfra::paths`
/// answers with an `fs::path` and that is what this hands back. The hazard has
/// not vanished -- it has moved to the edges that genuinely need a `juce::String`
/// or a JUCE file object, which is five functions in io/jucePath.hpp with a test
/// file to themselves.
///                                       (09.08.2026.) (SW port)

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

////////////////////////////////////////////////////////////////////////////////
//
// createUserPresetsFolder()
// -------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note Separate from presetsFolder(), and called when the browser opens rather
/// than from the getter. Asking where the presets go should not create anything
/// -- a test that wants to check the path should not leave a directory in
/// someone's Documents -- but a browser that opens on a folder which is not
/// there shows nothing and offers no way to make one.
///
/// \note Not asserted, and as of 07.08.2026 not reported either. A plugin does
/// not always get to write to the user's Documents folder: an AUv2 under macOS
/// app sandboxing may not, a locked-down or roaming home directory may not, and
/// a CI container certainly does not. None of those is a programming error, and
/// none of them should stop the editor opening -- the browser shows an empty
/// folder. What the caller does with `false` is the only signal left.
///                                       (31.07.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

bool createUserPresetsFolder()
{
    auto const &folder(presetsFolder());

    /// \note The `std::error_code` overloads throughout, never the throwing ones:
    /// this runs when the editor's preset button is pressed, on the host's
    /// message thread and behind the CLAP C entry points, where an escaping
    /// exception is undefined behaviour rather than a `false`. Which is also
    /// exactly what `juce::File::createDirectory()` answered with, so the shape
    /// of the function is unchanged.
    std::error_code error;
    if (std::filesystem::is_directory(folder, error))
        return true;

    /// \note create_directories(), plural: the root above it may not be there
    /// either, and JUCE's createDirectory() made the whole chain.
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
    /// \note There is nothing to animate on a machine with no displays, and
    /// asking anyway is fatal rather than merely pointless: JUCE's proxy
    /// component does
    /// `getDisplays().getDisplayForRect( ... )->scale` (juce_ComponentAnimator.cpp)
    /// and getDisplayForRect() returns null when the display list is empty. The
    /// try/catch below cannot help with a null dereference.
    ///
    ///   Reachable wherever a plugin is instantiated without a window server --
    /// offscreen rendering, CI, a scanning host on a headless box -- and it is
    /// ~ModuleUI that walks into it, so it takes only a loaded effect.
    ///                                       (29.07.2026.) (SW port)
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

/// \note Both of these asked a reference count of ours whether JUCE was up. It
/// was never a count of JUCE -- it was a count of *our editors*, which is a
/// different question and answered "no" for the whole of a plugin's life with no
/// window open, while the shim's MessageManager was running perfectly well. So
/// they ask JUCE, which is the thing being asked about, and `getInstanceWithoutCreating()`
/// is the call that does not bring one into existence to answer.
///                                           (02.08.2026.) (SW port)
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
    font.setHeight(11);
    return font;
}

void BackgroundImage::paint(juce::Graphics &graphics)
{
    graphics.setOpacity(Theme::singleton().settings().globalOpacity);
    paintImage(graphics, image());
}

Artwork const &BackgroundImage::image() const
{
    LE_ASSERT(pArtwork_);
    return *pArtwork_;
}

void BackgroundImage::setImage(Artwork const &artwork) { pArtwork_ = &artwork; }

BitmapButton::BitmapButton(juce::Component &parent, Artwork const &on, Artwork const &off,
                           juce::Colour const &overlayColourWhenOver, bool const toggled)
    : pOn_(&on), pOff_(&off), overOverlay_(overlayColourWhenOver)
{
    //...mrmlj...the settings bitmaps are currently broken...
    LE_ASSERT((on.getHeight() == off.getHeight()) || (&on == &resourceArtwork<SettingsOn>()));
    LE_ASSERT(on.getWidth() == off.getWidth());

    /// \note The 2013 comment here explained that juce::Button registered itself
    /// as a listener of its own Value, so a value set through automation or an
    /// LFO came back asynchronously and generated a bogus "value changed"; the
    /// fix was to cut the Button->Value->Button loop with
    /// getToggleStateValue().removeListener(this).
    ///
    ///   That is neither possible nor needed against JUCE 8. Button is no longer
    /// a Value::Listener -- it holds a private helper object (juce_Button.cpp:40)
    /// which this class cannot reach -- and that helper calls setToggleState
    /// with dontSendNotification for the click notification (juce_Button.cpp:58),
    /// on top of an early-out when the state has not actually changed
    /// (juce_Button.cpp:174). JUCE cuts the loop itself now.
    ///
    ///   Read from JUCE's sources rather than observed, so it is worth watching
    /// for doubled automation writes the first time this runs under a host.
    ///                                       (28.07.2026.) (SW port)

    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    // What setImages(resizeButtonNowToFitThisImage) used to do.
    setSize(off.getWidth(), off.getHeight());

    setClickingTogglesState(toggled);

    addToParentAndShow(parent, *this);
}

/// \note juce::ImageButton::getCurrentImage() had a third case, isOver(), which
/// asked for an over-image this class never supplied; getOverImage() then fell
/// back to the normal one. So hovering always drew `off`, and the hover is
/// expressed entirely through opacity and the overlay colour in paintButton().
Artwork const &BitmapButton::currentArtwork() const
{
    LE_ASSERT(pOn_ && pOff_);
    return (isDown() || getToggleState()) ? *pOn_ : *pOff_;
}

void BitmapButton::paintButton(juce::Graphics &graphics, bool isMouseOverButton, bool isButtonDown)
{
    if (!isEnabled())
        isMouseOverButton = isButtonDown = false;

    auto opacity(isButtonDown ? downOpacity()
                              : (isMouseOverButton ? overOpacity() : normalOpacity()));
    if (!isEnabled())
        opacity *= 0.3f; // as LookAndFeel_V2::drawImageButton dimmed a disabled one

    currentArtwork().draw(graphics, 0, 0, opacity,
                          isButtonDown ? downOverlay()
                                       : (isMouseOverButton ? overOverlay_ : normalOverlay()));
}

juce::Colour const &BitmapButton::downOverlay() { return juce::Colours::transparentWhite; }

juce::Colour const &BitmapButton::defaultOverOverlay() { return juce::Colours::transparentWhite; }

juce::Colour const &BitmapButton::normalOverlay() { return juce::Colours::transparentWhite; }

// Implementation note:
//   The built in JUCE ComboBox does not allow enough customization so we had to
// make our own. Just like the original ComboBox we use the PopupMenu class for
// the implementation. Because the juce::PopupMenu class is limited and/or too
// encapsulated we use here extremely dirty trickery to get to its internal
// details so as to be able to modify it according to our needs in manner that
// is easier and more efficient than that of the original juce::ComboBox (e.g.
// recreating the whole menu when the selection changes, holding duplicates of
// all items etc...) or to workaround bugs (e.g. the menu displaying in wrong
// places when in lower and/or right half of the screen)...
//                                            (17.03.2010.) (Domagoj Saric)

PopupMenu::PopupMenu() : menuHeight_(0), menuWidth_(0) {}

void PopupMenu::addItem(ItemID const newItemId, char const *const newItemText,
                        Artwork const *const icon, bool const enabled)
{
    juce::String text(newItemText);
    updateDimensionsForNewItem(text);
    items_.push_back({newItemId, std::move(text), icon, enabled, false, false, nullptr});
}

void PopupMenu::addSubMenu(PopupMenu &subMenu, char const *const name)
{
    LE_ASSERT(name);
    juce::String text(name);
    updateDimensionsForNewItem(text);
    items_.push_back({0, std::move(text), nullptr, true, false, false, &subMenu});
}

void PopupMenu::addSectionHeader(char const *const title)
{
    LE_ASSERT(title);
    juce::String text(title);
    updateDimensionsForNewItem(text);
    items_.push_back({0, std::move(text), nullptr, false, true, false, nullptr});
}

/// \note Nothing added to the measured height: a rule is a few pixels and the
/// height is only used to centre showCenteredAtRight().
void PopupMenu::addSeparator() { items_.push_back({0, {}, nullptr, false, false, true, nullptr}); }

void PopupMenu::updateDimensionsForNewItem(juce::String const &itemText)
{
    int idealWidth, idealHeight;
    Theme::singleton().Theme::getIdealPopupMenuItemSize(itemText, false, -1, idealWidth,
                                                        idealHeight);
    menuWidth_ = std::max<unsigned int>(menuWidth_, idealWidth + 4);
    menuHeight_ += idealHeight;
}

/// \note juce::PopupMenu reserves 0 for "the user dismissed the menu", so the
/// IDs handed to it are ours plus one. The 2016 code masked the top byte
/// instead, which cost it the top byte of the ID space.
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

void PopupMenu::showCenteredAtRight(juce::Component const &owner, OnChosen onChosen) const
{
    juce::Point<int> const ownerPosition(owner.getScreenPosition());
    unsigned int const ownerRight(ownerPosition.getX() + owner.getWidth());
    unsigned int const ownerVerticalMiddle(ownerPosition.getY() + (owner.getHeight() / 2));
    showAt(ownerRight + 6, ownerVerticalMiddle - (menuHeight_ / 2), 1,
           1 //...mrmlj...required with latest juce to actually get the menu on the right side...
           ,
           std::move(onChosen));
}

void PopupMenu::showCenteredBelow(juce::Component const &owner, OnChosen onChosen) const
{
    juce::Point<int> point(owner.localPointToGlobal(juce::Point<int>()));

    unsigned int const width(owner.getWidth());
    if (menuWidth_ > static_cast<unsigned int>(width))
    {
        point.setX(point.getX() - ((menuWidth_ - width) / 2));
    }

    showAt(point.getX(), point.getY(), width, owner.getHeight(), std::move(onChosen));
}

void PopupMenu::showAtScreenPosition(juce::Point<int> const screenPosition, OnChosen onChosen) const
{
    showAt(static_cast<unsigned int>(screenPosition.getX()),
           static_cast<unsigned int>(screenPosition.getY()), 1, 1, std::move(onChosen));
}

void PopupMenu::showAt(unsigned int const x, unsigned int const y, unsigned int const width,
                       unsigned int const height, OnChosen onChosen) const
{
    menuActive_ = true;
    /// \note `this` in the callback, where the flag used to be a static. A menu
    /// can outlive what opened it -- the host can close the editor while it is
    /// down -- but not the menu object itself: every caller owns its menu as a
    /// member and the editor dismisses whatever is open before it goes. See
    /// ~SpectrumWorxEditor().
    build(tickedIndex_)
        .showMenuAsync(juce::PopupMenu::Options()
                           .withTargetScreenArea(juce::Rectangle<int>(x, y, width, height))
                           .withMinimumWidth(width),
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

Artwork const *PopupMenuWithSelection::getSelectedItemIcon() const
{
    /// \note A reference into our own storage now, so it stays valid for as
    /// long as the item does. It used to point into juce::PopupMenu's internals.
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

ComboBox::ComboBox(juce::Component &parent, Artwork const &normalBackground,
                   Artwork const &selectedBackground)
    : normalBackground_(normalBackground), selectedBackground_(selectedBackground)
{
    LE_ASSERT(normalBackground.getWidth() == selectedBackground.getWidth());
    LE_ASSERT(normalBackground.getHeight() == selectedBackground.getHeight());

    setSizeFromImage(*this, normalBackground);
    addToParentAndShow(parent, *this);
}

void ComboBox::paint(juce::Graphics &graphics)
{
    paintImage(graphics, hasDirectFocus() ? selectedBackground_ : normalBackground_);

    graphics.setColour(juce::Colours::white);
    graphics.setFont(Theme::singleton().whiteFont());
    graphics.drawFittedText(getSelectedItemText(), 4, 2, getWidth() - 8,
                            normalBackground_.getHeight() - 3, juce::Justification::centred, 1,
                            0.1f);
}

/// \note \p onValueChanged runs later, on the message thread, and only if the
/// menu actually opened. The SafePointer is the point: a menu can outlive the
/// widget that opened it -- the host can close the editor while it is down --
/// and the 2016 code could not have this problem because the call blocked.
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

void Detail::paintTextButton(BitmapButton const &button, juce::Graphics &g,
                             unsigned int const textX, unsigned int const textY,
                             unsigned int const imageX, unsigned int const imageY,
                             bool const isMouseOverButton, bool const isButtonDown)
{
    g.setColour(juce::Colours::white);
    g.setFont(DrawableText::defaultFont());
    g.drawFittedText(button.getName(), textX, textY, button.getWidth() - textX, 11,
                     juce::Justification::horizontallyCentred, 1);

    button.currentArtwork().draw(
        g, static_cast<int>(imageX), static_cast<int>(imageY),
        isButtonDown
            ? BitmapButton::downOpacity()
            : (isMouseOverButton ? BitmapButton::overOpacity() : BitmapButton::normalOpacity()),
        isButtonDown ? BitmapButton::downOverlay()
                     : (isMouseOverButton ? BitmapButton::defaultOverOverlay()
                                          : BitmapButton::normalOverlay()));
}

LEDTextButton::LEDTextButton(juce::Component &parent, unsigned int const x, unsigned int const y,
                             char const *const text)
    : BitmapButton(parent, resourceArtwork<LEDOn>(), resourceArtwork<LEDOff>())
{
    setName(text);

    setBounds(x, y,
              getWidth() +
                  juce::GlyphArrangement::getStringWidthInt(DrawableText::defaultFont(), getName()),
              14);
}

void LEDTextButton::paintButton(juce::Graphics &g, bool const isMouseOverButton,
                                bool const isButtonDown)
{
    std::size_t const imageWidth(25);
    LE_ASSERT(currentArtwork().getWidth() == imageWidth);
    Detail::paintTextButton(*this, g, imageWidth, 3, 0, 0, isMouseOverButton, isButtonDown);
}

TextButton::TextButton(juce::Component &parent, unsigned int const x, unsigned int const y,
                       char const *const text)
{
    setName(text);

    juce::Font font(Theme::singleton().whiteFont());
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
        {{INTEGER_ALPHA(0.3), INTEGER_ALPHA(0.6)}, {INTEGER_ALPHA(1.0), INTEGER_ALPHA(0.8)}};
#undef INTEGER_ALPHA

    unsigned char const alpha(alphas[getToggleState()][isMouseOverButton]);

    juce::Font font(Theme::singleton().whiteFont());
    font.setHeight(static_cast<float>(height));

    g.setColour(juce::Colour((Theme::blueColour().getARGB() & 0x00FFFFFF) | (alpha << 24)));
    g.setFont(font);
    g.drawSingleLineText(getName(), 0, height);
}

Knob::Knob(juce::Component &parent, unsigned int const x, unsigned int const y,
           unsigned int const xMargin, unsigned int const yMargin)
{
    /// \note The Slider half of the same 2013 fix, and it went the same way.
    /// Slider::valueListener() never existed in stock JUCE -- it was an addition
    /// in the patched fork -- and JUCE 8's own Value listener already calls
    /// setValue with dontSendNotification (juce_Slider.cpp:433), which is what
    /// unhooking it was for. See the note in the BitmapButton constructor.
    ///                                       (28.07.2026.) (SW port)

    setBounds(x, y, xMargin, yMargin);
    //setTooltip             ( title                 );
    setSliderStyle(RotaryVerticalDrag);
    setTextBoxStyle(NoTextBox, true, 0, 0);
    //setPopupDisplayEnabled ( true, 0               ); //...mrmlj...for testing...
    setPopupMenuEnabled(true);
    setMouseDragSensitivity(800);
    addToParentAndShow(parent, *this);
}

void Knob::setupForParameter(char const *const title, Artwork const &filmStripToSizeFor,
                             param_type const defaultValue)
{
    setName(title);

    unsigned int const imageWidth(filmStripToSizeFor.getWidth());
    unsigned int const imageHeight(filmStripToSizeFor.getHeight() / numberOfKnobSubbitmaps);
    LE_ASSERT((filmStripToSizeFor.getHeight() % numberOfKnobSubbitmaps == 0));
    LE_ASSUME(imageWidth == imageHeight);

    unsigned int const xMargin(getWidth());
    unsigned int const yMargin(getHeight());

    unsigned int const width(imageWidth + xMargin);
    unsigned int const height(imageHeight + yMargin);
    setSize(width, height);

    setDoubleClickReturnValue(true, defaultValue);
}

void Knob::startedDragging() noexcept
{
    if (!Theme::singleton().settings().hideCursorOnKnobDrag)
        return;

    LE_ASSERT(juce::Desktop::getInstance().getNumMouseSources() == 1);
    // \note By value: getMainMouseSource() returns a prvalue in JUCE 8, and
    // enableUnboundedMouseMovement() is const, so a copy does the same work.
    auto mouseSource(juce::Desktop::getInstance().getMainMouseSource());

    /// \note Compared by value, not by address. In 2016 getMainMouseSource()
    /// returned a reference into Desktop's own list, so taking its address and
    /// comparing it with getDraggingMouseSource()'s pointer identified the
    /// source. JUCE 8 returns a prvalue -- MouseInputSource is a handle around a
    /// pimpl -- so `&mouseSource` is the address of the local copy above and
    /// never equals anything Desktop owns. The assertion could then only pass
    /// while nothing was dragging, i.e. it failed on every real knob drag.
    /// operator== compares the pimpl, which is the identity that was meant.
    ///                                       (29.07.2026.) (SW port)
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
    //mouseSource.enableUnboundedMouseMovement( false, !Theme::singleton().settings().hideCursorOnKnobDrag );

    //...mrmlj...neither of these works/helps because
    //...mrmlj...enableUnboundedMouseMovement() seems to handle it
    //...mrmlj...automatically (but imprecisely)...
    //juce::Desktop::setMousePosition( juce::Desktop::getLastMouseDownPosition() );
    //juce::Desktop::setMousePosition( this->localPointToGlobal( this->getBounds().getCentre() ) );
}

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

void Knob::paintFilmStrip(Artwork const &filmStrip, unsigned int const xMargin,
                          unsigned int const yMargin, juce::Graphics &graphics)
{
    LE_ASSERT(filmStrip.getWidth() == filmStrip.getHeight() / signed(numberOfKnobSubbitmaps));

    unsigned int const imageWidth(filmStrip.getWidth());
    unsigned int const imageHeight(imageWidth);
    unsigned int const pictureIndex(Math::convert<unsigned int>(
        (numberOfKnobSubbitmaps - 1) * juce::Slider::valueToProportionOfLength(getValue())));
    unsigned int const pictureOffset(pictureIndex * imageHeight);
    LE_ASSERT(pictureIndex < numberOfKnobSubbitmaps);
    LE_ASSERT(pictureOffset < static_cast<unsigned int>(filmStrip.getHeight()));

    filmStrip.drawScaled(
        graphics, {signed(xMargin), signed(yMargin), signed(imageWidth), signed(imageHeight)},
        {0, signed(pictureOffset), signed(imageWidth), signed(imageHeight)});
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
    Knob::setupForParameter(nullptr, resourceArtwork<EditorKnobStrip>(), defaultValue);

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

void EditorKnob::paint(juce::Graphics &graphics)
{
    Knob::paintFilmStrip(resourceArtwork<EditorKnobStrip>(), 0, 0, graphics);

    // For main knobs we display the value within the knob itself.
    LE_ASSERT(resourceArtwork<EditorKnobStrip>().getWidth() == 55);
    graphics.setColour(juce::Colours::white);
    {
        juce::Font font(Theme::singleton().whiteFont());
        font.setHeight(11);
        graphics.setFont(font);
    }

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
    ParameterID::Global parameterID;
    parameterID.index = parameterIndex_;
    char const *const pUnit(
        Plugin2HostPassiveInteropController::ParameterLabelGetter()(parameterID, nullptr));

    graphics.drawFittedText(juce::String(&valueString[0]) + pUnit, 14, 16, 28, 24,
                            juce::Justification::centred, 1, 0.1f);
}

/// \note EditorKnob::valueChanged() lives in spectrumWorxEditor.cpp. It is the
/// only thing in this file that instantiates
/// SpectrumWorxEditor::globalParameterChanged<>, which reaches host() and so
/// needs the complete SpectrumWorx -- and that is what used to drag the whole
/// 2016 VST2 plugin class, and the deleted VST 2.4 SDK behind it, into the
/// widget layer. Everything else here needs the editor declared, not defined.
///                                       (28.07.2026.) (SW port)

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
/// area rather than the editor. \see SpectrumWorxEditor::MainArea.
SpectrumWorxEditor &EditorKnob::editor() { return SpectrumWorxEditor::fromChild(*this); }

TitledComboBox::TitledComboBox(juce::Component &parent, unsigned int const x, unsigned int const y,
                               char const *const title)
    : ComboBox(parent, resourceArtwork<SettingsCombo>(), resourceArtwork<SettingsComboOn>()),
      title_(title, 4, 0, getWidth() - 8, 13, juce::Justification::left)
{
    TitledComboBox::setBounds(x, y, getWidth(), getHeight() + 15);
}

void TitledComboBox::paint(juce::Graphics &graphics)
{
    if (!hasValidSelection())
        return;
    graphics.setOrigin(0, +12);
    ComboBox::paint(graphics);
    graphics.setOrigin(0, -12);
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
                                                  ComboBox &comboBox)
{
    LE_ASSERT_MSG(comboBox.numberOfItems() == 0, "ComboBox already filled.");
    ComboBox::value_type parameterValue(0);
    while (strings)
    {
        comboBox.addItem(parameterValue, strings.front());
        ++parameterValue;
        strings.advance_begin(1);
    }
    comboBox.setValue(0);
}
} // namespace Detail

////////////////////////////////////////////////////////////////////////////////
// Theme
////////////////////////////////////////////////////////////////////////////////

/// \note Theme itself now lives in theme.hpp/theme.cpp -- it is a
/// LookAndFeel_V2 there, not a LookAndFeel, and it loads its fonts out of the
/// binary instead of registering them with the operating system. What stays
/// here are the two LFO-update policy queries, which were static members of
/// Theme only because they read Theme::settings(): they ask about a
/// ModuleControlBase and a ModuleUI, neither of which a LookAndFeel should
/// know exist, and their presence is what stopped Theme being separable.
///                                       (28.07.2026.) (SW port)

bool shouldUpdateLFOControl(ModuleControlBase const &control)
{
    Theme::LFOUpdateBehaviour const lfoUpdateBehaviour(Theme::settings().lfoUpdateBehaviour);
    return (lfoUpdateBehaviour == Theme::Always) ||
           (lfoUpdateBehaviour == Theme::WhenControlActive && control.isActive()) ||
           (lfoUpdateBehaviour == Theme::WhenControlSelected &&
            Detail::hasDirectFocus(control.widget()));
}

} // namespace LE::SW::GUI

/// \note Two weak `extern "C"` definitions of `strnlen` and `wcsnlen` stood
/// here, from 2013, for an OS X 10.6 whose libc had neither. They went on
/// 05.08.2026: the deployment target is 10.15, nothing in this tree calls
/// either, and being *weak* they lost to libc's own strong definitions anyway --
/// so what they had been doing since 2016 was occupying two symbols in every
/// macOS build. The `!__LP64__` guard that once narrowed them to 32-bit was
/// commented out, which is how they came to be compiled at all.
