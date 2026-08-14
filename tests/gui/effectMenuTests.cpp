////////////////////////////////////////////////////////////////////////////////
///
/// effectMenuTests.cpp
/// -------------------
///
///   The right-click effect menu, which is one list of effects meaning three
/// different things. On a strip, choosing one replaces that effect; between two
/// strips it goes in there and the rest shift along; past the last strip it is
/// added. Which of the three is decided by where the click landed and announced
/// by the menu's heading, so the two halves are asked separately here: where a
/// point puts the menu, and what choosing from it then does to the chain.
///
/// \note The menu is not opened here: what it would decide and what choosing
/// from it then does are both reachable without one on screen, and they are the
/// parts that can be wrong. A headless run can put one up -- menuScaleTests.cpp
/// does, to measure the window it lands as -- but nothing can be chosen from it
/// without a message loop. \see moduleDragTests.cpp, which is the same split for
/// the same rack.
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

#include "gui/editor/moduleMenuHolder.hpp"
#include "gui/modules/moduleUI.hpp"

#include "le/spectrumworx/effects/configuration/effectNames.hpp"

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
using Target = Editor::EffectMenuTarget;
using Strip = GUI::ModuleUI;

constexpr int slotWidth{Strip::width + Strip::distance};
constexpr std::uint8_t rackSlots{SW::Constants::maxNumberOfModules};

/// The middle of the strip drawn in \p slot, which is where a replacement is asked.
juce::Point<int> overSlot(std::uint8_t const slot)
{
    return {Strip::horizontalOffset + (slot * slotWidth) + (slotWidth / 2),
            Strip::verticalOffset + (Strip::height / 2)};
}

/// The boundary between two strips, which is where an insert is asked.
juce::Point<int> overGap(std::uint8_t const gap)
{
    return {Strip::horizontalOffset + (gap * slotWidth),
            Strip::verticalOffset + (Strip::height / 2)};
}

/// \brief An editor with \p effects, one strip each, in the order given.
///
/// \note Overlay placement, so that the skin's coordinates are the editor's --
/// every point above is a position in the skin. \see moduleDragTests.cpp.
Editor &rackOf(SWTest::Instance &instance, std::vector<std::uint8_t> const &effects)
{
    instance.openEditor(Editor::PanelPlacement::overlay);
    auto &editor(instance.editor());
    for (auto const effect : effects)
        editor.addUserAddedModule(effect);
    /// \note By hand: adding a module ends in a posted resync and a test has no
    /// message loop to deliver it.
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

/// \brief Carries \p target out and brings the rack up to date with it, which is
/// what the message loop would do a turn later.
void choose(Editor &editor, Target const target, std::uint8_t const effectIndex)
{
    editor.applyEffectMenuChoice(target, effectIndex);
    editor.resyncModuleRack();
}
//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
//
// Which of the three a point means
// --------------------------------
//
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A strip offers a replacement and the gaps between offer an insert",
          "[gui][modules][menu]")
{
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1, 2}));

    // The middle of a strip: take this one's place.
    for (std::uint8_t slot(0); slot < 3; ++slot)
    {
        CAPTURE(slot);
        auto const target(editor.effectMenuTargetAt(overSlot(slot)));
        CHECK(target.action == Target::replace);
        CHECK(int(target.slot) == slot);
    }

    /// \note And the boundaries between them: a gap has to have width or it is a
    /// target nobody hits, so a band at each end of a strip belongs to the gap
    /// beside it. These points are 8 px either side of the boundary and mean the
    /// same insert.
    for (auto const offset : {-8, 0, +8})
    {
        CAPTURE(offset);
        auto const target(editor.effectMenuTargetAt(overGap(2).translated(offset, 0)));
        CHECK(target.action == Target::insert);
        CHECK(int(target.slot) == 2);
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And it is a *narrow* band -- narrower than the drag's, where these
    /// same two points are an insert. A drag draws where it would land and can be
    /// walked back before the button is let go; a right-click is committed to the
    /// moment it goes down. So most of a strip is the strip, and the seam is
    /// something to be aimed at.
    ///
    ////////////////////////////////////////////////////////////////////////////
    for (auto const offset : {-16, +16})
    {
        CAPTURE(offset);
        CHECK(editor.effectMenuTargetAt(overGap(2).translated(offset, 0)).action ==
              Target::replace);
    }

    // The gap before the first strip is one too, which is how an effect is put
    // at the head of the chain.
    auto const front(editor.effectMenuTargetAt(overGap(0).translated(+4, 0)));
    CHECK(front.action == Target::insert);
    CHECK(int(front.slot) == 0);
}

