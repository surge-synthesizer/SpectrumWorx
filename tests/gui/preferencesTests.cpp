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

#include "gui/editor/zoomedEditor.hpp"
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

/// \brief The Interface page's combo boxes, told apart by how many choices each
/// offers rather than by the order they were added in.
///
/// \note A TabbedComponent keeps only the *current* page as a child, so the
/// engine page's three combo boxes -- which are TitledComboBoxes too -- are not
/// in the tree while the Interface tab is up. The count is required rather than
/// assumed, so that a layout change fails here instead of silently sending the
/// rest of the case at the wrong widget. Which is what it did: the colour scheme
/// box arrived as a fourth and this said so.
GUI::TitledComboBox &comboBoxOffering(Editor &editor, std::size_t const choices)
{
    /// Zoom and colour scheme, the page's three switches being LEDs.
    std::size_t constexpr onTheInterfacePage{2};

    auto const comboBoxes(descendantsOfType<GUI::TitledComboBox>(editor));
    REQUIRE(comboBoxes.size() == onTheInterfacePage);

    for (auto *const pComboBox : comboBoxes)
        if (pComboBox->numberOfItems() == choices)
            return *pComboBox;

    FAIL("no combo box on the Interface page offers " << choices << " choices");
    return *comboBoxes.front();
}

GUI::TitledComboBox &zoomComboBox(Editor &editor)
{
    return comboBoxOffering(editor, Preferences::zoomPercentages.size());
}

/// \brief One of the Interface page's LEDs, by the caption it carries.
///
/// \note Found through the zoom box's parent so that the module strips' own LEDs
/// cannot be picked up instead, and by name because there are three of them.
GUI::LEDTextButton &ledCaptioned(Editor &editor, juce::String const &caption)
{
    auto *const pPage(zoomComboBox(editor).getParentComponent());
    REQUIRE(pPage != nullptr);

    auto const buttons(descendantsOfType<GUI::LEDTextButton>(*pPage));
    REQUIRE(buttons.size() == 3);

    for (auto *const pButton : buttons)
        if (pButton->getName() == caption)
            return *pButton;

    FAIL("no LED on the Interface page is captioned " << caption);
    return *buttons.front();
}

GUI::LEDTextButton &hideCursorButton(Editor &editor)
{
    return ledCaptioned(editor, "Hide cursor on knob edits");
}
GUI::LEDTextButton &lfoAnimationButton(Editor &editor)
{
    return ledCaptioned(editor, "Animate LFO modulations");
}
GUI::LEDTextButton &lfoPreviewButton(Editor &editor)
{
    return ledCaptioned(editor, "Hover shows LFO settings");
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

    CHECK(preferences.showLFOAnimation());
    CHECK(preferences.previewLFOOnHover());
    CHECK(preferences.hideCursorOnKnobDrag());
    CHECK(preferences.zoomPercent() == Preferences::defaultZoomPercent);

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
        written.setShowLFOAnimation(false);
        written.setPreviewLFOOnHover(false);
        written.setPalette(GUI::ColourMap::ClassicRed);
        written.setHideCursorOnKnobDrag(false);
        written.setZoomPercent(75);

        REQUIRE(fs::exists(written.file()));
    }

    Preferences const read(folder);
    CHECK(!read.showLFOAnimation());
    CHECK(!read.previewLFOOnHover());
    CHECK(read.palette() == GUI::ColourMap::ClassicRed);
    CHECK(!read.hideCursorOnKnobDrag());
    CHECK(read.zoomPercent() == 75);
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
    preferences.setPalette(GUI::ColourMap::ClassicGreen);
    preferences.setShowLFOAnimation(false);
    preferences.setHideCursorOnKnobDrag(false);
    preferences.setZoomPercent(125);

    auto const file(contentsOf(preferences.file()));
    CAPTURE(file);

    CHECK(file.find("key=\"palette\" value=\"ClassicGreen\"") != std::string::npos);
    CHECK(file.find("key=\"showLFOAnimation\" value=\"0\"") != std::string::npos);
    CHECK(file.find("key=\"hideCursorOnKnobDrag\" value=\"0\"") != std::string::npos);
    CHECK(file.find("key=\"zoomPercent\" value=\"125\"") != std::string::npos);
}

TEST_CASE("A value this build does not recognise reads as the default", "[gui][preferences]")
{
    // The file is the user's to edit, and a downgrade is the same case.
    auto const folder(caseFolder("unrecognised"));

    write(folder / "SpectrumWorxUserDefaults.xml",
          "<?xml version = \"1.0\" encoding = \"UTF-8\" ?>\n"
          "<defaults version=\"1\">\n"
          "  <default key=\"palette\" value=\"Sideways\" type=\"1\"/>\n"
          "  <default key=\"zoomPercent\" value=\"300\" type=\"2\"/>\n"
          "  <default key=\"showLFOAnimation\" value=\"0\" type=\"2\"/>\n"
          "</defaults>\n");

    Preferences const preferences(folder);

    CHECK(preferences.palette() == GUI::ColourMap::ClassicBlue);

    /// \note 300 is a zoom, and a reasonable one -- issue #55 asks for it -- but
    /// it is not one this build offers, and the combo box could show nothing for
    /// it. A zoom is checked against the offered list rather than clamped to its
    /// ends for that reason.
    REQUIRE(!Preferences::isOfferedZoom(300));
    CHECK(preferences.zoomPercent() == Preferences::defaultZoomPercent);

    // ...and neither unreadable key cost it the one beside it.
    CHECK(!preferences.showLFOAnimation());
}

