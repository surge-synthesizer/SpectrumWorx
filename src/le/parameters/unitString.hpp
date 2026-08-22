////////////////////////////////////////////////////////////////////////////////
///
/// \file unitString.hpp
/// --------------------
///
///   A parameter's unit suffix as a compile-time string.
///
///   This was `boost::mpl::string< ' dB' >` -- a *multi-character literal* as a
/// non-type template parameter. Legal, but its value is implementation-defined
/// (the standard says only that it has type int), every compiler warns about it,
/// and it silently caps a unit at four characters per template argument, which is
/// why `Unit2< boost::mpl::string< ' dB/', 's' > >` had to exist at all: a comma
/// cannot appear inside a Boost.PP sequence element.
///
///   C++20 class-type NTTPs say the same thing as itself: `Unit< " dB/s" >`.
/// There is no length limit, no second trait, and no encoding question -- the
/// bytes in the literal are the bytes in the string, which for `Unit< "\xB0" >`
/// is the whole point (stage 0.6 had to write that one as an escape to stop the
/// CP-1252 conversion changing its value).
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef unitString_hpp__0B2E7F41_9C5A_4D38_A6E1_5F73C4B802D9
#define unitString_hpp__0B2E7F41_9C5A_4D38_A6E1_5F73C4B802D9
//------------------------------------------------------------------------------
#include <cstddef>

namespace LE::Parameters
{

////////////////////////////////////////////////////////////////////////////////
///
/// \struct FixedString
/// \internal
/// \brief A string literal usable as a template argument.
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   A structural type -- one public, non-mutable array member -- which is what
// lets it be an NTTP. Deliberately not a general-purpose string class: the
// implicit deduction guide from the constructor is the whole interface, so that
// UnitString< " dB" > reads as the literal it is.
////////////////////////////////////////////////////////////////////////////////

template <std::size_t lengthWithTerminator> struct FixedString
{
    consteval FixedString(char const (&literal)[lengthWithTerminator]) noexcept
    {
        for (std::size_t index(0); index < lengthWithTerminator; ++index)
            characters[index] = literal[index];
    }

    char characters[lengthWithTerminator];
}; // struct FixedString

////////////////////////////////////////////////////////////////////////////////
///
/// \struct UnitString
/// \brief The type a parameter's Unit trait carries, and what the plugin layers
/// ask for the suffix to print after a value.
///
////////////////////////////////////////////////////////////////////////////////

template <FixedString text> struct UnitString
{
    static constexpr char const *c_str() noexcept { return text.characters; }
    static constexpr std::size_t size() noexcept { return sizeof(text.characters) - 1; }
}; // struct UnitString

} // namespace LE::Parameters

#endif // unitString_hpp
