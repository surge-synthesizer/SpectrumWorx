////////////////////////////////////////////////////////////////////////////////
///
/// \file symmetric/parameter.hpp
/// -----------------------------
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef parameter_hpp__538CC108_800B_442B_882C_AD3800171536
#define parameter_hpp__538CC108_800B_442B_882C_AD3800171536
//------------------------------------------------------------------------------
#include "tag.hpp"

#include "le/parameters/linear/parameter.hpp"

#include "le/utility/assert.hpp"

#include <cstdint>
#include <type_traits>

namespace LE::Parameters
{

/// \note MaximumOffset is declared beside the other five traits in
/// le/parameters/parameter.hpp: a trait is a {tag, value} pair and nothing about
/// this one is symmetric-specific, and the namespaces that declare parameters
/// import the six of them together.

namespace Detail ///< \internal
{
template <class Traits, class... Defaults> struct SymmetricFloatParameterTraits;
template <class... TraitsParam, class... DefaultsParam>
struct SymmetricFloatParameterTraits<TraitPack<TraitsParam...>, DefaultsParam...>
    : LinearFloatParameterTraitsBase<
          0 - static_cast<std::int16_t>(
                  GetTraitDefaulted<Traits::Tag::MaximumOffset, TraitPack<TraitsParam...>,
                                    DefaultsParam...>::type::value),
          0 + GetTraitDefaulted<Traits::Tag::MaximumOffset, TraitPack<TraitsParam...>,
                                DefaultsParam...>::type::value,
          0,
          GetTraitDefaulted<Traits::Tag::ValuesDenominator, TraitPack<TraitsParam...>,
                            DefaultsParam...>::type::value>
{
  public:
    using Tag = SymmetricFloatParameterTag;

    using Traits = TraitPack<TraitsParam...>;
    using Defaults = TraitPack<DefaultsParam...>;

    template <class... NewTraits> struct Modify
    {
        using type = SymmetricFloatParameterTraits<TraitPack<NewTraits...>, TraitsParam...>;
    };
};

template <class Traits, class... Defaults> struct SymmetricIntegerParameterTraits;
template <class... TraitsParam, class... DefaultsParam>
struct SymmetricIntegerParameterTraits<TraitPack<TraitsParam...>, DefaultsParam...>
    : LinearParameterTraitsBase<
          0 - static_cast<int>(
                  GetTraitDefaulted<Traits::Tag::MaximumOffset, TraitPack<TraitsParam...>,
                                    DefaultsParam...>::type::value),
          0 + GetTraitDefaulted<Traits::Tag::MaximumOffset, TraitPack<TraitsParam...>,
                                DefaultsParam...>::type::value,
          0>
{
  public: // Types.
    using Tag = SymmetricIntegerParameterTag;

    using value_type = std::int16_t;
    using param_type = value_type;

    using Traits = TraitPack<TraitsParam...>;
    using Defaults = TraitPack<DefaultsParam...>;

    template <class... NewTraits> struct Modify
    {
        using type = SymmetricIntegerParameterTraits<TraitPack<NewTraits...>, TraitsParam...>;
    };

  public: // Values
    static unsigned int const rangeValuesDenominator = 1;

    static value_type minimum() { return SymmetricIntegerParameterTraits::unscaledMinimum; }
    static value_type maximum() { return SymmetricIntegerParameterTraits::unscaledMaximum; }
    static value_type default_() { return SymmetricIntegerParameterTraits::unscaledDefault; }

    static unsigned int const discreteValueDistance = 1;

    static bool isValidValue(value_type const value)
    {
        return isValueInRange<param_type>(value, minimum(), maximum());
    }
};
} // namespace Detail

////////////////////////////////////////////////////////////////////////////////
/// \internal
/// \struct SymmetricInteger
////////////////////////////////////////////////////////////////////////////////

struct SymmetricInteger
{
    template <class... NewTraits> struct Modify
    {
        using type =
            Detail::SymmetricIntegerParameterTraits<TraitPack<NewTraits...>,
                                                    Traits::ValuesDenominator<1>, Traits::Unit<"">>;
    };
};

////////////////////////////////////////////////////////////////////////////////
/// \internal
/// \struct SymmetricFloat
////////////////////////////////////////////////////////////////////////////////

struct SymmetricFloat
{
    template <class... NewTraits> struct Modify
    {
        using type =
            Detail::SymmetricFloatParameterTraits<TraitPack<NewTraits...>,
                                                  Traits::ValuesDenominator<1>, Traits::Unit<"">>;
    };
};

} // namespace LE::Parameters

#endif // parameter_hpp
