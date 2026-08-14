////////////////////////////////////////////////////////////////////////////////
///
/// moduleDragTests.cpp
/// -------------------
///
///   Dragging a module strip somewhere else in the rack, which is two gestures
/// wearing one drag: dropped **on** another strip the two change places, dropped
/// **between** two -- or past either end -- it is inserted there and the rest
/// shift along.
///
///   Both halves are asked here, and separately, because they fail separately.
/// `moduleDropAt()` decides which of the two a point means and is pure geometry;
/// `applyModuleDrop()` carries one out and is a permutation of the chain. A swap
/// that reads as an insert and an insert that permutes wrongly are different
/// bugs, and only the second is visible in the audio.
///
/// \note Not driven by a mouse. A JUCE drag needs a window, a
/// `MouseInputSource` that a synthesised event does not touch, and a drag image
/// on the desktop -- see the note on `eventOver` in moduleControlFocusTests.cpp
/// for what a hand-built event does and does not reach. What a drag *decides* and
/// what it *does* are both reachable without one, and they are the parts that can
/// be wrong.
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

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
using namespace LE;
using namespace LE::SW;

using Editor = GUI::SpectrumWorxEditor;
using Drop = Editor::ModuleDrop;
using Strip = GUI::ModuleUI;

constexpr int slotWidth{Strip::width + Strip::distance};

////////////////////////////////////////////////////////////////////////////////
///
///   Every point below is where the **dragged strip's middle** is, which is what
/// a drop is aimed with -- not where the pointer is. \see
/// SpectrumWorxEditor::draggedStripCentre(), and the last case in this file for
/// the difference.
///
////////////////////////////////////////////////////////////////////////////////

/// The middle of the strip drawn in \p slot, which is what a swap is aimed at.
juce::Point<int> overSlot(std::uint8_t const slot)
{
    return {Strip::horizontalOffset + (slot * slotWidth) + (slotWidth / 2),
            Strip::verticalOffset + (Strip::height / 2)};
}

/// The gap before \p slot, which is what an insert is aimed at.
juce::Point<int> overGap(std::uint8_t const gap)
{
    return {Strip::horizontalOffset + (gap * slotWidth),
            Strip::verticalOffset + (Strip::height / 2)};
}

/// \brief What a drag reports when the strip it is carrying has been brought to
/// \p stripTopLeft, having been picked up \p grabbedAt within it.
///
/// \note Written from where the strip is rather than from where the pointer is,
/// because that is what the user is looking at: JUCE carries a snapshot of the
/// strip offset by the grab, so this is the position of the thing on screen.
Editor::ModuleDrop dropWithStripAt(Editor const &editor, std::uint8_t const sourceSlot,
                                   juce::Point<int> const stripTopLeft,
                                   juce::Point<int> const grabbedAt)
{
    return editor.moduleDropAt(sourceSlot,
                               Editor::draggedStripCentre(stripTopLeft + grabbedAt, grabbedAt));
}

/// Where the strip in \p slot sits when it is not being dragged anywhere.
juce::Point<int> slotTopLeft(std::uint8_t const slot)
{
    return {Strip::horizontalOffset + (slot * slotWidth), Strip::verticalOffset};
}

/// A mouse move to \p point within \p component, hand-built. \see
/// moduleControlFocusTests.cpp for what a synthesised event does and does not do.
juce::MouseEvent moveTo(juce::Component &component, juce::Point<int> const point)
{
    auto const at(point.toFloat());
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                            juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component,
                            &component, juce::Time(), at, juce::Time(), 1, false);
}

