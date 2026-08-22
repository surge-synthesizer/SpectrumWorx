////////////////////////////////////////////////////////////////////////////////
///
/// \file plugin2HostImpl.hpp
/// -------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef plugin2HostImpl_hpp__945F8003_3F61_4CE0_9870_C44C37E5AD66
#define plugin2HostImpl_hpp__945F8003_3F61_4CE0_9870_C44C37E5AD66
//------------------------------------------------------------------------------
#include "plugin2Host.hpp"

#include "core/automatedModuleChain.hpp" //...mrmlj...SW::Program...

#include "le/plugins/plugin.hpp"
#include "le/utility/cstdint.hpp"
#include "le/utility/platformSpecifics.hpp"
#include "le/utility/span.hpp"

#include <optional>

namespace LE::SW
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Plugin2HostActiveInteropImpl
///
/// \brief Main SpectrumWorx plugin implementation class. Functionality shared
/// for all protocols.
///
////////////////////////////////////////////////////////////////////////////////

#pragma warning(push)
#pragma warning(disable : 4127) // Conditional expression is constant.

template <class Impl, class Protocol>
class Plugin2HostPassiveInteropImpl : public Plugin2HostPassiveInteropController
{
  public:
    ////////////////////////////////////////////////////////////////////////////
    // Parameters
    ////////////////////////////////////////////////////////////////////////////

    using AutomatedParameter = typename Plugins::AutomatedParameterFor<Protocol>::type;

    /// \note The engine's Program, and so the audio thread's to read. \see the
    /// overload, which every `[main-thread]` caller uses.
    Plugins::AutomatedParameterValue getParameter(ParameterID) const;
    static Plugins::AutomatedParameterValue getParameter(ParameterID, Program const &);

    static bool getParameterProperties(ParameterID, Plugins::ParameterInformation<Protocol> &,
                                       Program const *);

    /// \note getParameterProperties() without the name. The name is the only
    /// part of it that formats a string; a caller that just needs the range --
    /// the audio thread denormalising a parameter event, see clapParameterEdge
    /// -- has no business building one.
    static bool getParameterRanges(ParameterID, Plugins::ParameterInformation<Protocol> &,
                                   Program const *);

#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.
    struct
        ParameterValueStringGetter //...mrmlj...aggregate initialisation...: Plugin2HostPassiveInteropController::ParameterValueStringGetter
    {
        using result_type =
            Plugin2HostPassiveInteropController::ParameterValueStringGetter::result_type;

        result_type operator()(ParameterID::Module const parameterID,
                               Program const *LE_RESTRICT const pProgram) const
        {
            auto const pModule(
                pProgram->moduleChain().moduleAs<typename Impl::Module>(parameterID.moduleIndex));
            if (pModule)
                return pModule->getParameterValueString(parameterID.moduleParameterIndex,
                                                        baseGetter.printer);
            return nullptr;
        }

        result_type operator()(ParameterID::Global const parameterID,
                               Program const *const pProgram) const
        {
            return baseGetter(parameterID, pProgram);
        }
        result_type operator()(ParameterID::ModuleChain const parameterID,
                               Program const *const pProgram) const
        {
            return baseGetter(parameterID, pProgram);
        }
        result_type operator()(ParameterID::LFO const parameterID,
                               Program const *const pProgram) const
        {
            return baseGetter(parameterID, pProgram);
        }

        Plugin2HostPassiveInteropController::ParameterValueStringGetter const baseGetter;
    }; // struct ParameterValueStringGetter
#pragma warning(pop)

