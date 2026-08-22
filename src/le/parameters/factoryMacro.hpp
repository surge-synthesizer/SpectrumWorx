////////////////////////////////////////////////////////////////////////////////
///
/// \file factoryMacro.hpp
/// ----------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef factoryMacro_hpp__8F075C97_FE9A_47F0_A01B_A3632AA17AC9
#define factoryMacro_hpp__8F075C97_FE9A_47F0_A01B_A3632AA17AC9
//------------------------------------------------------------------------------
#include "parameter.hpp"
#include "parameterList.hpp"

namespace LE::Parameters
{

////////////////////////////////////////////////////////////////////////////////
///
/// Helper verbosity reducing macros for parameter specifications.
/// --------------------------------------------------------------
///
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
/// \def LE_DEFINE_PARAMETERS
///
/// \brief Declares the Parameters container out of parameters that have already
/// been declared -- LE_DEFINE_PARAMETERS( Interval, Wet, Bypass ).
///
///   The order is the order, and it is the whole of what this says. Presets
/// serialise by name and automation addresses by index, so this line is the one
/// that must not be reordered by accident; parameterTableTests holds it to a
/// committed snapshot.
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   Was a "two dimensional" Boost.PP sequence that both declared each parameter
// and collected them, which is why it needed a sequence walk: a parameter's name
// has to be pasted into a class declaration, and only the preprocessor can do
// that. Declaring each parameter on its own line with LE_DEFINE_PARAMETER leaves
// this macro nothing to iterate -- ParameterList takes the pack as it stands --
// and a parameter borrowed from elsewhere no longer needs the (( Key )) wrapper
// that told the walk to skip it.
//
//   Parameters remains a class of its own rather than an alias for the
// ParameterList it is: presets.hpp forward declares GlobalParameters::Parameters,
// and two effects declaring the same parameters would otherwise share one type.
////////////////////////////////////////////////////////////////////////////////

#define LE_DEFINE_PARAMETERS(...)                                                                  \
    struct Parameters : ::LE::Parameters::ParameterList<__VA_ARGS__>                               \
    {                                                                                              \
    }

} // namespace LE::Parameters

//------------------------------------------------------------------------------
#endif // factoryMacro_hpp
