////////////////////////////////////////////////////////////////////////////////
///
/// automatedModule.cpp
/// -------------------
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "automatedModule.hpp"

#include "core/host_interop/parameters.hpp"

#include "le/parameters/conversion.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/parameters/parser.hpp"  // BaseParameters parsers
#include "le/parameters/printer.hpp" // BaseParameters printers
#include "le/plugins/plugin.hpp" //...ugh...mrmlj...for Plugins::*AutomatedParameter usage in printer.hpp...clean this up...
#include "le/spectrumworx/effects/baseParameters.hpp" // BaseParameters printers
#include "le/spectrumworx/engine/moduleParameters.hpp"
#include <optional>

namespace LE::SW::Automation
{

using BaseParameters = Effects::BaseParameters::Parameters;

char const *getParameterValueString(std::uint8_t const index, ParameterPrinter const &printer,
                                    ModuleParameters const &module)
{
    if (index >= module.numberOfParameters())
    {
        LE_ASSERT(printer.printer.buffer[0] == 0);
        return nullptr;
    }

    return (index < BaseParameters::static_size)
               ? LE::Parameters::invokeFunctorOnIndexedParameter<BaseParameters>(
                     index, std::forward<ParameterPrinter const>(printer))
               : module.metaData().getParameterValueString(
                     module.effectSpecificParameterIndex(index), printer);
}

Parameters::ParsedValue parseParameterValue(std::uint8_t const index, ParameterParser const &parser,
                                            ModuleParameters const &module)
{
    if (index >= module.numberOfParameters())
        return {};

    return (index < BaseParameters::static_size)
               ? LE::Parameters::invokeFunctorOnIndexedParameter<BaseParameters>(index, parser)
               : module.metaData().parseParameterValue(module.effectSpecificParameterIndex(index),
                                                       parser);
}

char const *getParameterUnit(std::uint8_t const parameterIndex,
                             ModuleParameters const *LE_RESTRICT const pModule)
{
    if (parameterIndex < BaseParameters::static_size)
    {
        return ModuleParameters::parameterInfos()[parameterIndex].unit;
    }
    else
    {
        if (pModule && parameterIndex < pModule->numberOfParameters())
            return pModule
                ->effectSpecificParameterInfo(pModule->effectSpecificParameterIndex(parameterIndex))
                .unit;
    }
    return nullptr;
}

Plugins::AutomatedParameterValue internal2AutomatedValue(std::uint8_t const parameterIndex,
                                                         float const internalValue,
                                                         bool const normalised,
                                                         ModuleParameters const &module)
{
    LE_ASSERT(parameterIndex < module.numberOfParameters());
    if (!normalised)
        return internalValue;
    if (parameterIndex < module.numberOfBaseParameters)
        return sharedInternal2AutomatedValue(parameterIndex, internalValue, true);
    return effectInternal2AutomatedValue(module.effectSpecificParameterIndex(parameterIndex),
                                         internalValue, true, module);
}
float automated2InternalValue(std::uint8_t const parameterIndex,
                              Plugins::AutomatedParameterValue const automatedValue,
                              bool const normalised, ModuleParameters const &module)
{
    LE_ASSERT(parameterIndex < module.numberOfParameters());
    if (!normalised)
        return automatedValue;
    if (parameterIndex < module.numberOfBaseParameters)
        return sharedAutomated2InternalValue(parameterIndex, automatedValue, true);
    return effectAutomated2InternalValue(module.effectSpecificParameterIndex(parameterIndex),
                                         automatedValue, true, module);
}
Plugins::AutomatedParameterValue
sharedInternal2AutomatedValue(std::uint8_t const sharedParameterIndex, float const internalValue,
                              bool const normalised)
{
    if (!normalised)
        return internalValue;
    return ModuleParameters::parameterToNormalisedValue(
        internalValue, ModuleParameters::parameterInfos()[sharedParameterIndex]);
}
float sharedAutomated2InternalValue(std::uint8_t const sharedParameterIndex,
                                    Plugins::AutomatedParameterValue const automatedValue,
                                    bool const normalised)
{
    if (!normalised)
        return automatedValue;
    return ModuleParameters::normalisedToParameterValue(
        automatedValue, ModuleParameters::parameterInfos()[sharedParameterIndex]);
}
Plugins::AutomatedParameterValue
effectInternal2AutomatedValue(std::uint8_t const effectParameterIndex, float const internalValue,
                              bool const normalised, ModuleParameters const &module)
{
    if (!normalised)
        return internalValue;
    return module.parameterToNormalisedValue(
        internalValue, module.effectSpecificParameterInfo(effectParameterIndex));
}
float effectAutomated2InternalValue(std::uint8_t const effectParameterIndex,
                                    Plugins::AutomatedParameterValue const automatedValue,
                                    bool const normalised, ModuleParameters const &module)
{
    if (!normalised)
        return automatedValue;
    return module.normalisedToParameterValue(
        automatedValue, module.effectSpecificParameterInfo(effectParameterIndex));
}

std::optional<AutoAdjustedLFOParameter>
Detail::autoAdjustedLFOParameter(LFO &lfo, std::uint8_t const lfoParameterIndex,
                                 LFO::Timing const &timing)
{
    //LFO::value_type const * __restrict pSourceBound;
    //LFO::value_type       * __restrict pTargetBound;
    using LE::Parameters::IndexOf;
    auto const lowerBoundIndex(IndexOf<LFO::Parameters, LFO::LowerBound>::value);
    auto const upperBoundIndex(IndexOf<LFO::Parameters, LFO::UpperBound>::value);
    auto const syncTypesIndex(IndexOf<LFO::Parameters, LFO::SyncTypes>::value);
    auto const periodScaleIndex(IndexOf<LFO::Parameters, LFO::PeriodScale>::value);
    LE_ASSERT(lfoParameterIndex < ParameterCounts::lfoExportedParameters);
    switch (lfoParameterIndex)
    {
    /// \note The pair names the parameter that *moved*, which is the one the
    /// caller tells the host about -- these two named the one that was written.
    case lowerBoundIndex:
        if (lfo.upperBound() < lfo.lowerBound())
        {
            lfo.setUpperBound(lfo.lowerBound());
            return AutoAdjustedLFOParameter(upperBoundIndex, lfo.upperBound());
        }
        break;

    case upperBoundIndex:
        if (lfo.lowerBound() > lfo.upperBound())
        {
            lfo.setLowerBound(lfo.upperBound());
            return AutoAdjustedLFOParameter(lowerBoundIndex, lfo.lowerBound());
        }
        break;

    /// \note A period is a division of the bar and each sync type divides it
    /// differently, so the period that was set is not one the new type has. It
    /// was left where it stood -- the `\todo` on LFO::addSyncType since 2011 --
    /// which a preset then stored and the loader snapped somewhere else.
    /// \see issue #192
    case syncTypesIndex:
    {
        if (lfo.syncTypes() == LFO::Free)
            break; // a free period is a duration, and every one of them is legal
        auto const standing(lfo.periodScale());
        auto const snapped(LFO::snapPeriodScale(standing, lfo.syncTypes(), timing).first);
        if (snapped != standing)
        {
            lfo.setPeriodScale(snapped);
            return AutoAdjustedLFOParameter(periodScaleIndex, snapped);
        }
        break;
    }

    default:
        break;
    }
    return std::nullopt;
}

} // namespace LE::SW::Automation
