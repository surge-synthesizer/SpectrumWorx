////////////////////////////////////////////////////////////////////////////////
///
/// \file lexicalCastTests.cpp
/// --------------------------
///
///   Number to string, and the one thing these functions have to promise: that
/// they stay inside the buffer they were handed, whatever it is.
///
///   They used to take a bare `char *` and bound themselves by a *constant* the
/// interface asked callers to have used -- `RequiredStringStorage<T>::value`,
/// seventeen for a double. Nothing checked that a caller had, and the constant
/// was computed for a significant-digit format while the code printed `%.Nf`:
/// `%.1f` of 1e30 wants thirty-three characters. The `snprintf` was bounded and
/// truncated safely; the trailing-zero trim after it took its cursor from
/// snprintf's *return*, which is the length it would have wanted, so it walked
/// down from `buffer[32]` of a seventeen-byte buffer and wrote its terminator
/// there -- and handed that same out-of-range length back to a caller. Both
/// editor call sites `strcpy` a suffix at exactly that offset, into a 32-byte
/// stack array.
///
///   The size is a parameter now, so the cases below are written against buffers
/// of several sizes: the 32 bytes a display gives it, the 321 a `double` needs to
/// print at full precision, and a buffer far too small for anything, which has to
/// answer rather than write.
///
/// \note The guard bytes are what make this a test rather than a hope. Without a
/// sanitizer an overrun of a stack array is silent and usually harmless, which is
/// exactly how it survived: it was recorded as "release truncates
/// rather than overruns", and the truncation was never the problem.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "utility/localeHarness.hpp"

#include "le/utility/lexicalCast.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using LE::Utility::lexical_cast;
using LE::Utility::RequiredStringStorage;

/// What a display hands over, and what both editor call sites really use.
constexpr std::size_t displayBuffer{32};

/// What a `double` needs before `%f` of one can be printed in full.
constexpr std::size_t fullBuffer{RequiredStringStorage<double>::value};

////////////////////////////////////////////////////////////////////////////////
///
/// \class Guarded
///
/// \brief A buffer of \p capacity bytes with a wall of sentinel bytes behind it
/// that nothing is entitled to touch.
///
////////////////////////////////////////////////////////////////////////////////

template <std::size_t capacity> class Guarded
{
  public:
    static constexpr char canary{'\x7E'};

    Guarded() { storage_.fill(canary); }

    /// Only the part the caller is entitled to.
    std::span<char> buffer() { return std::span<char>(storage_).first(capacity); }

    /// Whether everything past it is still untouched.
    bool intact() const
    {
        return std::all_of(storage_.begin() + capacity, storage_.end(),
                           [](char const byte) { return byte == canary; });
    }

    std::string text() const { return std::string(storage_.data()); }

  private:
    std::array<char, capacity + 64> storage_;
}; // class Guarded

