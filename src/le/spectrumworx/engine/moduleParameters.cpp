////////////////////////////////////////////////////////////////////////////////
///
/// moduleParameters.cpp
/// --------------------
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "moduleParameters.hpp"

// \note For Detail::ParametersInformation, which parameterInfos() - defined
// below, and declared in the header for callers everywhere - returns.
#include "moduleImpl.hpp"

#include "le/parameters/conversion.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/parameters/runtimeInformation.hpp"

/// \note Unconditional, and it must be. This header carries the
/// `DisplayValueTransformer` specialisations for the base parameters, and this
/// is the only translation unit that instantiates
/// `ParametersInformation<BaseParameters>` -- so what is visible *here* is what
/// every caller of parameterInfos() sees. Start frequency and Stop frequency
/// take their " Hz" from those specialisations and nowhere else.
///
///   It stood under `#ifndef LE_NO_PRESETS`, next to presets.hpp and marked
/// "Bypass@presets", which made the two knobs' unit a function of whether
/// presets were compiled in: with the macro on they had none. Measured by
/// compiling this file both ways -- the string " Hz" is in one object and not
/// the other -- after the stage 7.0 parameter table snapshot noticed the unit
/// appear when stage 8 switched the macro off.
#include "le/spectrumworx/effects/baseParameters.hpp"

#include "le/spectrumworx/presets.hpp"
#include <optional>