/// \brief The cursor JUCE would show at \p point in \p strip: the component the
/// point lands on, resolved through the look and feel.
juce::MouseCursor cursorOver(Strip &strip, juce::Point<int> const point)
{
    auto *const pTarget(strip.getComponentAt(point));
    /// \note No assertion, so that the sweep below can call this 24 344 times and
    /// still report one number. `getComponentAt()` answers with the component
    /// itself where nothing else is, so a point inside always lands on something,
    /// and `NoCursor` matches neither answer the sweep allows.
    if (pTarget == nullptr)
        return juce::MouseCursor(juce::MouseCursor::NoCursor);

    /// \note The move first, and only where JUCE would deliver one: the strip
    /// carries two cursors and picks between them in `mouseMove`, so asking
    /// without having moved there reads whatever the last move left behind.
    if (pTarget == &strip)
        static_cast<juce::Component &>(strip).mouseMove(moveTo(strip, point));

    return pTarget->getLookAndFeel().getMouseCursorFor(*pTarget);
}

/// Whether a drag could start at \p point: the strip's own to handle, and one of
/// the two bands it offers as a handle. \see ModuleUI::mouseDrag().
bool draggableAt(Strip &strip, juce::Point<int> const point)
{
    return (strip.getComponentAt(point) == &strip) && strip.isDragHandle(point);
}

/// \brief An editor with \p effects, one strip each, in the order given.
///
/// \note Different effects on purpose: the whole question below is which module
/// ends up where, and a rack of three identical ones cannot answer it.
///
/// \note Overlay placement, so that the skin's coordinates are the editor's --
/// every point above is a position in the skin, and an editor with a panel
/// column draws the skin 201 px to the right of its own origin.
Editor &rackOf(SWTest::Instance &instance, std::vector<std::uint8_t> const &effects)
{
    instance.openEditor(Editor::PanelPlacement::overlay);
    auto &editor(instance.editor());
    for (auto const effect : effects)
        editor.addUserAddedModule(effect);
    /// \note By hand: adding a module ends in a posted resync and a test has no
    /// message loop to deliver it. \see twoInstanceTests.cpp.
    editor.resyncModuleRack();
    return editor;
}

/// What the chain is playing, as effect indices, which is the answer that matters.
std::vector<int> chainOrder(SWTest::Instance const &instance)
{
    auto &chain(const_cast<SWTest::Instance &>(instance).core().program().moduleChain());
    std::vector<int> order;
    for (std::uint8_t slot(0); slot < chain.size(); ++slot)
        order.push_back(chain.moduleAs<SW::Module>(slot)->effectTypeIndex());
    return order;
}

/// The same question of the rack, which is what the user is looking at.
std::vector<int> rackOrder(Editor &editor, std::size_t const strips)
{
    std::vector<int> order;
    for (std::uint8_t slot(0); slot < strips; ++slot)
        order.push_back(editor.regionInRackSlot(slot)->module().effectTypeIndex());
    return order;
}

std::string describe(std::vector<int> const &order)
{
    std::string text;
    for (auto const effect : order)
        text += std::to_string(effect) + " ";
    return text;
}

/// The editor as an image, which is what a user sees. \see overlayPanelTests.cpp.
juce::Image rendered(Editor &editor)
{
    juce::Image image(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
    juce::Graphics graphics(image);
    editor.paintEntireComponent(graphics, true);
    return image;
}

/// What fraction of \p area two renders disagree about.
double differenceOver(juce::Image const &left, juce::Image const &right,
                      juce::Rectangle<int> const &area)
{
    juce::Image::BitmapData const leftPixels(left, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData const rightPixels(right, juce::Image::BitmapData::readOnly);

    std::size_t different{0};
    for (int y(area.getY()); y < area.getBottom(); ++y)
        for (int x(area.getX()); x < area.getRight(); ++x)
            different += (leftPixels.getPixelColour(x, y) != rightPixels.getPixelColour(x, y));

    return double(different) / double(area.getWidth() * area.getHeight());
}

/// Where the strip in \p slot is drawn.
juce::Rectangle<int> slotRectangle(std::uint8_t const slot)
{
    return {Strip::horizontalOffset + (slot * slotWidth), Strip::verticalOffset, Strip::width,
            Strip::height};
}
//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
//
// Which of the two a point means
// ------------------------------
//
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The middle of a strip is a swap and its edges are inserts", "[gui][modules][drag]")
{
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1, 2, 3}));

    // Dragging the first strip. Over the middle of another one: change places.
    for (std::uint8_t target(1); target < 4; ++target)
    {
        CAPTURE(target);
        auto const drop(editor.moduleDropAt(0, overSlot(target)));
        CHECK(drop.action == Drop::swap);
        CHECK(int(drop.slot) == target);
    }

    // On a boundary between two: go between them. A gap is named by the slot to
    // its right, so the gap before slot 2 is gap 2.
    for (std::uint8_t gap(2); gap < 4; ++gap)
    {
        CAPTURE(gap);
        auto const drop(editor.moduleDropAt(0, overGap(gap)));
        CHECK(drop.action == Drop::insert);
        CHECK(int(drop.slot) == gap);
    }

    /// \note And a gap has width, which is the point of it. A gap that had to be
    /// hit exactly is one no user reaches, so the outer quarter of each strip
    /// belongs to the gap beside it -- these two points are 16 px either side of
    /// the boundary and both mean the same insert.
    CHECK(editor.moduleDropAt(0, overGap(2).translated(-16, 0)).action == Drop::insert);
    CHECK(int(editor.moduleDropAt(0, overGap(2).translated(-16, 0)).slot) == 2);
    CHECK(editor.moduleDropAt(0, overGap(2).translated(+16, 0)).action == Drop::insert);
    CHECK(int(editor.moduleDropAt(0, overGap(2).translated(+16, 0)).slot) == 2);
}