/// \brief Renders \p value into a \p capacity byte buffer and checks every bound
/// the function owes its caller.
/// \return what it wrote.
template <std::size_t capacity>
std::string
renderedSafely(double const value, std::uint8_t const decimalPlaces,
               LE::Utility::TrailingZeros const trailingZeros = LE::Utility::TrailingZeros::trim)
{
    Guarded<capacity> guarded;
    auto const written(lexical_cast(value, decimalPlaces, guarded.buffer(), trailingZeros));

    // Nothing past the buffer the caller gave.
    CHECK(guarded.intact());

    // ...and the length handed back is one a caller may index with, which is
    // what both editor call sites immediately do with strcpy().
    CHECK(written < capacity);
    CHECK(written == std::strlen(guarded.text().c_str()));

    return guarded.text();
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
///
/// \note The locale is the host's and the numbers are the plugin's. Every one of
/// these read or wrote a comma before the classic-locale imbue and the `_l`
/// strtod: the displays showed one, the preset files got one, and -- the half
/// that loses a user's work -- reading back a point that this plugin itself
/// wrote stopped at it, so "0.75" loaded as 0 and every factory preset came
/// apart into its integer parts.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A host's locale does not reach the numbers the plugin reads and writes",
          "[utility][lexical-cast][locale]")
{
    SWTest::CommaDecimalHost const host;
    if (!host)
        SKIP("No comma-decimal locale is installed on this machine.");

    SECTION("what it writes")
    {
        CHECK(renderedSafely<displayBuffer>(1.5, 2) == "1.5");
        CHECK(renderedSafely<displayBuffer>(-12.5, 1) == "-12.5");
        CHECK(renderedSafely<displayBuffer>(0.75, 4) == "0.75");

        // ...including the value too wide for the buffer, which takes the other
        // rendering path.
        CHECK(renderedSafely<displayBuffer>(1e300, 9).find(',') == std::string::npos);
    }

    SECTION("what it reads")
    {
        CHECK(LE::Utility::lexical_cast<double>("0.75") == 0.75);
        CHECK(LE::Utility::lexical_cast<float>("0.75") == 0.75f);
        CHECK(LE::Utility::parseNumber("0.75") == 0.75);

        // A display's own text, suffix and all.
        CHECK(LE::Utility::parseNumber("-12.5 dB") == -12.5);

        // Both of these have to survive: an infinity is what a gate minimum
        // prints as, and text that is not a number at all has to stay refused.
        CHECK(LE::Utility::parseNumber("-inf") == -std::numeric_limits<double>::infinity());
        CHECK(!LE::Utility::parseNumber("off"));
    }

    SECTION("and the two agree with each other")
    {
        for (double const value : {0.1, 0.75, -12.5, 1234.5678, 1e-7})
        {
            INFO("value: " << value);
            auto const printed(renderedSafely<fullBuffer>(value, 9));
            auto const readBack(LE::Utility::parseNumber(printed.c_str()));
            REQUIRE(readBack);
            CHECK(*readBack == Catch::Approx(value));
        }
    }
}

TEST_CASE("An ordinary value prints the way it always did", "[utility][lexical-cast]")
{
    // The trailing-zero trim, which is the whole reason this is not snprintf.
    CHECK(renderedSafely<displayBuffer>(1.5, 1) == "1.5");
    CHECK(renderedSafely<displayBuffer>(1.0, 1) == "1");
    CHECK(renderedSafely<displayBuffer>(1.0, 4) == "1");
    CHECK(renderedSafely<displayBuffer>(0.25, 2) == "0.25");
    CHECK(renderedSafely<displayBuffer>(-6.5, 1) == "-6.5");
    CHECK(renderedSafely<displayBuffer>(100.0, 0) == "100");
    CHECK(renderedSafely<displayBuffer>(22050.0, 1) == "22050");

    // A value that rounds to zero at the precision shown is zero, sign and all.
    CHECK(renderedSafely<displayBuffer>(-7e-15, 1) == "0");
}

TEST_CASE("A display keeps the decimal the trim would take off", "[utility][lexical-cast]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The other half of the same function, and what every float
    /// parameter's readout asks for. A knob turned past unity printed "0.8",
    /// "1", "1.1" -- the column changes width under the user's hand and the
    /// round number is the one that loses its point. \see issue #94.
    ///
    ////////////////////////////////////////////////////////////////////////////
    using LE::Utility::TrailingZeros;

    CHECK(renderedSafely<displayBuffer>(1.0, 1, TrailingZeros::keep) == "1.0");
    CHECK(renderedSafely<displayBuffer>(0.8, 1, TrailingZeros::keep) == "0.8");
    CHECK(renderedSafely<displayBuffer>(100.0, 1, TrailingZeros::keep) == "100.0");
    CHECK(renderedSafely<displayBuffer>(-6.0, 1, TrailingZeros::keep) == "-6.0");

    // Zero places is still zero places: there is no point to pad out.
    CHECK(renderedSafely<displayBuffer>(100.0, 0, TrailingZeros::keep) == "100");

    // And a value that rounds to zero keeps losing its sign, which is a
    // statement about the number rather than about the trim.
    CHECK(renderedSafely<displayBuffer>(-7e-15, 1, TrailingZeros::keep) == "0.0");

    // Infinity has no decimals to pad, whichever way it is asked.
    CHECK(renderedSafely<displayBuffer>(std::numeric_limits<double>::infinity(), 1,
                                        TrailingZeros::keep) == "inf");
}