TEST_CASE("The empty part of the rack is where an effect is added", "[gui][modules][menu]")
{
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1}));

    // Every slot past the last strip.
    for (std::uint8_t slot(2); slot < rackSlots; ++slot)
    {
        CAPTURE(slot);
        auto const target(editor.effectMenuTargetAt(overSlot(slot)));
        CHECK(target.action == Target::append);
        CHECK(int(target.slot) == 2);
    }

    /// \note Including the strip edge that abuts it. That edge is an insert zone
    /// by geometry, but the gap it names is the end of the rack -- where an
    /// insert and an add are the same thing -- so it is called what it does,
    /// rather than changing the heading across a boundary nothing else marks.
    auto const lastEdge(editor.effectMenuTargetAt(overSlot(1).translated(+24, 0)));
    CHECK(lastEdge.action == Target::append);

    /// \note And an editor with no modules at all, which is the state every one
    /// of them opens in: the whole rack is the empty part.
    SWTest::Instance empty;
    auto &bare(rackOf(empty, {}));
    CHECK(bare.effectMenuTargetAt(overSlot(0)).action == Target::append);
    CHECK(int(bare.effectMenuTargetAt(overSlot(0)).slot) == 0);
}

TEST_CASE("The menu is not offered away from the rack", "[gui][modules][menu]")
{
    /// \note The skin is mostly not the rack -- the spectrum display, the shared
    /// controls, the logo -- and a right-click there is somebody missing, not
    /// somebody asking for an effect.
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1}));

    // To the left of the first strip...
    CHECK(editor.effectMenuTargetAt({Strip::horizontalOffset - 1, overSlot(0).getY()}).action ==
          Target::none);
    // ...past the last slot the rack has...
    CHECK(editor.effectMenuTargetAt(overSlot(rackSlots)).action == Target::none);
    // ...and above and below it.
    CHECK(editor.effectMenuTargetAt({overSlot(0).getX(), Strip::verticalOffset - 1}).action ==
          Target::none);
    CHECK(editor.effectMenuTargetAt({overSlot(0).getX(), Strip::verticalOffset + Strip::height + 1})
              .action == Target::none);
}

TEST_CASE("A full rack offers replacement everywhere", "[gui][modules][menu]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note With every slot taken there is nowhere to insert, and the only way
    /// to make room would be to drop somebody's last module -- which is not what
    /// anybody means by "insert". So the gaps close and the strips are
    /// replaceable edge to edge, which is the one thing that still works.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1, 2, 3, 4}));

    REQUIRE(chainOrder(instance).size() == rackSlots);

    for (std::uint8_t slot(0); slot < rackSlots; ++slot)
    {
        CAPTURE(slot);
        CHECK(editor.effectMenuTargetAt(overSlot(slot)).action == Target::replace);
        // The boundary that would be a gap in a rack with room in it.
        auto const edge(editor.effectMenuTargetAt(overGap(slot).translated(+2, 0)));
        CHECK(edge.action == Target::replace);
        CHECK(int(edge.slot) == slot);
    }
}

TEST_CASE("The heading says which of the three it is", "[gui][modules][menu]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The heading is the whole of what tells a user that this menu is
    /// about to replace what they right-clicked rather than add to it, so it is
    /// held to the words rather than to "something non-empty".
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;

    /// \note Found by name rather than assumed to be an index, so that the case
    /// keeps meaning "the strip's own effect" if the effect list is reordered.
    auto const tonal(Effects::effectIndex("Tonal"));
    auto const frecho(Effects::effectIndex("Frecho"));
    REQUIRE(tonal >= 0);
    REQUIRE(frecho >= 0);

    auto &editor(rackOf(instance, {std::uint8_t(tonal), std::uint8_t(frecho)}));

    // A replacement names what it would replace, which is the strip pointed at...
    CHECK(editor.effectMenuHeader({Target::replace, 0}) == juce::String("Replace Tonal Effect"));
    // ...and it is *that* strip rather than the first one or a fixed word.
    CHECK(editor.effectMenuHeader({Target::replace, 1}) == juce::String("Replace Frecho Effect"));

    // The other two point at a gap, which has no name to give.
    CHECK(editor.effectMenuHeader({Target::insert, 1}) == juce::String("Insert effect"));
    CHECK(editor.effectMenuHeader({Target::append, 2}) == juce::String("Add effect"));
}

