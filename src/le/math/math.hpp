////////////////////////////////////////////////////////////////////////////////
///
/// \file math.hpp
/// --------------
///
/// Generic math routine collection.
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef math_hpp__C20A5FD2_AC91_41D7_8F61_B10C67B19A6D
#define math_hpp__C20A5FD2_AC91_41D7_8F61_B10C67B19A6D
//------------------------------------------------------------------------------
#include "le/utility/platformSpecifics.hpp"

#include "le/utility/intrinsics.hpp"

#include "le/utility/staticLog2.hpp"

#if defined(__ARM_NEON__) || defined(__aarch64__)
#include "arm_neon.h"
#endif // __ARM_NEON__

#include <algorithm>
/// \note span.hpp and <string> were included only #ifndef NDEBUG, but has()
/// and verifyFPValues() are declared over Span unconditionally, so this header
/// had never compiled in a release build.
#include "le/utility/span.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <source_location>
#include <string>
#include <type_traits>
//------------------------------------------------------------------------------
namespace LE::Math
{

// http://www.strchr.com/optimized_abs_function
using std::abs;
inline std::uint32_t abs(unsigned int const value) { return value; }
inline std::uint16_t abs(short const value)
{
    return static_cast<std::uint16_t>(std::abs(static_cast<int>(value)));
}
inline std::uint16_t abs(unsigned short const value) { return value; }
inline std::uint8_t abs(unsigned char const value) { return value; }
inline std::uint8_t abs(char const value)
{
    return static_cast<std::uint8_t>(std::abs(static_cast<int>(value)));
}
std::uint8_t abs(bool value);

float copySign(float targetNumber, float signSource);

int floor(float value);
int ceil(float value);
int round(float value);
int round(double value);
int truncate(float value);

float modulo(float dividend, float divisor);
int modulo(int dividend, int divisor);
unsigned int modulo(unsigned int dividend, unsigned int divisor);

struct SplitFloat
{
    int integer;
    float fractional;
};

SplitFloat splitFloat(float value);

bool equal(float const &left, float const &right);
bool equal(float const &left, unsigned int right);
template <typename T> bool equal(T const left, T const right)
{
    static_assert(!std::is_floating_point<T>::value, "Internal inconsistency");
    return left == right;
}

bool nearEqual(float left, float right);
bool nearEqual(float left, unsigned int right);

template <typename T> bool isZero(T const value) { return equal(value, T(0)); }
bool isZero(float const &value);

template <int ComparisonValue> bool is(float const &value)
{
    static float const comparisonValue(static_cast<float>(ComparisonValue));
    return equal(value, comparisonValue);
}

bool isNegative(float value);
bool isNegative(int value);
bool isNegative(unsigned int value);

float valueIfNot(float const &value, bool condition);

namespace PositiveFloats
{
bool isGreater(float left, float right);
bool isGreater(double left, double right);

float valueIfGreater(float testValue, float lowerBound, float value);

unsigned int ceil(float value);
unsigned int floor(float value);

float modulo(float dividend, float divisor);

bool isZero(float const &value);
} // namespace PositiveFloats

namespace PowerOfTwo
{
unsigned int ceil(float);
unsigned int floor(unsigned int);
unsigned int round(unsigned int);
std::uint8_t log2(unsigned int);
} // namespace PowerOfTwo

float log2(float value);
std::uint8_t log2(int value);
std::uint8_t log2(unsigned int value);
std::uint8_t log2(unsigned long value);

float ln(float);
float log10(float);
float exp(float);
float exp2(float);

void addPolar(float amp1, float phase1, float &amp2, float &phase2);

float clamp(float value, float lowerBound, float upperBound);
std::uint64_t clamp(std::int64_t value, std::uint64_t lowerBound, std::uint64_t upperBound);
std::uint32_t clamp(std::int32_t value, std::uint32_t lowerBound, std::uint32_t upperBound);
std::uint16_t clamp(std::int16_t value, std::uint16_t lowerBound, std::uint16_t upperBound);
std::uint8_t clamp(std::int8_t value, std::uint8_t lowerBound, std::uint8_t upperBound);

std::uint16_t clamp(std::uint16_t value, std::uint16_t lowerBound, std::uint16_t upperBound);

std::uint8_t firstSetBit(int);
std::uint8_t lastSetBit(int);
std::uint8_t numberOfSetBits(int);

bool isPowerOfTwo(int);
bool isPowerOfTwo(unsigned int);
bool isPowerOfTwo(float);

template <unsigned int value>
struct IsPowerOfTwo : std::integral_constant<bool, (1 << LE::Utility::staticLog2(value)) == value>
{
};

inline bool isNormalisedValue(float const value) { return (value >= 0) && (value <= 1); }

////////////////////////////////////////////////////////////////////////////////
///
/// \class Rng
///
/// \brief xorshift128+, as a value rather than as a hidden global.
///
///   These were free functions over a file-static `rng_state[2]`, and the
/// global was two bugs. The audible one: the engine runs every hop of channel 0
/// before channel 1 starts (`Processor::process`), so cutting a host block into
/// hop-sized calls does not change how many numbers are drawn but does change
/// *which channel gets which*. Freqverb and Whisperer therefore rendered
/// differently depending on the host's block size, for no reason a listener
/// could name. \see issue #86 and core/chunkTransparencyTests.cpp.
///
///   The other: two plugin instances on two audio threads read-modify-wrote
/// those two words with nothing between them, which is a data race and not
/// merely a shared sequence.
///
/// \note An instance per *channel* is what fixes the first -- one stream per
/// channel of one effect, advanced only by that channel's own hops, so the
/// number of `process()` calls cannot reach it. They live in the effects'
/// ChannelState objects, which is the per-channel thing an effect is already
/// handed. \see Engine::ModuleDSP::seedRandomState().
///
/// \note **Constructs seeded, from entropy.** A fixed default state would have
/// been the quiet version of the bug this class exists to remove: the engine
/// deals streams at `reset()`, and a module inserted into a running chain is
/// resized and reset without one -- so every instance that did that would have
/// been drawing the same "random" numbers as every other. An unseeded generator
/// must never be a shared constant. Deal over it whenever determinism is wanted;
/// that is what `seed()` is for.
///
////////////////////////////////////////////////////////////////////////////////

class Rng
{
  public:
    Rng() noexcept { seedFromEntropy(); }

