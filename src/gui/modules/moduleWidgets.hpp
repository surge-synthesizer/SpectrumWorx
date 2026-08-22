////////////////////////////////////////////////////////////////////////////////
///
/// \file moduleWidgets.hpp
/// -----------------------
///
///   The controls for one effect's parameters, owned by the region that draws
/// them.
///
/// \note Not by the module: a module carrying its effect's widget storage inline
/// is what would make `sw-dsp` -- the engine, the effects, and no host -- link
/// `juce_gui_basics`. Which effect's widgets to build is an *index* at runtime,
/// so the per-effect instantiation happens behind one table here.
///
/// See doc/tech/threading_model.md.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef moduleWidgets_hpp__5C81A93E_47D2_4B60_9F18_A3E620D7C154
#define moduleWidgets_hpp__5C81A93E_47D2_4B60_9F18_A3E620D7C154
//------------------------------------------------------------------------------
#include "le/utility/cstdint.hpp"

#include <memory>

namespace LE::SW::GUI
{

class ModuleUI;

////////////////////////////////////////////////////////////////////////////////
///
/// \class ModuleWidgets
///
/// \brief One effect's parameter controls, type-erased.
///
/// \note The widgets are children of the ModuleUI and are destroyed by
/// `ParameterWidgets::destroy()` in the derived destructor, so this must outlive
/// nothing and be destroyed before the region it built into.
///
////////////////////////////////////////////////////////////////////////////////

class ModuleWidgets
{
  public:
    virtual ~ModuleWidgets() = default;

  protected:
    ModuleWidgets() = default;

    ModuleWidgets(ModuleWidgets const &) = delete; // makes non-copyable
    ModuleWidgets &operator=(ModuleWidgets const &) = delete;
}; // class ModuleWidgets

/// \brief Builds \p effectIndex's controls into \p region, and names it.
///
/// \return null if the index is not one of the shipped effects.
std::unique_ptr<ModuleWidgets> createModuleWidgets(std::uint8_t effectIndex, ModuleUI &region);

} // namespace LE::SW::GUI

#endif // moduleWidgets_hpp
