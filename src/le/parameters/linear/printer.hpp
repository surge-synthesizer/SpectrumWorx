////////////////////////////////////////////////////////////////////////////////
///
/// \file printer.hpp
/// -----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef printer_hpp__AC776212_E8E5_4781_922F_DC16639D2364
#define printer_hpp__AC776212_E8E5_4781_922F_DC16639D2364
//------------------------------------------------------------------------------
#include "tag.hpp"

#include "le/parameters/printer_fwd.hpp"
#include "le/utility/lexicalCast.hpp"

#include <cstdio>
#include <type_traits>

namespace LE::Parameters::Detail
{
template <class TraitTag, class Traits, class... DefaultTraits> struct GetTraitDefaulted;

/// \note The buffer travels as a `PrintBuffer` rather than as its `begin()`.
/// `print()` was handed one -- a span, which knows how big it is -- and passed
/// only the pointer down, so the size was thrown away at exactly the layer that
/// needed it and `lexical_cast` was left guessing with a constant.
///                                           (08.08.2026.) (SW port)
/// \note One decimal place, and the zero that pads it out is *kept*. A float
/// parameter shows one decimal or none depending on its value otherwise, so a
/// knob swept past unity reads "0.8", "1", "1.1" -- the readout jumps a character
/// wide and back under the user's hand, and the round number, which is the one
/// worth recognising, is the one that loses its point. \see issue #94.
///                                           (17.08.2026.)
template <typename Source>
char const *printLinear(PrintBuffer const &buffer, Source const &parameterValue,
                        LinearFloatParameterTag const &)
{
    Utility::lexical_cast(parameterValue, 1, buffer, Utility::TrailingZeros::keep);
    return buffer.begin();
}

template <typename Source>
std::enable_if_t<std::is_integral_v<Source>, char const *> LE_FORCEINLINE
printLinear(PrintBuffer const &buffer, Source const parameterValue,
            LinearIntegerParameterTag const &)
{
    Utility::lexical_cast(parameterValue, buffer);
    return buffer.begin();
}

template <typename Source>
std::enable_if_t<std::is_floating_point_v<Source>, char const *>
printLinear(PrintBuffer const &buffer, Source const &parameterValue,
            LinearIntegerParameterTag const &)
{
    Utility::lexical_cast(parameterValue, 0, buffer);
    return buffer.begin();
}

template <class Parameter, typename Source>
char const *print(Source const &parameterValue, SW::Engine::Setup const &engineSetup,
                  PrintBuffer const &buffer, LinearParameterTag const &)
{
    typedef DisplayValueTransformer<Parameter> ValueTransformer;

    return printLinear(buffer, ValueTransformer::transform(parameterValue, engineSetup),
                       typename Parameter::Tag());
}
} // namespace LE::Parameters::Detail

#endif // printer_hpp
