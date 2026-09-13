////////////////////////////////////////////////////////////////////////////////
///
/// \file programWrite.hpp
/// ----------------------
///
///   Writing a `Program` and nothing else.
///
///   `Host2PluginInteropImpl::setParameter()` writes the *engine*: it
/// reconfigures the spectral setup for a global parameter, sizes a module's
/// storage when a slot changes, and tells the host about a bound that dragged its
/// counterpart. All of that is right for the copy the audio thread runs and wrong
/// for the copy the main thread keeps, which reconfigures nothing and processes
/// nothing.
///
///   So this is the same four cases, addressed the same way -- through
/// `invokeFunctorOnIdentifiedParameter` -- against a `Program &` rather than
/// against the plugin. It is the write half of the getters in
/// `plugin2HostImpl.inl`, which already take a `Program const *`.
///
/// See doc/tech/threading_model.md §2 rule 2.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef programWrite_hpp__3D7C1A96_4E52_4B8D_A1F0_9C64B2E7D538
#define programWrite_hpp__3D7C1A96_4E52_4B8D_A1F0_9C64B2E7D538
//------------------------------------------------------------------------------
#include "core/automatedModuleChain.hpp"
#include "core/modules/automatedModule.hpp"
/// \note The definitions, not just the declarations: this header instantiates
/// `setAutomatedParameter` and `setAutomatedLFOParameter`, and a translation unit
/// that includes only the interface links against neither.
#include "core/modules/automatedModuleImpl.inl"
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/parameterID.hpp"

#include "le/parameters/parametersUtilities.hpp"
#include "le/plugins/plugin.hpp"

namespace LE::SW
{

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Accepts a module into a chain that is never going to process one.
///
/// \note No `resize()`, which is the whole of the difference from
/// `SpectrumWorxCore::ModuleInitialiser`: a main-thread `Program`'s modules carry
/// parameters and no spectral storage, because nothing ever asks them for a
/// spectrum. That is not a new state for a module to be in -- the engine's own
/// initialiser returns true without sizing anything when there is no setup to
/// size against, which is every module put in a slot before `activate()`.
///
////////////////////////////////////////////////////////////////////////////////

struct ParametersOnlyModuleInitialiser
{
    using Module = SW::Module;
    bool operator()(Module &, std::uint8_t) const { return true; }
}; // struct ParametersOnlyModuleInitialiser

////////////////////////////////////////////////////////////////////////////////
///
/// \class ProgramParameterSetter
///
/// \brief `ParameterSetter`'s four cases with the engine taken out of them.
///
////////////////////////////////////////////////////////////////////////////////

template <class Protocol> class ProgramParameterSetter
{
  public:
    using AutomatedParameter = typename Plugins::AutomatedParameterFor<Protocol>::type;
    using result_type = void;

    ProgramParameterSetter(Plugins::AutomatedParameterValue const value,
                           LE::Parameters::LFO::Timing const &lfoTiming,
                           WhileModulated const whileModulated)
        : value_(value), lfoTiming_(lfoTiming), whileModulated_(whileModulated)
    {
    }

    result_type operator()(ParameterID::Global const parameterID,
                           Program *LE_RESTRICT const pProgram) const
    {
        LE::Parameters::invokeFunctorOnIndexedParameter(
            pProgram->parameters(), parameterID.index, typename AutomatedParameter::Setter{value_});
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note No "last filled or first unfilled slot" guard, deliberately, unlike
    /// the engine's. This is only ever told what the engine has already accepted
    /// -- the guard has run once, on the copy that owns the decision -- and
    /// applying it twice would let the two disagree whenever the second reading
    /// differed from the first.
    ///
    ////////////////////////////////////////////////////////////////////////////
    result_type operator()(ParameterID::ModuleChain const parameterID,
                           Program *LE_RESTRICT const pProgram) const
    {
        auto const effectIndex(
            AutomatedParameter::template convertAutomationToParameterValue<ModuleChainParameter>(
                value_));
        /// \note The destroying overload, deliberately: this is the main thread's
        /// Program, and the main thread is where things are destroyed.
        /// \see AutomatedModuleChain::setParameter.
        pProgram->moduleChain().setParameter(parameterID.moduleIndex,
                                             static_cast<std::int8_t>(effectIndex),
                                             ParametersOnlyModuleInitialiser{});
    }

    result_type operator()(ParameterID::Module const parameterID,
                           Program *LE_RESTRICT const pProgram) const
    {
        auto const pModule(pProgram->moduleChain().moduleAs<Module>(parameterID.moduleIndex));
        if (pModule)
            pModule->setAutomatedParameter(parameterID.moduleParameterIndex, value_,
                                           AutomatedParameter::normalised, whileModulated_);
    }

    /// \note The bounds fixup a `setAutomatedLFOParameter` can make to its
    /// counterpart is dropped rather than reported: the engine's copy of this call
    /// made the same fixup to the same numbers and told the host about it, and a
    /// second notification for one edit is what a host reads as two.
    result_type operator()(ParameterID::LFO const parameterID,
                           Program *LE_RESTRICT const pProgram) const
    {
        auto const pModule(pProgram->moduleChain().moduleAs<Module>(parameterID.moduleIndex));
        if (pModule &&
            (parameterID.moduleParameterIndex < pModule->numberOfLFOControledParameters()))
            Automation::setAutomatedLFOParameter<AutomatedParameter>(
                parameterID.moduleParameterIndex, parameterID.lfoParameterIndex, value_, *pModule,
                lfoTiming_);
    }

  private:
    Plugins::AutomatedParameterValue const value_;

    /// \note The engine's bar: there is one clock per instance and this Program
    /// is the same instance's. \see issue #11.
    LE::Parameters::LFO::Timing const lfoTiming_;

    WhileModulated const whileModulated_;
}; // class ProgramParameterSetter

/// \brief Applies \p value to \p parameterID in \p program. `[main-thread]`
template <class Protocol>
void setParameterIn(Program &program, ParameterID const parameterID,
                    Plugins::AutomatedParameterValue const value,
                    LE::Parameters::LFO::Timing const &lfoTiming,
                    WhileModulated const whileModulated = WhileModulated::ignored)
{
    invokeFunctorOnIdentifiedParameter(
        parameterID, ProgramParameterSetter<Protocol>{value, lfoTiming, whileModulated}, &program);
}

} // namespace LE::SW

#endif // programWrite_hpp
