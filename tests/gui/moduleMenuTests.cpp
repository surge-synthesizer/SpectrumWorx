////////////////////////////////////////////////////////////////////////////////
///
/// moduleMenuTests.cpp
/// -------------------
///
///   That the module menu offers every effect.
///
///   The menu used to be a compile-time walk over `LE_SW_EFFECT_LIST`: effects
/// in index order, a new sub-menu wherever a fourth column's group changed. That
/// order is ABI -- presets and automation address effects by index -- so the
/// menu could not be rearranged without breaking every file ever saved, which is
/// issue #121. `ModuleMenuLayout` is the separate table, and the one thing a
/// separate table needs somebody to check is that it has not lost anything: an
/// effect in no group is an effect nobody can add, and the preset loader, the
/// parameter table and the engine would all go on working perfectly without
/// noticing.
///
/// \note **Nothing here asserts an order.** The order is the whole point of the
/// table -- it is meant to be rearranged by moving a line, and a case that
/// restated it would turn every such move into a failing test. What is checked
/// is coverage, and that the menu is built from the layout whatever the layout
/// says.
///
/// \note The menu is never shown. `GUI::ModuleMenuHolder` builds it eagerly and
/// `GUI::PopupMenu` holds its own entries, so the rows are readable without a
/// modal window -- which a test binary has no message loop to answer anyway.
/// \see the note at the top of discreteParameterTests.cpp.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/editorHarness.hpp"

#include "gui/editor/moduleMenuHolder.hpp"
#include "gui/editor/moduleMenuLayout.hpp"

#include "le/spectrumworx/effects/configuration/constants.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <vector>

#include <cstdint>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace LE;
using namespace LE::SW;

using GUI::ModuleMenuLayout;

/// Every effect the menu offers, in whatever order it offers them.
std::vector<std::uint8_t> listedEffects()
{
    std::vector<std::uint8_t> effects;
    for (auto const &group : ModuleMenuLayout::groups())
        for (auto const effect : group.effects)
            effects.push_back(effect);
    return effects;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("Every effect is in exactly one menu group", "[gui][menu]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note This is the case the `std::terminate()` in
    /// `ModuleMenuLayout::groups()` exists for, asked in the one form a test can
    /// survive. `diagnose()` names what is wrong -- an effect listed twice, one
    /// listed under a name nothing streams as, one listed nowhere -- and the
    /// count and the set below are the same question asked of the result.
    ///
    ////////////////////////////////////////////////////////////////////////////
    CHECK(ModuleMenuLayout::diagnose().empty());

    auto const effects(listedEffects());
    CHECK(effects.size() == Effects::Constants::numberOfEffects);

    std::set<std::uint8_t> const distinct(effects.begin(), effects.end());
    REQUIRE(distinct.size() == effects.size()); // ...none of them twice
    for (std::uint8_t effect{0}; effect < Effects::Constants::numberOfEffects; ++effect)
    {
        INFO("effect " << unsigned(effect));
        CHECK(distinct.contains(effect));
    }
}

TEST_CASE("The menu is one sub-menu per group of the layout", "[gui][menu]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Against the layout rather than against a written-down list of
    /// groups: what this asks is whether `ModuleMenuHolder` still builds what
    /// the table says, which stays true however the table is rearranged. A
    /// group's own entries are not readable from the top menu -- a sub-menu's
    /// items belong to the sub-menu -- and the case above covers them.
    ///
    /// \note An editor, not just a JUCE: a PopupMenu measures every entry it is
    /// given against Theme::singleton(), and the theme is put up when the editor
    /// is.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;
    SWTest::Instance instance;
    instance.openEditor();

    GUI::ModuleMenuHolder holder;
    auto const &menu(holder.menuWithHeader("Add module"));

    auto const groups(ModuleMenuLayout::groups());

    // The heading, the rule under it, and then the groups.
    REQUIRE(menu.numberOfItems() == 2 + groups.size());
    CHECK(menu.getItemText(0) == "Add module");

    for (std::size_t index{0}; index < groups.size(); ++index)
    {
        INFO("group " << index);
        CHECK(menu.getItemText(static_cast<unsigned int>(2 + index)) ==
              juce::String(groups[index].title));
    }
}
