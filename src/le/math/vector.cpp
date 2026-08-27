////////////////////////////////////////////////////////////////////////////////
///
/// vector.cpp
/// ----------
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#if defined(__APPLE__)
#include "TargetConditionals.h"
#define LE_MATH_USE_ACC
/// \note The text named OS X 10.4, an OS from 2005 and the one thing in the
/// message that had stopped being worth saying. What it is for -- telling you at
/// build time which backend the vector primitives were compiled against -- is
/// all that is left of it.
#pragma message("LE.Math.Vector using the Accelerate framework.")
#endif // __APPLE__
//------------------------------------------------------------------------------

// Implementation note:
//   Disable the debug runtime checks for this module. NT2's debug performance
// was the reason; CMake does the same globally because this did not work with
// MSVC10.
//                                            (25.08.2011.) (Domagoj Saric)
#ifdef _MSC_VER
#pragma runtime_checks("", off)
#pragma check_stack(off)
#endif // MSVC

#include "le/utility/platformSpecifics.hpp"
#include "le/utility/span.hpp"

// A nice list of profilers:
// http://stackoverflow.com/questions/4394606/beyond-stack-sampling-c-profilers

#include "vector.hpp"

#include "constants.hpp"
#include "conversion.hpp"
#include "math.hpp"

#include "le/utility/intrinsics.hpp"

/// \note These were reached transitively through the Accelerate headers. The
/// portable arms below use std::memcpy / memmove / memset, std::log, exp,
/// sqrt, sin, cos and std::{min,max}_element with it out of the include graph.
#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(LE_MATH_USE_ACC)
// Implementation note:
//   vForce uses int const * to pass the size parameter while we use
// unsigned int so we assert here that it is safe to do a pointer
// reinterpret_cast.
//                                        (17.05.2011.) (Domagoj Saric)
static_assert((std::endian::native == std::endian::little) || (sizeof(unsigned int) == sizeof(int)),
              "Unexpected data sizes");

#if (TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR || (__MAC_OS_X_VERSION_MAX_ALLOWED >= __MAC_10_9))
#include "Accelerate/Accelerate.h"
#else
#include "vecLib/vDSP.h"
#include "vecLib/vForce.h"
#endif
#endif // LE_MATH_USE_ACC

// Other OSS libs
// http://simdx86.sourceforge.net
// http://sseplus.sourceforge.net
// http://sourceforge.net/projects/v3d
// http://sourceforge.net/projects/libsimd
// http://sourceforge.net/projects/framewave

#include "le/utility/assert.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <numeric>

