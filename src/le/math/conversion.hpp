////////////////////////////////////////////////////////////////////////////////
///
/// \file conversion.hpp
/// --------------------
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef conversion_hpp__768D47D6_2B13_4447_9C52_FBD4473F3850
#define conversion_hpp__768D47D6_2B13_4447_9C52_FBD4473F3850
//------------------------------------------------------------------------------
#include "math.hpp"

#include "le/utility/platformSpecifics.hpp"

#include "le/utility/assert.hpp"
#include <bit>
#include "le/utility/staticLog2.hpp"

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace LE::Math
{

////////////////////////////////////////////////////////////////////////////////
//
// Type conversion.
// ----------------
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// convert()
// ---------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \ingroup TypeConversion Type conversion
/// \brief Performs optimal conversion between numeric data types.
/// \throws nothing
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   The single template parameter versions have to come before the two
// parameter versions, otherwise Clang issues compiler warnings when we try to
// specialize the single parameter version for bools. We also need to define
// them immediately otherwise Clang would issue the same error in the .cpp file
// (because by then it would see the two parameter version).
//                                            (07.07.2011.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

template <typename Target> Target convert(float const source)
{
    return static_cast<Target>(Math::round(source));
}
template <typename Target> Target convert(double const source)
{
    return static_cast<Target>(Math::round(source));
}

namespace Detail
{
bool convertToBool(float);
bool convertToBool(double);
} // namespace Detail

template <> inline bool convert<bool>(float const source) { return Detail::convertToBool(source); }
template <> inline bool convert<bool>(double const source) { return Detail::convertToBool(source); }

template <> inline float convert<float>(float const source) { return source; }
template <> inline double convert<double>(double const source) { return source; }
template <> inline double convert<double>(float const source) { return source; }
template <> inline float convert<float>(double const source) { return static_cast<float>(source); }

namespace Detail
{
template <typename T> struct MakeSigned : std::make_signed<T>
{
};

template <> struct MakeSigned<float>
{
    using type = float;
};
template <> struct MakeSigned<double>
{
    using type = double;
};
template <> struct MakeSigned<bool>
{
    using type = char;
};
} // namespace Detail

template <typename Target, typename Source>
typename std::enable_if<
    !(std::is_same<Target, Source>::value || !std::is_fundamental<Source>::value), Target>::type
convert(Source const source)
{
    // Implementation note:
    //   In order to simplify generated x86 code for integer-to-float
    // conversions (avoiding the fild-jns-fadd instruction sequence) we first
    // cast all 32+bit source integer values to their signed counterparts.
    // We are safe to do this as we do not use values even close to the upper
    // limit of a signed int (INT_MAX).
    //                                        (22.03.2011.) (Domagoj Saric)
    using ConversionIntermediateType =
        typename std::conditional<std::is_unsigned<Source>::value &&
                                      (sizeof(Source) >= sizeof(int)),
                                  typename Detail::MakeSigned<Source>::type, Source>::type;

    Target const result(static_cast<Target>(static_cast<ConversionIntermediateType>(source)));
    LE_ASSERT(result == static_cast<Target>(source));
    return result;
}

template <> inline bool convert<bool, std::uint32_t>(std::uint32_t const source)
{
    return source != 0;
}
template <> inline bool convert<bool, std::int32_t>(std::int32_t const source)
{
    return source != 0;
}

template <typename TargetSource> TargetSource const &convert(TargetSource const &source)
{
    return source;
}

template <typename Target, typename Source> void convert(Source const source, Target &target)
{
    target = convert<Target>(source);
}

template <typename Target, typename Source>
typename std::enable_if<std::is_enum<Source>::value, Target>::type convert(Source const source)
{
    return convert<Target>(static_cast<unsigned int>(source));
}

template <typename Target, typename Source>
Target convert(Source const source, typename Source::value_type const * = nullptr)
{
    return convert<Target>(static_cast<typename Source::value_type>(source));
}

////////////////////////////////////////////////////////////////////////////////
//
// Unit conversion.
// ----------------
//
////////////////////////////////////////////////////////////////////////////////

float dB2NormalisedLinear(float dBValue);
float dB2NormalisedLinear(std::int8_t dBValue);
float dB2NormalisedLinear(std::uint8_t dBValue);
float dB2NormalisedLinear(std::int16_t dBValue);
float dB2NormalisedLinear(std::uint16_t dBValue);
inline float dB2NormalisedLinear(std::int32_t dBValue)
{
    return dB2NormalisedLinear(static_cast<std::int16_t>(dBValue));
}

float normalisedLinear2dB(float linearNormalisedValue);
double normalisedLinear2dB(double linearNormalisedValue);

float normalisedPower2dB(float linearNormalisedValue);
float dB2NormalisedPower(float dBValue);

float normalisedLinear2Percentage(float normalisedFloatValue);
double normalisedLinear2Percentage(double normalisedFloatValue);

template <typename T> float percentage2NormalisedLinear(T const percentage)
{
    return percentage / 100.0f;
}

float semitone2Interval12TET(float semitones);
float cents2Interval12TET(float cents);
float cents2Interval12TET(std::int16_t cents);
float octaves2Interval12TET(std::int8_t octaves);
float interval12TET2Semitone(float interval);

////////////////////////////////////////////////////////////////////////////////
//
// Type and range conversion.
// --------------------------
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// isValueInRange()
// ----------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \ingroup TypeAndRangeConversion Type and range conversion
/// \brief Checks whether a given value falls into the given closed range.
/// Allows a small epsilon for floating point values.
/// \throws nothing
///
////////////////////////////////////////////////////////////////////////////////

template <typename T>
typename std::enable_if<std::is_floating_point<T>::value, bool>::type
isValueInRange(T const value, T const rangeMinimum, T const rangeMaximum)
{
    // Implementation note:
    //   To avoid false assertion failures caused by floating point precision
    // errors we expand the specified range with a small epsilon constant in
    // assert-enabled builds.
    //                                        (14.07.2010.) (Domagoj Saric)
#ifndef NDEBUG
    T const maximumAllowedRoundingErrorEpsilon(
        static_cast<T>((sizeof(T) > sizeof(float)) ? 0.000000001 : 0.00001));
    return (value >= (rangeMinimum - maximumAllowedRoundingErrorEpsilon)) &&
           (value <= (rangeMaximum + maximumAllowedRoundingErrorEpsilon));
#else
    return (value >= rangeMinimum) && (value <= rangeMaximum);
#endif // NDEBUG
}

template <typename T>
typename std::enable_if<!std::is_floating_point<T>::value, bool>::type
isValueInRange(T const value, T const rangeMinimum, T const rangeMaximum)
{
    return (value >= rangeMinimum) && (value <= rangeMaximum);
}

////////////////////////////////////////////////////////////////////////////////
//
// convertLinearRange()
// --------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \ingroup TypeAndRangeConversion Type and range conversion
/// \brief Maps a value from the source range to the target range. Both ranges
/// are assumed to be linear.
/// \throws nothing
///
////////////////////////////////////////////////////////////////////////////////
/// \todo Try to come to an agreement about which parameter should come first,
/// the target or the source.
///                                           (17.02.2010.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

template <typename Target, int targetRangeOffset, unsigned int targetRangeSize,
          unsigned int targetRangeScaleFactor, typename Source, int sourceRangeOffset,
          unsigned int sourceRangeSize, unsigned int sourceRangeScaleFactor>
Target convertLinearRange(Source const sourceValue)
{
    Source const scaledSourceOffset(convert<Source>(sourceRangeOffset) / sourceRangeScaleFactor);
    Source const scaledSourceRange(convert<Source>(sourceRangeSize) / sourceRangeScaleFactor);

    LE_ASSERT(isValueInRange<Source>(sourceValue, scaledSourceOffset,
                                     scaledSourceOffset + scaledSourceRange));

    using SignedSource = typename Detail::MakeSigned<Source>::type;
    SignedSource const unoffsetSource(sourceValue - scaledSourceOffset);

    Target const scaledTargetOffset(convert<Target>(targetRangeOffset) / targetRangeScaleFactor);
    Target const scaledTargetRange(convert<Target>(targetRangeSize) / targetRangeScaleFactor);

    Target const targetValue(
        convert<Target>(unoffsetSource * scaledTargetRange / scaledSourceRange) +
        scaledTargetOffset);

    LE_ASSERT(isValueInRange<Target>(targetValue, scaledTargetOffset,
                                     scaledTargetOffset + scaledTargetRange));

    return targetValue;
}

template <typename Target, int targetRangeOffset, unsigned int targetRangeSize,
          unsigned int targetRangeScaleFactor, typename Source>
Target convertLinearRange(Source const sourceValue, Source const sourceMinimum,
                          Source const sourceMaximum)
{
    Target const scaledTargetMinimum(convert<Target>(targetRangeOffset) / targetRangeScaleFactor);
    Target const scaledTargetRange(convert<Target>(targetRangeSize) / targetRangeScaleFactor);

    LE_ASSERT(sourceMinimum < sourceMaximum);
    LE_ASSERT(isValueInRange<Source>(sourceValue, sourceMinimum, sourceMaximum));

    using SignedSource = typename Detail::MakeSigned<Source>::type;
    SignedSource const scaledSourceRange(sourceMaximum - sourceMinimum);
    SignedSource const unoffsetSource(sourceValue - sourceMinimum);

    Target const targetValue(
        convert<Target>(unoffsetSource * scaledTargetRange / scaledSourceRange) +
        scaledTargetMinimum);

    LE_ASSERT(isValueInRange<Target>(targetValue, scaledTargetMinimum,
                                     scaledTargetMinimum + scaledTargetRange));

    return targetValue;
}

template <typename Target, typename Source, int sourceRangeOffset, unsigned int sourceRangeSize,
          unsigned int sourceRangeScaleFactor>
Target convertLinearRange(Source const sourceValue, Target const targetMinimum,
                          Target const targetMaximum)
{
    Source const scaledSourceOffset(convert<Source>(sourceRangeOffset) /
                                    convert<Source>(sourceRangeScaleFactor));
    Source const scaledSourceRange(convert<Source>(sourceRangeSize) /
                                   convert<Source>(sourceRangeScaleFactor));

    LE_ASSERT(isValueInRange<Source>(sourceValue, scaledSourceOffset,
                                     scaledSourceOffset + scaledSourceRange));

    LE_ASSERT(targetMinimum <= targetMaximum);

    using SignedTarget = typename Detail::MakeSigned<Target>::type;
    SignedTarget const scaledTargetOffset(targetMinimum);
    SignedTarget const scaledTargetRange(targetMaximum - targetMinimum);

    Target const targetValue(static_cast<Target> // required for enums
                             (convert<Target>((sourceValue - scaledSourceOffset) *
                                              scaledTargetRange / scaledSourceRange) +
                              scaledTargetOffset));

    LE_ASSERT(isValueInRange<Target>(targetValue, targetMinimum, targetMaximum));

    return targetValue;
}

////////////////////////////////////////////////////////////////////////////////
//
// convertPowerOfTwo2LinearRange()
// -------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \ingroup TypeAndRangeConversion Type and range conversion
/// \brief Maps a value from the source, "power-of-two" non-linear, range to the
/// target, linear, range.
/// \throws nothing
///
////////////////////////////////////////////////////////////////////////////////

template <typename Target, int targetRangeOffset, unsigned int targetRangeSize,
          unsigned int targetRangeScaleFactor, unsigned int sourceMinimum,
          unsigned int sourceMaximum>
Target convertPowerOfTwo2LinearRange(unsigned int const sourceValue)
{
    //...mrmlj...ugh...clean this up asap...
    bool const normalised(targetRangeOffset == 0 && targetRangeSize == 1 &&
                          targetRangeScaleFactor == 1);

    LE_ASSERT(isPowerOfTwo(sourceValue));
    unsigned int const minimumExponent(LE::Utility::staticLog2(sourceMinimum));
    unsigned int const exponentRange(LE::Utility::staticLog2(sourceMaximum) - minimumExponent);
    unsigned int const valueExponent(PowerOfTwo::log2(sourceValue));
    LE_ASSERT(minimumExponent == PowerOfTwo::log2(sourceMinimum));
    return convertLinearRange<
        Target, normalised ? targetRangeOffset : 0, normalised ? targetRangeSize : exponentRange,
        normalised ? targetRangeScaleFactor : 1, unsigned int, minimumExponent, exponentRange, 1>(
        valueExponent);
}

////////////////////////////////////////////////////////////////////////////////
//
// convertLinearRange2PowerOfTwo()
// -------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \ingroup TypeAndRangeConversion Type and range conversion
/// \brief Maps a value from the source, linear, range to the target,
/// "power-of-two" non-linear, range.
/// \throws nothing
///
////////////////////////////////////////////////////////////////////////////////

template <unsigned int targetMinimum, unsigned int targetMaximum, typename Source,
          int sourceRangeUnscaledOffset, unsigned int sourceRangeUnscaledSize,
          unsigned int sourceRangeScaleFactor>
unsigned int convertLinearRange2PowerOfTwo(Source const sourceValue)
{
    std::uint8_t const minimumExponent(LE::Utility::staticLog2(targetMinimum));
    std::uint8_t const maximumExponent(LE::Utility::staticLog2(targetMaximum));
    std::uint8_t const exponentRange(maximumExponent - minimumExponent);

    //...mrmlj...ugh...clean this up asap...
    bool const normalised(sourceRangeUnscaledOffset == 0 && sourceRangeUnscaledSize == 1 &&
                          sourceRangeScaleFactor == 1);

    static_assert(sourceRangeUnscaledOffset % sourceRangeScaleFactor == 0,
                  "Internal inconsistency");
    static_assert(sourceRangeUnscaledSize % sourceRangeScaleFactor == 0, "Internal inconsistency");
    std::uint16_t const sourceOffset(
        normalised ? (sourceRangeUnscaledOffset / sourceRangeScaleFactor) : 0);
    std::uint16_t const sourceRange(normalised ? (sourceRangeUnscaledSize / sourceRangeScaleFactor)
                                               : exponentRange);

    LE_ASSERT(isValueInRange<Source>(sourceValue, sourceOffset, sourceOffset + sourceRange));

    std::uint8_t const exponent(convert<std::uint8_t>(sourceValue * exponentRange / sourceRange) -
                                (sourceOffset - minimumExponent));

    LE_ASSERT(exponent >= minimumExponent);
    LE_ASSERT(exponent <= maximumExponent);

    unsigned int const result(static_cast<std::uint16_t>(1U) << exponent);
    LE_ASSERT(isValueInRange<unsigned int>(result, targetMinimum, targetMaximum));
    return result;
}

} // namespace LE::Math

#endif // conversion_hpp
