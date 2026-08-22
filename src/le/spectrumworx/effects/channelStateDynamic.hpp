////////////////////////////////////////////////////////////////////////////////
///
/// \file channelStateDynamic.hpp
/// -----------------------------
///
/// Copyright (c) 2012 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef channelStateDynamic_hpp__44528C6F_047C_4814_BBB1_01EDE8DCAABF
#define channelStateDynamic_hpp__44528C6F_047C_4814_BBB1_01EDE8DCAABF
//------------------------------------------------------------------------------
#include "le/utility/platformSpecifics.hpp"

#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>
#include "le/utility/span.hpp"

namespace LE
{

namespace Utility
{
std::uint32_t align(std::uint32_t storageBytes);
}

namespace SW
{

namespace Engine
{
struct StorageFactors;
using Storage = LE::Utility::Span<char>;
} //namespace Engine

namespace Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class DynamicChannelState
///
/// \brief A channel state whose members size themselves out of the engine's
/// storage: derive from it, declare the members, and say what they are.
///
///     struct DynamicChannelState : DynamicChannelState_<DynamicChannelState>
///     {
///         PhaseVocoderShared::PitchShifter::ChannelState pvState;
///         History                                        historyBuffer;
///         auto members() { return std::tie( pvState, historyBuffer ); }
///     };
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   Was LE_DYNAMIC_CHANNEL_STATE( ((Type)(name))((Type)(name)) ), a Boost.PP
// sequence walked four times: once to declare the members and once each for the
// three things every one of these does to all of them. Only the first of the
// four needs a preprocessor -- a member has a name -- and members are just
// declarations, so writing them is not worse than writing them inside a macro
// argument. The other three read the members off std::tie, which is the same
// list said once more and the only thing the class still has to say.
////////////////////////////////////////////////////////////////////////////////

namespace Detail ///< \internal
{
template <class Members, std::size_t... indices>
std::uint32_t requiredStorageOf(Engine::StorageFactors const &factors,
                                std::index_sequence<indices...>)
{
    return (
        0u + ... +
        Utility::align(
            std::remove_cvref_t<std::tuple_element_t<indices, Members>>::requiredStorage(factors)));
}
} // namespace Detail

template <class Derived> struct DynamicChannelState_
{
    static std::uint32_t requiredStorage(Engine::StorageFactors const &factors)
    {
        using Members = decltype(std::declval<Derived &>().members());
        return Detail::requiredStorageOf<Members>(
            factors, std::make_index_sequence<std::tuple_size_v<Members>>());
    }

    void reset()
    {
        std::apply([](auto &...member) { (member.reset(), ...); }, self().members());
    }

    void resize(Engine::StorageFactors const &factors, Engine::Storage &storage)
    {
        std::apply([&](auto &...member) { (member.resize(factors, storage), ...); },
                   self().members());
    }

  private:
    Derived &self() { return static_cast<Derived &>(*this); }
}; // struct DynamicChannelState_

////////////////////////////////////////////////////////////////////////////////
/// \internal
/// \class CompoundChannelState
////////////////////////////////////////////////////////////////////////////////

template <typename... ChannelStates> struct CompoundChannelState : ChannelStates...
{
  private:
    // http://stackoverflow.com/questions/25680461/variadic-template-pack-expansion
    using expander = char[]; //...mrmlj...bad msvc12 codegen if int is used instead of char

  public:
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
#endif // __clang__
    void resize(Engine::StorageFactors const &factors, Engine::Storage &storage)
    {
        expander{0, (static_cast<ChannelStates &>(*this).resize(factors, storage), '\0')...};
    }
    void reset() { expander{0, (static_cast<ChannelStates &>(*this).reset(), '\0')...}; }

    static std::uint32_t requiredStorage(Engine::StorageFactors const &factors)
    {
        std::uint32_t sum(0);
        expander{0, (sum += Utility::align(ChannelStates::requiredStorage(factors)), '\0')...};
        return sum;
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif // __clang__
}; // struct CompoundChannelState

} // namespace Effects

} // namespace SW

} // namespace LE

#endif // channelStateDynamic_hpp
