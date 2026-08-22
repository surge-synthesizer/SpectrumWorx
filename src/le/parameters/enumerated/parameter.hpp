////////////////////////////////////////////////////////////////////////////////
///
/// \file enumerated/parameter.hpp
/// ------------------------------
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef parameter_hpp__5820E6B3_7684_4DF4_BC99_B0A5CCB0F3E9
#define parameter_hpp__5820E6B3_7684_4DF4_BC99_B0A5CCB0F3E9
//------------------------------------------------------------------------------
#include "tag.hpp"

#include "le/parameters/linear/parameter.hpp"

#include <cstdint>

namespace LE::Parameters
{

template <typename... Traits> struct TraitPack;

namespace Detail ///< \internal
{
////////////////////////////////////////////////////////////////////////////////
///
/// \note The default is a template argument rather than a trait, and that is
/// what makes it safe: `default_()` is read by everything that builds a
/// parameter table, by `clap_param_info.default_value` and by the parameter's
/// own constructor, so a specialisation written where any of those cannot see it
/// would silently hand back zero -- the primary-template failure
/// `parameter_system.md` §7 is about, in its quiet form. Carried on the type,
/// there is nowhere for it to be missing. \see issue #163.
///
////////////////////////////////////////////////////////////////////////////////

template <std::uint8_t numberOfValues, std::uint8_t defaultValue = 0>
struct EnumeratedParameterTraits : LinearParameterTraitsBase<0, numberOfValues - 1, defaultValue>
{
  public: // Types.
    using Tag = EnumeratedParameterTag;

    using value_type = std::uint8_t;
    using param_type = value_type;
    using binary_type = value_type;

    using Defaults = TraitPack<Traits::Unit<"">>;
    using Traits = TraitPack<>;

  public: // Values
    static unsigned int const rangeValuesDenominator = 1;

    static value_type minimum() { return 0; }
    static value_type maximum() { return numberOfValues - 1; }
    static value_type default_() { return defaultValue; }

    static value_type const discreteValueDistance = 1;

    static value_type const numberOfDiscreteValues = numberOfValues;

    static bool isValidValue(value_type const value)
    {
        return isValueInRange<param_type>(value, minimum(), maximum());
    }

  protected:
    static void increment(value_type &value) { ++value; }
    static void decrement(value_type &value) { --value; }
}; // struct EnumeratedParameterTraits
} // namespace Detail

////////////////////////////////////////////////////////////////////////////////
/// \internal
/// \class EnumeratedParameter
////////////////////////////////////////////////////////////////////////////////

template <std::uint8_t numberOfValues, std::uint8_t defaultValue = 0>
using EnumeratedParameter =
    Parameter<Detail::EnumeratedParameterTraits<numberOfValues, defaultValue>>;

////////////////////////////////////////////////////////////////////////////////
///
/// \def LE_ENUMERATED_PARAMETER
///
/// \brief Helps to define a parameter that has a discrete set of allowed
/// values.
///
///   It will assign automatically generated values to all the named values
/// specified in the valueSequence and will create a member enum with the enum
/// constants/"members" named just as specified in the valueSequence parameter.
///
////////////////////////////////////////////////////////////////////////////////

// Implementation note:
//   The value list was a Boost.PP sequence, ( Replace )( Sum ), for two reasons:
// BOOST_PP_SEQ_ENUM turned it into enumerators and BOOST_PP_SEQ_SIZE counted
// them, and the count is a template argument of the base class -- so it has to
// exist before the enum the class declares does.
//
//   The scoped enum below is that count, and it is the same list: an extra
// enumerator past the end of a list numbered from zero *is* the length. It costs
// a name beside the parameter and no argument-counting macro, whose only other
// spelling is a ladder of numbered arguments with a ceiling to raise later.

#define LE_ENUMERATED_PARAMETER_IMPL(parameterName, defaultValueExpression, ...)                   \
    enum class parameterName##Values_ : std::uint8_t{__VA_ARGS__, numberOfValues_};                \
    class parameterName                                                                            \
        : public LE::Parameters::EnumeratedParameter<static_cast<std::uint8_t>(                    \
                                                         parameterName##Values_::numberOfValues_), \
                                                     defaultValueExpression>                       \
    {                                                                                              \
      private:                                                                                     \
        using Base = type;                                                                         \
                                                                                                   \
      public:                                                                                      \
        parameterName(type::param_type const initialValue = Base::default_()) : Base(initialValue) \
        {                                                                                          \
        }                                                                                          \
        enum value_type : std::uint8_t                                                             \
        {                                                                                          \
            __VA_ARGS__                                                                            \
        };                                                                                         \
        operator value_type() const { return static_cast<value_type>(Base::getValue()); }          \
    }

#define LE_ENUMERATED_PARAMETER(parameterName, ...)                                                \
    LE_ENUMERATED_PARAMETER_IMPL(parameterName, 0, __VA_ARGS__)

////////////////////////////////////////////////////////////////////////////////
///
/// \def LE_ENUMERATED_PARAMETER_DEFAULTING_TO
///
/// \brief The same, with a default that is not the first value.
///
///   \p defaultValue is a *name*, and it is looked up in the very list the macro
/// is being given -- so a default that is not one of the values does not
/// compile, and inserting a value ahead of it does not silently move it. \see
/// issue #163.
///
/// \note Two thin macros over one body rather than one macro with a trait pack.
/// An enumerated parameter's declaration has never had anywhere to put a trait:
/// its arguments are the value list, all of it, and nothing in a variadic list
/// of enumerators can tell an enumerator from a trait.
///
/// \note And the default is a template argument rather than a specialisation
/// beside the parameter, which is where every other per-parameter answer in this
/// tree lives. `default_()` is read by everything that builds a parameter table,
/// by `clap_param_info.default_value` and by the parameter's own constructor; a
/// specialisation any of those could not see would quietly hand back zero rather
/// than fail. \see parameter_system.md §7.
///
////////////////////////////////////////////////////////////////////////////////

#define LE_ENUMERATED_PARAMETER_DEFAULTING_TO(parameterName, defaultValue, ...)                    \
    LE_ENUMERATED_PARAMETER_IMPL(parameterName,                                                    \
                                 static_cast<std::uint8_t>(parameterName##Values_::defaultValue),  \
                                 __VA_ARGS__)

} // namespace LE::Parameters

#endif // parameter_hpp
