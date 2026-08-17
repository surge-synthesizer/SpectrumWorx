////////////////////////////////////////////////////////////////////////////////
///
/// lexicalCast.cpp
/// ---------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "lexicalCast.hpp"

#include "platformSpecifics.hpp"

// Implementation note:
//   On *NIX platforms we link dynamically with the CRT so we just use the
// sprintf function.
//                                        (12.12.2011.) (Domagoj Saric)
//   MSVC builds used to route the float and int conversions through
// Boost.Spirit's karma/qi instead, to keep the statically linked CRT's printf
// out of the binary. That was the last Boost dependency in this file, so both
// platforms now take the CRT path; if the size ever matters again, hand roll it
// rather than bringing Spirit back.
//                                        (28.07.2026.) (SW port)
//   And then off the plain-CRT path again, for a reason that is not size:
// `snprintf` and `strtod` read the *global* locale, which the host owns and the
// plugin does not. See the note above renderInCLocale.
//                                        (08.08.2026.) (SW port)

#include <charconv>
#include <clocale>
#include <cmath>
#include <cstdlib>
#ifndef NDEBUG
#include <cctype>
#endif // NDEBUG
#include <cstring>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>

/// \note macOS keeps the POSIX 2008 per-locale entry points in their own header;
/// glibc declares them in <locale.h> and <stdlib.h>.
#if defined(__APPLE__)
#include <xlocale.h>
#endif