    /// \brief splitmix64, so that a small seed still fills both words.
    void seed(std::uint64_t seed) noexcept;

    /// \brief The clock and this object's own address -- distinct per generator
    /// by construction, and distinct per run. Seeding from the clock is what has
    /// always kept two instances on two tracks from producing the same noise.
    /// \see issue #105, which is about offering the other choice.
    void seedFromEntropy() noexcept;

    /// \brief A raw draw, which is also what one generator uses to seed another.
    std::uint64_t next() noexcept;

    float normalised() noexcept; ///< [0, 1]

    float ranged(float maximum) noexcept;                 ///< [0, maximum]
    float ranged(float minimum, float maximum) noexcept;  ///< [minimum, maximum]
    std::uint32_t ranged(std::uint32_t maximum) noexcept; ///< [0, maximum)
    std::uint16_t ranged(std::uint16_t maximum) noexcept; ///< [0, maximum)
    std::int32_t ranged(std::int32_t minimum, std::uint32_t maximum) noexcept;

    /// \note A ChannelState holding one of these declares it as a plain member
    /// and leaves it out of `members()`: it owns no engine storage, and a
    /// stream that restarted from the top on every transport stop would be a
    /// repeating noise pattern rather than a reset one. Seeding is the engine's
    /// job, at a moment it chooses.

  private:
    std::uint64_t state_[2];
}; // class Rng

template <class UnsignedInteger>
UnsignedInteger roundUpUnsignedIntegerDivision(UnsignedInteger const dividend,
                                               UnsignedInteger const divisor)
{
    static_assert(std::is_unsigned<UnsignedInteger>::value, "");
    return (dividend + (divisor - 1)) / divisor;
}

/// \note There is no denormal or FPU-exception guard here. Denormal flushing is
/// `sst::plugininfra::cpufeatures::FPUStateGuard`, taken once at the top of
/// `SpectrumWorxCLAP::process()` -- one guard, at the outermost point of the
/// callback, covering both architectures.

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The magnitude `Above100dB` draws the line at: 100 dB over unity.
///
/// \note A bound rather than a class, and the one thing every other enumerator
/// here cannot see. Uninitialised memory read as float is overwhelmingly *huge
/// and finite* -- the value that reached the FFT from an unconnected AUv2 bus
/// measured 2.9e33 -- so it passes every finiteness guard on the input path and
/// only becomes a NaN three layers later, inside `vDSP_zvabs`, where the
/// assertion that finally fires names the wrong thing. That cost a day.
///
/// \note 100 dB is chosen to be far above anything a signal path produces and
/// far below what garbage reads as. A host may legitimately hand a plugin
/// something well over 0 dBFS; it will not hand it 1e5.
///
////////////////////////////////////////////////////////////////////////////////

float constexpr hundredDecibels{1e5f};

enum FPClass
{
    SignalingNaN = 1 << 0,
    QuietNaN = 1 << 1,
    Infinity = 1 << 3,
    Positive = 1 << 4,
    Negative = 1 << 5,
    Normalised = 1 << 6,
    Denormalised = 1 << 7,
    Zero = 1 << 8,
    /// \note Spelled as the violation, like every other enumerator here: a mask
    /// is the list of things the range must *not* contain, so "no value is above
    /// 100 dB" is the same statement as "all values are within +/- 100 dB".
    Above100dB = 1 << 9,

