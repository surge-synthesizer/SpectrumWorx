////////////////////////////////////////////////////////////////////////////////
///
/// plugin2Host.cpp
/// ---------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "plugin2Host.hpp"

#include "core/automatedModuleChain.hpp" // ModuleChainParameter
#include "core/modules/automatedModule.hpp"

#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/engine/moduleParameters.hpp"
#include "le/utility/span.hpp"

namespace LE
{

template <typename Char>
char *copyToBuffer(Char const *string, LE::Utility::Span<char> const &buffer);

namespace SW
{

using LFO = Parameters::LFOImpl;

void Plugin2HostInteropControler::moduleChangedByUser(std::uint8_t const chainParameterIndex,
                                                      Module const *LE_RESTRICT const pModule) const
{
    moduleChangedByUser(chainParameterIndex,
                        pModule ? std::int8_t(pModule->effectTypeIndex()) : std::int8_t(noModule));

    if (parameterListChanged())
        return;

    moduleChanged(chainParameterIndex, pModule);
}

/// \note By effect index, because the editor's callers no longer have a module
/// to point at: filling a slot is a request the engine answers later, and what
/// the host is being told is what the *user* did. Reading it back off the chain
/// would be reading state another thread owns, to say something that was already
/// known.
///                                           (02.08.2026.) (SW port)
void Plugin2HostInteropControler::moduleChangedByUser(std::uint8_t const chainParameterIndex,
                                                      std::int8_t const effectIndex) const
{
    LE_ASSERT(GUI::isThisTheGUIThread());

    ParameterID parameterID;
    parameterID.value.type = ParameterID::ModuleChainParameter;
    parameterID.value._.moduleChain.moduleIndex = chainParameterIndex;
    ModuleChainParameter const parameter(effectIndex);
    ParameterValueForAutomation const automationValue = {
        Plugins::FullRangeAutomatedParameter ::convertParameterToAutomationValue(parameter),
        Plugins::NormalisedAutomatedParameter::convertParameterToAutomationValue(parameter)};

    /// \note The slot selector is a parameter like any other and the user just
    /// moved it, so the host hears about it either way. What
    /// parameterListChanged() governs is only whether the *rest* of that slot's
    /// parameters are then pushed one by one -- see the assertion at the top of
    /// Plugin2HostActiveInteropImpl::moduleChanged, which is that push.
    ///
    ///   This notification used to sit below the early return, so a host that
    /// re-reads the list itself was told nothing at all when a module was added
    /// from the plugin's own UI. Under CLAP that is every host: the parameters
    /// kept the names they were first read with, which for an empty slot is
    /// "N/A".
    ///                                       (29.07.2026.) (SW port)
    ///
    /// \note And only this: pushing the *rest* of that slot's parameters needs a
    /// module to read them off, so it stays in the overload that has one.
    automatedParameterChanged(parameterID, automationValue);
}

void Plugin2HostInteropControler::automatedParameterChanged(Module const &module,
                                                            std::uint8_t const moduleIndex,
                                                            std::uint8_t const moduleParameterIndex,
                                                            float const parameterValue) const
{
    //LE_ASSERT( moduleParameterID.moduleParameterIndex < Constants::maxNumberOfParametersPerModule ); //...mrmlj...TuneWorx...
    /// \todo Consider a smarter place to put this check (currently needed only
    /// for TuneWorx).
    ///                                       (18.01.2012.) (Domagoj Saric)
    if (moduleParameterIndex >= SW::Constants::maxNumberOfParametersPerModule)
        return;
    ParameterID parameterID;
    parameterID.value.type = ParameterID::ModuleParameter;
    parameterID.value._.module.moduleIndex = moduleIndex;
    parameterID.value._.module.moduleParameterIndex = moduleParameterIndex;
    Plugin2HostInteropControler::ParameterValueForAutomation const automationValue = {
        Automation::internal2AutomatedValue(moduleParameterIndex, parameterValue, false, module),
        Automation::internal2AutomatedValue(moduleParameterIndex, parameterValue, true, module)};
    automatedParameterChanged(parameterID, automationValue);
}

void Plugin2HostInteropControler::automatedParameterChanged(ParameterID::LFO const lfoParameterID,
                                                            float const value) const
{
    using namespace SW::Constants;
    using namespace ParameterCounts;
    LE_ASSUME(lfoParameterID.moduleIndex < maxNumberOfModules);
    LE_ASSUME(lfoParameterID.moduleParameterIndex < (maxNumberOfParametersPerModule - 1));
    LE_ASSUME(lfoParameterID.lfoParameterIndex < lfoExportedParameters);

    ParameterID parameterID;
    parameterID.value.type = ParameterID::LFOParameter;
    parameterID.value._.lfo = lfoParameterID;
    ParameterValueForAutomation const automationValue = {
        value, LFO::internal2AutomatedValue(lfoParameterID.lfoParameterIndex, value, true)};
    automatedParameterChanged(parameterID, automationValue);
}

void Plugin2HostInteropControler::globalParameterChanged(
    std::uint8_t const index, ParameterValueForAutomation::value_type const fullRange,
    ParameterValueForAutomation::value_type const normalised,
    bool const
        asDiscreteGesture //....mrmlj...ugh cleanup....for distinction between knobs and comboboxes
)
{
    ParameterID parameterID;
    parameterID.value.type = ParameterID::GlobalParameter;
    parameterID.value._.global.index = index;
    ParameterValueForAutomation const automationValue = {fullRange, normalised};
    if (asDiscreteGesture)
        automatedParameterBeginEdit(parameterID);
    automatedParameterChanged(parameterID, automationValue);
    if (asDiscreteGesture)
        automatedParameterEndEdit(parameterID);
}

void Plugin2HostInteropControler::modulesChanged(AutomatedModuleChain const &chain,
                                                 std::uint8_t /*const*/ firstModuleIndex,
                                                 std::uint8_t /*const*/ lastModuleIndex) const
{
    //...mrmlj...LE_ASSERT( firstModuleIndex <= lastModuleIndex );

    if (parameterListChanged())
        return;

    //...mrmlj...
    if (firstModuleIndex > lastModuleIndex)
        std::swap(firstModuleIndex, lastModuleIndex);

    std::uint8_t moduleIndex(firstModuleIndex);
    std::uint8_t const moduleIndexEnd(lastModuleIndex + 1);
    auto moduleIter(chain.Engine::ModuleChainImpl::module(moduleIndex)); //...mrmlj...
    while (moduleIndex != moduleIndexEnd)
    {
        LE_ASSERT_MSG(moduleIter->referenceCount_ >= 3,
                      "Module chain altered while notifying host about current state.");
        Module const *pModule(nullptr);
        if (!chain.isEnd(moduleIter))
        {
            pModule = &Engine::actualModule<Module const>(*moduleIter);
            ++moduleIter;
        }
        moduleChangedByUser(moduleIndex++, pModule);
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// Parameters
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
/// \class Plugin2HostPassiveInteropController::ParameterNameGetter
///
////////////////////////////////////////////////////////////////////////////////

#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.
struct Plugin2HostPassiveInteropController::ParameterNameGetter
{
    using result_type = void;

    result_type operator()(ParameterID::Global, Program const *) const;
    result_type operator()(ParameterID::ModuleChain, Program const *) const;
    result_type operator()(ParameterID::Module, Program const *) const;
    result_type operator()(ParameterID::LFO, Program const *) const;

    typedef LE::Utility::Span<char> Buffer;
    Buffer const buffer_;
}; // struct Plugin2HostPassiveInteropController
#pragma warning(pop)

void Plugin2HostPassiveInteropController::getParameterLabel(
    ParameterID const parameterID, LE::Utility::Span<char> const label,
    Program const *LE_RESTRICT const pProgram)
{
    LE_ASSERT(label[0] == 0);
    char const *const pUnit(
        invokeFunctorOnIdentifiedParameter(parameterID, ParameterLabelGetter(), pProgram));
    if (pUnit)
    {
        LE_ASSERT(std::strlen(pUnit) < unsigned(label.size() - 1));
        std::strcpy(label.begin(), pUnit);
    }
}

#if 0
void Plugin2HostPassiveInteropController::getParameterDisplay( ParameterID const parameterID, LE::Utility::Span<char> const text, Engine::Setup const & engineSetup, Plugins::AutomatedParameterValue const * LE_RESTRICT const pValue, Program const & program )
{
#ifdef _WIN32
    LE_ASSUME( pValue == nullptr );
#endif // _WIN32
    ParameterValueStringGetter const getter = {{ engineSetup, pValue, text }};
    char const * const pValueString( invokeFunctorOnIdentifiedParameter( parameterID, std::forward<ParameterValueStringGetter const>( getter ), &program ) );
    copyToBuffer( pValueString, text );
}
#endif

void Plugin2HostPassiveInteropController::getParameterName(
    ParameterID const parameterID, LE::Utility::Span<char> const name,
    Program const *LE_RESTRICT const pProgram)
{
    ParameterNameGetter const getter = {name};
    invokeFunctorOnIdentifiedParameter(parameterID, std::forward<ParameterNameGetter const>(getter),
                                       pProgram);
}

namespace
{
/// \note How many of Program's global parameters actually reach the host: all
/// of them, and this is the name for that rather than a subtraction.
///
///   It used to subtract InputMode, an I/O-routing parameter that entered the
/// list at LE_SW_ENGINE_INPUT_MODE >= 1 and was withheld from the host at >= 2
/// -- two different conditions, and numberOfParameters() and getParameterIDs()
/// each read a different one. Nothing defined the macro, so the parameter was
/// absent to begin with, the subtraction removed a real parameter, and
/// getParameterIDs wrote one ID past the buffer its own caller had sized.
/// Latent since 2013 and invisible to the 2016 builds, which all set it to 2.
/// The macro and the parameter are gone as of 04.08.2026 -- CLAP configures I/O
/// through audio-ports-config, not through an automatable parameter -- so what
/// is left is the name.
///                                           (28.07.2026.) (SW port)
std::uint16_t constexpr exportedGlobalParameters{Program::Parameters::static_size};
} // anonymous namespace

void Plugin2HostPassiveInteropController::getParameterIDs(
    LE::Utility::Span<Plugins::ParameterID> const ids, Program const *LE_RESTRICT const pProgram)
{
    LE_ASSERT_MSG(ids.size() >= exportedGlobalParameters + Constants::maxNumberOfModules,
                  "ParameterIDs buffer too small.");

    Plugins::ParameterID *LE_RESTRICT pParameterID(ids.begin());

    ParameterID parameterID;

    using index_t = std::uint8_t;
    using Module = Plugin2HostInteropControler::Module;

    /// \note AU does not support plugin initiated IO channel configuration
    /// changes so we do not 'export' the InputMode parameter.
    ///                                       (18.03.2013.) (Domagoj Saric)
    index_t const inputModeIndex(Program::Parameters::static_size); //...mrmlj...

    parameterID.binaryValue = 0;
    parameterID.value.type = ParameterID::GlobalParameter;
    for (index_t globalParameterIndex(0); globalParameterIndex < inputModeIndex;
         ++globalParameterIndex, ++pParameterID)
    {
        parameterID.value._.global.index = globalParameterIndex;
        pParameterID->value = parameterID.binaryValue;
        ParameterID(*pParameterID).verify();
    }
    for (index_t globalParameterIndex(inputModeIndex + 1);
         globalParameterIndex < Program::Parameters::static_size;
         ++globalParameterIndex, ++pParameterID)
    {
        parameterID.value._.global.index = globalParameterIndex - 1;
        pParameterID->value = parameterID.binaryValue;
        ParameterID(*pParameterID).verify();
    }

    parameterID.binaryValue = 0;
    parameterID.value.type = ParameterID::ModuleChainParameter;
    for (index_t moduleChainParameterIndex(0);
         moduleChainParameterIndex < Constants::maxNumberOfModules;
         ++moduleChainParameterIndex, ++pParameterID)
    {
        parameterID.value._.moduleChain.moduleIndex = moduleChainParameterIndex;
        pParameterID->value = parameterID.binaryValue;
        ParameterID(*pParameterID).verify();
    }

    parameterID.binaryValue = 0;
    parameterID.value.type = ParameterID::ModuleParameter;
    ParameterID lfoParameterID;
    lfoParameterID.value.type = ParameterID::LFOParameter;

    AutomatedModuleChain::const_iterator pModule(pProgram ? pProgram->moduleChain().begin()
                                                          : nullptr);
    index_t const numberOfModules(pProgram ? pProgram->moduleChain().size()
                                           : Constants::maxNumberOfModules);
    for (index_t moduleIndex(0); moduleIndex < numberOfModules; ++moduleIndex)
    {
        lfoParameterID.value._.lfo.moduleIndex = moduleIndex;
        parameterID.value._.module.moduleIndex = moduleIndex;

        index_t const numberOfModuleParameters(
            pProgram ? Engine::actualModule<Module>(*pModule++).numberOfParameters()
                     : Constants::maxNumberOfParametersPerModule);

        for (index_t moduleParameterIndex(0); moduleParameterIndex < numberOfModuleParameters;
             ++moduleParameterIndex)
        {
            parameterID.value._.module.moduleParameterIndex = moduleParameterIndex;
            pParameterID->value = parameterID.binaryValue;
            ParameterID(*pParameterID).verify();
            ++pParameterID;

            if (moduleParameterIndex != 0 /*Bypass*/)
            {
                using ParameterCounts::lfoExportedParameters;
                for (index_t lfoParameterIndex(0); lfoParameterIndex < lfoExportedParameters;
                     ++lfoParameterIndex, ++pParameterID)
                {
                    lfoParameterID.value._.lfo.moduleParameterIndex = moduleParameterIndex - 1;
                    lfoParameterID.value._.lfo.lfoParameterIndex = lfoParameterIndex;
                    pParameterID->value = lfoParameterID.binaryValue;
                    ParameterID(*pParameterID).verify();
                }
            }
        }
    }

    LE_ASSERT(pParameterID <= ids.end());
    LE_ASSERT(pParameterID == ids.end());
}

std::uint16_t
Plugin2HostPassiveInteropController::numberOfParameters(Program const *LE_RESTRICT const pProgram)
{
    // Must agree with getParameterIDs exactly: the host sizes the buffer it
    // passes there from what this returns.
    std::uint16_t numberOfParameters(exportedGlobalParameters + Constants::maxNumberOfModules);

    if (pProgram)
    {
        pProgram->moduleChain().forEach<Engine::ModuleParameters>(
            [&numberOfParameters](Engine::ModuleParameters const &module) {
                std::uint8_t const numberOfModuleParameters(module.numberOfParameters());
                /// \note std::uint16_t, and not the std::uint8_t this was
                /// written with. lfoExportedParameters used to be 7 in a build
                /// without the GUI against 5 with it, and the effects with the
                /// most parameters -- Armonizer and Tune Worx -- have enough
                /// that (parameters - 1) * 7 passes 255 and wraps to 256 less
                /// than the truth. numberOfParameters() then under-reports by
                /// exactly 256, and getParameterIDs, which counts the same thing
                /// in wider arithmetic, writes 256 IDs past the buffer the host
                /// sized from it.
                ///
                ///   The multiplier is unconditionally 5 now, which no effect
                /// overflows, so this is the narrow type's last line of defence
                /// rather than a live bug -- and it is why raising the count is
                /// not the local change it looks like.
                ///                               (28.07.2026.) (SW port)
                std::uint16_t const numberOfModuleLFOParameters(
                    (numberOfModuleParameters - 1 /*Bypass*/) *
                    ParameterCounts::lfoExportedParameters);
                numberOfParameters += numberOfModuleParameters + numberOfModuleLFOParameters;
            });
    }
    else
    {
        numberOfParameters +=
            Constants::maxNumberOfModules *
            (Constants::maxNumberOfParametersPerModule + ParameterCounts::lfoParametersPerModule);
    }

    return numberOfParameters;
}

namespace
{
struct UnitGetter
{
    typedef char const *result_type;
    template <class Parameter> result_type operator()() const
    {
        return LE::Parameters::DisplayValueTransformer<Parameter>::Suffix::c_str();
    }
};

AutomatedModuleChain::ModuleCPtr module(Program const *LE_RESTRICT const pProgram,
                                        std::uint8_t const moduleIndex)
{
    return pProgram ? pProgram->moduleChain().module(moduleIndex) : nullptr;
}
} // anonymous namespace

Plugin2HostPassiveInteropController::ParameterLabelGetter::result_type
Plugin2HostPassiveInteropController::ParameterLabelGetter::operator()(ParameterID::Global const id,
                                                                      Program const *) const
{
    return LE::Parameters::invokeFunctorOnIndexedParameter<GlobalParameters::Parameters>(
        id.index, UnitGetter());
}

Plugin2HostPassiveInteropController::ParameterLabelGetter::result_type
Plugin2HostPassiveInteropController::ParameterLabelGetter::operator()(
    ParameterID::Module const id, Program const *LE_RESTRICT const pProgram) const
{
    auto const pModule(module(pProgram, id.moduleIndex));
    if (pProgram && !pModule)
        return nullptr;
    auto const pUnitString(Automation::getParameterUnit(id.moduleParameterIndex, pModule.get()));
    return pUnitString;
}

Plugin2HostPassiveInteropController::ParameterLabelGetter::result_type
Plugin2HostPassiveInteropController::ParameterLabelGetter::operator()(
    ParameterID::LFO const id, Program const *LE_RESTRICT const pProgram) const
{
    using LE::Parameters::IndexOf;

    switch (id.lfoParameterIndex)
    {
    case IndexOf<LFO::Parameters, LFO::LowerBound>::value:
        break;
    case IndexOf<LFO::Parameters, LFO::UpperBound>::value:
        break;
    default:
        return nullptr;
    }

    ParameterID::Module const moduleParameterID = {
        ParameterID::Padding(),
        static_cast<std::uint8_t>(id.moduleParameterIndex + 1U /* Bypass */), id.moduleIndex};
    return (*this)(moduleParameterID, pProgram);
}

char const *Plugin2HostPassiveInteropController::ParameterValueStringGetter::operator()(
    ParameterID::Global const parameterID, Program const *LE_RESTRICT const pProgram) const
{
    return LE::Parameters::invokeFunctorOnIndexedParameter(
        pProgram->parameters(), parameterID.index,
        std::forward<Parameters::AutomatedParameterPrinter const>(printer));
}

/// \note The three `#if defined(_WIN32) LE_ASSUME( ... == Internal )` lines that
/// stood in this file are gone. They recorded that on Windows -- VST 2.4, which
/// never asks a plugin what some *other* value would read as -- the printer was
/// only ever asked about the parameter's own. CLAP does ask, on every platform,
/// so they had become false facts handed to the optimiser.
///                                           (09.08.2026.) (SW port)
char const *Plugin2HostPassiveInteropController::ParameterValueStringGetter::operator()(
    ParameterID::ModuleChain const parameterID, Program const *LE_RESTRICT const pProgram) const
{
    ModuleChainParameter::value_type parameter(noModule);
    if (!printer.forValue)
        parameter = pProgram->moduleChain().getParameterForIndex(parameterID.moduleIndex);
    else
        switch (printer.valueSource)
        {
        case Parameters::AutomatedParameterPrinter::NormalisedLinear:
            parameter = Plugins::NormalisedAutomatedParameter::convertAutomationToParameterValue<
                ModuleChainParameter>(*printer.forValue);
            break;
        case Parameters::AutomatedParameterPrinter::Linear:
            parameter = Plugins::FullRangeAutomatedParameter ::convertAutomationToParameterValue<
                ModuleChainParameter>(*printer.forValue);
            break;
        case Parameters::AutomatedParameterPrinter::Unchanged:
            parameter = Math::convert<ModuleChainParameter::value_type>(*printer.forValue);
            break;
            LE_DEFAULT_CASE_UNREACHABLE();
        }

    return (parameter != noModule) ? Effects::effectName(parameter) : emptySlot;
}

#if 0
char const * Plugin2HostPassiveInteropController::ParameterValueStringGetter::operator()( ParameterID::Module const parameterID, Program const * LE_RESTRICT const pProgram ) const
{
#if defined(_WIN32)
    LE_ASSUME( printer_.pValue_ == nullptr );
#endif // _WIN32 && ! FMOD

    auto const pModule( pProgram->moduleChain().module( parameterID.moduleIndex ) );
    if ( pModule )
        return pModule->getParameterValueString( parameterID.moduleParameterIndex, printer_ );
    return nullptr;
}
#endif // disabled

char const *Plugin2HostPassiveInteropController::ParameterValueStringGetter::operator()(
    ParameterID::LFO const parameterID, Program const *LE_RESTRICT const pProgram) const
{
    auto const pModule(pProgram->moduleChain().module(parameterID.moduleIndex));
    if (!pModule || (parameterID.moduleParameterIndex >= pModule->numberOfLFOControledParameters()))
        return nullptr;

    auto const lfoableModuleParameterIndex(parameterID.moduleParameterIndex);
    auto const moduleParameterIndex(lfoableModuleParameterIndex + 1 /* Bypass */);
    auto const lfoParameterIndex(parameterID.lfoParameterIndex);

    LFO const &lfo(pModule->lfo(lfoableModuleParameterIndex));

    using LE::Parameters::IndexOf;
    auto const lowerBoundIndex(IndexOf<LFO::Parameters, LFO::LowerBound>::value);
    auto const upperBoundIndex(IndexOf<LFO::Parameters, LFO::UpperBound>::value);
    switch (lfoParameterIndex)
    {
    default:
    {
        LE_ASSUME(lfoParameterIndex != lowerBoundIndex);
        LE_ASSUME(lfoParameterIndex != upperBoundIndex);

        return LE::Parameters::invokeFunctorOnIndexedParameter(
            lfo.parameters(), lfoParameterIndex,
            std::forward<Parameters::AutomatedParameterPrinter const>(printer));
    }

    case lowerBoundIndex:
    case upperBoundIndex:
        break;
    }

    if (!printer.forValue)
    {
        switch (lfoParameterIndex)
        {
        case lowerBoundIndex:
            printer.forValue = lfo.lowerBound();
            break;
        case upperBoundIndex:
            printer.forValue = lfo.upperBound();
            break;
            LE_DEFAULT_CASE_UNREACHABLE();
        }
    }

    /// \note Whichever side supplied it. A bound is shown in the units of the
    /// parameter it modulates -- that is what the delegation on the next line is
    /// -- and its own range is 0..1, so it is a normalised position across that
    /// parameter whether it came from the LFO or from a host asking about one.
    /// The edge the caller named was the edge of the *LFO* parameter and does
    /// not survive the change of subject. ParameterParser's LFO arm reads them
    /// back the same way round.
    printer.valueSource = Parameters::AutomatedParameterPrinter::NormalisedLinear;

    return Automation::getParameterValueString(moduleParameterIndex, printer, *pModule);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \class Plugin2HostPassiveInteropController::ParameterNameGetter
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
struct NameGetter
{
    typedef char const *result_type;
    template <class Parameter> result_type operator()() const
    {
        return LE::Parameters::Name<Parameter>::string_;
    }
}; // struct NameGetter
} // anonymous namespace

void Plugin2HostPassiveInteropController::ParameterNameGetter::operator()(
    ParameterID::Global const parameterID, Program const *) const
{
    copyToBuffer(LE::Parameters::invokeFunctorOnIndexedParameter<GlobalParameters::Parameters>(
                     parameterID.index, NameGetter()),
                 buffer_);
}

void Plugin2HostPassiveInteropController::ParameterNameGetter::operator()(
    ParameterID::ModuleChain const parameterID, Program const *) const
{
    // Implementation note:
    //   Previously, a more generic way of generating module chain parameter
    // names (based on the standard "UI elements" functionality) was used.
    // It was however, simplified to a single std::sprintf() after revision
    // 3602 as it was deemed enough.
    //                                    (22.02.2011.) (Domagoj Saric)
    LE_VERIFY(unsigned(std::snprintf(buffer_.begin(), buffer_.size(), "Module %u FX Select",
                                     parameterID.moduleIndex + 1)) < buffer_.size());
}

void Plugin2HostPassiveInteropController::ParameterNameGetter::operator()(
    ParameterID::Module const parameterID, Program const *LE_RESTRICT const pProgram) const
{
    auto const pModule(module(pProgram, parameterID.moduleIndex));
    if (pProgram &&
        (!pModule || (parameterID.moduleParameterIndex >= pModule->numberOfParameters())))
    {
        std::strcpy(&buffer_[0], notAvailable);
        return;
    }

    std::uint8_t const uiModuleIndex(parameterID.moduleIndex + 1);

    char *LE_RESTRICT pPosition(buffer_.begin());

    /// \note What is left of the buffer, which is what `copyToBuffer` below was
    /// already being handed and what these two were not -- they got a bare
    /// `pPosition` and a promise that the caller had allocated enough.
    auto const remaining([&]() { return LE::Utility::Span<char>(pPosition, buffer_.end()); });

    *pPosition++ = 'M';
    pPosition += Utility::lexical_cast(uiModuleIndex, remaining());
    *pPosition++ = ' ';

    using Module = Plugin2HostInteropControler::Module;
    using BaseParams = Effects::BaseParameters::Parameters;

    if (!pModule && (parameterID.moduleParameterIndex >= BaseParams::static_size))
    {
        *pPosition++ = 'P';
        pPosition += Utility::lexical_cast(
            Module::effectSpecificParameterIndex(parameterID.moduleParameterIndex) + 1,
            remaining());
        LE_ASSERT(pPosition <= buffer_.end());
    }
    else
    {
        char const *LE_RESTRICT const parameterName(
            (parameterID.moduleParameterIndex < BaseParams::static_size)
                ? LE::Parameters::invokeFunctorOnIndexedParameter<BaseParams>(
                      parameterID.moduleParameterIndex, NameGetter())
                : pModule
                      ->effectSpecificParameterInfo(
                          pModule->effectSpecificParameterIndex(parameterID.moduleParameterIndex))
                      .name);
        copyToBuffer(parameterName, LE::Utility::Span<char>(pPosition, buffer_.end()));
    }
}

void Plugin2HostPassiveInteropController::ParameterNameGetter::operator()(
    ParameterID::LFO const parameterID, Program const *LE_RESTRICT const pProgram) const
{
    auto const pModule(module(pProgram, parameterID.moduleIndex));
    if (pProgram && (!pModule || (parameterID.moduleParameterIndex + 1U /*Bypass*/ >=
                                  pModule->numberOfParameters())))
    {
        std::strcpy(&buffer_[0], notAvailable);
        return;
    }

    using namespace ParameterCounts;
    LE_ASSUME(parameterID.lfoParameterIndex < lfoExportedParameters);
    char const *LE_RESTRICT const lfoParameterName(
        LE::Parameters::invokeFunctorOnIndexedParameter<LFO::Parameters>(
            parameterID.lfoParameterIndex, NameGetter()));
    if (!pModule &&
        (parameterID.moduleParameterIndex + 1 >= Effects::BaseParameters::Parameters::static_size))
    {
        LE_VERIFY(
            unsigned(std::snprintf(
                buffer_.begin(), buffer_.size(), "M%u P%u - LFO %s", parameterID.moduleIndex + 1,
                parameterID.moduleParameterIndex - (lfoExportedParameters - 1) + 1,
                lfoParameterName)) < buffer_.size());
        return;
    }

    ParameterID::Module moduleParameterID;
    moduleParameterID.moduleIndex = parameterID.moduleIndex;
    moduleParameterID.moduleParameterIndex = parameterID.moduleParameterIndex + 1;
    (*this)(moduleParameterID, pProgram);

    Buffer remainingBuffer(&buffer_[std::strlen(buffer_.begin())], buffer_.end());
    remainingBuffer = Buffer(copyToBuffer(" - LFO ", remainingBuffer), remainingBuffer.end());
    LE_VERIFY(copyToBuffer(lfoParameterName, remainingBuffer) <= buffer_.end());
}

#if 0  //...mrmlj...MSVC12u5 bad codegen
template <> Plugins::ParameterID    Plugin2HostInteropControler::make<Plugins::ParameterID   >( SW::ParameterID const selector ) { return                           { selector.binaryValue }; }
template <> Plugins::ParameterIndex Plugin2HostInteropControler::make<Plugins::ParameterIndex>( SW::ParameterID const selector ) { return parameterIndexFromBinaryID( selector.binaryValue ); }
#endif // disabled

//..mrmlj...
ParameterID::ParameterID(Plugins::ParameterID const parameterID) : binaryValue(parameterID.value) {}
ParameterID::ParameterID(Plugins::ParameterIndex const parameterIndex)
    : binaryValue(parameterIDFromIndex(parameterIndex))
{
}

void ParameterID::verify() const
{
#ifndef NDEBUG
    switch (type())
    {
    case ParameterID::GlobalParameter:
    case ParameterID::ModuleChainParameter:
    case ParameterID::ModuleParameter:
    case ParameterID::LFOParameter:
        break;

        LE_DEFAULT_CASE_UNREACHABLE();
    }
#endif // NDEBUG
}

LE_NOINLINE ParameterID::BinaryValue
parameterIDFromIndex(Plugins::ParameterIndex const parameterIndex)
{
    using Parameters = GlobalParameters::Parameters;
    using namespace ParameterCounts;

    // http://stackoverflow.com/questions/5069489/performance-of-built-in-types-char-vs-short-vs-int-vs-float-vs-double
    ParameterID parameterID;

    LE_ASSERT(parameterIndex < maxNumberOfParameters);
    LE_ASSERT(maxNumberOfParameters <= std::numeric_limits<std::uint16_t>::max());
    auto index(parameterIndex.value);

    if (index < Parameters::static_size)
    {
        parameterID.value.type = ParameterID::GlobalParameter;
        parameterID.value._.global.index = static_cast<std::uint8_t>(index);
    }
    else if ((index -= Parameters::static_size) < Constants::maxNumberOfModules)
    {
        parameterID.value.type = ParameterID::ModuleChainParameter;
        parameterID.value._.moduleChain.moduleIndex = static_cast<std::uint8_t>(index);
    }
    else if ((index -= Constants::maxNumberOfModules) < Constants::maxNumberOfModuleParameters)
    {
        parameterID.value.type = ParameterID::ModuleParameter;
        parameterID.value._.module.moduleIndex =
            static_cast<std::uint8_t>(index) / Constants::maxNumberOfParametersPerModule;
        parameterID.value._.module.moduleParameterIndex =
            static_cast<std::uint8_t>(index) % Constants::maxNumberOfParametersPerModule;
    }
    else
    {
        index -= Constants::maxNumberOfModuleParameters;

        LE_ASSUME(index < lfoParameters);

        std::uint8_t const moduleIndex(
            index /
            lfoParametersPerModule); //...mrmlj...cannot cast index to uint8_t here because of FMOD...
        std::uint8_t const parameterIndex(index % lfoParametersPerModule);
        std::uint8_t const moduleParameterIndex(parameterIndex / lfoExportedParameters);
        std::uint8_t const lfoParameterIndex(parameterIndex % lfoExportedParameters);

        parameterID.value.type = ParameterID::LFOParameter;
        parameterID.value._.lfo.moduleIndex = moduleIndex;
        parameterID.value._.lfo.moduleParameterIndex = moduleParameterIndex;
        parameterID.value._.lfo.lfoParameterIndex = lfoParameterIndex;
    }

    return parameterID.binaryValue;
} // parameterIndex2ID()

LE_NOINLINE Plugins::ParameterIndex
parameterIndexFromBinaryID(ParameterID::BinaryValue const parameterIDValue)
{
    using Parameters = GlobalParameters::Parameters;
    using namespace ParameterCounts;

    ParameterID parameterID;
    parameterID.binaryValue = parameterIDValue;

    //...mrmlj...our VST2.4 code also uses IDs internally...
    //LE_ASSERT_MSG
    //(
    //    ( parameterID.value._.global.index != LE::Parameters::IndexOf<Parameters, InputMode>::value ) ||
    //    ( parameterID.value.type           !=  ParameterID::GlobalParameter                         ),
    //    "ParameterID based protocols (AU) should not use/export the InputMode parameter."
    //);

    switch (parameterID.value.type)
    {
    case ParameterID::GlobalParameter:
        return {static_cast<Plugins::ParameterIndex::value_type>(parameterID.value._.global.index)};
    case ParameterID::ModuleChainParameter:
        return {static_cast<Plugins::ParameterIndex::value_type>(
            Parameters::static_size + parameterID.value._.moduleChain.moduleIndex)};
    case ParameterID::ModuleParameter:
        return {static_cast<Plugins::ParameterIndex::value_type>(
            Parameters::static_size + Constants::maxNumberOfModules +
            parameterID.value._.module.moduleIndex * Constants::maxNumberOfParametersPerModule +
            parameterID.value._.module.moduleParameterIndex)};
    case ParameterID::LFOParameter:
        return {static_cast<Plugins::ParameterIndex::value_type>(
            Parameters::static_size + Constants::maxNumberOfModules +
            Constants::maxNumberOfModuleParameters +
            parameterID.value._.lfo.moduleIndex * lfoParametersPerModule +
            parameterID.value._.lfo.moduleParameterIndex * lfoExportedParameters +
            parameterID.value._.lfo.lfoParameterIndex)};

        LE_DEFAULT_CASE_UNREACHABLE();
    }
}

} // namespace SW

} // namespace LE