namespace LE::SW::Engine
{

using LFO = Parameters::LFOImpl;

ModuleParameters::ModuleParameters(
    //std::uint8_t           const moduleSlotIndex,
    EffectMetaData const &metadata, LFOPlaceholder *const pLFOStorage,
    float *const pUnmodulatedValues)
    : //moduleSlotIndex_( moduleSlotIndex ),
      metaData_(metadata), pLFOs_(reinterpret_cast<LFO *>(pLFOStorage)),
      pUnmodulatedValues_(pUnmodulatedValues)
{
    for (auto &lfoPlaceholder : lfos())
    {
        new (&lfoPlaceholder) LFO;
    }

    /// \note Zeroed rather than left as whatever the storage held. The real
    /// values arrive from captureUnmodulatedValues(), which the most derived
    /// constructor calls once the effect's own parameters exist -- but a module
    /// asked for one in between must not read uninitialised memory.
    std::fill_n(pUnmodulatedValues_, numberOfLFOControledParameters(), 0.0f);
}

bool ModuleParameters::bypass() const
{
    return baseParameters().get<Effects::BaseParameters::Bypass>();
}

namespace
{
struct ValueGetter
{
    typedef float result_type;
    template <class Parameter> result_type operator()(Parameter const &parameter) const
    {
        return Math::convert<float>(parameter.getValue());
    }
}; // struct ValueGetter
} // namespace
float ModuleParameters::getBaseParameter(std::uint8_t const baseParameterIndex) const
{
    return LE::Parameters::invokeFunctorOnIndexedParameter(baseParameters(), baseParameterIndex,
                                                           ValueGetter());
}

namespace
{
struct ValueSetter
{
    ValueSetter(float const value) : value_(value) {}
    typedef float result_type;
    template <class Parameter> result_type operator()(Parameter &parameter) const
    {
        static_assert(
            !std::is_same<typename Parameter::Tag, LE::Parameters::PowerOfTwoParameterTag>::value,
            "Automation-to-parameter-value conversion using Plugins::AutomatedParameter::Info is "
            "correct only for linear parameters." //...mrmlj...
        );
        parameter.setValue(Math::convert<typename Parameter::value_type>(value_));
        return Math::convert<float>(parameter.getValue());
    }
    float const value_;
}; // struct ValueSetter
} // anonymous namespace
float ModuleParameters::setBaseParameterLive(std::uint8_t const baseParameterIndex,
                                             float const parameterValue)
{
    return LE::Parameters::invokeFunctorOnIndexedParameter(baseParameters(), baseParameterIndex,
                                                           ValueSetter(parameterValue));
}

float ModuleParameters::setBaseParameter(std::uint8_t const baseParameterIndex,
                                         float const parameterValue)
{
    auto const setValue(setBaseParameterLive(baseParameterIndex, parameterValue));
    /// \note What the *user, the host or a preset* asked for, so it is the
    /// unmodulated value as well as the live one. The LFO comes through
    /// setBaseParameterFromLFOAux() instead, which writes only the live one.
    /// Bypass has no LFO and therefore no unmodulated slot.
    if (baseParameterIndex >= numberOfNonLFOBaseParameters)
        pUnmodulatedValues_[baseParameterIndex - numberOfNonLFOBaseParameters] = setValue;
    return setValue;
}

float ModuleParameters::unmodulatedBaseParameter(std::uint8_t const baseParameterIndex) const
{
    if (baseParameterIndex < numberOfNonLFOBaseParameters)
        return getBaseParameter(baseParameterIndex); // Bypass: never modulated
    return pUnmodulatedValues_[baseParameterIndex - numberOfNonLFOBaseParameters];
}

float ModuleParameters::unmodulatedEffectParameter(std::uint8_t const effectParameterIndex) const
{
    return pUnmodulatedValues_[numberOfLFOBaseParameters + effectParameterIndex];
}

float ModuleParameters::unmodulatedParameter(std::uint8_t const lfoableParameterIndex) const
{
    return pUnmodulatedValues_[lfoableParameterIndex];
}

void ModuleParameters::restoreUnmodulatedParameter(std::uint8_t const lfoableParameterIndex)
{
    auto const value(unmodulatedParameter(lfoableParameterIndex));
    // the Live setters, as the LFO's own writes are: the unmodulated value is
    // the one being restored *from* and nothing here changes it
    if (lfoableParameterIndex < numberOfLFOBaseParameters)
        setBaseParameterLive(
            static_cast<std::uint8_t>(lfoableParameterIndex + numberOfNonLFOBaseParameters), value);
    else
        setEffectParameterLive(
            static_cast<std::uint8_t>(lfoableParameterIndex - numberOfLFOBaseParameters), value);
}

void ModuleParameters::captureUnmodulatedValues()
{
    for (std::uint8_t index(numberOfNonLFOBaseParameters); index < numberOfBaseParameters; ++index)
        pUnmodulatedValues_[index - numberOfNonLFOBaseParameters] = getBaseParameter(index);

    auto const effectParameters(numberOfEffectSpecificParameters());
    for (std::uint8_t index(0); index < effectParameters; ++index)
        pUnmodulatedValues_[numberOfLFOBaseParameters + index] = getEffectParameter(index);
}

/// \note The shared base parameters' static descriptions. One definition, in
/// the one translation unit that can see the template that builds them; every
/// other caller has the declaration in the header and a call here.
ModuleParameters::ParameterInfos const &ModuleParameters::parameterInfos()
{
    return Detail::ParametersInformation<ModuleParameters::BaseParameters>::data;
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wassume"
#endif // __clang__
ParameterInfo const &ModuleParameters::parameterInfo(std::uint8_t const parameterIndex) const
{
    LE_ASSUME(parameterIndex < numberOfParameters());

    ParameterInfo const *LE_RESTRICT pParameterInfos;
    std::uint8_t index(parameterIndex);
    if (parameterIndex < BaseParameters::static_size)
    {
        pParameterInfos = &parameterInfos()[0];
    }
    else
    {
        pParameterInfos = metaData_.pParameterInfos;
        index = effectSpecificParameterIndex(index);
        LE_ASSUME(index < numberOfEffectSpecificParameters());
    }
    return pParameterInfos[index];
}

ParameterInfo const &
ModuleParameters::effectSpecificParameterInfo(std::uint8_t const parameterIndex) const
{
    LE_ASSUME(parameterIndex < numberOfEffectSpecificParameters());
    return metaData_.pParameterInfos[parameterIndex];
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif // __clang__

std::uint8_t ModuleParameters::effectSpecificParameterIndex(std::uint8_t const parameterIndex)
{
    LE_ASSUME(parameterIndex >= numberOfBaseParameters);
    return parameterIndex - numberOfBaseParameters;
}

void ModuleParameters::updateLFOs(LFO::Timer::TimingInformationChange const timingInformationChange)
{
    LE_ASSERT_MSG(timingInformationChange.timingInfoChanged(), "No need to call this.");

    for (auto &lfo : lfos())
        lfo.updateForNewTimingInformation(timingInformationChange);
}

LFO &ModuleParameters::lfo(std::uint8_t const lfoableParameterIndex)
{
    return lfos()[lfoableParameterIndex];
}
LFO const &ModuleParameters::lfo(std::uint8_t const lfoableParameterIndex) const
{
    return const_cast<ModuleParameters &>(*this).lfo(lfoableParameterIndex);
}

ModuleParameters::LFOs ModuleParameters::lfos() const
{
    return LFOs(pLFOs_, pLFOs_ + numberOfLFOControledParameters());
}

void ModuleParameters::updateBaseParametersFromLFOs(LFO::Timer const &timer)
{
    for (std::uint8_t baseParameter(numberOfNonLFOBaseParameters);
         baseParameter < numberOfBaseParameters; ++baseParameter)
    {
        LFO const &parameterLFO(baseLFO(baseParameter - numberOfNonLFOBaseParameters));
        if (parameterLFO.enabled())
        {
            auto const lfoValue(parameterLFO.getValue(timer));
            setBaseParameterFromLFOAux(baseParameter, lfoValue);
        }
    }
}

/// \note Up to SVN revision 8952 many more parameter related operations were
/// lowered to the individual/specific ModuleImpl<> instantiations. This, for
/// example, enabled both the parameters and their corresponding widgets to be
/// updated from the same place/the same loop iteration with optimal value range
/// and type conversion paths (e.g. whether the widget value should be updated
/// from the LFO value or the parameter value).
/// OTOH, such an approach coupled the DSP and GUI domains too much making it
/// difficult to effectively separate them which became a requirement for FMOD
/// Studio support.
/// For the above reason the design was refactored to separate the access to
/// parameters from the access to their corresponding widgets/GUI elements. This
/// approach has both pros and cons:
/// pros:
///  + better separation of DSP and GUI domains
///  + shorter compilation times
///  + smaller codegen size (more functionality can be moved up into the
///    non-template base classes)
/// cons:
///  - slower (e.g. generic value conversion without compile-time range
///    information, many more virtual function calls...).
///                                           (07.02.2014.) (Domagoj Saric)
void ModuleParameters::updateEffectParametersFromLFOs(LFO::Timer const &timer)
{
    auto const numberOfEffectSpecificParameters(this->numberOfEffectSpecificParameters());
    for (std::uint8_t effectParameter(0); effectParameter < numberOfEffectSpecificParameters;
         ++effectParameter)
    {
        LFO const &parameterLFO(effectLFO(effectParameter));
        if (parameterLFO.enabled())
        {
            auto const lfoValue(parameterLFO.getValue(timer));
            setEffectParameterFromLFOAux(effectParameter, lfoValue);
        }
    }
}

parameter_value_t ModuleParameters::setBaseParameterFromLFOAux(std::uint8_t const parameterIndex,
                                                               LFO::value_type const lfoValue)
{
    static_assert(LFO::minimumValue == 0 && LFO::maximumValue == 1,
                  "LFO::value_type not normalised.");
    auto const parameterValue(
        normalisedToParameterValue(lfoValue, parameterInfos()[parameterIndex]));
    /// \note Live, not `setBaseParameter`. An LFO modulates; it does not decide
    /// what the parameter *is*. Before the split it wrote through the same setter
    /// a user does, so `paramsValue` polled the sweep and saving a preset froze
    /// the LFO's instantaneous output into the file.
    return setBaseParameterLive(parameterIndex, parameterValue);
}

parameter_value_t ModuleParameters::setEffectParameterFromLFOAux(std::uint8_t const parameterIndex,
                                                                 LFO::value_type const lfoValue)
{
    static_assert(LFO::minimumValue == 0 && LFO::maximumValue == 1,
                  "LFO::value_type not normalised.");
    auto const &info(effectSpecificParameterInfo(parameterIndex));
    auto const parameterValue(normalisedToParameterValue(lfoValue, info));
    /// \note Live; see setBaseParameterFromLFOAux() above.
    return setEffectParameterLive(parameterIndex, parameterValue);
}

LFO::value_type
ModuleParameters::normalisedToParameterValue(parameter_value_t const normalisedValue,
                                             ParameterInfo const &parameterInfo)
{
    return Math::convertLinearRange<float, float, 0, 1, 1>(normalisedValue, parameterInfo.minimum,
                                                           parameterInfo.maximum);
}
parameter_value_t ModuleParameters::parameterToNormalisedValue(LFO::value_type const parameterValue,
                                                               ParameterInfo const &parameterInfo)
{
    return Math::convertLinearRange<float, 0, 1, 1, float>(parameterValue, parameterInfo.minimum,
                                                           parameterInfo.maximum);
}

namespace
{
using LFO = Parameters::LFOImpl;
std::optional<float> getParameterValueWithoutLFO(ParametersLoader const &parameterLoader,
                                                 ParameterInfo const &parameterInfo,
                                                 LFO &parameterLFO)
{
    auto const parameterValueWithoutLFO(
        parameterLoader.getLFOParameterValue<float>(parameterInfo.streamingName, parameterLFO));
    if (parameterValueWithoutLFO && (*parameterValueWithoutLFO >= parameterInfo.minimum) &&
        (*parameterValueWithoutLFO <= parameterInfo.maximum))
        return parameterValueWithoutLFO;
    else
        return std::nullopt;
}
} // anonymous namespace
void ModuleParameters::loadPresetParameters(ParametersLoader const &parameterLoader)
{
    //LE_ASSERT_MSG
    //(
    //    parameterLoader.currentEffectName() == Effects::effectName( effectTypeIndex() ),
    //    "ParametersLoader and module out-of-sync"
    //);

    {
        auto const bypassValue(parameterLoader.getSimpleParameterValue<bool>(
            LE::Parameters::streamingName<Effects::BaseParameters::Bypass>()));
        if (bypassValue)
            setBaseParameter(
                0, *bypassValue); //...mrmlj...assumes bypass is the first parameter/@ index 0
    }

    for (std::uint8_t i(1); i < numberOfBaseParameters; ++i)
    {
        auto const parameterValueWithoutLFO(
            getParameterValueWithoutLFO(parameterLoader, parameterInfos()[i], baseLFO(i - 1)));
        if (parameterValueWithoutLFO)
            setBaseParameter(i, *parameterValueWithoutLFO);
    }

    auto const effectSpecificParameters(numberOfEffectSpecificParameters());
    for (std::uint8_t i(0); i < effectSpecificParameters; ++i)
    {
        auto const &info(effectSpecificParameterInfo(i));

        /// \note The LFO is read either way -- a rate and a shape are state like
        /// any other -- and only the parameter's own value is dropped.
        auto const parameterValueWithoutLFO(
            getParameterValueWithoutLFO(parameterLoader, info, effectLFO(i)));

        /// \note **An event is never restored**, and a file written before that
        /// rule can be carrying one: `setEffectParameter` on a trigger arms it,
        /// and the next processed block consumes it. That is a preset that
        /// freezes the session it is loaded into. \see savePresetParameters().
        if (info.type == ParameterInfo::Trigger)
            continue;

        if (parameterValueWithoutLFO)
            setEffectParameter(i, *parameterValueWithoutLFO);
    }
}

void ModuleParameters::savePresetParameters(ParametersSaver const &parameterSaver) const
{
    ParametersSaver &saver(const_cast<ParametersSaver &>(parameterSaver)); //...mrmlj...

    saver.saveParameter<bool>(LE::Parameters::streamingName<Effects::BaseParameters::Bypass>(),
                              bypass());

    /// \note The *unmodulated* value throughout. This wrote `getBaseParameter`
    /// and `getEffectParameter` -- the live ones -- so saving a preset or a
    /// session while an LFO was running stored that LFO's instantaneous output as
    /// the parameter's value, and loading the file back read it as the setting.
    /// The load side has always read the value as an unmodulated one; see
    /// getParameterValueWithoutLFO() above.
    for (std::uint8_t i(1); i < numberOfBaseParameters; ++i)
    {
        saver.saveParameter<float>(parameterInfos()[i].streamingName, unmodulatedBaseParameter(i),
                                   baseLFO(i - 1));
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note **An event always streams off.** A trigger is armed by a press and
    /// disarmed by `TriggerParameter::consumeValue()` on the audio thread's copy
    /// of the Program -- so the main thread's, which is the copy a preset and a
    /// session are written from, is never disarmed and reads armed from the
    /// first press onwards. Writing that meant a file that fired itself on load,
    /// once, every time it was opened.
    ///
    ///   It is still *written*, at rest: the format is a full list of an
    /// effect's parameters and a reader warns about a name it does not find.
    ///
    /// \note Here as well as in ParametersSaver::valueToStream(), because an
    /// effect's parameters are saved by this runtime loop while the globals go
    /// through the templated functor. The two paths need the same rule stated
    /// twice; \see LE::Parameters::isAnEvent for the other one. Issue #65.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const effectSpecificParameters(numberOfEffectSpecificParameters());
    for (std::uint8_t i(0); i < effectSpecificParameters; ++i)
    {
        auto const &info(effectSpecificParameterInfo(i));
        saver.saveParameter<float>(
            info.streamingName,
            (info.type == ParameterInfo::Trigger) ? 0.0f : unmodulatedEffectParameter(i),
            effectLFO(i));
        //...mrmlj...
        //ParameterInfo const &       info  ( effectSpecificParameterInfo( i ) );
        //LFO           const &       lfo   ( effectLFO                  ( i ) );
        //void          const * const pValue( getEffectParameter         ( i ) );
        //switch ( info.type )
        //{
        //    case ParameterInfo::Boolean      : saver.saveParameter<bool        >( info.name,  static_cast<Parameters::Boolean const *>( pValue )->getValue(), lfo ); break;
        //    case ParameterInfo::Integer      : saver.saveParameter<int         >( info.name, *static_cast<         int        const *>( pValue )            , lfo ); break;
        //    case ParameterInfo::Enumerated   : saver.saveParameter<unsigned int>( info.name, *static_cast<unsigned int        const *>( pValue )            , lfo ); break;
        //    case ParameterInfo::FloatingPoint: saver.saveParameter<float       >( info.name, *static_cast<float               const *>( pValue )            , lfo ); break;
        //    LE_DEFAULT_CASE_UNREACHABLE();
        //}
    }
}

} // namespace LE::SW::Engine
