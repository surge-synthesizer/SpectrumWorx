////////////////////////////////////////////////////////////////////////////////
///
/// \file parametersUtilities.hpp
/// -----------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef parametersUtilities_hpp__EB1C0F5A_FC45_4407_A713_9197376BC784
#define parametersUtilities_hpp__EB1C0F5A_FC45_4407_A713_9197376BC784
//------------------------------------------------------------------------------
#include "le/utility/assert.hpp" // LE_ASSUME
#include "le/utility/cstdint.hpp"
#include "le/utility/typeList.hpp"

#include <type_traits>
#include <utility>

namespace LE::Parameters
{

////////////////////////////////////////////////////////////////////////////////
//
// The Parameters sequence
// -----------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief The index'th parameter *type* of a Parameters container, and a
/// reference to the index'th parameter *object* in one.
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   These two were boost::fusion::result_of::value_at and boost::fusion::at,
// reaching the container through 200 lines of at_impl / value_at_impl /
// begin_impl / deref_impl specialisations in fusionAdaptors.hpp. Every one of
// them forwarded to ParameterAt and get<>(), which LE_DEFINE_PARAMETERS
// generates and which are right here -- so the adaptation was a translation of
// the container's own interface into a vocabulary nothing else spoke.
////////////////////////////////////////////////////////////////////////////////

template <class Parameters, std::size_t index>
using ParameterAt = typename std::remove_const_t<Parameters>::template ParameterAt<index>::type;

template <std::size_t index, class Parameters> constexpr decltype(auto) at(Parameters &parameters)
{
    return parameters.template get<ParameterAt<Parameters, index>>();
}

/// \brief Calls f(parameter) for every parameter in the container, in
/// declaration order. Was boost::fusion::for_each.
template <class Parameters, class Functor> void forEach(Parameters &parameters, Functor &&functor)
{
    [&]<std::size_t... indices>(std::index_sequence<indices...>) {
        (functor(at<indices>(parameters)), ...);
    }(std::make_index_sequence<std::remove_const_t<Parameters>::static_size>());
}

/// \brief The same, last parameter first.
template <class Parameters, class Functor>
void forEachReversed(Parameters &parameters, Functor &&functor)
{
    [&]<std::size_t... indices>(std::index_sequence<indices...>) {
        constexpr std::size_t last{std::remove_const_t<Parameters>::static_size - 1};
        (functor(at<last - indices>(parameters)), ...);
    }(std::make_index_sequence<std::remove_const_t<Parameters>::static_size>());
}

////////////////////////////////////////////////////////////////////////////////
///
/// Private implementation details for this module.
///
////////////////////////////////////////////////////////////////////////////////

namespace Detail
{
////////////////////////////////////////////////////////////////////////////
///
/// \class Invoker
/// \internal
/// \brief Helper functor for invoking the parameter Functor with a proper
/// reference to a parameter object along with the parameter index.
///
////////////////////////////////////////////////////////////////////////////

template <class Parameters, class Functor> class Invoker : private Functor
{
  public:
    Invoker(Parameters &parameters, Functor &&functor) : Functor(functor), pParameters_(&parameters)
    {
    }

    typedef typename Functor::result_type result_type;

    template <class TypeIndex> result_type operator()(TypeIndex const &) const
    {
        return Functor::operator()(at<TypeIndex::value>(*pParameters_));
    }

  private:
    void operator=(Invoker const &);
    Parameters *LE_RESTRICT const pParameters_;
}; // class Invoker

template <class Parameters, class Functor> class StaticInvoker : public Functor
{
  public:
    typedef typename Functor::result_type result_type;

    template <class TypeIndex> result_type operator()(TypeIndex const &)
    {
        using Parameter = ParameterAt<Parameters, TypeIndex::value>;
        /// \note Functor is a template parameter, so the `template` keyword is
        /// required rather than a GNU extension -- without it `<` parses as
        /// less-than and the error surfaces at the call site rather than here.
        return Functor::template operator()<Parameter>();
    }

    template <class TypeIndex> result_type operator()(TypeIndex const &typeIndex) const
    {
        return const_cast<StaticInvoker &>(*this)(typeIndex);
    }

  private:
    StaticInvoker();
    ~StaticInvoker();
    StaticInvoker(StaticInvoker const &);
    void operator=(StaticInvoker const &);
}; // class StaticInvoker

template <class WrappingFunctor, class SourceFunctor>
WrappingFunctor &&forwardDownCast(SourceFunctor &sourceFunctor)
{
    return std::forward<WrappingFunctor>(static_cast<WrappingFunctor &>(sourceFunctor));
}
template <class WrappingFunctor, class SourceFunctor>
WrappingFunctor const &&forwardDownCast(SourceFunctor const &sourceFunctor)
{
    return std::forward<WrappingFunctor const>(static_cast<WrappingFunctor const &>(sourceFunctor));
}

template <class Parameters, class Functor>
typename Functor::result_type invokeFunctorOnIndexedParameter(std::uint8_t const parameterIndex,
                                                              Functor &&functor)
{
    LE_ASSUME(parameterIndex < Parameters::static_size);

    using ValidIndices = Utility::IndexList<std::uint8_t, Parameters::static_size>;
    return Utility::switchOn<ValidIndices>(parameterIndex, std::forward<Functor>(functor));
}
} // namespace Detail

////////////////////////////////////////////////////////////////////////////////
//
// forEachIndexed()
// ----------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief An extension to forEach() above that also passes the index
/// of the parameter as a template parameter to the passed functor.
///
/// \throws <anything_the_passed_functor's_operator()_may_throw>
///
////////////////////////////////////////////////////////////////////////////////

template <class Parameters, class Functor>
void forEachIndexed(Parameters &parameters, Functor &&functor)
{
    using ValidIndices = Utility::IndexList<std::uint8_t, Parameters::static_size>;
    using WrappedFunctor = Detail::Invoker<Parameters, Functor>;
    WrappedFunctor wrappedFunctor(parameters, functor);
    Utility::forEach<ValidIndices>(wrappedFunctor);
}

template <class Parameters, class Functor> void forEachIndexed(Functor &&functor)
{
    using ValidIndices = Utility::IndexList<std::uint8_t, Parameters::static_size>;
    using Invoker = Detail::StaticInvoker<Parameters, Functor>;
    Utility::forEach<ValidIndices>(Detail::forwardDownCast<Invoker>(functor));
}

////////////////////////////////////////////////////////////////////////////
//
// invokeFunctorOnIndexedParameter()
// ---------------------------------
//
////////////////////////////////////////////////////////////////////////////
///
/// \brief Enables runtime selection of a Parameter object based on its
/// index.
///
/// \param parameterIndex - index of the desired parameter
/// \param functor        - functor you wish to be invoked with the indexed
///                         parameter object
/// \return - returns the passed functor's return value
///
/// \throws <anything_the_passed_functor's_operator()_may_throw>
///
////////////////////////////////////////////////////////////////////////////
//
// Implementation note:
//   Because Parameter objects contained in a Parameters instance are all
// of a different type, bind and lambda expressions cannot be used to
// 'manipulate' them (contained Parameter objects) and invoke their member
// functions (as the passed functor's operator() has to be a template
// function). For this reason, with the current implementation, you must
// write manual functors to do the desired task.
//                                        (11.05.2009.) (Domagoj Saric)
//
////////////////////////////////////////////////////////////////////////////
// (todo)
//   Document requirements for passed functor objects.
//                                        (13.05.2009.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////

template <class Parameters, class Functor>
typename Functor::result_type invokeFunctorOnIndexedParameter(Parameters &parameters,
                                                              std::uint8_t const parameterIndex,
                                                              Functor &&functor)
{
    return Detail::invokeFunctorOnIndexedParameter<Parameters>(
        parameterIndex,
        Detail::Invoker<Parameters, Functor>(parameters, std::forward<Functor>(functor)));
}

template <class Parameters, class Functor>
typename Functor::result_type invokeFunctorOnIndexedParameter(std::uint8_t const parameterIndex,
                                                              Functor const &functor)
{
    typedef Detail::StaticInvoker<Parameters, Functor> Invoker;
    return Detail::invokeFunctorOnIndexedParameter<Parameters>(
        parameterIndex, Detail::forwardDownCast<Invoker>(functor));
}

////////////////////////////////////////////////////////////////////////////////
//
// IndexOf
// -------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Calculates the index (position or order) of a Parameter.
///
////////////////////////////////////////////////////////////////////////////////

template <class Parameters, class Parameter>
using IndexOf = typename Parameters::template IndexOf<Parameter>;

} // namespace LE::Parameters

#endif // parametersUtilities_hpp