TEST_CASE("A value too wide for the buffer stays inside it", "[utility][lexical-cast][hostile]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `%.1f` of 1e30 wants 33 characters and a display gives 32. This is
    /// the case the trim walked off the end of -- reading `buffer[32]` and
    /// writing a terminator there, in a release build, on a stack array.
    ///
    ////////////////////////////////////////////////////////////////////////////
    for (auto const decimalPlaces :
         {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{4}, std::uint8_t{9}})
    {
        INFO("decimal places: " << unsigned(decimalPlaces));

        for (auto const value : {1e30, -1e30, 1e17, 1e8, 1e300, -1e300})
        {
            INFO("value: " << value);
            auto const text(renderedSafely<displayBuffer>(value, decimalPlaces));

            // Whatever it decided to print, it must be a number and it must be
            // the right one -- a truncated "1000000000000000" for 1e30 is inside
            // the buffer and still a lie.
            CHECK(!text.empty());
            auto const readBack(lexical_cast<double>(text.c_str()));
            CHECK(std::abs(readBack - value) <= std::abs(value) * 1e-5);
        }
    }
}

TEST_CASE("A buffer big enough gets every digit that was asked for", "[utility][lexical-cast]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note What the size parameter buys, and why `RequiredStringStorage` still
    /// exists: given the room, `%f` is printed rather than fallen back from. This
    /// is the difference between a display and `presets.hpp`'s `makeString`,
    /// which writes the number into a file.
    ///
    ////////////////////////////////////////////////////////////////////////////
    /// \note 1e300 rather than 1e30. `%.1f` of 1e30 trims to thirty-one
    /// characters and so *fits* a display's thirty-two -- the trim runs before
    /// the fit is decided, which is worth knowing and is why this needs a value
    /// that cannot fit however much is trimmed.
    auto const wide(renderedSafely<fullBuffer>(1e300, 1));
    CHECK(wide.size() > 300); // every one of the integer digits
    CHECK(wide.find('e') == std::string::npos);

    // ...where the same value in a display's buffer is the compact form.
    auto const narrow(renderedSafely<displayBuffer>(1e300, 1));
    CHECK(narrow.find('e') != std::string::npos);

    // Both are the same number, which is the property that matters.
    CHECK(lexical_cast<double>(wide.c_str()) == 1e300);
    CHECK(lexical_cast<double>(narrow.c_str()) == 1e300);
}

TEST_CASE("A buffer too small for anything answers rather than writes",
          "[utility][lexical-cast][hostile]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Zero and an empty string, not half a number. A caller indexes with
    /// what comes back -- `strcpy(&buffer[written], " ms")` -- so a length is a
    /// promise about the buffer, and "as much as fitted" would be a wrong number
    /// presented as a right one.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Guarded<4> guarded;
    CHECK(lexical_cast(123456.789, 3, guarded.buffer()) == 0);
    CHECK(guarded.intact());
    CHECK(guarded.text().empty());

    // A buffer of nothing at all is not a crash either.
    CHECK(lexical_cast(1.5, 1, std::span<char>{}) == 0);

    // ...and one that *is* big enough for the compact form uses it.
    Guarded<16> roomy;
    CHECK(lexical_cast(123456.789, 3, roomy.buffer()) > 0);
    CHECK(roomy.intact());
    CHECK(lexical_cast<double>(roomy.text().c_str()) == Catch::Approx(123456.789).epsilon(0.001));
}

