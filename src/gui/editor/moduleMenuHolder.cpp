////////////////////////////////////////////////////////////////////////////////
///
/// moduleMenuHolder.cpp
/// --------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "moduleMenuHolder.hpp"

#include "moduleMenuLayout.hpp"

#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/effects/configuration/includedEffects.hpp"

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
//
// Private implementation details for this module.
// -----------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////

namespace
{
using Menu = ModuleMenuHolder::Menu;
using Menus = ModuleMenuHolder::Menus;

void addModuleToMenuEntry(Menu &menu, std::uint8_t const moduleIndex)
{
    unsigned int const menuEntryID(moduleIndex);
    char const *const moduleName(Effects::effectName(moduleIndex));
    bool const moduleEnabled(Effects::includedEffects[moduleIndex]);
    menu.addItem(menuEntryID, moduleName, nullptr, moduleEnabled);
}

/// \brief The effects themselves, which is the part that is built once.
///
/// \note A walk over ModuleMenuLayout rather than the compile-time recursion
/// over the effect list this used to be. That recursion took the effects in
/// index order and started a new sub-menu wherever the group changed, which made
/// the menu a *rendering* of `LE_SW_EFFECT_LIST` -- and that list's order is ABI.
/// \see issue #121.
void fillSubMenus(Menus &menus, std::true_type /*has sub menus*/)
{
    std::uint8_t menuIndex{1};
    for (auto const &group : ModuleMenuLayout::groups())
    {
        auto &menu(menus[menuIndex++]);
        for (auto const effect : group.effects)
            addModuleToMenuEntry(menu, effect);
    }
}

/// \note One of each pair below is chosen by hasSubMenus and the other is not
/// compiled into anything; which one that is is a configuration choice, not a
/// dead function.
[[maybe_unused]] void fillSubMenus(Menus &, std::false_type /*does not have sub menus*/) {}

/// \brief What goes under the heading: one entry per group, or, with too few
/// effects for that to be worth a level of nesting, one per effect.
void fillTopMenu(Menus &menus, std::true_type /*has sub menus*/)
{
    auto &topMenu(menus.front());
    std::uint8_t menuIndex{1};
    for (auto const &group : ModuleMenuLayout::groups())
        topMenu.addSubMenu(menus[menuIndex++], group.title);
}

[[maybe_unused]] void fillTopMenu(Menus &menus, std::false_type /*does not have sub menus*/)
{
    for (auto const &group : ModuleMenuLayout::groups())
        for (auto const effect : group.effects)
            addModuleToMenuEntry(menus.front(), effect);
}
} // namespace

////////////////////////////////////////////////////////////////////////////////
//
// ModuleMenuHolder::ModuleMenuHolder()
// ------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \throws std::bad_alloc Out of memory.
///
/// \note The menu count comes from the layout rather than from a constant beside
/// the effect list: how many groups there are is a property of the table that
/// declares them. `ModuleMenuLayout::groups()` is also what terminates if that
/// table has lost an effect, so building the menu is the moment the check runs.
///
////////////////////////////////////////////////////////////////////////////////

ModuleMenuHolder::ModuleMenuHolder()
    : menus_(1 + (hasSubMenus ? ModuleMenuLayout::groups().size() : 0))
{
    fillSubMenus(menus_, std::bool_constant<hasSubMenus>());
}

ModuleMenuHolder::Menu const &ModuleMenuHolder::menuWithHeader(char const *const title)
{
    auto &menu(topMenu());
    menu.clear();
    menu.addSectionHeader(title);
    menu.addSeparator();
    fillTopMenu(menus_, std::bool_constant<hasSubMenus>());
    return menu;
}

bool ModuleMenuHolder::isOwnerOfEntry(unsigned int const menuEntryID) const
{
    unsigned int const totalNumberOfMenuEntries(Effects::Constants::numberOfEffects);
    return menuEntryID < totalNumberOfMenuEntries;
}

std::uint8_t ModuleMenuHolder::effectIndexForEntry(unsigned int const menuEntryID) const
{
    LE_ASSERT(isOwnerOfEntry(menuEntryID));
    return static_cast<std::uint8_t>(menuEntryID);
}

Menu &ModuleMenuHolder::subMenu(std::uint8_t const index) { return menus_[1 + index]; }

} // namespace LE::SW::GUI
