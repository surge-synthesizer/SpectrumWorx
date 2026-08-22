////////////////////////////////////////////////////////////////////////////////
///
/// \file factory.hpp
/// -----------------
///
/// Copyright (c) 2014 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef moduleFactory_hpp__C444656C_70DA_479E_8BB5_C889A9B1EFA5
#define moduleFactory_hpp__C444656C_70DA_479E_8BB5_C889A9B1EFA5
//------------------------------------------------------------------------------
#include "le/utility/platformSpecifics.hpp"
#include "le/utility/cstdint.hpp"

#include "le/utility/intrusivePtr.hpp"

namespace LE::SW
{

struct ModuleFactory
{
    template <class ModuleInterface>
    static LE::Utility::IntrusivePtr<ModuleInterface> create(std::int8_t effectIndex);

    /// \note create() malloc()s and placement-news the *derived*
    /// ModuleInterface::Impl<Effect>, so destruction has to run that type's
    /// destructor and free() the same storage. `delete &module` would be
    /// mismatched with malloc, and would not run the derived destructor either --
    /// ModuleInterface's is not virtual outside the SDK build.
    template <class ModuleInterface> static void destroy(ModuleInterface const &);
}; // struct ModuleFactory

} // namespace LE::SW

#endif // moduleFactory_hpp
