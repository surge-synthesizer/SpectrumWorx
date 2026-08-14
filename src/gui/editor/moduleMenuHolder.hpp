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

#include <array>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class ModuleMenuHolder
///
/// \brief Constructs the menu for SpectrumWorx modules.
///
///    It uses the Modules typelist defined by the moduleList.hpp header, the
/// group hierarchy defined in the effectGroups.hpp header and the traits/
/// metadata provided by each module class (namely the parent group) to detect
/// interrelationships between individual modules and groups.
///
///    To speed up compilation, the current implementation has been vastly
/// simplified, it no longer supports submenus/nested groups and complex
/// hierarchies nor can it be configured through template parameters. This is
/// sufficient for the current SW design, check the SVN history (before revision
/// 2748) if a more complex implementation becomes necessary again.
///    As a further compile time optimization, the number of groups used (and
/// thus menus) is specified directly/hardcoded in the header (to prevent the
/// inclusion of the moduleList.hpp header) and then compile-time check in the
/// .cpp file.
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
    using Menus = std::array<Menu, 1 + (hasSubMenus ? Effects::Constants::numberOfGroups : 0)>;

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
    ///                                       (14.08.2026.) (SW port)
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
