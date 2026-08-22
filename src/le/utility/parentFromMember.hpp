////////////////////////////////////////////////////////////////////////////////
///
/// \file parentFromMember.hpp
/// --------------------------
///
/// Utility classes for getting an object's parent object (that holds it by
/// value, i.e. it is its member).
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef parentFromMember_hpp__185E793C_ECDF_4DFA_9136_5C9E5CA4F353
#define parentFromMember_hpp__185E793C_ECDF_4DFA_9136_5C9E5CA4F353
//------------------------------------------------------------------------------
#include "le/utility/platformSpecifics.hpp"

#include "le/parameters/parametersUtilities.hpp"

#include <optional>
#include <type_traits>

namespace LE::Utility
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class DummyStorage
///
/// \brief A helper class creating "empty"/trivially constructed and destructed
/// temporary objects that have the same size and alignment as the class it is
/// trying to impersonate/act as its placeholder (the class specified in the
/// template parameter).
///
////////////////////////////////////////////////////////////////////////////////

template <class T> struct DummyStorage
{
    using Storage = typename std::aligned_storage<sizeof(T), std::alignment_of<T>::value>::type;
    DummyStorage() {}
    DummyStorage(DummyStorage const &);
    /// \note Was `#ifdef __GNUC__`, which put clang-cl on the const arm and
    /// broke it. What decides this one is the language rather than the dialect --
    /// a const member left uninitialised by a constructor is ill-formed, and
    /// -fms-compatibility does not excuse it -- so the question is which
    /// compilers enforce the rule, and Clang does under every driver it has.
#if defined(__GNUC__) || defined(__clang__)
    Storage storage_; // const on MSVC only: GCC/Clang reject an uninitialised const member
#else
    Storage const storage_;
#endif
    T const &impersonate() const { return reinterpret_cast<T const &>(storage_); }
};

#pragma warning(push)
#pragma warning(disable : 4269) // 'const' automatic data initialized with
                                // compiler generated default constructor
                                // produces unreliable results

////////////////////////////////////////////////////////////////////////////////
///
/// \class ParentFromMember
///
/// \brief In the following scenario:
///    class B {};
///    class A { B b; }
/// ParentFromMember<A, B, &A::b> retrieves a reference to A given a reference
/// to B (assumes that the reference to B is a reference to a B object that is
/// actually a part of an A object).
///
////////////////////////////////////////////////////////////////////////////////

template <class ParentParam, class MemberParam, MemberParam ParentParam::*pMember>
class ParentFromMember
{
  public:
    using Parent = ParentParam;
    using Member = MemberParam;

  public:
    LE_FORCEINLINE Parent &operator()(Member &member) const
    {
        char *const pOpaqueMember(reinterpret_cast<char *>(&member));
        LE_ASSUME(pOpaqueMember);
        Parent const *const pDummyParent(reinterpret_cast<Parent const *>(pOpaqueMember));
        LE_ASSUME(pDummyParent);
        Member const *const pDummyMember(&(pDummyParent->*pMember));
        LE_ASSUME(pDummyMember);
        unsigned int const offset(static_cast<unsigned int>(
            reinterpret_cast<char const *>(pDummyMember) - pOpaqueMember));
        Parent *const pParent(reinterpret_cast<Parent *>(pOpaqueMember - offset));
        LE_ASSUME(pParent);
        return *pParent;
    }

    LE_FORCEINLINE Parent const &operator()(Member const &member) const
    {
        return operator()(const_cast<Member &>(member));
    }
}; // class ParentFromMember

////////////////////////////////////////////////////////////////////////////////
///
/// \class ParametersFromMember
///
/// \brief Retrieves a reference to a Parameters container given a member index
/// (needed because a container may hold two parameters of the same type, so an
/// index to a member cannot be deduced from its type) and a reference to a
/// corresponding member (assumes that the member referenced is actually a part
/// of the container object).
///
////////////////////////////////////////////////////////////////////////////////

