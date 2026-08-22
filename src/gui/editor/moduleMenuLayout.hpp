////////////////////////////////////////////////////////////////////////////////
///
/// \file moduleMenuLayout.hpp
/// --------------------------
///
///   What the module menu lists, in the order it lists it. \see issue #121.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef moduleMenuLayout_hpp__2B6D6E5A_1C3F_4E4E_9A62_2E9E1B0E7C41
#define moduleMenuLayout_hpp__2B6D6E5A_1C3F_4E4E_9A62_2E9E1B0E7C41
//------------------------------------------------------------------------------
#include "le/utility/span.hpp"

#include <string>

#include <cstdint>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class ModuleMenuLayout
///
/// \brief The module menu's groups and their entries, in the order they are
/// drawn -- which is **not** the order the effects are enumerated in.
///
/// \details An effect's index is ABI: presets and host automation refer to
/// effects by it, so `LE_SW_EFFECT_LIST` may be appended to but never reordered.
/// This table is the separate answer to what the menu shows and in what order.
///
/// \note The entries name effects by their **streaming name** -- the name a
/// preset writes, not the title the menu shows -- so retitling an effect leaves
/// this table alone.
///
////////////////////////////////////////////////////////////////////////////////

class ModuleMenuLayout
{
  public:
    struct Group
    {
        char const *title;
        /// The group's effects, by index, in the order the menu lists them.
        Utility::Span<std::uint8_t const> effects;
    }; // struct Group

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The groups, in menu order, resolved and verified on the first
    /// call.
    ///
    /// \note A table that has lost an effect is an effect no user can reach, and
    /// nothing downstream would notice -- so this **terminates** rather than
    /// carrying on with a menu that is quietly missing something. Call
    /// diagnose() to ask the same question and get an answer back.
    ///
    ////////////////////////////////////////////////////////////////////////////
    static Utility::Span<Group const> groups();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief What is wrong with the table -- a name no effect answers to, an
    /// effect listed twice, an effect not listed at all -- or an empty string.
    ///
    /// \note This is what makes the check above testable: a case can read the
    /// diagnosis where it cannot survive the termination.
    ///
    ////////////////////////////////////////////////////////////////////////////
    static std::string diagnose();
}; // class ModuleMenuLayout

} // namespace LE::SW::GUI

#endif // moduleMenuLayout_hpp
