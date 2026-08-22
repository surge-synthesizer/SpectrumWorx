////////////////////////////////////////////////////////////////////////////////
///
/// \file parameterID.hpp
/// ---------------------
///
/// Copyright (c) 2013 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef parameterID_hpp__28D1B16C_8F91_4EF2_A26C_151AEDFEEB03
#define parameterID_hpp__28D1B16C_8F91_4EF2_A26C_151AEDFEEB03
//------------------------------------------------------------------------------
#include "le/utility/cstdint.hpp"
#include "le/utility/platformSpecifics.hpp"

namespace LE
{

namespace Plugins
{
struct ParameterID;
struct ParameterIndex;
} // namespace Plugins

namespace SW
{

union ParameterID
{
#pragma warning(push)
#pragma warning(disable : 4480) // Nonstandard extension used: specifying underlying type for enum.
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note **Zero-based, and it stays that way.** The discriminator is the high
    /// byte of the packed value, so the first global parameter's ID is
    /// `0x00000000` -- legal as a `clap_id`, since only `CLAP_INVALID_ID` is
    /// reserved, but indistinguishable from an uninitialised one in a log or a
    /// debugger.
    ///
    ///   That is a real annoyance and it is the lesser one. Starting the enum at
    /// one was tried on 07.08.2026 and reverted the same day: it makes the
    /// discriminator disagree with every other index in this system, all of which
    /// count from zero, and the value a default-constructed ParameterID holds
    /// stops being a parameter at all -- so `invokeFunctorOnIdentifiedParameter`
    /// takes its unreachable default, and a zero ID goes from meaning "In" to
    /// meaning undefined behaviour in a release build.
    ///
    ///   Living with `In == 0x00000000` costs a moment's confusion at a debugger.
    /// The alternative costs an off-by-one in the one field that says what a
    /// packed ID *is*.
    ///
    ////////////////////////////////////////////////////////////////////////////

    enum Type : std::uint8_t
    {
        GlobalParameter,
        ModuleChainParameter,
        ModuleParameter,
        LFOParameter
    };
    enum Padding : std::uint8_t
    {
        Zero = 0
    };
#pragma warning(pop)

    /// \note Members in reverse order in order to get more intuitive/nicer
    /// hex binary ID values on little endian systems.
    ///                                       (20.03.2013.) (Domagoj Saric)
    /// \todo This naked struct approach is error prone in that the various
    /// members can be assigned in the wrong order. Fix this factory functions
    /// or C99 struct initialisers (constructors cannot be used in unions)
    ///                                           (20.03.2013.) (Domagoj Saric)
    struct Global
    {
        Padding padding0;
        Padding padding1;
        std::uint8_t index;
    };
    struct ModuleChain
    {
        Padding padding0;
        Padding padding1;
        std::uint8_t moduleIndex;
    };
    struct Module
    {
        Padding padding0;
        std::uint8_t moduleParameterIndex;
        std::uint8_t moduleIndex;
    };
    struct LFO
    {
        std::uint8_t lfoParameterIndex;
        std::uint8_t moduleParameterIndex;
        std::uint8_t moduleIndex;
    };

    struct Value
    {
        union
        {
            Global global;
            ModuleChain moduleChain;
            Module module;
            LFO lfo;
        } _;
        Type type;
    }; // struct Value

    using BinaryValue = std::uint32_t;

    static_assert(sizeof(Value) == sizeof(BinaryValue), "ParameterID too large");

    Value value;
    BinaryValue binaryValue;

    ParameterID() : binaryValue(0) {}
    ParameterID(Plugins::ParameterID);
    ParameterID(Plugins::ParameterIndex);

    Type type() const { return value.type; }

#ifdef __APPLE__
#undef verify
#endif // __APPLE__
    void verify() const;
}; // union ParameterID
static_assert(sizeof(ParameterID) == sizeof(ParameterID::BinaryValue), "ParameterID too large");

ParameterID::BinaryValue parameterIDFromIndex(Plugins::ParameterIndex);
Plugins::ParameterIndex parameterIndexFromBinaryID(ParameterID::BinaryValue);

template <class Functor, class PluginClass>
typename Functor::result_type
invokeFunctorOnIdentifiedParameter(ParameterID const parameterID, Functor &&functor,
                                   PluginClass *LE_RESTRICT const pEffect)
{
    switch (parameterID.type())
    {
    case ParameterID::GlobalParameter:
        return functor(parameterID.value._.global, pEffect);
    case ParameterID::ModuleChainParameter:
        return functor(parameterID.value._.moduleChain, pEffect);
    case ParameterID::ModuleParameter:
        return functor(parameterID.value._.module, pEffect);
    case ParameterID::LFOParameter:
        return functor(parameterID.value._.lfo, pEffect);

        LE_DEFAULT_CASE_UNREACHABLE();
    }
}

} // namespace SW

} // namespace LE

#endif // parameterID_hpp
