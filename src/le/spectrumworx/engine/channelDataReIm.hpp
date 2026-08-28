////////////////////////////////////////////////////////////////////////////////
///
/// \file channelDataReIm.hpp
/// -------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef channelDataReIm_hpp__E139EF92_51AE_4817_A166_E1B3399EDAE1
#define channelDataReIm_hpp__E139EF92_51AE_4817_A166_E1B3399EDAE1
//------------------------------------------------------------------------------
#include "buffers.hpp"

namespace LE::SW::Engine
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class FullChannelData_ReIm
///
/// \brief Holds real and imaginary components of the Fourier spectrum for
///  frequency domain effects.
///
////////////////////////////////////////////////////////////////////////////////

class FullChannelData_ReIm : public SharedStorageHalfFFTBufferPair
{
  public:
    DataRange const &reals() { return first(); }
    DataRange const &imags() { return second(); }

    ReadOnlyDataRange reals() const { return first(); }
    ReadOnlyDataRange imags() const { return second(); }
};

#pragma warning(push)
#pragma warning(disable : 4512) // Assignment operator could not be generated.

class ChannelData_ReIm : public SubRange<FullChannelData_ReIm, DataRange>
{
  public:
    ChannelData_ReIm(FullChannelData_ReIm &data, IndexRange const &workingRange);

    DataRange const &reals() { return first(); }
    DataRange const &imags() { return second(); }

    ReadOnlyDataRange reals() const { return first(); }
    ReadOnlyDataRange imags() const { return second(); }
};

#pragma warning(pop)

using FullMainSideChannelData_ReIm = MainSide<FullChannelData_ReIm>;
using MainSideChannelData_ReIm = MainSide<SubRange<FullMainSideChannelData_ReIm, ChannelData_ReIm>>;

} // namespace LE::SW::Engine

#endif // channelDataReIm_hpp
