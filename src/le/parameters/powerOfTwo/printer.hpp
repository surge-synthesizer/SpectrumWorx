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
#ifndef printer_hpp__8CF0596B_4588_4B99_8488_5F77415359B
#define printer_hpp__8CF0596B_4588_4B99_8488_5F77415359B
//------------------------------------------------------------------------------
#include "tag.hpp"

#include "le/math/conversion.hpp"
#include "le/parameters/linear/printer.hpp"
#include "le/parameters/uiElements.hpp" // DisplayValueTransformer

#include "le/utility/cstdint.hpp"

#include <type_traits>

namespace LE::Parameters::Detail
{

////////////////////////////////////////////////////////////////////////////////
///
/// \brief A power-of-two parameter, shown as whatever its display transform
/// makes of it -- the value itself for an FFT size, the percentage it overlaps
/// by for an overlap factor.
///
/// \note This used to ignore DisplayValueTransformer, on the grounds that
/// "PowerOfTwo parameters do not currently support/use the DisplayValueTransformer
/// functionality" -- and the overlap factor's percentage was supplied instead by
/// an explicit specialisation of this template, in plugin2Host.hpp. That
/// specialisation named two overloads that did not exist: `unsigned int` and
/// `float const &` where the templates here took `std::uint16_t` and `float`, so
/// the exact match always won and the percentage was never printed. Every caller
/// -- the CLAP entry point and the settings window's own combo box, which has a
/// comment saying it exists to get the percentage -- has been showing the raw
/// factor with a "%" after it since 2011.
///
///   One template, through the transformer, is what makes that unrepresentable:
/// there is no second spelling to specialise and miss, and it is the same
/// transformer the parser inverts.
///
/// \note The number of decimal places follows what the transform *returns*: an
/// identity transform hands back the parameter's own integral type and the value
/// prints as an integer, a percentage hands back a float and prints as one.
///
////////////////////////////////////////////////////////////////////////////////

template <class Parameter, typename Source>
char const *print(Source const parameterValue, SW::Engine::Setup const &engineSetup,
                  PrintBuffer const &buffer, PowerOfTwoParameterTag)
{
    auto const displayValue(DisplayValueTransformer<Parameter>::transform(
        Math::convert<typename Parameter::value_type>(parameterValue), engineSetup));

    if constexpr (std::is_floating_point_v<std::remove_cvref_t<decltype(displayValue)>>)
        return printLinear(buffer, displayValue, LinearFloatParameterTag());
    else
        return printLinear(buffer, displayValue, LinearIntegerParameterTag());
}

} // namespace LE::Parameters::Detail

#endif // printer_hpp
