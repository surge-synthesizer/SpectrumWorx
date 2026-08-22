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
/// measures the whole canvas now; a 287 x 545 hole in an 845 x 564 editor is 33 %
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

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
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

/// The one button in \p root reading \p text.
juce::Button &buttonNamed(juce::Component &root, juce::String const &text)
{
    std::vector<juce::Button *> matching;
    for (auto *const pButton : descendantsOfType<juce::Button>(root))
        if (pButton->getButtonText() == text)
            matching.push_back(pButton);
    REQUIRE(matching.size() == 1);
    return *matching.front();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief What pressing \p button does, minus the mouse.
///
/// \note `triggerClick()` wants a message loop and a test binary has none, and a
/// synthesised `mouseDown` never reaches a component in one either. Both of the
/// buttons this is used on toggle, so setting the state *is* the press -- JUCE
/// sends the click message from inside `setToggleState()` -- and that is what
/// puts the editor's own `buttonClicked` under test rather than the function it
/// happens to call.
///
////////////////////////////////////////////////////////////////////////////////

void press(juce::Button &button)
{
    REQUIRE_FALSE(button.getToggleState());
    button.setToggleState(true, juce::sendNotificationSync);
}

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

/// The editor as it was before any of this: 845 x 564, panels over the strips.
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
/// left alone these would be comparing an 845 px render with a 1147 px one.
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

////////////////////////////////////////////////////////////////////////////////
///
/// \brief What fraction of two equally sized rectangles two renders disagree
/// about.
///
/// \note A rectangle each, because the skin is not in the same place in both. An
/// editor that has grown a column draws the skin mainAreaX to the right of where
/// an unexpanded one draws it -- the panel is on the left -- so "is the rack the
/// same" is a question about one rectangle *of the skin* at two positions on
/// screen. \see withColumn() below.
///
/// \note The two images need not be the same size, only each big enough to hold
/// its own rectangle: the placement cases compare a grown editor against the
/// editor it grew from, which is the whole question those cases ask.
///
////////////////////////////////////////////////////////////////////////////////

/// \brief How far apart two pixels are, on their worst channel.
int channelDistance(juce::Colour const left, juce::Colour const right)
{
    auto const apart([](juce::uint8 const a, juce::uint8 const b) {
        return std::abs(static_cast<int>(a) - static_cast<int>(b));
    });
    return std::max({apart(left.getRed(), right.getRed()), apart(left.getGreen(), right.getGreen()),
                     apart(left.getBlue(), right.getBlue()),
                     apart(left.getAlpha(), right.getAlpha())});
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief What the same drawing at two offsets is allowed to differ by and still
/// be the same drawing.
///
///   One part in 255, which is a rasteriser rounding an antialiased edge and
/// nothing else. Windows failed both placement cases at 0.05 % of the module
/// rack -- some sixty pixels of a hundred and twenty thousand -- where macOS and
/// Linux were bit-identical, and a whole-pixel translation of one picture cannot
/// change it by more than rounding.
///
/// \note A tolerance on the *pixel*, not on the fraction. `< 0.001` of the area
/// would pass just as happily with sixty pixels of the wrong thing in them,
/// which is the failure these cases exist to catch: a panel bleeding into the
/// rack shows up as a handful of pixels that are completely wrong, not as a
/// handful that are one off. Anything worse than rounding still fails, and
/// differenceOver() says how much worse.
///
////////////////////////////////////////////////////////////////////////////////

int constexpr rasteriserRounding{1};

double differenceOver(juce::Image const &left, juce::Rectangle<int> const &leftArea,
                      juce::Image const &right, juce::Rectangle<int> const &rightArea)
{
    REQUIRE(leftArea.getWidth() == rightArea.getWidth());
    REQUIRE(leftArea.getHeight() == rightArea.getHeight());
    REQUIRE(left.getBounds().contains(leftArea));
    REQUIRE(right.getBounds().contains(rightArea));

    juce::Image::BitmapData const leftPixels(left, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData const rightPixels(right, juce::Image::BitmapData::readOnly);

    auto const offset(rightArea.getPosition() - leftArea.getPosition());

    std::size_t different{0};
    int worst{0};
    for (int y(leftArea.getY()); y < leftArea.getBottom(); ++y)
        for (int x(leftArea.getX()); x < leftArea.getRight(); ++x)
        {
            auto const apart(
                channelDistance(leftPixels.getPixelColour(x, y),
                                rightPixels.getPixelColour(x + offset.x, y + offset.y)));
            worst = std::max(worst, apart);
            different += (apart > rasteriserRounding);
        }

    //   So that a failure says how far off it was rather than only how much of
    // it was off: one channel apart is a rasteriser, forty is a bug.
    if (worst > 0)
        UNSCOPED_INFO("worst channel difference: " << worst << " of 255");

    return double(different) / double(leftArea.getWidth() * leftArea.getHeight());
}

/// The same rectangle in both, which is every case where neither editor moved.
double differenceOver(juce::Image const &left, juce::Image const &right,
                      juce::Rectangle<int> const &area)
{
    return differenceOver(left, area, right, area);
}

/// \brief Where an editor with a panel column draws \p skinArea: the panel takes
/// the left edge and the skin moves right by mainAreaX to make room.
/// \see SpectrumWorxEditor::MainArea.
juce::Rectangle<int> withColumn(juce::Rectangle<int> const &skinArea)
{
    return skinArea.translated(Editor::mainAreaX, 0);
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
            if (channelDistance(leftPixels.getPixelColour(x, y),
                                rightPixels.getPixelColour(x, y)) <= rasteriserRounding)
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

////////////////////////////////////////////////////////////////////////////////
///
/// \note Issue #129, and the report is exactly the route this takes: select a
/// tab, press PRESETS, press SETTINGS. The panel does not survive that -- the
/// browser deletes it -- so the selection has to live outside it, which is what
/// `Settings::lastPage()` is.
///
/// \note Two tabs in turn rather than one, for the reason the preset browser's
/// case gives about banks: `lastPage()` is process-wide, so a case that only
/// left it on the GUI page would pass just as happily against a panel that
/// always opened on the GUI page. Following it in both directions says it is
/// remembering rather than defaulting.
///
/// \note Through the buttons rather than through `showSettings()`, because the
/// bug was in the handler and not in the panel: `buttonClicked` asked for page 0
/// by name. Calling `showSettings()` here would have gone green with that line
/// still in place.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The settings panel reopens on the tab it was left on", "[gui][overlay]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(overlayEditor(instance));

    auto &settingsButton(buttonNamed(editor, "SETTINGS"));

    auto const pictureAfterLeavingItOn([&](unsigned int const page) {
        editor.showSettings(page);
        auto const left(rendered(editor));

        /// \note The browser through `showPresetBrowser()` and the panel back
        /// through the *button*, which is the asymmetry the bug asks for: the
        /// handler that was wrong is the settings one, and `togglePresetBrowser`
        /// asserts on `getPeer()` -- a headless editor has no window. All the
        /// PRESETS press contributes here is making the panel go away, which is
        /// what this call does.
        editor.showPresetBrowser(true); // "you tab away back to Presets"...
        press(settingsButton);          // ...and come back the way a user does.

        return std::pair{left, rendered(editor)};
    });

    auto const [leftOnEngine, reopenedOnEngine](pictureAfterLeavingItOn(Editor::enginePageIndex));
    auto const [leftOnInterface,
                reopenedOnInterface](pictureAfterLeavingItOn(Editor::interfacePageIndex));

    // The premise: the two pages are distinguishable at all.
    REQUIRE(differenceOver(leftOnEngine, leftOnInterface, overlayRectangle()) > 0);

    // Each reopened on the tab it was left on.
    CHECK(differenceOver(leftOnEngine, reopenedOnEngine, overlayRectangle()) == 0);
    CHECK(differenceOver(leftOnInterface, reopenedOnInterface, overlayRectangle()) == 0);

    // Which is not the same as always opening on one fixed page.
    CHECK(differenceOver(reopenedOnEngine, reopenedOnInterface, overlayRectangle()) > 0);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The other half of issue #129, and the reason the answer is the host's
/// rather than the editor's: a window that shuts takes the whole editor with it,
/// so a member on either the panel or the editor would forget. What holds it is
/// the same object the DAW extra state is written out of, which is what makes
/// the tab come back in a reopened project as well as in a reopened window.
/// \see SpectrumWorxCLAP::sessionState() and tests/clap/stateTests.cpp.
///
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
/// \note Issue #142. The four lines under the Engine page's combo boxes -- the
/// ripple amount, the frequency and time resolutions and the latency -- kept
/// describing the FFT size the user had just moved away from.
///
///   Two things were wrong and this covers both. The numbers come out of
/// `Engine::Setup`, which is rebuilt on whichever thread owns the engine some
/// time *after* a spectral parameter is queued, so reading it at the moment the
/// combo box changes reads the old one; and nothing marked the page dirty when
/// the rebuild did happen, so even a correct read was never drawn.
///
/// \note The second half is what needs the boolean rather than a picture.
/// `paintEntireComponent()` repaints whatever it is handed whether or not
/// anything asked, so the old code -- which recomputed the three lines inside
/// paint() -- draws the right numbers in a render and the wrong ones on screen.
/// A picture cannot tell those apart. `updateEngineInformationIfChanged()`
/// answering true is the editor saying it asked. \see rackResyncRequests(),
/// which exists for the same reason.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The engine information follows the engine, and says when it moved",
          "[gui][overlay][settings]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    auto &editor(overlayEditor(instance));

    editor.showSettings(Editor::enginePageIndex);
    auto const before(rendered(editor));

    // Nothing has moved, so nothing is asked to redraw.
    CHECK_FALSE(editor.updateEngineInformationIfChanged());

    ////////////////////////////////////////////////////////////////////////////
    /// \note Straight into the engine, in the audio thread's role, because that
    /// is where a queued spectral parameter is actually applied -- the editor's
    /// own `globalParameterChanged<>` only queues it, and nothing drains this
    /// harness's queue. What the case is about is the editor noticing that the
    /// engine moved, so the engine has to be what moves.
    ////////////////////////////////////////////////////////////////////////////
    using LE::SW::GlobalParameters::FFTSize;
    auto const wasSize(instance.engine().parameters().get<FFTSize>());
    REQUIRE(instance.engine().set<FFTSize>(wasSize / 2));
    REQUIRE(instance.engine().parameters().get<FFTSize>() != wasSize);

    // It moved, and the editor said so...
    CHECK(editor.updateEngineInformationIfChanged());
    // ...once. A thirty-hertz poll may not repaint thirty times a second.
    CHECK_FALSE(editor.updateEngineInformationIfChanged());

    // And the page draws the new numbers.
    CHECK(differenceOver(before, rendered(editor), overlayRectangle()) > 0);
}

TEST_CASE("The settings tab survives the editor window closing", "[gui][overlay]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;

    juce::Image enginePage, interfacePage;
    {
        auto &editor(overlayEditor(instance));
        editor.showSettings(Editor::enginePageIndex);
        enginePage = rendered(editor);
        editor.showSettings(Editor::interfacePageIndex);
        interfacePage = rendered(editor);
        instance.closeEditor();
    }

    // The premise: the two pages are distinguishable at all.
    REQUIRE(differenceOver(enginePage, interfacePage, overlayRectangle()) > 0);

    auto &reopened(overlayEditor(instance));
    press(buttonNamed(reopened, "SETTINGS"));

    CHECK(differenceOver(interfacePage, rendered(reopened), overlayRectangle()) == 0);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The other half of issue #129: not only *which tab* but *which panel*.
/// An always-visible column has to hold something, and what it holds when it is
/// filled from nothing should be where the user was rather than a fixed answer.
///
/// \note Which is not what pressing the lit button does, and the case below this
/// one says so: in `alwaysVisible` the two buttons are a two-way selector, so
/// pressing the lit SETTINGS lands on the browser. A resting state that read the
/// remembered panel would land back on settings and make the button a no-op.
/// \see openRememberedPanel() against openRestingPanel().
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("An always-visible column opens on the panel it was left on", "[gui][overlay]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;

    auto const columnAfterLeavingItOn([&](bool const settings) {
        instance.openEditor(Editor::PanelPlacement::alwaysVisible);
        if (settings)
            instance.editor().showSettings(Editor::interfacePageIndex);
        else
            instance.editor().showPresetBrowser(true);
        auto const left(rendered(instance.editor()));
        instance.closeEditor();

        // A window opened again, which is what a host does with a restored plugin.
        instance.openEditor(Editor::PanelPlacement::alwaysVisible);
        return std::pair{left, rendered(instance.editor())};
    });

    auto const [leftOnPresets, reopenedOnPresets](columnAfterLeavingItOn(false));
    auto const [leftOnSettings, reopenedOnSettings](columnAfterLeavingItOn(true));

    // The premise: the two panels are distinguishable at all.
    REQUIRE(differenceOver(leftOnPresets, leftOnSettings, panelColumnRectangle()) > 0);

    CHECK(differenceOver(leftOnPresets, reopenedOnPresets, panelColumnRectangle()) == 0);
    CHECK(differenceOver(leftOnSettings, reopenedOnSettings, panelColumnRectangle()) == 0);

    // Which is not the same as always opening on one fixed panel.
    CHECK(differenceOver(reopenedOnPresets, reopenedOnSettings, panelColumnRectangle()) > 0);
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

    /// \note The middle of the logo's hit area, read off `MainArea::logoArea`
    /// rather than written out here -- the skin is redrawn as ordinary work and
    /// a coordinate copied into this file would send the click into empty space
    /// the first time the logo moved, which passes for the wrong reason. The
    /// main area is what the click goes to, because the logo is a position in
    /// the skin and the skin is that component rather than the editor.
    auto const logo(Editor::MainArea::logoArea().getCentre().toFloat());
    auto &skin(static_cast<juce::Component &>(editor.mainArea()));
    skin.mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), logo,
                                    juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                                    1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &skin, &skin, juce::Time(), logo,
                                    juce::Time(), 1, false));

    auto const clicked(rendered(editor));

    // It opened something...
    CHECK(differenceOver(closed, clicked, overlayRectangle()) > leastOfTheRectangleAPanelCovers);
    // ...and it is the About page rather than a panel with no page in it.
    CHECK(differenceOver(aboutPage, clicked, overlayRectangle()) == 0);
}

TEST_CASE("The two panels are mutually exclusive and land in the same place", "[gui][overlay]")
{
    // `showPanel()` asserts the invariant; this is what it looks like on screen.
    // There is one 287 x 545 rectangle whatever the placement -- so "open the
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
    /// \note Swapping the panels also changes something *outside* the rectangle
    /// -- the two buttons that opened them, since `showSettings()` un-toggles
    /// the presets button and the other way about -- which is what makes the
    /// exclusion visible to a user rather than only true.
    ///
    /// \note Said as "something outside the rectangle moved" and no longer as
    /// "and all of it is left of `overlayX`". Where those two buttons sit is a
    /// layout decision and the layout is being worked on; the claim this case
    /// is for is the exclusion, and their position is not part of it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const moved(differenceBoundsOutside(settingsAlone, browser, overlayRectangle()));
    CAPTURE(moved.toString().toStdString());
    CHECK(!moved.isEmpty());
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
    CHECK(differenceOver(opened, withColumn(moduleRack()), rendered(overlayEditor(bare)),
                         moduleRack()) == 0);

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
    CHECK(differenceOver(closed, overlayRectangle(), open, withColumn(overlayRectangle())) == 0);
    // ...and the rack the user was working on is untouched, which is the point.
    CHECK(differenceOver(closed, moduleRack(), open, withColumn(moduleRack())) == 0);

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

////////////////////////////////////////////////////////////////////////////////
///
/// \note **Nothing here pins "a host that refuses the resize gets the overlay",
/// deliberately.** That is the right answer for an editor with one fixed size and
/// the wrong one for an editor the user can zoom: a host that refuses
/// `expandedWidth` has not refused the column, it has refused *that many window
/// units*, and what a zoomable editor owes it is a smaller zoom rather than a
/// different layout. Pinning the current behaviour would make that change look
/// like a regression.
///
/// \note What is *not* being given up is that the editor asks and reads the
/// answer: "A panel gets a column of its own" above still counts the requests
/// and checks their contents, and pluginTests.cpp still holds the shim to
/// asking in window units through `ZoomedEditor::scaled()`. Only the refusal
/// path's consequence is unstated, and it is unstated because it is about to
/// change.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The always-visible column never empties", "[gui][overlay]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The width is taken in the constructor and not through
    /// requestEditorSize(), so this case is also what says that: the shim builds
    /// the editor inside `guiCreate()` and answers `guiGetSize()` out of it, so
    /// an editor that asked afterwards would open at 845 and jump.
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
    auto const banks(LE::SW::FactoryPresets::banks());

    /// \note Hashed rather than kept: eighteen 845 x 564 ARGB images is 34 MB,
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

    editor.showFactoryBank("No Bank");
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
///
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
/// \note Two banks and no assumption about where the browser starts: a case that
/// opened one and called the first picture "the root" would pass against a
/// browser that always opened at the root. Asking it to follow *two* different
/// banks in turn says it is remembering rather than defaulting, from any
/// starting point.
///
/// \note One instance, its window shut and opened again, which is the lifetime
/// the report is about -- the browser does not outlive the window. Where it was
/// left used to be a process-wide static and this case used two instances to say
/// so; it is the session's as of 21.08.2026, so a second instance is a second
/// answer and would say nothing. \see PanelState and issue #129.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The preset browser reopens where it was left", "[gui][overlay][presets]")
{
    SWTest::HostSideJuce const juce;

    auto const banks(LE::SW::FactoryPresets::banks());
    REQUIRE(banks.size() >= 2);

    SWTest::Instance instance;

    auto const pictureAfterLeavingItAt([&](std::string const &bank) {
        auto &editor(overlayEditor(instance));
        editor.showFactoryBank(bank);
        auto const left(rendered(editor));
        instance.closeEditor();

        auto &reopened(overlayEditor(instance));
        reopened.showPresetBrowser(true);
        return std::pair{left, rendered(reopened)};
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
        editor.showFactoryBank("No Bank");
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