TEST_CASE("The effect list is the same list under any heading", "[gui][modules][menu]")
{
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1}));

    GUI::ModuleMenuHolder holder;

    /// \note Read at once, because all three are the same menu object refilled:
    /// the effects are built once and only the heading is per use.
    struct Shape
    {
        juce::String header;
        bool ruledOff;
        unsigned int items;
    };
    auto const headed([&holder, &editor](Target const target) {
        auto const &menu(holder.menuWithHeader(editor.effectMenuHeader(target).toRawUTF8()));
        return Shape{menu.getItemText(0), menu.getItemText(1).isEmpty(), menu.numberOfItems()};
    });

    auto const replace(headed({Target::replace, 0}));
    CHECK(replace.header.startsWith("Replace "));
    // The separator under it, which carries no text of its own.
    CHECK(replace.ruledOff);
    // ...and the effects themselves, which is what the menu is for.
    CHECK(replace.items > 2);

    auto const insert(headed({Target::insert, 1}));
    CHECK(insert.header == juce::String("Insert effect"));
    CHECK(insert.items == replace.items);

    auto const append(headed({Target::append, 2}));
    CHECK(append.header == juce::String("Add effect"));
    CHECK(append.items == replace.items);
}

////////////////////////////////////////////////////////////////////////////////
//
// What choosing an effect does
// ----------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   The rack and the chain, both. The rack is what the user is looking at and
/// the chain is what is playing; an edit that reaches one and not the other is a
/// window that looks right and plays something else.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Replacing an effect leaves the rest of the rack where it was", "[gui][modules][menu]")
{
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1, 2}));

    REQUIRE(chainOrder(instance) == std::vector<int>{0, 1, 2});

    choose(editor, {Target::replace, 1}, 3);

    INFO("chain " << describe(chainOrder(instance)));
    CHECK(chainOrder(instance) == std::vector<int>{0, 3, 2});
    CHECK(rackOrder(editor, 3) == std::vector<int>{0, 3, 2});

    // The ends too, which is where an off-by-one would land.
    choose(editor, {Target::replace, 0}, 4);
    choose(editor, {Target::replace, 2}, 1);

    INFO("chain " << describe(chainOrder(instance)));
    CHECK(chainOrder(instance) == std::vector<int>{4, 3, 1});
    CHECK(rackOrder(editor, 3) == std::vector<int>{4, 3, 1});
}

TEST_CASE("Inserting an effect shifts the rest along", "[gui][modules][menu]")
{
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1, 2}));

    REQUIRE(chainOrder(instance) == std::vector<int>{0, 1, 2});

    /// \note Into the middle, which is the case that tells a real insert from an
    /// append that happened to be drawn in the right place: the chain grows by
    /// one and everything from the gap onwards moves down a slot.
    choose(editor, {Target::insert, 1}, 3);

    INFO("chain " << describe(chainOrder(instance)));
    CHECK(chainOrder(instance) == std::vector<int>{0, 3, 1, 2});
    CHECK(rackOrder(editor, 4) == std::vector<int>{0, 3, 1, 2});

    // And at the head of the chain, which is the gap with nothing to its left.
    choose(editor, {Target::insert, 0}, 4);

    INFO("chain " << describe(chainOrder(instance)));
    CHECK(chainOrder(instance) == std::vector<int>{4, 0, 3, 1, 2});
    CHECK(rackOrder(editor, 5) == std::vector<int>{4, 0, 3, 1, 2});
}

TEST_CASE("Adding an effect puts it on the end", "[gui][modules][menu]")
{
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    auto &editor(rackOf(instance, {0, 1}));

    choose(editor, {Target::append, 2}, 3);

    CHECK(chainOrder(instance) == std::vector<int>{0, 1, 3});
    CHECK(rackOrder(editor, 3) == std::vector<int>{0, 1, 3});
}

TEST_CASE("A choice that would change nothing changes nothing", "[gui][modules][menu]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Two of them, and the second is the one that matters: the menu is
    /// asynchronous, so a rack that filled up while it was open would otherwise
    /// have an insert applied to it with nowhere to put the module.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    {
        SWTest::Instance instance;
        auto &editor(rackOf(instance, {0, 1, 2}));
        auto const asked(editor.rackResyncRequests());

        // The effect that is already there.
        choose(editor, {Target::replace, 1}, 1);

        CHECK(chainOrder(instance) == std::vector<int>{0, 1, 2});
        CHECK(editor.rackResyncRequests() == asked);
    }

    {
        SWTest::Instance instance;
        auto &editor(rackOf(instance, {0, 1, 2, 3, 4}));
        auto const asked(editor.rackResyncRequests());

        // An insert into a rack with no room for one.
        choose(editor, {Target::insert, 2}, 1);

        CHECK(chainOrder(instance) == std::vector<int>{0, 1, 2, 3, 4});
        CHECK(editor.rackResyncRequests() == asked);
    }
}
