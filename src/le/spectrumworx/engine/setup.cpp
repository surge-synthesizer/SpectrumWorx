////////////////////////////////////////////////////////////////////////////////
///
/// setup.cpp
/// ---------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "setup.hpp"

#ifndef NDEBUG
#include "le/spectrumworx/engine/automatableParameters.hpp"
#endif // NDEBUG
#include "le/math/conversion.hpp"
#include "le/math/dft/domainConversion.hpp"
#include "le/math/dft/fft.hpp"
#include "le/math/math.hpp"

namespace LE
{

namespace Math
{
template void convert<float, unsigned int>(unsigned int source, float &target);
template void convert<unsigned int, unsigned int>(unsigned int source, unsigned int &target);
} // namespace Math

namespace SW::Engine
{

Setup::Setup()
    : // Implementation note:
      //   Sometimes an effect might request the sample rate before the host sets
      // it (e.g. loading a session on plugin startup) so we set a reasonable
      // default here to avoid undefined behaviour and/or crashes.
      //                                        (20.05.2010.) (Domagoj Saric)
      // Implementation note:
      //   FFT size and overlapping factor must not be set to their default values
      // so that the initial updateEngineSetup() call correctly detects that the
      // FFT and the WOLA buffer need to (re)sized/(re)calculated.
      //                                        (21.12.2010.) (Domagoj Saric)
      fftSize_(0),
      overlappingFactor_(
          1), // To prevent div-by-zero crashes in stepSize() before the engine is fully configured
      sampleRate_(Constants::defaultSampleRate), windowFunction_(Constants::defaultWindow),
      numberOfChannels_(0), numberOfSideChannels_(0), wolaGain_(0), maximumAmplitude_(0)
{
}

////////////////////////////////////////////////////////////////////////////////
//
// Setup::numberOfBins()
// ---------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Returns the number of 'unique'/'significant' DFT bins for the current
/// setup. Because of the Hermitian form used by the FFT implementation this is
/// actually (N/2)+1 not just (N/2).
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   Other functions that require this number do not use this function for
// performance reasons (for example for their specific needs they need a float
// value or they can skip the +1 addition...).
//                                            (03.03.2010.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

std::uint16_t Setup::numberOfBins() const
{
    return static_cast<std::uint16_t>(fftSize<unsigned int>()) / 2 + 1;
}

////////////////////////////////////////////////////////////////////////////////
//
// Setup::normalisedFrequencyToBin()
// ---------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Gets the DFT bin/index corresponding to a particular frequency taking
/// into consideration the "unwrapped Hermitian form" data layout [that there
/// are actually ((FFT-size / 2) + 1), not (FFT-size / 2), valid frequencies or
/// bins.
///
////////////////////////////////////////////////////////////////////////////////

std::uint16_t Setup::normalisedFrequencyToBin(float const normalisedFrequency) const
{
    LE_ASSERT_MSG(Math::isNormalisedValue(normalisedFrequency), "Frequency out of range.");
    auto const result(Math::convert<std::uint16_t>(normalisedFrequency * fftSize<float>() / 2));
    LE_ASSERT_MSG(result ==
                      Math::convert<std::uint16_t>(normalisedFrequency * (numberOfBins() - 1)),
                  "Frequency-bin calculation bug.");
    return result;
}

////////////////////////////////////////////////////////////////////////////////
//
// Setup::frequencyPercentageToBin()
// ---------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief @see Setup::normalisedFrequencyToBin().
///
////////////////////////////////////////////////////////////////////////////////

std::uint16_t Setup::frequencyPercentageToBin(float const percentage) const
{
    return normalisedFrequencyToBin(Math::percentage2NormalisedLinear(percentage));
}

////////////////////////////////////////////////////////////////////////////////
//
// Setup::frequencyPercentageToBin()
// ---------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief @see Setup::normalisedFrequencyToBin().
///
////////////////////////////////////////////////////////////////////////////////

std::uint16_t Setup::frequencyPercentageToBin(std::uint8_t const percentage) const
{
    LE_ASSERT_MSG(percentage >= 0 && percentage <= 100, "Percentage value out of range.");
    auto const result(fftSize<unsigned int>() * percentage / 100 / 2);
    LE_ASSERT_MSG(result == unsigned(percentage * (numberOfBins() - 1) / 100),
                  "Percentage-bin calculation bug.");
    return static_cast<std::uint16_t>(result);
}

/// \note Clamped rather than asserted, for the same reason hzToNormalisedFrequency()
/// below is. A parameter's range is fixed in Hz -- Octaver's `Low pass` reaches
/// 16 kHz -- and the sample rate is the host's to choose, so a parameter at rest
/// can sit above Nyquist without anybody having done anything wrong. Every caller
/// already clamped the result into its working range; the assertion was the only
/// thing that minded.
std::uint16_t Setup::frequencyInHzToBin(std::uint32_t const frequency) const
{
    auto const maximumFrequency(sampleRate<unsigned int>() / 2);
    auto const lastBin(numberOfBins() - 1);
    if (frequency >= maximumFrequency) // also the not-yet-configured sample rate of zero
        return static_cast<std::uint16_t>(lastBin);
    return static_cast<std::uint16_t>(lastBin * frequency / maximumFrequency);
}

float Setup::normalisedFrequencyToHz(float const normalisedFrequency) const
{
    LE_ASSERT_MSG(Math::isNormalisedValue(normalisedFrequency), "Frequency out of range.");
    return normalisedFrequency * sampleRate<float>() / 2;
}

/// \note Clamped where the forward direction asserts, because the two have
/// different callers: normalisedFrequencyToHz() is handed a stored parameter,
/// which is in range by construction, and this one is handed a number a user
/// typed. "40000 Hz" at 44.1 kHz is Nyquist, not an assertion.
float Setup::hzToNormalisedFrequency(float const hz) const
{
    return Math::clamp(hz * 2 / sampleRate<float>(), 0.0f, 1.0f);
}

////////////////////////////////////////////////////////////////////////////////
//
// Setup::milliSecondsToSteps()
// ----------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Rounds up.
///
////////////////////////////////////////////////////////////////////////////////

std::uint16_t Setup::milliSecondsToSteps(std::uint16_t const milliSeconds) const
{
    auto const result(Math::roundUpUnsignedIntegerDivision(
        milliSeconds * sampleRate<std::uint32_t>() / stepSize<std::uint16_t>(), 1000U));
    LE_ASSERT_MSG(result <= (std::numeric_limits<std::uint16_t>::max)(), "Step overflow");
    return result;
}

float Setup::secondsToSteps(float const seconds) const { return seconds / stepTime(); }
float Setup::stepsPerSecond() const { return secondsToSteps(1); }
float Setup::frameTime() const { return frameSize<float>() / sampleRate<float>(); }
float Setup::stepTime() const { return stepSize<float>() / sampleRate<float>(); }

////////////////////////////////////////////////////////////////////////////////
//
// Setup::latencyInSamples()
// -------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Calculate processing latency, required for latency compensating (PDC
/// - Plugin Delay Compensation) hosts.
///
/// This is in effect equivalent to the window/frame size. Overlap factor/step
/// size has no influence on this value because each sample has to "pass
/// through" the entire FFT buffer before appearing at the output regardless of
/// window overlapping.
///
/// http://www.mathworks.com/help/dsp/ref/overlapaddfftfilter.html
/// http://dsp.stackexchange.com/questions/2537/do-fft-based-filtering-methods-add-intrinsic-latency-to-a-real-time-algorithm
///
////////////////////////////////////////////////////////////////////////////////

std::uint16_t Setup::latencyInSamples() const
{
    return static_cast<std::uint16_t>(frameSize<unsigned int>());
}

float Setup::latencyInMilliseconds() const { return frameTime() * 1000; }

void Setup::setWindowFunction(Window const window)
{
    LE_ASSERT_MSG(window >= 0, "Unknown window.");
    LE_ASSERT_MSG(window < Constants::NumberOfWindows, "Unknown window.");
    windowFunction_ = window;
}

void Setup::setNumberOfChannels(std::uint8_t const numberOfMainChannels,
                                std::uint8_t const numberOfSideChannels)
{
    LE_ASSERT_MSG(numberOfSideChannels <= numberOfMainChannels, "Too many side channel inputs.");
    numberOfChannels_ = numberOfMainChannels;
    numberOfSideChannels_ = numberOfSideChannels;
}

void Setup::updateMaximumAmplitude()
{
    LE_ASSERT_MSG(Engine::FFTSize::isValidValue(
                      static_cast<std::uint16_t>(static_cast<unsigned int>(fftSize_))),
                  "Invalid FFT size.");
    maximumAmplitude_ = Math::FFT_float_real_1D::maximumAmplitude(fftSize<float>());
}

void Setup::verifyOverlapFactor()
{
    LE_ASSERT_MSG(Engine::OverlapFactor::isValidValue(
                      static_cast<std::uint8_t>(static_cast<unsigned int>(overlappingFactor_))),
                  "Invalid overlap factor.");
}

} // namespace SW::Engine

} // namespace LE
