////////////////////////////////////////////////////////////////////////////////
///
/// \file typeList.hpp
/// ------------------
///
///   A list of types, and the two things this codebase ever does with one:
/// visit every element, and pick the element a runtime index names.
///
///   Replaces boost::mpl::{vector, range_c} as the lists, staticForEach.hpp as
/// the visitor, and the vendored copy of Steven Watanabe's 2007 `boost::switch_`
/// proposal as the dispatcher. That last one was 140 lines of Boost.Preprocessor
/// generating a `case` per element up to BOOST_SWITCH_LIMIT, which stage 4 had
/// to raise from 50 to 64 when every build started carrying all 57 effects. A
/// pack expansion has no such limit and needs no macro.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef typeList_hpp__6D4B1A97_2E80_4F5C_B3D1_7A9E0C284F16
#define typeList_hpp__6D4B1A97_2E80_4F5C_B3D1_7A9E0C284F16
//------------------------------------------------------------------------------
#include "assert.hpp"
#include "platformSpecifics.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace LE::Utility
{

template <class... Types> struct TypeList
{
    static std::size_t constexpr size{sizeof...(Types)};
};

/// \internal
namespace Detail
{
template <typename T, class Sequence> struct IndexListImpl;

template <typename T, T... indices> struct IndexListImpl<T, std::integer_sequence<T, indices...>>
{
    using type = TypeList<std::integral_constant<T, indices>...>;
};

template <class... Types, class F> constexpr void forEach(TypeList<Types...>, F &&f)
{
    (f.template operator()<Types>(), ...);
}

template <class... Types, typename Index, class F>
typename std::remove_reference_t<F>::result_type switchOn(TypeList<Types...>, Index const index,
                                                          F &&f)
{
    using Functor = std::remove_reference_t<F>;
    using Case = typename Functor::result_type (*)(Functor &);

    /// \note The empty list has to compile and can never be called. An effect
    /// with no parameters -- Robotizer is one -- instantiates
    /// invokeFunctorOnIndexedParameter() over an empty ValidIndices, so this
    /// function exists for it; there is no index it could be reached with,
    /// because there is nothing to index. Without the `if constexpr` the table
    /// below is `Case cases[]{}`, a zero-length array, which Clang and GCC take
    /// as an extension and **MSVC rejects** (C2466, "cannot allocate an array of
    /// constant size 0").
    if constexpr (sizeof...(Types) == 0)
    {
        LE_UNREACHABLE_CODE();
    }
    else
    {
        // Implementation note:
        //   Positional, which is why switchOn() below takes only an IndexList:
        // the Boost original switched on `at_c<V, n>::type::value` and so could
        // take any list of integral constants, but it was never given one whose
        // value was not its position. A table indexed directly is the honest
        // spelling of that, and it compiles to the jump table the generated
        // `switch` was there to produce.
        static constexpr Case cases[]{
            +[](Functor &functor) -> typename Functor::result_type { return functor(Types()); }...};

        LE_ASSERT_MSG(static_cast<std::size_t>(index) < sizeof...(Types),
                      "Index outside the dispatched list.");
        LE_ASSUME(static_cast<std::size_t>(index) < sizeof...(Types));
        return cases[static_cast<std::size_t>(index)](f);
    }
}
} // namespace Detail

/// \brief TypeList<integral_constant<T, 0>, ..., integral_constant<T, count-1>>.
template <typename T, T count>
using IndexList = typename Detail::IndexListImpl<T, std::make_integer_sequence<T, count>>::type;

/// \brief Calls f.operator()<Type>() for every Type in \p List, in order.
///
/// \note The functor is a class with a template operator() rather than a generic
/// lambda because what it receives is a *type*, not a value -- there is nothing
/// to pass.
template <class List, class F> constexpr void forEach(F &&f)
{
    Detail::forEach(List(), std::forward<F>(f));
}

/// \brief Calls f(std::integral_constant<T, index>()) -- the compile-time
/// constant equal to a runtime index.
///
/// \note \p List must come from IndexList; see the note in Detail::switchOn.
/// An index outside it asserts and is thereafter assumed unreachable, which is
/// what the `assert_no_default_case` functor every call site used to pass did.
template <class List, typename Index, class F>
typename std::remove_reference_t<F>::result_type switchOn(Index const index, F &&f)
{
    return Detail::switchOn(List(), index, std::forward<F>(f));
}

} // namespace LE::Utility

#endif // typeList_hpp
