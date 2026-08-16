////////////////////////////////////////////////////////////////////////////////
///
/// processor.cpp
/// -------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "processor.hpp"

#include "module.hpp"
#include "moduleChainImpl.hpp"

#include "le/math/conversion.hpp"
#include "le/math/math.hpp"
#include "le/math/constants.hpp"
#include "le/math/vector.hpp"
#include "le/math/windows.hpp"
#include "le/spectrumworx/effects/effects.hpp"
#include "le/utility/parentFromMember.hpp"
#include "le/utility/platformSpecifics.hpp"

#include "le/utility/stackBuffer.hpp"

#include "le/utility/assert.hpp"

#include <algorithm>
#include <cfloat>
#include "le/utility/span.hpp"

namespace LE::SW::Engine
{

void Processor::preProcess() { modules().preProcessAll(lfoTimer(), engineSetup()); }

/// \see the note on the declaration.
void Processor::preProcessForFirstFrame()
{
    if (preProcessedThisCall_)
        return;
    preProcessedThisCall_ = true;
    preProcess();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \class Processor::ProcessParameters
///
////////////////////////////////////////////////////////////////////////////////

class Processor::ProcessParameters
{
  public:
    ProcessParameters(InputData inputs, InputData sideChannel, OutputData outputs,
                      Channels const &channels, std::uint32_t numberOfSamples, float outputGain,
                      float mixAmount);
    ProcessParameters(ProcessParameters const &) = delete;

    std::uint8_t currentChannel() const { return currentChannel_; }
    std::uint32_t numberOfSamples() const { return numberOfSamples_; }

    float const &mixPercentage() const { return mixPercentage_; }
    float const &outputScaling() const
    {
        return outputScaling_;
    } ///< Combined postAmp and mixPercentage

    bool doMix() const { return doMix_; }

    float const *mainChannel() const
    {
        LE_ASSUME(*ppMainChannels_);
        return *ppMainChannels_;
    }
    float const *sideChannel() const { return *ppSideChannels_; }
    float *output() const
    {
        LE_ASSERT(pOutput_);
        return *pOutput_;
    }
    ChannelBuffers &channelBuffers() const { return channelBuffers_.front(); }

    bool haveSideChannel() const { return sideChannel() != nullptr; }

    bool setNextChannel(std::uint8_t const numberOfChannels)
    {
        ++currentChannel_;
        ++ppMainChannels_;
        ++pOutput_;
        channelBuffers_.advance_begin(1);
        if (haveSideChannel())
            ++ppSideChannels_;

        if (currentChannel() < numberOfChannels)
            return true;
        else
            return false;
    }

  private:
    InputData ppMainChannels_;
    InputData ppSideChannels_;
    OutputData pOutput_;

    LE::Utility::Span<ChannelBuffers> channelBuffers_;

    std::uint8_t currentChannel_;

    std::uint32_t const numberOfSamples_;

    float const mixPercentage_;
    float const outputScaling_;

    bool const doMix_;
}; // class Processor::ProcessParameters

////////////////////////////////////////////////////////////////////////////////
//
// Processor::process()
// --------------------
//
////////////////////////////////////////////////////////////////////////////////

void Processor::process /// \throws nothing
    (InputData const mainInputs, InputData const sideInputs, OutputData const outputs,
     std::uint32_t const samples, float const outputGain, float const mixAmount)
{
    LE_ASSERT_MSG(engineSetup().fftSize<std::uint16_t>() &&
                      engineSetup().windowOverlappingFactor<std::uint8_t>(),
                  "WOLA parameters not setup.");

    /// \note A Math::FPUDisableDenormalsGuard stood here under
    /// #ifdef LE_SW_SDK_BUILD, which nothing defined, so it never ran. The guard
    /// is one sst::plugininfra FPUStateGuard at the top of
    /// SpectrumWorxCLAP::process() now -- the outermost point of the audio
    /// callback, which is the right scope for it and the only one all four
    /// formats share.
    ///                                   (29.07.2026.) (SW port)
    ///
    /// \note Armed rather than called: the sampling happens at the first frame
    /// this call produces, and not at all if it produces none.
    /// \see preProcessForFirstFrame().
    preProcessedThisCall_ = false;

    ProcessParameters processParameters(mainInputs, sideInputs, outputs, channels_, samples,
                                        outputGain, mixAmount);

    do
    {
        processSingleChannel(processParameters);
    } while (processParameters.setNextChannel(engineSetup().numberOfChannels()));
}

namespace
{
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wassume"
#endif // __clang__
float **makeDeinterLeaveBuffers(LE::Utility::Span<float> const deinterLeavedDataStorage,
                                LE::Utility::Span<float *> const deinterLeavedDataPointers,
                                std::uint16_t const size, std::uint8_t const numberOfChannels)
{
    LE_ASSUME(deinterLeavedDataStorage.begin());
    LE_ASSUME(deinterLeavedDataPointers.begin());
    for (std::uint8_t channel(0); channel < numberOfChannels; ++channel)
    {
#ifndef NDEBUG
        if (size == 0)
            deinterLeavedDataPointers[channel] = deinterLeavedDataStorage.begin();
        else
#endif // NDEBUG
            deinterLeavedDataPointers[channel] = &deinterLeavedDataStorage[channel * size];
        LE_ASSUME(deinterLeavedDataPointers[channel]);
    }
    auto const resultPointer(deinterLeavedDataPointers.begin());
    LE_ASSUME(resultPointer);
    return resultPointer;
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif // __clang__

/// \note Clang's alloca returns unaligned pointers and the Boost.SIMD
/// macros used to crash with Apple's Clang (Xcode 5, 6) so always look
/// here if strange things happen with Clang builds and non-mono interleaved
/// input (because of Clang's unaligned alloca we have to use the aligned
/// version of the macro even for the pointer array).
/// https://bugs.chromium.org/p/nativeclient/issues/detail?id=3795
/// https://llvm.org/bugs/show_bug.cgi?id=22728
///                                       (24.05.2016.) (Domagoj Saric)
#define LE_MAKE_DEINTERLEAVE_BUFFERS(resultPointer, size, numberOfChannels)                        \
    LE_ALIGNED_STACK_BUFFER(resultPointer##DeinterLeavedDataStorage, float,                        \
                            size *numberOfChannels);                                               \
    LE_ALIGNED_STACK_BUFFER(resultPointer##DeinterLeavedDataPointers, float *, numberOfChannels);  \
    resultPointer =                                                                                \
        makeDeinterLeaveBuffers(resultPointer##DeinterLeavedDataStorage,                           \
                                resultPointer##DeinterLeavedDataPointers, size, numberOfChannels)
} // anonymous namespace

void Processor::process /// \throws nothing
    (InterleavedInputData interleavedMainInputs, InterleavedInputData interleavedSideInputs,
     InterleavedOutputData interleavedOutputs, std::uint32_t samples, float const outputGain,
     float const mixAmount)
{
    auto const numberOfChannels(engineSetup().numberOfChannels());

    /// \note `ImplausibleAudio` rather than `InvalidOrSlow`: this is the outermost
    /// point audio arrives from outside the engine, and it is the one place a
    /// magnitude bound names what is wrong rather than describing what it turned
    /// into three layers down. \see Math::hundredDecibels.
    LE_MATH_VERIFY_VALUES(Math::ImplausibleAudio,
                          LE::Utility::makeSpan(interleavedMainInputs, samples * numberOfChannels),
                          "main input");
    if (interleavedSideInputs)
        LE_MATH_VERIFY_VALUES(
            Math::ImplausibleAudio,
            LE::Utility::makeSpan(interleavedSideInputs, samples * numberOfChannels), "side input");

    LE_ASSERT_MSG(engineSetup().fftSize<std::uint16_t>() &&
                      engineSetup().windowOverlappingFactor<std::uint8_t>(),
                  "WOLA parameters not setup.");

    /// \note A Math::FPUDisableDenormalsGuard stood here under
    /// #ifdef LE_SW_SDK_BUILD, which nothing defined, so it never ran. The guard
    /// is one sst::plugininfra FPUStateGuard at the top of
    /// SpectrumWorxCLAP::process() now -- the outermost point of the audio
    /// callback, which is the right scope for it and the only one all four
    /// formats share.
    ///                                   (29.07.2026.) (SW port)
    ///
    /// \note Armed rather than called: the sampling happens at the first frame
    /// this call produces, and not at all if it produces none.
    /// \see preProcessForFirstFrame().
    preProcessedThisCall_ = false;

    float const *LE_RESTRICT const *LE_RESTRICT mainInputs;
    float const *LE_RESTRICT const *LE_RESTRICT sideInputs;
    float *LE_RESTRICT const *LE_RESTRICT outputs;

    std::uint32_t processBlockSize;

    if (numberOfChannels == 1)
    {
        mainInputs = &interleavedMainInputs;
        sideInputs = interleavedSideInputs ? &interleavedSideInputs : nullptr;
        outputs = &interleavedOutputs;

        processBlockSize = samples;
    }
    else
    {
        processBlockSize = std::min<std::uint32_t>(samples, engineSetup().fftSize<std::uint16_t>());
        std::uint16_t const deinterleaveBlockSize(Math::alignIndex(processBlockSize));
        LE_MAKE_DEINTERLEAVE_BUFFERS(outputs, deinterleaveBlockSize, numberOfChannels);
        mainInputs = outputs; // we support in-place processing (so we can reuse the buffer)
        if (interleavedSideInputs)
        {
            LE_MAKE_DEINTERLEAVE_BUFFERS(sideInputs, deinterleaveBlockSize, numberOfChannels);
        }
        else
        {
            sideInputs = nullptr;
        }
    }

    while (samples)
    {
        if (numberOfChannels == 1)
        {
            LE_ASSUME(processBlockSize == samples);
        }
        else
        {
            processBlockSize = std::min<std::uint32_t>(processBlockSize, samples);
            Math::deinterleave(interleavedMainInputs, const_cast<float *const *>(mainInputs),
                               processBlockSize, numberOfChannels);
            if (interleavedSideInputs)
                Math::deinterleave(interleavedSideInputs, const_cast<float *const *>(sideInputs),
                                   processBlockSize, numberOfChannels);
        }

        ProcessParameters processParameters(mainInputs, sideInputs, outputs, channels_,
                                            processBlockSize, outputGain, mixAmount);

        do
        {
            processSingleChannel(processParameters);
        } while (processParameters.setNextChannel(numberOfChannels));

        if (numberOfChannels != 1)
        {
            Math::interleave(outputs, interleavedOutputs, processBlockSize, numberOfChannels);

            interleavedMainInputs += processBlockSize * numberOfChannels;
            if (interleavedSideInputs)
                interleavedSideInputs += processBlockSize * numberOfChannels;
            interleavedOutputs += processBlockSize * numberOfChannels;

            samples -= processBlockSize;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// Processor::processSingleChannel()
// ---------------------------------
//
////////////////////////////////////////////////////////////////////////////////

void Processor::processSingleChannel(ProcessParameters const &processParameters) /// \throws nothing
{
    auto const stepSize(engineSetup().stepSize<std::uint16_t>());
    auto const windowSize(engineSetup().windowSize<std::uint16_t>());
    LE_ASSERT(windowSize == static_cast<std::uint16_t>(analysisWindow().size()));

    // Implementation note:
    //   ChannelBuffers::readyOutputDataSize() does not take into account the
    // samples that are not yet ready/'fully complete' (have not gone through
    // all the OLA steps) but are nonetheless real data. As each OLA step adds
    // windowSize new samples of which only the first hopSize become 'fully
    // complete' (i.e. constitute the final OLA step for that hop-sized chunk of
    // samples) it follows that we always have (windowSize - hopSize) of such
    // 'valid but not complete' samples following the
    // ChannelBuffers::outputOLAPosition_ (readyOutputDataSize()).
    //                                        (11.02.2010.) (Domagoj Saric)
    std::uint16_t const incompleteOutputDataSizeFromPreviousSteps(windowSize - stepSize);

    float const *LE_RESTRICT pCompleteNewInput(processParameters.mainChannel());
    float const *LE_RESTRICT pCompleteNewSideChannel(processParameters.sideChannel());
    std::uint32_t inputSamples(processParameters.numberOfSamples());
    bool const useSideChannel(processParameters.haveSideChannel());
    ChannelBuffers &channelBuffers(processParameters.channelBuffers());
    float *LE_RESTRICT pOutput(processParameters.output());

    using namespace Math;

    LE_MATH_VERIFY_VALUES(Math::ImplausibleAudio,
                          ReadOnlyDataRange(pCompleteNewInput, pCompleteNewInput + inputSamples),
                          "main input");
    LE_MATH_VERIFY_VALUES(
        Math::ImplausibleAudio,
        ReadOnlyDataRange(pCompleteNewSideChannel,
                          pCompleteNewSideChannel + (pCompleteNewSideChannel ? inputSamples : 0)),
        "side input");

    while (inputSamples)
    {
        // Fill the input FIFO buffers just right up to the window size, the
        // minimum we need for one pass of processing.
        std::uint16_t const previousData(channelBuffers.inputDataSize());
        std::uint16_t const neededData(windowSize - previousData);
        std::uint16_t const sizeToConsume(
            static_cast<std::uint16_t>(std::min<std::uint32_t>(neededData, inputSamples)));

        channelBuffers.addNewData(pCompleteNewInput, pCompleteNewSideChannel, sizeToConsume,
                                  useSideChannel);
        LE_ASSERT_MSG(channelBuffers.inputDataSize() <= windowSize, "Too much data consumed.");
        inputSamples -= sizeToConsume;

        /// \note Assertion failures/crashes occur with 0% overlap due to the
        /// output OLA buffer overruns. As a workaround, the second check is
        /// performed. This needs further investigation...
        ///                                   (04.03.2015.) (Domagoj Saric)
        if (                                                  // Process if:
            (channelBuffers.inputDataSize() == windowSize) && // - we have enough input data
            (channelBuffers.readyOutputDataSize() <=
             channelBuffers.outputBufferSize() - windowSize) // - we have space for output data
        )
        {
            /// \note Here rather than at the top of the call: this is the first
            /// point at which a frame is certain, and a parameter that is
            /// *consumed* rather than read must not be sampled anywhere else.
            /// \see preProcessForFirstFrame(). Cheap after the first frame -- a
            /// predictable branch on a flag -- and it must stay inside this
            /// branch rather than above the loop, which would sample on a block
            /// that never reaches it.
            preProcessForFirstFrame();

            // The Window+FFT phase:
            // Implementation note:
            //   As we cannot window the input data directly (because we
            // need it non windowed for later OLA steps) we have to copy it
            // to a new location before windowing. To reduce the number of
            // buffers and data copies we use the fact that the current FFT
            // implementation also requires copying of input data (because
            // it supports only in-place operation so it would overwrite
            // input data if it was not first copied to the output location
            // before performing the FFT) so the two copy operations are
            // merged into one: input data is copied into the destination
            // buffer, windowed and then the FFT is performed.
            //                                (11.02.2010.) (Domagoj Saric)
            channelBuffers.setCurrentDataToChannelData(useSideChannel, fft_, analysisWindow());

            // The processing phase:
            {
                auto const channel(processParameters.currentChannel());
                auto &data(channelBuffers.channelData());
                auto &engineSetup(this->engineSetup());
                modules().forEach<ModuleDSP>([&, channel](ModuleDSP const &module) {
                    module.process(channel, data, engineSetup);
                });
            }

            // The IFFT+Window+Overlap-Add phase:
            //  Get the time-domain results, window them and add with/to the
            // output FIFO buffer at the current position.
            float *const pOutput(
                channelBuffers.putNewTimeDomainDataToOutput(fft_, synthesisWindow()));

            // Scale the results to:
            // - apply the user selected post/output gain
            // - compensate for the WOLA gain.
            multiply(pOutput, processParameters.outputScaling() / engineSetup().wolaGain(),
                     stepSize);

            if (processParameters.doMix())
            {
                // Implementation note:
                //   To avoid redundant buffers and data copying we scale
                // the input data in-place (as it was already consumed and
                // will be discarded) and add it to output data.
                //                            (11.02.2010.) (Domagoj Saric)
                float const inputScaling(1 - processParameters.mixPercentage());
                multiply(channelBuffers.inputBuffer(), inputScaling, stepSize);
                add(channelBuffers.inputBuffer(), pOutput, stepSize);
            }

            channelBuffers.moveForwardByHopSize(stepSize, useSideChannel);
        } // if ( channelBuffers.inputDataSize() == windowSize )

        std::uint16_t const availableOutputData(channelBuffers.readyOutputDataSize());
        std::uint16_t const sizeToProduce(sizeToConsume);
        if (sizeToProduce > availableOutputData) [[unlikely]]
        {
            // We have just started processing and the latency time has not yet
            // passed so we do not have enough data and therefore simply zero
            // the part of the output that we have no data for.
            std::uint16_t const amountToZero(sizeToProduce - availableOutputData);
            Math::clear(pOutput, amountToZero);
            pOutput += amountToZero;
        }
        LE_ASSERT_MSG(availableOutputData <=
                          (sizeToConsume + engineSetup().frameSize<unsigned int>()),
                      "Produced too much data.");
        auto const amountToExtract(std::min(sizeToProduce, availableOutputData));
        channelBuffers.extractChunkOfReadyOutputData(pOutput, amountToExtract,
                                                     incompleteOutputDataSizeFromPreviousSteps);

        LE_MATH_VERIFY_VALUES(Math::InvalidOrSlow,
                              ReadOnlyDataRange(pOutput, pOutput + amountToExtract), "output");

        pOutput += amountToExtract;
    } // while ( inputSamples )
}

////////////////////////////////////////////////////////////////////////////////
//
// Processor::calculateWindowAndWOLAGain()
// ---------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
// - general information
//  http://sipl.technion.ac.il/Info/new/Staff/Academic/Malah/Publications/Shpiro_Algebraic_ICASSP84.pdf
// - polyphase DFT/Window presum DFT/Weighted-Overlap-Add
//  http://www.dsprelated.com/showmessage/123311/1.php
//  http://www.dsprelated.com/showmessage/45449/1.php
//  http://web.archive.org/web/20010210052902/http://www.chipcenter.com/dsp/DSP000315F1.html
//  http://hdl.lib.byu.edu/1877/etd157
//  http://eetimes.com/design/embedded/4007611/DSP-Tricks-Building-a-practical-spectrum-analyzer
//  http://www.littleendian.com/shared/papers/Time_Aliasing_Methods_of_Spectrum_Estimation.pdf
//  http://groups.yahoo.com/group/softrock40/message/1299
//  http://www.rfel.com/download/D02003-Polyphase%20DFT%20data%20sheet.pdf
//  http://www.rfel.com/download/w03006-comparison_of_fft_and_polydft_transient_response.pdf
//  http://www.ee.cityu.edu.hk/~hcso/canadian97_1.pdf
//  http://www.eurasip.org/Proceedings/Eusipco/Eusipco2005/defevent/papers/cr1183.pdf
//  http://dev.vinux-project.org/time-aliased-hann
////////////////////////////////////////////////////////////////////////////////

void Processor::calculateWindowAndWOLAGain()
{
    auto const windowSize(engineSetup().windowSize<std::uint16_t>());
    auto const stepSize(engineSetup().stepSize<std::uint16_t>());

    auto const analysisWindowFunction(engineSetup().windowFunction());

    Math::calculateWindow(analysisWindow_, analysisWindowFunction);

    /// \note
    ///   The WOLA (Weighted Overlap and Add) method requires that a window be
    /// applied to the signal both before and after the DFT, these are the
    /// analysis and synthesis windows respectively. The COLA (Constant Overlap
    /// and Add) condition must therefore apply to the product of the analysis
    /// and synthesis windows. The simplest solution (as 'prescribed' by J.O.S
    /// in Spectral Audio Signal Processing,
    /// http://www.dsprelated.com/dspbooks/sasp/Choice_WOLA_Window.html
    /// http://www.dsprelated.com/dspbooks/sasp/Overlap_Add_Decomposition.html)
    /// is to take the square root of a chosen window (that obeys the COLA
    /// condition) and use that for both the analysis and synthesis windowing.
    /// This approach is not good enough for our purposes because taking the
    /// square root "deforms" the window and it looses its spectral qualities
    /// which in turn hinders phase vocoder performance.
    ///   As discussed in the "WOLA and the phase vocoder" thread on the
    /// music-dsp list the solution is to either use a power complementary
    /// window (such as the Vorbis window, or the Hann window with overlap
    /// factors larger than 2) or to use different analysis and synthesis
    /// windows and to divide the synthesis window with the analysis window
    /// (e.g. use Hamming for analysis and Hann-divided-by-Hamming for
    /// synthesis). We use the latter as a general solution with special
    /// handling for windows that don't work well with the default approach.
    ///                                       (25.04.2012.) (Domagoj Saric)

    /// \todo Power complementary windows do not actually need two windows.
    /// Refactor the relevant code so that it does not allocate and initialise
    /// the (duplicated) synthesis window (rather it should simply alias the
    /// analysis window).
    ///                                       (25.04.2012.) (Domagoj Saric)
    /// \note As a quick-workaround/optimisation we make the synthesis window
    /// alias the analysis window when possible to improve locality of reference
    /// (but the extra wasted allocation is still performed).
    ///                                       (04.03.2015.) (Domagoj Saric)

    Engine::Constants::Window synthesisWindowFunction(Engine::Constants::Hann);
    /// \note Quick-hack: 'reset'/clear the synthesisWindow_ range so that its
    /// status can be used as a signal whether to skip automatic synthesis
    /// window generation (required for the flat top window which needs its own
    /// logic).
    ///                                       (05.03.2015.) (Domagoj Saric)
    synthesisWindow_.alias(FFTWindow());
    // https://ccrma.stanford.edu/~jos/parshl/Choice_Hop_Size.html
    auto const overlapFactor(engineSetup().windowOverlappingFactor<std::uint8_t>());
    switch (analysisWindowFunction)
    {
        namespace Engine = LE::SW::Engine;

    /// \note Hann is power complementary for overlap factors > 2 so reuse
    /// the analysis window for those cases, otherwise fallback to the old
    /// sqrt approach (the "automatic synthesis window generation" approach
    /// does not seem to work no matter what other 'output' window is
    /// chosen).
    ///                                   (05.03.2015.) (Domagoj Saric)
    case Engine::Constants::Hann:
        if (overlapFactor <= 2)
            //synthesisWindowFunction = Engine::Constants::Triangle;
            Math::squareRoot(analysisWindow_);
        LE_ASSERT(synthesisWindowFunction == Engine::Constants::Hann);
        break;

    // Blackman and Blackman-Harris windows seem to be power complementary
    // at high overlap factors.
    case Engine::Constants::Blackman:
    case Engine::Constants::BlackmanHarris:
        if (overlapFactor > 3)
            synthesisWindowFunction = analysisWindowFunction;
        break;

    /// \note Flat top does not seem to work with the "automatic synthesis
    /// window generation" (at overlaps below 75% it just sounds bad and
    /// at higher overlaps the sound 'breaks down' as soon as any
    /// modification is done in the frequency domain). The fallback sqrt
    /// procedure also requires special handling because flat top windows
    /// use negative values that cannot have their square root taken.
    ///                                   (05.03.2015.) (Domagoj Saric)
    case Engine::Constants::FlatTop:
    {
        synthesisWindow_.alias(synthesisWindowBackup_);
        // Take the square root of the absolute values of the window and
        // restore the signs only to the analysis window:
        float *LE_RESTRICT pAnalysisWindowSample(analysisWindow_.begin());
        float *LE_RESTRICT pSynthesisWindowSample(synthesisWindow_.begin());
        while (pAnalysisWindowSample != analysisWindow_.end())
        {
            auto const inputSample(*pAnalysisWindowSample);
            auto const sample(std::sqrt(std::abs(inputSample)));
            *pAnalysisWindowSample++ = Math::copySign(sample, inputSample);
            *pSynthesisWindowSample++ = sample;
        }
        break;
    }

    case Engine::Constants::Rectangle:
        /// \note We want 'intuitive'/expected behaviour (no amplitude
        /// modulation) for the rectangle window so we must disable the
        /// "automatic synthesis window generation" @ 0% overlap.
        ///                               (04.03.2015.) (Domagoj Saric)
        if (overlapFactor == 1)
            synthesisWindowFunction = analysisWindowFunction;
        break;

    default:
        break;
    }

    if (synthesisWindowFunction == analysisWindowFunction)
    {
        synthesisWindow_.alias(analysisWindow_);
    }
    else if (!synthesisWindow_)
    { // Default solution: synthesis window = Hann / analysis window.
        synthesisWindow_.alias(synthesisWindowBackup_);
        Math::calculateWindow(synthesisWindow_, synthesisWindowFunction);
        LE_ASSERT(synthesisWindow_.back() != 0);
        float const *LE_RESTRICT pAnalysisWindowSample(analysisWindow_.begin());
        float *LE_RESTRICT pSynthesisWindowSample(synthesisWindow_.begin());
        if (*pAnalysisWindowSample == 0)
        {
            pAnalysisWindowSample++;
            pSynthesisWindowSample++;
            //*pSynthesisWindowSample++ = 0; //...mrmlj...?
        }
        while (pSynthesisWindowSample != synthesisWindow_.end())
            *pSynthesisWindowSample++ /= *pAnalysisWindowSample++;
    }

    LE_MATH_VERIFY_VALUES(Math::InvalidOrSlow, analysisWindow_, "analysis  window");
    LE_MATH_VERIFY_VALUES(Math::InvalidOrSlow, synthesisWindow_, "synthesis window");

    // Calculate the WOLA gain and ripple/variation:

    // Fill a temporary buffer with overlap-added copies of the window(s) to
    // determine the total gain and whether the COLA condition is (sufficiently)
    // satisfied (the gain variation is sufficiently small).
    LE_ALIGNED_STACK_BUFFER(wolaBuffer, real_t, windowSize);
    Math::clear(wolaBuffer);
    /// \note LE_RESTRICT spelled out rather than carried in by
    /// DataRange::iterator: it is the declaration that makes the promise, and
    /// this walk of a freshly made stack buffer is the one place in the tree
    /// that was making it through the alias.
    ///                                       (05.08.2026.) (SW port)
    for (auto *LE_RESTRICT pBufferPosition(wolaBuffer.begin());; pBufferPosition += stepSize)
    {
        auto const bufferSpaceLeft(static_cast<std::uint16_t>(wolaBuffer.end() - pBufferPosition));
        Math::addProduct(&analysisWindow_[0], &synthesisWindow_[0], pBufferPosition,
                         std::min(windowSize, bufferSpaceLeft));
        if (bufferSpaceLeft <= stepSize)
            break;
    }
    // Implementation note:
    //   We take the mean value here instead of just 'any' value from the valid
    // range (where all elements should be equal/constant if the COLA condition
    // is ideally fulfilled) as this will give a better value for non-COLA
    // window + overlap factor combinations.
    //                                        (25.01.2010.) (Domagoj Saric)
    {
        float minimum(std::numeric_limits<float>::max());
        float maximum(0);
        float mean(0);
        float const *LE_RESTRICT pWOLAValue(&wolaBuffer[windowSize - stepSize]);
        float const *const pWOLAEnd(&wolaBuffer[windowSize - 1] + 1);
        while (pWOLAValue != pWOLAEnd)
        {
            float const value(*pWOLAValue++);
            mean += std::abs(value);
            minimum = std::min(value, minimum);
            maximum = std::max(value, maximum);
        }
        mean /= Math::convert<float>(stepSize);

        float const wolaGain(mean);
        float const variation((maximum - minimum) / maximum / wolaGain);

        engineSetup().setWOLAGainAndRipple(wolaGain, variation);
    }
}

void Processor::setNumberOfChannels(std::uint8_t const numberOfMainChannels,
                                    std::uint8_t const numberOfSideChannels)
{
    engineSetup().setNumberOfChannels(numberOfMainChannels, numberOfSideChannels);
}

bool Processor::setSampleRate(float const sampleRate, StorageFactors &currentStorageFactors)
{
    float const currentSampleRate(engineSetup().sampleRate<float>());
    //...mrmlj...assert that we are in a non-processing state...
    engineSetup().setSampleRate(sampleRate);

    StorageFactors const storageFactors{
        currentStorageFactors.fftSize, currentStorageFactors.overlapFactor,
        currentStorageFactors.numberOfChannels, engineSetup().sampleRate<std::uint32_t>()};

    if (!storageFactors.complete())
    {
        // See the related note in resize().
        return true;
    }

    LE_ASSERT_MSG(Processor::requiredStorage(storageFactors) ==
                      Processor::requiredStorage(currentStorageFactors),
                  "Processor storage assumed not to depend on the sampling rate.");

    if (modules().resizeAll(storageFactors, currentStorageFactors))
    {
        currentStorageFactors = storageFactors;
        return true;
    }
    else
    {
        engineSetup().setSampleRate(currentSampleRate);
        return false;
    }
}

bool Processor::resize(StorageFactors &currentStorageFactors,
                       StorageFactors const &newStorageFactors, Setup::Window const window,
                       Engine::HeapSharedStorage &sharedStorage)
{
    /// \note If not all storage factors have been set yet, simply save the new
    /// values and return true (in expectation of a future resize() with
    /// complete storage factors when the actual allocation will be attempted).
    /// This is required in the plugin for certain hosts that set certain
    /// storage factor related parameters before calling initialise():
    ///  - Ableton - setSampleRate()
    ///  - n-Track - setNumberOfChannels().
    ///                                       (24.01.2013.) (Domagoj Saric)
    if (!newStorageFactors.complete())
    {
        currentStorageFactors = newStorageFactors;
        engineSetup().setWindowFunction(window);
        return true;
    }
    else if (newStorageFactors == currentStorageFactors)
    {
        LE_ASSERT(Processor::requiredStorage(newStorageFactors) ==
                  Processor::requiredStorage(currentStorageFactors));
        LE_ASSERT(analysisWindow_ && synthesisWindow_ && channels_);
        if (window != engineSetup().windowFunction())
        {
            changeWindowFunction(window);
            LE_ASSERT(window == engineSetup().windowFunction());
        }
        return true;
    }

    auto const currentMainStorageSize(sharedStorage.size());
    auto const requiredStorage(Processor::requiredStorage(newStorageFactors));

    auto const currentSampleRate(engineSetup().sampleRate<float>());
    engineSetup().setSampleRate(newStorageFactors.samplerate);

    bool allocationSucceeded(sharedStorage.resize(requiredStorage));
    if (allocationSucceeded)
    {
        if (modules().resizeAll(newStorageFactors, currentStorageFactors))
        {
            currentStorageFactors = newStorageFactors;
        }
        else
        {
            allocationSucceeded = false;
            engineSetup().setSampleRate(currentSampleRate);
            LE_VERIFY(sharedStorage.resize(currentMainStorageSize));
        }

        changeWOLAParameters(currentStorageFactors, window, sharedStorage);

        //...mrmlj...rethink whether we should do this at all here (if the user
        //...mrmlj...has to reset the processor anyway...
        clearSideChannelData();
        resetChannelBuffers();
    }
    return allocationSucceeded;
}

StorageFactors Processor::makeFactors(std::uint16_t const fftSize, std::uint8_t const overlapFactor,
                                      std::uint8_t const numberOfChannels,
                                      std::uint32_t const sampleRate)
{
    StorageFactors const storageFactors = {fftSize, overlapFactor, numberOfChannels, sampleRate};
    return storageFactors;
}

void Processor::changeWOLAParameters(StorageFactors const &storageFactors,
                                     Setup::Window const window, Storage storage)
{
    LE_ASSERT(storage);
    this->resize(storageFactors, storage);
    LE_ASSERT_MSG(unsigned(storage.size()) <= storageFactors.numberOfChannels *
                                                  Utility::Constants::vectorAlignment, //...mrmlj...
                  "Requested storage space not consumed.");

    engineSetup().setFFTSize(storageFactors.fftSize);
    engineSetup().setOverlappingFactor(storageFactors.overlapFactor);
    changeWindowFunction(window);
}

void Processor::changeWindowFunction(Setup::Window const window)
{
    engineSetup().setWindowFunction(window);
    calculateWindowAndWOLAGain();
}

void Processor::clearSideChannelData()
{
    for (auto &channel : channels_)
        channel.channelData().clearSideChannelData();
}

void Processor::resetChannelBuffers()
{
    std::uint16_t const initialSilenceSamples(engineSetup().windowSize<std::uint16_t>() -
                                              engineSetup().stepSize<std::uint16_t>());
    for (auto &channel : channels_)
        channel.reset(initialSilenceSamples);
}

Processor &Processor::fromEngineSetup(Setup &engineSetup)
{
    return Utility::ParentFromMember<Processor, Setup, &Processor::engineSetup_>()(engineSetup);
}
Processor const &Processor::fromEngineSetup(Setup const &engineSetup)
{
    return fromEngineSetup(const_cast<Setup &>(engineSetup));
}

std::uint32_t Processor::requiredStorage(StorageFactors const &factors)
{
    return Math::FFT_float_real_1D::requiredStorage(factors) +
           FFTWindow ::requiredStorage(factors) + // analysis
           FFTWindow ::requiredStorage(factors) + // synthesis
           Channels ::requiredStorage(factors);
}

void Processor::resize(StorageFactors const &factors, Storage &storage)
{
    fft_.resize(factors, storage);
    analysisWindow_.resize(factors, storage);
    synthesisWindow_.resize(factors, storage);
    channels_.resize(factors, storage);

    synthesisWindowBackup_.alias(synthesisWindow_);
}

std::uint32_t Processor::Channels::requiredStorage(StorageFactors const &factors)
{
    using Utility::align;
    std::uint16_t const channelBuffersBaseSize(sizeof(value_type));
    std::uint32_t const channelBuffersStorageSize(value_type::requiredStorage(factors));
    LE_ASSERT(align(channelBuffersStorageSize) == channelBuffersStorageSize);
    std::uint32_t const totalSizePerChannel(align(channelBuffersBaseSize) +
                                            channelBuffersStorageSize);
    return factors.numberOfChannels * totalSizePerChannel;
}

void Processor::Channels::resize(StorageFactors const &factors, Storage &storage)
{
    Utility::SharedStorageBuffer<ChannelBuffers>::resize(
        factors.numberOfChannels * sizeof(value_type), storage);

    std::uint16_t const windowSize(factors.fftSize);
    std::uint16_t const stepSize(factors.fftSize / factors.overlapFactor);
    std::uint16_t const initialSilenceSamples(windowSize - stepSize);
    for (auto &channelBuffers : *this)
    {
        ChannelBuffers *LE_RESTRICT const pNewChannelBuffers(new (&channelBuffers)
                                                                 ChannelBuffers());
        LE_ASSUME(pNewChannelBuffers);
        pNewChannelBuffers->resize(factors, storage);
        pNewChannelBuffers->reset(initialSilenceSamples);
    }
}

namespace
{
float const *const dummyNullSidePointer(nullptr);
} // anonymous namespace

/// \note mixAmount was `float const volatile` on __APPLE__ only, against
/// "broken codegen by Xcode 7.1(.1) Clang" -- a 2015 compiler. A volatile
/// qualified parameter is deprecated in C++20, and the declaration up in the
/// class never carried it, so only the two reads in the member initialiser list
/// were ever affected. Dropped, and the Release goldens say the arithmetic did
/// not move.
///                                           (02.08.2026.) (SW port)
LE_FORCEINLINE Processor::ProcessParameters::ProcessParameters(
    InputData const inputs, InputData const sideChannels, OutputData const outputs,
    Channels const &channels, std::uint32_t const numberOfSamples, float const outputGain,
    float const mixAmount)
    : ppMainChannels_(inputs), ppSideChannels_(sideChannels ? sideChannels : &dummyNullSidePointer),
      pOutput_(outputs),

      channelBuffers_(channels),

      currentChannel_(0), numberOfSamples_(numberOfSamples),

      mixPercentage_(mixAmount), outputScaling_(outputGain * mixAmount), doMix_(mixPercentage_ < 1)
{
}

void Processor::setPosition(std::uint32_t const absolutePositionInSamples)
{
    lfoTimer().setPosition(absolutePositionInSamples, engineSetup().sampleRate<float>());
}

void Processor::updatePosition(std::uint32_t const deltaSamples)
{
    handleTimingInformationChange(lfoTimer().updatePositionAndTimingInformation(
        deltaSamples, engineSetup().sampleRate<float>()));
}

Processor::LFO::Timer::TimingInformationChange
Processor::updatePositionAndTimingInformation(float const positionInBars, float const barDuration,
                                              std::uint8_t const measureNumerator)
{
    auto const timingChange(lfoTimer().updatePositionAndTimingInformation(
        positionInBars, barDuration, measureNumerator));
    handleTimingInformationChange(timingChange);
    return timingChange;
}

Processor::LFO::Timer::TimingInformationChange
Processor::updatePositionAndTimingInformation(std::uint32_t const deltaNumberOfSamples)
{
    auto const timingChange(lfoTimer().updatePositionAndTimingInformation(
        deltaNumberOfSamples, engineSetup().sampleRate<float>()));
    handleTimingInformationChange(timingChange);
    return timingChange;
}

void Processor::handleTimingInformationChange(
    LFO::Timer::TimingInformationChange const timingInformationChange)
{
    if (timingInformationChange.timingInfoChanged())
        updateModuleLFOs(timingInformationChange);
}

void Processor::updateModuleLFOs(LFO::Timer::TimingInformationChange const timingInformationChange)
{
    modules().forEach<Engine::ModuleDSP>(
        [&](Engine::ModuleDSP &module) { module.updateLFOs(timingInformationChange); });
}

/// \note Processor instances do not actually hold ModuleChainImpl instances as
/// different project types have different requirements/usage patterns regarding
/// module chains (e.g. multiple chains/programs for VST2.4 plugins). For this
/// reason each project type must define the non-const getter in the appropirate
/// module.
///                                           (21.03.2015.) (Domagoj Saric)
ModuleChainImpl const &Processor::modules() const
{
    return const_cast<Processor &>(*this).modules();
}

} // namespace LE::SW::Engine