TEST_CASE("Every offered zoom scales the skin by its own percentage", "[gui][preferences]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note What "100%" means, pinned: no transform at all. The skin was drawn
    /// at 1.5x until 19.08.2026 and its constants were rescaled rather than that
    /// factor being kept, so the window is the size it always was and 100 % is
    /// now literally one. Every size the host is told goes through this.
    /// \see ZoomedEditor.
    ///
    ////////////////////////////////////////////////////////////////////////////
    using Zoomed = GUI::ZoomedEditor;

    useCaseFolder("scaling");

    CHECK(Zoomed::scaleForZoom(100) == 1.0f);
    CHECK(Zoomed::scaleForZoom(200) == 2.0f);
    CHECK(Zoomed::scaleForZoom(50) == 0.5f);

    for (auto const percent : Preferences::zoomPercentages)
    {
        CAPTURE(percent);
        GUI::preferences().setZoomPercent(percent);

        CHECK(Zoomed::scaledForCurrentZoom(Editor::estimatedWidth) ==
              Zoomed::scaled(Editor::estimatedWidth, Zoomed::scaleForZoom(percent)));
    }
}

////////////////////////////////////////////////////////////////////////////////
// The Interface page
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The Interface page opens showing what is in the preferences", "[gui][preferences]")
{
    useCaseFolder("pageReads");
    GUI::preferences().setZoomPercent(75);
    GUI::preferences().setShowLFOAnimation(false);
    GUI::preferences().setHideCursorOnKnobDrag(false);

    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(editorOnTheInterfacePage(instance));

    CHECK(zoomComboBox(editor).getSelectedID() == 75);
    CHECK(!lfoAnimationButton(editor).getToggleState());
    CHECK(lfoPreviewButton(editor).getToggleState());
    CHECK(!hideCursorButton(editor).getToggleState());
}

TEST_CASE("Toggling the LFO switches on the page writes them to the file", "[gui][preferences]")
{
    useCaseFolder("pageWritesTheLFOLEDs");
    GUI::preferences().setShowLFOAnimation(true);
    GUI::preferences().setPreviewLFOOnHover(true);

    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(editorOnTheInterfacePage(instance));

    /// \note sendNotificationSync, which is what makes this reachable headlessly:
    /// juce::Button calls its listeners from inside the call rather than posting.
    lfoAnimationButton(editor).setToggleState(false, juce::sendNotificationSync);
    lfoPreviewButton(editor).setToggleState(false, juce::sendNotificationSync);

    CHECK(!GUI::preferences().showLFOAnimation());
    CHECK(!GUI::preferences().previewLFOOnHover());

    // And on disk, which is the half issue #61 was about.
    Preferences const onDisk(GUI::preferences().file().parent_path());
    CHECK(!onDisk.showLFOAnimation());
    CHECK(!onDisk.previewLFOOnHover());
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

////////////////////////////////////////////////////////////////////////////////
// The zoom
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("An editor opens at the zoom the last session chose", "[gui][preferences][zoom]")
{
    useCaseFolder("zoomAtOpening");
    GUI::preferences().setZoomPercent(75);

    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;

    // Built the way SpectrumWorxCLAP::createEditor() builds it: nothing tells the
    // wrapper what zoom to use, so what it opens at is what was remembered.
    GUI::ZoomedEditor const wrapper(
        std::make_unique<Editor>(instance, Editor::PanelPlacement::overlay));

    CHECK(wrapper.zoomPercent() == 75);

    /// \note And the skin did not move: the editor is laid out in the 845 x 564
    /// it always was and the wrapper is its scaled shadow. That split is what
    /// keeps every offset in the editor a constant.
    CHECK(wrapper.editor().getWidth() == Editor::estimatedWidth);
    CHECK(wrapper.getWidth() ==
          GUI::ZoomedEditor::scaled(Editor::estimatedWidth, GUI::ZoomedEditor::scaleForZoom(75)));
}

TEST_CASE("Choosing a zoom on the page resizes the editor and remembers it",
          "[gui][preferences][zoom]")
{
    useCaseFolder("pageWritesTheZoom");
    GUI::preferences().setZoomPercent(100);

    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;

    GUI::ZoomedEditor wrapper(std::make_unique<Editor>(instance, Editor::PanelPlacement::overlay));
    auto &editor(wrapper.editor());
    editor.showSettings(Editor::interfacePageIndex);

    auto &comboBox(zoomComboBox(editor));
    REQUIRE(comboBox.getSelectedID() == 100);
    auto const atOneHundred(wrapper.getWidth());

    comboBox.setSelectedID(200);
    Editor::Settings::comboBoxValueChanged(comboBox);

    CHECK(wrapper.zoomPercent() == 200);
    CHECK(wrapper.getWidth() > atOneHundred);
    CHECK(wrapper.getWidth() ==
          GUI::ZoomedEditor::scaled(Editor::estimatedWidth, GUI::ZoomedEditor::scaleForZoom(200)));

    // The skin is where it was; only the transform over it moved.
    CHECK(editor.getWidth() == Editor::estimatedWidth);

    /// \note And the window was told, in skin pixels -- the scaling into window
    /// units is SpectrumWorxCLAP's and is measured against a real host in
    /// tests/clap/pluginTests.cpp. Without this the editor would be drawn at the
    /// new scale inside a window still the old size.
    ///
    /// \note `announcedSizes`, not `requestedSizes`: a zoom is not a request and
    /// nothing may make it conditional on the host's answer. \see
    /// EditorHost::editorSizeChanged().
    CHECK(instance.requestedSizes.empty());
    REQUIRE(!instance.announcedSizes.empty());
    CHECK(instance.announcedSizes.back() ==
          juce::Point<int>{Editor::estimatedWidth, Editor::estimatedHeight});

    CHECK(GUI::preferences().zoomPercent() == 200);

    Preferences const onDisk(GUI::preferences().file().parent_path());
    CHECK(onDisk.zoomPercent() == 200);
}