TEST_CASE("Either end of the rack is reachable without being on it", "[gui][modules][drag]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The two drops with nothing to aim at. "Before the first strip" and
    /// "after the last" are gaps at the edge of the rack, and half of each is
    /// outside it -- so a zone that stopped at the rack would make the two ends
    /// the hardest places in it to reach, which is the wrong way round: moving
    /// something to the front or the back of a chain is the commonest thing
    /// anyone does to one.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1, 2}));

    // Dragging the last strip to the front, from off the left end of the rack.
    auto const beforeTheRack(editor.moduleDropAt(2, overGap(0).translated(-24, 0)));
    CHECK(beforeTheRack.action == Drop::insert);
    CHECK(int(beforeTheRack.slot) == 0);

    // ...and the first strip to the back, from off the right end of it.
    auto const afterTheRack(editor.moduleDropAt(0, overGap(3).translated(+24, 0)));
    CHECK(afterTheRack.action == Drop::insert);
    CHECK(int(afterTheRack.slot) == 3);

    // Further out than that is a drag let go somewhere else, and does nothing.
    CHECK(editor.moduleDropAt(0, {0, Strip::verticalOffset}).action == Drop::nothing);
    CHECK(editor.moduleDropAt(0, overGap(3).translated(+120, 0)).action == Drop::nothing);
    // The empty slots past the last strip are not part of the rack either.
    CHECK(editor.moduleDropAt(0, overSlot(4)).action == Drop::nothing);
}

TEST_CASE("A drop that would move nothing is not offered", "[gui][modules][drag]")
{
    /// \note Three points mean "leave it where it is": the strip itself, and the
    /// gap either side of it -- it is already in both. Offering any of them would
    /// light the indicator for a move the user then does not see happen.
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1, 2}));

    CHECK(editor.moduleDropAt(1, overSlot(1)).action == Drop::nothing);
    CHECK(editor.moduleDropAt(1, overGap(1)).action == Drop::nothing);
    CHECK(editor.moduleDropAt(1, overGap(2)).action == Drop::nothing);

    // Its neighbours are still swaps, which is what says the three above are
    // about the source strip rather than about the middle of the rack.
    CHECK(editor.moduleDropAt(1, overSlot(0)).action == Drop::swap);
    CHECK(editor.moduleDropAt(1, overSlot(2)).action == Drop::swap);

    /// \note And a rack of one has nowhere to go at all -- worth saying, because
    /// it is the state every editor starts in after the first module is added.
    SWTest::Instance alone;
    auto &lonely(rackOf(alone, {0}));
    CHECK(lonely.moduleDropAt(0, overSlot(0)).action == Drop::nothing);
    CHECK(lonely.moduleDropAt(0, overGap(1)).action == Drop::nothing);
}

