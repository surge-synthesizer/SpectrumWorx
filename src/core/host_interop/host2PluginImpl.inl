////////////////////////////////////////////////////////////////////////////////
///
/// host2PluginImpl.inl
/// -------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef host2PluginImpl_inl__9BFA5CD4_B26C_4469_8F03_0D71BD78D43A
#define host2PluginImpl_inl__9BFA5CD4_B26C_4469_8F03_0D71BD78D43A
//------------------------------------------------------------------------------
#include "host2PluginImpl.hpp"

#include "plugin2Host.hpp"

#include "core/modules/automatedModuleImpl.inl"

#include "le/parameters/conversion.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/plugins/plugin.hpp"
#include "le/utility/platformSpecifics.hpp"
#include "le/utility/cstdint.hpp"

#include "le/utility/assert.hpp"
#include "le/utility/intrusivePtr.hpp"

#ifdef _DEBUG
#include <cstdio>
#endif // _DEBUG
#include <cstring>

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
/// \class SpectrumWorxSharedImpl<>::ParameterSetter
///
////////////////////////////////////////////////////////////////////////////////

/// \note Nothing here touches the interface: this runs on whichever thread the
/// parameter arrived on, which for a host automation event is the audio thread.
/// The plugin raises `ToUI::ChainChanged` and the editor recomputes its rack on
/// the main thread. \see doc/tech/threading_model.md §5.

class ModuleChainParameter;

#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.

template <class Impl, class Protocol> class Host2PluginInteropImpl<Impl, Protocol>::ParameterSetter
{
  private:
    /// \todo Consider solving this with a wrapper, like it was done for
    /// AutomatedModuleChain parameters.
    ///                                       (07.07.2010.) (Domagoj Saric)
    struct GlobalParameterSetter
    {
        typedef ErrorCode result_type;

        template <class Parameter> result_type operator()() const
        {
            bool const success(Impl::template setGlobalParameter<Parameter>(
                effect_,
                AutomatedParameter::template convertAutomationToParameterValue<Parameter>(value_)));
            return success ? Plugins::ErrorCode<Protocol>::Success
                           : Plugins::ErrorCode<Protocol>::OutOfMemory;
        }

        Impl &effect_;
        Plugins::AutomatedParameterValue const value_;
    }; // struct GlobalParameterSetter

  public:
    using result_type = ErrorCode;
    ////////////////////////////////////////////////////////////////////////////
    result_type operator()(ParameterID::Global const parameterID,
                           Impl *LE_RESTRICT const pEffect) const
    {
        return LE::Parameters::invokeFunctorOnIndexedParameter<GlobalParameters::Parameters>(
            parameterID.index, GlobalParameterSetter{*pEffect, value_});
    }
    ////////////////////////////////////////////////////////////////////////////
    result_type operator()(ParameterID::ModuleChain const parameterID,
                           Impl *LE_RESTRICT const pImpl) const
    {
        // Implementation note:
        //   Unconditional setting of module chain parameters through indices
        // would allow for creation of a non-contiguous module rack (with empty
        // slots between modules), this is something we do not want so here we
        // explicitly allow changes only for the last filled or the first
        // unfilled slot.
        //                                    (15.03.2010.) (Domagoj Saric)

        //...mrmlj...consider issuing group begin/end gesture notifications
        //...mrmlj...here (as well as for presets)...
        //...https://developer.apple.com/library/mac/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/TheAudioUnitView/TheAudioUnitView.html
        //...https://developer.apple.com/library/mac/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/AudioUnitDevelopmentFundamentals/AudioUnitDevelopmentFundamentals.html
        //...http://web.archive.org/web/20111010140734/http://developer.apple.com/library/mac/#/web/20111011163210/http://developer.apple.com/library/mac/samplecode/FilterDemo/Listings/Source_CocoaUI_AppleDemoFilter_UIView_m.html
        //...http://www.juce.com/forum/topic/au-support-begin-and-end-gesture
        //...https://groups.google.com/forum/#!msg/coreaudio-api/2YMCPgKmcmw/7uiKqRU1SdEJ
        //...http://web.archiveorange.com/archive/v/q7bubMk8ylQlGENSdNUr
        //...http://osdir.com/ml/coreaudio-api/2010-06/msg00209.html
        //...https://developer.apple.com/library/mac/technotes/tn2104/_index.html

        std::int8_t const noModule(-1);
        auto const moduleIndex(parameterID.moduleIndex);
        auto const effectIndex(
            AutomatedParameter::template convertAutomationToParameterValue<ModuleChainParameter>(
                value_));
        auto &moduleChain(pImpl->moduleChain());
        auto const chainLength(moduleChain.size());
        bool const addModule(effectIndex != noModule);
        bool const targetSlotFull(moduleIndex < chainLength);
        bool const previousSlotFull(moduleIndex - 1 < chainLength);
        bool const nextSlotEmpty(moduleIndex + 1 == chainLength);
        if ((addModule == targetSlotFull) || (addModule && previousSlotFull) ||
            (!addModule && nextSlotEmpty))
        {
            /// \note This allocates, and it may be running inside process() --
            /// a host writing a slot selector is a parameter event like any
            /// other. The granted concession, and the only one left: every
            /// other route builds its module on the main thread and hands the
            /// engine a pointer. See issue #9.
            typename Impl::Module *pDisplaced(nullptr);
            auto const result(moduleChain.setParameter(moduleIndex, effectIndex,
                                                       pImpl->moduleInitialiser(), &pDisplaced));

            /// \note Handed back rather than dropped. This runs inside
            /// `process()`, and letting the last reference to the module that
            /// came out expire here is a `delete` plus a `HeapSharedStorage`
            /// free on the audio thread -- which is what the retire protocol
            /// exists to prevent, and which every other route into the chain
            /// already goes through. \see AutomatedModuleChain::setParameter.
            if (pDisplaced)
                pImpl->retireModule(*pDisplaced);

            if (result.second == effectIndex)
            {
                //...mrmlj...http://lists.apple.com/archives/coreaudio-api/2005/Oct/msg00164.html
                if (pImpl->host().wantsManualDependentParameterNotifications())
                    pImpl->moduleChanged(moduleIndex, result.first.get());
                return Plugins::ErrorCode<Protocol>::Success;
            }
            return Plugins::ErrorCode<Protocol>::OutOfMemory;
        }
        else
        {
            return Plugins::ErrorCode<Protocol>::OutOfRange;
        }
    }
    ////////////////////////////////////////////////////////////////////////////
    result_type operator()(ParameterID::Module const parameterID,
                           Impl *LE_RESTRICT const pEffect) const
    {
        auto const pModule(pEffect->moduleChain().template moduleAs<typename Impl::Module>(
            parameterID.moduleIndex));
        if (pModule)
        {
            pModule->setAutomatedParameter(parameterID.moduleParameterIndex, value_,
                                           AutomatedParameter::normalised);
            return Plugins::ErrorCode<Protocol>::Success;
        }
        return Plugins::ErrorCode<Protocol>::OutOfRange;
    }
    ////////////////////////////////////////////////////////////////////////////
    result_type operator()(ParameterID::LFO const parameterID,
                           Impl *LE_RESTRICT const pEffect) const
    {
        auto const pModule(pEffect->moduleChain().template moduleAs<typename Impl::Module>(
            parameterID.moduleIndex));
        if (pModule &&
            (parameterID.moduleParameterIndex < pModule->numberOfLFOControledParameters()))
        {
            // Implementation note:
            //   Updating a bounds parameter can cause an implicit update/fixup
            // to its counterpart parameter so we notify the host about the
            // possible change.
            //                                (04.07.2011.) (Domagoj Saric)
            auto const implicitlyUpdatedParameter(
                Automation::template setAutomatedLFOParameter<AutomatedParameter>(
                    parameterID.moduleParameterIndex, parameterID.lfoParameterIndex, value_,
                    *pModule));
            if (pEffect->host().wantsManualDependentParameterNotifications() &&
                implicitlyUpdatedParameter)
            {
                ParameterID::LFO const implicitlyUpdatedLFOParameterID = {
                    implicitlyUpdatedParameter->first, parameterID.moduleParameterIndex,
                    parameterID.moduleIndex};
                ParameterID implicitlyUpdatedParameterID;
                implicitlyUpdatedParameterID.value.type = ParameterID::LFOParameter;
                implicitlyUpdatedParameterID.value._.lfo = implicitlyUpdatedLFOParameterID;
                auto const parameterSelector(
                    Plugin2HostInteropControler::make<typename Impl::ParameterSelector>(
                        implicitlyUpdatedParameterID));
                pEffect->host().automatedParameterChanged(parameterSelector,
                                                          implicitlyUpdatedParameter->second);
            }
            return Plugins::ErrorCode<Protocol>::Success;
        }
        return Plugins::ErrorCode<Protocol>::OutOfRange;
    }
    ////////////////////////////////////////////////////////////////////////////
    Plugins::AutomatedParameterValue const value_;
}; // ParameterSetter

