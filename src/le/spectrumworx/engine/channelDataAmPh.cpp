////////////////////////////////////////////////////////////////////////////////
///
/// channelDataAmPh.cpp
/// -------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "channelDataAmPh.hpp"

#include "le/spectrumworx/effects/indexRange.hpp" //...mrmlj...

#include "le/math/vector.hpp"

#include <cstdint>
#ifndef NDEBUG
#include <limits>
#endif // NDEBUG

namespace LE::SW
{

namespace Engine
{
////////////////////////////////////////////////////////////////////////////////
//...mrmlj...these should go into engine/buffers.cpp...
namespace Detail
{
DataRange resize(DataRange const &range, IndexRange const &workingRange)
{
#ifndef NDEBUG
    if (!workingRange)
        return DataRange();
#endif // NDEBUG
    return DataRange(&range[workingRange.begin()], &range[workingRange.end() - 1] + 1);
}

/// \note This returned `std::uint16_t`, and the byte count does not fit: a buffer
/// of 2N floats at the maximum FFT size is 2 * 8192 * 4 = 65536 bytes, which
/// truncates to **zero**. That is not hypothetical — `FFT_float_real_1D`'s work
/// buffer on the Accelerate path is exactly such a `DoubleFFTBuffer`, so
/// `requiredStorage()` answered 0 for it at fftSize 8192, and so did
/// `WindowBuffer` with a presum factor of 2. The debug assert below caught it;
/// a release build silently sized the buffer to nothing.
///
///   The element counts still fit in 16 bits and the callers all sum into 32,
/// so widening the byte count is the whole fix.
std::uint32_t fftBufferSize(std::uint8_t const a, std::uint8_t const b, std::uint8_t const c,
                            std::uint8_t const sizeOfT, std::uint16_t const fftSize)
{
    using Utility::Constants::vectorAlignment;
    LE_ASSERT_MSG(fftSize * a / b < std::numeric_limits<std::uint16_t>::max(),
                  "Short integer overflow");
    std::uint32_t const elements(std::uint16_t(std::uint32_t(fftSize * a) / b) + c);
    auto const storageBytes(elements * sizeOfT);
    return storageBytes;
}
} // namespace Detail
////////////////////////////////////////////////////////////////////////////////
} // namespace Engine
namespace Engine
{

ChannelData_AmPh::ChannelData_AmPh(FullChannelData_AmPh &data, IndexRange const &workingRange)
    : SubRange<FullChannelData_AmPh, DataRange>(data, workingRange)
{
}

namespace
{ //...mrmlj...using internal knowledge of ChannelData_AmPh storage requirements
  //...mrmlj...(that it depends only on the FFT size) only to avoid including
  //...mrmlj...engine/setup.hpp...
Engine::StorageFactors storageFactors(std::uint16_t const fftSize) { return {fftSize, 0, 0, 0}; }
} // anonymous namespace

ChannelData_AmPhStorage::ChannelData_AmPhStorage(std::uint16_t const fftSize,
                                                 std::uint16_t const beginBin,
                                                 std::uint16_t const endBin, Storage storage)
    : ChannelData_AmPh(constructFull(storageFactors(fftSize), storage),
                       IndexRange(beginBin, endBin))
{
    //...mrmlj...(failures in pitch shifter if data outside user range is not zeroed)...
    Math::clear(full().amps().begin(), this->amps().begin());
    Math::clear(this->amps().end(), full().amps().end());
    Math::clear(full().phases().begin(), this->phases().begin());
    Math::clear(this->phases().end(), full().phases().end());
}

std::uint32_t ChannelData_AmPhStorage::requiredStorage(std::uint16_t const fftSize)
{
    return FullChannelData_AmPh::requiredStorage(storageFactors(fftSize));
}

DataRange subRange(DataRange const &range, std::uint16_t const beginIndex,
                   std::uint16_t const endIndex)
{
    LE_ASSERT_MSG(beginIndex <= endIndex, "Backward range.");
    LE_ASSERT_MSG(endIndex <= unsigned(range.size()), "Index out of range.");
    return DataRange(&range.begin()[beginIndex], &range.begin()[endIndex]);
}

} // namespace Engine

} // namespace LE::SW
