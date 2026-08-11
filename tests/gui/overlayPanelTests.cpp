////////////////////////////////////////////////////////////////////////////////
///
/// overlayPanelTests.cpp
/// ---------------------
///
///   The two panels that share one rectangle: that opening either one paints
/// over it, that each of the settings tabs paints a *page* and not just a tab
/// bar, and that the two are mutually exclusive.
///
///   And where that rectangle is, which is SpectrumWorxEditor::PanelPlacement's
/// question: over the module strips, in a column the editor asks its host for, or
/// in one it never gives back. The cases below the panel-placement section pin
/// the first, because what they measure is the overlay rectangle.
///
///   Written against the bug 6.4 found by looking at a picture. Clicking the
/// SpectrumWorx logo asked the settings panel for tab 3 of three; JUCE clamps an
/// out-of-range index to -1, so the panel opened with no page in it. As a
/// transparent desktop window that was invisible; as an overlay it is a hole in
/// the editor. Nothing headless could see it, because `sw-show-ui --render`
/// asserted an exit code and a panel that paints nothing exits 0. `--render`
/// measures the whole canvas now; a 191 x 363 hole in a 563 x 376 editor is 33 %
/// of it and would pass that floor comfortably, so the region is what has to be
/// looked at, and that is this file.
///
/// \note Pixels rather than the component tree, deliberately. "Is the About page
/// a child of the tabbed component" is a question the broken build answered
/// correctly -- the page existed, the tab index did not select it. What was wrong
/// is what was on the screen, so that is what is measured.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/editorHarness.hpp"

/// \note Before anything that names SW::Module: the module chain downcasts a
/// node to it, and this is the header with the complete type.
#include "core/modules/moduleDSPAndGUI.hpp"

