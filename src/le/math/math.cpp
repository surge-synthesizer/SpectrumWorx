////////////////////////////////////////////////////////////////////////////////
///
/// math.cpp
/// --------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
// General IEEE floating point, integer arithmetic, etc. information and tricks:
// http://chrishecker.com/images/f/fb/Gdmfp.pdf
// http://books.google.com/books?id=M2QYbTVd0VgC&pg=PA167&lpg=PA167&dq=game+programming+gems+IEEE&source=bl&ots=K4sV7CuLUW&sig=abo8s2JooXhBZ2VlElCJgv9wOg8&hl=en&ei=oZ85S5PmEIuImgPn9Oy6DQ&sa=X&oi=book%5Fresult&ct=result&resnum=2&ved=0CA8Q6AEwAQ#v=onepage&q=game%20programming%20gems%20IEEE&f=false
// http://musicdsp.org/archive.php?classid=5#273
// http://locklessinc.com/articles/sat_arithmetic
// http://pandorawiki.org/Floating_Point_Optimization
// http://www.altdevblogaday.com/2012/05/20/thats-not-normalthe-performance-of-odd-floats
// http://dbp-consulting.com/StrictAliasing.pdf
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "math.hpp"

#include "le/math/constants.hpp"
#include "le/math/conversion.hpp"
#include "le/utility/intrinsics.hpp"
#include "le/utility/platformSpecifics.hpp"

#include "le/utility/assert.hpp"
#include <bit>
#include <cstdint>
#include <limits>

#include <cmath>
#include <chrono>

