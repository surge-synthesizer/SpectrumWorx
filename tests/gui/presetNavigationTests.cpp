////////////////////////////////////////////////////////////////////////////////
///
/// presetNavigationTests.cpp
/// -------------------------
///
///   The preset browser's navigation row, which issue #44 put where "Ignore
/// external audio" used to be: up a folder, a User toggle, and a jog that steps
/// between the presets in whatever is listed.
///
///   And the thing the row exists to make possible: the browser opens *inside*
/// a tree rather than on a two-row listing of the word "Factory" and the word
/// "User". That was the report -- a plugin with hundreds of presets in it showed two
/// rows and no preset names, every time the window opened.
///
/// \note Through the list box and the buttons rather than through pixels, unlike
/// overlayPanelTests.cpp beside it. What is being asked here is "which rows are
/// there and which one is selected", and the model answers that exactly; a
/// render would answer it by proxy and fail for a dozen reasons that are not
/// this.
///
/// \note `mouseDown` and `mouseUp` rather than `triggerClick()`: JUCE 8 posts
/// the latter through the message queue, and there is no loop running one in a
/// test binary.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/editorHarness.hpp"

/// \note Before anything that names SW::Module, as overlayPanelTests.cpp does:
/// the module chain downcasts a node to it.
#include "core/modules/moduleDSPAndGUI.hpp"

#include "gui/gui.hpp"
#include "le/spectrumworx/factoryPresets.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
using namespace LE;
using namespace LE::SW;

using Editor = GUI::SpectrumWorxEditor;

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