namespace LE::Utility
{

// http://code.google.com/p/stringencoders/source/browse/trunk/src/modp_numtoa.c
// http://www.dreamincode.net/code/snippet2482.htm
// http://www.piumarta.com/software/fcvt

namespace
{
////////////////////////////////////////////////////////////////////////////////
///
/// \brief \p value as text spelled the way this plugin's files and displays
/// spell it, whatever locale the host has set.
///
/// \note The point of the file, and what `snprintf` could not do. Number
/// formatting reads the *global* locale, and a plugin does not own that: the
/// host sets it, or inherits it from the desktop, and a comma-decimal one made
/// this write "1,5" into preset files and into every parameter display. Each of
/// those files then read back as 1 -- `strtod` stops at the comma -- so the
/// plugin corrupted its own presets on the way out and could not read a factory
/// one on the way in, because those were written with a point. The user sees a
/// plugin that loses every fractional value it saves.
///
///   `imbue( std::locale::classic() )` is the fix in one line: a stream carries
/// its own locale, so nothing global reaches this. `std::to_chars` would have
/// been the other answer and is not available -- its floating point half is a
/// libc++ dylib symbol introduced in macOS 13.3 and this ships to 10.15. The
/// integer overloads below do use it, being header only.
///
/// \note Same text as before, to the byte. `num_put` is specified to format
/// through `printf("%.*f")` with the imbued locale's numpunct, so the classic
/// locale gives what `%.*f` gave -- verified across the magnitudes, both
/// infinities and a NaN before this was written, because a preset corpus digest
/// depends on it.
///                                           (08.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

std::string renderInCLocale(double const value, std::ios_base &(&notation)(std::ios_base &),
                            unsigned int const precision)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << notation << std::setprecision(static_cast<int>(precision)) << value;
    return stream.str();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief \p text into \p buffer, answering how much of it was used and leaving
/// the empty string behind when it did not fit.
///
/// \note The whole of what these overloads had to get right and did not.
/// `snprintf` bounds its *write* and then returns the length it would have
/// wanted, so a caller that takes the return value as "characters written" has a
/// number pointing past the end of its own buffer -- which the editor then
/// indexes with, to `strcpy` a suffix on. A length that came from a string that
/// is already known to fit cannot be that number.
///
////////////////////////////////////////////////////////////////////////////////

unsigned int copyInto(std::span<char> const buffer, std::string_view const text)
{
    if (buffer.empty()) [[unlikely]]
        return 0;

    /// \note `>=` rather than `>`: the terminator needs the last byte.
    if (text.size() >= buffer.size()) [[unlikely]]
    {
        /// \note Still a string a caller may use.
        buffer[0] = '\0';
        return 0;
    }

    std::memcpy(buffer.data(), text.data(), text.size());
    buffer[text.size()] = '\0';
    return static_cast<unsigned int>(text.size());
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The integer overloads' half of the above: `to_chars` is header only
/// for these, and locale independent by definition.
///
////////////////////////////////////////////////////////////////////////////////

template <typename Value> unsigned int printInto(std::span<char> const buffer, Value const value)
{
    if (buffer.empty()) [[unlikely]]
        return 0;

    /// \note One byte held back: `to_chars` does not terminate, so the byte it
    /// stops on is the one this writes the terminator into.
    auto const [pEnd,
                error](std::to_chars(buffer.data(), buffer.data() + buffer.size() - 1, value));

    if (error != std::errc{}) [[unlikely]]
    {
        buffer[0] = '\0';
        return 0;
    }

    *pEnd = '\0';
    return static_cast<unsigned int>(pEnd - buffer.data());
}

/// \note What `%g` printed, and the width the fallback below is reasoned about
/// with: six significant digits.
unsigned int constexpr generalPrecision{6};
} // anonymous namespace

unsigned int lexical_cast(std::int32_t const value, std::span<char> const buffer)
{
    return printInto(buffer, value);
}
unsigned int lexical_cast(std::int64_t const value, std::span<char> const buffer)
{
    return printInto(buffer, value);
}
unsigned int lexical_cast(std::uint32_t const value, std::span<char> const buffer)
{
    return printInto(buffer, value);
}
unsigned int lexical_cast(std::uint64_t const value, std::span<char> const buffer)
{
    return printInto(buffer, value);
}

unsigned int lexical_cast(float const value, std::span<char> const buffer)
{
    return lexical_cast(value, 4, buffer);
}
unsigned int lexical_cast(double const value, std::span<char> const buffer)
{
    return lexical_cast(value, maximumDecimalPlaces, buffer);
}
unsigned int lexical_cast(float const value, std::uint8_t const decimalPlaces,
                          std::span<char> const buffer, TrailingZeros const trailingZeros)
{
    return lexical_cast(static_cast<double>(value), decimalPlaces, buffer, trailingZeros);
}
////////////////////////////////////////////////////////////////////////////////
///
/// \note Rendered on its own and copied back, rather than straight into the
/// caller's buffer.
///
///   Fixed notation has no bound the caller's buffer can be trusted to satisfy:
/// `%.1f` of 1e30 wants thirty-three characters and a display hands over
/// thirty-two. The snprintf this used to be was bounded and so truncated safely,
/// but the trailing-zero trim below took its cursor from snprintf's *return*,
/// which is the length it wanted rather than the length it wrote -- so it walked
/// past the end of the caller's array, wrote its terminator there, and handed the
/// same out-of-range length back.
///
///   So the trim runs over a rendering that cannot have been truncated, and the
/// caller sees a result only once it is known to fit *its* buffer. What does not
/// fit is printed in general notation instead of truncated: fewer significant
/// digits than were asked for, but the right number, inside the buffer, and
/// something `strtod` reads back. Truncating would have been none of those --
/// 1e30 would read "1000000000000000". A buffer too small for even that gets the
/// empty string and a length of zero, which is the one answer no caller can
/// misuse.
///
/// \note `RequiredStringStorage<double>` sizes nothing here any more -- the
/// rendering brings its own storage -- and is no longer a claim about the caller
/// either. It is what a caller that wants every digit should declare.
///                                           (08.08.2026.) (SW port)
///
/// \note The trim is the caller's to ask for. It is what makes the text the
/// shortest that reads back, which is right for a file and wrong for a readout
/// -- \see TrailingZeros. The `%g` fallback below is unaffected either way: it is
/// reached only when fixed notation does not fit at all, and a value that wide
/// has no trailing zeros to argue about.
///                                           (17.08.2026.)
///
////////////////////////////////////////////////////////////////////////////////

LE_NOINLINE unsigned int lexical_cast(double const value, std::uint8_t const decimalPlaces,
                                      std::span<char> const buffer,
                                      TrailingZeros const trailingZeros)
{
    if (buffer.empty()) [[unlikely]]
        return 0;

    LE_ASSERT_MSG(decimalPlaces <= maximumDecimalPlaces,
                  "Wider than RequiredStringStorage is computed for.");
    std::string rendered(renderInCLocale(value, std::fixed, decimalPlaces));
    auto totalCharactersWritten(static_cast<unsigned int>(rendered.size()));

    /// \note `length > decimalPlaces` and not merely `decimalPlaces`: infinity
    /// and NaN print as three characters whatever the precision asked for, and
    /// there is no point in them to trim back to.
    if ((trailingZeros == TrailingZeros::trim) && decimalPlaces &&
        (totalCharactersWritten > decimalPlaces))
    {
        /// \note Trim trailing zeros.
        ///                                   (15.12.2011.) (Domagoj Saric)
        char *const pStart(rendered.data());
        char *pEnd(pStart + totalCharactersWritten);
        char const *const pDot(pEnd - decimalPlaces - 1);
        LE_ASSERT(*pEnd == '\0');
        LE_ASSERT(*pDot == '.' || !std::isfinite(value));
        while ((pEnd != pDot) && (*--pEnd == '0'))
        {
        }
        pEnd += (pEnd != pDot);
        LE_ASSERT(*pEnd == '0' || *pEnd == '.' || *pEnd == '\0');
        totalCharactersWritten = static_cast<unsigned int>(pEnd - pStart);
        rendered.resize(totalCharactersWritten);
    }
    else
    {
        LE_ASSERT(std::isalnum(rendered[totalCharactersWritten - 1]));
    }

    totalCharactersWritten = copyInto(buffer, rendered);
    if (totalCharactersWritten == 0) [[unlikely]]
    {
        /// \note Six significant digits and a three-digit exponent is thirteen
        /// characters at the widest, so this fits anything but a very small
        /// buffer -- and `copyInto` answers zero and an empty string for one of
        /// those rather than half a number.
        totalCharactersWritten =
            copyInto(buffer, renderInCLocale(value, std::defaultfloat, generalPrecision));
        if (totalCharactersWritten == 0)
            return 0;
    }

    ////////////////////////////////////////////////////////////////////////////
    /// \note And "-0" is "0". A value that rounds to zero at the precision being
    /// shown *is* zero as far as this string is concerned, and the sign in front
    /// of it is noise a user has to interpret: a module gain sitting at exactly
    /// 0 dB reads as "-0 dB" the moment a host normalises the value and writes it
    /// back, because -48 + (48/72) * 72 is -7e-15 rather than 0.
    ///
    ///   Here rather than at that subtraction, because the subtraction is not
    /// wrong -- it is as close to zero as a float gets -- and because every other
    /// route to a tiny negative lands here too.
    ///                                       (07.08.2026.) (SW port)
    ////////////////////////////////////////////////////////////////////////////
    if (buffer[0] == '-')
    {
        char const *pCharacter(buffer.data() + 1);
        while ((*pCharacter == '0') || (*pCharacter == '.'))
            ++pCharacter;
        if (*pCharacter == '\0')
        {
            // the terminator too
            std::memmove(buffer.data(), buffer.data() + 1, totalCharactersWritten);
            --totalCharactersWritten;
        }
    }

    /// \note In double rather than float, and with a relative tolerance beside
    /// the absolute one. Read back as a float this compared `inf` against `inf`
    /// for any value over FLT_MAX -- a NaN, so the check was vacuously false and
    /// only the `isfinite` arm was holding it up. The relative arm is what the
    /// `%g` fallback above needs: six significant digits is all it promises.
    ///                                       (08.08.2026.) (SW port)
    [[maybe_unused]] auto const readBack(lexical_cast<double>(buffer.data()));
    LE_ASSERT_MSG(!std::isfinite(value) ||
                      (std::abs(readBack - value) < (1 / std::pow(10.0, decimalPlaces))) ||
                      (std::abs(readBack - value) <= std::abs(value) * 1e-5),
                  "Zero trimming broken.");
    return totalCharactersWritten;
}

template <> bool lexical_cast<bool>(char const *const valueString)
{
    LE_ASSERT(valueString[0] == '0' || valueString[0] == '1');
    LE_ASSERT(valueString[1] == '\0' || valueString[1] == '"' || valueString[1] == '<');
    std::uint8_t const value(valueString[0] - '0');
    LE_ASSUME((value == 0) || (value == 1));
    return reinterpret_cast<bool const &>(value);
}

template <> int lexical_cast<int>(char const *valueString) { return std::atoi(valueString); }

template <> long lexical_cast<long>(char const *const valueString)
{
    return lexical_cast<int>(valueString);
}
template <> unsigned int lexical_cast<unsigned int>(char const *const valueString)
{
    return lexical_cast<int>(valueString);
}

namespace
{
////////////////////////////////////////////////////////////////////////////////
///
/// \brief `strtod`, reading the point this plugin writes rather than the one the
/// host's locale happens to name.
///
/// \note The other half of the locale problem, and the half that loses data: a
/// comma-decimal locale makes `strtod` stop at the point in "0.75" and answer
/// **0**. Every factory preset and every file this plugin has ever written spells
/// a fraction that way, so under such a host they all load as their integer
/// parts -- silently, since stopping early is not an error.
///
/// \note Not the stream extraction the printing side uses. `num_get`'s accepted
/// character set is specified without `i` or `n` in it, so `>> value` is not
/// required to read "inf" -- and this plugin prints exactly that, for the
/// ExImPloder gate's minimum. `strtod` reads it, always has, and taking the
/// locale as an argument is the only thing it was missing.
///                                           (08.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

/// \note These three ask which C runtime is underneath, not which dialect the
/// compiler speaks, and the answer is _MSC_VER on its own: clang-cl links the
/// same MSVC CRT that cl.exe does, so `!defined(__clang__)` sent it to POSIX
/// newlocale/strtod_l, which that CRT does not have.
///                                           (09.08.2026.) (SW port)
#if defined(_MSC_VER)
using CLocale = ::_locale_t;
#else
using CLocale = ::locale_t;
#endif

CLocale cLocale()
{
    /// \note Created once and never freed. It is a raw handle with no destructor
    /// to order against whatever the host runs after main, and one per process is
    /// not a leak that grows.
    static CLocale const locale{
#if defined(_MSC_VER)
        ::_create_locale(LC_ALL, "C")
#else
        ::newlocale(LC_ALL_MASK, "C", nullptr)
#endif
    };
    return locale;
}

double toDouble(char const *const text, char **const ppEnd)
{
    auto const locale(cLocale());

    /// \note A locale this platform could not create leaves the global one, which
    /// is what this had before and is right whenever the host has not moved it.
    if (!locale) [[unlikely]]
        return std::strtod(text, ppEnd);

#if defined(_MSC_VER)
    return ::_strtod_l(text, ppEnd, locale);
#else
    return ::strtod_l(text, ppEnd, locale);
#endif
}

double lexical_cast_double_worker(char const *&pValueString);

float lexical_cast_float_worker(char const *&pValueString)
{
    return static_cast<float>(lexical_cast_double_worker(pValueString));
}

double lexical_cast_double_worker(char const *&pValueString)
{
    char *pEnd;
    double const result(toDouble(pValueString, &pEnd));
    pValueString = pEnd;
    return result;
}
} // namespace

template <> float lexical_cast<float>(char const *valueString)
{
    return lexical_cast_float_worker(valueString);
}
template <> double lexical_cast<double>(char const *valueString)
{
    return lexical_cast_double_worker(valueString);
}

std::optional<double> parseNumber(char const *const text)
{
    if (!text)
        return {};

    char *pEnd;
    double const value(toDouble(text, &pEnd));

    /// \note The two answers the lexical_cast<> above cannot give. `strtod`
    /// leaves pEnd where it started when it read nothing at all, which is the
    /// only way to tell "" and "off" from "0"; and it happily reads "nan", which
    /// is a value nothing downstream may be handed.
    if (pEnd == text)
        return {};
    if (std::isnan(value))
        return {};

    return value;
}

} // namespace LE::Utility
