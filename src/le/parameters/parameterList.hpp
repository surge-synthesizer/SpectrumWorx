////////////////////////////////////////////////////////////////////////////////
///
/// \file parameterList.hpp
/// -----------------------
///
///   The container LE_DEFINE_PARAMETERS gives an effect: the parameters
/// themselves, and the four things anything ever asks of them -- how many there
/// are, the type at a position, the position of a type, and the object of a
/// type.
///
///   All four were generated per parameter by a Boost.PP sequence walk, one
/// ParameterAt specialisation, one IndexOf specialisation, one get() overload
/// and one member each. They are a function of the parameter list alone, so
/// they are written once here and the macro names the list.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef parameterList_hpp__0C4E9B15_7A32_4D68_9F21_6B8D5E3A47C0
#define parameterList_hpp__0C4E9B15_7A32_4D68_9F21_6B8D5E3A47C0
//------------------------------------------------------------------------------
#include "le/utility/assert.hpp" // LE_ASSUME

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

namespace LE::Parameters
{

namespace Detail ///< \internal
{

////////////////////////////////////////////////////////////////////////////////
///
/// \struct ParameterSlot
/// \internal
/// \brief One parameter, at one position.
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   The position is in the type so that get<Parameter>() can name the slot it
// wants. A container cannot in fact hold the same parameter twice -- the macro
// this replaces would have emitted two identical get() overloads and failed to
// compile -- so the index is not there to disambiguate, it is there to make the
// cast in get() writable.
////////////////////////////////////////////////////////////////////////////////

template <std::size_t index, class Parameter> struct ParameterSlot
{
    Parameter parameter_;
}; // struct ParameterSlot

template <class Indices, class... Parameters> struct ParameterSlots;

template <std::size_t... indices, class... Parameters>
struct ParameterSlots<std::index_sequence<indices...>, Parameters...>
    : ParameterSlot<indices, Parameters>...
{
}; // struct ParameterSlots

/// \brief The position of \p Parameter in the pack, or sizeof...(Parameters) if
/// it is not in it -- the "not one of mine" answer IndexOf has always given.
template <class Parameter, class... Parameters> constexpr std::uint8_t indexOf()
{
    // The trailing false keeps the array non-empty for a parameterless container.
    bool constexpr matches[]{std::is_same_v<Parameter, Parameters>..., false};
    for (std::uint8_t index{0}; index < sizeof...(Parameters); ++index)
        if (matches[index])
            return index;
    return sizeof...(Parameters);
}

} // namespace Detail

////////////////////////////////////////////////////////////////////////////////
///
/// \class ParameterList
///
/// \brief The Parameters container: the parameters in declaration order, and
/// static_size / ParameterAt / IndexOf / get / set over them.
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   Storage is a base class per parameter rather than a std::tuple because the
// layout is load bearing in two ways that a tuple does not honour.
//
//   The first parameter must be at offset zero. Module::parameterOffset()
// carries LE_ASSUME( pParameterOffsets_[ 0 ] == 0 ), which is an assumption the
// optimiser acts on rather than an assert -- and libc++ lays a tuple out in
// reverse, so the first parameter would land last.
//
//   The parameters must be laid out flat. Offsets into the container are
// std::uint8_t, so they have to fit in 255 bytes: Tune Worx declares
// thirty-odd parameters, two dozen of them booleans. Bases are allocated in
// declaration order and pack like members do, so the run of booleans stays a
// run of bytes. A recursively nested holder -- struct { P first; Holder rest; }
// -- would instead align every level to the widest parameter still to come and
// give each of those booleans three bytes of padding.
////////////////////////////////////////////////////////////////////////////////

template <class... Params>
class ParameterList : private Detail::ParameterSlots<std::index_sequence_for<Params...>, Params...>
{
  public:
    static std::uint8_t const static_size = static_cast<std::uint8_t>(sizeof...(Params));
    using size = std::integral_constant<std::uint8_t, static_size>;

    /// \brief The type of the parameter at \p index.
    template <unsigned int index> struct ParameterAt
    {
        using type = std::tuple_element_t<index, std::tuple<Params...>>;
    };

    /// \brief The position of \p Parameter, or static_size if it is not ours.
    template <class Parameter>
    struct IndexOf : std::integral_constant<std::uint8_t, Detail::indexOf<Parameter, Params...>()>
    {
    };

    template <class Parameter> Parameter &get() { return slot<Parameter>(*this).parameter_; }
    template <class Parameter> Parameter const &get() const
    {
        return slot<Parameter>(*this).parameter_;
    }

    template <class Parameter> void set(typename Parameter::param_type const value)
    {
        get<Parameter>().setValue(value);
    }

  private:
    template <class Parameter, class Self> static constexpr auto &slot(Self &self)
    {
        using Slot = Detail::ParameterSlot<IndexOf<Parameter>::value, Parameter>;
        return static_cast<std::conditional_t<std::is_const_v<Self>, Slot const, Slot> &>(self);
    }
}; // class ParameterList

} // namespace LE::Parameters

#endif // parameterList_hpp