TEST_CASE("Where a strip was picked up does not move the drop", "[gui][modules][drag]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The reported fault, and the reason the point above is the strip's
    /// middle rather than the pointer's. A drop decided from the pointer is out
    /// by however far from the middle the strip was picked up -- up to half a
    /// strip, which is the exact width of a swap zone. Carrying a strip until it
    /// sits over another one and being told "insert to the left of it" is what
    /// that looks like, and which way it was wrong depended on which edge had
    /// been grabbed.
    ///
    ///   Swept across the strip rather than sampled at three points: the fault
    /// grows with the distance from the middle, so a case that only tried the
    /// middle would have passed against the code that had it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1, 2, 3}));

    for (int grabX(0); grabX < Strip::width; ++grabX)
    {
        CAPTURE(grabX);
        juce::Point<int> const grabbedAt(grabX, Strip::height / 2);

        // The strip carried until it covers slot 2: change places with it.
        auto const onto(dropWithStripAt(editor, 0, slotTopLeft(2), grabbedAt));
        CHECK(onto.action == Drop::swap);
        CHECK(int(onto.slot) == 2);

        /// \note And straddling the boundary between slots 2 and 3, which is what
        /// "between these two" looks like when the thing being aimed is a strip:
        /// half of it over each.
        auto const between(
            dropWithStripAt(editor, 0, slotTopLeft(2).translated(slotWidth / 2, 0), grabbedAt));
        CHECK(between.action == Drop::insert);
        CHECK(int(between.slot) == 3);
    }

    /// \note And the strip left where it started is "nothing", whichever edge it
    /// was picked up by -- which is what a click on a strip is.
    for (int grabX(0); grabX < Strip::width; ++grabX)
    {
        CAPTURE(grabX);
        CHECK(dropWithStripAt(editor, 1, slotTopLeft(1), {grabX, Strip::height / 2}).action ==
              Drop::nothing);
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// What the strip says can be picked up
// ------------------------------------
//
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The cursor is a hand exactly where a strip can be picked up", "[gui][modules][drag]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Every point in the strip, because the claim is an "exactly": the
    /// cursor has to be a hand where a drag would start and an arrow everywhere
    /// else, and the second half is the one that used to be wrong. Counted rather
    /// than asserted per point -- 68 x 358 is 24 344 of them and one number is
    /// easier to read than 24 344 green ticks.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1}));

    auto *const pStrip(editor.regionInSlot(0));
    REQUIRE(pStrip != nullptr);
    auto &strip(*pStrip);

    std::size_t disagreed{0}, handles{0}, deadSpace{0}, controls{0};
    for (int y(0); y < Strip::height; ++y)
        for (int x(0); x < Strip::width; ++x)
        {
            juce::Point<int> const point(x, y);
            bool const canDrag(draggableAt(strip, point));

            if (strip.getComponentAt(point) != &strip)
                ++controls;
            else
                ++(canDrag ? handles : deadSpace);

            disagreed +=
                (cursorOver(strip, point) != (canDrag ? juce::MouseCursor::DraggingHandCursor
                                                      : juce::MouseCursor::NormalCursor));
        }

    CHECK(disagreed == 0);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note All three kinds of point exist, which is what stops the sweep passing
    /// on a strip that is all one thing. `deadSpace` is the new one and the reason
    /// this case was rewritten: the strip's own uncovered area used to *be* the
    /// draggable region, so a slip of a few pixels off a knob moved the module
    /// instead of the value. Those pixels are the strip's and are not a handle.
    ///
    ////////////////////////////////////////////////////////////////////////////
    CHECK(handles > 0);
    CHECK(deadSpace > 0);
    CHECK(controls > 0);

    ////////////////////////////////////////////////////////////////////////////
    // The two bands, named. The sweep says the cursor agrees with the rule; these
    // say the rule is the one that was asked for.
    ////////////////////////////////////////////////////////////////////////////

    // The top corner, beside the eject `X` rather than on it.
    CHECK(draggableAt(strip, {1, 1}));
    CHECK(cursorOver(strip, {1, 1}) == juce::MouseCursor::DraggingHandCursor);

    // The effect's name, under the blue rule.
    juce::Point<int> const name(Strip::width / 2, Strip::height - 2);
    CHECK(draggableAt(strip, name));
    CHECK(cursorOver(strip, name) == juce::MouseCursor::DraggingHandCursor);

    /// \note And the rule is exactly where the strip draws it: one pixel above is
    /// the effect's, one pixel below is the handle's.
    CHECK(strip.isDragHandle({1, Strip::nameRule}));
    CHECK_FALSE(strip.isDragHandle({1, Strip::nameRule - 1}));

    /// \note The top band follows the eject button's artwork rather than a
    /// constant, so it is held to a shape rather than to a number: tall enough to
    /// hit, and nowhere near the effect's controls.
    CHECK(strip.isDragHandle({1, 10}));
    CHECK_FALSE(strip.isDragHandle({1, Strip::height / 4}));

    // A control, which is not a place a drag starts...
    auto const knob(strip.effectSpecificParameterControl(0).widget().getBounds().getCentre());
    CHECK_FALSE(draggableAt(strip, knob));
    CHECK(cursorOver(strip, knob) == juce::MouseCursor::NormalCursor);

    // ...and neither is the strip beside one, which is what changed.
    juce::Point<int> const beside(0, Strip::nameRule - 1);
    REQUIRE(strip.getComponentAt(beside) == &strip);
    CHECK_FALSE(draggableAt(strip, beside));
    CHECK(cursorOver(strip, beside) == juce::MouseCursor::NormalCursor);
}

////////////////////////////////////////////////////////////////////////////////
//
// What the drop does
// ------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   The rack and the chain, both, and they are not the same check. The rack is
/// told first so that the strips move where the user let go of them, and the
/// engine is told second; a permutation applied to one and not the other is a
/// window that looks right and plays the old order.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Dropping a strip on another changes the two places", "[gui][modules][drag]")
{
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1, 2, 3, 4}));

    REQUIRE(chainOrder(instance) == std::vector<int>{0, 1, 2, 3, 4});

    // Not neighbours, which is the case a single move cannot express.
    editor.applyModuleDrop(1, {Drop::swap, 3});
    editor.resyncModuleRack();

    INFO("chain " << describe(chainOrder(instance)));
    CHECK(chainOrder(instance) == std::vector<int>{0, 3, 2, 1, 4});
    CHECK(rackOrder(editor, 5) == std::vector<int>{0, 3, 2, 1, 4});

    // Neighbours, where the swap is a move and the second one would be a no-op.
    editor.applyModuleDrop(0, {Drop::swap, 1});
    editor.resyncModuleRack();

    INFO("chain " << describe(chainOrder(instance)));
    CHECK(chainOrder(instance) == std::vector<int>{3, 0, 2, 1, 4});
    CHECK(rackOrder(editor, 5) == std::vector<int>{3, 0, 2, 1, 4});

    // ...and the ends, which is the widest swap the rack has.
    editor.applyModuleDrop(4, {Drop::swap, 0});
    editor.resyncModuleRack();

    INFO("chain " << describe(chainOrder(instance)));
    CHECK(chainOrder(instance) == std::vector<int>{4, 0, 2, 1, 3});
    CHECK(rackOrder(editor, 5) == std::vector<int>{4, 0, 2, 1, 3});
}

