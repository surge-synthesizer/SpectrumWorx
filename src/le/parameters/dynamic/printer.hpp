////////////////////////////////////////////////////////////////////////////////
///
/// \file printer.hpp
/// -----------------
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef printer_hpp__616A36CA_66D2_41CD_B907_0BA22A99E3EA
#define printer_hpp__616A36CA_66D2_41CD_B907_0BA22A99E3EA
//------------------------------------------------------------------------------
#include "tag.hpp"

#include "le/parameters/printer_fwd.hpp"
#include "le/utility/lexicalCast.hpp"

namespace LE::Parameters
{

/// \todo Yet to be fully and properly implemented.
///                                           (11.03.2011.) (Domagoj Saric)

namespace Detail
{
template <class Parameter>
char const *print(typename Parameter::value_type const parameterValue, SW::Engine::Setup const &,
                  PrintBuffer const &buffer, DynamicRangeParameterTag)
{
    // the whole buffer rather than its begin(), so the bound is the caller's
    // own and the check is not made one write too late
    Utility::lexical_cast(parameterValue, buffer);
    return buffer.begin();
}
} // namespace Detail

} // namespace LE::Parameters

#endif // printer_hpp
