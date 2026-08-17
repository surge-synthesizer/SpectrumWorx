////////////////////////////////////////////////////////////////////////////////
///
/// \file channelBuffers.hpp
/// ------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef channelBuffers_hpp__604836C8_E697_4307_848D_CF2026BF6E99
#define channelBuffers_hpp__604836C8_E697_4307_848D_CF2026BF6E99
//------------------------------------------------------------------------------
#include "channelData.hpp"

#include "le/utility/buffers.hpp"

#include <cstdint>

namespace LE::SW::Engine
{

class ChannelBuffers
{
  public:
    std::uint16_t inputDataSize() const { return inputOLAPosition_; }
    std::uint16_t readyOutputDataSize() const;

    void addNewData(float const *LE_RESTRICT &pNewMainChannelData,
                    float const *LE_RESTRICT &pNewSideChannelData, std::uint16_t sizeToCopy,
                    bool useSideChannel);

    void setCurrentDataToChannelData(bool useSideChannel, Math::FFT_float_real_1D const &fft,
                                     ReadOnlyDataRange const &window);

    float *putNewTimeDomainDataToOutput(Math::FFT_float_real_1D const &fft,
                                        ReadOnlyDataRange const &window);

    void moveForwardByHopSize(std::uint16_t hopSize, bool useSideChannel);

    void extractChunkOfReadyOutputData(float *pTargetBuffer, std::uint16_t chunkSize,
                                       std::uint16_t incompleteOutputOLASamples);

    float *inputBuffer();

    std::uint16_t outputBufferSize() const { return static_cast<std::uint16_t>(outputOLA_.size()); }

    ChannelData &channelData() { return channelData_; }
    ChannelData const &channelData() const { return channelData_; }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Puts both FIFOs back to the state a stream starts in.
    ///
    /// \param initialInputSilence how much silence the input FIFO pretends to
    ///        have already received, so that the first real hop completes a
    ///        window rather than waiting for a whole one. `windowSize - stepSize`.
    ///
    /// \param initialReadyOutput how much silence the output FIFO starts out
    ///        holding, so that a caller asking for a partial hop can always be
    ///        answered. `stepSize`, and the second argument is what makes the
    ///        engine's delay independent of the block size it is called with.
    ///        \see doc/tech/latency.md.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void reset(std::uint16_t initialInputSilence, std::uint16_t initialReadyOutput);
    void resize(StorageFactors const &, Storage &);
    static std::uint32_t requiredStorage(StorageFactors const &);

  private:
    std::uint16_t inputOLAPosition_;
    std::uint16_t outputOLAPosition_;

    ChannelData channelData_;

    using MainOLA = Engine::WindowBuffer<real_t>;
    MainOLA mainOLA_;
    using SideOLA = Engine::WindowBuffer<real_t>;
    SideOLA sideOLA_;

    struct OutputOLA : Utility::SharedStorageBuffer<real_t>
    {
        static std::uint32_t requiredStorage(StorageFactors const &);

        void resize(StorageFactors const &factors, Storage &storage)
        {
            Utility::SharedStorageBuffer<real_t>::resize(requiredStorage(factors), storage);
        }
    }; // struct OutputOLA
    OutputOLA outputOLA_;
}; // class ChannelBuffers

} // namespace LE::SW::Engine

#endif // channelBuffers_hpp
