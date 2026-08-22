////////////////////////////////////////////////////////////////////////////////
///
/// channelBuffers.cpp
/// ------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "channelBuffers.hpp"

#include "le/spectrumworx/engine/buffers.hpp"
#include "le/math/dft/fft.hpp"
#include "le/math/vector.hpp"

#include "le/utility/assert.hpp"

#include <algorithm>

namespace LE::SW::Engine
{

////////////////////////////////////////////////////////////////////////////////
//
// ChannelBuffers::readyOutputDataSize()
// -------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   As the name implies, readyOutputDataSize() does not take into account the
// samples that are not yet ready/'fully complete' (have not gone through all
// the OLA steps) but are nonetheless real data. This is because ChannelBuffers
// objects do not keep track of the current frame and step sizes that are
// required for calculation of the total size of
// valid/relevant samples in the output OLA buffer.
//                                            (27.07.2010.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

std::uint16_t ChannelBuffers::readyOutputDataSize() const /// \throws nothing
{
    return outputOLAPosition_;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Priming the *input* FIFO with `windowSize - stepSize` is what stops the
/// first frame waiting for a whole window of real input. It cannot reduce the
/// latency below that, which is how far behind the newest frame the overlap-add
/// is still summing. \see doc/tech/latency.md for the derivation.
///
/// \note The second priming: `outputOLA_` is cleared, so declaring `stepSize` of
/// it ready hands out one hop of silence before any real output -- and that hop
/// is what a block whose length is not a whole number of hops draws its remainder
/// from. Without it the engine has nothing to answer such a block with and emits
/// silence into a running stream, shifting everything after it.
/// \see Processor::processSingleChannel().
///
////////////////////////////////////////////////////////////////////////////////

void ChannelBuffers::reset(std::uint16_t const initialInputSilence,
                           std::uint16_t const initialReadyOutput)
{
    inputOLAPosition_ = initialInputSilence;
    outputOLAPosition_ = initialReadyOutput;
    mainOLA_.clear();
    sideOLA_.clear();
    outputOLA_.clear();
}

namespace
{
void addNewDataWorker(float const *LE_RESTRICT &pInputData, DataRange const &outputBuffer,
                      std::uint16_t const outputBufferPosition, std::uint16_t const sizeToCopy)
{
    LE_ASSERT_MSG(unsigned(outputBuffer.size()) >= unsigned(outputBufferPosition + sizeToCopy),
                  "Buffer overflow.");
    Math::copy(pInputData, outputBuffer.begin() + outputBufferPosition, sizeToCopy);
    pInputData += sizeToCopy;
}
} // anonymous namespace

void ChannelBuffers::addNewData(float const *LE_RESTRICT &pNewMainChannelData,
                                float const *LE_RESTRICT &pNewSideChannelData,
                                std::uint16_t const sizeToCopy, bool const useSideChannel)
{
    LE_ASSERT_MSG(unsigned(inputOLAPosition_ + sizeToCopy) <= mainOLA_.size(),
                  "Buffer size mismatch.");

    addNewDataWorker(pNewMainChannelData, mainOLA_, inputOLAPosition_, sizeToCopy);
    if (useSideChannel)
        addNewDataWorker(pNewSideChannelData, sideOLA_, inputOLAPosition_, sizeToCopy);

    inputOLAPosition_ += sizeToCopy;
}

void ChannelBuffers::setCurrentDataToChannelData(bool const useSideChannel,
                                                 Math::FFT_float_real_1D const &fft,
                                                 ReadOnlyDataRange const &window)
{
    LE_ASSERT_MSG(inputDataSize() == unsigned(window.size()), "Buffer size mismatch.");
    channelData_.setNewTimeDomainData(mainOLA_.begin(), useSideChannel ? sideOLA_.begin() : 0, fft,
                                      window);
}

////////////////////////////////////////////////////////////////////////////////
//
// ChannelBuffers::moveForwardByHopSize()
// --------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// Move forward by one hop size, in other words discard a step-sized chunk of
/// oldest input data (i.e. from the beginning of the FIFO buffer) and move the
/// output buffer target position by the same amount.
///
/// \throws nothing
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
void shiftBufferToLeft(DataRange const &buffer, std::uint16_t const dataSize,
                       std::uint16_t const hopSize)
{
    LE_ASSERT_MSG(dataSize >= hopSize, "Move size too large.");
    LE_ASSERT_MSG(dataSize <= static_cast<unsigned int>(buffer.size()), "Buffer size mismatch.");
    Math::move(&buffer[hopSize - 1] + 1, &buffer[0], dataSize - hopSize);
}
} // anonymous namespace

void ChannelBuffers::moveForwardByHopSize(std::uint16_t const hopSize, bool const useSideChannel)
{
    LE_ASSERT_MSG(inputOLAPosition_ >= hopSize, "Move amount - buffer position mismatch");

    shiftBufferToLeft(mainOLA_, inputDataSize(), hopSize);
    if (useSideChannel)
        shiftBufferToLeft(sideOLA_, inputDataSize(), hopSize);

    inputOLAPosition_ -= hopSize;
    outputOLAPosition_ += hopSize;
    LE_ASSERT_MSG(outputOLAPosition_ <= outputOLA_.size(), "Buffer overflow");
}

float *ChannelBuffers::putNewTimeDomainDataToOutput(Math::FFT_float_real_1D const &fft,
                                                    ReadOnlyDataRange const &window)
{
    LE_ASSERT_MSG((readyOutputDataSize() + window.size()) <= outputOLA_.size(), "Buffer overflow.");
    LE_ASSERT_MSG(window.size() == unsigned(fft.size()), "Window-FFT sizes mismatched.");

    float const *const pNewData(channelData_.getNewTimeDomainData(fft, true /*FFT shift*/));
    float *const pOutput(&outputOLA_[readyOutputDataSize()]);

    // Implementation note:
    //   Windowing and adding in one step.
    //                                        (11.02.2010.) (Domagoj Saric)
    unsigned int const frameSize(fft.size());
    LE_ASSERT_MSG(&pOutput[frameSize] <= outputOLA_.end(), "Output OLA buffer overflow!");
    Math::addProduct(pNewData, &window[0], pOutput, frameSize);

    return pOutput;
}

////////////////////////////////////////////////////////////////////////////////
//
// ChannelBuffers::extractChunkOfReadyOutputData()
// -----------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Moves a chunk of ready output data to the target location. Performs
/// necessary related bookkeeping.
///
/// \throws nothing
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   Because ChannelBuffers objects do not keep track of the current frame and
// step sizes that are required for calculation of the total size of
// valid/relevant samples in the output OLA buffer this information has to be
// supplied through the incompleteOutputOLASamples parameter.
//                                            (27.07.2010.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

void ChannelBuffers::extractChunkOfReadyOutputData(float *LE_RESTRICT const pTargetBuffer,
                                                   std::uint16_t const chunkSize,
                                                   std::uint16_t const incompleteOutputOLASamples)
{
    LE_ASSERT_MSG(chunkSize <= readyOutputDataSize(), "Insufficient data.");

    // copy the results to the output location.
    Math::copy(outputOLA_.begin(), pTargetBuffer, chunkSize);

    // Remove the above saved output chunk from the output FIFO buffer by
    // shifting its contents to the left.

    // Calculate the number of valid samples left when we take out the requested
    // chunk.
    std::uint16_t const validOutputSamples(readyOutputDataSize() + incompleteOutputOLASamples -
                                           chunkSize);
    LE_ASSERT_MSG(unsigned(chunkSize + validOutputSamples) <= outputOLA_.size(), "Buffer overrun.");
    Math::move(outputOLA_.begin() + chunkSize, outputOLA_.begin(), validOutputSamples);

    LE_ASSERT(outputOLAPosition_ >= chunkSize);
    outputOLAPosition_ -= chunkSize;

    //  Zero the leftover samples on the right side, otherwise they would be
    // used again in the overlap-add step leading to saturation.
    // Implementation note:
    //   To avoid zeroing the entire right portion of the buffer we zero only
    // the minimum required size. If we assume that between buffer resets data
    // is read and written to in same-sized chunks it would seem that erasing a
    // portion of the same size that was just taken out should be enough
    // (erasing only a hop-sized chunk is not enough as this function is not
    // called after every OLA step but when there is enough data to satisfy the
    // host's request, i.e. readyOutputDataSize() >= chunkSize).
    //                                        (11.02.2010.) (Domagoj Saric)
    Math::clear(outputOLA_.begin() + validOutputSamples, chunkSize);
}

////////////////////////////////////////////////////////////////////////////////
//
// ChannelBuffers::inputBuffer()
// -----------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Allows access to the internal buffer for storing input data after it
/// was consumed. Useful for avoiding redundant buffer allocations and/or data
/// copying.
///
/// \throws nothing
///
////////////////////////////////////////////////////////////////////////////////

float *ChannelBuffers::inputBuffer()
{
    LE_ASSERT_MSG(channelData_.sourceTimeDomainDataWasConsumed(), "Incorrect buffer state.");
    return mainOLA_.begin();
}

std::uint32_t ChannelBuffers::requiredStorage(StorageFactors const &factors)
{
    using Utility::align;
    return ChannelData::requiredStorage(factors) + align(MainOLA ::requiredStorage(factors)) +
           align(SideOLA ::requiredStorage(
               factors)) + //...mrmlj...we allocated memory for the side channel even if there is no side channel...
           align(OutputOLA ::requiredStorage(factors));
}

void ChannelBuffers::resize(StorageFactors const &factors, Storage &storage)
{
    channelData_.resize(factors, storage);
    mainOLA_.resize(factors, storage);
    sideOLA_.resize(factors, storage);
    outputOLA_.resize(factors, storage);
}

std::uint32_t ChannelBuffers::OutputOLA::requiredStorage(StorageFactors const &factors)
{
    // Implementation note:
    //   In addition to the full window size we need windowSize - overlapSize
    // samples for the output OLA buffer in order to accommodate the final OLA
    // step that produces windowSize complete samples (in this last step we have
    // windowSize - overlapSize complete samples and need to add one more window
    // to make the last overlapSize samples complete):
    //
    // windowSize + ( windowSize - overlapSize ) =
    //
    // 2 * windowSize - fftSize / overlapFactor =
    //
    // 2 * windowSize * overlapFactor - fftSize
    // ---------------------------------------- =
    //               overlapFactor
    //
    // 2 * fftSize * overlapFactor - fftSize
    // ------------------------------------- =
    //             overlapFactor
    //
    //            2 * overlapFactor - 1
    // fftSize * -----------------------
    //                overlapFactor
    //
    //                                        (05.10.2011.) (Domagoj Saric)

    std::uint8_t const overlapFactor(factors.overlapFactor);
    std::uint8_t const a(2 * overlapFactor - 1);
    std::uint8_t const b(overlapFactor);
    std::uint8_t const c(0);

    auto const storageBytes(
        Engine::Detail::fftBufferSize(a, b, c, sizeof(value_type), factors.fftSize));
    return storageBytes;
}

} // namespace LE::SW::Engine
