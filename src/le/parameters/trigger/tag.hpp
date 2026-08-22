////////////////////////////////////////////////////////////////////////////////
///
/// \file trigger/tag.hpp
/// ---------------------
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef tag_hpp__52B1FD32_BCD7_416A_8B25_1A640BF6E636
#define tag_hpp__52B1FD32_BCD7_416A_8B25_1A640BF6E636
//------------------------------------------------------------------------------
#include "le/parameters/boolean/tag.hpp"

#include <type_traits>

namespace LE::Parameters
{

struct TriggerParameterTag : BooleanParameterTag
{
};

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Whether \p Parameter is an *event* rather than a value.
///
///   A trigger is the one parameter kind with nothing to remember: it is set
/// true, the engine consumes it -- `TriggerParameter::consumeValue()` reads it
/// and disarms it -- and it is false again. `setValue` can only ever set it
/// true, so nobody outside the engine can put it back.
///
///   Which makes "what is its value" the wrong question everywhere except the
/// moment between the two, and that moment belongs to one thread. Streaming and
/// the host-facing readback both ask it, and both are answered `false` here.
/// \see ParametersSaver::valueToStream() in le/spectrumworx/presets.hpp.
///
////////////////////////////////////////////////////////////////////////////////

template <class Parameter>
inline constexpr bool isAnEvent{std::is_same_v<typename Parameter::Tag, TriggerParameterTag>};

} // namespace LE::Parameters

#endif // tag_hpp