TEST_CASE("Dropping a strip between two shifts the rest along", "[gui][modules][drag]")
{
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1, 2, 3, 4}));

    REQUIRE(chainOrder(instance) == std::vector<int>{0, 1, 2, 3, 4});

    // The first to the back: everything else moves down one.
    editor.applyModuleDrop(0, {Drop::insert, 5});
    editor.resyncModuleRack();

    INFO("chain " << describe(chainOrder(instance)));
    CHECK(chainOrder(instance) == std::vector<int>{1, 2, 3, 4, 0});
    CHECK(rackOrder(editor, 5) == std::vector<int>{1, 2, 3, 4, 0});

    // The last to the front: everything else moves up one.
    editor.applyModuleDrop(4, {Drop::insert, 0});
    editor.resyncModuleRack();

    INFO("chain " << describe(chainOrder(instance)));
    CHECK(chainOrder(instance) == std::vector<int>{0, 1, 2, 3, 4});
    CHECK(rackOrder(editor, 5) == std::vector<int>{0, 1, 2, 3, 4});

    /// \note A gap beyond the source names a slot one lower once the strip has
    /// left it, and this is the case that tells a right answer from an off-by-one:
    /// slot 1 into gap 3 lands between what were slots 2 and 3.
    editor.applyModuleDrop(1, {Drop::insert, 3});
    editor.resyncModuleRack();

    INFO("chain " << describe(chainOrder(instance)));
    CHECK(chainOrder(instance) == std::vector<int>{0, 2, 1, 3, 4});
    CHECK(rackOrder(editor, 5) == std::vector<int>{0, 2, 1, 3, 4});
}