namespace LE::Math
{
/// \note Every float-as-integer read in this file goes through std::bit_cast.
/// They were `reinterpret_cast<int const &>( aFloat )`, which is not a
/// reinterpretation of the bits but a read of a float through an int lvalue --
/// a strict aliasing violation, so the compiler is entitled to assume it never
/// happens and to reorder the read against the write that produced the value.
/// GCC 15 says so at -O3 (`-Wstrict-aliasing`, seven of these); Apple Clang's
/// -O3 does not, which is how they survived. std::bit_cast is the C++20
/// spelling of what all of them meant, it is defined, and it compiles to the
/// same instruction.
#ifdef _MSC_VER
#pragma runtime_checks("", off)
#pragma check_stack(off)
#endif // _MSC_VER

std::uint8_t abs(bool const value)
{
    LE_ASSERT_MSG(value == 0 || value == 1, "Invalid input");
    return static_cast<std::uint8_t>(value);
}

namespace PositiveFloats
{
unsigned int ceil(float const value)
{
    LE_ASSUME(value >= 0);
    float const belowHalf(0.45f); //...mrmlj...
    int const result(round(value + belowHalf));
    LE_ASSERT_MSG(std::ceil(value) == static_cast<float>(result), "Unexpected result");
    return result;
}

unsigned int floor(float const value)
{
    unsigned int const result(Math::truncate(value));
    //...mrmlj...need not hold when (mis)used for fast(er) phase mapping...
    //LE_ASSERT( std::floorf( value ) == static_cast<float>( result ) );
    return result;
}

float modulo(float const dividend, float const divisor)
{
    LE_ASSUME(divisor != 0);
    //...mrmlj...signed so that it can be (mis)used for fast(er) phase mapping...
    /*unsigned*/ int const divisionFloor(truncate(dividend / divisor));
    float const mod(dividend - (divisionFloor * divisor));
    //...mrmlj...need not hold when (mis)used for fast(er) phase mapping...
    //LE_ASSERT( std::floor( value ) == static_cast<float>( result ) );
    return mod;
}

bool isZero(float const &value)
{
    LE_ASSERT_MSG(std::isfinite(value), "Invalid input");
    auto const valueBits(std::bit_cast<unsigned int>(value));
    return valueBits == 0;
}
} // namespace PositiveFloats

int floor(float const value)
{
    // http://www.masm32.com/board/index.php?PHPSESSID=df3d20eef32d75578b6e4c0bf9b44819&action=printpage;topic=9515.0
    int const truncatedValue(truncate(value));
    int const result(truncatedValue - (isNegative(value) & (truncatedValue != value)));
    LE_ASSERT_MSG(std::floor(value) == static_cast<float>(result), "Unexpected result");
    return result;
}

int ceil(float const value)
{
    // http://www.masm32.com/board/index.php?PHPSESSID=272a49f2a96ecb36c9a0b830e847c358&topic=9514.0

    float const valueX2(value * 2);
    int const result(-(round(-0.5f - valueX2) >> 1));
    LE_ASSERT_MSG(std::ceil(value) == static_cast<float>(result), "Unexpected result");
    return result;
}

// http://ompf.org/forum/viewtopic.php?f=11&t=1271
// http://mubench.sourceforge.net/results.html
float modulo(float const dividend, float const divisor)
{
    LE_ASSUME(divisor != 0);
    int const divisionFloor(floor(dividend / divisor));
    float const mod(dividend - (divisionFloor * divisor));
    // Implementation note:
    //   std::fmod() works with double precision so its internal divisionFloor
    // result can differ by one from ours when dividend / divisor is very close
    // to an integer. In these cases our routine will produce a small negative
    // mod result and will thus differ from the std::fmod() result so we skip
    // the below sanity check for those cases.
    //                                        (05.01.2011.) (Domagoj Saric)
    /// \note The reference used to be std::fmod, which truncates towards zero
    /// where this floors -- so the two differ by exactly `divisor` for every
    /// negative dividend, and the assert fired on all of them. Flooring is
    /// deliberate here (it is what makes this usable for phase mapping into
    /// [0, 2pi)), so the reference is the floored modulo, not fmod.
    /// Phasevolution was the first effect to feed it a negative dividend.
    ///
    /// \note The skip compares the two *floors*: truncation agrees where they
    /// differ, which is how -10pi mod 2pi got through it.
    ///
    /// \note The tolerance is scaled to the dividend rather than to the result.
    /// `mod` is a cancellation, so its error is an ulp of the dividend however
    /// small the remainder -- and a ULP count on the result only held where the
    /// compiler could fuse the multiply-subtract, which x86-64 cannot.
    LE_ASSERT_MSG((std::fabs(mod - static_cast<float>(std::fmod(dividend, divisor) +
                                                      ((std::fmod(dividend, divisor) != 0) &&
                                                               ((dividend < 0) != (divisor < 0))
                                                           ? divisor
                                                           : 0))) <=
                   8 * std::fabs(dividend) * std::numeric_limits<float>::epsilon()) ||
                      (divisionFloor != static_cast<int>(std::floor(static_cast<double>(dividend) /
                                                                    static_cast<double>(divisor)))),
                  "Broken modulo.");
    return mod;
}

int modulo(int const dividend, int const divisor)
{
    LE_ASSUME(divisor != 0);
    return dividend % divisor;
}

unsigned int modulo(unsigned int const dividend, unsigned int const divisor)
{
    LE_ASSUME(divisor != 0);
    return dividend % divisor;
}

std::uint32_t clamp(std::int32_t const value, std::uint32_t const lowerBound,
                    std::uint32_t const upperBound)
{
    // http://stackoverflow.com/questions/427477/fastest-way-to-clamp-a-real-fixed-floating-point-value
    // http://graphics.stanford.edu/~seander/bithacks.html#IntegerMinOrMax
    // http://www.coranac.com/documents/bittrick
    // http://stackoverflow.com/questions/707370/clean-efficient-algorithm-for-wrapping-integers-in-c

    LE_ASSERT_MSG(lowerBound <= upperBound, "Invalid input");

#if defined(_MSC_VER)
    //...mrmlj...MSVC generates bad code for the ternary operator...
    if (value < static_cast<std::int32_t>(lowerBound))
        return lowerBound;
    else if (value > static_cast<std::int32_t>(upperBound))
        return upperBound;
    return value;
#else
    return std::min<std::uint32_t>(std::max<std::int32_t>(value, lowerBound), upperBound);
#endif // _MSC_VER
}

std::uint64_t clamp(std::int64_t const value, std::uint64_t const lowerBound,
                    std::uint64_t const upperBound)
{
    return std::min<std::uint64_t>(std::max<std::int64_t>(value, lowerBound), upperBound);
}
std::uint16_t clamp(std::int16_t const value, std::uint16_t const lowerBound,
                    std::uint16_t const upperBound)
{
    return std::min<std::uint16_t>(std::max<std::int16_t>(value, lowerBound), upperBound);
}
std::uint8_t clamp(std::int8_t const value, std::uint8_t const lowerBound,
                   std::uint8_t const upperBound)
{
    return std::min<std::uint8_t>(std::max<std::int8_t>(value, lowerBound), upperBound);
}

std::uint16_t clamp(std::uint16_t const value, std::uint16_t const lowerBound,
                    std::uint16_t const upperBound)
{
    return clamp(static_cast<std::int16_t>(value), lowerBound, upperBound);
}

SplitFloat splitFloat(float const value)
{
    SplitFloat result;
    result.integer = truncate(value);
    result.fractional = value - result.integer;
    return result;
}

bool equal(float const &left, float const &right)
{
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wassume"
#endif // __clang__
    LE_ASSUME(std::isfinite(left));
    LE_ASSUME(std::isfinite(right));
#ifdef __clang__
#pragma clang diagnostic pop
#endif // __clang__

#if defined(_MSC_VER)
    return std::bit_cast<unsigned int>(left) == std::bit_cast<unsigned int>(right);
#else
    return left == right;
#endif
}

bool equal(float const &left, unsigned int const right)
{
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wassume"
#endif // __clang__
    LE_ASSUME(std::isfinite(left));
#ifdef __clang__
#pragma clang diagnostic pop
#endif // __clang__

    float const rightFloat(convert<float>(right));

    return std::bit_cast<unsigned int>(left) == std::bit_cast<unsigned int>(rightFloat);
}

bool nearEqual(float const left, float const right)
{
    // Implementation note:
    //   See the previously listed float comparison related links.
    //                                        (05.01.2011.) (Domagoj Saric)

    int const leftBits(std::bit_cast<int>(left));
    int const rightBits(std::bit_cast<int>(right));

    int const lexicographicallyOrderedLeftArray[2] = {leftBits,
                                                      static_cast<int>(0x80000000 - leftBits)};
    int const lexicographicallyOrderedRightArray[2] = {rightBits,
                                                       static_cast<int>(0x80000000 - rightBits)};

    int const lexicographicallyOrderedLeft(lexicographicallyOrderedLeftArray[isNegative(left)]);
    int const lexicographicallyOrderedRight(lexicographicallyOrderedRightArray[isNegative(right)]);

    /// \todo Reinvestigate this number and choose and document a meaningful
    /// value for our purposes.
    ///                                       (06.12.2010.) (Domagoj Saric)
    int const maximumDifferenceInULPs(3000);
    return Math::abs(lexicographicallyOrderedLeft - lexicographicallyOrderedRight) <
           maximumDifferenceInULPs;
}

bool nearEqual(float const left, unsigned int const right)
{
    float const rightFloat(convert<float>(right));

    int const leftBits(std::bit_cast<int>(left));
    int const rightBits(std::bit_cast<int>(rightFloat));

    int const lexicographicallyOrderedLeftArray[2] = {leftBits,
                                                      static_cast<int>(0x80000000 - leftBits)};

    int const lexicographicallyOrderedLeft(lexicographicallyOrderedLeftArray[isNegative(left)]);
    int const lexicographicallyOrderedRight(rightBits);

    /// \todo Reinvestigate this number and choose and document a meaningful
    /// value for our purposes.
    ///                                       (06.12.2010.) (Domagoj Saric)
    int const maximumDifferenceInULPs(3000);
    return Math::abs(lexicographicallyOrderedLeft - lexicographicallyOrderedRight) <
           maximumDifferenceInULPs;
}

bool isZero(float const &value)
{
    //...mrmlj...avoid going to the SIMD unit...
    auto const valueAbsoluteBits(std::bit_cast<unsigned int>(value) & 0x7FFFFFFFu);
    auto const positive(std::bit_cast<float>(valueAbsoluteBits));
    return PositiveFloats::isZero(positive);
}

bool isNegative(float const value)
{
    static_assert(sizeof(int) == sizeof(float), "Unexpected data sizes");
    auto const result(isNegative(std::bit_cast<int>(value)));
    LE_ASSERT_MSG((result == (value < 0)) || (value == -0.0f), "Unexpected result");
    return result;
}

bool isNegative(int const value)
{
    std::uint8_t const valueNumberOfBits(sizeof(value) * 8);
    std::uint8_t const valueSign(std::bit_cast<unsigned int>(value) >> (valueNumberOfBits - 1));
    LE_ASSERT_MSG(valueSign == (value < 0), "Unexpected result");
    return valueSign != 0;
}

bool isNegative(unsigned int /*value*/) { return false; }

float ln(float const value) { return std::log(value); }
/// \note `std::log2`, like its four neighbours, and previously
///
///     #if defined(__GNUC__) && !defined(__ANDROID__)
///         return ::__builtin_log2f(value);
///     #else
///         return /*std*/ ::log2(value) / LE::Math::Constants::ln2;
///     #endif
///
/// -- where the `#else` is wrong and always was. `::log2` is already base two,
/// so dividing by ln2 multiplies by 1/ln2: log2(8) came back 4.328 and
/// log2(0.5) came back -1.443. The commented-out `std` is the fossil of it; the
/// divisor belongs to a natural log, which is what the call must once have been.
///
///   Live callers: interval12TET2Semitone() -- so every pitch effect --
/// musicalScales.cpp's octave detection, and the LFO's skew factor.
/// `scalarTests.cpp` is what pins them.
float log2(float const value) { return std::log2(value); }
float log10(float const value) { return std::log10(value); }
float exp(float const value) { return std::exp(value); }
float exp2(float const value) { return std::exp2(value); }

std::uint8_t log2(int const value)
{
    LE_ASSERT_MSG(!isNegative(value), "Invalid input");
    return log2(static_cast<unsigned long>(value));
}

std::uint8_t log2(unsigned int const value) { return firstSetBit(value); }

std::uint8_t log2(unsigned long const value)
{
    LE_ASSERT_MSG(static_cast<unsigned int>(value) == value,
                  "Value out of range."); //...mrmlj...
    return log2(static_cast<unsigned int>(value));
}

namespace PowerOfTwo
{
/// \note Declared as ceil( float ) and defined as ceil( float const & ), so
/// the declaration had never resolved to anything. Nothing in the tree called
/// it; sw-tests is the first thing that tried.
unsigned int ceil(float const value)
{
    // http://stackoverflow.com/questions/466204/rounding-off-to-nearest-power-of-2
    // http://www.gamedev.net/community/forums/topic.asp?topic_id=229831

    auto const valueBits(std::bit_cast<unsigned int>(value));
    unsigned int const notPowerOfTwo((valueBits << 9) != 0);
    unsigned int const exponent(
        (valueBits >> 23) // remove fractional part of the floating point number
        - 127             // subtract 127 (the bias) from the exponent
        + notPowerOfTwo   // add one to the exponent if the value was not a power of two
    );

    LE_ASSUME(exponent < (sizeof(1U) * 8));

    return 1U << exponent;
}

unsigned int floor(unsigned int const value) { return firstSetBit(value); }

unsigned int round(unsigned int const value)
{
    // http://en.wikipedia.org/wiki/Power_of_two#Algorithm_to_convert_any_number_into_nearest_power_of_two_number
    // http://stackoverflow.com/questions/1983303/using-bts-assembly-instruction-with-gcc-compiler
    // http://gcc.gnu.org/bugzilla/show_bug.cgi?id=36473

    LE_ASSERT_MSG(value != 0, "Invalid input");
    unsigned int const firstSetBitInValue(firstSetBit(value));
    /// \note The MSVC arm here was _bittest( &value, firstSetBitInValue - 1 ),
    /// which unlike this one did not short-circuit: at value 1 the index is -1
    /// and it read the word before `value`.
    unsigned int const isNextBitSet(firstSetBitInValue &&
                                    ((value & (1U << (firstSetBitInValue - 1))) != 0));

    unsigned int const exponent(firstSetBitInValue + isNextBitSet);

    LE_ASSUME(exponent < (sizeof(1U) * 8));

    return 1U << exponent;
}

std::uint8_t log2(unsigned int const value)
{
    LE_ASSERT_MSG(isPowerOfTwo(value), "Invalid input");
    return firstSetBit(value);
}
} // namespace PowerOfTwo

////////////////////////////////////////////////////////////////////////////////
//
// numberOfSetBits()
// -----------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Returns the number of bits set in the passed integer value.
///
/// \throws nothing
///
////////////////////////////////////////////////////////////////////////////////

std::uint8_t numberOfSetBits(int const value)
{
    // Implementation note:
    //   http://tekpool.wordpress.com/category/bit-count.
    //                                        (11.05.2009.) (Domagoj Saric)
    auto const uCount(value - ((value >> 1) & 033333333333) - ((value >> 2) & 011111111111));
    return static_cast<std::uint8_t>(((uCount + (uCount >> 3)) & 030707070707) % 63);
}

////////////////////////////////////////////////////////////////////////////////
//
// firstSetBit()
// -------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Returns the index of the first set MSB in the passed integer value.
/// Expects that the passed value is non-zero/has at least one bit set.
///
/// \throws nothing
///
////////////////////////////////////////////////////////////////////////////////
// http://stackoverflow.com/questions/364985/algorithm-for-finding-the-smallest-power-of-two-thats-greater-or-equal-to-a-give
////////////////////////////////////////////////////////////////////////////////

#if defined(_MSC_VER) && !defined(_XBOX)
#pragma intrinsic(_BitScanReverse)
#endif // _MSC_VER

std::uint8_t firstSetBit(int const value)
{
    LE_ASSERT_MSG(value, "Invalid input");
#if defined(_MSC_VER)
#ifdef _XBOX
    std::uint8_t const leadingZeroBits(_CountLeadingZeros(value));
    return (sizeof(value) * 8) - 1 - leadingZeroBits;
#else
    unsigned long firstSetBitIndex;
    LE_VERIFY(_BitScanReverse(&firstSetBitIndex, value) && "No bits set in the passed value.");
    return static_cast<std::uint8_t>(firstSetBitIndex);
#endif
#elif defined(__GNUC__)
    std::uint8_t const leadingZeroBits(__builtin_clz(value));
    return (sizeof(value) * 8) - 1 - leadingZeroBits;
#else // _MSC_VER
#error not implemented.
#endif // _MSC_VER
}

////////////////////////////////////////////////////////////////////////////////
//
// isPowerOfTwo()
// --------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Returns whether the passed value is a power-of-two value.
///
/// \throws nothing
///
////////////////////////////////////////////////////////////////////////////////

bool isPowerOfTwo(unsigned int const value)
{
    // http://graphics.stanford.edu/~seander/bithacks.html#DetermineIfPowerOf2
    bool const result((value & (value - 1)) == 0);
    // If only one bit is set then it is certainly a power-of-two value.
    LE_ASSERT_MSG(result == (numberOfSetBits(value) == 1), "Power of two logic bug.");
    return result;
}

bool isPowerOfTwo(int const value)
{
    LE_ASSERT_MSG(!isNegative(value), "Invalid input");
    return isPowerOfTwo(static_cast<unsigned int>(value));
}

////////////////////////////////////////////////////////////////////////////////
//
// Rng
// ---
//
////////////////////////////////////////////////////////////////////////////////

namespace
{

// https://channel9.msdn.com/Events/GoingNative/2013/rand-Considered-Harmful
// https://www.youtube.com/watch?v=45Oet5qjlms "PCG: A Family of Better Random Number Generators"
// http://eternallyconfuzzled.com/arts/jsw_art_rand.aspx
// http://www.pcg-random.org/other-rngs.html
// http://www.boost.org/doc/libs/release/doc/html/boost_random/reference.html#boost_random.reference.generators
// https://github.com/s9w/articles/blob/master/perf%20cpp%20random.md
// http://stackoverflow.com/questions/1640258/need-a-fast-random-generator-for-c
// http://stackoverflow.com/questions/1046714/what-is-a-good-random-number-generator-for-a-game
// http://burtleburtle.net/bob/rand/smallprng.html
// https://en.wikipedia.org/wiki/Xorshift
// https://en.wikipedia.org/wiki/Talk%3AXorshift#32-bit_code_for_xorshift1024.2A_and_xorshift128.2B
// http://xorshift.di.unimi.it

// http://xorshift.di.unimi.it/xorshift128plus.c
// http://www001.upp.so-net.ne.jp/isaku/en/dxor156.c.html
// http://www.irrelevantconclusion.com/2012/02/pretty-fast-random-floats-on-ps3
// http://www.reedbeta.com/blog/2013/01/12/quick-and-easy-gpu-random-numbers-in-d3d11

// http://security.stackexchange.com/questions/47446/can-the-xor-of-two-rng-outputs-ever-be-less-secure-than-one-of-them

/// \note The 64 bit RNG is much slower in 32bit builds so we 'reduce'/limit
/// its output width for those builds so that at least the ranged and
/// floating point wrapper functions don't have to go through the slow
/// emulated 64bit math path.
///                                       (06.10.2015.) (Domagoj Saric)
using rand_t = std::size_t;

/// \note The width the float conversion below divides by. Named here because it
/// is the one thing `next()` returning a full 64 bits took away: the narrowing
/// is now the *caller's*, so the scale has to match what the caller narrowed to.
rand_t narrow(std::uint64_t const wideResult)
{
    if constexpr (sizeof(std::size_t) >= sizeof(std::uint64_t))
        return static_cast<rand_t>(wideResult);
    else
        return static_cast<rand_t>(static_cast<std::uint32_t>(wideResult) + (wideResult >> 32));
}

} // anonymous namespace

std::uint64_t Rng::next() noexcept
{
    LE_ASSERT_MSG(state_[0] && state_[1], "RNG state is all zero.");

    std::uint64_t s1(state_[0]);
    std::uint64_t const s0(state_[1]);

    s1 ^= s1 << 23;                         // a
    s1 = s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26); // b, c

