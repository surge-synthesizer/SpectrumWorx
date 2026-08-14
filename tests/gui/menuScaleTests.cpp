////////////////////////////////////////////////////////////////////////////////
///
/// menuScaleTests.cpp
/// ------------------
///
///   Whether a popup menu follows the zoom of the editor that opened it. A menu
/// is its own desktop window and inherits nothing: JUCE takes its scale from the
/// component the menu names as its target, and lays it out in that component's
/// unscaled coordinates. Name no component and the menu is drawn at 1:1 beside
/// an editor drawn at 1.5, which is what this is here to stop happening again.
///
/// \note Both cases open the same menu twice, at two zooms, and compare -- the
/// menu window's own bounds are in unscaled skin pixels either way, so "follows
/// the zoom" is exactly "lands on the same skin pixel and is drawn 1.5x bigger".
/// Each zoom brings JUCE up and takes it down again, so the second opening
/// cannot find the first one's window still on the desktop.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/editorHarness.hpp"

/// \note Before anything that names SW::Module. \see effectMenuTests.cpp.
#include "core/modules/moduleDSPAndGUI.hpp"

#include "gui/editor/zoomedEditor.hpp"
#include "gui/modules/moduleUI.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
//------------------------------------------------------------------------------
namespace
{
using namespace LE;
using namespace LE::SW;

using Editor = GUI::SpectrumWorxEditor;

/// What a menu came out as: how big it is drawn, and where in skin pixels.
struct OpenedMenu
{
    float scale;
    juce::Rectangle<int> bounds;
}; // struct OpenedMenu

/// \brief The menu window JUCE put on the desktop.
///
/// \note There is at most one: a menu is shown and read in the same breath here,
/// and JUCE is taken down between openings.
OpenedMenu measureOpenMenu()
{
    auto &desktop(juce::Desktop::getInstance());
    for (int index(0); index < desktop.getNumComponents(); ++index)
    {
        auto *const pComponent(desktop.getComponent(index));
        if (pComponent && (pComponent->getName() == "menu"))
            return {pComponent->getDesktopScaleFactor(), pComponent->getBounds()};
    }
    FAIL("No menu window reached the desktop.");
    return {};
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The same menu, in the same place, whatever the editor's zoom.
///
/// \note Give or take a pixel of position: a click arrives as a whole screen
/// pixel and 1.5 of those is not a whole skin pixel, so the round trip back into
/// the skin's coordinates can land either side. The size has no round trip in it
/// and is exact.
///
////////////////////////////////////////////////////////////////////////////////
void checkSamePlace(juce::Rectangle<int> const zoomed, juce::Rectangle<int> const unzoomed)
{
    INFO("1.0 " << unzoomed.toString() << " / 1.5 " << zoomed.toString());
    CHECK(zoomed.getWidth() == unzoomed.getWidth());
    CHECK(zoomed.getHeight() == unzoomed.getHeight());
    CHECK(std::abs(zoomed.getX() - unzoomed.getX()) <= 1);
    CHECK(std::abs(zoomed.getY() - unzoomed.getY()) <= 1);
}

/// The middle of the first strip, which is where a replacement is right-clicked.
juce::Point<int> const middleOfTheFirstStrip{
    GUI::ModuleUI::horizontalOffset + (GUI::ModuleUI::width / 2),
    GUI::ModuleUI::verticalOffset + (GUI::ModuleUI::height / 2)};

/// \brief Right-clicks the rack of an editor drawn at \p zoom.
///
/// \note JUCE and the editor both live and die inside this call, so that what
/// the next zoom finds on the desktop is only ever its own menu.
OpenedMenu rightClickTheRack(float const zoom)
{
    SWTest::HostSideJuce const juceLifetime;
    SWTest::Instance instance;
    instance.openEditor(Editor::PanelPlacement::overlay);
    auto &editor(instance.editor());
    editor.addUserAddedModule(0);
    editor.resyncModuleRack();
    editor.setTransform(juce::AffineTransform::scale(zoom));

    editor.showEffectMenuAt(editor.localPointToGlobal(middleOfTheFirstStrip));
    auto const measured(measureOpenMenu());
    juce::PopupMenu::dismissAllActiveMenus();
    return measured;
}

/// The same, for the other way a menu is asked for: below the widget it belongs to.
OpenedMenu belowTheEditor(float const zoom)
{
    SWTest::HostSideJuce const juceLifetime;
    SWTest::Instance instance;
    instance.openEditor(Editor::PanelPlacement::overlay);
    auto &editor(instance.editor());
    editor.setTransform(juce::AffineTransform::scale(zoom));

    GUI::PopupMenu menu;
    menu.addItem(0, "One");
    menu.addItem(1, "Two");
    menu.showCenteredBelow(editor, nullptr);
    auto const measured(measureOpenMenu());
    /// \note Off the bottom edge of the *skin*, which is what the editor still
    /// measures itself in however it is drawn.
    CHECK(measured.bounds.getY() == editor.getHeight());
    juce::PopupMenu::dismissAllActiveMenus();
    return measured;
}
//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("A right-clicked menu is drawn at the editor's zoom", "[gui][menu][zoom]")
{
    auto const unzoomed(rightClickTheRack(1.0f));
    auto const zoomed(rightClickTheRack(GUI::ZoomedEditor::defaultZoom));

    CHECK(unzoomed.scale == 1.0f);
    CHECK(zoomed.scale == GUI::ZoomedEditor::defaultZoom);
    /// \note The click is a screen position, and the strip it is on is 1.5x
    /// further across the screen when the editor is zoomed. The menu still has
    /// to come up on that strip.
    checkSamePlace(zoomed.bounds, unzoomed.bounds);
}

TEST_CASE("A menu below a widget is drawn at the widget's zoom", "[gui][menu][zoom]")
{
    auto const unzoomed(belowTheEditor(1.0f));
    auto const zoomed(belowTheEditor(GUI::ZoomedEditor::defaultZoom));

    CHECK(unzoomed.scale == 1.0f);
    CHECK(zoomed.scale == GUI::ZoomedEditor::defaultZoom);
    /// \note And this one hangs off the bottom edge, which is 1.5x lower down.
    checkSamePlace(zoomed.bounds, unzoomed.bounds);
}