TEST_CASE("A drop of nothing leaves the rack alone", "[gui][modules][drag]")
{
    /// \note Which is also what a plain click on a strip is: `mouseUp` reaches
    /// moduleDragEnd() whether or not a drag ever started, and a click is let go
    /// inside the strip it started in.
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1, 2}));

    editor.applyModuleDrop(1, {});
    editor.resyncModuleRack();

    CHECK(chainOrder(instance) == std::vector<int>{0, 1, 2});
    CHECK(rackOrder(editor, 3) == std::vector<int>{0, 1, 2});
}

////////////////////////////////////////////////////////////////////////////////
//
// What the drag draws
// -------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   The two drops look different because they *are* different, and the drawing
/// is the only warning a user gets before letting go. Measured off the picture
/// rather than off the indicator's bounds, for the reason overlayPanelTests.cpp
/// gives: what would be wrong is what is on the screen.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A swap fills the strip it would change places with", "[gui][modules][drag]")
{
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1, 2}));

    auto const idle(rendered(editor));

    editor.showModuleDrop({Drop::swap, 2});
    auto const marked(rendered(editor));

    // The target strip is filled -- most of it, not an edge of it...
    CHECK(differenceOver(idle, marked, slotRectangle(2)) > 0.66);
    // ...and it is the only one that changed, which is what "this one" means.
    CHECK(differenceOver(idle, marked, slotRectangle(0)) == 0);
    CHECK(differenceOver(idle, marked, slotRectangle(1)) == 0);

    editor.showModuleDrop({});
    CHECK(differenceOver(idle, rendered(editor), slotRectangle(2)) == 0);
}

TEST_CASE("An insert draws a line between two strips", "[gui][modules][drag]")
{
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1, 2}));

    auto const idle(rendered(editor));

    editor.showModuleDrop({Drop::insert, 2});
    auto const marked(rendered(editor));

    /// \note A line, so what it changes is a sliver of each of the two strips it
    /// is between and nothing else. The 0.2 is the other side of the swap case's
    /// 0.66: four pixels of a 68 px strip is 6 %, and a filled strip would be
    /// well over either number.
    CHECK(differenceOver(idle, marked, slotRectangle(1)) > 0);
    CHECK(differenceOver(idle, marked, slotRectangle(1)) < 0.2);
    CHECK(differenceOver(idle, marked, slotRectangle(2)) > 0);
    CHECK(differenceOver(idle, marked, slotRectangle(2)) < 0.2);
    // The strip that is not beside the gap is untouched.
    CHECK(differenceOver(idle, marked, slotRectangle(0)) == 0);

    // And the gap at the very front is drawable too, which is the edge case:
    // half of that line is outside the rack.
    editor.showModuleDrop({Drop::insert, 0});
    CHECK(differenceOver(idle, rendered(editor), slotRectangle(0)) > 0);

    editor.showModuleDrop({});
    CHECK(differenceOver(idle, rendered(editor), slotRectangle(0)) == 0);
}