#pragma warning(pop)

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
///
/// Parameters and automation
///
////////////////////////////////////////////////////////////////////////////////

template <class Impl, class Protocol>
typename Plugins::ErrorCode<Protocol>::value_type
Host2PluginInteropImpl<Impl, Protocol>::setParameter(ParameterID const parameterID,
                                                     Plugins::AutomatedParameterValue const value)
{
    LE_ASSERT(parameterIndexFromBinaryID(parameterID.binaryValue) <
              ParameterCounts::maxNumberOfParameters);

    // VST threading issues discussion:
    // http://forum.cockos.com/showthread.php?t=60633
    if (impl().blockAutomation())
    {
        return Plugins::ErrorCode<Protocol>::CannotDoInCurrentContext;
    }

    // Implementation note:
    //   Ableton Live (7, 8 & 9) sometimes calls setParameter() after being
    // notified about a parameter change. This is completely redundant/wrong and
    // and the plugin has to be able to handle this. Otherwise an
    // if ( value == getParameter( parameterID ) return; like check has to be
    // inserted here in order to ignore the redundant call.
    // http://www.kvraudio.com/forum/viewtopic.php?t=230479
    //                                        (07.07.2010.) (Domagoj Saric)

    // whether this is an *edit* is the caller's question, not this one's: a
    // host's parameter event is one, and drainCommands() applying what the
    // interface queued is not -- for a preset load, not at all. Marking here
    // called every parameter a load carried an edit
    ParameterSetter const setter = {value};
    return invokeFunctorOnIdentifiedParameter(parameterID,
                                              std::forward<ParameterSetter const>(setter), &impl());
}

} // namespace LE::SW

#endif // host2PluginImpl_inl
