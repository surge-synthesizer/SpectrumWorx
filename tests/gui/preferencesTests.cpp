////////////////////////////////////////////////////////////////////////////////
///
/// preferencesTests.cpp
/// --------------------
///
///   The three interface preferences, and the two halves of issue #61: that they
/// reach the file at all, and that the Interface page is wired to the file rather
/// than to a process-wide default that every session started over.
///
/// \note The last two cases go through the widgets rather than through
/// `Preferences` directly, because "it is saved" and "the panel saves it" are
/// different claims and only the second one was ever wrong. What they cannot go
/// through is the combo box's popup menu: it is asynchronous and there is no
/// message loop in a test binary, so a case drives `comboBoxValueChanged()` -- the
/// function the menu's own callback calls, and the only thing it does with the
/// menu's answer.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/editorHarness.hpp"

#include "gui/preferences.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
using namespace LE;
using namespace LE::SW;

using Editor = GUI::SpectrumWorxEditor;
using Preferences = GUI::Preferences;

fs::path preferencesRoot() { return fs::path(SW_TEST_OUTPUT_DIR) / "preferences"; }

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Points the whole binary's preferences at an empty folder in the build
/// tree, before any case runs.
///
/// \note Registered here and it covers every file: `GUI::preferences()` is
/// process-wide and several gui cases in other files open the Interface page.
/// Without this they would read the preferences of whoever is running the suite,
/// so what they saw would depend on that person's last click.
///
/// \note testRunStarting rather than a fixture, because the earliest thing that
/// reads these is a case in another file.
///
/// \note **Nothing writes here, and this folder is never emptied.** ctest runs one
/// process per case and eight at a time, so every case that means to *write* a
/// preference takes a folder of its own below -- see caseFolder(). A shared one
/// would have cases overwriting each other's file and deleting the folder out
/// from under each other, which is what one release run of this file did before
/// they were separated.
///
////////////////////////////////////////////////////////////////////////////////

class PreferencesInTheBuildTree final : public Catch::EventListenerBase
{
  public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(Catch::TestRunInfo const &) override
    {
        GUI::setPreferencesFolder(preferencesRoot() / "unwritten");
    }
}; // class PreferencesInTheBuildTree

CATCH_REGISTER_LISTENER(PreferencesInTheBuildTree)

/// A folder of this case's own, empty. \p name has to be unique in this file.
fs::path caseFolder(char const *const name)
{
    auto const folder(preferencesRoot() / name);
    fs::remove_all(folder);
    return folder;
}

/// The same, and the process-wide preferences moved onto it -- for the cases that
/// go through the editor, which reaches `GUI::preferences()` rather than an
/// instance a case can hand it.
void useCaseFolder(char const *const name) { GUI::setPreferencesFolder(caseFolder(name)); }

std::string contentsOf(fs::path const &file)
{
    std::ifstream stream(file);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

void write(fs::path const &file, std::string const &text)
{
    fs::create_directories(file.parent_path());
    std::ofstream stream(file);
    stream << text;
}

////////////////////////////////////////////////////////////////////////////////
// Reaching the widgets
////////////////////////////////////////////////////////////////////////////////

/// Every descendant of \p root that is a \p Widget, in child order.
template <typename Widget> std::vector<Widget *> descendantsOfType(juce::Component &root)
{
    std::vector<Widget *> found;
    for (auto *const pChild : root.getChildren())
    {
        if (auto *const pWidget(dynamic_cast<Widget *>(pChild)); pWidget)
            found.push_back(pWidget);
        for (auto *const pDeeper : descendantsOfType<Widget>(*pChild))
            found.push_back(pDeeper);
    }
    return found;
}

/// \brief The Interface page's two combo boxes, told apart by how many choices
/// each offers rather than by the order they were added in.
///
/// \note A TabbedComponent keeps only the *current* page as a child, so the
/// engine page's three combo boxes -- which are TitledComboBoxes too -- are not
/// in the tree while the Interface tab is up. The count is required rather than
/// assumed, so that a layout change fails here instead of silently sending the
/// rest of the case at the wrong widget.
GUI::TitledComboBox &comboBoxOffering(Editor &editor, unsigned int const choices)
{
    auto const comboBoxes(descendantsOfType<GUI::TitledComboBox>(editor));
    REQUIRE(comboBoxes.size() == 2);

    for (auto *const pComboBox : comboBoxes)
        if (pComboBox->numberOfItems() == choices)
            return *pComboBox;

    FAIL("no combo box on the Interface page offers " << choices << " choices");
    return *comboBoxes.front();
}

GUI::TitledComboBox &mouseOverComboBox(Editor &editor) { return comboBoxOffering(editor, 3); }
GUI::TitledComboBox &lfoUpdateComboBox(Editor &editor) { return comboBoxOffering(editor, 4); }

/// The one LED on the Interface page, found through the combo boxes so that the
/// module strips' own LEDs cannot be picked up instead.
GUI::LEDTextButton &hideCursorButton(Editor &editor)
{
    auto *const pPage(mouseOverComboBox(editor).getParentComponent());
    REQUIRE(pPage != nullptr);

    auto const buttons(descendantsOfType<GUI::LEDTextButton>(*pPage));
    REQUIRE(buttons.size() == 1);
    return *buttons.front();
}

/// An editor with the Interface page up.
Editor &editorOnTheInterfacePage(SWTest::Instance &instance)
{
    instance.openEditor(Editor::PanelPlacement::overlay);
    instance.editor().showSettings(Editor::interfacePageIndex);
    return instance.editor();
}

} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
// The file
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("With no preferences file every value is at its default", "[gui][preferences]")
{
    Preferences const preferences(caseFolder("absent"));

    CHECK(preferences.moduleUIMouseOverReaction() == Preferences::Never);
    CHECK(preferences.lfoUpdateBehaviour() == Preferences::Always);
    CHECK(preferences.hideCursorOnKnobDrag());

    /// \note And reading wrote nothing. A plugin that has only ever been opened
    /// has no preferences to record, and a file appearing in the user's folder on
    /// first launch is a claim that it does.
    CHECK(!fs::exists(preferences.file()));
}

