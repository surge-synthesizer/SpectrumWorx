////////////////////////////////////////////////////////////////////////////////
///
/// \file processor.hpp
/// -------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
// https://github.com/jamoma/JamomaDSP
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef processor_hpp__F3D546FF_92C1_4B24_8CCD_EDD88713742D
#define processor_hpp__F3D546FF_92C1_4B24_8CCD_EDD88713742D
//------------------------------------------------------------------------------
#include "buffers.hpp"
#include "channelBuffers.hpp"
#include "setup.hpp"

#include "le/math/dft/fft.hpp"
#include "le/parameters/lfoImpl.hpp"
#include "le/utility/platformSpecifics.hpp"

#include <cstdint>

namespace LE::SW::Engine
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Processor
///
////////////////////////////////////////////////////////////////////////////////

class ModuleChainImpl;

class Processor
{
  public:
    using InputData = float const *LE_RESTRICT const *LE_RESTRICT;
    using OutputData = float *LE_RESTRICT const *LE_RESTRICT;
    using InterleavedInputData = float const *LE_RESTRICT;
    using InterleavedOutputData = float *LE_RESTRICT;

    void process // separated channel data
        (InputData mainInputs, InputData sideChannels, OutputData outputs, std::uint32_t samples,
         float outputGain, float mixAmount);
    void process // interleaved channel data
        (InterleavedInputData mainInputs, InterleavedInputData sideChannels,
         InterleavedOutputData outputs, std::uint32_t samples, float outputGain, float mixAmount);

    void reset() { lfoTimer().reset(); }

    void setNumberOfChannels(std::uint8_t numberOfMainChannels, std::uint8_t numberOfSideChannels);

    void changeWOLAParameters(StorageFactors const &, Setup::Window, Storage);

    void changeWindowFunction(Setup::Window);

  public:
    void clearSideChannelData();
    void resetChannelBuffers();

    Setup const &engineSetup() const { return engineSetup_; }
    Math::FFT_float_real_1D const &fft() const { return fft_; }
    ReadOnlyDataRange const &analysisWindow() const { return analysisWindow_; }
    ReadOnlyDataRange const &synthesisWindow() const { return synthesisWindow_; }

    FullChannelData_AmPh const &currentAmPhData(std::uint8_t const channel) const
    {
        return static_cast<ChannelData const &>(channels_[channel].channelData()).currentAmPhData();
    }
    FullChannelData_ReIm const &currentReImData(std::uint8_t const channel) const
    {
        return static_cast<ChannelData const &>(channels_[channel].channelData()).currentReImData();
    }

    static Processor &fromEngineSetup(Setup &);
    static Processor const &fromEngineSetup(Setup const &);

  public: //...mrmlj...SDK and plugin shared functionality (cleanup and extract)...
    bool setSampleRate(float const sampleRate, StorageFactors &currentStorageFactors);
    bool resize(StorageFactors &currentStorageFactors, StorageFactors const &newStorageFactors,
                Setup::Window window, Engine::HeapSharedStorage &sharedStorage);

    static StorageFactors makeFactors(std::uint16_t fftSize, std::uint8_t overlapFactor,
                                      std::uint8_t numberOfChannels, std::uint32_t sampleRate);

  protected: // LFO & timing
    using LFO = Parameters::LFOImpl;

    void setPosition(std::uint32_t absolutePositionInSamples);
    void updatePosition(std::uint32_t deltaSamples);

    LFO::Timer::TimingInformationChange
    updatePositionAndTimingInformation(float positionInBars, float barDuration,
                                       std::uint8_t measureNumerator);
    LFO::Timer::TimingInformationChange
    updatePositionAndTimingInformation(std::uint32_t deltaNumberOfSamples);

    void handleTimingInformationChange(LFO::Timer::TimingInformationChange);

    LFO::Timer &lfoTimer() { return lfoTimer_; }
    LFO::Timer const &lfoTimer() const { return lfoTimer_; }

  private:
    void updateModuleLFOs(LFO::Timer::TimingInformationChange);

    class ProcessParameters;

    Setup &engineSetup() { return engineSetup_; }

    void processSingleChannel(ProcessParameters const &);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Samples every module's parameters, once, for this call -- and not
    /// until a frame is about to be built from them.
    ///
    ///   `preProcess()` ran unconditionally at the top of process(). A caller's
    /// block and a spectral frame are different quantities, though: the frame
    /// rate is the hop -- `fftSize / overlapFactor` -- so a large FFT under a
    /// small block produces frames more rarely than blocks arrive, and every
    /// block in between still sampled the parameters.
    ///
    ///   For a value that is merely read that costs nothing. For one that is
    /// *consumed* it is fatal: `TriggerParameter::consumeValue()` reads a
    /// trigger and disarms it, so a Freeze armed during a block that produced no
    /// frame was thrown away before anything could act on it. Measured at a
    /// 2048-sample hop under 512-sample blocks, one press in four survived --
    /// which is what "the button works sometimes" was. An LFO never noticed,
    /// because it rearms the parameter every block.
    ///
    /// \note Once per *call*, not once per frame, and the flag is what says so.
    /// A block holding four frames still samples once, exactly as before -- so
    /// nothing an existing caller renders can move. And the channel loop is
    /// outside the frame loop, so without the flag channel 0's first frame would
    /// consume a trigger that channel 1 then never saw.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void preProcessForFirstFrame();
    void preProcess();

    /// Whether preProcessForFirstFrame() has already run for the call in hand.
    bool preProcessedThisCall_{false};

    ModuleChainImpl &modules();
    ModuleChainImpl const &modules() const;

    void calculateWindowAndWOLAGain();

  private:
    using FFTWindow = WindowBuffer<>;

    struct Channels : Utility::SharedStorageBuffer<ChannelBuffers>
    {
        static std::uint32_t requiredStorage(StorageFactors const &);

        void resize(StorageFactors const &factors, Storage &storage);
    }; // struct Channels

  private:
    Setup engineSetup_;
    LFO::Timer lfoTimer_;
    Math::FFT_float_real_1D fft_;
    FFTWindow analysisWindow_;
    FFTWindow synthesisWindow_;
    Channels channels_;

    /// \note See the related note in the calculateWindowAndWOLAGain() member
    /// function.
    ///                                       (04.03.2015.) (Domagoj Saric)
    FFTWindow synthesisWindowBackup_;

  public:
    static std::uint32_t requiredStorage(StorageFactors const &);

    void resize(StorageFactors const &, Storage &);
}; // class Processor

} // namespace LE::SW::Engine

#endif // processor_hpp
