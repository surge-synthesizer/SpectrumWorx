////////////////////////////////////////////////////////////////////////////////
///
/// \file parameter.hpp
/// -------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef parameter_hpp__B49E51E6_E59F_4C49_A702_B6533579846D
#define parameter_hpp__B49E51E6_E59F_4C49_A702_B6533579846D
//------------------------------------------------------------------------------
#include "unitString.hpp"

#include "le/utility/assert.hpp"

#include <cstdint>
#include <type_traits>

namespace LE::Parameters
{

namespace Traits
{

////////////////////////////////////////////////////////////////////////////////
//
// Parameter "traits" (or "properties" or "options") classes.
// ----------------------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \defgroup ParameterProperties Parameter properties
///
///   These are actually a form of 'named' parameters for the Parameter class.
/// They define their underlying type and their default value (which is used if
/// the trait/property/option is not explicitly specified in a Parameter
/// declaration.
///
////////////////////////////////////////////////////////////////////////////////
///
/// \struct Minimum
/// \ingroup ParameterProperties
///
////////////////////////////////////////////////////////////////////////////////
///
/// \struct Maximum
/// \ingroup ParameterProperties
///
////////////////////////////////////////////////////////////////////////////////
///
/// \struct Default
/// \ingroup ParameterProperties
///
////////////////////////////////////////////////////////////////////////////////
///
/// \struct ValuesDenominator
/// \ingroup ParameterProperties
///
///   A value with which the default and range values are divided to calculate
/// their real values (used for floating point type parameters, specifying them
/// as rational numbers, e.g.
/// 'real minimum value' = minimumValue / valuesDenominator.
///
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
/// \struct TraitPair
/// \internal
/// \brief A {tag, value} association, which is all a trait ever was.
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   Was boost::mpl::pair. Nothing here uses an MPL sequence algorithm on a
// trait; GetTraitImpl (linear/parameter.hpp) partially specialises on the pair
// and reads `second` off it, and that is the entire protocol.
////////////////////////////////////////////////////////////////////////////////

template <class Tag, class Value> struct TraitPair
{
    /// \note GetTrait (linear/parameter.hpp) expands `TraitsSequence::type...`,
    /// so a trait has to answer to that the way an MPL metafunction did.
    using type = TraitPair;
    using first = Tag;
    using second = Value;
};

/// \brief What GetTrait answers with when a trait is in neither the parameter's
/// own list nor its defaults. Was boost::mpl::void_.
struct NoTrait
{
    using type = NoTrait;
};

// Helper verbosity-reducing macros for Parameter trait declarations.

#define DECLARE_PARAMETER_TRAIT(name, valueType)                                                   \
    namespace Tag                                                                                  \
    {                                                                                              \
    struct name;                                                                                   \
    }                                                                                              \
    template <valueType vvalue>                                                                    \
    struct name : TraitPair<Tag::name, std::integral_constant<valueType, vvalue>>                  \
    {                                                                                              \
    }

DECLARE_PARAMETER_TRAIT(Minimum, int);                    /// \ingroup ParameterProperties
DECLARE_PARAMETER_TRAIT(Maximum, int);                    /// \ingroup ParameterProperties
DECLARE_PARAMETER_TRAIT(Default, int);                    /// \ingroup ParameterProperties
DECLARE_PARAMETER_TRAIT(ValuesDenominator, unsigned int); /// \ingroup ParameterProperties

// Helper macro cleanup.
#undef DECLARE_PARAMETER_TRAIT

////////////////////////////////////////////////////////////////////////////////
///
/// \struct MaximumOffset
/// \ingroup ParameterProperties
/// \brief The range of a symmetric parameter, which runs from -offset to
/// +offset.
///
////////////////////////////////////////////////////////////////////////////////
/// \note Declared here rather than in symmetric/parameter.hpp, where it used to
/// be: a trait is a {tag, value} pair and there is nothing symmetric-specific
/// about this one, and the namespaces that declare parameters import the six
/// traits together.

namespace Tag
{
struct MaximumOffset;
}

template <std::uint16_t value>
using MaximumOffset = TraitPair<Tag::MaximumOffset, std::integral_constant<std::int16_t, value>>;

////////////////////////////////////////////////////////////////////////////////
///
/// \class Unit
/// \ingroup ParameterProperties
/// \brief Specifies the unit type/dimension of a parameter through a string
/// suffix (used for 'printing' of parameter values).
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   Was a pair of ints holding multi-character literals -- Unit< ' dB' > --
// with a four-characters-per-argument ceiling and a companion `Unit2` trait
// that existed only because a comma cannot appear inside a Boost.PP sequence
// element. A C++20 class-type NTTP has neither problem, so `Unit2` is gone and
// the two effects that needed it (frecho, slew_limiter) say their unit inline.
////////////////////////////////////////////////////////////////////////////////

namespace Tag
{
struct Unit;
}

template <FixedString text> struct Unit : TraitPair<Tag::Unit, UnitString<text>>
{
};

} // namespace Traits

////////////////////////////////////////////////////////////////////////////////
///
/// \class Parameter
///
///   Implements the Parameter concept based on the specified traits.
///
///   None of its member functions may throw.
///
////////////////////////////////////////////////////////////////////////////////

template <class ImplTraits> class Parameter : public ImplTraits
{
  public:
    typedef Parameter type;

    typedef typename ImplTraits::value_type value_type;
    typedef typename ImplTraits::param_type param_type;
    typedef value_type binary_type;

  public:
    // Intentional implicit conversion.
    Parameter(param_type const initialValue = ImplTraits::default_())
    {
        // Traits sanity checks.
        LE_ASSERT(this->isValidValue(ImplTraits::minimum()));
        LE_ASSERT(this->isValidValue(ImplTraits::default_()));
        LE_ASSERT(this->isValidValue(ImplTraits::maximum()));

        LE_ASSERT(ImplTraits::maximum() >= ImplTraits::minimum());

        setValue(initialValue);
    }

    value_type const &getValue() const
    {
        LE_ASSERT_MSG(this->isValidValue(value_), "Parameter in inconsistent state.");
        return value_;
    }
    void setValue(param_type value)
    {
        LE_ASSERT_MSG(this->isValidValue(value),
                      "Specified value is invalid or out of range for this parameter.");
        value_ = value;
    }

    void reset() { setValue(ImplTraits::default_()); }

  public:
    operator value_type const &() const { return getValue(); }

    Parameter &operator++()
    {
        LE_ASSERT_MSG(getValue() != ImplTraits::maximum(),
                      "Tried to increment a parameter past its maximum value.");
        ImplTraits::increment(value_);
        LE_ASSERT_MSG(ImplTraits::isValidValue(value_),
                      "Parameter in inconsistent state after increment.");
        return *this;
    }

    Parameter &operator--()
    {
        LE_ASSERT_MSG(getValue() != ImplTraits::minimum(),
                      "Tried to decrement a parameter below its minimum value.");
        ImplTraits::decrement(value_);
        LE_ASSERT_MSG(ImplTraits::isValidValue(value_),
                      "Parameter in inconsistent state after decrement.");
        return *this;
    }

    bool operator!=(param_type other) const { return this->getValue() != other; }

  protected:
    value_type value_;

  private:
    static_assert(ImplTraits::unscaledMaximum >= ImplTraits::unscaledMinimum, "Invalid range.");

    static_assert((ImplTraits::unscaledDefault >= ImplTraits::unscaledMinimum) &&
                      (ImplTraits::unscaledDefault <= ImplTraits::unscaledMaximum),
                  "Default value out of range.");
}; // class Parameter

template <class OriginalParameter, class... NewTraits> struct Modify
{
    typedef typename OriginalParameter::template Modify<NewTraits...>::type ModifiedTraits;
    typedef Parameter<ModifiedTraits> type;
};

/// \brief Changing nothing is the original parameter, not a Parameter<> rebuilt
/// from its traits.
///
/// \note The difference is what a parameter adds on top of Parameter<>:
/// TriggerParameter's consumeValue(), which reads the trigger and disarms it, is
/// not a trait and does not survive the round trip through one. This is the case
/// LE_DEFINE_PARAMETER used to spell as a separate macro, chosen by counting the
/// elements of a Boost.PP sequence.
template <class OriginalParameter> struct Modify<OriginalParameter>
{
    typedef OriginalParameter type;
};

////////////////////////////////////////////////////////////////////////////////
///
/// Helper verbosity reducing macros for parameter specifications.
/// --------------------------------------------------------------
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   In order to create unique types (for individual parameters), structs that
// publicly derive from a specialized templates are used instead of typedefs.
// This also gives far more readable compiler and linker errors and also seems
// to slightly improve build times.
//                                            (25.09.2009.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
/// \def LE_DEFINE_PARAMETER
/// \brief Creates the struct representing the individual parameter and adds the
/// forwarding constructor (to minimize the difference between a typedef
/// parameter specification and a struct that publicly derives from Parameter<>.
///
///   LE_DEFINE_PARAMETER( Interval, SymmetricFloat, MaximumOffset<24>, Unit<" st"> )
/// -- the name to give the parameter, the parameter to build it out of, and the
/// traits to change. Naming no trait renames: the new parameter has the same
/// traits as the one it was built from.
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   Was a Boost.PP sequence, ( Interval )( SymmetricFloat )( MaximumOffset<24> ),
// walked to prefix every trait with LE::Parameters::Traits:: and counted to tell
// the rename case from the modify case. Modify already takes a pack and already
// accepts an empty one -- Boolean::Modify<> is a specialisation that exists --
// so __VA_ARGS__ goes straight through and the two cases are one.
//
//   The traits are no longer qualified here, so they have to be visible where
// the parameter is declared; the namespaces that declare parameters import them
// beside the parameter types they already import.
////////////////////////////////////////////////////////////////////////////////

#define LE_DEFINE_PARAMETER(name, ...)                                                             \
    class name : public ::LE::Parameters::Modify<__VA_ARGS__>::type                                \
    {                                                                                              \
      public:                                                                                      \
        name(type::value_type const initialValue = type::default_()) { setValue(initialValue); }   \
    }

} // namespace LE::Parameters

#endif // parameter_hpp