TEST_CASE("Every preference survives a new instance over the same folder", "[gui][preferences]")
{
    auto const folder(caseFolder("roundTrip"));

    {
        Preferences written(folder);
        written.setModuleUIMouseOverReaction(Preferences::WhenParentModuleSelected);
        written.setLFOUpdateBehaviour(Preferences::WhenControlActive);
        written.setHideCursorOnKnobDrag(false);

        REQUIRE(fs::exists(written.file()));
    }

    Preferences const read(folder);
    CHECK(read.moduleUIMouseOverReaction() == Preferences::WhenParentModuleSelected);
    CHECK(read.lfoUpdateBehaviour() == Preferences::WhenControlActive);
    CHECK(!read.hideCursorOnKnobDrag());
}

TEST_CASE("The file names its keys and its enumerated values", "[gui][preferences]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The on-disk contract, pinned the way the preset format's is. What
    /// makes it worth a case rather than a comment is that the alternative --
    /// writing the enumerator's ordinal -- reads back wrong rather than not at
    /// all the first time somebody inserts a value in the middle, and nothing
    /// else in the suite would notice.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const folder(caseFolder("names"));

    Preferences preferences(folder);
    preferences.setModuleUIMouseOverReaction(Preferences::WhenParentOrNothingSelected);
    preferences.setLFOUpdateBehaviour(Preferences::WhenControlSelected);
    preferences.setHideCursorOnKnobDrag(false);

    auto const file(contentsOf(preferences.file()));
    CAPTURE(file);

    CHECK(file.find("key=\"moduleUIMouseOverReaction\" value=\"WhenParentOrNothingSelected\"") !=
          std::string::npos);
    CHECK(file.find("key=\"lfoUpdateBehaviour\" value=\"WhenControlSelected\"") !=
          std::string::npos);
    CHECK(file.find("key=\"hideCursorOnKnobDrag\" value=\"0\"") != std::string::npos);
}

TEST_CASE("A value this build does not recognise reads as the default", "[gui][preferences]")
{
    // The file is the user's to edit, and a downgrade is the same case.
    auto const folder(caseFolder("unrecognised"));

    write(folder / "SpectrumWorxUserDefaults.xml",
          "<?xml version = \"1.0\" encoding = \"UTF-8\" ?>\n"
          "<defaults version=\"1\">\n"
          "  <default key=\"moduleUIMouseOverReaction\" value=\"Sideways\" type=\"1\"/>\n"
          "  <default key=\"lfoUpdateBehaviour\" value=\"WhenControlActive\" type=\"1\"/>\n"
          "</defaults>\n");

    Preferences const preferences(folder);

    CHECK(preferences.moduleUIMouseOverReaction() == Preferences::Never);
    // ...and the key it could not read did not cost it the one beside it.
    CHECK(preferences.lfoUpdateBehaviour() == Preferences::WhenControlActive);
}

////////////////////////////////////////////////////////////////////////////////
// The Interface page
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The Interface page opens showing what is in the preferences", "[gui][preferences]")
{
    useCaseFolder("pageReads");
    GUI::preferences().setModuleUIMouseOverReaction(Preferences::WhenParentModuleSelected);
    GUI::preferences().setLFOUpdateBehaviour(Preferences::NoUpdate);
    GUI::preferences().setHideCursorOnKnobDrag(false);

    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(editorOnTheInterfacePage(instance));

    CHECK(mouseOverComboBox(editor).getSelectedID() == Preferences::WhenParentModuleSelected);
    CHECK(lfoUpdateComboBox(editor).getSelectedID() == Preferences::NoUpdate);
    CHECK(!hideCursorButton(editor).getToggleState());
}

TEST_CASE("Choosing a mouse-over reaction on the page writes it to the file", "[gui][preferences]")
{
    useCaseFolder("pageWritesTheComboBox");
    GUI::preferences().setModuleUIMouseOverReaction(Preferences::Never);

    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(editorOnTheInterfacePage(instance));

    auto &comboBox(mouseOverComboBox(editor));
    comboBox.setSelectedID(Preferences::WhenParentOrNothingSelected);
    Editor::Settings::comboBoxValueChanged(comboBox);

    CHECK(GUI::preferences().moduleUIMouseOverReaction() ==
          Preferences::WhenParentOrNothingSelected);

    // And on disk, which is the half issue #61 was about.
    Preferences const onDisk(GUI::preferences().file().parent_path());
    CHECK(onDisk.moduleUIMouseOverReaction() == Preferences::WhenParentOrNothingSelected);
}

TEST_CASE("Toggling hide-cursor-on-knob-drag on the page writes it to the file",
          "[gui][preferences]")
{
    useCaseFolder("pageWritesTheLED");
    GUI::preferences().setHideCursorOnKnobDrag(true);

    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(editorOnTheInterfacePage(instance));

    auto &button(hideCursorButton(editor));
    REQUIRE(button.getToggleState());

    /// \note sendNotificationSync, which is what makes this reachable headlessly:
    /// juce::Button calls its listeners from inside the call rather than posting.
    button.setToggleState(false, juce::sendNotificationSync);

    CHECK(!GUI::preferences().hideCursorOnKnobDrag());

    Preferences const onDisk(GUI::preferences().file().parent_path());
    CHECK(!onDisk.hideCursorOnKnobDrag());
}
