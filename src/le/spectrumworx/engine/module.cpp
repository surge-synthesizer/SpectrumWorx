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
#include "module.hpp"

#include "moduleNode.hpp" //...mrmlj...for lack of moduleNode.cpp

#include "channelData.hpp"
#include "le/math/conversion.hpp"
#include "le/math/math.hpp"
#include "le/math/vector.hpp"
#include "le/spectrumworx/engine/setup.hpp"
#include "le/utility/platformSpecifics.hpp"

namespace LE::SW::Engine
{

ModuleDSP::~ModuleDSP() {}

void ModuleDSP::preProcess(LFO::Timer const &timer, Setup const &engineSetup,
                           MIDINoteStatus const &midiNotes)
{
    if (bypass())
        return;
    ModuleParameters::updateBaseParametersFromLFOs(timer);
    ModuleParameters::updateEffectParametersFromLFOs(timer);
    setup(engineSetup, midiNotes);
}

void ModuleDSP::seedRandomState(Math::Rng &source)
{
    /// \note The LFOs first and then the channels, in that order, every time --
    /// the walk *is* the identity, so it has to be the same walk on every reset
    /// or the same patch would seed differently on two runs of the same session.
    auto const lfoRange(lfos());
    for (auto &lfo : lfoRange)
        lfo.seed(source.next());

    doSeedChannelStates(source);
}

void ModuleDSP::setup(Setup const &engineSetup, MIDINoteStatus const &midiNotes)
{
    using namespace Effects::BaseParameters;

    float const leftFrequency(baseParameters().get<StartFrequency>());
    float const rightFrequency(baseParameters().get<StopFrequency>());
    //...mrmlj...for now we will just fix the range until all other code (LFO) ensures a valid range...
    //LE_ASSERT( leftFrequency <= rightFrequency );
    workingRange_.setNewRange(
        engineSetup.normalisedFrequencyToBin(std::min(leftFrequency, rightFrequency)),
        engineSetup.normalisedFrequencyToBin(rightFrequency));
    doPreProcess(engineSetup, midiNotes);
}

bool ModuleDSP::allocateStorage(
    StorageFactors const &storageFactors, std::uint16_t const channelStateSize,
    std::uint32_t const channelStateRequiredStorage // HistoryBuffer requires uint32_t
)
{
    using Utility::align;

    auto const numberOfChannels(storageFactors.numberOfChannels);

    auto const baseNumberOfBytes(numberOfChannels * channelStateSize);

    auto const alignmentPadding(
        channelStateRequiredStorage ? align(baseNumberOfBytes) - baseNumberOfBytes : 0);

    auto const bufferNumberOfBytes(numberOfChannels * align(channelStateRequiredStorage));

    auto const totalBytes(baseNumberOfBytes + alignmentPadding + bufferNumberOfBytes);

    return storage_.resize(totalBytes);
}

void ModuleDSP::process(std::uint8_t const channel, ChannelData &channelData,
                        Setup const &engineSetup) const
{
    if (!bypass())
    {
        using namespace Math;
        using namespace Effects::BaseParameters;

        float const &wet(baseParameters().get<Wet>());
        float const &gain(baseParameters().get<Gain>());

        bool const blend(!is<100>(wet));
        bool const amplify(!isZero(gain));

        bool amPh2ReIm; //...mrmlj...quick-fix for blending bug with amPh2ReIm effects...

        doProcess(channel, ChannelDataProxy(channelData, *this, blend, amPh2ReIm), engineSetup);

        if (blend)
        {
            channelData.blendWithPreviousData(wet / 100, amPh2ReIm);
        }
        if (amplify)
        {
            channelData.amplifyCurrentData(dB2NormalisedLinear(gain));
        }
    }
}

ModuleDSP::ChannelDataProxy::ChannelDataProxy(ChannelData &data, ModuleDSP const &module,
                                              bool const doBlend, bool &amPh2ReIm)
    : module_(module), data_(data), amPh2ReIm_(amPh2ReIm), blendRequired_(doBlend)
{
    amPh2ReIm = false;
}

LE_NOINLINE ModuleDSP::ChannelDataProxy::operator MainSideChannelData_AmPh() const
{
    return MainSideChannelData_AmPh(data_.freshAmPhData(blendRequired_), module_.workingRange());
}

ModuleDSP::ChannelDataProxy::operator MainSideChannelData_ReIm() const
{
    return MainSideChannelData_ReIm(data_.freshReImData(blendRequired_), module_.workingRange());
}

LE_NOINLINE ModuleDSP::ChannelDataProxy::operator ChannelData_AmPh() const
{
    return ChannelData_AmPh(data_.freshAmPhData(blendRequired_).main(), module_.workingRange());
}

LE_NOINLINE ModuleDSP::ChannelDataProxy::operator ChannelData_ReIm() const
{
    return ChannelData_ReIm(data_.freshReImData(blendRequired_).main(), module_.workingRange());
}

ModuleDSP::ChannelDataProxy::operator ChannelData_AmPh2ReIm() const
{
    amPh2ReIm_ = true;
    ChannelData::AmPhReImData const bothDomainData(data_.freshAmPh2ReImData(blendRequired_));
    ChannelData_AmPh2ReIm const result = {
        MainSideChannelData_AmPh(bothDomainData.first, module_.workingRange()),
        ChannelData_ReIm(bothDomainData.second.main(), module_.workingRange())};
    LE_ASSERT(result.input.main().beginBin() == result.output.beginBin());
    LE_ASSERT(result.input.main().endBin() == result.output.endBin());
    return result;
}

ModuleDSP::ChannelDataProxy::operator ChannelData_ReIm2AmPh() const
{
    ChannelData::AmPhReImData const bothDomainData(data_.freshReIm2AmPhData(blendRequired_));
    ChannelData_ReIm2AmPh const result = {
        MainSideChannelData_ReIm(bothDomainData.second, module_.workingRange()),
        MainSideChannelData_AmPh(bothDomainData.first, module_.workingRange()),
    };
    LE_ASSERT(result.input.beginBin() == result.output.beginBin());
    LE_ASSERT(result.input.endBin() == result.output.endBin());
    return result;
}

void *ModuleDSP::getEffectParameterPtr(std::uint8_t const parameterIndex)
{
    LE_ASSUME(pParameterOffsets_[0] == 0);
    std::uint16_t const parameterOffset(parametersBaseOffset_ + pParameterOffsets_[parameterIndex]);
    return reinterpret_cast<char *>(this) + parameterOffset;
}

void const *ModuleDSP::getEffectParameterPtr(std::uint8_t const parameterIndex) const
{
    return const_cast<ModuleDSP &>(*this).getEffectParameterPtr(parameterIndex);
}

float ModuleDSP::setEffectParameter(std::uint8_t const parameterIndex, float const value,
                                    ParameterInfo const &cachedInfo)
{
    LE_ASSERT(&cachedInfo == &effectSpecificParameterInfo(parameterIndex));
    void *LE_RESTRICT const pValue(getEffectParameterPtr(parameterIndex));
    LE_ASSERT_MSG(static_cast<float>(value) >= cachedInfo.minimum, "Parameter value out of range");
    LE_ASSERT_MSG(static_cast<float>(value) <= cachedInfo.maximum, "Parameter value out of range");
    switch (cachedInfo.type)
    {
        //...mrmlj...internal TriggerParameter knowledge...
    case ParameterInfo::Trigger:
        return (*static_cast<char *>(pValue) |= Math::convert<char>(value));
    case ParameterInfo::Boolean:
        return (*static_cast<bool *>(pValue) = Math::convert<bool>(value));
    case ParameterInfo::Enumerated:
        return (*static_cast<std::uint8_t *>(pValue) = Math::convert<std::uint8_t>(value));
    case ParameterInfo::Integer:
        return (*static_cast<std::int16_t *>(pValue) = Math::convert<std::int16_t>(value));
    case ParameterInfo::FloatingPoint:
        return (*static_cast<float *>(pValue) = value);
        LE_DEFAULT_CASE_UNREACHABLE();
    }
}

void intrusive_ptr_add_ref(ModuleNode const *LE_RESTRICT const pModuleNode)
{
    LE_ASSUME(pModuleNode);
    ++pModuleNode->referenceCount_;
}

////////////////////////////////////////////////////////////////////////////////
// ModuleParameters set/getEffectParameter default implementations
////////////////////////////////////////////////////////////////////////////////

float ModuleParameters::getEffectParameter(std::uint8_t const parameterIndex) const
{
    auto &impl(static_cast<ModuleDSP const &>(*this));
    auto const &info(impl.effectSpecificParameterInfo(parameterIndex));
    void const *const pValue(impl.getEffectParameterPtr(parameterIndex));
    switch (info.type)
    {
    case ParameterInfo::Trigger:
    case ParameterInfo::Boolean:
        return Math::convert<float>(*static_cast<bool const *>(pValue));
    case ParameterInfo::Enumerated:
        return Math::convert<float>(*static_cast<std::uint8_t const *>(pValue));
    case ParameterInfo::Integer:
        return Math::convert<float>(*static_cast<std::int16_t const *>(pValue));
    case ParameterInfo::FloatingPoint:
        return *static_cast<float const *>(pValue);
        LE_DEFAULT_CASE_UNREACHABLE();
    }
}

float ModuleParameters::setEffectParameter(std::uint8_t const parameterIndex, float const value)
{
    auto const setValue(setEffectParameterLive(parameterIndex, value));
    /// \note Moved down from `SW::Module::setEffectParameter`, which was an
    /// override that existed to push the value into a widget and carried this on
    /// the way past. It is a statement about the engine, so it belongs here.
    LE_ASSERT((value == setValue) ||
              (effectSpecificParameterInfo(parameterIndex).type != ParameterInfo::FloatingPoint));
    /// \note What the *user, the host or a preset* asked for, so it is the
    /// unmodulated value as well as the live one. The LFO comes through
    /// setEffectParameterFromLFOAux(), which calls the Live form directly and
    /// leaves this alone. See ModuleParameters::unmodulatedBaseParameter().
    pUnmodulatedValues_[numberOfLFOBaseParameters + parameterIndex] = setValue;
    return setValue;
}

float ModuleParameters::setEffectParameterLive(std::uint8_t const parameterIndex, float const value)
{
    auto const &info(effectSpecificParameterInfo(parameterIndex));
    return static_cast<ModuleDSP &>(*this).setEffectParameter(parameterIndex, value, info);
}

} // namespace LE::SW::Engine
