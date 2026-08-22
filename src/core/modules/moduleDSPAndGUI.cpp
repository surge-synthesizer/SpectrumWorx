////////////////////////////////////////////////////////////////////////////////
///
/// module.cpp
/// ----------
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "moduleDSPAndGUI.hpp"

#include "automatedModuleImpl.inl"

#include "le/utility/parentFromMember.hpp"
#include "core/modules/factory.hpp"
#include "le/spectrumworx/engine/moduleNode.hpp" // for intrusive_ptr_release_deleter

#include "le/utility/intrusivePtr.hpp"

#include <optional>

namespace LE::SW
{

// Implementation note:
//   This is required to prevent Clang from inlining the base destructor into
// each ModuleDSP<> destructor.
//                                            (13.12.2011.) (Domagoj Saric)
LE_NOINLINE Module::~Module() {}

/// \note A module builds and owns no widgets: the editor owns the region, builds
/// it in the region's own constructor on the thread that owns widgets, and the
/// region holds a counted reference so the module cannot go while it is drawn.

float Module::setParameterValueFromUI(std::uint8_t const parameterIndex, float const value)
{
    LE_ASSERT_MSG((parameterIndex == 0) || !lfo(parameterIndex - 1).enabled(),
                  "Parameter changed from the GUI while its LFO is enabled?");
    return (parameterIndex < numberOfBaseParameters)
               ? ModuleDSP::setBaseParameter(parameterIndex, value)
               : ModuleDSP::setEffectParameter(effectSpecificParameterIndex(parameterIndex), value);
}

namespace Engine
{
/// \note Nothing here touches an editor region, and nothing may: the reference
/// that reaches zero can be the *audio thread's* -- the chain holds one per node
/// while it walks it -- so taking a widget down here would post a message from
/// inside the audio callback. A region holds a counted reference of its own, so a
/// module that is drawn does not reach zero.
void LE_NOINLINE intrusive_ptr_release_deleter(ModuleNode const *LE_RESTRICT const pModuleNode)
{
    auto const &module(actualModule<Module>(*pModuleNode));
    LE_ASSERT(module.referenceCount_ == 0);
    ModuleFactory::destroy(module);
}
} // namespace Engine

} // namespace LE::SW