    NaN = SignalingNaN | QuietNaN,
    Invalid = NaN | Infinity,
    InvalidOrSlow = Invalid | Denormalised,
    /// \brief What a buffer arriving from outside the engine has to be.
    ImplausibleAudio = InvalidOrSlow | Above100dB
};

template <unsigned FPClasses>
unsigned int LE_NOINLINE has(float const *LE_RESTRICT pRange, std::size_t rangeSize)
{
    unsigned int result(0);

    while (rangeSize-- && (result != FPClasses))
    {
        /// \note To handle the case where user input data contains denormals
        /// and the fpclassify function handles denormals while we have them
        /// disabled we also check how the value compares to zero if the
        /// fpclassify function detects a denormal value.
        ///                                   (24.05.2016.) (Domagoj Saric)
        auto const value(*pRange++);
        auto const valueNonZero(value != 0);
#ifdef _MSC_VER
        switch (/*std*/ ::_fpclass(value))
        {
        case _FPCLASS_SNAN:
            result |= (FPClasses & SignalingNaN);
            break;
        case _FPCLASS_QNAN:
            result |= (FPClasses & QuietNaN);
            break;
        case _FPCLASS_NINF:
            result |= (FPClasses & Infinity);
            break;
        case _FPCLASS_NN:
            result |= (FPClasses & (Negative | Normalised));
            break;
        case _FPCLASS_ND:
            result |= (FPClasses & (Negative | (Denormalised * valueNonZero)));
            break;
        case _FPCLASS_NZ:
            result |= (FPClasses & (/*Negative |*/ Zero));
            break; //...mrmlj...Talking Wind...
        case _FPCLASS_PZ:
            result |= (FPClasses & (Positive | Zero));
            break;
        case _FPCLASS_PD:
            result |= (FPClasses & (Positive | (Denormalised * valueNonZero)));
            break;
        case _FPCLASS_PN:
            result |= (FPClasses & (Positive | Normalised));
            break;
        case _FPCLASS_PINF:
            result |= (FPClasses & Infinity);
            break;
        }
#else
        switch (std::fpclassify(value))
        {
#ifdef FP_NANS
        case FP_NANS:
            result |= (FPClasses & SignalingNaN);
            break;
#endif // FP_NANS
        case FP_NAN:
            result |= (FPClasses & QuietNaN);
            break;
        case FP_INFINITE:
            result |= (FPClasses & Infinity);
            break;
        case FP_ZERO:
            result |= (FPClasses & Zero);
            break;
        case FP_SUBNORMAL:
            if (valueNonZero)
                result |= (FPClasses & Denormalised);
            break;
        case FP_NORMAL:
            result |= (FPClasses & Normalised);
            break;
        }
        if (std::signbit(value) &&
            /*...mrmlj...Talking Wind...*/ value < -std::numeric_limits<float>::epsilon())
            result |= (FPClasses & Negative);
        else
            result |= (FPClasses & Positive);
#endif // _MSC_VER
        /// \note Finite values only. A NaN and an infinity have their own bits
        /// and every guard on the input path already carries them; this one is
        /// for the value that passes all of those, which is the whole reason it
        /// exists.
        if constexpr (FPClasses & Above100dB)
            if (std::isfinite(value) && (std::fabs(value) > hundredDecibels))
                result |= Above100dB;
    }

    return result;
}

template <unsigned FPClasses> unsigned int has(LE::Utility::Span<float const> const &range)
{
    return has<FPClasses>(range.begin(), range.size());
}

template <unsigned FPClasses>
void verifyFPValues(
    [[maybe_unused]] float const *const pRange, [[maybe_unused]] std::size_t const rangeSize,
    [[maybe_unused]] char const *const valueName,
    [[maybe_unused]] std::source_location const &location = std::source_location::current())
{
#ifndef NDEBUG
#ifdef LE_ENABLE_ASSERT_HANDLER
#define LE_AUX_VERIFY_FP_VALUES_FAILURE(...) LE::Utility::assertionFailed(__VA_ARGS__)
#else
#define LE_AUX_VERIFY_FP_VALUES_FAILURE(valueName, errorString, location)                          \
    LE_ASSERT_MSG(false, (std::string(errorString " @ ") + valueName).c_str())
#endif // LE_ENABLE_ASSERT_HANDLER
    unsigned int const fpClasses(has<FPClasses>(pRange, rangeSize));
    if (fpClasses & FPClasses & NaN)
        LE_AUX_VERIFY_FP_VALUES_FAILURE(valueName, "NaN value found", location);
    if (fpClasses & FPClasses & Infinity)
        LE_AUX_VERIFY_FP_VALUES_FAILURE(valueName, "Infinite value found", location);
    if (fpClasses & FPClasses & Positive)
        LE_AUX_VERIFY_FP_VALUES_FAILURE(valueName, "Positive value found", location);
    if (fpClasses & FPClasses & Negative)
        LE_AUX_VERIFY_FP_VALUES_FAILURE(valueName, "Negative value found", location);
    if (fpClasses & FPClasses & Normalised)
        LE_AUX_VERIFY_FP_VALUES_FAILURE(valueName, "Normalised value found", location);
    if (fpClasses & FPClasses & Denormalised)
        LE_AUX_VERIFY_FP_VALUES_FAILURE(valueName, "Denormalised value found", location);
    if (fpClasses & FPClasses & Zero)
        LE_AUX_VERIFY_FP_VALUES_FAILURE(valueName, "Zero value found", location);
    if (fpClasses & FPClasses & Above100dB)
        LE_AUX_VERIFY_FP_VALUES_FAILURE(valueName, "Value beyond +/- 100 dB found", location);
#undef LE_AUX_VERIFY_FP_VALUES_FAILURE
#endif // NDEBUG
}

template <unsigned FPClasses>
void verifyFPValues(LE::Utility::Span<float const> const &range, char const *const valueName,
                    std::source_location const &location = std::source_location::current())
{
    return verifyFPValues<FPClasses>(range.begin(), range.size(), valueName, location);
}

#ifdef NDEBUG
#ifndef LE_MATH_VERIFY_VALUES // required for unity builds
#define LE_MATH_VERIFY_VALUES(fpClasses, range, valueName) (void(0))
#endif // LE_MATH_VERIFY_VALUES
#else
#define LE_MATH_VERIFY_VALUES(fpClasses, range, valueName)                                         \
    /*::LE::*/ Math::verifyFPValues<fpClasses>(range, valueName)
#endif // NDEBUG

////////////////////////////////////////////////////////////////////////////////
/// Force-inlined implementations for functions that MSVC10 does not inline when
/// optimizing for size.
////////////////////////////////////////////////////////////////////////////////

LE_FORCEINLINE float clamp(float const value, float const lowerBound, float const upperBound)
{
    /// \note Unqualified min/max here only ever found LE::Math's pointer-range
    /// overloads — this branch had never been compiled.
    return std::min(std::max(value, lowerBound), upperBound);
}

LE_FORCEINLINE float copySign(float const targetNumber, float const signSource)
{
    // http://stackoverflow.com/questions/2922619/how-to-efficiently-compare-the-sign-of-two-floating-point-values-while-handling-n

#if defined(__GNUC__)
    float const result(__builtin_copysignf(targetNumber, signSource));
#else
    int const targetNumberAbsoluteBits(reinterpret_cast<int const &>(targetNumber) & 0x7FFFFFFF);
    int const signSourceSignBit(reinterpret_cast<int const &>(signSource) & 0x80000000);
    int const resultBits(targetNumberAbsoluteBits | signSourceSignBit);

    float const result(reinterpret_cast<float const &>(resultBits));
#endif // __GNUC__

#ifdef _MSC_VER
    LE_ASSERT(result == /*std*/ ::_copysign(targetNumber, signSource));
#else
    LE_ASSERT(result == /*std*/ ::copysign(targetNumber, signSource));
#endif // _MSC_VER
    return result;
}

LE_FORCEINLINE float valueIfNot(float const &value, bool const condition)
{
    auto const mask(static_cast<std::uint32_t>(static_cast<std::uint8_t>(condition) - 1));
    union
    {
        std::uint32_t bits;
        float value;
    } resultBits;
    resultBits.value = value;
    resultBits.bits &= mask;
    float const result(resultBits.value);
    LE_ASSERT(result == (condition ? 0 : value));
    return result;
}

////////////////////////////////////////////////////////////////////////////////
//
// round()
// -------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \ingroup TypeConversion Type conversion
/// \brief Rounds a floating point value to the nearest integer.
///   Faster than a simple static_cast as it bypasses hidden ftol() calls/FPU
/// setup code inserted by the (MSVC) compiler.
///
/// \throws nothing
///
////////////////////////////////////////////////////////////////////////////////
/// \todo Investigate:
///    - boost::numeric_cast<>
///    - http://ldesoras.free.fr/doc/articles/rounding_en.pdf
///    - http://www.mega-nerd.com/FPcast
///    - http://www.codeproject.com/KB/cpp/floatutils.aspx
///    - http://www.stereopsis.com/FPU.html
///    - http://stackoverflow.com/questions/2550281/floating-point-vs-integer-calculations-on-modern-hardware
///    - http://chrishecker.com/Miscellaneous_Technical_Articles#Floating_Point
///    - http://stackoverflow.com/questions/78619/what-is-the-fastest-way-to-convert-float-to-int-on-x86
///    - http://www.devmaster.net/forums/showthread.php?t=10153
///    - http://software.intel.com/en-us/articles/fast-floating-point-to-integer-conversions
///    - http://stereopsis.com/sree/fpu2006.html
///    - http://chrishecker.com/images/f/fb/Gdmfp.pdf
///    - http://www.cs.uaf.edu/2009/fall/cs301/lecture/12_09_float_to_int.html
///                                           (14.12.2009.) (Domagoj Saric)
/// \todo Implement float->float rounding (it could speed up the phase vocoder/
/// mapTo2Pi function).
///                                           (19.02.2016.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

LE_FORCEINLINE std::int32_t round(float const floatingPointValue)
{
#if defined(_MSC_VER)
#ifdef _XBOX
    return __frnd(floatingPointValue);
#else
    /// \note std::lrintf is the same operation under the same rounding mode as
    /// the x86-32 fld/fistp this replaces and as the __builtin_lrintf arm below:
    /// both follow the current mode, and nothing here changes it from the
    /// default nearest-even.
    return static_cast<std::int32_t>(std::lrintf(floatingPointValue));
#endif
#elif defined(__GNUC__)
    /// \note Neither Clang nor GCC inline a call to __builtin_lrintf (at
    /// least when targeting the ARM).
    ///                                   (02.11.2012.) (Domagoj Saric)
    return static_cast<std::int32_t>(::__builtin_lrintf(floatingPointValue));
#endif // _MSC_VER
}

LE_FORCEINLINE int round(double const floatingPointValue)
{
#if defined(__GNUC__)
    return static_cast<int>(::__builtin_lrint(floatingPointValue));
#elif defined(_XBOX)
    return __frnd(floatingPointValue);
#elif defined(_MSC_VER)
    /// \note MSVC fell through to the magic-number union below -- which carried
    /// an __asm cross-check of its own result, in a debug build, on an
    /// architecture where MSVC cannot assemble it. std::lrint is the same
    /// rounding under the same mode and needs neither.
    return static_cast<int>(std::lrint(floatingPointValue));
#elif 1 //...mrmlj...was LE_LITTLE_ENDIAN; the union below is byte-order dependent
    double const magic((1ULL << 52) * 1.5);
    union
    {
        double asDouble;
        int asInteger;
    } bits = {floatingPointValue + magic};
    return bits.asInteger;
#endif
}

////////////////////////////////////////////////////////////////////////////////
//
// truncate()
// ----------
//
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   See the notes for round().
//                                            (06.12.2010.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

LE_FORCEINLINE int truncate(float const floatingPointValue)
{
    /// \note The upper bound used to read std::numeric_limits<int>::max(),
    /// which no float can hold: it converts to 2147483648.0f, one more than it
    /// says, and the compiler said so. Written as the constant the comparison
    /// was already made against. The lower bound stays a limits call because
    /// -2^31 is a power of two and converts exactly.
    LE_ASSERT_MSG(floatingPointValue < 2147483648.0f, "Float out of int range.");
    LE_ASSERT_MSG(floatingPointValue > std::numeric_limits<int>::min(), "Float out of int range.");
    return static_cast<int>(floatingPointValue);
}

namespace PositiveFloats
{
LE_FORCEINLINE bool isGreater(float const left, float const right) { return left > right; }

LE_FORCEINLINE bool isGreater(double const left, double const right) { return left > right; }

// Implementation note:
//   This function is required because MSVC10 is unable to generate good
// code if the calling code simply uses the isGreater() and
// fast_bool_t::mask() functions directly.
//                                        (01.12.2011.) (Domagoj Saric)
LE_FORCEINLINE float valueIfGreater(float const testValue, float const lowerBound,
                                    float const value)
{
    auto const isGreater_(isGreater(testValue, lowerBound));
    return isGreater_ ? value : 0;
}
} // namespace PositiveFloats

} // namespace LE::Math

#endif // math_hpp