    state_[0] = s0;
    state_[1] = s1;

    return s1 + s0;
}

void Rng::seed(std::uint64_t const seed) noexcept
{
    // splitmix64, so that a small seed still fills both words. Neither may be
    // zero: xorshift128+ cannot leave the all-zero state.
    auto mix([state = seed]() mutable {
        state += 0x9E3779B97F4A7C15ull;
        auto z(state);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    });
    state_[0] = mix() | 1;
    state_[1] = mix() | 1;
}

void Rng::seedFromEntropy() noexcept
{
    /// \note This object's address rather than a stack address, which is what
    /// the process-global version took. A stack address is the same for every
    /// iteration of the loop that constructs a module's channel states, so two
    /// channels seeded microseconds apart could have collided on it; `this` is
    /// distinct among live generators by definition. The clock separates runs,
    /// and reused addresses across them.
    seed(static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()) ^
         reinterpret_cast<std::uintptr_t>(this));
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief A random number in the [0, 1] interval.
///
////////////////////////////////////////////////////////////////////////////////

float Rng::normalised() noexcept
{
    /// \note The divisor used to be std::numeric_limits<rand_t>::max(), which no
    /// double can hold: it converts to one more than it is -- 2^digits -- and
    /// the compiler said so. 2^digits is therefore the scale that was being
    /// applied, and it is spelt here as something a double does hold exactly.
    constexpr double scale(1 /
                           (static_cast<double>((std::numeric_limits<rand_t>::max() / 2) + 1) * 2));
    auto const result(static_cast<float>(static_cast<double>(narrow(next())) * scale));
    LE_ASSUME(result >= 0);
    LE_ASSUME(result <= 1);
    return result;
}