template <class ParametersParam, unsigned int memberIndex> class ParametersFromMember
{
  public:
    using Parameters = ParametersParam;
    using Member = LE::Parameters::ParameterAt<Parameters, memberIndex>;

  public:
    LE_FORCEINLINE Parameters &operator()(Member &member) const
    {
        DummyStorage<Parameters> const fakeContainer;

        ptrdiff_t const offset(reinterpret_cast<char const *>(&LE::Parameters::at<memberIndex>(
                                   reinterpret_cast<Parameters const &>(fakeContainer))) -
                               reinterpret_cast<char const *>(&fakeContainer));

        return *reinterpret_cast<Parameters *>(reinterpret_cast<char *>(&member) - offset);
    }

    LE_FORCEINLINE Parameters const &operator()(Member const &member) const
    {
        return operator()(const_cast<Member &>(member));
    }
}; // class ParametersFromMember

////////////////////////////////////////////////////////////////////////////////
///
/// \class OptionalFromInstance
///
/// \brief In the following scenario:
///    class A {};
///    typedef std::optional<A> OptionalA;
/// OptionalFromInstance<A> retrieves a reference to OptionalA given a reference
/// to A (assumes that the reference to A is a reference to an A object that is
/// actually stored in a std::optional<>).
///
////////////////////////////////////////////////////////////////////////////////

template <typename T, bool optionalMustBeInitialised = true> class OptionalFromInstance
{
  public:
    LE_FORCEINLINE std::optional<T> &operator()(T &instance) const
    {
        //...mrmlj...assumptions about optional internals...
        /// \note Boost.Optional put its engaged flag first, so this used to
        /// subtract the payload offset. libstdc++, libc++ and the MS STL all
        /// store std::optional's payload at offset zero and the flag after it,
        /// so the instance and its optional share an address.
        static_assert(sizeof(std::optional<T>) >= sizeof(T), "");
        auto &optionalInstance(
            *reinterpret_cast<std::optional<T> *>(reinterpret_cast<char *>(&instance)));
        LE_ASSERT(!optionalMustBeInitialised ||
                  (optionalInstance.has_value() && &*optionalInstance == &instance));
        return optionalInstance;
    }

    LE_FORCEINLINE std::optional<T> const &operator()(T const &instance) const
    {
        return operator()(const_cast<T &>(instance));
    }
}; // OptionalFromInstance

////////////////////////////////////////////////////////////////////////////////
///
/// \class ParentFromOptionalMember
///
/// \brief Same as ParentFromMember but works with objects wrapped/held in
/// std::optionals.
///
////////////////////////////////////////////////////////////////////////////////

template <class ParentParam, class MemberParam, std::optional<MemberParam> ParentParam::*pMember,
          bool optionalMustBeInitialised = true>
class ParentFromOptionalMember
{
  public:
    using Parent = ParentParam;
    using Member = MemberParam;

  public:
    LE_FORCEINLINE Parent &operator()(Member &member) const
    {
        return ParentFromMember<Parent, std::optional<Member>, pMember>()(
            OptionalFromInstance<Member, optionalMustBeInitialised>()(member));
    }

    LE_FORCEINLINE Parent const &operator()(Member const &member) const
    {
        return operator()(const_cast<Member &>(member));
    }
}; // class ParentFromOptionalMember

////////////////////////////////////////////////////////////////////////////////
///
/// \class MemberFromMember
///
/// \brief Wraps referencing between member of a (parent) class.
///
////////////////////////////////////////////////////////////////////////////////

template <class ParentParam, class SourceMemberParam, class TargetMemberParam,
          SourceMemberParam ParentParam::*pSourceMember,
          TargetMemberParam ParentParam::*pTargetMember>
class MemberFromMember : private ParentFromMember<ParentParam, SourceMemberParam, pSourceMember>
{
  public:
    using Parent = typename std::remove_const<ParentParam>::type;
    using SourceMember = typename std::remove_const<SourceMemberParam>::type;
    using TargetMember = typename std::remove_const<TargetMemberParam>::type;

  public:
    LE_FORCEINLINE TargetMember &operator()(SourceMember &sourceMember) const
    {
        Parent &parent(
            ParentFromMember<Parent, SourceMember, pSourceMember>::operator()(sourceMember));
        return parent.*pTargetMember;
    }

    LE_FORCEINLINE TargetMember const &operator()(SourceMember const &sourceMember) const
    {
        return operator()(const_cast<SourceMember &>(sourceMember));
    }
}; // class MemberFromMember

#pragma warning(pop)

} // namespace LE::Utility

#endif // parentFromMember_hpp
