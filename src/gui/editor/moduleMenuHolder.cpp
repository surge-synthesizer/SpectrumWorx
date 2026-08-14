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

#include "le/spectrumworx/effects/configuration/effectIndexToGroupMapping.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/effects/configuration/includedEffects.hpp"
#include "le/utility/typeList.hpp"

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

template <unsigned int moduleIndex, unsigned int subMenuIndex>
void addModulesToMenu(Menus &, std::true_type /*end reached*/)
{
}

template <unsigned int moduleIndex, unsigned int subMenuIndex>
LE_FORCEINLINE void addModulesToMenu(Menus &menus, std::false_type /*end not reached*/)
{
    std::uint8_t const menuIndex(1 + subMenuIndex);
    addModuleToMenuEntry(menus[menuIndex], moduleIndex);

    using LastModule = std::bool_constant<(moduleIndex + 1) == Effects::Constants::numberOfEffects>;
    unsigned int const nextModuleIndex(moduleIndex + !LastModule::value);

    typedef typename Effects::Group<moduleIndex>::type CurrentModuleGroup;
    typedef typename Effects::Group<nextModuleIndex>::type NextModuleGroup;
    unsigned int const nextSubMenuIndex(subMenuIndex +
                                        !std::is_same_v<CurrentModuleGroup, NextModuleGroup>);

    addModulesToMenu<nextModuleIndex, nextSubMenuIndex>(menus, LastModule());
}

////////////////////////////////////////////////////////////////////////////
///
/// \class TopMenusAdder
///
/// \brief A helper functor for adding menus from a ModuleMenuHolder to a
/// parent menu.
///
////////////////////////////////////////////////////////////////////////////

class TopMenusAdder
{
  public:
    TopMenusAdder(Menus &menus) : pCurrentMenu_(&menus[1]), parentMenu_(menus.front()) {}

    template <class Group> void operator()()
    {
        parentMenu_.addSubMenu(*pCurrentMenu_++, Group::name());
    }

  private:
    void operator=(TopMenusAdder const &);

    Menu *pCurrentMenu_;
    Menu &parentMenu_;
};

#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.

struct FlatMenuAdder
{
    typedef void result_type;
    template <typename ModuleMenuIndex> void operator()() const
    {
        addModuleToMenuEntry(menus[0], ModuleMenuIndex::value);
    }
    Menus &menus;
};

#pragma warning(pop)

/// \brief The effects themselves, which is the part that is built once.
void fillSubMenus(Menus &menus, std::true_type /*has sub menus*/)
{
    // Implementation note:
    //   Our own implementation of the boost::mpl::detail::execute() helper
    // template function that passes along all intermediate results for
    // vastly improved compilation times (on GCC atleast).
    //                                    (23.09.2010.) (Domagoj Saric)
    addModulesToMenu<0, 0>(menus, std::false_type());
}

/// \note One of each pair below is chosen by hasSubMenus and the other is not
/// compiled into anything; which one that is is a configuration choice, not a
/// dead function.
[[maybe_unused]] void fillSubMenus(Menus &, std::false_type /*does not have sub menus*/) {}

/// \brief What goes under the heading: one entry per group, or, with too few
/// effects for that to be worth a level of nesting, one per effect.
void fillTopMenu(Menus &menus, std::true_type /*has sub menus*/)
{
    TopMenusAdder topMenusAdder(menus);
    Utility::forEach<Effects::Groups>(topMenusAdder);
}

[[maybe_unused]] void fillTopMenu(Menus &menus, std::false_type /*does not have sub menus*/)
{
    FlatMenuAdder const adder = {menus};
    Utility::forEach<Effects::ValidIndices>(adder);
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
////////////////////////////////////////////////////////////////////////////////

ModuleMenuHolder::ModuleMenuHolder() { fillSubMenus(menus_, std::bool_constant<hasSubMenus>()); }

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