/// \brief A random number in the [0, maximum] interval.
float Rng::ranged(float const maximum) noexcept { return normalised() * maximum; }

/// \brief A random number in the [minimum, maximum] interval.
float Rng::ranged(float const minimum, float const maximum) noexcept
{
    LE_ASSUME(maximum >= minimum);
    auto const result(minimum + ranged(maximum - minimum));
    LE_ASSUME(result >= minimum);
    LE_ASSUME(result <= maximum);
    return result;
}

namespace
{
/// \note For now we intentionally go with the naive modulo approach because of
/// the small ranges of random number we require vs the large value range of the
/// RNG. http://c-faq.com/lib/randrange.html
///                                       (05.10.2015.) (Domagoj Saric)
template <typename T> T moduloOf(std::uint64_t const draw, T const maximum)
{
    // [0, 0) has one answer; Burrito's Target Range is Minimum<0>, so a knob
    // reaches this, and a plain modulo is SIGFPE on x86 \see issue #190
    if (!maximum)
        return 0;
    return static_cast<T>(narrow(draw) % maximum);
}
} // anonymous namespace

/// \brief A random number in the [0, maximum) interval.
std::uint32_t Rng::ranged(std::uint32_t const maximum) noexcept
{
    return moduloOf(next(), maximum);
}
std::uint16_t Rng::ranged(std::uint16_t const maximum) noexcept
{
    return moduloOf(next(), maximum);
}

/// \brief A random number in the [minimum, maximum) interval.
std::int32_t Rng::ranged(std::int32_t const minimum, std::uint32_t const maximum) noexcept
{
    LE_ASSUME(static_cast<signed>(maximum) > minimum);
    std::int32_t const result(minimum + ranged(maximum - minimum));
    LE_ASSUME(result >= minimum);
    LE_ASSUME(result <= static_cast<signed>(maximum));
    return result;
}

} // namespace LE::Math

/// \note Non-template juce::jmin / jmax / jlimit overloads for float lived
/// here, gated on the GUI being built, so that the GUI's uses picked these up
/// instead of the patched fork's. JUCE 8 declares all three as constexpr
/// templates in
/// juce_MathsFunctions.h, which does the same thing at least as well and does
/// not need a definition in someone else's namespace. Redefining them is an
/// ODR hazard for no gain.