/// \brief The browser's list, which is the only one in the editor.
juce::ListBox &listOf(Editor &editor)
{
    auto const lists(descendantsOfType<juce::ListBox>(editor));
    REQUIRE(lists.size() == 1);
    return *lists.front();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \enum Nav
///
/// \brief The navigation row, by position in it.
///
/// \note The order is the order the four are constructed in, which is also left
/// to right. Both are asserted below rather than assumed: a fifth glyph, or a
/// reordering, should fail here and not silently send the rest of a case at the
/// wrong button.
///
////////////////////////////////////////////////////////////////////////////////

enum Nav
{
    Up,
    User,
    JogPrevious,
    JogNext,
    numberOfNavButtons
};

std::vector<GUI::GlyphButton *> navigationRow(Editor &editor)
{
    /// \note Told from the editor's own lock -- "ignore external audio", beside
    /// the sidechain source, which is a GlyphButton too -- by its parent rather
    /// than by its index: these four are on the browser panel and that one is
    /// not.
    auto *const pPanel(listOf(editor).getParentComponent());

    std::vector<GUI::GlyphButton *> inBrowser;
    for (auto *const pButton : descendantsOfType<GUI::GlyphButton>(editor))
        if (pButton->getParentComponent() == pPanel)
            inBrowser.push_back(pButton);

    REQUIRE(inBrowser.size() == numberOfNavButtons);
    for (unsigned int button(1); button < numberOfNavButtons; ++button)
        REQUIRE(inBrowser[button - 1]->getX() < inBrowser[button]->getX());

    return inBrowser;
}

/// \brief Presses \p button as a user does, synchronously.
///
/// \note Half a mouse, as tests/gui/moduleControlFocusTests.cpp explains at
/// length: JUCE tracks a gesture in the MouseInputSource and nothing here
/// touches that. Enough for a button, which acts on the release.
void click(juce::Button &button)
{
    auto const centre(button.getLocalBounds().getCentre().toFloat());
    juce::MouseEvent const press(juce::Desktop::getInstance().getMainMouseSource(), centre,
                                 juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f,
                                 0.0f, 0.0f, 0.0f, 0.0f, &button, &button, juce::Time(), centre,
                                 juce::Time(), 1, false);
    /// \note Through the Component, because juce::Button narrows these two to
    /// protected -- a button expects to be pressed by JUCE and not by its
    /// neighbours. The base class's declaration is the public one.
    auto &component(static_cast<juce::Component &>(button));
    component.mouseDown(press);
    component.mouseUp(press);
}

/// How many rows the browser is listing.
int rowCount(Editor &editor) { return listOf(editor).getListBoxModel()->getNumRows(); }

////////////////////////////////////////////////////////////////////////////////
///
/// \brief How many banks the top of the factory tree has: the entries in
/// FactoryPresets::banks() with no separator in them.
///
/// \note Counted here rather than written down, so that adding a bank to the
/// binary does not fail these cases. What they are about is that the browser
/// lists *the banks* rather than the word "Factory", and that is a comparison
/// against this number and not against a literal.
///
////////////////////////////////////////////////////////////////////////////////

int topLevelBankCount()
{
    int banks(0);
    for (auto const &bank : FactoryPresets::banks())
        banks += (bank.find('/') == std::string::npos);
    return banks;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The browser, opened on \p instance with the panels over the strips, at
/// the top of the factory tree.
///
/// \note Put there rather than found there. Where the browser was last left is
/// *process-wide* -- PresetBrowser::lastPlace(), which is what makes a second
/// instance open where the first one was -- so in one test binary each case
/// inherits wherever the previous one wandered to. Two cases here failed on
/// that and neither was about it.
///
////////////////////////////////////////////////////////////////////////////////

Editor &browserEditor(SWTest::Instance &instance)
{
    instance.openEditor(Editor::PanelPlacement::overlay);
    auto &editor(instance.editor());
    editor.showFactoryBank({});
    return editor;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
//
// The reported bug: the browser opened on two words.
//
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The preset browser opens inside the factory tree", "[gui][presets]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(browserEditor(instance));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Against the *banks*, which is the number this has to be: "more than
    /// two" would pass on the root listing plus a `..` row, which is exactly the
    /// state being fixed.
    ///
    ////////////////////////////////////////////////////////////////////////////
    CHECK(rowCount(editor) == topLevelBankCount());
}

TEST_CASE("Up is disabled at the top of a tree and enabled below it", "[gui][presets]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(browserEditor(instance));

    auto const row(navigationRow(editor));

    CHECK_FALSE(row[Up]->isEnabled());

    editor.showFactoryBank("Echoes");
    CHECK(row[Up]->isEnabled());

    //   ...and it goes back up to the bank list rather than to a root listing.
    click(*row[Up]);
    CHECK_FALSE(row[Up]->isEnabled());
    CHECK(rowCount(editor) == topLevelBankCount());
}

TEST_CASE("The User toggle swaps trees and says which one is showing", "[gui][presets]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(browserEditor(instance));

    auto const row(navigationRow(editor));

    CHECK_FALSE(row[User]->getToggleState());
    auto const factoryRows(rowCount(editor));

    click(*row[User]);
    CHECK(row[User]->getToggleState());
    //   A fresh user folder is empty, and that is the point: the toggle went
    //   somewhere other than the factory banks.
    CHECK(rowCount(editor) != factoryRows);

    click(*row[User]);
    CHECK_FALSE(row[User]->getToggleState());
    CHECK(rowCount(editor) == factoryRows);
}

////////////////////////////////////////////////////////////////////////////////
//
// The jog
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// The jog
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note These select rows, and selecting a preset *loads* it -- so they take
/// an Instance whose engine was never set up. \see the note on that argument in
/// editorHarness.hpp; the short version is that re-setting the engine up for a
/// preset's FFT size is the CLAP layer's job and there is no CLAP layer here.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The jog is dead until a preset is selected", "[gui][presets]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(browserEditor(instance));

    auto const row(navigationRow(editor));

    REQUIRE(rowCount(editor) == topLevelBankCount()); // the banks, and nothing else
    REQUIRE(listOf(editor).getLastRowSelected() == -1);

    CHECK_FALSE(row[JogPrevious]->isEnabled());
    CHECK_FALSE(row[JogNext]->isEnabled());

    //   ...and inside a bank, where there *are* presets but none of them is the
    // one being listened to yet.
    editor.showFactoryBank("Echoes");

    REQUIRE(rowCount(editor) > 0);
    REQUIRE(listOf(editor).getLastRowSelected() == -1);

    CHECK_FALSE(row[JogPrevious]->isEnabled());
    CHECK_FALSE(row[JogNext]->isEnabled());
}

TEST_CASE("Each jog arrow is dead at its own end of the folder", "[gui][presets]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance(false /*an engine that was never set up*/);
    auto &editor(browserEditor(instance));

    editor.showFactoryBank("Echoes");
    auto const row(navigationRow(editor));
    auto &list(listOf(editor));

    auto const presets(rowCount(editor)); // a bank holds no sub-folders
    REQUIRE(presets > 2);

    list.selectRow(0);
    CHECK_FALSE(row[JogPrevious]->isEnabled()); // the first preset has no back
    CHECK(row[JogNext]->isEnabled());

    list.selectRow(presets - 1);
    CHECK(row[JogPrevious]->isEnabled());
    CHECK_FALSE(row[JogNext]->isEnabled()); // and the last one has no forward

    list.selectRow(1);
    CHECK(row[JogPrevious]->isEnabled());
    CHECK(row[JogNext]->isEnabled());
}

TEST_CASE("The jog steps between the presets in a bank", "[gui][presets]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance(false /*an engine that was never set up*/);
    auto &editor(browserEditor(instance));

    editor.showFactoryBank("Echoes");
    auto const row(navigationRow(editor));
    auto &list(listOf(editor));

    list.selectRow(0);

    click(*row[JogNext]);
    CHECK(list.getLastRowSelected() == 1);

    click(*row[JogNext]);
    CHECK(list.getLastRowSelected() == 2);

    click(*row[JogPrevious]);
    CHECK(list.getLastRowSelected() == 1);

    click(*row[JogPrevious]);
    CHECK(list.getLastRowSelected() == 0);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Clamped rather than wrapped. The button is disabled here, so this
    /// is belt and braces -- what it pins is that stepPreset() and the
    /// enablement ask the same question, which is what canStep() is for.
    ///
    ////////////////////////////////////////////////////////////////////////////
    click(*row[JogPrevious]);
    CHECK(list.getLastRowSelected() == 0);
}
