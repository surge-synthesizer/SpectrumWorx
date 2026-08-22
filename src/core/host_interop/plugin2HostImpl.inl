////////////////////////////////////////////////////////////////////////////////
///
/// plugin2HostImpl.inl
/// -------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef plugin2HostImpl_inl__AB4A42D6_2C95_4C31_B90E_55BB2A91243A
#define plugin2HostImpl_inl__AB4A42D6_2C95_4C31_B90E_55BB2A91243A
//------------------------------------------------------------------------------
#include "plugin2HostImpl.hpp"

#include "core/modules/moduleDSPAndGUI.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/parameters/parser.hpp" //...mrmlj...required only for ParameterParser...
#include "le/parameters/runtimeInformation.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/engine/moduleParameters.hpp"
#include "le/spectrumworx/effects/baseParameters.hpp" //...mrmlj...required only for getParameterProperties()...

#include "le/utility/polymorphicDowncast.hpp"
#include "le/utility/span.hpp"

#include <cstring>
#include <optional>

namespace LE::SW
{

//------------------------------------------------------------------------------
// http://forum.cockos.com/showthread.php?p=538840
// http://www.kvraudio.com/forum/viewtopic.php?t=253666
// http://www.cockos.com/reaper/sdk/vst/vst_ext.php
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
//
// Indexed parameter functors.
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
/// \class SpectrumWorxSharedImpl<>::ParameterGetter
///
////////////////////////////////////////////////////////////////////////////////

template <class AutomatedParameter> struct ParameterGetterBase
{
    typedef Plugins::AutomatedParameterValue result_type;

    result_type operator()(ParameterID::Global const parameterID,
                           Program const *LE_RESTRICT const pProgram) const
    {
        return LE::Parameters::invokeFunctorOnIndexedParameter(
            pProgram->parameters(), parameterID.index, typename AutomatedParameter::Getter());
    }

    result_type operator()(ParameterID::ModuleChain const parameterID,
                           Program const *LE_RESTRICT const pProgram) const
    {
        return AutomatedParameter::convertParameterToAutomationValue(
            pProgram->moduleChain().getParameterForIndex(parameterID.moduleIndex));
    }

    result_type operator()(ParameterID::LFO const parameterID,
                           Program const *LE_RESTRICT const pProgram) const
    {
        return (*this)(parameterID, pProgram->moduleChain().module(parameterID.moduleIndex).get());
    }

    result_type
    operator()(ParameterID::LFO const parameterID,
               Plugin2HostInteropControler::Module const *LE_RESTRICT const pModule) const
    {
        return pModule
                   ? Automation::getAutomatedLFOParameter<AutomatedParameter>(
                         parameterID.moduleParameterIndex, parameterID.lfoParameterIndex, *pModule)
                   : Automation::getDefaultAutomatedLFOParameter<AutomatedParameter>(
                         parameterID.lfoParameterIndex);
    }
}; // class ParameterGetterBase

template <class ActualModule, class AutomatedParameter>
struct ParameterGetter : ParameterGetterBase<AutomatedParameter>
{
    using Base = ParameterGetterBase<AutomatedParameter>;
    using result_type = typename Base::result_type;
    using Base::operator();

    result_type operator()(ParameterID::Module const parameterID,
                           Program const *LE_RESTRICT const pProgram) const
    {
        LE_ASSUME(parameterID.moduleParameterIndex < Constants::maxNumberOfModuleParameters);
        return (*this)(
            parameterID,
            pProgram->moduleChain().moduleAs<ActualModule>(parameterID.moduleIndex).get());
    }