    /// \note The engine's own Program, which is only the right one to read on the
    /// thread that owns it. Every `[main-thread]` caller passes its own; see the
    /// overload below and doc/tech/threading_model.md §2 rule 2.
    void getParameterDisplay(ParameterID const parameterID, LE::Utility::Span<char> const text,
                             Plugins::AutomatedParameterValue const *LE_RESTRICT const pValue) const
    {
        getParameterDisplay(parameterID, text, pValue, impl().program());
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Which edge a supplied value sits on, by ID type.
    ///
    ///   By ID type and not by protocol: `getParameter()` does not answer in the
    /// same units for all four, and a printer told the wrong one prints the wrong
    /// number rather than failing. Read against ParameterGetter and
    /// ParameterParser, which are the same three-way agreement from the other two
    /// sides.
    ///
    ///   A module or LFO parameter's automation value *is* the engine's stored
    /// value when the protocol is not normalised -- see
    /// Automation::internal2AutomatedValue -- so it wants no conversion at all. A
    /// global's and a slot selector's went out through the protocol's range
    /// mapping and have to come back through it.
    ///
    ////////////////////////////////////////////////////////////////////////////

    static LE::Parameters::AutomatedParameterPrinter::ValueSource
    valueSourceFor(ParameterID const parameterID)
    {
        using Printer = LE::Parameters::AutomatedParameterPrinter;
        if (AutomatedParameter::normalised)
            return Printer::NormalisedLinear;
        switch (parameterID.type())
        {
        case ParameterID::ModuleParameter:
        case ParameterID::LFOParameter:
            return Printer::Unchanged;
        case ParameterID::GlobalParameter:
        case ParameterID::ModuleChainParameter:
            break;
        }
        return Printer::Linear;
    }

    void getParameterDisplay(ParameterID const parameterID, LE::Utility::Span<char> const text,
                             Plugins::AutomatedParameterValue const *LE_RESTRICT const pValue,
                             Program const &program) const
    {
        //...mrmlj...duplicated in SpectrumWorxEditorFMOD as a quick-hack around fmod ugliness
        //...mrmlj...(printing required both in the DSP and UI)...

        // http://www.juce.com/forum/topic/juce-module-automatically-handle-plugin-parameters
        ParameterValueStringGetter const getter = {
            {pValue ? std::optional<Plugins::AutomatedParameterValue>(*pValue) : std::nullopt,
             valueSourceFor(parameterID),
             {text, impl().engineSetup()}}};
        char const *const pValueString(invokeFunctorOnIdentifiedParameter(
            parameterID, std::forward<ParameterValueStringGetter const>(getter), &program));
        copyToBuffer(pValueString, text);
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief getParameterDisplay() backwards: the automation value that would
    /// display as \p display, or nothing when no value of this parameter does.
    ///
    /// \note The value, on the protocol's own edge -- the same one getParameter()
    /// answers on -- so that a caller can hand the two to the same host without
    /// knowing which parameter it is holding.
    ///
    /// \note \p program for the same reason getParameterDisplay() takes one: the
    /// answer depends on which effect the slot holds, and the two copies of that
    /// are owned by two different threads. doc/tech/threading_model.md §2 rule 2.
    ///
    ////////////////////////////////////////////////////////////////////////////

    std::optional<Plugins::AutomatedParameterValue>
    getParameterFromDisplay(ParameterID, char const *display, Program const &) const;

    /* </Parameters> */

    template <Plugins::PluginCapability pluginCapability> bool queryDynamicCapability() const
    {
        return false;
    }

  protected:
    Impl &impl() { return static_cast<Impl &>(*this); }
    Impl const &impl() const { return const_cast<Plugin2HostPassiveInteropImpl &>(*this).impl(); }
}; // class Plugin2HostPassiveInteropImpl

template <class Impl, class Protocol, class Base = Plugin2HostInteropControler>
class Plugin2HostActiveInteropImpl : public Base
{
  public:
    using AutomatedParameter = typename Plugins::AutomatedParameterFor<Protocol>::type;

  protected:
#ifdef _MSC_VER
    Plugin2HostActiveInteropImpl() {}
    template <typename ConstructionParameter>
    Plugin2HostActiveInteropImpl(ConstructionParameter const constructionParameter)
        : Base(constructionParameter)
    {
    }
#else
    using Base::Base;
#endif // _MSC_VER

    using ErrorCode = Plugins::ErrorCode<Protocol>;
    static typename ErrorCode::value_type makeErrorCode(ErrorCode const errorCode)
    {
        return errorCode;
    }
    static typename ErrorCode::value_type makeErrorCode(bool const success)
    {
        return success ? ErrorCode::Success : ErrorCode::OutOfMemory;
    }

  protected:
    void automatedParameterChanged(ParameterID const parameterID,
                                   Plugins::AutomatedParameterValue const newValue) const
    {
        impl().host().automatedParameterChanged(
            Plugin2HostInteropControler::make<typename Impl::ParameterSelector>(parameterID),
            newValue);
        impl().markCurrentProgramAsModified();
    }

  protected:
    Impl &impl() { return static_cast<Impl &>(*this); }
    Impl const &impl() const { return const_cast<Plugin2HostActiveInteropImpl &>(*this).impl(); }

  protected: // Plugin2HostInteropControler virtual overrides
    friend class Host2PluginInteropImpl<Impl, Protocol>;
    typedef Plugin2HostInteropControler::ParameterValueForAutomation ParameterValueForAutomation;
    void automatedParameterBeginEdit(ParameterID const parameterID) const override final
    {
        return impl().host().automatedParameterBeginEdit(
            Plugin2HostInteropControler::make<typename Impl::ParameterSelector>(parameterID));
    }
    void automatedParameterEndEdit(ParameterID const parameterID) const override final
    {
        return impl().host().automatedParameterEndEdit(
            Plugin2HostInteropControler::make<typename Impl::ParameterSelector>(parameterID));
    }
    void gestureBegin(char const *const description) const override final
    {
        return impl().host().gestureBegin(description);
    }
    void gestureEnd() const override final { return impl().host().gestureEnd(); }
    void automatedParameterChanged(ParameterID, ParameterValueForAutomation) const override final;
    void moduleChanged(std::uint8_t moduleIndex,
                       Plugin2HostInteropControler::Module const *) const override final;
    bool parameterListChanged() const override final
    {
        return impl().host().parameterListChanged();
    }
    void presetChangeBegin() const override final { return impl().host().presetChangeBegin(); }
    void presetChangeEnd() const override final { return impl().host().presetChangeEnd(); }
    bool latencyChanged() override final;

}; // class Plugin2HostActiveInteropImpl

#pragma warning(pop)

} // namespace LE::SW

#endif // plugin2HostImpl_hpp