#include "gui/modules/moduleUI.hpp"
#include "le/spectrumworx/factoryPresets.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <map>
#include <string>
//------------------------------------------------------------------------------
namespace
{
using namespace LE;
using namespace LE::SW;

using Editor = GUI::SpectrumWorxEditor;

/// The rectangle a panel is given when it is laid over the module strips.
juce::Rectangle<int> overlayRectangle()
{
    return {Editor::overlayX, Editor::overlayY, Editor::overlayWidth, Editor::overlayHeight};
}

/// The rectangle it is given when the editor has grown a column for it.
juce::Rectangle<int> panelColumnRectangle()
{
    return {Editor::panelColumnX, Editor::overlayY, Editor::overlayWidth, Editor::overlayHeight};
}

/// The editor as it was before any of this: 563 x 376, panels over the strips.
juce::Rectangle<int> unexpandedEditor()
{
    return {0, 0, Editor::estimatedWidth, Editor::estimatedHeight};
}

/// \brief The five module slots -- what an overlay covers and a column does not.
///
/// \note Wider than overlayRectangle(), and that is the point: an overlay hides
/// slots 3 to 5, so a case that only compared its own rectangle would pass while
/// the panel ate the first two. This is the region the whole change is about.
juce::Rectangle<int> moduleRack()
{
    return {GUI::ModuleUI::horizontalOffset, GUI::ModuleUI::verticalOffset,
            SW::Constants::maxNumberOfModules * (GUI::ModuleUI::width + GUI::ModuleUI::distance),
            GUI::ModuleUI::height};
}

/// \brief An editor whose panels go over the module strips.
///
/// \note Said rather than assumed. The default placement grows the editor, so a
/// case that means to measure the overlay rectangle has to ask for the overlay --
/// left alone these would be comparing a 563 px render with a 764 px one.
Editor &overlayEditor(SWTest::Instance &instance)
{
    instance.openEditor(Editor::PanelPlacement::overlay);
    return instance.editor();
}

/// \brief The editor as an image, which is what `--render` does and what a user
/// sees.
juce::Image rendered(Editor &editor)
{
    juce::Image image(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
    juce::Graphics graphics(image);
    editor.paintEntireComponent(graphics, true);
    return image;
}

/// \brief What fraction of \p area two renders disagree about.
///
/// \note The two need not be the same size, only both big enough to hold \p area:
/// the placement cases compare the left 563 px of a grown editor against the
/// editor it grew from, which is the whole question those cases ask.
double differenceOver(juce::Image const &left, juce::Image const &right,
                      juce::Rectangle<int> const &area)
{
    REQUIRE(right.getBounds().contains(area));
    REQUIRE(left.getBounds().contains(area));

    juce::Image::BitmapData const leftPixels(left, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData const rightPixels(right, juce::Image::BitmapData::readOnly);

    std::size_t different{0};
    for (int y(area.getY()); y < area.getBottom(); ++y)
        for (int x(area.getX()); x < area.getRight(); ++x)
            different += (leftPixels.getPixelColour(x, y) != rightPixels.getPixelColour(x, y));

    return double(different) / double(area.getWidth() * area.getHeight());
}

/// \brief The smallest rectangle holding every pixel two renders disagree about
/// outside \p ignore, or an empty one if they agree everywhere outside it.
juce::Rectangle<int> differenceBoundsOutside(juce::Image const &left, juce::Image const &right,
                                             juce::Rectangle<int> const &ignore)
{
    REQUIRE(left.getBounds() == right.getBounds());

    juce::Image::BitmapData const leftPixels(left, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData const rightPixels(right, juce::Image::BitmapData::readOnly);

    juce::Rectangle<int> bounds;
    for (int y(0); y < left.getHeight(); ++y)
        for (int x(0); x < left.getWidth(); ++x)
        {
            if (ignore.contains(x, y))
                continue;
            if (leftPixels.getPixelColour(x, y) == rightPixels.getPixelColour(x, y))
                continue;
            bounds =
                bounds.isEmpty() ? juce::Rectangle<int>(x, y, 1, 1) : bounds.getUnion({x, y, 1, 1});
        }
    return bounds;
}

/// \brief How much of \p area is something other than its commonest colour.
///
/// \see tools/show-ui/main.cpp, which asks the same question of a whole page.
double drawnFractionOver(juce::Image const &image, juce::Rectangle<int> const &area)
{
    juce::Image::BitmapData const pixels(image, juce::Image::BitmapData::readOnly);

    std::map<juce::uint32, std::size_t> histogram;
    for (int y(area.getY()); y < area.getBottom(); ++y)
        for (int x(area.getX()); x < area.getRight(); ++x)
            ++histogram[pixels.getPixelColour(x, y).getARGB()];

    std::size_t modal{0};
    for (auto const &[colour, count] : histogram)
        modal = std::max(modal, count);

    auto const total(std::size_t(area.getWidth()) * std::size_t(area.getHeight()));
    return double(total - modal) / double(total);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Measured on 03.08.2026 rather than guessed. A real panel covers 96-99 %
/// of the rectangle; a panel with no page in it covers its 16 px tab bar and
/// nothing else, which measures **2.8 %** -- taken by reverting the 6.4 fix and
/// reading the number off the failure, not by arithmetic on the tab bar's height.
/// Two thirds is a long way below the first and a long way above the second,
/// which is the whole of what this constant has to be.
///
////////////////////////////////////////////////////////////////////////////////

constexpr double leastOfTheRectangleAPanelCovers{0.66};

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("Opening the preset browser paints over the overlay rectangle", "[gui][overlay]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(overlayEditor(instance));

    auto const closed(rendered(editor));

    editor.showPresetBrowser(true);
    auto const open(rendered(editor));

    CHECK(differenceOver(closed, open, overlayRectangle()) > leastOfTheRectangleAPanelCovers);
    CHECK(drawnFractionOver(open, overlayRectangle()) > 0);

    // ...and shutting it puts the editor back, so the panel is an overlay rather
    // than something that ate what was under it.
    editor.showPresetBrowser(false);
    CHECK(differenceOver(closed, rendered(editor), overlayRectangle()) == 0);
}

TEST_CASE("Every settings tab paints a page and not just a tab bar", "[gui][overlay]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The shape of the 6.4 bug, stated. "The panel opened and there was
    /// nothing in it" is, as a fraction of the overlay rectangle, the 16 px tab
    /// bar against the 99 % a real page covers -- so a page that fails to build
    /// or fails to be selected is a large, obvious number here, and was an exit
    /// code of 0 before.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(overlayEditor(instance));

    auto const closed(rendered(editor));

    for (unsigned int page(0); page < Editor::numberOfSettingsPages; ++page)
    {
        CAPTURE(page);
        editor.showSettings(page);
        auto const open(rendered(editor));

        CHECK(differenceOver(closed, open, overlayRectangle()) > leastOfTheRectangleAPanelCovers);
    }
}

TEST_CASE("Clicking the logo opens the About page, not an empty panel", "[gui][overlay]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The 6.4 bug itself, and it is a case of its own because the index
    /// was never the panel's: `showSettings()` was given `numberOfSettingsPages`
    /// by a mouse handler, JUCE clamped 3-of-three to -1, and the panel opened
    /// with no page selected. Every case above calls `showSettings()` with an
    /// index it chose itself and so can never see it -- checked by putting the
    /// old value back and watching them stay green, which is what sent this case
    /// through `mouseDown` instead.
    ///
    ///   With the old value back this one goes red, and by a margin that says the
    /// measure is the right one: the panel covers **2.8 %** of its rectangle
    /// instead of 99 %. That 2.8 % is the tab bar, drawn over nothing.
    ///                                       (03.08.2026.) (SW port)
    ///
    /// \note Through `juce::Component`, because the override is private on the
    /// editor and public on the base -- access is checked against the static type
    /// of the expression, and a mouse handler being unreachable from the outside
    /// is exactly why this path had no coverage.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;

    // What the About page looks like, opened the way the settings button opens it.
    auto const closed(rendered(overlayEditor(instance)));
    instance.editor().showSettings(Editor::aboutPageIndex);
    auto const aboutPage(rendered(instance.editor()));

    /// \note A second editor rather than shutting the panel on the first:
    /// `showSettings()` is what the button's handler calls *after* deciding to
    /// open, so it is not a toggle and calling it again leaves the panel up.
    instance.closeEditor();
    auto &editor(overlayEditor(instance));
    REQUIRE(differenceOver(closed, rendered(editor), overlayRectangle()) == 0);

    /// \note (37, 321) is the middle of the logo's hit area, which
    /// `SpectrumWorxEditor::mouseDown` spells as {12, 290, 51, 63}.
    juce::Point<float> const logo(37, 321);
    static_cast<juce::Component &>(editor).mouseDown(juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(), logo,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        &editor, &editor, juce::Time(), logo, juce::Time(), 1, false));

    auto const clicked(rendered(editor));

    // It opened something...
    CHECK(differenceOver(closed, clicked, overlayRectangle()) > leastOfTheRectangleAPanelCovers);
    // ...and it is the About page rather than a panel with no page in it.
    CHECK(differenceOver(aboutPage, clicked, overlayRectangle()) == 0);
}

TEST_CASE("The two panels are mutually exclusive and land in the same place", "[gui][overlay]")
{
    // `showPanel()` asserts the invariant; this is what it looks like on screen.
    // There is one 191 x 363 rectangle whatever the placement -- so "open the
    // other one" has to mean "and shut this one", or they draw on top of each
    // other.
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(overlayEditor(instance));

    editor.showSettings(Editor::interfacePageIndex);
    auto const settingsAlone(rendered(editor));

    // Through the browser, which is the order tools/show-ui's editor-settings
    // page uses and the order a user takes.
    editor.showPresetBrowser(true);
    auto const browser(rendered(editor));
    CHECK(differenceOver(settingsAlone, browser, overlayRectangle()) > 0);

    editor.showSettings(Editor::interfacePageIndex);
    CHECK(differenceOver(settingsAlone, rendered(editor), overlayRectangle()) == 0);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Neither panel paints outside its rectangle -- but swapping them is
    /// not confined to it either, and the difference is the point. 660 pixels of
    /// the 563 x 376 editor change out there, and they are the two buttons that
    /// opened the panels: `showSettings()` un-toggles the presets button and the
    /// other way about, which is what makes the exclusion visible to a user
    /// rather than only true.
    ///
    ///   Stated as "everything that moved is in the left column", because that
    /// is where those buttons are and it needs no constant this file does not
    /// already have. An overlay that leaked past its own edge would land in the
    /// module strips, which are to the right of `overlayX`.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const moved(differenceBoundsOutside(settingsAlone, browser, overlayRectangle()));
    CAPTURE(moved.toString().toStdString());

    auto const leftColumn(editor.getLocalBounds().withWidth(Editor::overlayX));
    CHECK(!moved.isEmpty());
    CHECK(leftColumn.contains(moved));
}

////////////////////////////////////////////////////////////////////////////////
//
// Panel placement
// ---------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   The overlay is the arrangement stage 6.4 could ship: a fixed-size editor has
/// exactly one panel-sized rectangle in it, and it is the one the module strips
/// are drawn in. Which means opening the preset browser hides the rack the user
/// is working on -- correct, and not what anyone wants while an effect is live.
///
///   So the editor asks its host for the space instead. `expandContract` takes a
/// column while a panel is up and gives it back; `alwaysVisible` never gives it
/// back and rests on the preset browser. Both come down to one question a picture
/// can answer -- **is anything under the panel different** -- and that is what
/// these ask, over `moduleRack()`: the five slots, not just the rectangle a panel
/// used to land in. The rest of the old editor is not held to it, because the
/// button that opened the panel legitimately lights up.
///
/// \note The refusal case is not an afterthought. `request_resize` is the host's
/// to decline and some will, so `expandContract` against a host that says no has
/// to *be* the overlay rather than an editor drawn wider than its window. That is
/// the case with the least chance of ever being exercised by hand.
///                                           (06.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("What a plugin opens with is the column, with presets in it", "[gui][overlay]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The shipping arrangement, named as such. Everything else here picks
    /// a placement, so nothing else would notice the default changing -- and the
    /// default is what every user sees on the first open.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    instance.openEditor();
    auto &editor(instance.editor());

    CHECK(editor.panelPlacement() == Editor::PanelPlacement::alwaysVisible);
    CHECK(editor.panelHasOwnColumn());
    CHECK(editor.getWidth() == Editor::expandedWidth);

    // The browser is in the column, and the rack is not under it -- said against
    // an editor with no panel up at all, which is the rack undisturbed.
    auto const opened(rendered(editor));
    CHECK(drawnFractionOver(opened, panelColumnRectangle()) > 0);

    SWTest::Instance bare;
    CHECK(differenceOver(opened, rendered(overlayEditor(bare)), moduleRack()) == 0);

    /// \note And the same picture as asking for the browser by hand, which is
    /// what says the resting panel is the browser rather than something that
    /// merely fills the space.
    editor.showSettings(Editor::interfacePageIndex);
    REQUIRE(differenceOver(opened, rendered(editor), panelColumnRectangle()) > 0);
    editor.showPresetBrowser(true);
    CHECK(differenceOver(opened, rendered(editor), panelColumnRectangle()) == 0);

    // Nothing was ever asked of the host: the column is taken at construction.
    CHECK(instance.requestedSizes.empty());
}

TEST_CASE("A panel gets a column of its own rather than the module strips", "[gui][overlay]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    instance.openEditor(Editor::PanelPlacement::expandContract);
    auto &editor(instance.editor());

    REQUIRE(editor.getWidth() == Editor::estimatedWidth);
    REQUIRE_FALSE(editor.panelHasOwnColumn());

    auto const closed(rendered(editor));

    editor.showPresetBrowser(true);
    auto const open(rendered(editor));

    // The editor grew, and asked its host to grow with it.
    CHECK(editor.panelHasOwnColumn());
    CHECK(editor.getWidth() == Editor::expandedWidth);
    REQUIRE(instance.requestedSizes.size() == 1);
    CHECK(instance.requestedSizes.back() ==
          juce::Point<int>(Editor::expandedWidth, Editor::estimatedHeight));

    // The browser is in the new column...
    CHECK(drawnFractionOver(open, panelColumnRectangle()) > 0);
    CHECK(differenceOver(closed, open, overlayRectangle()) == 0);
    // ...and the rack the user was working on is untouched, which is the point.
    CHECK(differenceOver(closed, open, moduleRack()) == 0);

    // Shutting it gives the space back and asks for that too.
    editor.showPresetBrowser(false);
    CHECK_FALSE(editor.panelHasOwnColumn());
    CHECK(editor.getWidth() == Editor::estimatedWidth);
    REQUIRE(instance.requestedSizes.size() == 2);
    CHECK(instance.requestedSizes.back() ==
          juce::Point<int>(Editor::estimatedWidth, Editor::estimatedHeight));
    // ...and now the *whole* editor is back to where it started, buttons included.
    CHECK(differenceOver(closed, rendered(editor), unexpandedEditor()) == 0);
}

TEST_CASE("Swapping panels in the column asks the host for nothing", "[gui][overlay]")
{
    /// \note The two share one rectangle wherever it is, so going from one to the
    /// other is not a resize -- and a `request_resize` per swap would be a window
    /// flickering its width for no reason a user could see. `setPanelColumnVisible`
    /// returning early on "already there" is what this is.
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    instance.openEditor(Editor::PanelPlacement::expandContract);
    auto &editor(instance.editor());

    editor.showPresetBrowser(true);
    REQUIRE(instance.requestedSizes.size() == 1);

    editor.showSettings(Editor::interfacePageIndex);
    CHECK(instance.requestedSizes.size() == 1);
    CHECK(editor.getWidth() == Editor::expandedWidth);

    editor.showPresetBrowser(true);
    CHECK(instance.requestedSizes.size() == 1);
    CHECK(editor.getWidth() == Editor::expandedWidth);
}

TEST_CASE("A host that refuses the resize gets the overlay", "[gui][overlay]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    instance.grantResizes = false;
    instance.openEditor(Editor::PanelPlacement::expandContract);
    auto &editor(instance.editor());

    auto const closed(rendered(editor));

    editor.showPresetBrowser(true);
    auto const open(rendered(editor));

    // It asked, was told no, and stayed the size it was...
    CHECK(instance.requestedSizes.size() == 1);
    CHECK_FALSE(editor.panelHasOwnColumn());
    CHECK(editor.getWidth() == Editor::estimatedWidth);

    // ...with the panel over the module strips, which is overlay placement.
    CHECK(differenceOver(closed, open, overlayRectangle()) > leastOfTheRectangleAPanelCovers);
    CHECK(drawnFractionOver(open, overlayRectangle()) > 0);

    editor.showPresetBrowser(false);
    CHECK(differenceOver(closed, rendered(editor), unexpandedEditor()) == 0);
}

TEST_CASE("The always-visible column never empties", "[gui][overlay]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The width is taken in the constructor and not through
    /// requestEditorSize(), so this case is also what says that: the shim builds
    /// the editor inside `guiCreate()` and answers `guiGetSize()` out of it, so
    /// an editor that asked afterwards would open at 563 and jump.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    instance.openEditor(Editor::PanelPlacement::alwaysVisible);
    auto &editor(instance.editor());

    CHECK(editor.panelHasOwnColumn());
    CHECK(editor.getWidth() == Editor::expandedWidth);

    auto const resting(rendered(editor));
    CHECK(drawnFractionOver(resting, panelColumnRectangle()) > 0);

    /// \note Against a second editor *switched* into alwaysVisible rather than
    /// built that way, because those are two different paths to the same picture
    /// -- the constructor's and panelPlacement()'s -- and only the first is the
    /// one a plugin takes.
    SWTest::Instance switched;
    switched.openEditor(Editor::PanelPlacement::overlay);
    switched.editor().panelPlacement(Editor::PanelPlacement::alwaysVisible);
    REQUIRE(switched.editor().getWidth() == Editor::expandedWidth);
    CHECK(differenceOver(resting, rendered(switched.editor()), panelColumnRectangle()) == 0);

    // Going to the settings and shutting them again comes back to the browser
    // rather than to a hole, and the editor never changes width doing it.
    editor.showSettings(Editor::interfacePageIndex);
    CHECK(editor.getWidth() == Editor::expandedWidth);
    CHECK(differenceOver(resting, rendered(editor), panelColumnRectangle()) > 0);

    editor.showPresetBrowser(false);
    CHECK(editor.getWidth() == Editor::expandedWidth);
    CHECK(differenceOver(resting, rendered(editor), panelColumnRectangle()) == 0);

    // Nothing was ever asked of the host: the column was taken at construction.
    CHECK(instance.requestedSizes.empty());
}

TEST_CASE("Changing placement moves the panel that is already up", "[gui][overlay]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(overlayEditor(instance));

    editor.showSettings(Editor::interfacePageIndex);
    auto const asOverlay(rendered(editor));
    REQUIRE(editor.getWidth() == Editor::estimatedWidth);
    REQUIRE(drawnFractionOver(asOverlay, overlayRectangle()) > 0);

    editor.panelPlacement(Editor::PanelPlacement::expandContract);
    auto const inColumn(rendered(editor));
    CHECK(editor.getWidth() == Editor::expandedWidth);
    // The panel went with it: the strips are back and the column has the panel.
    CHECK(differenceOver(asOverlay, inColumn, overlayRectangle()) > 0);
    CHECK(drawnFractionOver(inColumn, panelColumnRectangle()) > 0);

    editor.panelPlacement(Editor::PanelPlacement::overlay);
    CHECK(editor.getWidth() == Editor::estimatedWidth);
    CHECK(differenceOver(asOverlay, rendered(editor), unexpandedEditor()) == 0);
}

////////////////////////////////////////////////////////////////////////////////
//
// The banks
// ---------
//
////////////////////////////////////////////////////////////////////////////////
///
///   One of the eighteen factory banks was ever drawn -- the root listing, which
/// is two rows saying "Factory" and "User" -- and no case had ever opened one.
///
///   The precedent is the effect sweep: `tools/show-ui`'s editor-module page
/// rendered one effect of 57 for a month, and widening it to all 57 immediately
/// found that it had been rendering *no module at all* and that every one of the
/// 57 PNGs was byte-identical. The banks are the same shape of cheap breadth,
/// and the same failure is available to them: a browser that lists nothing draws
/// its chrome, covers its rectangle, and passes anything that only asks whether
/// the panel opened.
///
/// \note So this asks two things, and the second is the one with teeth: that
/// each bank draws, and that the eighteen renders are **eighteen different
/// pictures**. `sw-show-ui` can render a bank by name through
/// SW_SHOW_UI_PRESET_BANK, one process per bank, and could not have compared
/// them.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Every factory bank draws, and draws something of its own", "[gui][overlay][presets]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(overlayEditor(instance));

    auto const closed(rendered(editor));

    /// \note Eighteen, not fifteen. It read `>= 15` while banks() was answering
    /// with exactly the fifteen directories that hold a `.swp` -- so the sweep
    /// passed over a listing three banks short of what the plugin ships, which is
    /// the shape of failure this case was written to catch.
    ///                                       (10.08.2026.) (SW port)
    auto const banks(LE::SW::FactoryPresets::banks());
    REQUIRE(banks.size() >= 18);

    /// \note Hashed rather than kept: eighteen 563 x 376 ARGB images is 15 MB,
    /// and what is being asked is only whether any two agree.
    std::map<std::size_t, std::string> byPicture;
    unsigned int drawn{0};

    for (auto const &bank : banks)
    {
        INFO("bank " << bank);

        editor.showFactoryBank(bank);
        auto const open(rendered(editor));

        // It opened, and it painted the rectangle rather than a tab bar's worth
        // of it -- the same measure the settings pages are held to.
        CHECK(differenceOver(closed, open, overlayRectangle()) > leastOfTheRectangleAPanelCovers);

        std::size_t hash{0};
        {
            juce::Image::BitmapData const pixels(open, juce::Image::BitmapData::readOnly);
            auto const area(overlayRectangle());
            for (int y(area.getY()); y < area.getBottom(); ++y)
                for (int x(area.getX()); x < area.getRight(); ++x)
                    hash = hash * 1099511628211u + pixels.getPixelColour(x, y).getARGB();
        }

        auto const [entry, inserted](byPicture.emplace(hash, bank));
        // Two banks that render identically are two banks whose *contents* never
        // reached the list -- which is what the effect sweep found.
        INFO("identical to bank " << entry->second);
        CHECK(inserted);
        ++drawn;
    }

    CHECK(drawn == banks.size());
    CHECK(byPicture.size() == banks.size());

    /// \note And back to where it started, so the panel is an overlay rather
    /// than something that ate what was under it -- eighteen banks deep.
    editor.showPresetBrowser(false);
    CHECK(differenceOver(closed, rendered(editor), overlayRectangle()) == 0);
}

TEST_CASE("A bank that is not there leaves the browser drawable", "[gui][overlay][presets]")
{
    /// \note `setFactoryBank` takes a string and the browser filters its list by
    /// it, so a name nothing matches produces an empty listing rather than a
    /// failure -- which is reachable from a saved location the banks have since
    /// been renamed under. What it must not do is leave the panel unpaintable.
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(overlayEditor(instance));

    auto const closed(rendered(editor));

    editor.showFactoryBank("No Such Bank");
    auto const open(rendered(editor));

    // The panel is there; what is in the list is the browser's business.
    CHECK(differenceOver(closed, open, overlayRectangle()) > leastOfTheRectangleAPanelCovers);
    CHECK(drawnFractionOver(open, overlayRectangle()) > 0);

    // ...and a real bank after it still lists, so the browser is not stuck.
    auto const banks(LE::SW::FactoryPresets::banks());
    REQUIRE_FALSE(banks.empty());
    editor.showFactoryBank(banks.front());
    CHECK(differenceOver(open, rendered(editor), overlayRectangle()) > 0);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The browser is built and destroyed with the editor, so "where it was"
/// has to survive a window being closed and reopened -- which is the only way a
/// user meets this. It did not: the destructor wrote `currentDirectory_` into a
/// global and nothing recorded which of the three locations was showing, and
/// that is a member initialiser, so every browser opened at the root whatever
/// the user had last been looking at.
///
/// \note Measured off the picture rather than off the member, for the reason at
/// the top of this file: what was wrong is what was on the screen. A bank's
/// listing and the root's two entries are not the same picture.
///                                           (08.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

/// \note Two banks and no assumption about where the browser starts. Where it
/// was last left is process-wide -- it has to be, since the browser does not
/// outlive the window -- so a case that opened one and called the first picture
/// "the root" would be reading whatever the previous case left behind. Asking it
/// to follow *two* different banks in turn says it is remembering rather than
/// defaulting, from any starting point.
TEST_CASE("The preset browser reopens where it was left", "[gui][overlay][presets]")
{
    SWTest::HostSideJuce const juce;

    auto const banks(LE::SW::FactoryPresets::banks());
    REQUIRE(banks.size() >= 2);

    auto const pictureAfterLeavingItAt([](std::string const &bank) {
        juce::Image left;
        {
            SWTest::Instance instance;
            auto &editor(overlayEditor(instance));
            editor.showFactoryBank(bank);
            left = rendered(editor);
            instance.closeEditor();
        }

        SWTest::Instance instance;
        auto &editor(overlayEditor(instance));
        editor.showPresetBrowser(true);
        return std::pair{left, rendered(editor)};
    });

    auto const [leftAtFirst, reopenedAtFirst](pictureAfterLeavingItAt(banks.front()));
    auto const [leftAtSecond, reopenedAtSecond](pictureAfterLeavingItAt(banks.back()));

    // The premise: the two banks are distinguishable at all.
    REQUIRE(differenceOver(leftAtFirst, leftAtSecond, overlayRectangle()) > 0);

    // Each reopened where it was left.
    CHECK(differenceOver(leftAtFirst, reopenedAtFirst, overlayRectangle()) == 0);
    CHECK(differenceOver(leftAtSecond, reopenedAtSecond, overlayRectangle()) == 0);

    // Which is not the same as always opening on one fixed thing.
    CHECK(differenceOver(reopenedAtFirst, reopenedAtSecond, overlayRectangle()) > 0);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note A remembered bank is a name from a previous run and a later build need
/// not still ship it, so restoring one has to survive the name matching nothing.
/// The browser must still open and still be navigable -- the failure this guards
/// is a browser that reopens onto a location it cannot show and stays there.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A remembered bank that is no longer shipped still opens", "[gui][overlay][presets]")
{
    SWTest::HostSideJuce const juce;

    {
        SWTest::Instance instance;
        auto &editor(overlayEditor(instance));
        editor.showPresetBrowser(true);
        editor.showFactoryBank("No Such Bank");
        instance.closeEditor();
    }

    SWTest::Instance instance;
    auto &editor(overlayEditor(instance));
    editor.showPresetBrowser(true);

    // It painted rather than leaving a hole, which is this file's measure.
    CHECK(drawnFractionOver(rendered(editor), overlayRectangle()) > 0);

    // And it is not stuck there: a bank that does exist still lists.
    auto const banks(LE::SW::FactoryPresets::banks());
    REQUIRE_FALSE(banks.empty());
    auto const empty(rendered(editor));
    editor.showFactoryBank(banks.front());
    CHECK(differenceOver(empty, rendered(editor), overlayRectangle()) > 0);
}
