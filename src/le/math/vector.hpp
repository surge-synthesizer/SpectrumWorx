////////////////////////////////////////////////////////////////////////////////
///
/// \file vector.hpp
/// ----------------
///
/// \brief Common operations for vectorized data. Mostly SIMD optimized.
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef vector_hpp__F5D72A06_E9E6_42D9_A4DB_4E7917819F41
#define vector_hpp__F5D72A06_E9E6_42D9_A4DB_4E7917819F41
//------------------------------------------------------------------------------
#include "le/utility/platformSpecifics.hpp"

#include <algorithm> // std::copy_n, in convertSamples below
#include <cstdint>
#include <limits>
#include "le/utility/span.hpp"

namespace LE::Math
{

unsigned int alignIndex(unsigned int index);
void *align(void *pointer);

////////////////////////////////////////////////////////////////////////////////
/// Range based interfaces (mostly unimplemented).
////////////////////////////////////////////////////////////////////////////////

using InputRange = LE::Utility::Span<float const>;
using OutputRange = LE::Utility::Span<float>;
using InputOutputRange = OutputRange;

void copy(InputRange const &, OutputRange const &);

void clear(InputOutputRange);
void fill(InputOutputRange, float value);
void negate(InputOutputRange);
void negate(InputOutputRange, unsigned int stride);
void randomize(InputOutputRange);
void reverse(InputOutputRange);
void swap(InputOutputRange const &, InputOutputRange const &);

float const &min(InputRange const &);
float const &max(InputRange const &);

void add(InputRange const &, InputOutputRange const &);
void add(InputRange const &, float constant, OutputRange const &);

void multiply(InputRange const &, InputRange const &, OutputRange);
void multiply(InputRange const &, InputOutputRange const &);
void multiply(InputRange const &, float multiplier, OutputRange);
void multiply(InputOutputRange const &, float multiplier);

void addProduct(InputRange, InputRange, InputOutputRange);

void cosine(InputRange, OutputRange);
void sine(InputRange, OutputRange);
void sinCos(InputRange, OutputRange, OutputRange);

void amplitudes(InputRange reals, InputRange imags, OutputRange amplitudes);
void phases(InputRange reals, InputRange imags, OutputRange phases);

void ln(InputRange const &, OutputRange const &);
void ln(InputOutputRange const &);
void exp(InputOutputRange const &);

void square(InputOutputRange);
void squareRoot(InputOutputRange);

float rms(InputRange const &);

void mix(InputRange const &amps, InputRange const &phases, InputOutputRange const &reals,
         InputOutputRange const &imags, float amPhWeight);
void mix(InputRange const &amps, InputRange const &phases, InputOutputRange const &reals,
         InputOutputRange const &imags, float amPhGain, float reImGain);

/// \note forcePositive floors the running sum at zero: pass it wherever the
/// input is a magnitude, because a running sum cannot preserve non-negativity
/// on its own. \see issue #84
void movingAverage(InputOutputRange const &, unsigned int windowWidth, bool forcePositive);
void symmetricMovingAverage(InputRange const &, OutputRange, unsigned int windowWidth,
                            bool forcePositive);

////////////////////////////////////////////////////////////////////////////////
/// Iterator interfaces.
////////////////////////////////////////////////////////////////////////////////

void copy(float const *pBegin, float const *pBeginEnd, float *pDestination);

void clear(float *pBegin, float const *pEnd);
void fill(float *pBegin, float const *pEnd, float value);
void negate(float *pBegin, float const *pEnd);
void reverse(float *pBegin, float const *pEnd);
void swap(float *pBegin, float const *pEnd, float *pDestination);

float const &min(float const *pBegin, float const *pEnd);
float const &max(float const *pBegin, float const *pEnd);

void add(float const *pInput, float *pInputOutput, float const *pOutputEnd);
void add(float const *pInput, float constant, float *pOutput, float const *pOutputEnd);

void multiply(float const *pFirstArray, float const *pSecondArray, float *pOutput,
              float const *pOutputEnd);
void multiply(float const *pInput, float *pInputOutput, float const *pOutputEnd);
void multiply(float scalar, float const *pInput, float *pOutput, float const *pOutputEnd);
void multiply(float scalar, float *pInputOutput, float const *pOutputEnd);

void addProduct(float const *pInput1, float const *pInput2, float *pInput3AndOutput,
                float const *pOutputEnd);

void sine(float const *pInput, float *pOutput, float const *pOutputEnd);
void cosine(float const *pInput, float *pOutput, float const *pOutputEnd);
void sinCos(float const *pInput, float const *pInputEnd, float *pSines, float *pCosines);

void amplitudes(float const *pReals, float const *pImags, float *pAmplitudes,
                float const *pAmplitudesEnd);
void phases(float const *pReals, float const *pImags, float *pPhases, float const *pPhasesEnd);

void polar2rectangular(float const *pAmplitudes, float const *pPhases, float *pReals, float *pImags,
                       float const *pImagsEnd);
void rectangular2polar(float const *pReals, float const *pImags, float *pAmplitudes, float *pPhases,
                       float const *pPhasesEnd);

void ln(float const *pInput, float *pOutput, float const *pOutputEnd);
void ln(float *pInputOutput, float const *pOutputEnd);
void exp(float *pInputOutput, float const *pOutputEnd);

void square(float *pInputOutput, float const *pOutputEnd);
void squareRoot(float *pInputOutput, float const *pOutputEnd);

float rms(float const *pData, float const *pDataEnd);

void mix(float const *pInput1, float const *pInput2, float *pOutput, float const *pOutputEnd,
         float input1Weight);
void mix(float const *pInput1, float const *pInput2, float *pOutput, float const *pOutputEnd,
         float input1Weight, float input2Weight);

////////////////////////////////////////////////////////////////////////////////
/// "Pointers + size" based interfaces.
////////////////////////////////////////////////////////////////////////////////

void copy(float const *pInput, float *pOutput, unsigned int numberOfElements);
void move(float const *pInput, float *pOutput, unsigned int numberOfElements);

void clear(float *pArray, unsigned int numberOfElements);
void fill(float *pArray, float value, unsigned int numberOfElements);
void negate(float *pArray, unsigned int numberOfElements);
void negate(float *pArray, unsigned int stride, unsigned int numberOfElements);
void randomize(float *pArray, unsigned int numberOfElements);
void reverse(float *pArray, unsigned int numberOfElements);
void swap(float *pFirstArray, float *pSecondArray, unsigned int numberOfElements);

float const &min(float const *pArray, unsigned int numberOfElements);
float const &max(float const *pArray, unsigned int numberOfElements);

void add(float const *pInput, float *pInputOutput, unsigned int numberOfElements);
void add(float const *pInput, float constant, float *pOutput, unsigned int numberOfElements);

void multiply(float const *pFirstArray, float const *pSecondArray, float *pOutput,
              unsigned int numberOfElements);
void multiply(float const *pInput, float *pInputOutput, unsigned int numberOfElements);
void multiply(float const *pInput, float scalar, float *pOutput, unsigned int numberOfElements);
void multiply(float *pInputOutput, float scalar, unsigned int numberOfElements);

void addProduct(float const *pInput1, float const *pInput2, float *pInput3AndOutput,
                unsigned int numberOfElements);

void sine(float const *pInput, float *pOutput, unsigned int numberOfElements);
void cosine(float const *pInput, float *pOutput, unsigned int numberOfElements);
void sinCos(float const *pInput, float *pSines, float *pCosines, unsigned int numberOfElements);

void amplitudes(float const *LE_RESTRICT pReals, float const *LE_RESTRICT pImags,
                float *LE_RESTRICT pAmplitudes, std::uint16_t numberOfElements);
void phases(float const *LE_RESTRICT pReals, float const *LE_RESTRICT pImags,
            float *LE_RESTRICT pPhases, std::uint16_t numberOfElements);

void polar2rectangular(float const *pAmplitudes, float const *pPhases, float *pReals, float *pImags,
                       std::uint16_t numberOfElements);
void rectangular2polar(float const *pReals, float const *pImags, float *pAmplitudes, float *pPhases,
                       std::uint16_t numberOfElements);

void ln(float const *pInput, float *pOutput, unsigned int numberOfElements);
void ln(float *pInputOutput, unsigned int numberOfElements);
void exp(float *pInputOutput, unsigned int numberOfElements);

void square(float *pInputOutput, unsigned int numberOfElements);
void squareRoot(float *pInputOutput, unsigned int numberOfElements);

float rms(float const *pData, unsigned int numberOfElements);

void mix(float const *pInput1, float const *pInput2, float *pOutput, float input1Weight,
         unsigned int numberOfElements);

void interleave(float const *LE_RESTRICT const *LE_RESTRICT pInputs, float *LE_RESTRICT pOutput,
                std::uint16_t numberOfElements, std::uint8_t numberOfChannels);
void deinterleave(float const *LE_RESTRICT pInput, float *LE_RESTRICT const *LE_RESTRICT pOutputs,
                  std::uint16_t numberOfElements, std::uint8_t numberOfChannels);

/// \note These may be used in both AudioIO and SW and so have to be in the
/// header (until LE.Math is separated into a shared library).
///                                           (05.02.2016.) (Domagoj Saric)

// http://blog.frama-c.com/index.php?post/2013/10/09/Overflow-float-integer
// http://stackoverflow.com/questions/9832430/arm-saturate-signed-int-to-unsigned-byte
// http://stackoverflow.com/questions/24546927/behavior-of-arm-neon-float-integer-conversion-with-overflow
LE_FORCEINLINE void convertSample(float const &LE_RESTRICT input, std::int16_t &LE_RESTRICT output)
{
#if 0 // slower & not autovectorized
    static auto constexpr scale( static_cast<float>( std::numeric_limits<std::int16_t>::max() ) );
    output = Math::convert<std::int16_t>( Math::clamp( input, -1.0f, +1.0f ) * scale );
#else
    static auto constexpr scale(static_cast<float>(std::numeric_limits<std::int32_t>::max()));
    /// \note We can use a plain static_cast/truncation here because the one bit
    /// bit of precision lost here does not matter (we discard all of the lower
    /// 16 bits anyway + floats have only 24 bits of precision to begin with).
    ///                                       (19.02.2016.) (Domagoj Saric)
    output = static_cast<std::int16_t>(static_cast<std::int32_t>(input * scale) >> 16);
#endif
}

LE_FORCEINLINE void convertSample(std::int16_t const &LE_RESTRICT input, float &LE_RESTRICT output)
{
    static float constexpr scale(-1.0f / std::numeric_limits<std::int16_t>::min());
    output = input * scale;
}

LE_FORCEINLINE void convertSample(std::int32_t const &LE_RESTRICT input, float &LE_RESTRICT output)
{
    static float constexpr scale(-1.0f / std::numeric_limits<std::int32_t>::min());
    output = input * scale;
}

template <typename Input, typename Output>
void convertSamples(Input const *LE_RESTRICT pInput, Output *LE_RESTRICT pOutput,
                    std::uint32_t samples)
{
#ifdef __clang__
#pragma clang loop vectorize(enable) interleave(enable)
#endif // __clang__
    while (samples--)
        convertSample(*pInput++, *pOutput++);
}

template <typename Sample>
void convertSamples(Sample const *LE_RESTRICT const pInput, Sample *LE_RESTRICT const pOutput,
                    std::uint32_t const samples)
{
    std::copy_n(pInput, samples, pOutput);
}

} // namespace LE::Math

#endif // vector_hpp
