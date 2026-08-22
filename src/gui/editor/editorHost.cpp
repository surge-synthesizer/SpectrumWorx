////////////////////////////////////////////////////////////////////////////////
///
/// editorHost.cpp
/// --------------
///
///   The one thing on `EditorHost` that is not a pure virtual: the conversion
/// between the value a user is looking at and the value the host automation edge
/// carries. \see EditorHost::editGlobalParameter().
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "editorHost.hpp"

#include "core/host_interop/plugin2Host.hpp"
#include "core/parameterID.hpp"

#include "le/parameters/parametersUtilities.hpp"
#include "le/plugins/clap/tag.hpp"
#include "le/plugins/plugin.hpp"
#include "le/spectrumworx/engine/parameters.hpp"

namespace LE::SW::GUI
{
namespace
{
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
///
/// \note `FullRangeAutomatedParameter` is not the identity it reads as. A
/// power-of-two parameter crosses the edge as its **exponent** -- the FFT size
/// as 11, not as 2048 -- and an enumerated one as its ordinal; only the linear
/// parameters, which is the three gain-shaped globals, pass through unchanged.
/// That is why this was invisible: the three knobs were right and the three
/// combo boxes on the settings page were not.
///
////////////////////////////////////////////////////////////////////////////////

struct ToAutomationValue
{
    using result_type = Plugins::AutomatedParameterValue;
    using AutomatedParameter = Plugins::AutomatedParameterFor<Plugins::Protocol::CLAP>::type;

    float const value;

    template <class Parameter> result_type operator()() const
    {
        return AutomatedParameter::template convertParameterValueToAutomationValue<Parameter>(
            static_cast<typename Parameter::value_type>(value));
    }
}; // struct ToAutomationValue

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

void EditorHost::editGlobalParameter(std::uint8_t const index, float const value) const
{
    ParameterID parameterID;
    parameterID.value.type = ParameterID::GlobalParameter;
    parameterID.value._.global = {ParameterID::Zero, ParameterID::Zero, index};

    editParameter(parameterID,
                  static_cast<float>(
                      LE::Parameters::invokeFunctorOnIndexedParameter<GlobalParameters::Parameters>(
                          index, ToAutomationValue{value})));
}

} // namespace LE::SW::GUI