    result_type operator()(ParameterID::Module const parameterID,
                           ActualModule const *LE_RESTRICT const pModule) const
    {
        // Implementation note:
        //   If chunks are not used/supported and/or the host generated UI is
        // used, the host application "manually" iterates over all parameters to
        // save/restore them so we must explicitly handle the case(s) of
        // parameters that do not actually exist (e.g. the corresponding module
        // does not currently exist or it does not have the specified
        // parameter).
        //                                    (26.06.2009.) (Domagoj Saric)
        return pModule ? pModule->getAutomatedParameter(parameterID.moduleParameterIndex,
                                                        AutomatedParameter::normalised)
                       : result_type();
    }
}; // struct ParameterGetter : ParameterGetterBase<AutomatedParameter>

////////////////////////////////////////////////////////////////////////////////
///
/// \class ParameterParser
///
/// \brief ParameterGetter backwards: the value a parameter would have to hold
/// for it to display as this text, in the same automation units ParameterGetter
/// answers in -- or nothing, when no value of it displays as that.
///
/// \note One struct where the getter is a base and a derived, because there is
/// no caller for the half of it that does not know the module type.
///
////////////////////////////////////////////////////////////////////////////////

#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.

template <class ActualModule, class AutomatedParameter> struct ParameterParser
{
    using result_type = std::optional<Plugins::AutomatedParameterValue>;

    ////////////////////////////////////////////////////////////////////////////
    /// \internal
    /// \brief The one arm that has a compile time parameter list *and* needs the
    /// protocol's own units: a global's automation value is its own value put
    /// through the protocol's range mapping, which only the parameter's type
    /// knows how to do.
    ////////////////////////////////////////////////////////////////////////////

    struct AutomationValueParser
    {
        using result_type = ParameterParser::result_type;

        template <class Parameter> result_type operator()() const
        {
            auto const value(parser.template operator()<Parameter>());
            if (!value)
                return {};
            return AutomatedParameter::template convertParameterValueToAutomationValue<Parameter>(
                Math::convert<typename Parameter::value_type>(*value));
        }

        LE::Parameters::ParameterValueParser const parser;
    }; // struct AutomationValueParser

    result_type operator()(ParameterID::Global const parameterID, Program const *) const
    {
        return LE::Parameters::invokeFunctorOnIndexedParameter<GlobalParameters::Parameters>(
            parameterID.index, AutomationValueParser{parser});
    }

    ////////////////////////////////////////////////////////////////////////////
    /// \note By title, because a title is what the slot selector displays -- see
    /// ParameterValueStringGetter's ModuleChain arm, which prints
    /// `Effects::effectName()` or `emptySlot`.
    ///
    /// \note The empty slot is answered before the lookup rather than after it,
    /// because `effectIndex()` says -1 both for "the empty slot" and for "no
    /// effect has that title" -- and those are the two opposite answers.
    ////////////////////////////////////////////////////////////////////////////

    result_type operator()(ParameterID::ModuleChain, Program const *) const
    {
        if (!parser.text)
            return {};

        std::int8_t effectIndex{AutomatedModuleChain::noModule};
        if (std::strcmp(parser.text, emptySlot) != 0)
        {
            effectIndex = Effects::effectIndex(parser.text);
            if (effectIndex == AutomatedModuleChain::noModule)
                return {};
        }

        return AutomatedParameter::template convertParameterValueToAutomationValue<
            ModuleChainParameter>(effectIndex);
    }

    result_type operator()(ParameterID::Module const parameterID,
                           Program const *LE_RESTRICT const pProgram) const
    {
        return (*this)(
            parameterID,
            pProgram->moduleChain().template moduleAs<ActualModule>(parameterID.moduleIndex).get());
    }

    result_type operator()(ParameterID::Module const parameterID,
                           ActualModule const *LE_RESTRICT const pModule) const
    {
        if (!pModule)
            return {};

        auto const value(
            Automation::parseParameterValue(parameterID.moduleParameterIndex, parser, *pModule));
        if (!value)
            return {};
        return Automation::internal2AutomatedValue(parameterID.moduleParameterIndex, *value,
                                                   AutomatedParameter::normalised, *pModule);
    }

    result_type operator()(ParameterID::LFO const parameterID,
                           Program const *LE_RESTRICT const pProgram) const
    {
        return (*this)(parameterID, pProgram->moduleChain().module(parameterID.moduleIndex).get());
    }

    result_type
    operator()(ParameterID::LFO const parameterID,
               Plugin2HostInteropControler::Module const *LE_RESTRICT const pModule) const
    {
        using LFO = LE::Parameters::LFOImpl;
        using LE::Parameters::IndexOf;

        if (!pModule ||
            (parameterID.moduleParameterIndex >= pModule->numberOfLFOControledParameters()))
            return {};

        switch (parameterID.lfoParameterIndex)
        {
        default:
            break;

            ////////////////////////////////////////////////////////////////////////
            ///
            /// \note The period is text no general-purpose parser reads: `1/8T bars`
            /// is not a number with a unit after it, and `strtof` stops at the
            /// slash. So it is read the way it was written, by whoever knows the
            /// sync mask. \see LFOImpl::parsePeriodScale() and issue #158.
            ///
            ////////////////////////////////////////////////////////////////////////

        case IndexOf<LFO::Parameters, LFO::PeriodScale>::value:
        {
            auto const periodScale(LFO::parsePeriodScale(
                parser.text, pModule->lfo(parameterID.moduleParameterIndex).syncTypes()));
            if (!periodScale)
                return {};
            return LFO::internal2AutomatedValue(parameterID.lfoParameterIndex, *periodScale,
                                                AutomatedParameter::normalised);
        }

            ////////////////////////////////////////////////////////////////////////
            /// \note The two bounds are shown in the units of the parameter they
            /// modulate rather than as the normalised numbers they are -- see
            /// ParameterValueStringGetter's LFO arm, which prints them through the
            /// module parameter. So they are read back the same way round: parse in
            /// the module parameter's units, normalise, and that is the bound.
            ////////////////////////////////////////////////////////////////////////
            ////////////////////////////////////////////////////////////////////
            ///
            /// \note The two that cross as a *choice* rather than as a quantity.
            /// What a host is handed is the choice's ordinal, and
            /// `paramsTextToValue` puts whatever comes back through
            /// `CLAPEdge::toHost` -- so this owes it the natural value and not an
            /// automation one. \see CLAPEdge::choiceCount() and issue #159.
            ///
            ////////////////////////////////////////////////////////////////////

        case IndexOf<LFO::Parameters, LFO::SyncTypes>::value:
        {
            auto const choice(LFO::parseSyncChoice(parser.text));
            if (!choice)
                return {};
            return LFO::syncTypeOfChoice(*choice);
        }

        case IndexOf<LFO::Parameters, LFO::Waveform>::value:
        {
            auto const waveform(LE::Parameters::invokeFunctorOnIndexedParameter<LFO::Parameters>(
                parameterID.lfoParameterIndex, parser));
            if (!waveform)
                return {};
            return *waveform;
        }

        case IndexOf<LFO::Parameters, LFO::LowerBound>::value:
        case IndexOf<LFO::Parameters, LFO::UpperBound>::value:
        {
            auto const moduleParameterIndex(
                static_cast<std::uint8_t>(parameterID.moduleParameterIndex + 1U /*Bypass*/));
            auto const value(
                Automation::parseParameterValue(moduleParameterIndex, parser, *pModule));
            if (!value)
                return {};
            auto const bound(
                Automation::internal2AutomatedValue(moduleParameterIndex, *value, true, *pModule));
            return LFO::internal2AutomatedValue(parameterID.lfoParameterIndex, bound,
                                                AutomatedParameter::normalised);
        }
        }

        auto const value(LE::Parameters::invokeFunctorOnIndexedParameter<LFO::Parameters>(
            parameterID.lfoParameterIndex, parser));
        if (!value)
            return {};
        return LFO::internal2AutomatedValue(parameterID.lfoParameterIndex, *value,
                                            AutomatedParameter::normalised);
    }

    LE::Parameters::ParameterValueParser const parser;
}; // struct ParameterParser

#pragma warning(pop)

////////////////////////////////////////////////////////////////////////////////
///
/// \class ParameterInfoGetter
///
////////////////////////////////////////////////////////////////////////////////

#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.

template <class Protocol> class ParameterInfoGetter : public Plugins::ParameterInformation<Protocol>
{
  public:
    using Base = Plugins::ParameterInformation<Protocol>;
    using result_type = void;

    result_type operator()(ParameterID::Global const id, Program const *)
    {
        LE::Parameters::invokeFunctorOnIndexedParameter<GlobalParameters::Parameters>(
            id.index, std::forward<Base>(*this));
    }

    result_type operator()(ParameterID::ModuleChain /*const id*/, Program const *)
    {
        Base::template operator()<ModuleChainParameter>();
        this->markAsMeta();
    }

    result_type operator()(ParameterID::Module const id, Program const *LE_RESTRICT const pProgram)
    {
        auto const pModule(pProgram ? pProgram->moduleChain().module(id.moduleIndex) : nullptr);

        auto const moduleParameterIndex(id.moduleParameterIndex);

        using namespace LE::Parameters::Traits;
        typedef LE::Parameters::LinearUnsignedInteger::Modify<
            Minimum<0>, Maximum<0>, Default<0>>::type NotAvailableParameter;

        if (pProgram && (!pModule || (moduleParameterIndex >= pModule->numberOfParameters())))
        { // Dynamic parameter list:
            Base::template operator()<NotAvailableParameter>();
            return;
        }

        typedef Effects::BaseParameters::Parameters BaseParams;

        if (moduleParameterIndex < BaseParams::static_size)
        {
            LE::Parameters::invokeFunctorOnIndexedParameter<BaseParams>(moduleParameterIndex,
                                                                        std::forward<Base>(*this));
            switch (moduleParameterIndex)
            {
                using LE::Parameters::IndexOf;
            default:
                break;
            case IndexOf<BaseParams, Effects::BaseParameters::StartFrequency>::value:
            case IndexOf<BaseParams, Effects::BaseParameters::StopFrequency>::value:
                this->markAsMeta();
            }
        }
        else
        {
            if (pModule)
            {
                this->set(pModule->parameterInfo(moduleParameterIndex));
            }
            else
            {
                typedef LE::Parameters::LinearFloat::Modify<
                    Minimum<0>, Maximum<1>, Default<0>>::type GenericModuleChainParameter;
                Base::template operator()<GenericModuleChainParameter>();
            }
        }
    }

    result_type operator()(ParameterID::LFO const id, Program const *LE_RESTRICT const pProgram)
    {
        auto const pModule(pProgram ? pProgram->moduleChain().module(id.moduleIndex) : nullptr);

        /// \note The + 1 is the Bypass parameter an LFO ID skips: an LFO
        /// parameter with moduleParameterIndex k modulates module parameter
        /// k + 1, which is how ParameterNameGetter builds the module ID it
        /// delegates to. Testing k instead would let the last module parameter's
        /// LFO report a real range while the name and the printer both say "N/A".
        if (pProgram && (!pModule || (id.moduleParameterIndex + 1U /*Bypass*/ >=
                                      pModule->numberOfParameters())))
        { // Dynamic parameter list:
            using namespace LE::Parameters::Traits;
            typedef LE::Parameters::LinearUnsignedInteger::Modify<
                Minimum<0>, Maximum<0>, Default<0>>::type NotAvailableParameter;
            Base::template operator()<NotAvailableParameter>();
        }
        else
        {
            using LFO = LE::Parameters::LFOImpl;
            using LFOParameters = LFO::Parameters;
            auto const lfoParameterIndex(id.lfoParameterIndex);
            LE::Parameters::invokeFunctorOnIndexedParameter<LFOParameters>(
                lfoParameterIndex, std::forward<Base>(*this));
            switch (lfoParameterIndex)
            {
                using LE::Parameters::IndexOf;
            default:
                break;
            case IndexOf<LFOParameters, LFO::LowerBound>::value:
            case IndexOf<LFOParameters, LFO::UpperBound>::value:
                this->markAsMeta();
            }
        }
    }

    ParameterInfoGetter(ParameterInfoGetter const &) = delete;
    ~ParameterInfoGetter() = delete;
}; // class ParameterInfoGetter

#pragma warning(pop)

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
///
/// Parameters and automation
///
////////////////////////////////////////////////////////////////////////////////

template <class Impl, class Protocol>
Plugins::AutomatedParameterValue
Plugin2HostPassiveInteropImpl<Impl, Protocol>::getParameter(ParameterID const parameterID) const
{
    return getParameter(parameterID, impl().program());
}

template <class Impl, class Protocol>
Plugins::AutomatedParameterValue
Plugin2HostPassiveInteropImpl<Impl, Protocol>::getParameter(ParameterID const parameterID,
                                                            Program const &program)
{
    return invokeFunctorOnIdentifiedParameter(
        parameterID, ParameterGetter<typename Impl::Module, AutomatedParameter>(), &program);
}

template <class Impl, class Protocol>
std::optional<Plugins::AutomatedParameterValue>
Plugin2HostPassiveInteropImpl<Impl, Protocol>::getParameterFromDisplay(
    ParameterID const parameterID, char const *const display, Program const &program) const
{
    using Parser = ParameterParser<typename Impl::Module, AutomatedParameter>;
    return invokeFunctorOnIdentifiedParameter(parameterID, Parser{{display, impl().engineSetup()}},
                                              &program);
}

template <class Impl, class Protocol>
bool Plugin2HostPassiveInteropImpl<Impl, Protocol>::getParameterRanges(
    ParameterID const parameterID, Plugins::ParameterInformation<Protocol> &parameterInfo,
    Program const *LE_RESTRICT const pProgram)
{
    parameterInfo.clear(); //...mrmlj...Audition CS5.5

    using InfoGetter = ParameterInfoGetter<Protocol>;
    invokeFunctorOnIdentifiedParameter(
        parameterID, std::forward<InfoGetter>(static_cast<InfoGetter &>(parameterInfo)), pProgram);
    return true;
}

template <class Impl, class Protocol>
bool Plugin2HostPassiveInteropImpl<Impl, Protocol>::getParameterProperties(
    ParameterID const parameterID, Plugins::ParameterInformation<Protocol> &parameterInfo,
    Program const *LE_RESTRICT const pProgram)
{
    getParameterRanges(parameterID, parameterInfo, pProgram);

    auto *const pNameBuffer(parameterInfo.nameBuffer());
    if (pNameBuffer)
    {
        getParameterName(parameterID, LE::Utility::makeSpan(*pNameBuffer), pProgram);
        parameterInfo.nameSet(); //...mrmlj...
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
///
/// Plugin2HostInteropControler virtual interface implementation
///
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// Plugin2HostInteropImpl<>::hostTryIOConfigurationChange()
// --------------------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Informs the host about changes to the number of IO channels and/or
/// buses.
///
////////////////////////////////////////////////////////////////////////////////
// Implementation notes:
//   Notifications for latency and IO mode are done with two separate calls
// because this was found to be necessary with some hosts (e.g. Ableton Live 7
// and 8) that would otherwise return true from the ioChanged() call even though
// they accepted only part of the change (e.g. the latency change but not the IO
// mode change).
//                                            (28.06.2010.) (Domagoj Saric)
//   Because frequent ioChanged() calls are known to freeze Ableton Live
// (especially version 8) we check and make the calls only if really necessary.
//                                            (20.07.2010.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////
// Related links:
// http://forums.cockos.com/showthread.php?p=308093
// http://forum.cockos.com/showthread.php?t=36483
// http://forum.cockos.com/showthread.php?t=27875
// http://www.koders.com/delphi/fid5A86C7191CBBB828E612DF16911D8577D1ED6FD1.aspx?s=delphi
// http://www.koders.com/delphi/fidDB7E639EEB1D59388C5CD56BAAF28B40A93CCE45.aspx?s=delphi
// http://forum.ableton.com/viewtopic.php?t=26856&postdays=0&postorder=asc&start=60
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// Plugin2HostInteropImpl<>::latencyChanged()
// ------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Informs the host about changes to the plugin latency.
///
////////////////////////////////////////////////////////////////////////////////

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4389) // Signed/unsigned mismatch.
#endif                          // _MSC_VER

template <class Impl, class Protocol, class Base>
bool Plugin2HostActiveInteropImpl<Impl, Protocol, Base>::latencyChanged()
{
    /// \note Up to SVN revision 8346 this function tried to perform this
    /// notification asynchronously (posting it to the GUI thread) in order to
    /// workaround for potential deadlocks in some hosts when they get called
    /// back here immediately during automation (for example of the FFTSize
    /// parameter). This however created a new problem in VST 2.4 where the same
    /// callback ("ioChanged") is used both for latency and channel
    /// configuration changes - the asynchronous latency notification could
    /// happen right in the middle of an attempt to change the IO mode. For this
    /// reason this notification is now performed synchronously hoping that
    /// deadlock-problematic hosts have or will be fixed.
    ///                                       (04.10.2013.) (Domagoj Saric)
    ///
    /// \note And there was an assertion here, under _WIN32, checking that the
    /// host's input and output counts still matched the engine's -- that a
    /// latency notification had not landed in the middle of an IO mode change.
    /// Its own message names the reason it existed: "which cannot be separated in
    /// VST 2.4", the format where one callback carried both. CLAP has separate
    /// extensions for latency and for audio ports, so the situation it guarded
    /// against cannot arise, and getNumInputs()/getNumOutputs() went with the VST
    /// 2.4 host proxy that had them.
    ///
    ///   Windows-only, so nothing has compiled it since the port began.
    auto const newLatency(impl().engineSetup().latencyInSamples());
    return impl().host().reportNewLatencyInSamples(newLatency);
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

template <class Impl, class Protocol, class Base>
void Plugin2HostActiveInteropImpl<Impl, Protocol, Base>::moduleChanged(
    std::uint8_t const moduleIndex,
    Plugin2HostInteropControler::Module const *LE_RESTRICT const pModuleBase) const
{
    LE_ASSERT_MSG(!parameterListChanged(),
                  "Should not get here if host supports parameter list changes");

    using AutomatedParameterValue = typename AutomatedParameter::value_type;

    ParameterID fullModuleParameterID;
    fullModuleParameterID.value.type = ParameterID::ModuleParameter;
    fullModuleParameterID.value._.module.moduleIndex = moduleIndex;
    ParameterID::Module &moduleParameterID(fullModuleParameterID.value._.module);

    ParameterID fullLFOParameterID;
    fullLFOParameterID.value.type = ParameterID::LFOParameter;
    fullLFOParameterID.value._.lfo.moduleIndex = moduleIndex;
    ParameterID::LFO &lfoParameterID(fullLFOParameterID.value._.lfo);

    LE_ASSERT(impl().program().moduleChain().module(moduleIndex).get() == pModuleBase);

    typedef typename Impl::Module Module;
    auto const pModule(LE::Utility::polymorphicDowncast<Module const *>(pModuleBase));

    ParameterGetter<Module, AutomatedParameter> /*const*/ getParameter; //...mrmlj...

    auto const &host(impl().host());

    moduleParameterID.moduleParameterIndex = 0;
    while (moduleParameterID.moduleParameterIndex != Constants::maxNumberOfParametersPerModule)
    {
        typedef typename Impl::ParameterSelector ParameterSelector;

        AutomatedParameterValue const moduleParameterValue(
            getParameter(moduleParameterID, pModule));
        host.automatedParameterChanged(
            Base::template make<ParameterSelector>(fullModuleParameterID), moduleParameterValue);

        if (moduleParameterID.moduleParameterIndex != 0 /*Bypass*/)
        {
            lfoParameterID.moduleParameterIndex = moduleParameterID.moduleParameterIndex - 1;
            lfoParameterID.lfoParameterIndex = 0;
            while (lfoParameterID.lfoParameterIndex != ParameterCounts::lfoExportedParameters)
            {
                AutomatedParameterValue const lfoParameterValue(
                    getParameter(lfoParameterID, pModule));
                host.automatedParameterChanged(
                    Base::template make<ParameterSelector>(fullLFOParameterID), lfoParameterValue);
                ++lfoParameterID.lfoParameterIndex;
            }
        }

        ++moduleParameterID.moduleParameterIndex;
    }
}

template <class Impl, class Protocol, class Base>
void Plugin2HostActiveInteropImpl<Impl, Protocol, Base>::automatedParameterChanged(
    ParameterID const parameterID, ParameterValueForAutomation const value) const
{
    bool const normalised(AutomatedParameter::normalised);
    automatedParameterChanged(parameterID, normalised ? value.normalised : value.fullRange);
}

} // namespace LE::SW

#endif // plugin2HostImpl_inl
