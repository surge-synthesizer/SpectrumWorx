////////////////////////////////////////////////////////////////////////////////
///
/// \file moduleMenuHolder.hpp
/// --------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef moduleMenuHolder_hpp__B8739940_B004_4435_99BD_39B934BF4DC4
#define moduleMenuHolder_hpp__B8739940_B004_4435_99BD_39B934BF4DC4
//------------------------------------------------------------------------------
#include "gui/gui.hpp"

#include "le/spectrumworx/effects/configuration/constants.hpp"
#include "le/utility/cstdint.hpp"

#include <vector>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class ModuleMenuHolder
///
/// \brief Constructs the menu for SpectrumWorx modules.
///
///    One sub-menu per group, filled from ModuleMenuLayout -- which is where the
/// groups, their order and the order of the effects within them are declared.
///
/// \note It read the effect list itself until 18.08.2026: a compile-time
/// recursion over `LE_SW_EFFECT_LIST` that took the effects in index order and
/// started a new sub-menu wherever a fourth column's group changed. That made
/// the menu a rendering of a table whose order is ABI and can therefore never
/// move, which is issue #121. The class-template configurability the 2010
/// version had -- nested groups, arbitrary hierarchies -- went in 2010 and has
/// not come back; a deeper menu would be another table, not another traversal.
///
////////////////////////////////////////////////////////////////////////////////

class ModuleMenuHolder
{
  public:
    ModuleMenuHolder(ModuleMenuHolder const &) = delete; // makes non-copyable
    ModuleMenuHolder &operator=(ModuleMenuHolder const &) = delete;

    static unsigned int const minimumEffectsForSubMenus = 15;
    static bool const hasSubMenus =
        Effects::Constants::numberOfIncludedEffects >= minimumEffectsForSubMenus;

    using Menu = GUI::PopupMenu;
    /// \note A vector, sized from ModuleMenuLayout, rather than a std::array
    /// sized from a `numberOfGroups` constant kept beside the effect list. How
    /// many groups there are is now a property of the table that lists them, and
    /// a PopupMenu is neither copyable nor movable -- which `std::vector<Menu>
    /// v( n )` does not ask it to be. \see issue #121.
    using Menus = std::vector<Menu>;

  public: // Public interface.
    ModuleMenuHolder();

    bool isOwnerOfEntry(unsigned int menuEntryID) const;
    std::uint8_t effectIndexForEntry(unsigned int menuEntryID) const;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The effect list, under \p title and a separating rule.
    ///
    /// \note The same entries every time; only the heading changes, and it is
    /// what says which of the several things choosing one can now mean -- adding
    /// an effect, inserting one between two others, replacing one. The top menu
    /// is therefore refilled per use rather than built once in the constructor:
    /// it is a handful of strings, and the effects' own sub-menus -- the part
    /// that is a compile-time traversal -- are still built exactly once.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Menu const &menuWithHeader(char const *title);

  private:
    Menu &topMenu() { return menus_.front(); }
    Menu &subMenu(std::uint8_t index);

  private:
    Menus menus_;
}; // class ModuleMenuHolder

} // namespace LE::SW::GUI

#endif // moduleMenuHolder_hpp