TEST_CASE("A number with a suffix appended after it stays inside the buffer",
          "[utility][lexical-cast][hostile]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The editor's idiom, which is the reason the returned length matters
    /// as much as the writing does: render into the buffer *less the suffix*,
    /// then `strcpy` the suffix at the offset that comes back.
    ///
    ///   It is safe by exactly one byte, and the reason is worth writing down
    /// because it is not obvious. `sizeof(suffix)` counts the suffix's own
    /// terminator, and `lexical_cast` separately reserves one for a terminator
    /// that the `strcpy` then overwrites -- so the last index either can touch is
    /// `size - 2`. Sweeping the magnitudes rather than naming the extremal value,
    /// because which value is extremal depends on the precision and on whether
    /// the trailing-zero trim fires.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const renderWithSuffix(
        [](auto const &suffix, double const value, std::uint8_t const decimalPlaces) {
            Guarded<displayBuffer> guarded;
            auto const buffer(guarded.buffer());

            auto const written(
                lexical_cast(value, decimalPlaces, buffer.first(buffer.size() - sizeof(suffix))));

            // The offset the caller is about to index with has to leave room for the
            // whole suffix, terminator included.
            REQUIRE(written + sizeof(suffix) <= buffer.size());
            std::strcpy(&buffer[written], suffix);

            CHECK(guarded.intact());
            return guarded.text();
        });

    for (auto const decimalPlaces :
         {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{9}})
    {
        INFO("decimal places: " << unsigned(decimalPlaces));

        for (double magnitude(1); magnitude < 1e308; magnitude *= 10)
        {
            INFO("magnitude: " << magnitude);
            for (double const value : {magnitude, -magnitude, magnitude * 1.5})
            {
                auto const milliseconds(renderWithSuffix(" ms", value, decimalPlaces));
                CHECK(milliseconds.ends_with(" ms"));

                auto const percent(renderWithSuffix("%", value, decimalPlaces));
                CHECK(percent.ends_with("%"));
            }
        }
    }
}

TEST_CASE("The extremes of the type stay inside the buffer", "[utility][lexical-cast][hostile]")
{
    for (auto const decimalPlaces : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{9}})
    {
        INFO("decimal places: " << unsigned(decimalPlaces));

        // Not checked for what they read back as -- infinity and NaN have no
        // round trip through this. Only that they do not leave the buffer.
        Guarded<displayBuffer> infinite;
        CHECK(lexical_cast(std::numeric_limits<double>::infinity(), decimalPlaces,
                           infinite.buffer()) < displayBuffer);
        CHECK(infinite.intact());

        Guarded<displayBuffer> nan;
        CHECK(lexical_cast(std::numeric_limits<double>::quiet_NaN(), decimalPlaces, nan.buffer()) <
              displayBuffer);
        CHECK(nan.intact());

        CHECK(
            renderedSafely<displayBuffer>(std::numeric_limits<double>::denorm_min(), decimalPlaces)
                .size() > 0);
    }
}

TEST_CASE("The integer overloads keep the width their constant promises", "[utility][lexical-cast]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `RequiredStringStorage<std::int32_t>` was 11, and `-2147483648` is
    /// eleven characters *plus* its terminator -- one short, which nothing had
    /// ever driven. It is computed from the format now.
    ///
    ////////////////////////////////////////////////////////////////////////////
    {
        Guarded<RequiredStringStorage<std::int32_t>::value> guarded;
        auto const written(
            lexical_cast(std::numeric_limits<std::int32_t>::min(), guarded.buffer()));
        CHECK(guarded.intact());
        CHECK(written == 11);
        CHECK(guarded.text() == "-2147483648");
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And the 64-bit overloads exist. `long` used to widen to the *32 bit*
    /// one, so every value over 2^31 was silently truncated on the LP64 platforms
    /// this ships on.
    ///
    ////////////////////////////////////////////////////////////////////////////
    {
        Guarded<RequiredStringStorage<std::int64_t>::value> guarded;
        constexpr std::int64_t big{5'000'000'000};
        CHECK(lexical_cast(big, guarded.buffer()) == 10);
        CHECK(guarded.intact());
        CHECK(guarded.text() == "5000000000");
    }
}