namespace LE::Math
{

namespace Constants
{
std::size_t const vectorSize = Utility::Constants::vectorAlignment / sizeof(float);
} // namespace Constants

void *align(void *const pointer)
{
    std::size_t const vectorAlignment = Constants::vectorSize * sizeof(float); //...mrmlj...
    return reinterpret_cast<void *>((reinterpret_cast<std::size_t>(pointer) + vectorAlignment - 1) &
                                    ~(vectorAlignment - 1));
}

unsigned int alignIndex(unsigned int const index)
{
    return (index + Constants::vectorSize - 1) & ~(Constants::vectorSize - 1);
}

////////////////////////////////////////////////////////////////////////////////
/// Range based interfaces.
///
/// \note Forwarders, all of them. Every primitive is implemented once, in the
/// "pointers + size" section at the bottom of this file, and the two interfaces
/// above it turn their arguments into a pointer and a count.
///
///   It used to be a choice. `LE_MATH_NATIVE_POINTER_SIZE_INTERFACE` was defined
/// on Apple and undefined everywhere else, and it decided which of the pointer
/// forms held the implementation and which forwarded -- because with two
/// vectorised backends the natural shape of the call differed: vDSP and vvv take
/// a pointer and a count, NT2's loops took a pair of iterators. Exactly one of
/// the two could hold the body; the other had to forward, or they recursed.
///
///   There is one backend now, so the macro was describing a choice nobody
/// makes. It is gone, and with it the second body of every primitive it guarded.
////////////////////////////////////////////////////////////////////////////////

void copy(InputRange const &input, OutputRange const &output)
{
    LE_ASSERT_MSG(input.size() <= output.size(), "Buffer sizes mismatch.");
    copy(input.begin(), output.begin(), static_cast<unsigned int>(input.size()));
}

void clear(InputOutputRange const data)
{
    clear(data.begin(), static_cast<unsigned int>(data.size()));
}

void fill(InputOutputRange const data, float const value)
{
    fill(data.begin(), value, static_cast<unsigned int>(data.size()));
}

/// \note vector.hpp declared this alongside the strided overload but nothing
/// ever defined it. Nothing called it either; sw-tests is the first thing that
/// tried.
void negate(InputOutputRange const data) { negate(data, 1); }

void negate(InputOutputRange data, unsigned int const stride)
{
#if defined(LE_MATH_USE_ACC)
    negate(data.begin(), stride, static_cast<unsigned int>(data.size()));
#else
    // Implementation note:
    //   We cannot just write while ( data ) because this checks for equality
    // between begin() and end() and begin can actually go past end with strides
    // larger than one.
    //                                        (01.07.2011.) (Domagoj Saric)
    while (data.begin() < data.end())
    {
        data.front() = -data.front();
        data.advance_begin(stride);
    }
#endif
}

float const &min(InputRange const &data)
{
    return min(data.begin(), static_cast<unsigned int>(data.size()));
}

float const &max(InputRange const &data)
{
    return max(data.begin(), static_cast<unsigned int>(data.size()));
}

void add(InputRange const &input, InputOutputRange const &inputOutput)
{
    LE_ASSERT_MSG(input.size() <= inputOutput.size(), "Buffer sizes mismatch.");
    add(input.begin(), inputOutput.begin(), inputOutput.end());
}

void add(InputRange const &input, float const constant, OutputRange const &output)
{
    LE_ASSERT_MSG(input.size() <= output.size(), "Buffer sizes mismatch.");
    add(input.begin(), constant, output.begin(), output.end());
}

void multiply(InputRange const &input, float const multiplier, OutputRange const output)
{
    LE_ASSERT_MSG(input.size() <= output.size(), "Buffer sizes mismatch.");
    multiply(multiplier, input.begin(), output.begin(), output.begin() + input.size());
}

void multiply(InputOutputRange const &data, float const multiplier)
{
    multiply(multiplier, data.begin(), data.end());
}

void ln(InputRange const &input, OutputRange const &output)
{
    LE_ASSERT_MSG(input.size() == output.size(), "Buffer sizes mismatch.");
    ln(input.begin(), output.begin(), output.end());
}

void ln(InputOutputRange const &data) { ln(data.begin(), data.end()); }

void exp(InputOutputRange const &data) { exp(data.begin(), data.end()); }

void square(InputOutputRange const data) { square(data.begin(), data.end()); }

void squareRoot(InputOutputRange const data) { squareRoot(data.begin(), data.end()); }

float rms(InputRange const &data) { return rms(data.begin(), data.end()); }

void mix(InputRange const &amps, InputRange const &phases, InputOutputRange const &realsParam,
         InputOutputRange const &imagsParam, float const amPhGain, float const reImGain)
{
    //...mrmlj...use vForce...
    // https://developer.apple.com/library/mac/#documentation/Performance/Conceptual/vecLib/Reference/reference.html

    LE_ASSERT_MSG(amps.size() >= realsParam.size(), "Buffer sizes mismatch.");
    LE_ASSERT_MSG(phases.size() >= realsParam.size(), "Buffer sizes mismatch.");
    LE_ASSERT_MSG(realsParam.size() == imagsParam.size(), "Buffer sizes mismatch.");

    float const *LE_RESTRICT pAmp(amps.begin());
    float const *LE_RESTRICT pPhase(phases.begin());
    float *LE_RESTRICT pReal(realsParam.begin());
    float *LE_RESTRICT pImag(imagsParam.begin());
    auto counter(realsParam.size());
    while (counter--)
    {
        float const weightedAmp(*pAmp++ * amPhGain);
        float const phase(*pPhase++);
        *pReal = weightedAmp * std::cos(phase) + *pReal * reImGain;
        *pImag = weightedAmp * std::sin(phase) + *pImag * reImGain;
        ++pReal;
        ++pImag;
    }
}

void mix(InputRange const &amps, InputRange const &phases, InputOutputRange const &realsParam,
         InputOutputRange const &imagsParam, float const amPhWeight)
{
    mix(amps, phases, realsParam, imagsParam, amPhWeight, 1 - amPhWeight);
}

/// \note A term below the ulp of the partial sum vanishes into it and is then
/// subtracted back out, leaving a residue no later term removes -- which is why
/// a running sum cannot keep a non-negative input non-negative. \see issue #84
namespace
{
struct SumFloor
{
    bool forcePositive;
    float operator()(float const sum) const { return forcePositive ? std::max(0.0f, sum) : sum; }
};
} // anonymous namespace

// http://www.analog.com/static/imported-files/tech_docs/dsp_book_Ch15.pdf
void movingAverage(InputOutputRange const &data, unsigned int const windowWidth,
                   bool const forcePositive)
{
    LE_ASSERT_MSG(windowWidth, "Wrong parameters.");

    SumFloor const floored{forcePositive};
    float const inverseWindowWidth(1 / convert<float>(windowWidth));
    InputOutputRange window(&data[0], &data[windowWidth]);
    float sum(floored(std::accumulate(window.begin(), window.end(), 0.0f)));

    // Main full window section
    {
        while (window.end() != data.end())
        {
            float const oldSumValue(window.front());
            window.front() = sum * inverseWindowWidth;
            window.advance_begin(1);
            window.advance_end(1);
            float const newSumValue(window.back());
            sum -= oldSumValue;
            sum += newSumValue;
            sum = floored(sum);
        }
    }

    // Trailing partial window section
    {
        float tailWindowWidth(convert<float>(windowWidth));
        while (window)
        {
            float const oldSumValue(window.front());
            window.front() = sum / tailWindowWidth--;
            window.advance_begin(1);
            sum -= oldSumValue;
            sum = floored(sum);
        }
    }
}

/// \note The window is narrowed here rather than by the caller: it is counted in
/// bins and the working range is not, so no parameter range establishes the fit.
/// Sharper never clamped at all. \see issue #190
void symmetricMovingAverage(InputRange const &input, OutputRange const output,
                            unsigned int const windowWidth, bool const forcePositive)
{
    LE_ASSERT_MSG(input.size() == output.size(), "Input/output buffer sizes mismatched.");

    auto const dataSize(unsigned(input.size()));
    if (!dataSize)
        return;

    SumFloor const floored{forcePositive};

    // half a window either side of a centre sample; zero is the identity
    unsigned int const halfWindowWidth(std::min(windowWidth / 2, (dataSize - 1) / 2));
    unsigned int const fullWindowWidth(halfWindowWidth + 1 + halfWindowWidth);
    InputRange window(&input[0], &input[halfWindowWidth + 1 - 1] + 1);
    float *pOutputSample(output.begin());
    float sum(floored(std::accumulate(window.begin(), window.end(), 0.0f)));

    // Leading partial window section (before the halfWindowWidth + 1 sample)
    {
        LE_ASSERT_MSG(unsigned(window.size()) == halfWindowWidth + 1, "Algorithm bug.");

        float leadWindowWidth(convert<float>(halfWindowWidth + 1));
        float const *const pLeadWindowEnd(&input[fullWindowWidth - 1] + 1);
        while (window.end() != pLeadWindowEnd)
        {
            *pOutputSample++ = sum / leadWindowWidth++;
            window.advance_end(1);
            sum = floored(sum + window.back());
        }
    }

    // Main full window section
    {
        LE_ASSERT_MSG(pOutputSample == &output[halfWindowWidth], "Algorithm bug.");
        LE_ASSERT_MSG(unsigned(window.size()) == fullWindowWidth, "Algorithm bug.");

        float const inverseWindowWidth(1 / convert<float>(fullWindowWidth));
        while (window.end() != input.end())
        {
            *pOutputSample++ = sum * inverseWindowWidth;
            sum -= window.front();
            window.advance_begin(1);
            window.advance_end(1);
            sum += window.back();
            sum = floored(sum);
        }
    }

    // Trailing partial window section
    {
        LE_ASSERT_MSG(pOutputSample == output.end() - (halfWindowWidth + 1), "Algorithm bug.");
        LE_ASSERT_MSG(unsigned(window.size()) == fullWindowWidth, "Algorithm bug.");

        float tailWindowWidth(convert<float>(fullWindowWidth));
        while (pOutputSample != output.end())
        {
            *pOutputSample++ = sum / tailWindowWidth--;
            sum -= window.front();
            window.advance_begin(1);
            sum = floored(sum);
        }
    }
}

void swap(InputOutputRange const &range1, InputOutputRange const &range2)
{
    LE_ASSERT_MSG(range1.size() == range2.size(), "Buffer sizes mismatch.");
    swap(range1.begin(), range2.begin(), static_cast<unsigned int>(range1.size()));
}

////////////////////////////////////////////////////////////////////////////////
/// Iterator interfaces.
////////////////////////////////////////////////////////////////////////////////

void copy(float const *const pBegin, float const *const pBeginEnd, float *const pDestination)
{
    LE_ASSERT_MSG(pBegin <= pBeginEnd, "Invalid range.");
    copy(pBegin, pDestination, static_cast<unsigned int>(pBeginEnd - pBegin));
}

void clear(float *const pBegin, float const *const pEnd)
{
    LE_ASSERT_MSG(pBegin <= pEnd, "Invalid range.");
    clear(pBegin, static_cast<unsigned int>(pEnd - pBegin));
}

void fill(float *const pBegin, float const *const pEnd, float const value)
{
    LE_ASSERT_MSG(pBegin <= pEnd, "Invalid range.");
    fill(pBegin, value, static_cast<unsigned int>(pEnd - pBegin));
}

void negate(float *const pBegin, float const *const pEnd)
{
    LE_ASSERT_MSG(pBegin <= pEnd, "Invalid range.");
    negate(pBegin, static_cast<unsigned int>(pEnd - pBegin));
}

void reverse(float *LE_RESTRICT const pBegin, float const *const pEnd)
{
    LE_ASSERT_MSG(pBegin <= pEnd, "Invalid range.");
    reverse(pBegin, static_cast<unsigned int>(pEnd - pBegin));
}

void swap(float *LE_RESTRICT const pBegin, float const *const pEnd,
          float *LE_RESTRICT const pDestination)
{
    LE_ASSERT_MSG(pBegin <= pEnd, "Invalid range.");
    swap(pBegin, pDestination, static_cast<unsigned int>(pEnd - pBegin));
}

float const &min(float const *const pBegin, float const *const pEnd)
{
    return min(pBegin, static_cast<unsigned int>(pEnd - pBegin));
}

float const &max(float const *const pBegin, float const *const pEnd)
{
    return max(pBegin, static_cast<unsigned int>(pEnd - pBegin));
}

void add(float const *const pInputData, float *const pInputOutput, float const *const pOutputEnd)
{
    add(pInputData, pInputOutput, static_cast<unsigned int>(pOutputEnd - pInputOutput));
}

void add(float const *const pInputData, float const scalar, float *const pOutput,
         float const *const pOutputEnd)
{
    add(pInputData, scalar, pOutput, static_cast<unsigned int>(pOutputEnd - pOutput));
}

void multiply(float const *const pFirstArray, float const *const pSecondArray, float *const pOutput,
              float const *const pOutputEnd)
{
    multiply(pFirstArray, pSecondArray, pOutput, static_cast<unsigned int>(pOutputEnd - pOutput));
}

void multiply(float const *const pInputData, float *const pInputOutput,
              float const *const pOutputEnd)
{
    multiply(pInputData, pInputOutput, static_cast<unsigned int>(pOutputEnd - pInputOutput));
}

void multiply(float const scalar, float const *LE_RESTRICT const pInputData,
              float *LE_RESTRICT const pOutput, float const *LE_RESTRICT const pOutputEnd)
{
    multiply(pInputData, scalar, pOutput, static_cast<unsigned int>(pOutputEnd - pOutput));
}

void multiply(float const scalar, float *LE_RESTRICT const pInputOutput,
              float const *LE_RESTRICT const pOutputEnd)
{
    multiply(pInputOutput, scalar, static_cast<unsigned int>(pOutputEnd - pInputOutput));
}

void addProduct(float const *LE_RESTRICT const pInputData1,
                float const *LE_RESTRICT const pInputData2,
                float *LE_RESTRICT const pInput3AndOutput,
                float const *LE_RESTRICT const pOutputEnd)
{
    addProduct(pInputData1, pInputData2, pInput3AndOutput,
               static_cast<unsigned int>(pOutputEnd - pInput3AndOutput));
}

void rectangular2polar(float const *LE_RESTRICT const pReals, float const *LE_RESTRICT const pImags,
                       float *LE_RESTRICT const pAmplitudes, float *LE_RESTRICT const pPhases,
                       float const *const pPhasesEnd)
{
    rectangular2polar(pReals, pImags, pAmplitudes, pPhases,
                      static_cast<std::uint16_t>(pPhasesEnd - pPhases));
}

void ln(float *LE_RESTRICT const pInputOutput, float const *LE_RESTRICT const pOutputEnd)
{
    ln(pInputOutput, static_cast<unsigned int>(pOutputEnd - pInputOutput));
}

void ln(float const *LE_RESTRICT const pInput, float *LE_RESTRICT const pOutput,
        float const *LE_RESTRICT const pOutputEnd)
{
    ln(pInput, pOutput, static_cast<unsigned int>(pOutputEnd - pOutput));
}

void exp(float *LE_RESTRICT const pInputOutput, float const *LE_RESTRICT const pOutputEnd)
{
    exp(pInputOutput, static_cast<unsigned int>(pOutputEnd - pInputOutput));
}

void square(float *const pInputOutput, float const *const pOutputEnd)
{
    square(pInputOutput, static_cast<unsigned int>(pOutputEnd - pInputOutput));
}

void squareRoot(float *const pInputOutput, float const *const pOutputEnd)
{
    squareRoot(pInputOutput, static_cast<unsigned int>(pOutputEnd - pInputOutput));
}

float rms(float const *const pData, float const *const pDataEnd)
{
    return rms(pData, static_cast<unsigned int>(pDataEnd - pData));
}

void LE_NOINLINE
mix(float const *LE_RESTRICT pInput1, float const *LE_RESTRICT pInput2,
    float *LE_RESTRICT pOutput, // restrict allows two pointers to point to the exact same object
    float const *LE_RESTRICT const pOutputEnd, float const input1Weight)
{
    // Blending formula:
    //
    // out = x * carrier + ( 1 - x ) * blender
    // out = x * carrier + blender - x * blender
    // out = x * ( carrier - blender ) + blender
    //
    // x = 1 => out = carrier
    // x = 0 => out = blender

    while (pOutput < pOutputEnd)
    {
        float const input1(*pInput1++);
        float const input2(*pInput2++);
        *pOutput++ = input1Weight * (input1 - input2) + input2;
    }
}

void LE_NOINLINE
mix(float const *LE_RESTRICT pInput1, float const *LE_RESTRICT pInput2,
    float *LE_RESTRICT pOutput, // restrict allows two pointers to point to the exact same object
    float const *LE_RESTRICT const pOutputEnd, float const input1Weight, float const input2Weight)
{
    while (pOutput < pOutputEnd)
    {
        float const input1(*pInput1++);
        float const input2(*pInput2++);
        *pOutput++ = input1 * input1Weight + input2 * input2Weight;
    }
}

////////////////////////////////////////////////////////////////////////////////
/// "Pointers + size" based interfaces.
////////////////////////////////////////////////////////////////////////////////

void copy(float const *LE_RESTRICT const pInput, float *LE_RESTRICT const pOutput,
          unsigned int const numberOfElements)
{
    LE_ASSERT_MSG((pOutput >= pInput + numberOfElements) || (pInput >= pOutput + numberOfElements),
                  "Buffer overlap."); // Use move for overlapping ranges.
    std::memcpy(pOutput, pInput, numberOfElements * sizeof(*pInput));
}

void move(float const *const pInput, float *const pOutput, unsigned int const numberOfElements)
{
    //...mrmlj...vDSP_mmov seems slower/non-vectorized
    std::memmove(pOutput, pInput, numberOfElements * sizeof(*pInput));
}

void clear(float *const pArray, unsigned int const numberOfElements)
{
    //...mrmlj...vDSP_vclr seems slower/non-vectorized
    std::memset(pArray, 0, numberOfElements * sizeof(*pArray));
}

void fill(float *const pArray, float const value, unsigned int const numberOfElements)
{
#ifdef LE_MATH_USE_ACC
    vDSP_vfill(const_cast<float *>(&value), pArray, 1, numberOfElements);
#else
    std::fill_n(pArray, numberOfElements, value);
#endif
}

void negate(float *const pArray, unsigned int const numberOfElements)
{
#ifdef LE_MATH_USE_ACC
    negate(pArray, 1, numberOfElements);
#else
    for (float &value : LE::Utility::makeSpan(pArray, numberOfElements))
        value = -value;
#endif
}

void negate(float *const pArray, unsigned int const stride, unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    vDSP_vneg(pArray, stride, pArray, stride, numberOfElements);
#else
    negate(InputOutputRange(pArray, pArray + numberOfElements), stride);
#endif
}

void reverse(float *const pArray, unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    vDSP_vrvrs(pArray, 1, numberOfElements);
#else
    std::reverse(pArray, pArray + numberOfElements);
#endif
}

void swap(float *const pFirstArray, float *const pSecondArray, unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    vDSP_vswap(pFirstArray, 1, pSecondArray, 1, numberOfElements);
#else
    std::swap_ranges(pFirstArray, pFirstArray + numberOfElements, pSecondArray);
#endif
}

float const &min(float const *const pArray, unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    float result;
    vDSP_Length resultIndex;
    vDSP_minvi(const_cast<float *>(pArray), 1, &result, &resultIndex, numberOfElements);
    LE_ASSERT(pArray[resultIndex] == result);
    return pArray[resultIndex];
#else
    return *std::min_element(pArray, pArray + numberOfElements);
#endif
}

float const &max(float const *const pArray, unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    float result;
    vDSP_Length resultIndex;
    vDSP_maxvi(const_cast<float *>(pArray), 1, &result, &resultIndex, numberOfElements);
    LE_ASSERT(pArray[resultIndex] == result);
    return pArray[resultIndex];
#else
    return *std::max_element(pArray, pArray + numberOfElements);
#endif
}

/// \note The portable arms below are elementwise and in the caller's order, so
/// both GCC and Clang vectorise them at -O2 and above without needing
/// reassociation, and the rounding is the scalar loop's. They used to sit in the
/// iterator section; they are here because this is where the one implementation
/// of each primitive lives.

void add(float const *const pInput, float *const pInputOutput, unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    vDSP_vadd(pInput, 1, pInputOutput, 1, pInputOutput, 1, numberOfElements);
#else
    for (unsigned int element(0); element < numberOfElements; ++element)
        pInputOutput[element] += pInput[element];
#endif // LE_MATH_USE_ACC
}

void add(float const *const pInput, float const constant, float *const pOutput,
         unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    vDSP_vsadd(const_cast<float *>(pInput), 1, const_cast<float *>(&constant), pOutput, 1,
               numberOfElements);
#else
    for (unsigned int element(0); element < numberOfElements; ++element)
        pOutput[element] = pInput[element] + constant;
#endif // LE_MATH_USE_ACC
}

/// \note Deliberately not restrict-qualified: the in-place overloads below call
/// these with the same pointer for two arguments.
void multiply(float const *const pFirstArray, float const *const pSecondArray, float *const pOutput,
              unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    vDSP_vmul(pFirstArray, 1, pSecondArray, 1, pOutput, 1, numberOfElements);
#else
    for (unsigned int element(0); element < numberOfElements; ++element)
        pOutput[element] = pFirstArray[element] * pSecondArray[element];
#endif // LE_MATH_USE_ACC
}

void multiply(float const *const pInput, float *const pInputOutput,
              unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    multiply(pInput, pInputOutput, pInputOutput, numberOfElements);
#else
    for (unsigned int element(0); element < numberOfElements; ++element)
        pInputOutput[element] *= pInput[element];
#endif // LE_MATH_USE_ACC
}

/// \note No scalar == 0 / scalar == 1 short circuits: vDSP_vsmul has none
/// either, and the difference is observable on a non-finite input.
///
/// \note Not restrict-qualified: the in-place overload below passes the same
/// pointer as both the input and the output.
void multiply(float const *const pInput, float const scalar, float *const pOutput,
              unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    vDSP_vsmul(pInput, 1, &scalar, pOutput, 1, numberOfElements);
#else
    for (unsigned int element(0); element < numberOfElements; ++element)
        pOutput[element] = scalar * pInput[element];
#endif // LE_MATH_USE_ACC
}

void multiply(float *const pInputOutput, float const scalar, unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    multiply(pInputOutput, scalar, pInputOutput, numberOfElements);
#else
    for (unsigned int element(0); element < numberOfElements; ++element)
        pInputOutput[element] *= scalar;
#endif // LE_MATH_USE_ACC
}

void addProduct(float const *const pInput1, float const *const pInput2,
                float *const pInput3AndOutput, unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    vDSP_vma(const_cast<float *>(pInput1), 1, const_cast<float *>(pInput2), 1, pInput3AndOutput, 1,
             pInput3AndOutput, 1, numberOfElements);
#else
    for (unsigned int element(0); element < numberOfElements; ++element)
        pInput3AndOutput[element] += pInput1[element] * pInput2[element];
#endif // LE_MATH_USE_ACC
}

void rectangular2polar(float const *LE_RESTRICT const pReals, float const *LE_RESTRICT const pImags,
                       float *LE_RESTRICT const pAmplitudes, float *LE_RESTRICT const pPhases,
                       std::uint16_t const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    DSPSplitComplex data = {const_cast<float *>(pReals), const_cast<float *>(pImags)};
    vDSP_zvabs(&data, 1, pAmplitudes, 1, numberOfElements);
    int const vDSPNumberOfElements(numberOfElements);
    vvatan2f(pPhases, pImags, pReals, &vDSPNumberOfElements);
#if TARGET_OS_IPHONE && !defined(NDEBUG)
    // Implementation note:
    //   The iOS phase computation returns NaNs for inputs with zero
    // reals. This causes a chain of assertion failures but the output
    // still sounds fine, so we zero the NaNs in development builds.
    //                                    (28.11.2011.) (Domagoj Saric)
    for (float &phase : LE::Utility::makeSpan(pPhases, numberOfElements))
    {
        if (!std::isfinite(phase))
            phase = 0;
    }
#endif // TARGET_OS_IPHONE
#else
    for (std::uint16_t element(0); element < numberOfElements; ++element)
    {
        float const real(pReals[element]);
        float const imag(pImags[element]);
        pAmplitudes[element] = std::hypot(real, imag);
        pPhases[element] = std::atan2(imag, real);
    }
#endif // LE_MATH_USE_ACC

    LE_MATH_VERIFY_VALUES(
        Math::InvalidOrSlow | Negative,
        LE::Utility::Span<float const>(pAmplitudes, pAmplitudes + numberOfElements), "amplitudes");
    LE_MATH_VERIFY_VALUES(Math::InvalidOrSlow,
                          LE::Utility::Span<float const>(pPhases, pPhases + numberOfElements),
                          "phases");
}

void amplitudes(float const *LE_RESTRICT const pReals, float const *LE_RESTRICT const pImags,
                float *LE_RESTRICT const pAmplitudes, float const *LE_RESTRICT const pAmplitudesEnd)
{
#if defined(LE_MATH_USE_ACC)
    DSPSplitComplex data = {const_cast<float *>(pReals), const_cast<float *>(pImags)};
    vDSP_zvabs(&data, 1, pAmplitudes, 1, pAmplitudesEnd - pAmplitudes);
#else
    for (auto index(pAmplitudesEnd - pAmplitudes); index--;)
    {
        pAmplitudes[index] = std::hypot(pReals[index], pImags[index]);
    }
#endif // LE_MATH_USE_ACC

    LE_MATH_VERIFY_VALUES(Math::InvalidOrSlow | Negative,
                          LE::Utility::Span<float const>(pAmplitudes, pAmplitudesEnd),
                          "amplitudes");
}

void polar2rectangular(float const *LE_RESTRICT const pAmplitudes,
                       float const *LE_RESTRICT const pPhases, float *LE_RESTRICT const pReals,
                       float *LE_RESTRICT const pImags, std::uint16_t const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    int const vDSPNumberOfElements(numberOfElements);
    vvsincosf(pImags, pReals, pPhases, &vDSPNumberOfElements);
    multiply(pAmplitudes, pReals, numberOfElements);
    multiply(pAmplitudes, pImags, numberOfElements);
#else
    {
        float const *LE_RESTRICT pPhase(pPhases);
        float *LE_RESTRICT pCosines(pReals);
        float *LE_RESTRICT pSines(pImags);
        auto counter(numberOfElements);
        while (counter--)
        {
            float const phase(*pPhase++);
            *pSines++ = std::sin(phase);
            *pCosines++ = std::cos(phase);
        }
    }
    multiply(pAmplitudes, pReals, numberOfElements);
    multiply(pAmplitudes, pImags, numberOfElements);
#endif // Math impl
}

void ln(float const *LE_RESTRICT const pInput, float *LE_RESTRICT const pOutput,
        unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    ::vvlogf(pOutput, pInput, &reinterpret_cast<int const &>(numberOfElements));
#else
    for (unsigned int element(0); element < numberOfElements; ++element)
        pOutput[element] = std::log(pInput[element]);
#endif // LE_MATH_USE_ACC
}

void ln(float *const pInputOutput, unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    ::vvlogf(pInputOutput, pInputOutput, &reinterpret_cast<int const &>(numberOfElements));
#else
    for (unsigned int element(0); element < numberOfElements; ++element)
        pInputOutput[element] = std::log(pInputOutput[element]);
#endif // LE_MATH_USE_ACC
}

void exp(float *const pInputOutput, unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    ::vvexpf(pInputOutput, pInputOutput, &reinterpret_cast<int const &>(numberOfElements));
#else
    for (unsigned int element(0); element < numberOfElements; ++element)
        pInputOutput[element] = std::exp(pInputOutput[element]);
#endif // LE_MATH_USE_ACC
}

void square(float *LE_RESTRICT const pInputOutput, unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    vDSP_vsq(pInputOutput, 1, pInputOutput, 1, numberOfElements);
#else
    for (unsigned int element(0); element < numberOfElements; ++element)
    {
        float const value(pInputOutput[element]);
        pInputOutput[element] = value * value;
    }
#endif // LE_MATH_USE_ACC
}

void squareRoot(float *const pInputOutput, unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    vvsqrtf(pInputOutput, pInputOutput, &reinterpret_cast<int const &>(numberOfElements));
#else
    for (unsigned int element(0); element < numberOfElements; ++element)
        pInputOutput[element] = std::sqrt(pInputOutput[element]);
#endif // LE_MATH_USE_ACC
}

float rms(float const *const pData, unsigned int const numberOfElements)
{
#if defined(LE_MATH_USE_ACC)
    float result;
    vDSP_rmsqv(const_cast<float *>(pData), 1, &result, numberOfElements);
    return result;
#else
    float sumOfSquares(0);
    for (unsigned int element(0); element < numberOfElements; ++element)
        sumOfSquares += pData[element] * pData[element];
    return std::sqrt(sumOfSquares / convert<float>(numberOfElements));
#endif // LE_MATH_USE_ACC
}

void mix(float const *const pInput1, float const *const pInput2, float *const pOutput,
         float const input1Weight, unsigned int const numberOfElements)
{
    mix(pInput1, pInput2, pOutput, pOutput + numberOfElements, input1Weight);
}

void interleave(float const *LE_RESTRICT const *LE_RESTRICT const pInputs,
                float *LE_RESTRICT pOutput, std::uint16_t const numberOfElements,
                std::uint8_t const numberOfChannels)
{
    std::uint16_t element(0);
    switch (numberOfChannels)
    {
    case 1:
        LE_UNREACHABLE_CODE();

    default:
        for (; element < numberOfElements; ++element)
        {
            for (std::uint8_t channel(0); channel < numberOfChannels; ++channel)
            {
                *pOutput++ = pInputs[channel][element];
            }
        }
    }
}

void deinterleave(float const *LE_RESTRICT pInput,
                  float *LE_RESTRICT const *LE_RESTRICT const pOutputs,
                  std::uint16_t const numberOfElements, std::uint8_t const numberOfChannels)
{
    std::uint16_t element(0);
    switch (numberOfChannels)
    {
    case 1:
        LE_UNREACHABLE_CODE();

    default:
        for (; element < numberOfElements; ++element)
        {
            for (std::uint8_t channel(0); channel < numberOfChannels; ++channel)
            {
                pOutputs[channel][element] = *pInput++;
            }
        }
    }
}

void addPolar(float const amp1, float const phase1, float &LE_RESTRICT amp2,
              float &LE_RESTRICT phase2)
{
    float real1(std::cos(phase1));
    float imag1(std::sin(phase1));
    float real2(std::cos(phase2));
    float imag2(std::sin(phase2));

    real1 *= amp1;
    imag1 *= amp1;
    real2 *= amp2;
    imag2 *= amp2;

    auto const real(real1 + real2);
    auto const imag(imag1 + imag2);

    amp2 = std::hypot(real, imag);
    phase2 = std::atan2(imag, real);
}

} // namespace LE::Math
