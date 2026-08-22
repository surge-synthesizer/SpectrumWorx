////////////////////////////////////////////////////////////////////////////////
///
/// \file constants.hpp
/// -------------
///
/// Effect counts, derived from effectsList.hpp.
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef constants_hpp__2BFA9531_49DB_46E6_A20E_3B57837B893F
#define constants_hpp__2BFA9531_49DB_46E6_A20E_3B57837B893F
//------------------------------------------------------------------------------
#include "effectsList.hpp"

#include <cstdint>

namespace LE::SW::Effects::Constants
{

std::uint8_t constexpr numberOfEffects{LE_SW_NUMBER_OF_EFFECTS};

/// \note Every effect ships in every build now that editions are gone. Kept as
/// a separate name because the plugin, the presets and the GUI all still ask
/// the question separately.
std::uint8_t constexpr numberOfIncludedEffects{numberOfEffects};

} // namespace LE::SW::Effects::Constants

#endif // constants_hpp
