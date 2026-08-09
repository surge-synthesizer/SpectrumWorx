////////////////////////////////////////////////////////////////////////////////
///
/// \file presetBrowser.hpp
/// -----------------------
///
/// SpectrumWorx preset browser implementation.
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef presetBrowser_hpp__228370D3_4C4C_46B8_8544_9273C3AAEB61A
#define presetBrowser_hpp__228370D3_4C4C_46B8_8544_9273C3AAEB61A
//------------------------------------------------------------------------------
#include "gui/gui.hpp"

#include "le/spectrumworx/presets.hpp"
#include "le/utility/platformSpecifics.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace LE::SW
{

class SpectrumWorx;

namespace GUI
{

class SpectrumWorxEditor;

class PresetBrowser final : public BackgroundImage,
                            private juce::ListBoxModel,
                            private juce::Button::Listener,
                            private juce::TextEditor::Listener
{
  public:
    PresetBrowser();
    ~PresetBrowser();

    SpectrumWorxEditor &editor();
    SpectrumWorxEditor const &editor() const;

  private: // JUCE Component overrides.
    void paint(juce::Graphics &) override;

  private: // JUCE ButtonListener overrides.
    void buttonClicked(juce::Button *) override;

  private: // JUCE TextEditorListener overrides.
    void textEditorTextChanged(juce::TextEditor &) override;
    void textEditorReturnKeyPressed(juce::TextEditor &) override;
    void textEditorEscapeKeyPressed(juce::TextEditor &) override;
    void textEditorFocusLost(juce::TextEditor &) override;

  private: // JUCE ListBoxModel overrides.
    int getNumRows() noexcept override;
    void paintListBoxItem(int rowNumber, juce::Graphics &, int width, int height,
                          bool rowIsSelected) override;
    void listBoxItemDoubleClicked(int row, juce::MouseEvent const &) override;
    void deleteKeyPressed(int lastRowSelected) noexcept override;
    void returnKeyPressed(int lastRowSelected) noexcept override;
    void selectedRowsChanged(int lastRowSelected) override;

  private:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \enum Location
    ///
    /// \brief Which of the three things the list is showing.
    ///
    /// \note The browser was a directory browser: one `juce::File` and
    /// `std::filesystem` over it. The factory banks are in the binary now (8.2)
    /// and have no directory to point it at, so where it is looking became a
    /// small sum type rather than a path. `Root` is a list of two entries and
    /// exists so that there is somewhere `..` can go from either tree.
    ///                                       (31.07.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    enum struct Location : std::uint8_t
    {
        Root,    ///< "Factory" and "User"; no storage behind it
        Factory, ///< an embedded bank, addressed by factoryBank_. Read only.
        User     ///< a real directory, addressed by currentDirectory_
    };

    struct Item
    {
        enum struct Kind : std::uint8_t
        {
            Parent,  ///< the ".." row
            Section, ///< "Factory" or "User", only at the Root
            Folder,  ///< a bank or a sub-directory
            Preset
        };

        juce::String name;
        Kind kind{Kind::Preset};

        bool isDirectory() const { return kind != Kind::Preset; }

        bool operator==(Item const &other) const;
        bool operator<(Item const &other) const;
    };

  private:
  public:
    /// \note Public for `tools/show-ui`, which opens the browser on a bank to
    /// render it. See SpectrumWorxEditor::showFactoryBank().
    void setFactoryBank(juce::String const &bank);

  private:
    void setNewFolder(juce::File const &);
    void setRoot();

    /// \brief Up one level, wherever "up" is from here.
    void goToParent();

    /// \brief What the header strip says, for whichever of the three it is
    /// showing. Only `User` has a path to print.
    juce::String locationLabel() const;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \struct Place
    ///
    /// \brief Where the browser was when it last closed.
    ///
    /// \note Process-wide and main thread only, because the browser is built
    /// and destroyed every time the overlay opens and closes -- so where it was
    /// has to outlive it. It used to be half remembered: the destructor wrote
    /// `currentDirectory_` into `GUI::presetsFolder()` and nothing recorded
    /// `location_`, which is a member initialiser, so the browser reopened at
    /// `Root` whatever the user had been looking at. Writing the user's
    /// wanderings into `presetsFolder()` also moved the anchor `goToParent()`
    /// stops at and the folder `createUserPresetsFolder()` makes; that global
    /// means the preset root now and nothing writes to it.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    struct Place
    {
        Location location{Location::Root};
        juce::String factoryBank; ///< when location is Factory
        juce::File folder;        ///< when location is User
    };

    static Place &lastPlace();
    void restoreLastPlace();

    void refresh();
    void refreshRoot();
    void refreshFactory();
    void refreshUserDirectory();

    /// \brief The selected preset's bytes, from the binary or from a file.
    Preset::InMemoryPreset selectedPresetData() const;

    bool inFactory() const { return location_ == Location::Factory; }

    void refreshAndSelectPreset(juce::String const &presetName);

    void showFilenameEditBox(juce::String const &presetName, unsigned int atRow);
    void hideFilenameEditBox();

    void saveCurrentPreset(juce::String const &presetName, juce::File const &targetFile);

    /// \note Retries itself from the dialog's callback rather than from a loop;
    /// see the definition.
    void renameTo(juce::File const &sourceFile, juce::File const &targetFile,
                  juce::String const &newName);

    void saveDirtyComment();
    void presetSelectionChanged();

    void deselectAllRows();

    void addOneRow(bool const value) { addOneRow_ = value; }

    /// \note Was `bool askForOverwrite()`, answered where it was asked. JUCE 8
    /// builds with JUCE_MODAL_LOOPS_PERMITTED=0 -- and a plugin has no business
    /// spinning a modal loop inside a host's message thread -- so the answer
    /// arrives later, on the message thread.
    static void askForOverwrite(std::function<void(bool)> onAnswer);

    bool enablePresetSaving() const;

    unsigned int selectedIndex() const;

    Item const &item(unsigned int index) const;
    Item const &selectedItem() const;
    juce::File file(unsigned int index) const;
    juce::File selectedFile() const;

    Item const *findPreset(juce::String const &presetName) const;

    juce::TextEditor &comment() { return commentBox_; }
    BackgroundImage &background() { return *this; }

  private: // friend class Detail::BackgroundWithCurrentFolder;
    juce::TextEditor presetNameEditBox_;
    juce::TextEditor commentBox_;
    juce::ListBox listBox_;
    BitmapButton save_;
    BitmapButton saveAs_;
    BitmapButton delete_;
    BitmapButton browseArrow_;
    LEDTextButton ignoreExternalSamples_;

    bool ignoreSelectionChange_;
    bool addOneRow_;
    bool newPresetPending_;

    int dirtyCommentPresetIndex_;

    Location location_{Location::Root};

    /// The bank, relative to the preset root, when location_ is Factory. Empty
    /// means the top of the factory tree.
    juce::String factoryBank_;

    juce::File currentDirectory_;
    juce::Array<Item> files_;

    juce::String originalComment_;

    /// \note Held rather than stack-local: JUCE 8's FileChooser reports through
    /// launchAsync() and must outlive the call that starts it.
    std::unique_ptr<juce::FileChooser> folderChooser_;
}; // class PresetBrowser

} // namespace GUI

} // namespace LE::SW

#endif // presetBrowser_hpp
