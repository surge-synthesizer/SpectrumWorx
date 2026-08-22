////////////////////////////////////////////////////////////////////////////////
///
/// \internal
/// \file parameters.hpp
/// --------------------
///
/// Shared LE parameter forward declarations required by all effects.
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef parameters_hpp__E052A5AB_F608_48B9_9C09_DC2497B4086D
#define parameters_hpp__E052A5AB_F608_48B9_9C09_DC2497B4086D
//------------------------------------------------------------------------------
#include "le/parameters/factoryMacro.hpp"

#if defined(_MSC_VER) && !defined(__clang__) && (_MSC_VER < 1800)
#include "le/parameters/boolean/parameter.hpp"
#endif // old MSVC

namespace LE
{

namespace Parameters
{
template <class Traits> class Parameter; ///< \internal

#if !(defined(_MSC_VER) && !defined(__clang__) && (_MSC_VER < 1800))
struct Boolean;
#endif // !old MSVC

struct LinearUnsignedInteger;
struct LinearSignedInteger;
struct LinearFloat;

struct SymmetricInteger;
struct SymmetricFloat;

class TriggerParameter;
} // namespace Parameters

namespace SW::Effects
{

// Implementation note:
//   Import shared parameter types into the Effects namespace for less
// verbose effect parameters definitions with the LE_DEFINE_PARAMETERS macro.
//                                            (15.04.2011.) (Domagoj Saric)

using Parameters::Boolean;

using Parameters::LinearFloat;
using Parameters::LinearSignedInteger;
using Parameters::LinearUnsignedInteger;

using Parameters::SymmetricFloat;
using Parameters::SymmetricInteger;

using Parameters::TriggerParameter;

// Implementation note:
//   And the traits, which LE_DEFINE_PARAMETER used to qualify itself: it walked
// the trait sequence prefixing every element with LE::Parameters::Traits::, and
// a variadic macro forwards its arguments rather than visiting them. They are
// imported rather than named at each of the ~870 declarations that use them.

using Parameters::Traits::Default;
using Parameters::Traits::Maximum;
using Parameters::Traits::MaximumOffset;
using Parameters::Traits::Minimum;
using Parameters::Traits::Unit;
using Parameters::Traits::ValuesDenominator;

} // namespace SW::Effects

} // namespace LE

#endif // parameters_hpp
