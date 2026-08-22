////////////////////////////////////////////////////////////////////////////////
///
/// \file spectrumWorxCore.hpp
/// --------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef spectrumWorxCore_hpp__8FE46440_DFB7_4B10_AFC4_2536AE1BFE2B
#define spectrumWorxCore_hpp__8FE46440_DFB7_4B10_AFC4_2536AE1BFE2B
//------------------------------------------------------------------------------
#include "automatedModuleChain.hpp"
#include "configuration/versionConfiguration.hpp"
#include "host_interop/host2Plugin.hpp"
#include "host_interop/parameters.hpp"
#include "host_interop/plugin2Host.hpp"

#include "le/plugins/plugin.hpp"
#include "le/spectrumworx/engine/configuration.hpp"
#include "le/spectrumworx/engine/processor.hpp"
#include "le/utility/buffers.hpp"
#include "le/utility/cstdint.hpp"
#include "le/utility/intrinsics.hpp"
#include "le/utility/platformSpecifics.hpp"

#include <atomic>

namespace LE::SW
{

class Module;
class ModuleDSP;

////////////////////////////////////////////////////////////////////////////////
///
/// \class SpectrumWorxCore
///
////////////////////////////////////////////////////////////////////////////////

class SpectrumWorxCore : public Host2PluginInteropControler,
                         public Plugin2HostPassiveInteropController,
                         protected Engine::Processor
{
  protected:
    class InputBuffers;

  public:
    /// \note Before every unqualified use of `Module` below, so that the name
    /// does not mean `::LE::SW::Module` in part of this class and this typedef in
    /// the rest. GCC's -Wchanges-meaning is what catches it.
    typedef SW::Module Module;

    // Plugin required traits.
    /// \note Currently changing the order of reported parameters (or major
    /// groups of parameters) requires the manual updating of the following
    /// SpectrumWorxCore member functions:
    ///  - invokeFunctorOnIdentifiedParameter()
    ///  - parameterIDFromIndex
    ///  - parameterIndexFromBinaryID
    ///                                       (07.03.2013.) (Domagoj Saric)
    static std::uint16_t constexpr maxNumberOfParameters = ParameterCounts::maxNumberOfParameters;
    // Auro 13.1 http://www.auro-technologies.com/system/engine
    static std::uint8_t constexpr maxNumberOfOutputs = 13 + 1;
    static std::uint8_t constexpr maxNumberOfInputs = maxNumberOfOutputs * 2;
    static std::uint16_t constexpr maxLatency = Engine::Constants::maximumFFTSize;
    static std::uint16_t constexpr maxLookAhead = Engine::Constants::maximumFFTSize;
    static std::uint16_t constexpr maxTailSize = 0;
    static std::uint16_t constexpr minimumProcessBufferSize = 0;
    static std::uint8_t constexpr requiredBufferAlignement = Utility::Constants::vectorAlignment;
    static std::uint8_t constexpr versionMajor = SW_VERSION_MAJOR;
    static std::uint8_t constexpr versionMinor = SW_VERSION_MINOR;
    static std::uint8_t constexpr versionPatch = SW_VERSION_PATCH;
    static std::uint32_t constexpr version =
        (versionMajor * 1000) + (versionMinor * 100) + (versionPatch * 10);
    static char const name[];
    static char const productString[];

  public:
    /// \todo Further research and test the meaning and application of "inserts"
    /// and "sends".
    /// http://en.wikipedia.org/wiki/Insert_(effects_processing)
    /// http://en.wikipedia.org/wiki/Aux-send
    /// http://www.chellman.org/audio/sends_versus_inserts.html
    /// http://www.sweetwater.com/expert-center/techtips/d--02/12/2001
    /// http://www.ccisolutions.com/StoreFront/category/channel-inserts?Origin=ArticlesBox
    /// http://www.kvraudio.com/forum/viewtopic.php?t=97120
    /// http://www.kvraudio.com/forum/viewtopic.php?t=97194
    ///                                       (08.07.2010.) (Domagoj Saric)
    DECLARE_PLUGIN_CAPABILITIES(Plugins::AsInsert, Plugins::AsSend, Plugins::Ch1in1out,
                                Plugins::Ch2in1out, Plugins::Ch2in2out, Plugins::Ch4in2out,
                                Plugins::Ch4in4out, Plugins::Ch8in4out, Plugins::Ch8in8out,
                                Plugins::ReceiveVSTTimeInfo, Plugins::MixDryWet,
                                Plugins::UsesAFixedSizeGUI);

  public: // AU effect interface:
    std::uint8_t numberOfInputChannels() const { return engineSetup().numberOfChannels(); }
    std::uint8_t numberOfOutputChannels() const { return numberOfInputChannels(); }
    std::uint8_t numberOfSideChannels() const { return engineSetup().numberOfSideChannels(); }
    std::uint32_t processBlockSize() const { return buffers().blockSize(); }

    /// \note Unchecked, and it matters: `engineSetup()` asserts that the setup
    /// still agrees with the *spectral* parameters -- the FFT size, the overlap
    /// factor and the window size factor -- and the sample rate is not one of
    /// them, so that assertion has nothing to say about the value this returns.
    /// `process()` reaches it on every block, through `updateLFOTiming()`.
    float getSampleRate() const { return uncheckedEngineSetup().sampleRate<float>(); }

    void reset();
    void uninitialise();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Pins this instance's randomness, so that a render can be repeated.
    ///
    ///   Every generator in the engine -- one per LFO, one per channel of each
    /// effect that draws -- is dealt its stream from a single per-instance
    /// source at `reset()`. That source normally takes the clock and a stack
    /// address, which is what keeps two instances on two tracks from producing
    /// the same noise; this replaces it with a value, and everything downstream
    /// follows deterministically because the deal is a fixed walk of the chain.
    ///
    /// \param seed zero restores the clock, which is the default.
    ///
    /// \note **Sticky, and takes effect at the next `reset()`** -- which
    /// `resume()` performs, so a caller sets this once during setup and every
    /// pass thereafter repeats. Re-dealing here would mean a seed the next
    /// transport stop discards, and mutating the chain from whichever thread set
    /// the parameter. \see doc/tech/threading_model.md.
    ///
    /// \note Used by the test harness. Offering it to the *user*, so that an
    /// offline bounce can be made reproducible, is issue #105.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void setRandomSeed(std::uint64_t seed);

  public: // VST2.4 effect interface:
    void resume();
    void suspend();

    bool setBlockSize(unsigned int blockSize);
    bool setSampleRate(float const &sampleRate);

  public: // Shared plugin interface:
    bool initialise();

  public:
    Engine::Setup const &engineSetup() const;
    Engine::Setup const &uncheckedEngineSetup() const;

    Processor::LFO::Timer const &lfoTimer() const { return Engine::Processor::lfoTimer(); }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Whether anything can be inside process() right now.
    ///
    /// \note The whole of the synchronisation, and it is not a lock. While this
    /// is false there is no audio thread -- `clap_plugin::activate` and
    /// `deactivate` bracket the only window in which one exists -- so the main
    /// thread owns the engine outright and builds, installs and destroys what it
    /// likes. While it is true the audio thread owns the engine and everything
    /// the main thread wants done crosses as a message.
    ///
    ///   That is why the publish helpers below take a queue *and* apply
    /// themselves directly: which of the two it is depends only on this.
    ///
    ////////////////////////////////////////////////////////////////////////////
    bool engineIsRunning() const { return !suspended_; }

    Engine::StorageFactors const &currentStorageFactors() const { return currentStorageFactors_; }

    static SpectrumWorxCore const &fromEngineSetup(Engine::Setup const &);

    ////////////////////////////////////////////////////////////////////////////
    //
    // Publish and retire: the only two ways the chain ever changes.
    //
    // Both are pure pointer surgery -- no allocation, no destruction, no file
    // IO, no formatting -- so both are legal inside process(). Whoever *builds*
    // the module or the chain does so on the main thread and hands it over;
    // whatever comes back out goes the other way and is destroyed there.
    // \see doc/tech/threading_model.md §5.
    //
    ////////////////////////////////////////////////////////////////////////////

    /// \brief Puts \p pModule in \p slot, or empties the slot when it is null.
    ///
    /// \param pModule one reference, transferred; already resized and
    ///        initialised against the current setup by whoever built it.
    /// \return the module that came out, also one reference, for the caller to
    ///         release -- never null-checked away and never released here.
    Module *installModuleInSlot(std::uint8_t slot, Module *pModule);

    /// \brief Exchanges the live chain with \p chain, which comes back holding
    /// what used to be live.
    void swapModuleChain(AutomatedModuleChain &chain);

  protected:
    SpectrumWorxCore();
#ifndef NDEBUG
    ~SpectrumWorxCore() {}
#endif // NDEBUG

    /// \brief One block. Always -- nothing takes a lock here and nothing else
    /// mutates the engine while this runs, so there is no block to decline and no
    /// caller left with a buffer to fill in for one.
    void process(float const *const *inputs, float const *const *pSideChannels,
                 float *const *outputs, float const &outputGainScale, unsigned int samples);

    bool updateEngineSetup();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The spectral setup, deferred.
    ///
    /// \note FFT size, overlap factor and window function reallocate the whole
    /// spectral working set and resize every module in the chain, which is not
    /// something a block may wait for. So the parameter moves and the *setup*
    /// does not: this records that they disagree, the plugin asks the host to
    /// restart, and `deactivate()` applies it where the audio thread
    /// definitionally is not running -- which is also the only point at which
    /// `clap_plugin_latency` permits the latency to change, and the FFT size
    /// *is* the latency.
    ///
    ///   `engineSetup()`'s assertion tolerates the gap while it is open.
    ///
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Acquire against the release in `deferOrApplySpectralSetup()`. The
    /// flag is read by a thread that did not write it -- the audio thread sets it
    /// from `drainCommands()` and `deactivate()` is where it is acted on -- and
    /// what has to be visible with it is the *parameter* the setter moved just
    /// before, which is what `applyPendingSpectralSetup()` then reads. A relaxed
    /// pair would order the flag and say nothing about the value behind it.
    bool spectralSetupPending() const
    {
        return spectralSetupPending_.load(std::memory_order_acquire);
    }
    bool applyPendingSpectralSetup();

  public:
    using LFO = LE::Parameters::LFOImpl;
    using Parameters = GlobalParameters::Parameters;

    Program &program()
    {
        LE_ASSUME(pProgram_);
        return *pProgram_;
    }
    Program const &program() const
    {
        LE_ASSUME(pProgram_);
        return *pProgram_;
    }

    Parameters &parameters() { return program().parameters(); }
    Parameters const &parameters() const { return program().parameters(); }

    ModuleChain &moduleChain() { return program().moduleChain(); }
    ModuleChain const &moduleChain() const { return program().moduleChain(); }

    void setProgram(Program &program) { pProgram_ = &program; }

    Program const &dynamicParameterAccessContext() const
    {
        return program();
    } //...mrmlj...for lack of implicit conversion to Program...

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.
#endif                  // _MSC_VER
    struct ModuleInitialiser
    {
        typedef SpectrumWorxCore::Module Module;

        bool operator()(Module &, std::uint8_t moduleIndex) const;

        Engine::StorageFactors const &storageFactors;
        SpectrumWorxCore const &effect;
    }; // struct ModuleInitialiser
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

    ModuleInitialiser moduleInitialiser();

    ////////////////////////////////////////////////////////////////////////////
    // IO setup
    ////////////////////////////////////////////////////////////////////////////

  public:
    std::uint8_t numberOfChannels() const;

    enum struct IOChangeResult : std::uint8_t
    {
        Failed = false,
        Succeeded = true,
        NoChangeRequired
    };
    IOChangeResult setNumberOfChannels(std::uint8_t numberOfInputChannels,
                                       std::uint8_t numberOfOutputChannels);

  protected:
    void updateInputModeForIOConfig(std::uint8_t numberOfMainChannels,
                                    std::uint8_t numberOfSideChannels);

    void setReportedNumberOfChannels(std::uint8_t numberOfMainChannels,
                                     std::uint8_t numberOfSideChannels);

    static bool checkChannelConfiguration(std::uint8_t numberOfInputChannels,
                                          std::uint8_t numberOfOutputChannels);
    bool setNumberOfChannelsImpl(std::uint8_t numberOfMainChannels,
                                 std::uint8_t numberOfSideChannels);

    /// \note What honours `ChannelData`'s invariant: `setNewTimeDomainData()`
    /// will not clear the side spectrum when handed a null side channel, so
    /// whoever makes the side data disappear has to. Called from
    /// `SpectrumWorxCLAP` on both paths that retire a sample, and from
    /// `Processor::changeWOLAParameters()` when the buffers are resized.
    void clearSideChannelData();

    /* </IO configuration> */

  protected:
  public:
    template <class Parameter>
    static bool setGlobalParameter(Parameter &parameter,
                                   typename Parameter::param_type const newValue)
    {
        parameter.setValue(newValue);
        return true;
    }

#pragma warning(push)
#pragma warning(disable : 4389)              // Signed/unsigned mismatch.
    template <class Parameter, class SWImpl> //...mrmlj...ugly duplication workaround...
    static bool setGlobalParameter(SWImpl &swImpl, typename Parameter::param_type const newValue)
    {
        // the same check the other engine mutators have. Every route in
        // satisfies it: a host automation event and a drained command arrive on
        // the audio thread, a preset with nothing running arrives on the main
        // thread owning the engine, and a preset with audio running queues
        LE_ASSERT_MSG(swImpl.currentThreadMayMutateEngineState(),
                      "A global parameter written by a thread that does not own the engine.");

        Parameter &parameter(swImpl.parameters().template get<Parameter>());
        typename Parameter::value_type const oldValue(parameter);
        if (newValue == oldValue)
            return true;
        bool const result(swImpl.setGlobalParameter(parameter, newValue));
        LE_ASSERT_MSG(
            (result && (parameter.getValue() == newValue)) ||
                (!result && (parameter.getValue() == oldValue)),
            "setGlobalParameter left the engine parameters and/or setup in an inconsistent state.");
        return result;
    }
#pragma warning(pop)

    bool setGlobalParameter(FFTSize &, FFTSize ::param_type newValue);
    bool setGlobalParameter(OverlapFactor &, OverlapFactor ::param_type newValue);
    bool setGlobalParameter(WindowFunction &, WindowFunction::param_type newValue);

  protected:
    void updateForWindowChange(unsigned int window);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Whether the calling thread may mutate engine state right now.
    ///
    /// \note The rule threading_model.md §2 states: **the audio thread owns the
    /// engine while one exists, and the main thread owns it while one does not**.
    ///
    /// \note `Threading::isAudioThread()` is "this call is inside an
    /// `[audio-thread]` entry point", not "this call is under `process()`" -- an
    /// engine can be driven from a test's own thread, a host is not obliged to
    /// use the same OS thread twice, and `reset()` is `[audio-thread]` between
    /// blocks. \see `Threading::ScopedAudioThreadEntry`.
    ///
    ////////////////////////////////////////////////////////////////////////////
    bool currentThreadMayMutateEngineState() const;

  public:
    /// \note One of the three chain mutations, and public for the same reason as
    /// the two above: `Threading::publishModuleMove()` reaches it, from whichever
    /// thread turns out to own the engine.
    void moveModule(std::uint8_t sourceIndex, std::uint8_t targetIndex);

  protected:
    InputBuffers &buffers() { return buffers_; }
    InputBuffers const &buffers() const { return buffers_; }

  protected:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Whether a host's automation write should be refused right now.
    ///
    /// \note Never. Blocking is an answer to hosts that echoed a parameter
    /// change back as automation while the load which caused it was still
    /// running, and nothing here echoes anything. A preset writes every parameter
    /// it names, so an automation event arriving during one is either overwritten
    /// by the load or lands after it -- and landing after it is what a running
    /// lane is supposed to do.
    ///
    /// \note Which also keeps `Host2PluginInteropControler`'s preset-load flag
    /// the interface's own, read only on the main thread and needing no
    /// synchronisation.
    ///
    ////////////////////////////////////////////////////////////////////////////
    bool blockAutomation() const { return false; }

  private:
    bool isEngineSetupUpToDate() const;

    /// \see the definition.
    bool deferOrApplySpectralSetup();

    void resetChannelBuffers();

    bool resize(Engine::StorageFactors const &newfactors);

  private:
    friend class Engine::Processor;
    static Engine::ModuleChainImpl &modules(Engine::Processor &processor)
    {
        return static_cast<SpectrumWorxCore &>(processor).moduleChain();
    }

  protected:
    class InputBuffers
    {
      public:
        using Channels = Utility::SharedStorageBuffer<Engine::real_t *LE_RESTRICT>;

        InputBuffers() : blockSize_(0), forceSideChannel_(false) {}

        /// \note `std::uint32_t` here and on the member, `setBlockSize()` taking
        /// an `unsigned int` straight from the host: narrowed to 16 bits a block
        /// of 65536 becomes 0, and an offline render is exactly where a host uses
        /// a block far larger than its realtime one.
        bool resize(std::uint32_t blockSize, std::uint8_t numberOfMainChannels,
                    std::uint8_t numberOfSideChannels);

        Engine::DataRange mainChannel(unsigned int const channel) const
        {
            return Engine::DataRange(mainChannels_[channel], mainChannels_[channel] + blockSize());
        }
        Engine::DataRange sideChannel(unsigned int const channel) const
        {
            return Engine::DataRange(sideChannels_[channel], sideChannels_[channel] + blockSize());
        }

        Channels const &mainChannels() const { return mainChannels_; }
        Channels const &sideChannels() const { return sideChannels_; }

        std::uint32_t blockSize() const;

        std::uint8_t numberOfMainChannels() const
        {
            return static_cast<std::uint8_t>(mainChannels_.size());
        }
        std::uint8_t numberOfSideChannels() const
        {
            return static_cast<std::uint8_t>(sideChannels_.size());
        }

        void forceSideChannel(bool const value) { forceSideChannel_ = value; }

        bool operator!() const { return !storage_; }

      private:
        static void initializeChannelPointers(std::size_t blockBytes, Channels &,
                                              Engine::Storage &);

      private:
        Channels mainChannels_;
        Channels sideChannels_;
        std::uint32_t blockSize_;
        Engine::HeapSharedStorage storage_;

        //...mrmlj...
        bool forceSideChannel_;
    }; // class InputBuffers

  private:
    InputBuffers buffers_;

    Program *LE_RESTRICT pProgram_;

    /// \note Not a debugging aid: `engineIsRunning()` is what decides whether a
    /// structural change is applied here and now or handed to the audio thread.
    bool suspended_;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The FFT size, overlap factor or window function the engine is not
    /// running yet.
    ///
    /// \note Atomic because both threads write it: a knob or a host automation
    /// event reaches `setGlobalParameter` from `drainCommands()`, on the audio
    /// thread, and a preset load reaches the same setter from the main thread
    /// with audio running. The whole point of the flag is to be noticed by the
    /// *other* thread, which a plain `bool` does not promise.
    ///
    ////////////////////////////////////////////////////////////////////////////

    std::atomic<bool> spectralSetupPending_{false};

    Engine::StorageFactors currentStorageFactors_;
    Engine::HeapSharedStorage sharedStorage_;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Where every generator in this instance gets its stream from.
    ///
    ///   Per instance rather than per process, so that two plugins on two tracks
    /// do not draw from one sequence -- which would be a data race as well as a
    /// shared stream. \see Math::Rng.
    ///
    /// \note Only ever read at `reset()` and `setRandomSeed()`, never from the
    /// audio path -- the dealt streams are what the audio path touches, and each
    /// of those belongs to exactly one channel of one module.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Math::Rng seedSource_;

    /// \brief What to seed `seedSource_` with at the next reset, or zero for the
    /// clock. \see setRandomSeed().
    std::uint64_t fixedSeed_{0};
}; // class SpectrumWorxCore

} // namespace LE::SW

#endif // spectrumWorxCore_hpp
