////////////////////////////////////////////////////////////////////////////////
///
/// \file setup.hpp
/// ---------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef setup_hpp__6341F270_69CC_4677_A35B_374632A96E07
#define setup_hpp__6341F270_69CC_4677_A35B_374632A96E07
//------------------------------------------------------------------------------
#include "configuration.hpp"

#include "le/utility/platformSpecifics.hpp"

#include "le/utility/assert.hpp"

#include <cstdint>

namespace LE
{

namespace Math
{
template <typename Target, typename Source> void convert(Source const source, Target &target);
} // namespace Math

namespace SW::Engine
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Setup
///
/// \brief Holds parameters describing a particular engine setup. This include
/// the FFT configuration, the sample rate and the number of audio channels.
///
///    Stores all parameter values in different storage types (currently
/// unsigned int and float) as different effects need/use them in that way.
/// The user simply selects the wanted storage type when calling the appropriate
/// member template getter/setter. This way the type conversion is done in only
/// one place.
///
///    None of its member functions may throw.
///
////////////////////////////////////////////////////////////////////////////////

class Setup
{
  public:
    using Window = LE::SW::Engine::Constants::Window;

  public: // Read only interface for effects.
    template <typename T> T fftSize() const { return fftSize_; }
    template <typename T> T windowOverlappingFactor() const { return overlappingFactor_; }
    template <typename T> T sampleRate() const { return sampleRate_; }

    Window windowFunction() const { return windowFunction_; }
    float const &wolaGain() const { return wolaGain_; }
    std::uint8_t numberOfChannels() const { return numberOfChannels_; }
    std::uint8_t numberOfSideChannels() const { return numberOfSideChannels_; }
    float const &maximumAmplitude() const { return maximumAmplitude_; }
    float const &wolaRippleFactor() const { return wolaRippleFactor_; }

  public: // Utility interface.
    template <typename T> T frameSize() const { return fftSize<T>(); }
    template <typename T> T stepSize() const
    {
        return frameSize<T>() / windowOverlappingFactor<T>();
    }
    template <typename T> T windowSize() const { return fftSize<T>(); }

    /// \note Zero before the host has activated the plugin, there being no bin to
    /// spread the spectrum across -- and for an integer T the division that would
    /// otherwise happen is undefined behaviour. \see issue #81.
    template <typename T> T frequencyRangePerBin() const
    {
        if (fftSize<unsigned int>() == 0)
            return T(0);
        return sampleRate<T>() / fftSize<T>();
    }

    std::uint16_t numberOfBins() const;
    float frameTime() const;
    float stepTime() const;

    std::uint16_t latencyInSamples() const;
    float latencyInMilliseconds() const;

    std::uint16_t frequencyInHzToBin(std::uint32_t frequency) const;
    float normalisedFrequencyToHz(float normalisedFrequency) const;
    /// \brief normalisedFrequencyToHz() backwards, clamped: what a user types is
    /// not obliged to be inside Nyquist.
    float hzToNormalisedFrequency(float hz) const;
    std::uint16_t normalisedFrequencyToBin(float normalisedFrequency) const;
    std::uint16_t frequencyPercentageToBin(float percentage) const;
    std::uint16_t frequencyPercentageToBin(std::uint8_t percentage) const;

    std::uint16_t milliSecondsToSteps(std::uint16_t milliSeconds) const;
    float secondsToSteps(float seconds) const;
    float stepsPerSecond() const;

    bool hasSideChannel() const { return numberOfSideChannels() != 0; }

  public: // Non-const interface for the core engine.
    Setup();

    template <typename T> void setFFTSize(T const &newValue);
    template <typename T> void setOverlappingFactor(T const &newValue);
    template <typename T> void setSampleRate(T const &newValue) { sampleRate_ = newValue; }

    void setWindowFunction(Window);
    void setNumberOfChannels(std::uint8_t numberOfMainChannels, std::uint8_t numberOfSideChannels);

    void setWOLAGainAndRipple(float const gain, float const ripple)
    {
        wolaGain_ = gain;
        wolaRippleFactor_ = ripple;
    }

  private:
    void updateMaximumAmplitude();
    void verifyOverlapFactor();

  private:
    struct PositiveNumber
    {
        using real_t = float;
        using integer_t = std::uint32_t;

        real_t real;
        integer_t integer;

        operator real_t &()
        {
            LE_ASSUME(real >= 0);
            return real;
        }
        operator real_t const &() const
        {
            LE_ASSUME(real >= 0);
            return real;
        }
        operator integer_t &() { return integer; }
        operator integer_t const &() const { return integer; }

        operator std::uint16_t() const { return static_cast<std::uint16_t>(integer); }
        operator std::uint8_t() const { return static_cast<std::uint8_t>(integer); }

        template <typename T> PositiveNumber &operator=(T const other)
        {
            LE_ASSUME(other >= 0);
            Math::convert(other, real);
            Math::convert(other, integer);
            return *this;
        }

        template <typename T> PositiveNumber(T const other) { (*this) = other; }
    }; // struct PositiveNumber

  private:
    PositiveNumber fftSize_;
    PositiveNumber overlappingFactor_;
    PositiveNumber sampleRate_;
    Window windowFunction_;
    std::uint8_t numberOfChannels_;
    std::uint8_t numberOfSideChannels_;
    float wolaGain_;
    float maximumAmplitude_;
    float wolaRippleFactor_;
}; // class Setup

////////////////////////////////////////////////////////////////////////////////
//
// Setup::setFFTSize()
// -------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// Sets the current FFT size and updates the maximum amplitude value.
///
////////////////////////////////////////////////////////////////////////////////

template <typename T> void Setup::setFFTSize(T const &newValue)
{
    fftSize_ = newValue;
    updateMaximumAmplitude();
}

////////////////////////////////////////////////////////////////////////////////
//
// Setup::setOverlappingFactor()
// -----------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// Sets the current sample rate and asserts it is in the supported range.
///
////////////////////////////////////////////////////////////////////////////////

template <typename T> void Setup::setOverlappingFactor(T const &newValue)
{
    overlappingFactor_ = newValue;
    verifyOverlapFactor();
}

} // namespace SW::Engine

} // namespace LE

#endif // setup_hpp
