////////////////////////////////////////////////////////////////////////////////
///
/// lfoImpl.cpp
/// -----------
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
// http://en.wikipedia.org/wiki/Time_signature
// http://en.wikipedia.org/wiki/Mensural_notation
// http://www.u-he.com/zebra/manual/?page_id=15
// http://www.kvraudio.com/forum/viewtopic.php?t=280178
// http://www.kvraudio.com/forum/viewtopic.php?t=196528
// http://www.kvraudio.com/forum/viewtopic.php?t=212337
// http://www.kvraudio.com/forum/viewtopic.php?t=214140
// http://www.kvraudio.com/forum/viewtopic.php?t=257719
// http://www.kvraudio.com/forum/viewtopic.php?t=270213
// http://www.kvraudio.com/forum/viewtopic.php?t=170968
// http://web.forret.com/tools/bpm_tempo.asp
// http://testtone.com/calculators/lfo-speed-calculator
// http://mp3.deepsound.net/eng/samples_calculs.php
//------------------------------------------------------------------------------
#include "lfoImpl.hpp"

#include "le/math/constants.hpp"
#include "le/math/math.hpp"
#include "le/parameters/conversion.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/utility/countof.hpp"
#include "le/utility/parentFromMember.hpp"

#include "le/utility/assert.hpp"
#include <algorithm>
#include <ranges>

