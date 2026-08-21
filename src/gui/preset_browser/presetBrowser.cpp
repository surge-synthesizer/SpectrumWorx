////////////////////////////////////////////////////////////////////////////////
///
/// presetBrowser.cpp
/// -----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "presetBrowser.hpp"

#include "configuration/versionConfiguration.hpp"
#include "gui/editor/spectrumWorxEditor.hpp"
#include "io/jucePath.hpp"

#include "le/parameters/uiElements.hpp"
#include "le/spectrumworx/factoryPresets.hpp"
#include "le/spectrumworx/presetStorage.hpp"
#include "le/spectrumworx/presets.hpp"
#include "le/utility/countof.hpp"
#include "le/utility/tchar.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <filesystem>
#include <system_error>

#include "le/utility/assert.hpp"

namespace LE::SW::GUI
{

namespace
{
typedef juce::String::CharPointerType::CharType char_t;

/// \note The length went with the byte arithmetic in refreshUserDirectory();
/// `fs::path::extension()` compares the whole thing.
static char_t const presetExtension[] = _T( ".swp" );
} // namespace

#pragma warning(push)
#pragma warning(disable : 4355) // 'this' used in base member initializer list.

PresetBrowser::PresetBrowser()
    : PanelBackground(Browser),
      /// \note 54 x 22 where the artwork was 48 x 16: the extra six is the room
      /// a lit button's halo needs, and the pills land where they always did
      /// because the three positions below moved in by the same three pixels.
      ///                                       (18.08.2026.)
      save_(*this, "Save", 81, 33, false), saveAs_(*this, "Save as", 81, 33, false),
      delete_(*this, "Delete", 81, 33, false),
      browseArrow_(*this, ArrowStyle::stepWidth, ArrowStyle::stepHeight, false,
                   ColourMap::MouseOverGlow),
      upFolder_(*this, GlyphButton::Glyph::FolderUp),
      userPresets_(*this, GlyphButton::Glyph::User, true /*toggles*/),
      jogPrevious_(*this, GlyphButton::Glyph::JogPrevious),
      jogNext_(*this, GlyphButton::Glyph::JogNext), ignoreSelectionChange_(false),
      addOneRow_(false), newPresetPending_(false), dirtyCommentPresetIndex_(-1)
{
    listBox_.setModel(this);

    setSizeFromPanel();

    /// \note All three start disabled and refresh() decides Save-As from there.
    /// It used to be settled once, here, before the browser had ever listed
    /// anything, and nothing re-enabled it: Save-As was unclickable for the
    /// life of the plugin.
    ///                                       (01.08.2026.) (SW port)
    save_.setEnabled(false);
    saveAs_.setEnabled(false);
    delete_.setEnabled(false);
    save_.addListener(this);
    saveAs_.addListener(this);
    delete_.addListener(this);

    browseArrow_.addListener(this);

    upFolder_.addListener(this);
    userPresets_.addListener(this);
    jogPrevious_.addListener(this);
    jogNext_.addListener(this);

    /// \note Here rather than at the first save: an empty browser with no way
    /// to make a folder is not a usable answer to "you have no presets yet".
    GUI::createUserPresetsFolder();

    /// \note Where it was last left, and the list of factory banks the first
    /// time, rather than the user's folder. A new installation's user folder is
    /// empty, and a browser that opens on nothing while 303 presets sit in the
    /// binary is the state this was reported in.
    restoreLastPlace();

    browseArrow_.setTopLeftPosition(261, 15);
    save_.setTopLeftPosition(21, 45);
    saveAs_.setTopLeftPosition(21 + 83, 45);
    delete_.setTopLeftPosition(21 + 83 + 83, 45);

    /// \note The navigation row, in the gap the panel already had between the
    /// Save buttons and the list. The four x positions are issue #44's mock-up
    /// measured off it: up against the list's left frame, the user centred on
    /// the panel, and the jog's two halves abutting against its right frame.
    upFolder_.setTopLeftPosition(10, GlyphStyle::rowTop);
    userPresets_.setTopLeftPosition(131, GlyphStyle::rowTop);
    jogPrevious_.setTopLeftPosition(233, GlyphStyle::rowTop);
    jogNext_.setTopLeftPosition(jogPrevious_.getRight() + 1, GlyphStyle::rowTop);

    listBox_.setBounds(17, 119, getWidth() - 33, 351);
    comment().setBounds(12, 484, getWidth() - 25, 43);

    addChildComponent(&presetNameEditBox_);
    presetNameEditBox_.setAlwaysOnTop(true);
    presetNameEditBox_.setFont(Theme::singleton().labelFont());
    presetNameEditBox_.addListener(this);

    listBox_.setOpaque(false);
    listBox_.setRowHeight(24);
    /// \note `getViewport()->setScrollBarThickness( 12 )` stood here. The width
    /// is Theme's now, so the list and the comment box below it get the same one
    /// -- this was the only bar that had been told, and the comment box's was
    /// JUCE's eighteen. \see Theme::getDefaultScrollbarWidth(), issue #90.
    ///                                       (17.08.2026.)

    comment().setMultiLine(true);
    comment().setReturnKeyStartsNewLine(true);
    comment().setPopupMenuEnabled(true);
    comment().setIndents(4, 0);
    comment().addListener(this);

    takeColours();
    {
        juce::Font font(Theme::singleton().Theme::getPopupMenuFont());
        font.setHeight(17);
        comment().setFont(font);
    }

    //  Required so that a preset can be deselected by clicking anywhere outside
    // the file list box.
    this->setWantsKeyboardFocus(true);

    // Implementation note:
    //   We enable the comment box only when a preset is selected (in other
    // words to create a new preset with a comment you first need to create a
    // preset and then add a comment to the new/existing preset).
    //                                        (27.05.2010.) (Domagoj Saric)
    comment().setEnabled(false);
    comment().setInputRestrictions(PresetHeader::maxCommentLength - 1);

    LE_ASSERT(!save_.getMouseClickGrabsKeyboardFocus());
    LE_ASSERT(!saveAs_.getMouseClickGrabsKeyboardFocus());
    LE_ASSERT(!delete_.getMouseClickGrabsKeyboardFocus());

    addToParentAndShow(*this, comment());
    addToParentAndShow(*this, listBox_);

    /// \note `OwnedWindow<PresetBrowser>::attach()` stood here and made this a
    /// desktop window owned by the editor's. The editor parents and positions it
    /// now -- see SpectrumWorxEditor::openOverlay().
    ///                                       (01.08.2026.) (SW port)
}

#pragma warning(pop)

PresetBrowser::Place &PresetBrowser::lastPlace()
{
    LE_ASSERT(isThisTheGUIThread());
    static Place place;
    return place;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Validated rather than trusted. A remembered folder is a path from a
/// previous run of the host and the user may have moved or deleted it since, and
/// a remembered bank is a name that a later build need not still ship. Either
/// way the answer is the top of the factory tree, which is the one listing that
/// is always there and never empty.
///
////////////////////////////////////////////////////////////////////////////////

void PresetBrowser::restoreLastPlace()
{
    auto const &place(lastPlace());

    switch (place.location)
    {
    case Location::Factory:
        return setFactoryBank(place.factoryBank);

    case Location::User:
        if (std::error_code error; std::filesystem::is_directory(place.folder, error))
            return setNewFolder(place.folder);
        break;
    }

    setFactoryBank({});
}

PresetBrowser::~PresetBrowser()
{
    //...mrmlj...fade out does not work for 'on desktop components'
    //this->fadeOutComponent( 600, 0, 0, 0.2f );
    //juce::Point<int> const centre( this->getBounds().getCentre() );
    //juce::Desktop::getInstance().getAnimator().animateComponent( this, juce::Rectangle<int>( centre, centre ), 0, 600, true, 0, 0 );
    lastPlace() = Place{location_, factoryBank_, currentDirectory_};
}

/// \note A factory bank is compiled into the binary, so there is nothing to save
/// into, rename or delete there.
bool PresetBrowser::enablePresetSaving() const { return location_ == Location::User; }

////////////////////////////////////////////////////////////////////////////////
//
// PresetBrowser::selectedPresetData()
// -----------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The one place the two sources meet. Both hand back the same thing --
/// a writable, NUL-terminated buffer -- so everything downstream of here is the
/// same code for a factory preset and for the user's own.
///
////////////////////////////////////////////////////////////////////////////////

Preset::InMemoryPreset PresetBrowser::selectedPresetData() const
{
    auto const &item(selectedItem());
    if (inFactory())
        return FactoryPresets::load(factoryBank_.toStdString(), item.name.toStdString());
    return readPresetFile(selectedFile());
}

void PresetBrowser::presetSelectionChanged()
{
    Item const &item(selectedItem());
    if (item.isDirectory())
    {
        save_.setEnabled(false);
        delete_.setEnabled(false);
        return;
    }

    bool const enablePresetSaving(this->enablePresetSaving());

    save_.setEnabled(enablePresetSaving);
    delete_.setEnabled(enablePresetSaving);

    auto const presetData(selectedPresetData());
    bool const succeeded(presetData &&
                         editor().loadPreset(presetData.get(), editor().ignoreExternalSample(),
                                             originalComment_, item.name));

    if (!succeeded)
    {
        Preset::reportPresetLoadingError();
        return;
    }

    comment().setText(originalComment_, false);
    comment().setEnabled(enablePresetSaving);
    LE_ASSERT(comment().getWantsKeyboardFocus() || !comment().isEnabled());
}

unsigned int PresetBrowser::selectedIndex() const
{
    int const index(listBox_.getLastRowSelected());
    LE_ASSERT_MSG(index >= 0, "Nothing selected");
    return index;
}

PresetBrowser::Item const &PresetBrowser::item(unsigned int const index) const
{
    Item const &item(files_.getReference(index));
    /// \note Hoisted, and ignored in a shipping build: LE_ASSERT does not
    /// evaluate its argument under NDEBUG, so the code this fills in is unread
    /// there. The `std::error_code` overload all the same -- the throwing one
    /// would be reachable from a paint.
    std::error_code error;
    LE::Utility::ignoreUnused(error);
    LE_ASSERT((location_ != Location::User) || item.isDirectory() ||
              std::filesystem::exists(file(index), error));
    return item;
}

PresetBrowser::Item const &PresetBrowser::selectedItem() const { return item(selectedIndex()); }

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The row's file, or an empty path when the row does not have one. See
/// selectedPresetData(), which is what the load path uses.
///
/// \note The two conditions were `LE_ASSERT`s, which a shipped build does not
/// compile -- so what stood between a factory bank and a delete or an overwrite
/// was that the buttons were disabled. Enablement is a statement about the
/// interface and this is a statement about the file system, and the two are only
/// as equal as every path that sets one of them. Now they cannot disagree:
/// anywhere but `User` there is no file to return, and every caller of this
/// treats an empty one as nothing to do.
///                                           (08.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

fs::path PresetBrowser::file(unsigned int const index) const
{
    if (location_ != Location::User)
        return {};

    Item const &item(this->item(index));
    if (item.isDirectory())
        return {};

    return currentDirectory_ / LE::IO::juceStringToPath(item.name + presetExtension);
}

fs::path PresetBrowser::selectedFile() const { return file(selectedIndex()); }

PresetBrowser::Item const *PresetBrowser::findPreset(juce::String const &presetName) const
{
    return std::find_if(files_.begin(), files_.end(),
                        [&](Item const &item) { return presetName == item.name; });
}

void PresetBrowser::listBoxItemDoubleClicked(int const row, juce::MouseEvent const &)
{
    Item const &item(this->item(row));
    switch (item.kind)
    {
    case Item::Kind::Folder:
        if (inFactory())
            return setFactoryBank(factoryBank_.isEmpty() ? item.name
                                                         : factoryBank_ + "/" + item.name);
        return setNewFolder(currentDirectory_ / LE::IO::juceStringToPath(item.name));

    case Item::Kind::Preset:
        /// \note Renaming is the double-click action on a preset, and a factory
        /// bank is in the binary.
        if (!inFactory())
            showFilenameEditBox(item.name, listBox_.getLastRowSelected());
        return;
    }
}

void PresetBrowser::paintListBoxItem(int const rowNumber, juce::Graphics &graphics, int const width,
                                     int const height, bool const rowIsSelected)
{
    /// \note A row past the end of the listing is not something to assert on:
    /// JUCE paints whatever row number the recycled RowComponent last held
    /// (RowComponent::paint, juce_ListBox.cpp) and updateContents() stamps one
    /// on every component it keeps, not just on the ones the model has rows for.
    /// The extras sit below the viewed component and are normally clipped away
    /// -- but the editor is drawn through a scale transform, and the software
    /// renderer (which is what a juce::Image gets everywhere except macOS)
    /// rounds a transformed clip outwards, leaving the first one past the end a
    /// sliver to paint in. Drawing nothing for it is the whole job.
    ///
    /// This was an `#ifdef __APPLE__` guard for the other way the same thing
    /// happens: double-clicking the last preset brings up the filename edit box
    /// and adds the bogus row (addOneRow_) that then gets painted.
    ///                                        (25.11.2011.) (Domagoj Saric)
    if (rowNumber >= files_.size())
        return;

    if (rowIsSelected)
        graphics.fillAll(Theme::singleton().Theme::findColour(
            juce::DirectoryContentsDisplayComponent::highlightColourId));

    Item const &item(this->item(rowNumber));

    bool const isDirectory(item.isDirectory());

    unsigned int const x(isDirectory ? 24 : 6);

    /// \note Drawn rather than asked for. This was
    /// `Theme::getDefaultFolderImage()`, which JUCE 8 answers with a Drawable,
    /// may answer with nothing at all, and draws in its own look and feel's
    /// colours rather than in this skin's -- so the one mark in the panel that
    /// did not follow the palette was the folder. \see GlyphPainter.
    if (isDirectory)
        GlyphPainter::paintFolder(graphics,
                                  juce::Rectangle<float>(3.0f, 0.0f, static_cast<float>(x) - 6.0f,
                                                         static_cast<float>(height)),
                                  ColourMap::getColour(ColourMap::Accent));

    graphics.setColour(Theme::singleton().Theme::findColour(
        juce::DirectoryContentsDisplayComponent::textColourId));
    graphics.setFont(height * 0.7f);

    // The 4 on the right is issue #76's margin: a name as wide as the list had
    // its last glyph against the scrollbar.
    graphics.drawFittedText(item.name, x, 0, width - x - 6, height,
                            juce::Justification::centredLeft, 1);
}

void PresetBrowser::deleteKeyPressed(int /*lastRowSelected*/) noexcept {}

void PresetBrowser::returnKeyPressed(int /*lastRowSelected*/) noexcept {}

void PresetBrowser::textEditorTextChanged(juce::TextEditor &editor)
{
    LE_ASSERT((&editor == &this->presetNameEditBox_) || (&editor == &this->comment()));
    if (&editor == &comment())
        dirtyCommentPresetIndex_ = listBox_.getLastRowSelected();
}

////////////////////////////////////////////////////////////////////////////////
//
// PresetBrowser::textEditorReturnKeyPressed()
// -------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note This is the save path the plan calls out: it asked the user two
/// questions -- "overwrite?" and "retry?" -- and read the answers as return
/// values, one of them from inside a `while` condition. Neither dialog can
/// answer in place under JUCE 8, so both are inverted into continuations.
///
///   Each one captures a `SafePointer` rather than `this`. The browser is an
/// owned window and the user can shut it while a dialog is up, which under the
/// old synchronous code was impossible and is now the normal way to dismiss one.
///
////////////////////////////////////////////////////////////////////////////////

void PresetBrowser::textEditorReturnKeyPressed(juce::TextEditor &editor)
{
    LE_ASSERT(listBox_.getViewport()->isVerticalScrollBarShown() ||
              listBox_.getViewport()->isHorizontalScrollBarShown());
    LE_ASSERT(&editor == &this->presetNameEditBox_);

    // Implementation note:
    //   We simply do not accept empty input. The user either has to cancel the
    // operation or enter something meaningful.
    //                                        (14.12.2009.) (Domagoj Saric)
    juce::String const userEntry(editor.getText());
    if (userEntry.isEmpty())
        return;

    fs::path const targetFile(currentDirectory_ /
                              LE::IO::juceStringToPath(userEntry + presetExtension));

    juce::Component::SafePointer<PresetBrowser> const self(this);

    /// \note One `error_code` for the two probes below rather than one each: both
    /// ask the same question of the same path, neither reads the answer, and the
    /// point of it is only that they cannot throw.
    std::error_code error;

    if (newPresetPending_)
    {
        auto const save([self, userEntry, targetFile] {
            if (!self)
                return;
            self->newPresetPending_ = false;
            self->hideFilenameEditBox();
            self->saveCurrentPreset(userEntry, targetFile);
        });

        if (!std::filesystem::exists(targetFile, error))
            return save();

        askForOverwrite([save, targetFile](bool const overwrite) {
            if (!overwrite)
                return;
            std::error_code ignored;
            std::filesystem::remove(targetFile, ignored);
            save();
        });
        return;
    }

    fs::path const sourceFile(selectedFile());
    /// \note An empty source is a row with no file behind it -- a factory
    /// preset, or the root's two section entries. Nothing to rename.
    ///
    /// \note `==` on two paths is a lexical compare where `juce::File`'s was
    /// case-insensitive off Linux. Both of these are built from the same
    /// `currentDirectory_` and a name the file system just reported, so the
    /// comparison is between two spellings of the same origin -- which is the
    /// case lexical equality answers correctly.
    if (sourceFile.empty() || (sourceFile == targetFile))
        return;

    auto const rename([self, sourceFile, targetFile, userEntry] {
        if (self)
            self->renameTo(sourceFile, targetFile, userEntry);
    });

    if (!std::filesystem::exists(targetFile, error))
        return rename();

    askForOverwrite([rename](bool const overwrite) {
        if (overwrite)
            rename();
    });
}

/// \note Was the body of a `while` loop whose condition asked the user whether
/// to retry. It calls itself from the dialog's callback instead -- one live
/// attempt at a time, and the stack unwinds between them.
void PresetBrowser::renameTo(fs::path const &sourceFile, fs::path const &targetFile,
                             juce::String const &newName)
{
    /// \note `rename()`, which is what `juce::File::moveFileTo` came down to for
    /// two paths on the same volume -- and both of these are children of
    /// `currentDirectory_`, so they always are.
    std::error_code error;
    std::filesystem::rename(sourceFile, targetFile, error);
    if (!error)
    {
        hideFilenameEditBox();
        refreshAndSelectPreset(newName);
        return;
    }

    juce::Component::SafePointer<PresetBrowser> const self(this);
    GUI::warningOkCancelBox(_T( "Error writing." ), _T( "Retry?" ),
                            [self, sourceFile, targetFile, newName](bool const retry) {
                                if (retry && self)
                                    self->renameTo(sourceFile, targetFile, newName);
                            });
}

void PresetBrowser::textEditorEscapeKeyPressed(juce::TextEditor &editor)
{
    if (&editor == &this->presetNameEditBox_)
    {
        hideFilenameEditBox();
    }
    else
    {
        LE_ASSERT(&editor == &this->comment());
        comment().setText(originalComment_, false);
    }
}

void PresetBrowser::textEditorFocusLost(juce::TextEditor &editor)
{
    if (&editor == &this->presetNameEditBox_)
    {
        hideFilenameEditBox();
    }
    else
    {
        LE_ASSERT(&editor == &this->comment());
        // Implementation note:
        //   No need to do anything here: already handled in focusLost().
        //                                    (15.03.2010.) (Domagoj Saric)
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// PresetBrowser::saveDirtyComment()
// ---------------------------------
//
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   Focus change monitoring and handling logic failed to work as desired
// when a juce::TextEditor::Listener was used to 'listen' to the comment
// 'TextEditor' because JUCE sends notifications through listeners
// asynchronously so a focus lost notification was received after the
// ListBoxModel::selectedRowsChanged() notification was received when all
// the required information on the current preset and a possibly non-saved
// comment are lost. For this reason a little utility class was used that simply
// derives from juce::TextEditor and override its focusLost() method to handle
// it directly (and synchronously). This approach however required RTTI on OSX
// (because JUCE converts between TextEditorListener and Component instance
// pointers with dynamic_cast) so the "dirty comment tracking" technique is
// currently used.
//                                            (16.12.2010.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

void PresetBrowser::saveDirtyComment()
{
    if (dirtyCommentPresetIndex_ < 0)
        return;

    // A factory preset's comment is in the binary; there is nothing to write to.
    if (location_ != Location::User)
    {
        dirtyCommentPresetIndex_ = -1;
        return;
    }

    fs::path const dirtyPreset(this->file(dirtyCommentPresetIndex_));

    dirtyCommentPresetIndex_ = -1;

    if (std::error_code error; std::filesystem::is_regular_file(dirtyPreset, error))
    {
        juce::String const newComment(comment().getText());

        //...assert that the file/preset on disk is the same as the one in selectedPreset...
        /// \note A read-modify-write of the header alone: the document that goes
        /// back is the one that came off disk, so a 2.x preset stays 2.x and
        /// only its Comment moves. Editing a comment is not a reason to rewrite
        /// somebody's file into a grammar the plugin they had it from cannot
        /// read.
        std::string newPresetData;
        {
            auto const pPresetData(readPresetFile(dirtyPreset));
            if (!pPresetData)
                return;
            PresetHeader const presetHeader(
                std::string_view(newComment.toRawUTF8(), newComment.getNumBytesAsUTF8()));
            Preset preset;
            if (!preset.loadFrom(pPresetData.get()))
                return;
            preset.setHeader(presetHeader);
            newPresetData = preset.saveTo();
        }
        writePresetFile(dirtyPreset, newPresetData.c_str(),
                        static_cast<unsigned int>(newPresetData.size() + 1));
    }
}

void PresetBrowser::saveCurrentPreset(juce::String const &presetName, fs::path const &targetFile)
{
    std::error_code error;
    bool const shouldRefresh(!std::filesystem::exists(targetFile, error));

    originalComment_ = comment().getText();
    editor().savePreset(targetFile, editor().ignoreExternalSample(), originalComment_);

    if (shouldRefresh)
        refreshAndSelectPreset(presetName);
}

void PresetBrowser::buttonClicked(juce::Button *const pButton)
{
    if (pButton == &save_)
    {
        /// \note Asked rather than assumed. These two used to run on the
        /// strength of the button being enabled; `file()` says why that is not
        /// the same thing.
        fs::path const target(selectedFile());
        if (!target.empty())
            saveCurrentPreset(selectedItem().name, target);
    }
    else if (pButton == &delete_)
    {
        fs::path const target(selectedFile());
        if (!target.empty())
        {
            std::error_code ignored;
            std::filesystem::remove(target, ignored);
            refresh();
            delete_.setEnabled(false);
            deselectAllRows();
        }
    }
    else if (pButton == &saveAs_)
    {
        ////////////////////////////////////////////////////////////////////////
        ///
        /// \note A name the user is *offered*, so it is allowed to be imperfect;
        /// what it is not allowed to do is take forever, and it did.
        ///
        ///   The suffix used to be measured by hand -- `1 + 1 + 2 + counter/100
        /// + 1` -- and written with `snprintf` straight into `juce::String`'s
        /// buffer. The arithmetic is off by one number: at the round where
        /// `counter` reads 99 the width is still computed for two digits, and
        /// the three-digit " (100)" is truncated to " (100" with the closing
        /// bracket dropped. The name that comes out is not the name the next
        /// round makes either, so the search stops converging and spins -- with
        /// the user's finger on Save As and a hundred presets of the same name,
        /// which is a real state for a bank built by duplicating one preset.
        ///
        ///   Built as a string now, with a bound. Running out of numbers offers a
        /// name that is already taken, which is the same thing this offered on
        /// the very first round when nothing collided: the edit box is up and the
        /// user types over it.
        ///                                   (08.08.2026.) (SW port)
        ///
        ////////////////////////////////////////////////////////////////////////

        constexpr unsigned int mostNamesToTry{1000};

        juce::String const baseName(editor().currentProgramName());
        juce::String newPreset(baseName);

        for (unsigned int counter(1);
             (findPreset(newPreset) != files_.end()) && (counter <= mostNamesToTry); ++counter)
            newPreset = baseName + juce::String::formatted(" (%02u)", counter);

        // Implementation note:
        //   Creating a new preset is done asynchronously (as it waits for
        // user input) so we return here immediately (delegating further
        // processing to the textEditorReturnKeyPressed() callback) and skip
        // the comment().moveKeyboardFocusToSibling() call as this would
        // cause the popup preset name edit box to disappear immediately.
        //                                    (17.12.2009.) (Domagoj Saric)

        newPresetPending_ = true;

        showFilenameEditBox(newPreset, PresetBrowser::getNumRows());
        return;
    }
    else if (pButton == &upFolder_)
    {
        goToParent();
    }
    else if (pButton == &userPresets_)
    {
        /// \note The toggle state is already the new one -- juce::Button flips
        /// it before it tells its listeners -- so this reads the answer rather
        /// than deciding it, and updateNavigation() will agree with it.
        if (userPresets_.getToggleState())
            setNewFolder(GUI::presetsFolder());
        else
            setFactoryBank({});
    }
    else if (pButton == &jogPrevious_)
    {
        stepPreset(-1);
    }
    else if (pButton == &jogNext_)
    {
        stepPreset(+1);
    }
    else
    {
        LE_ASSERT(pButton == &browseArrow_);
        /// \note Asynchronous, and the chooser is a member: JUCE 8 has no
        /// browseForDirectory(), and launchAsync() reports through a callback
        /// that the chooser has to still be alive to make.
        ///
        /// \note Opened where the list is, and `currentDirectory_` is only that
        /// at `User` -- from the root or a factory bank it is stale or empty,
        /// which starts a native dialog wherever that resolves to.
        ///                                   (08.08.2026.) (SW port)
        ///
        /// \note **One of the two places in `src/` that may name `juce::File`**,
        /// the other being the editor's audio file chooser;
        /// tests/checkNoJuceFile.cmake allowlists both. `juce::FileChooser` is
        /// handed one and answers with one, so the conversion happens on the way
        /// in and on the way out and the type reaches nothing else --
        /// `setNewFolder()` takes an `fs::path`.
        std::error_code error;
        bool const haveCurrent((location_ == Location::User) &&
                               std::filesystem::is_directory(currentDirectory_, error));
        juce::File const startFrom(
            LE::IO::pathToJuceFile(haveCurrent ? currentDirectory_ : GUI::presetsFolder()));
        folderChooser_ = std::make_unique<juce::FileChooser>(
            "Please select a folder with SpectrumWorx presets...", startFrom);
        folderChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [self = juce::Component::SafePointer<PresetBrowser>(this)](
                juce::FileChooser const &chooser) {
                auto const chosen(LE::IO::juceFileToPath(chooser.getResult()));
                /// \note A cancelled chooser reports an
                /// empty file, and a directory that has
                /// gone away between the dialog opening
                /// and closing reports one that is not
                /// there.
                std::error_code ignored;
                if (self && std::filesystem::is_directory(chosen, ignored))
                    self->setNewFolder(chosen);
            });
    }

    // Implementation note:
    //   Force the comment box to loose focus when a button is clicked (we do
    // not want the buttons to do so automatically because this messes up our
    // other related logic).
    //                                        (27.05.2010.) (Domagoj Saric)
    comment().moveKeyboardFocusToSibling(false);
}

void PresetBrowser::showFilenameEditBox(juce::String const &presetName, unsigned int atRow)
{
    LE_ASSERT(presetNameEditBox_.getParentComponent() == this);

    listBox_.scrollToEnsureRowIsOnscreen(atRow);

    juce::Rectangle<int> rowRect(
        listBox_.getRowPosition(atRow, true).translated(listBox_.getX(), listBox_.getY()));
    rowRect.setTop(rowRect.getY() - 3);
    rowRect.setBottom(rowRect.getBottom() + 2);
    //rowRect.setRight ( rowRect.getRight() - 1 );//...mrmlj...for testing...
    rowRect.setLeft(rowRect.getX() - 2);
    int const verticalOverflow(rowRect.getBottom() - listBox_.getBounds().getBottom());
    if (verticalOverflow > 0)
    {
        rowRect.setPosition(rowRect.getX(), rowRect.getY() - verticalOverflow);
        addOneRow(true);
        ++atRow;
    }

    presetNameEditBox_.setSelectAllWhenFocused(true);
    presetNameEditBox_.setBounds(rowRect);
    presetNameEditBox_.setText(presetName);
    presetNameEditBox_.setVisible(true);
    presetNameEditBox_.grabKeyboardFocus();
    presetNameEditBox_.setSelectAllWhenFocused(false);

    listBox_.updateContent();
    listBox_.scrollToEnsureRowIsOnscreen(atRow);
}

void PresetBrowser::hideFilenameEditBox()
{
    // Implementation note:
    //   The edit box should not be hidden if it lost focus due to a modal
    // component pop up (like a message box).
    //                                        (22.03.2010.) (Domagoj Saric)
    if (!presetNameEditBox_.isVisible() || presetNameEditBox_.getNumCurrentlyModalComponents() != 0)
        return;

    fadeOutComponent(presetNameEditBox_, 1, 200, false);

    listBox_.grabKeyboardFocus();
    this->addOneRow(false);
    listBox_.updateContent();
}

void PresetBrowser::askForOverwrite(std::function<void(bool)> onAnswer)
{
    GUI::warningOkCancelBox(_T( "File already exists!" ), _T( "Overwrite?" ), std::move(onAnswer));
}

void PresetBrowser::setNewFolder(fs::path const &file)
{
    location_ = Location::User;
    factoryBank_.clear();
    currentDirectory_ = file;
    deselectAllRows();
    refresh();
    background().repaint();
}

void PresetBrowser::setFactoryBank(juce::String const &bank)
{
    location_ = Location::Factory;
    factoryBank_ = bank;
    deselectAllRows();
    refresh();
    background().repaint();
}

////////////////////////////////////////////////////////////////////////////////
//
// PresetBrowser::goToParent()
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note There was no way up before this. `setNewFolder` had a "does the path
/// end in `..`" test, but `refresh()` skipped `..` when listing a directory, so
/// no row ever carried that name and the test never fired -- the only way out of
/// a folder was the browse arrow and a native file dialog.
///                                           (31.07.2026.) (SW port)
///
/// \note Up out of the top of a tree is *nowhere*, where it used to be the
/// root's two-word listing. That listing is gone (\see Location) and the
/// button that reaches this is disabled there, so the guard below is what
/// makes those two statements one.
///                                           (19.08.2026.) issue #44
///
////////////////////////////////////////////////////////////////////////////////

void PresetBrowser::goToParent()
{
    if (atTopOfTree())
        return;

    switch (location_)
    {
    case Location::Factory:
    {
        auto const separator(factoryBank_.lastIndexOfChar('/'));
        return setFactoryBank(separator < 0 ? juce::String()
                                            : factoryBank_.substring(0, separator));
    }

    case Location::User:
        return setNewFolder(currentDirectory_.parent_path());
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// PresetBrowser::atTopOfTree()
// ----------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The second `User` test is for the browse arrow, which can put
/// `currentDirectory_` anywhere on the volume: from outside the preset root
/// the walk up never reaches it, and without a second stop the user can climb
/// out of their home directory a click at a time. `parent_path()` of a root is
/// that root, which is what says there is no further to go.
///
////////////////////////////////////////////////////////////////////////////////

bool PresetBrowser::atTopOfTree() const
{
    if (location_ == Location::Factory)
        return factoryBank_.isEmpty();

    return (currentDirectory_ == GUI::presetsFolder()) ||
           (currentDirectory_.parent_path() == currentDirectory_);
}

////////////////////////////////////////////////////////////////////////////////
//
// PresetBrowser::stepPreset()
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The presets are the tail of the listing, and that is not an assumption
/// -- `Item::operator<` sorts by kind first and `Folder` sorts before `Preset`,
/// so one index says where the folders stop.
///
/// \note Selecting the row is the whole of the action: `selectedRowsChanged()`
/// is what loads a preset, and going through it is what makes a jog step and a
/// click the same thing as far as everything downstream is concerned.
///
////////////////////////////////////////////////////////////////////////////////

void PresetBrowser::stepPreset(int const direction)
{
    if (!canStep(direction))
        return; // and the button that reaches this is disabled there

    int const next(listBox_.getLastRowSelected() + direction);
    listBox_.selectRow(next);
    listBox_.scrollToEnsureRowIsOnscreen(next);
}

bool PresetBrowser::presetIsSelected() const
{
    int const selected(listBox_.getLastRowSelected());

    /// \note The upper bound is not paranoia: addOneRow_ puts a row in the list
    /// that `files_` has no entry for while the filename edit box is up.
    return (selected >= 0) && (selected < files_.size()) &&
           !files_.getReference(selected).isDirectory();
}

bool PresetBrowser::canStep(int const direction) const
{
    if (!presetIsSelected())
        return false;

    int const next(listBox_.getLastRowSelected() + direction);

    /// \note Off the end of the listing, or onto a folder -- which, because the
    /// folders sort first, is what stepping back past the first preset is.
    return (next >= 0) && (next < files_.size()) && !files_.getReference(next).isDirectory();
}

////////////////////////////////////////////////////////////////////////////////
//
// PresetBrowser::updateNavigation()
// ---------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note Reached from refresh(), which every move between folders comes through,
/// and from selectedRowsChanged(), which every move within one does. The jog
/// needs the second: where the browser is has not changed when a row is clicked,
/// and whether the jog has anywhere to go has -- each half of it separately,
/// because the first preset in a listing has a forward and no back.
///
////////////////////////////////////////////////////////////////////////////////

void PresetBrowser::updateNavigation()
{
    upFolder_.setEnabled(!atTopOfTree());
    userPresets_.setToggleState(location_ == Location::User, juce::dontSendNotification);

    jogPrevious_.setEnabled(canStep(-1));
    jogNext_.setEnabled(canStep(+1));
}

SpectrumWorxEditor &PresetBrowser::editor()
{
    SpectrumWorxEditor *LE_RESTRICT const pEditor(&SpectrumWorxEditor::fromPresetBrowser(*this));
    return *pEditor;
}

SpectrumWorxEditor const &PresetBrowser::editor() const
{
    return const_cast<PresetBrowser &>(*this).editor();
}

void PresetBrowser::selectedRowsChanged(int const lastRowSelected)
{
    saveDirtyComment();

    /// \note Before the two early exits below, not after them: what the jog can
    /// do depends on the selection whether or not this browser is the one that
    /// asked for the change, and deselecting is exactly the case that turns it
    /// off. \see updateNavigation().
    updateNavigation();

    if (ignoreSelectionChange_ || (lastRowSelected == -1))
        return;

    ignoreSelectionChange_ = true;
    presetSelectionChanged();
    ignoreSelectionChange_ = false;
}

int PresetBrowser::getNumRows() noexcept { return files_.size() + addOneRow_; }

////////////////////////////////////////////////////////////////////////////////
//
// PresetBrowser::refresh()
// ------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note Two listings, because there are two things to list: a bank out of the
/// binary, or a directory. Only the second is what this browser used to do.
///
////////////////////////////////////////////////////////////////////////////////

void PresetBrowser::refresh()
{
    files_.clearQuick();

    switch (location_)
    {
    case Location::Factory:
        refreshFactory();
        break;
    case Location::User:
        refreshUserDirectory();
        break;
    }

    std::sort(files_.begin(), files_.end());

    /// \note Save-As is the one button that depends on *where* the browser is
    /// rather than on what is selected in it, so this is where it belongs: the
    /// two location setters both come through here, and so does
    /// refreshAndSelectPreset(). save_ and delete_ stay with the selection.
    saveAs_.setEnabled(enablePresetSaving());

    updateNavigation();

    listBox_.updateContent();
}

////////////////////////////////////////////////////////////////////////////////
//
// PresetBrowser::refreshFactory()
// -------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note FactoryPresets::banks() is every directory under the preset root, as a
/// path relative to it, so the children of the current bank are the ones that
/// start with it and have no further separator. Three of the fifteen banks have
/// a sub-folder, which is why this is a filter rather than a lookup.
///
/// \note What this reads from banks() has to include the directories that lead
/// to presets without holding any -- a grandchild is skipped here on the
/// understanding that its parent is a row the user can open, and for those three
/// banks there was no such row. See collectBanks() in factoryPresets.cpp.
///
////////////////////////////////////////////////////////////////////////////////

void PresetBrowser::refreshFactory()
{
    juce::String const prefix(factoryBank_.isEmpty() ? juce::String() : factoryBank_ + "/");

    for (auto const &bank : FactoryPresets::banks())
    {
        juce::String const path(bank.c_str());
        if (!path.startsWith(prefix) || (path == factoryBank_))
            continue;

        auto const remainder(path.substring(prefix.length()));
        if (remainder.isEmpty() || remainder.containsChar('/'))
            continue; // a grandchild; it shows up once its parent is opened

        files_.add(Item{remainder, Item::Kind::Folder});
    }

    if (!factoryBank_.isEmpty())
        for (auto const &preset : FactoryPresets::presets(factoryBank_.toStdString()))
            files_.add(Item{preset.c_str(), Item::Kind::Preset});
}

////////////////////////////////////////////////////////////////////////////////
//
// PresetBrowser::refreshUserDirectory()
// -------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note **The `juce::File` hop is gone from the front of this**, and it was a
/// bug on Windows: `currentDirectory_.getFullPathName().getCharPointer()` handed
/// `directory_iterator` a narrow `char const *`, which `fs::path` decodes with
/// the active code page there rather than as UTF-8. `currentDirectory_` is an
/// `fs::path` now and goes in as itself.
///
/// \note What comes back still needs `pathToJuceString()`, for the reason the
/// long note in gui.cpp gives: `juce::String( char const *, size_t )` reads bytes
/// through `CharPointer_ASCII` and widens each one into its own code point, so a
/// preset named in anything but ASCII would list under a mangled name and then
/// fail to be found again by it. `.u8string()` is what the bytes are; the
/// conversion is io/jucePath.hpp's.
///
/// \note `stem()` and `extension()` in place of the byte arithmetic and the
/// `std::_tcscmp` that stood here. `stem()` on `a.b.swp` is `a.b`, which is what
/// subtracting four bytes did.
///                                       (09.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void PresetBrowser::refreshUserDirectory()
{
    std::error_code listingError;
    Item item;
    for (auto const &entry : std::filesystem::directory_iterator(currentDirectory_, listingError))
    {
        bool const isDirectory(entry.is_directory(listingError));
        if (!isDirectory && (entry.path().extension() != presetExtension))
            continue;

        item.kind = isDirectory ? Item::Kind::Folder : Item::Kind::Preset;
        item.name =
            LE::IO::pathToJuceString(isDirectory ? entry.path().filename() : entry.path().stem());
        files_.add(item);
    }
}

void PresetBrowser::refreshAndSelectPreset(juce::String const &presetName)
{
    refresh();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Checked, where the check was an assertion that a shipped build does
    /// not compile. `findPreset()` answers `end()` when the name is not in the
    /// list, and `end() - begin()` is the size -- one past the last row -- so
    /// what followed was a `juce::Array` read past its end and a selection of a
    /// row that does not exist.
    ///
    ///   The name not matching is not hypothetical: this is called with the name
    /// a preset was just saved under, and what comes back from `refresh()` is
    /// what the *file system* reports. A decomposed accent on macOS, or a
    /// character Windows will not put in a file name, and the two spellings
    /// differ.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    Item const *const pItem(findPreset(presetName));
    if (pItem == files_.end())
        return;

    unsigned int const indexToSelect(static_cast<unsigned int>(pItem - files_.begin()));

    comment().grabKeyboardFocus();

    listBox_.scrollToEnsureRowIsOnscreen(indexToSelect);
    ignoreSelectionChange_ = true;
    listBox_.selectRow(indexToSelect);
    ignoreSelectionChange_ = false;
    bool const enablePresetSaving(this->enablePresetSaving());
    delete_.setEnabled(enablePresetSaving);
    comment().setEnabled(enablePresetSaving);
}

void PresetBrowser::deselectAllRows()
{
    listBox_.deselectAllRows();
    // Implementation note:
    //   A single place to ensure that whenever the preset selection is cleared
    // the comment box is also cleared. In the special case when the selection
    // is only switched from one preset to another this will be taken care of
    // implicitly (the comment will be automatically set to the one of the newly
    // selected preset).
    //                                    (23.03.2010.) (Domagoj Saric)
    comment().clear();
    comment().setEnabled(false);

    save_.setEnabled(false);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note `currentDirectory_`, unconditionally. That member
/// only means anything at `User`: at `Root` it is whatever the last visited
/// folder was -- empty on a fresh browser -- and at `Factory` it names a
/// directory the list is not showing, so the strip described somewhere the user
/// was not.
///                                           (08.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

juce::String PresetBrowser::locationLabel() const
{
    switch (location_)
    {
    case Location::Factory:
        return factoryBank_.isEmpty() ? juce::String(_T( "Factory" ))
                                      : _T( "Factory/" ) + factoryBank_;

    case Location::User:
    {
        ///   "User", and then whatever is under it: the same shape the Factory
        /// arm has, and for the reason issue #134 gives. The strip is 250 px of
        /// room and an absolute path spends all of it on a home directory, a
        /// vendor folder and a Presets folder -- so the one part that says where
        /// the user actually is fell off the right-hand end.
        ///
        /// \note `lexically_relative()`, which touches no disk: this is asked
        /// from paint(). It answers "." for the root itself, and an empty path
        /// or one starting in ".." for somewhere with no route down from it --
        /// which the browse button can reach, and which is the one case with
        /// nothing shorter to say than the path.
        auto const under(currentDirectory_.lexically_relative(GUI::presetsFolder()));
        if (under.empty() || (*under.begin() == _T( ".." )))
            return LE::IO::pathToJuceString(currentDirectory_);
        if (under == _T( "." ))
            return _T( "User" );

        /// \note Joined as a path rather than with a literal "User/", so that
        /// the separator between the folders is the one the platform names them
        /// with. Factory's is a slash because a bank is a name in the binary
        /// rather than a directory on disk.
        return LE::IO::pathToJuceString(fs::path(_T( "User" )) / under);
    }
    }
    LE_UNREACHABLE_CODE();
}

void PresetBrowser::takeColours()
{
    presetNameEditBox_.setColour(juce::TextEditor::backgroundColourId,
                                 ColourMap::getColour(ColourMap::Ground));
    presetNameEditBox_.setColour(juce::TextEditor::focusedOutlineColourId,
                                 ColourMap::getColour(ColourMap::Accent));

    comment().setColour(juce::TextEditor::backgroundColourId,
                        ColourMap::getColour(ColourMap::Transparent));
    comment().setColour(juce::TextEditor::textColourId, ColourMap::getColour(ColourMap::Accent));
    comment().setColour(juce::TextEditor::highlightColourId,
                        ColourMap::getColour(ColourMap::TextFaint));
}

void PresetBrowser::paint(juce::Graphics &graphics)
{
    PanelBackground::paint(graphics);
    graphics.setColour(ColourMap::getColour(ColourMap::Text));
    graphics.setFont(20);
    graphics.drawFittedText(locationLabel(), 20, 15, 233, 18, juce::Justification::centredLeft, 1);
}

bool PresetBrowser::Item::operator==(Item const &other) const { return name == other.name; }

/// \note By kind first, so ".." is always the top row and folders always precede
/// presets, and by name within a kind.
bool PresetBrowser::Item::operator<(Item const &other) const
{
    if (this->kind != other.kind)
        return this->kind < other.kind;
    return this->name < other.name;
}

} // namespace LE::SW::GUI
