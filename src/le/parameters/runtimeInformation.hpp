////////////////////////////////////////////////////////////////////////////////
///
/// \file runtimeInformation.hpp
/// ----------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef runtimeInformation_hpp__3FF98324_ED63_409C_8624_1DC2E69AF3CE
#define runtimeInformation_hpp__3FF98324_ED63_409C_8624_1DC2E69AF3CE
//------------------------------------------------------------------------------
#include "le/utility/abi.hpp"

#include <cstdint>

namespace LE::Parameters
{

/// \addtogroup Parameters
/// @{

////////////////////////////////////////////////////////////////////////////////
///
/// \class RuntimeInformation
///
/// \brief Runtime/dynamic parameter metadata
///
/// \details All instances of this struct (as well as the data its members point
/// to) are statically allocated and it is therefore safe to hold references to
/// them.
///
////////////////////////////////////////////////////////////////////////////////

struct RuntimeInformation
{
    typedef float value_type; ///< Uniform type used for marshaling values of all parameter types

    /// \brief Possible parameter types (does not cover power-of-two parameters yet)
    enum Type
#ifndef DOXYGEN_ONLY
        : std::uint8_t
#endif // DOXYGEN_ONLY
    {
        Boolean,
        Enumerated,
        FloatingPoint,
        Integer,
        Trigger
    };

    Type const type; ///< parameter's type

    value_type const minimum;  ///< parameter's minimum possible/allowed value
    value_type const maximum;  ///< parameter's maximum possible/allowed value
    value_type const default_; ///< parameter's default value

    char const *LE_RESTRICT const name; ///< parameter's name, as shown to a user

    /// \brief The name the parameter is written to a preset or to session state
    /// under. Defaults to \ref name and diverges from it only where a display
    /// name has been changed since a file was written; see Parameters::
    /// StreamingName. Never null.
    char const *LE_RESTRICT const streamingName;

    char const *LE_RESTRICT const
        unit; ///< parameter's unit (e.g. "Hz" or "%", may be an empty string but never null)

    /// \brief For <VAR>Enumerated</VAR> parameters, an array of C strings (of
    /// length <VAR>maximum</VAR>) representing the individual values of the
    /// enumerated parameter. A nullptr for all other types of parameters.
    char const *LE_RESTRICT const *LE_RESTRICT const enumeratedValueStrings;
}; // struct RuntimeInformation

/// @} // group Parameters

} // namespace LE::Parameters

#endif // runtimeInformation_hpp