namespace LE::Parameters
{

using Enabled = LFOImpl::Enabled;
using PeriodScale = LFOImpl::PeriodScale;
using Phase = LFOImpl::Phase;
using LowerBound = LFOImpl::LowerBound;
using UpperBound = LFOImpl::UpperBound;
using SyncTypes = LFOImpl::SyncTypes;

void LFO::setEnabled(bool const value)
{
    static_cast<LFOImpl &>(*this).parameters().set<Enabled>(value);
}

void LFO::setPhase(LFOImpl::value_type const newPhase)
{
    LE_ASSERT(newPhase >= -0.5);
    LE_ASSERT(newPhase <= +0.5);
    static_cast<LFOImpl &>(*this).parameters().set<Phase>(newPhase);
}

void LFO::setWaveform(Waveform const waveform)
{
    static_cast<LFOImpl &>(*this).parameters().set<LFOImpl::Waveform>(waveform);
}

bool LFO::setLowerBound(LFOImpl::value_type const newLowerBound)
{
    static_cast<LFOImpl &>(*this).setLowerBound(newLowerBound);
    auto const upperBound(this->upperBound());
    if (upperBound < newLowerBound)
    {
        setUpperBound(newLowerBound);
        return true;
    }
    return false;
}

bool LFO::setUpperBound(LFOImpl::value_type const newUpperBound)
{
    LE_ASSERT(LFOImpl::isValueInRange(newUpperBound));
    static_cast<LFOImpl &>(*this).setUpperBound(newUpperBound);
    auto const lowerBound(this->lowerBound());
    if (lowerBound > newUpperBound)
    {
        setLowerBound(newUpperBound);
        return true;
    }
    return false;
}

std::uint16_t LFO::setPeriodInMilliseconds(std::uint16_t const periodInMilisecons)
{
    return Math::convert<std::uint16_t>(setPeriodInSeconds(periodInMilisecons / 1000.0f) * 1000.0f);
}

float LFO::setPeriodInSeconds(float const periodInSeconds)
{
    auto &impl(static_cast<LFOImpl &>(*this));

    /// \note The bar the period is a fraction *of*, which is the reference one
    /// for a free LFO and the host's for a synced one. \see LFOImpl::getValue
    bool const freeRunning(impl.syncTypes() == LFO::Free);
    auto const bar(freeRunning ? LFOImpl::Timer::referenceBarDuration
                               : LFOImpl::Timer::basePeriod());

    auto const periodScale(periodInSeconds / bar);
    auto const clampedPeriodScale(freeRunning
                                      ? impl.clampFreePeriod(periodScale)
                                      : impl.snapSyncedPeriod(periodScale, impl.syncTypes()).first);
    impl.setPeriodScale(clampedPeriodScale);
    return clampedPeriodScale * bar;
}

/// \todo These synchronization type altering functions do not automatically
/// cause period scale resnapping. Reconsider this.
///                                       (23.02.2011.) (Domagoj Saric)
void LFO::addSyncType(SyncType const syncType)
{
    auto &parameters(static_cast<LFOImpl &>(*this).parameters());
    LE_ASSERT(syncType && "No sync type specified.");
    LE_ASSERT(!hasEnabledSync(syncType) && "Sync type already enabled.");
    parameters.set<SyncTypes>(parameters.get<SyncTypes>().getValue() | syncType);
}

void LFO::removeSyncType(SyncType const syncType)
{
    auto &parameters(static_cast<LFOImpl &>(*this).parameters());
    LE_ASSERT(syncType && "No sync type specified.");
    LE_ASSERT(hasEnabledSync(syncType) && "Sync type not enabled.");
    parameters.set<SyncTypes>(parameters.get<SyncTypes>().getValue() & ~syncType);
}

bool LFO::hasEnabledSync(SyncType const syncType) const { return (syncTypes() & syncType) != 0; }

bool LFO::enabled() const
{
    return static_cast<LFOImpl const &>(*this).parameters().get<Enabled>();
}
std ::uint8_t LFO::syncTypes() const
{
    return static_cast<LFOImpl const &>(*this).parameters().get<SyncTypes>();
}
LFOImpl::value_type LFO::phase() const
{
    return static_cast<LFOImpl const &>(*this).parameters().get<Phase>();
}
LFOImpl::value_type LFO::lowerBound() const
{
    return static_cast<LFOImpl const &>(*this).parameters().get<LowerBound>();
}
LFOImpl::value_type LFO::upperBound() const
{
    return static_cast<LFOImpl const &>(*this).parameters().get<UpperBound>();
}
LFO ::Waveform LFO::waveForm() const
{
    return static_cast<LFO::Waveform>(
        static_cast<LFOImpl const &>(*this).parameters().get<LFOImpl::Waveform>().getValue());
}

LFOImpl::value_type LFOImpl::periodScale() const { return parameters().get<PeriodScale>(); }

void LFOImpl::setLowerBound(value_type const newLowerBound)
{
    LE_ASSERT(isValueInRange(newLowerBound));
    parameters().set<LowerBound>(newLowerBound);
}

void LFOImpl::setUpperBound(value_type const newUpperBound)
{
    LE_ASSERT(isValueInRange(newUpperBound));
    parameters().set<UpperBound>(newUpperBound);
}

void LFOImpl::setPeriodScale(value_type const newPeriodScale)
{
    LE_ASSERT(newPeriodScale >= LFOImpl::currentPeriodScaleMinimum());
    LE_ASSERT(newPeriodScale <= LFOImpl::currentPeriodScaleMaximum());
    parameters().set<PeriodScale>(newPeriodScale);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The **reference** measure numerator rather than the host's, so that the
/// range is a constant. It was `Timer::measureNumeratorFloat()`, which made the
/// parameter's minimum a function of the host's time signature -- and that pair
/// is what `CLAPEdge` normalises against, so the host's 0..1 meant a different
/// period in three four than in four four, and `clap_param_info.default_value`
/// moved with the meter. CLAP has no rescan flag that means "the default
/// changed", so there was no way to tell a host about it either.
///
///   The meter still decides the *grid* a synced period snaps to -- see
/// `snapSyncedPeriodScale()`, which divides by the host's numerator throughout.
/// It no longer decides what values are representable. Four four gives the wider
/// of the two, so nothing that used to be in range has left it.
///                                           (06.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

LFOImpl::value_type LFOImpl::currentPeriodScaleMinimum()
{
    return (2.0f / 3.0f /*for triplets    */) / LFOImpl::minimumPeriodAsMaximumBeatDenominator /
           LFOImpl::Timer::referenceMeasureNumerator;
}
LFOImpl::value_type LFOImpl::currentPeriodScaleMaximum()
{
    return (3.0f / 2.0f /*for dotted notes*/) * LFOImpl::maximumPeriodInNumberOfBars;
}

namespace
{
using lfo_value_t = LFOImpl::value_type;

template <int inputMinimum, int inputMaximum, typename Input>
lfo_value_t convertToLFORange(Input const inputvalue)
{
    return Math::convertLinearRange<lfo_value_t, LFOImpl::minimumValue,
                                    LFOImpl::maximumValue - LFOImpl::minimumValue, 1, Input,
                                    inputMinimum, inputMaximum - inputMinimum, 1>(inputvalue);
}

using LFOState = lfo_value_t[2];

lfo_value_t sine(lfo_value_t const position, LFOState &, bool /*newPeriodBegun*/)
{
    LE_ASSUME(position >= 0);
    LE_ASSUME(position <= 1);
    lfo_value_t const offsetSine(-std::cos(Math::Constants::twoPi * position));
    return convertToLFORange<-1, +1>(offsetSine);
}

lfo_value_t sawtooth(lfo_value_t const position, LFOState &, bool /*newPeriodBegun*/)
{
    LE_ASSUME(position >= 0);
    LE_ASSUME(position <= 1);
    return convertToLFORange<0, 1>(position);
}

lfo_value_t reverseSawtooth(lfo_value_t const position, LFOState &, bool /*newPeriodBegun*/)
{
    LE_ASSUME(position >= 0);
    LE_ASSUME(position <= 1);
    return convertToLFORange<0, 1>(1 - position);
}

lfo_value_t triangle(lfo_value_t const position, LFOState &state, bool const newPeriodBegun)
{
    LE_ASSUME(position >= 0);
    LE_ASSUME(position <= 1);
    return (position < 0.5f) ? sawtooth(position * 2.0f, state, newPeriodBegun)
                             : reverseSawtooth((position - 0.5f) * 2.0f, state, newPeriodBegun);
}

lfo_value_t square(lfo_value_t const position, LFOState &, bool /*newPeriodBegun*/)
{
    LE_ASSUME(position >= 0);
    LE_ASSUME(position <= 1);
    return position > 0.5f;
}

lfo_value_t exponent(lfo_value_t const position, LFOState &, bool /*newPeriodBegun*/)
{
    LE_ASSUME(position >= 0);
    LE_ASSUME(position <= 1);

    lfo_value_t const e(2.71828182845904523536f);

    lfo_value_t exponent;
    if (position < 0.5f)
        exponent = (position) * 2;
    else if (position > 0.5f)
        exponent = (1 - position) * 2;
    else
        exponent = 1;

    lfo_value_t const result(Math::exp(exponent));
    return Math::convertLinearRange<lfo_value_t, LFOImpl::minimumValue,
                                    LFOImpl::maximumValue - LFOImpl::minimumValue, 1, lfo_value_t>(
        result, 1.0f, e);
}

lfo_value_t randomWhacko(lfo_value_t /*position*/, LFOState &, bool /*newPeriodBegun*/)
{
    return Math::normalisedRand();
}

lfo_value_t randomHold(lfo_value_t const position, LFOState &state, bool const newPeriodBegun)
{
    if (newPeriodBegun)
    {
        state[0] = randomWhacko(position, state, newPeriodBegun);
    }
    return state[0];
}

lfo_value_t randomSlide(lfo_value_t const position, LFOState &state, bool const newPeriodBegun)
{
    LE_ASSUME(position >= 0);
    LE_ASSUME(position <= 1);
    if (newPeriodBegun)
    {
        lfo_value_t const oldTarget(state[0] + state[1]);
        lfo_value_t const newTarget(randomWhacko(position, state, newPeriodBegun));
        state[0] = newTarget - oldTarget;
        state[1] = oldTarget;
    }

    return position * state[0] + state[1];
}

lfo_value_t dirac(lfo_value_t /*position*/, LFOState &, bool const newPeriodBegun)
{
    if (newPeriodBegun)
        return static_cast<lfo_value_t>(LFOImpl::maximumValue);
    else
        return static_cast<lfo_value_t>(LFOImpl::minimumValue);
}

lfo_value_t diracUpsideDown(lfo_value_t /*position*/, LFOState &, bool const newPeriodBegun)
{
    if (newPeriodBegun)
        return static_cast<lfo_value_t>(LFOImpl::minimumValue);
    else
        return static_cast<lfo_value_t>(LFOImpl::maximumValue);
}

/// \note Was two typedefs: a `decltype( &sine )` arm working around a Clang 3.5
/// crash, and an arm that appended an MSVC-only `throw()` -- a 2005-era hint
/// that let MSVC omit unwind setup for the call.
///
///   `throw()` became a synonym for `noexcept` in C++17 and was removed from the
/// language in C++20, and none of the waveform functions is noexcept. So on MSVC
/// the table below was initialising noexcept function pointers from throwing
/// functions, which C++17 made ill-formed -- eleven errors, one per entry, saying
/// only that a reinterpret_cast would be needed.
///
///   The Clang arm expanded to this same signature, top-level parameter const
/// being no part of a function's type, so one plain typedef replaces both.
///                                       (30.07.2026.) (SW port)
typedef lfo_value_t (*GetWaveformAmplitudeForPosition)(lfo_value_t position, LFOState &,
                                                       bool newPeriodBegun);

GetWaveformAmplitudeForPosition const lfoFunctions[] = {
    &sine,       &triangle,    &sawtooth,     &reverseSawtooth, &square,         &exponent,
    &randomHold, &randomSlide, &randomWhacko, &dirac,           &diracUpsideDown};
} // anonymous namespace

LFOImpl::LFOImpl()
{
    state_[0] = minimumValue;
    state_[1] = 0;
}

LFOImpl::value_type LFOImpl::getWaveformAmplitudeForPosition(value_type const position,
                                                             bool const newPeriodBegun) const
{
    return lfoFunctions[waveForm()](position, state_, newPeriodBegun);
}

LFOImpl::value_type LFOImpl::getValue(Timer const &timer) const
{
    value_type const periodScale(this->periodScale());

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note **Which clock, and it is the whole of what SyncTypes means.** A
    /// synced period is a fraction of the host's bar, so it is read against the
    /// host's bar clock and follows the tempo. A free one is a duration, so it
    /// is read against a bar that never changes length -- see
    /// `Timer::currentTimeInReferenceBars()`, which also has the identity
    /// showing this is what the old rescale-the-parameter arrangement computed.
    ///                                       (06.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    bool const freeRunning(syncTypes() == LFO::Free);
    value_type const currentTime(freeRunning ? timer.currentTimeInReferenceBars()
                                             : timer.currentTimeInBars());
    value_type const previousTime(freeRunning ? timer.previousTimeInReferenceBars()
                                              : timer.previousTimeInBars());

    //...mrmlj...
#ifndef NDEBUG
    value_type const periodOffset(Math::abs(periodScale * phase()));
#else
    value_type const periodOffset(periodScale * phase());
#endif // _DEBUG

    value_type const currentPeriodNormalisedPosition(
        Math::splitFloat((periodOffset + currentTime) / periodScale).fractional);
    LE_ASSERT(currentPeriodNormalisedPosition >= 0);
    LE_ASSERT(currentPeriodNormalisedPosition <= 1);

    value_type const previousPeriodPosition(
        Math::PositiveFloats::modulo((periodOffset + previousTime), periodScale));
    value_type const previousTimeDifferenceToPeriodBoundary(periodScale - previousPeriodPosition);
    value_type const periodEndForPreviousTime(previousTime +
                                              previousTimeDifferenceToPeriodBoundary);
    bool const newPeriod(currentTime > periodEndForPreviousTime);

    value_type const newValue(
        getWaveformAmplitudeForPosition(currentPeriodNormalisedPosition, newPeriod));

    LE_ASSERT(isValueInRange(newValue));
    value_type const result(
        Math::convertLinearRange<value_type, value_type, LFOImpl::minimumValue,
                                 LFOImpl::maximumValue - LFOImpl::minimumValue, 1>(
            newValue, lowerBound(), upperBound()));
    LE_ASSERT(isValueInBounds(result));

    return result;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note 'Free' LFO absolute<->relative value conversion: the `T` attribute is
/// milliseconds for a free LFO and bars for a synced one, and has been since
/// 2011. That is not what changed here -- the file format is untouched. What
/// changed is the **`referenceBarDuration` rather than `Timer::basePeriod()`**:
/// the conversion used to read the process-global bar duration, so the same
/// preset loaded at 140 BPM produced a different `PeriodScale` than at 120, and
/// the same session saved at two tempi wrote two different files.
///
///   That is why `[preset-corpus]` used to turn red about one run in three when
/// the whole suite ran in one process -- 153 of the 303 rows, the ones with a
/// tempo-synced LFO -- and why the two test binaries had to be split. Nothing
/// was ever fixed there; this is the fix. A constant converts the same way from
/// any tempo, which is what "the file holds an absolute duration" was always
/// supposed to mean.
///                                       (07.01.2011.) (Domagoj Saric)
///                                       (06.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

template <>
LFOImpl::PeriodScale::value_type LFOImpl::adjustValueForPreset(PeriodScale const &periodScale) const
{
    LE_ASSERT(periodScale == this->periodScale());
    if (syncTypes() == LFO::Free)
    {
        return periodScale * LFOImpl::Timer::referenceBarDuration * 1000;
    }
    return periodScale;
}

template <>
LFOImpl::PeriodScale::value_type LFOImpl::adjustValueFromPreset<LFOImpl::PeriodScale>(
    LFOImpl::PeriodScale::value_type const periodScale) const
{
    if (syncTypes() == LFO::Free)
        return clampFreePeriod(periodScale / LFOImpl::Timer::referenceBarDuration / 1000);
    else
        return snapSyncedPeriod(periodScale, syncTypes()).first;
}

void LFOImpl::snapPeriodScaleFromAutomation(PeriodScale &periodScale)
{
    auto const &parameters(Utility::ParametersFromMember<Parameters, 1>()(periodScale));
    auto const &lfo(
        Utility::ParentFromMember<LFOImpl, Parameters, &LFOImpl::parameters_>()(parameters));

    if (lfo.syncTypes() != LFO::Free)
        periodScale = snapSyncedPeriod(periodScale, lfo.syncTypes()).first;
    else
    {
        LE_ASSERT(clampFreePeriod(periodScale) == periodScale);
    }
}

bool LFOImpl::isValueInBounds(value_type const value) const
{
    return Math::isValueInRange<value_type>(value, lowerBound(), upperBound());
}

bool LFOImpl::isValueInRange(value_type const value)
{
    return Math::isValueInRange<value_type>(value, static_cast<value_type>(minimumValue),
                                            static_cast<value_type>(maximumValue));
}

LFOImpl::value_type LFOImpl::clampFreePeriod(value_type const absolutePeriod)
{
    return Math::clamp(absolutePeriod, currentPeriodScaleMinimum(), currentPeriodScaleMaximum());
}

namespace
{
lfo_value_t snapSyncedPeriodScale(lfo_value_t const periodScale)
{
    using namespace Math;

    float const measureNumerator(LFOImpl::Timer::measureNumeratorFloat());
    std::uint8_t const numberOfBeats(round(periodScale * measureNumerator));
    if (numberOfBeats > LFOImpl::Timer::measureNumerator())
    {
        float const numberOfBeatsClampedToPowerOfTwo(
            convert<float>(PowerOfTwo::round(numberOfBeats)));

        float const lowerBound(1.0f);
        float const upperBound(LFOImpl::maximumPeriodInNumberOfBars);

        return clamp(numberOfBeatsClampedToPowerOfTwo / measureNumerator, lowerBound, upperBound);
    }
    else if (numberOfBeats > 0)
    {
        LE_ASSERT((numberOfBeats >= 1) && (numberOfBeats <= LFOImpl::Timer::measureNumerator()));

        auto const wholeDivisorFinder([](std::uint8_t const value) {
            return LFOImpl::Timer::measureNumerator() % value == 0;
        });

        auto const upperClosest(*std::ranges::find_if(
            std::views::iota(numberOfBeats, LFOImpl::Timer::measureNumerator()),
            wholeDivisorFinder));
        auto const lowerClosest(*std::ranges::find_if(
            std::views::iota(std::uint8_t(1), numberOfBeats), wholeDivisorFinder));
        auto const closest(((upperClosest - numberOfBeats) < (numberOfBeats - lowerClosest))
                               ? upperClosest
                               : lowerClosest);
        return convert<float>(closest) / measureNumerator;
    }
    else
    {
        LE_ASSERT(numberOfBeats < 1);

        float const upperBound(1 / measureNumerator);
        float const lowerBound(upperBound / LFOImpl::minimumPeriodAsMaximumBeatDenominator);

        float const clamped(clamp(periodScale, lowerBound, upperBound));

        float const scaledNumerator(LFOImpl::minimumPeriodAsMaximumBeatDenominator *
                                    measureNumerator);

        std::uint8_t const clampedToPowerOfTwo(PowerOfTwo::round(round(clamped * scaledNumerator)));

        return convert<float>(clampedToPowerOfTwo) / (scaledNumerator);
    }
}

lfo_value_t snapSyncedPeriodScale(lfo_value_t const periodScale, float const tempoScale)
{
    return snapSyncedPeriodScale(periodScale * tempoScale) / tempoScale;
}
} // anonymous namespace

LFOImpl::SnappedPeriod LFOImpl::snapSyncedPeriod(value_type const periodScale,
                                                 std::uint8_t const syncTypes)
{
    LE_ASSERT(syncTypes != Free);

    float const quarterPeriod(snapSyncedPeriodScale(periodScale, 1 / 1.0f));
    float const tripletPeriod(snapSyncedPeriodScale(periodScale, 3 / 2.0f));
    float const dottedPeriod(snapSyncedPeriodScale(periodScale, 2 / 3.0f));

    SnappedPeriod const nearestPeriods[] = {
        SnappedPeriod((syncTypes & Quarter) ? quarterPeriod : std::numeric_limits<float>::max(),
                      Quarter),
        SnappedPeriod((syncTypes & Triplet) ? tripletPeriod : std::numeric_limits<float>::max(),
                      Triplet),
        SnappedPeriod((syncTypes & Dotted) ? dottedPeriod : std::numeric_limits<float>::max(),
                      Dotted)};

    return *std::ranges::min_element(
        nearestPeriods, [=](SnappedPeriod const &left, SnappedPeriod const &right) {
            return (Math::abs(right.first - periodScale) > Math::abs(left.first - periodScale));
        });
}

LFOImpl::SnappedPeriod LFOImpl::snapPeriodScale(value_type const periodScale,
                                                std::uint8_t const syncTypes)
{
    return (syncTypes == Free) ? SnappedPeriod(clampFreePeriod(periodScale), Free)
                               : snapSyncedPeriod(periodScale, syncTypes);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note **A tempo change moves nothing here any more.** The Free arm used to
/// rescale the period by the bar-duration ratio so that the period in seconds
/// survived a tempo change -- correct in what it sounded like and wrong in what
/// it did to the number: `PeriodScale` is host-visible and automatable, so a
/// project's automation lane, and the value saved in it, moved on their own
/// whenever the tempo did. It was worth asking whether a genuine tempo change
/// should move a host-visible parameter at all; the answer is no, and the way to
/// get it is to measure a free period against a bar that does not change length
/// rather than to keep rewriting the period. See `getValue()`.
///
///   The synced arm stays, and is not the same thing: a *meter* change alters
/// which divisions of a bar exist, so a period snapped to the old grid has to be
/// resnapped to the new one. That is a quantisation, not a rescale.
///                                           (06.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void LFOImpl::updateForNewTimingInformation(
    Timer::TimingInformationChange const &timingInformationChage)
{
    if (syncTypes() == Free)
        return;

    if (timingInformationChage.measureNumeratorChanged())
        setPeriodScale(snapSyncedPeriod(periodScale(), syncTypes()).first);
}

namespace
{
#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.
struct LFOParameterGetter
{
    typedef Plugins::AutomatedParameterValue result_type;

#pragma warning(push)
#pragma warning(disable : 4127) // Conditional expression is constant.
    template <class Parameter> result_type operator()() const
    {
        LE_ASSERT_MSG(
            Parameter::isValidValue(Math::convert<typename Parameter::value_type>(value_)),
            "Invalid LFO parameter value.");
        result_type result(
            LE::Parameters::convertParameterValueToLinearValue<result_type, 0, 1, 1, Parameter>(
                value_));
        if (std::is_same<Parameter, LFOImpl::PeriodScale>::value)
            result = LFOImpl::linearisePeriodScale(result);
        return result;
    }
#pragma warning(pop)

    float const value_;
}; // class LFOParameterGetter
#pragma warning(pop)
} // namespace
Plugins::AutomatedParameterValue LFOImpl::internal2AutomatedValue(std::uint8_t const parameterIndex,
                                                                  float const internalValue,
                                                                  bool const normalised)
{
    if (!normalised)
        return internalValue;

    LFOParameterGetter const getter = {internalValue};
    return LE::Parameters::invokeFunctorOnIndexedParameter<Parameters>(parameterIndex, getter);
}

//float LFOImpl::automated2InternalValue( std::uint8_t const parameterIndex, Plugins::AutomatedParameterValue const automatedValue, bool const normalised )
//{
//    if ( !normalised )
//        return automatedValue;
//    return 0;
//}

// LFOImpl::PeriodScale "linearization" section.
/// \todo Cleanup with a new 'logarithmic' parameter/control.
///                                           (25.01.2012.) (Domagoj Saric)
namespace
{
static Plugins::AutomatedParameterValue normalisedPeriodScaleSkewFactor()
{
    unsigned int const middleValue(1);
    auto const minimum(LFOImpl::PeriodScale::minimum());
    auto const maximum(LFOImpl::PeriodScale::maximum());

    auto const normalisedMiddleValue((middleValue - minimum) / (maximum - minimum));

    Plugins::AutomatedParameterValue const skewFactor(-1 / Math::log2(normalisedMiddleValue));

    return skewFactor;
}
} // namespace

Plugins::AutomatedParameterValue LFOImpl::unlinearisePeriodScale(
    Plugins::AutomatedParameterValue const linearisedNormalisedPeriodScale)
{
    using value_type = Plugins::AutomatedParameterValue;
    value_type const normalisedSkewedAutomationValue(linearisedNormalisedPeriodScale);
    value_type const normalisedUnskewedAutomationValue(
        Math::exp(Math::ln(normalisedSkewedAutomationValue) / normalisedPeriodScaleSkewFactor()));
    LE_ASSERT(Math::isNormalisedValue(normalisedUnskewedAutomationValue));
    return normalisedUnskewedAutomationValue;
}

Plugins::AutomatedParameterValue
LFOImpl::linearisePeriodScale(Plugins::AutomatedParameterValue const nonlinearNormalisedPeriodScale)
{
    return std::pow(nonlinearNormalisedPeriodScale, normalisedPeriodScaleSkewFactor());
}

// Implementation note:
//   'Free' LFO periods are saved to presets with absolute millisecond values so
// that their value would be restored correctly independent of the current BPM.
// The conversion between an absolute period value and a period scale (used for
// synced LFOs) requires the current bar duration, to avoid requiring the active
// LFOImpl::Timer instance to be passed to preset loading/saving code a global
// variable is used. This should not create problems if the assumption that no
// host uses more than one tempo value at any given time is correct.
//                                            (07.01.2011.) (Domagoj Saric)
// Assume 120 BPM 4/4
std::atomic<LFOImpl::value_type> LFOImpl::Timer::barDuration_(LFOImpl::Timer::referenceBarDuration);
std::atomic<std::uint8_t> LFOImpl::Timer::measureNumerator_(4);

namespace
{
/// \note Relaxed throughout; see the note on the declarations.
template <typename T> T relaxed(std::atomic<T> const &value)
{
    return value.load(std::memory_order_relaxed);
}
template <typename T> void relaxed(std::atomic<T> &value, T const newValue)
{
    value.store(newValue, std::memory_order_relaxed);
}
} // anonymous namespace

LFOImpl::Timer::Timer() { reset(); }

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The timing change to report, given what this timer already knew.
///
/// \note The whole of the fix for "every LFO period moves on the first block of a
/// session that is not at 120 BPM". Both overloads below compare the incoming
/// timing against `barDuration_`/`measureNumerator_`, which start life -- and go
/// back, on reset() -- holding an *assumption*: 120 BPM in 4/4. A host that says
/// 140 is not changing the tempo, it is telling us the tempo for the first time,
/// and the difference matters because `updateForNewTimingInformation()` rescales
/// every Free LFO's period by the bar-duration ratio to keep its period constant
/// in seconds. Against a real change that is right; against an assumption it
/// silently moved a parameter the host had never written, which is what
/// clap-validator's four state cases were reporting.
///
/// \note Reported for the measure numerator as well as the bar duration, not just
/// the arm that was failing: the same sentence is true of both -- there was
/// nothing to change *from* -- and the synced arm resnaps the period on a
/// numerator change for the same reason the free arm rescales it. That half was
/// reasoned rather than measured until 10.08.2026, nothing in the suite having
/// driven a meter other than 4/4; `A host that opens in five four is stating its
/// meter rather than changing it` (`tests/parameters/lfoTests.cpp`) and its twin
/// through the plugin in `tests/clap/pluginTests.cpp` are what measure it. Both
/// go red on a revert of this line, dragging a quarter-of-a-bar period onto the
/// new meter's grid on the first block.
///                                           (03.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

LFOImpl::Timer::TimingInformationChange
LFOImpl::Timer::establishedChange(value_type const barDuration, std::uint8_t const measureNumerator)
{
    if (!timingInformationEstablished_)
    {
        timingInformationEstablished_ = true;
        return {1, false}; // a ratio of one rescales nothing
    }
    return {relaxed(barDuration_) / barDuration, relaxed(measureNumerator_) != measureNumerator};
}

LFOImpl::Timer::TimingInformationChange LFOImpl::Timer::updatePositionAndTimingInformation(
    float const positionInBars, float const barDuration, std::uint8_t const measureNumerator)
{
    LE_ASSERT(std::isfinite(positionInBars));
    LE_ASSERT(std::isfinite(barDuration));

    LE_ASSERT(positionInBars >= 0);
    LE_ASSERT(barDuration >= 0);

    // Position
    previousTimeInBars_ = currentTimeInBars_;
    currentTimeInBars_ = positionInBars;

    // Timing info
    // Implementation note:
    //   Bar duration change alone (e.g. when only the tempo changes) would be
    // implicitly handled (i.e. no update would be required because we remember
    // LFO durations relative to bar durations anyway) if it weren't for free
    // LFOs which require that their (absolute) value remains constant
    // (therefore their relative duration must be updated when the bar
    // duration changes).
    //                                        (02.02.2011.) (Domagoj Saric)
    TimingInformationChange const changeInfo(establishedChange(barDuration, measureNumerator));

    relaxed(barDuration_, barDuration);
    relaxed(measureNumerator_, measureNumerator);

    LE_ASSERT(std::isfinite(currentTimeInBars_));
    LE_ASSERT(std::isfinite(previousTimeInBars_));
    LE_ASSERT(std::isfinite(relaxed(barDuration_)));

    return changeInfo;
}

LFOImpl::Timer::TimingInformationChange
LFOImpl::Timer::updatePositionAndTimingInformation(unsigned int const deltaNumberOfSamples,
                                                   float const sampleRate)
{
    // Position
    float const timeToAdvanceInSeconds(Math::convert<float>(deltaNumberOfSamples) / sampleRate);
    float const timeToAdvanceInBars(timeToAdvanceInSeconds / relaxed(barDuration_));

    LE_ASSERT((currentTimeInBars_ >= previousTimeInBars_) || (currentTimeInBars_ == 0));
    previousTimeInBars_ = currentTimeInBars_;
    currentTimeInBars_ += timeToAdvanceInBars;

    // Timing info: the assumption, which is what the reference bar *is*.
    std::uint8_t const measureNumerator(referenceMeasureNumerator);
    float const barDuration(referenceBarDuration);

    TimingInformationChange const changeInfo(establishedChange(barDuration, measureNumerator));

    relaxed(barDuration_, barDuration);
    relaxed(measureNumerator_, measureNumerator);

    return changeInfo;
}

void LFOImpl::Timer::setPosition(unsigned int const numberOfSamples, float const sampleRate)
{
    float const timeInSeconds(Math::convert<float>(numberOfSamples) / sampleRate);
    setPosition(timeInSeconds);
}

void LFOImpl::Timer::setPosition(float const timeInSeconds)
{
    /// \note Three `LE_ASSUME`s stood here -- "assume 120 BPM 4/4" -- and two of
    /// them compared the wrong pair: `barDuration_ == 4` and `measureNumerator_
    /// == 60.0f / 120 * 4`, i.e. each against the other's value. An `LE_ASSUME`
    /// is a promise to the optimiser rather than a check, so a false one is
    /// undefined behaviour and nothing would have said so. Deleted rather than
    /// corrected: nothing calls this (`Engine::Processor::setPosition` is the
    /// only caller and has none of its own), so a promise about it cannot be
    /// tested and is not worth keeping.
    ///                                       (02.08.2026.) (SW port)

    // Position
    float const timeInBars(timeInSeconds / relaxed(barDuration_));

    LE_ASSERT((currentTimeInBars_ > previousTimeInBars_) || (currentTimeInBars_ == 0));
    previousTimeInBars_ = currentTimeInBars_;
    currentTimeInBars_ = timeInBars;
}

void LFOImpl::Timer::reset()
{
    currentTimeInBars_ = 0;
    previousTimeInBars_ = 0;

    // Back to the assumption, which is what the reference bar is.
    relaxed(barDuration_, referenceBarDuration);
    relaxed(measureNumerator_, referenceMeasureNumerator);
}

LFOImpl::value_type LFOImpl::Timer::measureNumeratorFloat()
{
    return Math::convert<LFOImpl::value_type>(measureNumerator());
}

bool LFOImpl::Timer::TimingInformationChange::barDurationChanged() const
{
    return !Math::is<1>(barDurationChangeRatio_);
}

//...mrmlj...a 'dynamic' (bounds) parameter...
LFOImpl::value_type LFOImpl::PeriodScaleParameterTraits::minimum()
{
    return LFOImpl::currentPeriodScaleMinimum();
}
LFOImpl::value_type LFOImpl::PeriodScaleParameterTraits::maximum()
{
    return LFOImpl::currentPeriodScaleMaximum();
}
bool LFOImpl::PeriodScaleParameterTraits::isValidValue(param_type value)
{
    return Math::isValueInRange<param_type>(value, minimum(), maximum());
}

} // namespace LE::Parameters
