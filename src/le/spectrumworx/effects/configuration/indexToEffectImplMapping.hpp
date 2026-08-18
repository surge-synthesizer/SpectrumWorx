////////////////////////////////////////////////////////////////////////////////
///
/// \file indexToEffectImplMapping.hpp
/// ----------------------------
///
/// Effect index -> implementation class. Was 57 explicit template
/// specialisations written by CMake; now one tuple indexed by std::tuple_element.
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef indexToEffectImplMapping_hpp__A27FC4FD_E1A2_424F_97E4_17387681054F
#define indexToEffectImplMapping_hpp__A27FC4FD_E1A2_424F_97E4_17387681054F
//------------------------------------------------------------------------------
#include "allEffectImpls.hpp"
#include "constants.hpp"
#include "effectsList.hpp"

#include <tuple>

namespace LE::SW::Effects
{

#define LE_SW_AUX_EFFECT_IMPL(folder, module, name) name##Impl,
using EffectImpls = std::tuple<LE_SW_EFFECT_LIST(LE_SW_AUX_EFFECT_IMPL) void>;
#undef LE_SW_AUX_EFFECT_IMPL

static_assert(std::tuple_size_v<EffectImpls> == Constants::numberOfEffects + 1, "");

template <unsigned int index> struct ImplForIndex
{
    using type = std::tuple_element_t<index, EffectImpls>;
};

} // namespace LE::SW::Effects

#endif // indexToEffectImplMapping_hpp
