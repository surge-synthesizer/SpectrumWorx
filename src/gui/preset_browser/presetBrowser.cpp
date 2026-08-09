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

#include "le/parameters/uiElements.hpp"
#include "le/spectrumworx/factoryPresets.hpp"
#include "le/spectrumworx/presetFile.hpp"
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

static char_t const presetExtension[] = _T( ".swp" );
unsigned int const presetExtensionLength = _countof(presetExtension) - 1;
} // namespace

#pragma warning(push)
#pragma warning(disable : 4355) // 'this' used in base member initializer list.

PresetBrowser::PresetBrowser()
    : BackgroundImage(resourceArtwork<PresetBackground>()),
      save_(*this, resourceArtwork<PresetSaveDown>(), resourceArtwork<PresetSaveUp>(),
            juce::Colours::transparentWhite, false),
      saveAs_(*this, resourceArtwork<PresetSaveAsDown>(), resourceArtwork<PresetSaveAsUp>(),
              juce::Colours::transparentWhite, false),
      delete_(*this, resourceArtwork<PresetDeleteDown>(), resourceArtwork<PresetDeleteUp>(),
              juce::Colours::transparentWhite, false),
      browseArrow_(*this, resourceArtwork<ChangeWaveform>(), resourceArtwork<ChangeWaveform>(),
                   juce::Colours::white.withAlpha(0.5f), false),
      ignoreExternalSamples_(*this, 15, 58, "Ignore external audio"), ignoreSelectionChange_(false),
      addOneRow_(false), newPresetPending_(false), dirtyCommentPresetIndex_(-1)
{
    listBox_.setModel(this);

    setSizeFromImage(*this, this->image());

    /// \note All three start disabled and refresh() decides Save-As from there.
    /// It used to be settled once, here, where location_ is still its default
    /// Root -- so it was disabled before the browser had ever listed anything
    /// and nothing re-enabled it. Save-As was unclickable for the life of the
    /// plugin.
    ///                                       (01.08.2026.) (SW port)
    save_.setEnabled(false);
    saveAs_.setEnabled(false);
    delete_.setEnabled(false);
    save_.addListener(this);
    saveAs_.addListener(this);
    delete_.addListener(this);

    browseArrow_.addListener(this);

    /// \note Here rather than at the first save: an empty browser with no way
    /// to make a folder is not a usable answer to "you have no presets yet".
    GUI::createUserPresetsFolder();

    /// \note Where it was last left, and the root -- "Factory" and "User" --
    /// the first time, rather than the user's folder. A new installation's user
    /// folder is empty, and a browser that opens on nothing while 303 presets
    /// sit in the binary is the state this was reported in.
    restoreLastPlace();

    browseArrow_.setTopLeftPosition(174, 10);
    save_.setTopLeftPosition(17, 33);
    saveAs_.setTopLeftPosition(17 + 55, 33);
    delete_.setTopLeftPosition(17 + 55 + 55, 33);
    listBox_.setBounds(11, 79, getWidth() - 22, 234);
    comment().setBounds(8, 322, getWidth() - 13, 29);

    addChildComponent(&presetNameEditBox_);
    presetNameEditBox_.setAlwaysOnTop(true);
    presetNameEditBox_.setFont(Theme::singleton().whiteFont());
    presetNameEditBox_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black);
    presetNameEditBox_.setColour(juce::TextEditor::focusedOutlineColourId,
                                 Theme::singleton().blueColour());
    presetNameEditBox_.addListener(this);

    listBox_.setOpaque(false);
    listBox_.setRowHeight(16);
    listBox_.getViewport()->setScrollBarThickness(12);

    comment().setMultiLine(true);
    comment().setReturnKeyStartsNewLine(true);
    comment().setPopupMenuEnabled(true);
    comment().setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    comment().setColour(juce::TextEditor::textColourId, Theme::singleton().blueColour());
    comment().setColour(juce::TextEditor::highlightColourId, juce::Colours::lightgrey);
    comment().addListener(this);
    {
        juce::Font font(Theme::singleton().Theme::getPopupMenuFont());
        font.setHeight(11);
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
/// way the answer is the root, which is the one place that is always there.
///
////////////////////////////////////////////////////////////////////////////////

void PresetBrowser::restoreLastPlace()
{
    auto const &place(lastPlace());

    switch (place.location)
    {
    case Location::Root:
        break;

    case Location::Factory:
        return setFactoryBank(place.factoryBank);

    case Location::User:
        if (place.folder.isDirectory())
            return setNewFolder(place.folder);
        break;
    }

    setRoot();
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
    bool const succeeded(presetData && editor().loadPreset(presetData.get(),
                                                           ignoreExternalSamples_.getToggleState(),
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
    LE_ASSERT((location_ != Location::User) || item.isDirectory() ||
              currentDirectory_.getChildFile(item.name + presetExtension).exists());
    return item;
}

PresetBrowser::Item const &PresetBrowser::selectedItem() const { return item(selectedIndex()); }

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The row's file, or an empty `juce::File` when the row does not have
/// one. See selectedPresetData(), which is what the load path uses.
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

juce::File PresetBrowser::file(unsigned int const index) const
{
    if (location_ != Location::User)
        return {};

    Item const &item(this->item(index));
    if (item.isDirectory())
        return {};

    return currentDirectory_.getChildFile(item.name + presetExtension);
}

juce::File PresetBrowser::selectedFile() const { return file(selectedIndex()); }

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
    case Item::Kind::Parent:
        return goToParent();

    case Item::Kind::Section:
        if (item.name == "Factory")
            return setFactoryBank({});
        return setNewFolder(GUI::presetsFolder());

    case Item::Kind::Folder:
        if (inFactory())
            return setFactoryBank(factoryBank_.isEmpty() ? item.name
                                                         : factoryBank_ + "/" + item.name);
        return setNewFolder(currentDirectory_.getChildFile(item.name));

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

    unsigned int const x(isDirectory ? 16 : 4);

    graphics.setColour(juce::Colours::black);

    if (isDirectory)
    {
        //...mrmlj...
        //paintImage
        //(
        //    graphics,
        //    Theme::singleton().Theme::getDefaultFolderImage(),
        //    2, 2
        //);
        /// \note JUCE 8's getDefaultFolderImage() returns a Drawable rather than
        /// an Image, and may return nothing at all.
        if (auto const *const pFolderImage = Theme::singleton().Theme::getDefaultFolderImage())
            pFolderImage->drawWithin(
                graphics,
                juce::Rectangle<float>(2.0f, 2.0f, static_cast<float>(x) - 4.0f,
                                       static_cast<float>(height) - 4.0f),
                juce::RectanglePlacement::xLeft | juce::RectanglePlacement::xRight |
                    juce::RectanglePlacement::onlyReduceInSize,
                1.0f);
    }

    graphics.setColour(Theme::singleton().Theme::findColour(
        juce::DirectoryContentsDisplayComponent::textColourId));
    graphics.setFont(height * 0.7f);

    graphics.drawFittedText(item.name, x, 0, width - x, height, juce::Justification::centredLeft,
                            1);
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

    juce::File const targetFile(currentDirectory_.getChildFile(userEntry + presetExtension));

    juce::Component::SafePointer<PresetBrowser> const self(this);

    if (newPresetPending_)
    {
        auto const save([self, userEntry, targetFile] {
            if (!self)
                return;
            self->newPresetPending_ = false;
            self->hideFilenameEditBox();
            self->saveCurrentPreset(userEntry, targetFile);
        });

        if (!targetFile.exists())
            return save();

        askForOverwrite([save, targetFile](bool const overwrite) {
            if (!overwrite)
                return;
            targetFile.deleteFile();
            save();
        });
        return;
    }

    juce::File const sourceFile(selectedFile());
    /// \note An empty source is a row with no file behind it -- a factory
    /// preset, or the root's two section entries. Nothing to rename.
    if ((sourceFile == juce::File()) || (sourceFile == targetFile))
        return;

    auto const rename([self, sourceFile, targetFile, userEntry] {
        if (self)
            self->renameTo(sourceFile, targetFile, userEntry);
    });

    if (!targetFile.exists())
        return rename();

    askForOverwrite([rename](bool const overwrite) {
        if (overwrite)
            rename();
    });
}

/// \note Was the body of a `while` loop whose condition asked the user whether
/// to retry. It calls itself from the dialog's callback instead -- one live
/// attempt at a time, and the stack unwinds between them.
void PresetBrowser::renameTo(juce::File const &sourceFile, juce::File const &targetFile,
                             juce::String const &newName)
{
    if (sourceFile.moveFileTo(targetFile))
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

    juce::File const dirtyPreset(this->file(dirtyCommentPresetIndex_));

    dirtyCommentPresetIndex_ = -1;

    if (dirtyPreset.existsAsFile())
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

void PresetBrowser::saveCurrentPreset(juce::String const &presetName, juce::File const &targetFile)
{
    bool const shouldRefresh(!targetFile.exists());

    originalComment_ = comment().getText();
    editor().savePreset(targetFile, ignoreExternalSamples_.getToggleState(), originalComment_);

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
        juce::File const target(selectedFile());
        if (target != juce::File())
            saveCurrentPreset(selectedItem().name, target);
    }
    else if (pButton == &delete_)
    {
        juce::File const target(selectedFile());
        if (target != juce::File())
        {
            target.deleteFile();
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
        juce::File const startFrom((location_ == Location::User) && currentDirectory_.isDirectory()
                                       ? currentDirectory_
                                       : GUI::presetsFolder());
        folderChooser_ = std::make_unique<juce::FileChooser>(
            "Please select a folder with SW presets...", startFrom);
        folderChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                        juce::FileBrowserComponent::canSelectDirectories,
                                    [self = juce::Component::SafePointer<PresetBrowser>(this)](
                                        juce::FileChooser const &chooser) {
                                        auto const chosen(chooser.getResult());
                                        /// \note A cancelled chooser reports an
                                        /// empty file, and a directory that has
                                        /// gone away between the dialog opening
                                        /// and closing reports one that is not
                                        /// there.
                                        if (self && chosen.isDirectory())
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
    GUI::warningOkCancelBox(_T( "File already exists." ), _T( "Overwrite?" ), std::move(onAnswer));
}

void PresetBrowser::setNewFolder(juce::File const &file)
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

void PresetBrowser::setRoot()
{
    location_ = Location::Root;
    factoryBank_.clear();
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
////////////////////////////////////////////////////////////////////////////////

void PresetBrowser::goToParent()
{
    switch (location_)
    {
    case Location::Root:
        return;

    case Location::Factory:
    {
        if (factoryBank_.isEmpty())
            return setRoot();
        auto const separator(factoryBank_.lastIndexOfChar('/'));
        return setFactoryBank(separator < 0 ? juce::String()
                                            : factoryBank_.substring(0, separator));
    }

    case Location::User:
        /// \note Up out of the user's own preset folder is the root rather than
        /// the rest of the filesystem. Somewhere above `~/Documents` there is
        /// nothing a preset browser should be showing.
        if (currentDirectory_ == GUI::presetsFolder())
            return setRoot();
        return setNewFolder(currentDirectory_.getParentDirectory());
    }
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
/// \note Three listings, because there are three things to list: two fixed
/// entries at the root, a bank out of the binary, or a directory. Only the last
/// is what this browser used to do.
///
////////////////////////////////////////////////////////////////////////////////

void PresetBrowser::refresh()
{
    files_.clearQuick();

    switch (location_)
    {
    case Location::Root:
        refreshRoot();
        break;
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
    /// three location setters all come through here, and so does
    /// refreshAndSelectPreset(). save_ and delete_ stay with the selection.
    saveAs_.setEnabled(enablePresetSaving());

    listBox_.updateContent();
}

void PresetBrowser::refreshRoot()
{
    files_.add(Item{"Factory", Item::Kind::Section});
    files_.add(Item{"User", Item::Kind::Section});
}

////////////////////////////////////////////////////////////////////////////////
//
// PresetBrowser::refreshFactory()
// -------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note FactoryPresets::banks() is every directory that holds presets, as a
/// path relative to the root, so the children of the current bank are the ones
/// that start with it and have no further separator. Three of the fifteen banks
/// have a sub-folder, which is why this is a filter rather than a lookup.
///
////////////////////////////////////////////////////////////////////////////////

void PresetBrowser::refreshFactory()
{
    files_.add(Item{"..", Item::Kind::Parent});

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
/// \note `fromUTF8()`, for the same reason `rootPath()` needs it -- see the long
/// note in gui.cpp. What `directory_iterator` hands back is whatever bytes the
/// file system holds, and `juce::String( char const *, size_t )` would read them
/// through `CharPointer_ASCII` and widen each one into its own code point. A
/// preset named in anything but ASCII would list under a mangled name here, and
/// then fail to be found again by it.
///
///   `nameLength` stays as it is: it is a count of bytes, and `bufferSizeBytes`
/// is what the second parameter of `fromUTF8()` wants too. It is only the
/// *interpretation* of those bytes that changes.
///                                       (09.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void PresetBrowser::refreshUserDirectory()
{
    files_.add(Item{"..", Item::Kind::Parent});

    std::error_code listingError;
    Item item;
    for (auto const &entry : std::filesystem::directory_iterator(
             currentDirectory_.getFullPathName().getCharPointer().getAddress(), listingError))
    {
        auto const fileName(entry.path().filename().string());
        if (fileName == "." || fileName == "..")
            continue;

        bool const isDirectory(entry.is_directory(listingError));
        auto const fullNameLength(static_cast<unsigned int>(fileName.size()));
        unsigned int const nameLength(
            isDirectory ? fullNameLength
                        : (fullNameLength - std::min(fullNameLength, presetExtensionLength)));

        if (isDirectory || std::_tcscmp(fileName.c_str() + nameLength, presetExtension) == 0)
        {
            item.kind = isDirectory ? Item::Kind::Folder : Item::Kind::Preset;
            item.name = juce::String::fromUTF8(fileName.c_str(), static_cast<int>(nameLength));
            files_.add(item);
        }
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
/// \note `currentDirectory_.getFullPathName()`, unconditionally. That member
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
    case Location::Root:
        return _T( "Presets" );

    case Location::Factory:
        return factoryBank_.isEmpty() ? juce::String(_T( "Factory" ))
                                      : _T( "Factory/" ) + factoryBank_;

    case Location::User:
        return currentDirectory_.getFullPathName();
    }
    LE_UNREACHABLE_CODE();
}

void PresetBrowser::paint(juce::Graphics &graphics)
{
    BackgroundImage::paint(graphics);
    graphics.setColour(juce::Colours::white);
    graphics.drawFittedText(locationLabel(), 9, 8, 162, 17, juce::Justification::centredLeft, 1);
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
