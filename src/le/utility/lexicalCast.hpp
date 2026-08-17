////////////////////////////////////////////////////////////////////////////////
///
/// \file lexicalCast.hpp
/// ---------------------
///
/// String<->binary conversion routines.
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef lexicalCast_hpp__432C7AD4_64B3_4058_BA65_5F4401951A08
#define lexicalCast_hpp__432C7AD4_64B3_4058_BA65_5F4401951A08
//------------------------------------------------------------------------------
#include "tchar.hpp"

#include "le/utility/platformSpecifics.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

namespace LE::Utility
{

////////////////////////////////////////////////////////////////////////////////
// Binary -> String
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
/// \note Every one of these takes the buffer it may write to, and not a bare
/// pointer.
///
///   It was a pointer, and the bound was a *constant* the interface asked its
/// callers to have used -- `RequiredStringStorage<T>::value`, seventeen for a
/// double. Nothing checked that a caller had, and the constant was computed for
/// a significant-digit format while the code printed `%.Nf`: `%.1f` of 1e30
/// wants thirty-three characters. The overrun that followed was in the
/// trailing-zero trim rather than in the `snprintf`, and it walked off the end of
/// a caller's stack array and handed back a length pointing past it.
///
///   A span carries the size the caller actually has, so the two cannot disagree
/// -- and `char[N]`, `std::array<char, N>` and `LE::Utility::Span<char>` all
/// convert to one, which is why the call sites read the same as before. A bare
/// pointer does not convert, which is the point: there is no way left to call
/// these without saying how much room there is.
///                                           (08.08.2026.) (SW port)
///
/// \return the number of characters written, the terminator not counted, or zero
/// when the value does not fit -- in which case \p buffer holds the empty string.
/// Never a length a caller cannot index with, which is what the editor's two
/// `strcpy(&buffer[written], " ms")` sites do with it.
///
/// \note A value too wide for \p buffer at the precision asked for is printed
/// with `%g` rather than truncated. Fewer significant digits than were asked for,
/// but the right number, inside the buffer, and something `strtod` reads back;
/// a truncated `%.1f` of 1e30 would have read "1000000000000000".
///
///   **So a very wide value does not round-trip at full precision, and whether
/// that matters is the caller's to decide.** For a display it is the right
/// answer: six significant digits rather than nine, in a buffer that is as big as
/// the widget. For `presets.hpp`'s `makeString`, which writes the number into a
/// file, it would be a loss -- so that one sizes its buffer with
/// `RequiredStringStorage`, 321 bytes for a `double`, which is what `%.9f` of one
/// actually needs.
///
///   Nothing in the shipping banks is anywhere near that wide and the corpus
/// digests did not move, so this is a property to keep in mind rather than a
/// defect. A parameter whose values ever reach 1e300 wants its own printer, not a
/// wider buffer.
///                                           (08.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

unsigned int lexical_cast(std::int32_t, std::span<char> buffer);
unsigned int lexical_cast(std::int64_t, std::span<char> buffer);
unsigned int lexical_cast(std::uint32_t, std::span<char> buffer);
unsigned int lexical_cast(std::uint64_t, std::span<char> buffer);
unsigned int lexical_cast(float, std::span<char> buffer);
unsigned int lexical_cast(double, std::span<char> buffer);

////////////////////////////////////////////////////////////////////////////////
///
/// \enum TrailingZeros
///
/// \brief Whether the zeros that pad a fixed-point rendering out to
/// \p decimalPlaces are kept or trimmed away.
///
/// \note `trim` is the older behaviour and stays the default: it is what writes
/// the shortest text that reads back, which is what a preset file wants.
///
///   A *display* wants the other one. A knob swept past unity read "0.8", "1",
/// "1.1" -- the column jumps a character wide and back as the user turns it, and
/// the one value that matters most is the one that loses its point. \see issue
/// #94, and le/parameters/linear/printer.hpp, which is where every float
/// parameter's readout is rendered.
///                                           (17.08.2026.)
///
////////////////////////////////////////////////////////////////////////////////

enum class TrailingZeros : std::uint8_t
{
    trim,
    keep
}; // enum class TrailingZeros

unsigned int lexical_cast(float, std::uint8_t decimalPlaces, std::span<char> buffer,
                          TrailingZeros = TrailingZeros::trim);
unsigned int lexical_cast(double, std::uint8_t decimalPlaces, std::span<char> buffer,
                          TrailingZeros = TrailingZeros::trim);

/// \note The narrow integers widen to the signedness they already have. `long`
/// and `unsigned long` used to widen to the *32 bit* overloads, which silently
/// truncated every value over 2^31 on the LP64 platforms this ships on; they are
/// the 64 bit overloads above now.
inline unsigned int lexical_cast(std::int8_t const value, std::span<char> const buffer)
{
    return lexical_cast(static_cast<std::int32_t>(value), buffer);
}
inline unsigned int lexical_cast(std::int16_t const value, std::span<char> const buffer)
{
    return lexical_cast(static_cast<std::int32_t>(value), buffer);
}
inline unsigned int lexical_cast(std::uint8_t const value, std::span<char> const buffer)
{
    return lexical_cast(static_cast<std::uint32_t>(value), buffer);
}
inline unsigned int lexical_cast(std::uint16_t const value, std::span<char> const buffer)
{
    return lexical_cast(static_cast<std::uint32_t>(value), buffer);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief A buffer size that cannot be too small for \p T.
///
/// \note No longer a contract -- the overloads above take the size they are given
/// and none of them depends on this. It is a convenience for whoever has to
/// declare the array, and it is now computed for the format that is actually
/// printed rather than for a significant-digit one: the sign, the integer part at
/// its widest, the point, the most decimal places any overload asks for, and the
/// terminator.
///
///   Which makes it much larger than the seventeen it used to be for a double --
/// 321, because `%f` of DBL_MAX is 309 integer digits. That is the number, and a
/// buffer of it is the only way a `double` is printed at full precision; a
/// display that wants 32 bytes is welcome to them and gets `%g` for the handful
/// of values that do not fit. It also fixes `std::int32_t`, whose old value of 11
/// was one short of what `-2147483648` needs with its terminator.
///
////////////////////////////////////////////////////////////////////////////////

/// \note The `%.Nf` format is built with a single digit for N, so this is the
/// most any overload can ask for. \see lexicalCast.cpp
inline constexpr std::uint8_t maximumDecimalPlaces{9};

template <typename T> struct RequiredStringStorage
{
  private:
    using value_type = std::remove_const_t<std::remove_reference_t<T>>;
    using binary_type = std::conditional_t<std::is_enum_v<value_type>, std::uint8_t, value_type>;
    using numeric_limits = std::numeric_limits<binary_type>;
    static_assert(numeric_limits::is_specialized, "Internal inconsistency");

    static constexpr std::size_t sign{1};
    static constexpr std::size_t point{1};
    static constexpr std::size_t terminator{1};

  public:
    static constexpr std::size_t value{
        numeric_limits::is_integer
            ? sign + numeric_limits::digits10 + 1 + terminator
            : sign + static_cast<std::size_t>(numeric_limits::max_exponent10) + 1 + point +
                  maximumDecimalPlaces + terminator};
}; // struct RequiredStringStorage

static_assert(RequiredStringStorage<std::int32_t>::value >= sizeof("-2147483648"));
static_assert(RequiredStringStorage<double>::value >= 321, "%f of DBL_MAX is 309 integer digits");

////////////////////////////////////////////////////////////////////////////////
// String -> Binary
////////////////////////////////////////////////////////////////////////////////

template <typename T> T lexical_cast(char const *);

template <> bool lexical_cast<bool>(char const *valueString);
template <> int lexical_cast<int>(char const *valueString);
template <> long lexical_cast<long>(char const *valueString);
template <> unsigned int lexical_cast<unsigned int>(char const *valueString);
template <> float lexical_cast<float>(char const *valueString);
template <> double lexical_cast<double>(char const *valueString);

template <> inline std::uint8_t lexical_cast<std::uint8_t>(char const *const valueString)
{
    return static_cast<std::uint8_t>(lexical_cast<std::uint32_t>(valueString));
}
template <> inline std::uint16_t lexical_cast<std::uint16_t>(char const *const valueString)
{
    return static_cast<std::uint16_t>(lexical_cast<std::uint32_t>(valueString));
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The number \p text begins with, or nothing at all when it does not
/// begin with one.
///
/// \note The distinction the lexical_cast<> above cannot make, and the reason
/// this exists beside it: `strtod` answers 0 for text it could not read a number
/// out of, so a caller that only takes the value cannot tell "0" from "off" from
/// "". That is not hypothetical -- it is how a typed value first reached this
/// plugin's engine, and how a "nan" round tripped through it.
///
///   A trailing unit is not an error: "-12.5 dB" is the number this returns and
/// the suffix the display added. Leading whitespace is skipped, `nan` is
/// refused outright, and an infinity is answered as one -- it is what the
/// ExImPloder gate's minimum prints as, and every caller clamps to a range
/// anyway.
///
////////////////////////////////////////////////////////////////////////////////

std::optional<double> parseNumber(char const *text);

} // namespace LE::Utility

#endif // lexicalCast_hpp
