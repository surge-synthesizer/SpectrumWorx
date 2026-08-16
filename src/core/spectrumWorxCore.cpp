////////////////////////////////////////////////////////////////////////////////
///
/// spectrumWorxCore.cpp
/// --------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "spectrumWorxCore.hpp"

#include "modules/moduleDSPAndGUI.hpp"
#include "threading/threadCheck.hpp"

#include "le/math/conversion.hpp"
#include "le/math/math.hpp"
#include "le/math/vector.hpp"
#include "le/parameters/lfo.hpp"
#include "le/utility/clear.hpp"
#include "le/utility/parentFromMember.hpp"
#include "le/utility/tchar.hpp"

#include "le/utility/stackBuffer.hpp"

#include "le/utility/assert.hpp"

/// \note `#include <windows.h> // CRITICAL_SECTION` stood here, for the cast
/// currentThreadOwnsTheProcessLock() used to make. The mutex answers for itself
/// now, so the one thing in this file that needed the platform header is gone.
///                                           (02.08.2026.) (SW port)

#include <cstdlib>
#include <ctime>
#include <string_view>

namespace LE
{

namespace SW
{

namespace Engine
{
ModuleChainImpl &Processor::modules() { return SpectrumWorxCore::modules(*this); }
} // namespace Engine

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxCore UI elements definitions.
//
////////////////////////////////////////////////////////////////////////////////

char const SpectrumWorxCore::name[] = "SpectrumWorx" SW_EDITION_STRING;
char const SpectrumWorxCore::productString[] = "SpectrumWorx (Little Endian Ltd. (c) (tm))";

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxCore::SpectrumWorxCore()
// ------------------------------------
//
////////////////////////////////////////////////////////////////////////////////

SpectrumWorxCore::SpectrumWorxCore()
{
    Utility::clear(currentStorageFactors_);

    suspended_ = true;

    setReportedNumberOfChannels(maxNumberOfOutputs, maxNumberOfInputs - maxNumberOfOutputs);
}

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxCore::process()
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCore::process /// \throws nothing
    (float const *const *pMainChannels, float const *const *const pSideChannels,
     float *const *const outputs, float const &outputGainScale, unsigned int const samples)
{
    /// \note An `#ifdef _DEBUG` block stood around this function's body: it
    /// enabled FPU exceptions for the call and wrapped everything in
    /// `catch (...)` that raised `GUI::warningMessageBox`. `_DEBUG` is
    /// Microsoft's, defined by the debug CRT and by no other compiler, so the
    /// block was live in exactly one configuration -- and that configuration
    /// would not link, because sw-dsp has no GUI to call and must not: it is
    /// goal 3 of threading_model.md and `engine-links-no-juce` checks it. The
    /// engine's other message boxes went the same way, to the counted reporter
    /// `reportPresetProblem` uses.
    ///
    ///   Raising a modal dialog is also the last thing to do here. This is the
    /// audio callback; a dialog on this thread is the deadlock the whole
    /// threading redesign was about, and the function's own contract two lines
    /// above says it throws nothing.
    ///                                       (05.08.2026.) (SW port)
    LE_ASSERT_MSG(samples <= buffers_.blockSize(), "Process called with a too large block size.");
    LE_ASSERT_MSG(!!buffers(), "Input buffers not initialised.");

    // If gain needs to be applied to input data we first need to copy it to
    // internal buffers:
    using namespace GlobalParameters;
    float const &inputGain(parameters().get<InputGain>());
    if (!Math::is<1>(inputGain))
    {
        unsigned int const numberOfChannels(engineSetup().numberOfChannels());
        LE_STACK_BUFFER(mainChannels, float const *, numberOfChannels);
        for (unsigned int channel(0); channel < numberOfChannels; ++channel)
        {
            float *LE_RESTRICT const pChannel(buffers().mainChannel(channel).begin());
            Math::multiply(pMainChannels[channel], inputGain, pChannel, samples);
            mainChannels[channel] = pChannel;
        }
        pMainChannels = &mainChannels[0];
    }

    Engine::Processor::process(pMainChannels, pSideChannels, outputs, samples,
                               parameters().get<OutputGain>() * outputGainScale,
                               parameters().get<MixPercentage>());
}

void SpectrumWorxCore::suspend() // pause
{
    //...mrmlj...could be called on startup when the flag is already set by the
    //constructor...
    //LE_ASSERT( !suspended_ );
    suspended_ = true;
}

void SpectrumWorxCore::resume()
{
#ifndef __APPLE__ //...mrmlj...isHost( "Vst2Au2" )
    LE_ASSERT(suspended_);
#endif // __APPLE__
    /// \note Before the flag, not after: reset() mutates the engine, and
    /// currentThreadMayMutateEngineState() is what says whether this thread may.
    reset();
    suspended_ = false;
}

void SpectrumWorxCore::reset()
{
    Math::rngSeed();
    moduleChain().resetAll();
    resetChannelBuffers();
    Engine::Processor::reset();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note **Everything here counts in `std::size_t`.** The parameter was a
/// `std::uint16_t` and so was `blockBytes`, computed as `blockSize * 4`:
///
///   - at 16384 samples `blockBytes` wrapped to 0, so `requiredStorage` came out
///     as the pointer array alone, `initializeChannelPointers` advanced by
///     nothing and laid every channel's buffer on top of the others, and
///     `blockSize_` still recorded 16384. Every subsequent `mainChannel(c)`
///     write then ran off the end of a block sized for none of it.
///   - at 65536 the parameter itself truncated to 0.
///
///   `setBlockSize()` takes an `unsigned int` from the host and the conversion
///   was implicit, so neither had a diagnostic. Both sizes are reachable: an
///   offline render is exactly where a host hands over a block far larger than
///   its realtime one, and 16384 is not an unusual choice.
///
/// \note And the allocation is checked before anything is written, which it was:
/// `storage_.resize()` failing already returned false. What is new is that it can
/// no longer be asked for an absurd size by arithmetic rather than by the caller.
///                                           (16.08.2026.)
///
////////////////////////////////////////////////////////////////////////////////

bool SpectrumWorxCore::InputBuffers::resize(std::uint32_t const blockSize,
                                            std::uint8_t const numberOfMainChannels,
                                            std::uint8_t numberOfSideChannels)
{
    //...mrmlj...ugh...external audio problems ugly quick-fix...
    numberOfSideChannels =
        std::max<std::uint8_t>(numberOfSideChannels, forceSideChannel_ * numberOfMainChannels);

    if ((blockSize == this->blockSize()) &&
        (numberOfMainChannels == this->numberOfMainChannels()) &&
        (numberOfSideChannels == this->numberOfSideChannels()))
        return true;

    /// \note A `static_assert(std::is_pointer<float *LE_RESTRICT>::value, "")`
    /// stood here, marked "typeTraits.hpp debugging" -- somebody's scratch check
    /// on how the compiler of the day treated the restrict qualifier, with an
    /// empty message, asserting nothing about this function. GCC fails it:
    /// `__restrict__` is part of the type there and `std::is_pointer` does not
    /// know to look through it. A leftover probe should not decide whether the
    /// engine builds.
    ///                                       (05.08.2026.) (SW port)
    using Utility::align;
    std::uint8_t const numberOfChannels(numberOfMainChannels + numberOfSideChannels);
    std::uint8_t const baseChannelStorage(sizeof(Channels::iterator));
    std::size_t const blockBytes(std::size_t{blockSize} * sizeof(Engine::real_t));
    auto const channelDataStorage(align(blockBytes));
    auto const requiredStorage(align(numberOfChannels * baseChannelStorage) +
                               numberOfChannels * channelDataStorage);

    if (!storage_.resize(requiredStorage))
        return false;

    blockSize_ = blockSize;

    Engine::Storage storage(storage_);
    mainChannels_.resize(numberOfMainChannels * baseChannelStorage, storage);
    sideChannels_.resize(numberOfSideChannels * baseChannelStorage, storage);
    initializeChannelPointers(blockBytes, mainChannels_, storage);
    initializeChannelPointers(blockBytes, sideChannels_, storage);
    LE_ASSERT_MSG(storage.size() < 16, "Requested storage not consumed.");

    return true;
}

void SpectrumWorxCore::InputBuffers::initializeChannelPointers(std::size_t const blockBytes,
                                                               Channels &channels,
                                                               Engine::Storage &storage)
{
    char *LE_RESTRICT pStorage(storage.begin());
    for (auto &pointer : channels)
    {
        typedef Channels::value_type pointer_t;
        pointer_t const newBeginning(static_cast<pointer_t>(Math::align(pStorage)));
        unsigned int const alignmentFixup(
            static_cast<unsigned int>(reinterpret_cast<char const *>(newBeginning) - pStorage));
        pointer = newBeginning;
        pStorage += alignmentFixup + blockBytes;
        LE_ASSERT(pStorage <= storage.end());
    }
    storage = Engine::Storage(pStorage, storage.end());
}

unsigned int SpectrumWorxCore::InputBuffers::blockSize() const { return blockSize_; }

bool SpectrumWorxCore::ModuleInitialiser::operator()(Module &module,
                                                     std::uint8_t /*const moduleIndex*/) const
{
    /// \note AUs can have their parameters changed while uninitialised.
    ///                                       (19.04.2013.) (Domagoj Saric)
    bool const initialised(storageFactors.complete());
#if defined(_WIN32)
    LE_ASSUME(initialised);
#endif // _WIN32
    /// \note A module accepted into the chain before the engine has been set up
    /// is *not* set up here. It used to be, against whatever Setup happened to
    /// exist -- which before activate() means no sample rate, no bins and no step
    /// time. An effect asked to work that out lands anywhere from a harmless zero
    /// to indexing off the end of an empty spectrum: Bandpass and Bandstop read
    /// bin[0] of nothing, Denoiser divides an amplitude range it computed as
    /// empty, and every knob whose range quantises to a step time has nothing to
    /// quantise against.
    ///
    ///   Deferring is safe because it is not a special case: the engine resizes
    /// its whole chain whenever its setup changes, which is how a runtime FFT
    /// size change already works, and activate() is just the first such change.
    /// Returning true is still right -- the module belongs in the chain, it
    /// simply has nothing to be configured against yet.
    ///                                       (29.07.2026.) (SW port)
    if (!initialised)
        return true;

    if (module.resize(storageFactors))
    {
        /// \note resize() must also call reset() so we don't have to.
        ///                                   (05.04.2012.) (Domagoj Saric)
        //...mrmlj...preProcess() should not require initialisation/resizing (ChannelState allocation)...
        module.initialise(effect.engineSetup());
        return true;
    }
    return false;
}

SpectrumWorxCore::ModuleInitialiser SpectrumWorxCore::moduleInitialiser()
{
    return {currentStorageFactors(), *this};
}

////////////////////////////////////////////////////////////////////////////////
// IO setup
////////////////////////////////////////////////////////////////////////////////

std::uint8_t SpectrumWorxCore::numberOfChannels() const { return engineSetup().numberOfChannels(); }

SpectrumWorxCore::IOChangeResult
SpectrumWorxCore::setNumberOfChannels(std::uint8_t const numberOfInputChannels,
                                      std::uint8_t const numberOfOutputChannels)
{
    if (!checkChannelConfiguration(numberOfInputChannels, numberOfOutputChannels))
        return IOChangeResult::Failed;

    auto const numberOfMainChannels(numberOfOutputChannels);
    auto const numberOfSideChannels(numberOfInputChannels - numberOfOutputChannels);

    if ((numberOfMainChannels == engineSetup().numberOfChannels()) &&
        (numberOfSideChannels == engineSetup().numberOfSideChannels()))
        return IOChangeResult::NoChangeRequired;

    return static_cast<IOChangeResult>(
        setNumberOfChannelsImpl(numberOfMainChannels, numberOfSideChannels));
}

bool SpectrumWorxCore::setNumberOfChannelsImpl(std::uint8_t const numberOfMainChannels,
                                               std::uint8_t const numberOfSideChannels)
{
    LE_ASSERT(currentThreadMayMutateEngineState());

    if (!buffers().resize(buffers().blockSize(), numberOfMainChannels, numberOfSideChannels))
        return false;

    Engine::StorageFactors newStorageFactors(currentStorageFactors());
    newStorageFactors.numberOfChannels = numberOfMainChannels;
    if (!resize(newStorageFactors))
    {
        LE_VERIFY(buffers().resize(buffers().blockSize(), engineSetup().numberOfChannels(),
                                   engineSetup().numberOfSideChannels()));
        return false;
    }

    updateInputModeForIOConfig(numberOfMainChannels, numberOfSideChannels);
    setReportedNumberOfChannels(numberOfMainChannels,
                                numberOfSideChannels); //...mrmlj...spaghetti...
    return true;
}

bool SpectrumWorxCore::checkChannelConfiguration(std::uint8_t const numberOfInputChannels,
                                                 std::uint8_t const numberOfOutputChannels)
{
#ifndef __APPLE__
    /// \note AU hosts can try any IO configuration (the auval tool specifically
    /// will). See the related note in
    /// PropertyHandler<kAudioUnitProperty_StreamFormat>::set() in the
    /// au/plugin.inl file.
    ///                                       (27.08.2014.) (Domagoj Saric)
    LE_ASSERT_MSG(!(numberOfInputChannels < numberOfOutputChannels),
                  "SW cannot/does not downmix side channels.");
    LE_ASSERT_MSG(!(numberOfInputChannels > 2 * numberOfOutputChannels),
                  "SW cannot 'produce' channels.");
#endif // __APPLE__

    return (numberOfInputChannels == numberOfOutputChannels) ||
           (numberOfInputChannels == 2 * numberOfOutputChannels);
}

void SpectrumWorxCore::updateInputModeForIOConfig(
    [[maybe_unused]] std::uint8_t const numberOfMainChannels,
    [[maybe_unused]] std::uint8_t const numberOfSideChannels)
{
}

void SpectrumWorxCore::setReportedNumberOfChannels(std::uint8_t const numberOfMainChannels,
                                                   std::uint8_t const numberOfSideChannels)
{
    Engine::Processor::setNumberOfChannels(numberOfMainChannels, numberOfSideChannels);
}

/// \note Three answers over three stages, and this is the last of them. It was a
/// `reinterpret_cast` of the mutex to a `CRITICAL_SECTION` under `_WIN32` --
/// invalid for the MS STL's `std::mutex` when it was written and invalid for the
/// `std::recursive_mutex` that replaced it -- and a hardcoded `true` everywhere
/// else, so the six sites that assert this asserted nothing at all on any
/// platform this port has built for. Stage 0 made the mutex answer for itself.
/// Stage 6 deleted the mutex, and what is left is the invariant the mutex was
/// standing in for.
///                                           (02.08.2026.) (SW port)
bool SpectrumWorxCore::currentThreadMayMutateEngineState() const
{
    return !engineIsRunning() || Threading::isAudioThread();
}

bool SpectrumWorxCore::setBlockSize(unsigned int const newBlockSize)
{
    if (newBlockSize == buffers().blockSize())
        return true;
    LE_ASSERT(currentThreadMayMutateEngineState());
    return buffers().resize(newBlockSize, engineSetup().numberOfChannels(),
                            engineSetup().numberOfSideChannels());
}

bool SpectrumWorxCore::isEngineSetupUpToDate() const
{
    using namespace Engine;
    Parameters const &parameters(this->parameters());
    return ((uncheckedEngineSetup().fftSize<unsigned int>() == parameters.get<FFTSize>()) &&
            (uncheckedEngineSetup().windowOverlappingFactor<unsigned int>() ==
             parameters.get<OverlapFactor>()));
}

Engine::Setup const &SpectrumWorxCore::engineSetup() const
{
    /// \note The third term is new and is the price of the restart route: while
    /// a restart is pending the FFT size parameter reads what the user asked for
    /// and the setup still reads what the engine is running, and they are
    /// *supposed* to disagree. Making that legal is a change to this assertion's
    /// contract rather than to any of the call sites that reach it.
    ///                                       (02.08.2026.) (SW port)
    LE_ASSERT(isEngineSetupUpToDate() || spectralSetupPending() ||
              !currentStorageFactors().complete());
    return uncheckedEngineSetup();
}

Engine::Setup const &SpectrumWorxCore::uncheckedEngineSetup() const
{
    return Engine::Processor::engineSetup();
}

bool SpectrumWorxCore::resize(Engine::StorageFactors const &newfactors)
{
    LE_ASSERT(currentThreadMayMutateEngineState());
    return Engine::Processor::resize(
        currentStorageFactors_, newfactors,
        static_cast<Engine::Setup::Window>(parameters().get<WindowFunction>().getValue()),
        sharedStorage_);
}

bool SpectrumWorxCore::updateEngineSetup()
{
    using namespace Engine;

    //...mrmlj...rethink this...LE_ASSERT( !isEngineSetupUpToDate() );
    Setup const &setup(uncheckedEngineSetup());

    LE_ASSERT(currentThreadMayMutateEngineState());

    Parameters &parameters(this->parameters());

    /// \note An assertion that the setup's window function still agrees with the
    /// parameter stood here. It cannot: a window change is one of the three that
    /// now waits for a restart, and applying it is this function's own job --
    /// resize() passes the parameter down and calls changeWindowFunction() when
    /// nothing else moved.
    ///                                       (02.08.2026.) (SW port)

    StorageFactors storageFactors(
        Processor::makeFactors(parameters.get<FFTSize>(), parameters.get<OverlapFactor>(),
                               setup.numberOfChannels(), setup.sampleRate<std::uint32_t>()));

    if (resize(storageFactors))
        return true;

    // Restore previous settings on failure:
    parameters.set<FFTSize>(setup.fftSize<FFTSize ::value_type>());
    parameters.set<OverlapFactor>(setup.windowOverlappingFactor<OverlapFactor::value_type>());
    updateInputModeForIOConfig(setup.numberOfChannels(), setup.numberOfSideChannels());

    return false;
}

void SpectrumWorxCore::updateForWindowChange(unsigned int const window)
{
    LE_ASSERT(window == parameters().get<WindowFunction>());
    /// \note AU can set a preset before initialise() (and thus
    /// updateEngineSetup()) is called.
    ///                                       (05.03.2013.) (Domagoj Saric)
    bool const storageFactorsComplete(currentStorageFactors().complete());
#ifdef _WIN32
    LE_ASSUME(storageFactorsComplete);
#endif // _WIN32
    if (storageFactorsComplete)
        Engine::Processor::changeWindowFunction(static_cast<Engine::Constants::Window>(window));
}

bool SpectrumWorxCore::setSampleRate(float const &sampleRate)
{
    return Engine::Processor::setSampleRate(sampleRate, currentStorageFactors_);
}

bool SpectrumWorxCore::initialise()
{
    /// \note We do not abort early on error here to 'handle' bad protocols like
    /// VST2.4 (that do not allow a return code/error for the initialise() call)
    /// by performing as much initialisation as possible to minimize the chance
    /// of crashing the whole host if something fails here.
    ///                                       (05.03.2013.) (Domagoj Saric)
    bool success(true);
    success &= updateEngineSetup();

    Math::rngSeed();

    return success;
}

void SpectrumWorxCore::uninitialise()
{
    LE_ASSERT_MSG(sharedStorage_, "Not initialised.");
    //Utility::clear( currentStorageFactors_ );
    setNumberOfChannelsImpl(0, 0);
    LE_ASSERT(currentStorageFactors_.numberOfChannels == 0);
    LE_ASSERT(buffers().mainChannels().size() == 0);
    LE_ASSERT(buffers().sideChannels().size() == 0);
    LE_VERIFY(moduleChain().resizeAll(currentStorageFactors(), currentStorageFactors()));
    sharedStorage_.resize(0);
}

void SpectrumWorxCore::clearSideChannelData()
{
    LE_ASSERT(currentThreadMayMutateEngineState());
    Engine::Processor::clearSideChannelData();
}

void SpectrumWorxCore::resetChannelBuffers()
{
    LE_ASSERT(currentThreadMayMutateEngineState());
    Engine::Processor::resetChannelBuffers();
}

/// \note `SpectrumWorxCore::handleTimingInformationChange()` stood here. It hid
/// `Processor`'s -- non-virtually -- to assert that the timing never changes,
/// "because SpectrumWorxCore is used by protocols that do not provide tempo
/// information" (02.02.2012.). The CLAP made that false the moment it started
/// feeding the host's tempo, and the assumption was one of the nine `LE_ASSUME`s
/// clang was dropping on the floor for containing a call -- so it never fired.
///
///   Nothing named it: `Processor::updatePosition()` and the two
/// `updatePositionAndTimingInformation()`s call their own, statically, and it was
/// private here. Deleted rather than turned into an assert, because reachable it
/// would have been the wrong behaviour twice over -- an assert on a legitimate
/// tempo change, and, in release, an LFO update swallowed.
///                                           (02.08.2026.) (SW port)

//namespace GUI { bool isThisTheGUIThread(); bool isGUIInitialised(); }
SpectrumWorxCore const &SpectrumWorxCore::fromEngineSetup(Engine::Setup const &engineSetup)
{
    //LE_ASSERT_MSG( !GUI::isGUIInitialised() || !GUI::isThisTheGUIThread(), "Ambiguous!" ); //...mrmlj...
    return static_cast<SpectrumWorxCore const &>(Engine::Processor::fromEngineSetup(engineSetup));
}

void SpectrumWorxCore::moveModule(std::uint8_t const sourceIndex, std::uint8_t const targetIndex)
{
    LE_ASSERT(currentThreadMayMutateEngineState());
    moduleChain().moveModule(sourceIndex, targetIndex);
}

////////////////////////////////////////////////////////////////////////////////
// Publish and retire
////////////////////////////////////////////////////////////////////////////////

SW::Module *SpectrumWorxCore::installModuleInSlot(std::uint8_t const slot,
                                                  SW::Module *const pModule)
{
    LE_ASSERT(currentThreadMayMutateEngineState());

    auto &chain(moduleChain());
    /// \note The *base* overload, explicitly. `AutomatedModuleChain::module()`
    /// hides it and answers with an `IntrusivePtr` that is simply null past the
    /// end -- which `isEnd()` then reads as a node that is not the root, i.e. as
    /// a full slot.
    auto const pSlotNode(chain.Engine::ModuleChainImpl::module(slot));
    auto const slotFull(!chain.isEnd(pSlotNode));

    /// \note Taken *before* the relink and handed straight back out: unlinking
    /// drops the chain's reference, and with the interface holding none -- a
    /// module the host swapped out with the window shut -- that reference is the
    /// last one, so the unlink itself would call the deleter. Which is a free(),
    /// inside process().
    SW::Module *const pOutgoing(slotFull ? &Engine::actualModule<SW::Module>(*pSlotNode) : nullptr);
    if (pOutgoing)
        intrusive_ptr_add_ref(&Engine::node(*pOutgoing));

    if (pModule)
        chain.insertAtAndReplace(pSlotNode, Engine::node(*pModule));
    else if (slotFull)
        chain.remove(*pSlotNode);

    /// \note And the reference the caller transferred in is the chain's now, so
    /// the one the factory handed over is released here rather than leaked.
    if (pModule)
        intrusive_ptr_release(&Engine::node(*pModule));

    return pOutgoing;
}

void SpectrumWorxCore::swapModuleChain(AutomatedModuleChain &chain)
{
    LE_ASSERT(currentThreadMayMutateEngineState());
    moduleChain().swap(chain);
}

////////////////////////////////////////////////////////////////////////////////
// The spectral setup
////////////////////////////////////////////////////////////////////////////////

bool SpectrumWorxCore::applyPendingSpectralSetup()
{
    LE_ASSERT_MSG(!engineIsRunning(), "The spectral setup may only be applied with audio stopped.");
    /// \note Cleared before the work rather than after it, and with an exchange
    /// rather than a store: this is the only consumer, but it runs with the
    /// engine stopped and the pair reads as the test-and-clear it is.
    if (!spectralSetupPending_.exchange(false, std::memory_order_acq_rel))
        return true;
    return updateEngineSetup();
}

/// \brief Applies the spectral setup now, or records that it has to wait.
///
/// \note The one decision the three spectral setters share. `updateEngineSetup()`
/// reallocates the shared storage and resizes every module in the chain, so it
/// may not run while a block might be in flight -- and there is no lock left to
/// make it wait. Nothing is processing exactly when `engineIsRunning()` is false,
/// which is when the main thread owns the engine and may simply do it.
///                                           (02.08.2026.) (SW port)
bool SpectrumWorxCore::deferOrApplySpectralSetup()
{
    /// \note Release: the parameter this is recording was written immediately
    /// before, and whoever sees the flag has to see the value too.
    spectralSetupPending_.store(true, std::memory_order_release);
    return engineIsRunning() ? true : applyPendingSpectralSetup();
}

bool SpectrumWorxCore::setGlobalParameter(FFTSize &parameter, FFTSize::param_type const newValue)
{
    parameter.setValue(newValue);
    return deferOrApplySpectralSetup();
}

bool SpectrumWorxCore::setGlobalParameter(OverlapFactor &parameter,
                                          OverlapFactor::param_type const newValue)
{
    parameter.setValue(newValue);
    return deferOrApplySpectralSetup();
}

/// \note Through the same deferral as the other two, where it used to recompute
/// the window tables on the spot. It allocates nothing, so it looked harmless --
/// but it rewrites the analysis and synthesis windows the WOLA path is reading,
/// so a block spanning the write got half of each. `updateEngineSetup()` applies
/// it: `resize()` takes the window function as an argument and calls
/// `changeWindowFunction()` itself when nothing else moved.
///                                           (02.08.2026.) (SW port)
bool SpectrumWorxCore::setGlobalParameter(WindowFunction &parameter,
                                          WindowFunction::param_type const newValue)
{
    parameter.setValue(newValue);
    return deferOrApplySpectralSetup();
}

} // namespace SW

#ifndef NDEBUG
LE_WEAK_SYMBOL extern char const assertionFailureMessageTitle[] =
    "SpectrumWorx internal error. Press Ok to ignore or Cancel to abort...";
#endif // NDEBUG

} // namespace LE
