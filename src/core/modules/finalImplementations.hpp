////////////////////////////////////////////////////////////////////////////////
///
/// \file finalImplementations.hpp
///
/// Implementations of different module interfaces for a given effect
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef finalImplementations_hpp__A4E1EEF8_DCF0_4AB1_8A68_6767C135FDC6
#define finalImplementations_hpp__A4E1EEF8_DCF0_4AB1_8A68_6767C135FDC6
//------------------------------------------------------------------------------
#include "le/parameters/boolean/tag.hpp"
#include "le/parameters/enumerated/tag.hpp"
#include "le/parameters/linear/tag.hpp"
#include "le/parameters/symmetric/tag.hpp"
#include "le/parameters/trigger/tag.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/spectrumworx/effects/effects.hpp"
#include "core/modules/moduleDSPAndGUI.hpp"
#include "le/spectrumworx/engine/moduleImpl.hpp"
#include "le/spectrumworx/engine/moduleParameters.hpp"
#include "le/utility/cstdint.hpp"
#include "le/utility/platformSpecifics.hpp"

#include <array>

namespace LE::SW
{

/// \note A module carries no widget storage: `GUI::WidgetsFor<Effect>` lives in
/// gui/modules/moduleWidgets.cpp, owned by the region that draws it and chosen by
/// effect index, so `sw-dsp` needs nothing from `juce_gui_basics`.

////////////////////////////////////////////////////////////////////////////////
///
/// \class Module::Impl<>
///
////////////////////////////////////////////////////////////////////////////////

template <class Effect> class Module::Impl final : public Engine::ModuleEffectImpl<Effect, Module>
{
  public:
    template <typename EffectTypeIndex>
    Impl(EffectTypeIndex) : Impl::ModuleEffectImpl(EffectTypeIndex(), this)
    {
    }
}; // class Module::Impl

} // namespace LE::SW

#endif // finalImplementations_hpp
