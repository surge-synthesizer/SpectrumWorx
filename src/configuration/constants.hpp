////////////////////////////////////////////////////////////////////////////////
///
/// \file constants.hpp
/// -------------------
///
/// Project wide global configuration.
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef constants_hpp__A6314879_EA41_4A25_9F86_751B6318B5D3
#define constants_hpp__A6314879_EA41_4A25_9F86_751B6318B5D3
//------------------------------------------------------------------------------
#include "le/utility/cstdint.hpp"

namespace LE::SW::Constants
{

std::uint8_t const maxNumberOfModules(5);

/// \brief How many parameters of a module a host can address, base block
/// included.
///
/// \note Eighteen because the base block is five and the widest effect declares
/// thirteen. Not a round number, and not one to raise for headroom: one more
/// costs a row in each of the `maxNumberOfModules` slots, plus the seven LFO
/// parameters that drive it in each -- forty in a host's automation list for a
/// parameter no effect has.
///
/// \note Ten until issue #156, which put TuneWorx's `Semi05`..`Semi12` out of a
/// DAW's reach for fifteen years. `largestEffectParameterCount` in factory.cpp
/// is the compile-time assertion that no effect is over the ceiling again.
std::uint8_t const maxNumberOfParametersPerModule(18);
std::uint8_t const maxNumberOfModuleParameters(maxNumberOfModules *maxNumberOfParametersPerModule);

std::uint8_t const numberOfPrograms(4);

} // namespace LE::SW::Constants

#endif // constants_hpp
