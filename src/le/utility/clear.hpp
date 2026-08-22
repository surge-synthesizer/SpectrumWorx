////////////////////////////////////////////////////////////////////////////////
///
/// \file clear.hpp
/// ---------------
///
/// Copyright (c) 2013 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef clear_hpp__834DF3B1_52B1_4110_899E_A926DA81EF95
#define clear_hpp__834DF3B1_52B1_4110_899E_A926DA81EF95
//------------------------------------------------------------------------------
#include "platformSpecifics.hpp"

#include <cstring>
#include <type_traits>

namespace LE::Utility
{

namespace Detail
{
template <unsigned int size> void clear(void *LE_RESTRICT const pPODObject)
{
    std::memset(pPODObject, 0, size);
}
} // namespace Detail

template <typename POD> void clear(POD &pod)
{
    /// \note These two are what memsetting to zero actually needs -- that the
    /// bytes may be copied around, and that all-zero is a value the type could
    /// have had anyway. `is_pod` would also demand standard layout, which memset
    /// does not care about.
    static_assert(std::is_trivially_copyable_v<POD> &&
                      std::is_trivially_default_constructible_v<POD>,
                  "Will not memset a non trivial type.");
    Detail::clear<sizeof(pod)>(&pod);
}

} // namespace LE::Utility

#endif // clear_hpp
