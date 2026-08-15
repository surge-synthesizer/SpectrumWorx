////////////////////////////////////////////////////////////////////////////////
///
/// \file spectrumWorxCLAP.cpp
/// -------------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "spectrumWorxCLAP.hpp"
#include "gui/editor/spectrumWorxEditor.hpp"
#include "gui/editor/zoomedEditor.hpp"

#include "core/modules/factory.hpp"
// \note Order matters and is not alphabetical: finalImplementations.hpp defines
// Module::Impl<> and needs Module complete. factory.cpp includes them in this
// order for the same reason.
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/modules/finalImplementations.hpp"

// \note The interop templates are defined in .inl files that only their
// instantiating translation unit includes -- this one. They call through to a
// module's UI on a slot change, so the complete type is needed.
#include "gui/modules/moduleUI.hpp"

#include "core/host_interop/clapParameterEdge.hpp"
#include "core/host_interop/host2PluginImpl.inl"
#include "core/host_interop/plugin2HostImpl.inl"
#include "core/host_interop/programWrite.hpp"
#include "core/threading/publish.hpp"

#include "core/threading/threadCheck.hpp"

#include "gui/gui.hpp" // warningMessageBox()

// The state format: GUI::loadPreset() takes the editor as a pointer precisely so
// that this can call it with none, and savePreset() is the writer at the far end
// of it. See doc/tech/streaming_format.md.
#include "gui/editor/presetLoading.hpp"
#include "io/jucePath.hpp"
#include "le/spectrumworx/presetStorage.hpp"
#include "le/spectrumworx/presetStorage.hpp" // maximumPresetSize

#include "le/math/vector.hpp" // Math::copy(), for the sample's wrap

#include <sst/clap_juce_shim/menu_helper.h> // the host's own parameter menu
#include <sst/plugininfra/cpufeatures.h>
#include <sst/plugininfra/version_information.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <optional>

namespace LE::SW
{
namespace
{
constexpr clap_id mainInputPort{0};
constexpr clap_id sideChainInputPort{1};
constexpr clap_id mainOutputPort{2};

/// \brief Assigns a parameter its own-units value, with no automation edge in
/// between. \see ToEngine::SetUnexportedLFOParameter, which is its only user:
/// what travels there is what the interface already stored, not an automation
/// value, because the parameter it names is not automatable.
struct RawParameterSetter
{
    using result_type = void;

    float value;

    template <class Parameter> result_type operator()(Parameter &parameter) const
    {
        parameter.setValue(static_cast<typename Parameter::value_type>(value));
    }
}; // struct RawParameterSetter

/// \note `stateMagic`, the four bytes `SWX1`, stood here in front of a
/// `(uint32 id, double value)` array. Dropped with the blob it introduced rather
/// than kept as a fallback: nothing has shipped, so the only sessions holding
/// one are development sessions in this tree, and a permanent second reader for
/// a format no user has is dead weight from the day it is written. A stream that
/// does not begin with `<` fails the parse, which is how one is refused.
///                                           (02.08.2026.) (SW port)

bool writeFully(clap_ostream const *const stream, void const *const data, std::size_t size)
{
    auto const *cursor(static_cast<char const *>(data));
    while (size > 0)
    {
        auto const written(stream->write(stream, cursor, size));
        if (written <= 0)
            return false;
        cursor += written;
        size -= static_cast<std::size_t>(written);
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Reads \p stream to its end into a NUL-terminated, writable buffer.
///
/// \note The whole stream before any of it is parsed, because the document is
/// not self-delimiting: a host may hand back any number of bytes and the reader
/// has to see all of them. `readFully` into a fixed size, which is what the
/// binary blob used, cannot express that.
///
/// \note A `read` of 0 is the end and a negative is an error, and the two are
/// not the same answer -- a truncated read that reported failure would be
/// indistinguishable from an empty state. The buffer grows geometrically; the
/// shape is `sst::plugininfra::patch_support::inStreamToPatch`, which does the
/// same job for the other Surge Synth Team plugins.
///
////////////////////////////////////////////////////////////////////////////////

std::optional<std::vector<char>> readWholeStream(clap_istream const *const stream)
{
    constexpr std::size_t chunk{1u << 12};

    std::vector<char> buffer;
    std::size_t used(0);
    for (;;)
    {
        /// \note Bounded, because the loop's only exit was the stream saying it
        /// was done. A host that hands over a stream which never does -- a
        /// corrupt project, a pipe nothing closes -- grew this until the
        /// allocation threw, and `stateLoad` is `noexcept`. The number is the
        /// preset reader's, for the reason given where it is declared.
        ///                                   (08.08.2026.) (SW port)
        if (used >= maximumPresetSize)
            return std::nullopt;

        buffer.resize(used + chunk);
        auto const read(stream->read(stream, buffer.data() + used, chunk));
        if (read < 0)
            return std::nullopt;
        if (read == 0)
            break;
        used += static_cast<std::size_t>(read);
    }

    buffer.resize(used + 1);
    buffer[used] = '\0'; // the parse is destructive and wants a terminator
    return buffer;
}

////////////////////////////////////////////////////////////////////////////////
//
// sampleChunk()
// -------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief One block of a looped sample channel, advancing \p position past it.
///
/// \return the sample's own data where a whole block is contiguously available,
/// which is every block but the one that wraps; \p workBuffer, filled, where it
/// is not. So the common case costs nothing and only the wrap copies.
///
/// \note LE::SW::getChannelDataChunk in the 2016 plugin class, moved here with
/// its shape intact -- it is the whole of what feeding the engine from a file
/// amounts to.
///
////////////////////////////////////////////////////////////////////////////////

float const *sampleChunk(Sample::ChannelData const &channelData, std::uint32_t &position,
                         std::uint32_t chunkSize, float *LE_RESTRICT const workBuffer)
{
    auto const dataSize(static_cast<std::uint32_t>(channelData.size()));
    LE_ASSERT(position <= dataSize);
    if (dataSize > (position + chunkSize))
    {
        auto const *const pChunk(&channelData[position]);
        position += chunkSize;
        return pChunk;
    }

    auto *workBufferPosition(workBuffer);
    while (chunkSize)
    {
        if (position == dataSize)
            position = 0;
        auto const amountToCopy(std::min<std::uint32_t>(dataSize - position, chunkSize));
        Math::copy(&channelData[position], workBufferPosition, amountToCopy);
        workBufferPosition += amountToCopy;
        position += amountToCopy;
        chunkSize -= amountToCopy;
    }
    return workBuffer;
}
} // namespace

/// \note Every string here comes from the build (src/CMakeLists.txt), because
/// the bundle identifiers are made of the same ones and a second copy is how
/// they drift. SW_CLAP_ID in particular is the plugin's identity in three
/// places at once: the CLAP id, the `.clap`/`.vst3`/`.component` bundle
/// identifiers, and -- by way of a SHA-1 in clap-wrapper -- the VST3 class id.
///                                           (01.08.2026.) (SW port)
clap_plugin_descriptor const *descriptor()
{
    static char const *features[]{CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO,
                                  "spectral", nullptr};
    static clap_plugin_descriptor const description{
        CLAP_VERSION,
        SW_CLAP_ID,
        PRODUCT_NAME,
        SW_VENDOR,
        SW_VENDOR_URL,
        "",
        "",
        sst::plugininfra::VersionInformation::project_version_and_hash,
        "Modular spectral effects",
        &features[0]};
    return &description;
}

clap_plugin const *createPlugin(clap_host const *const host)
{
    return (new SpectrumWorxCLAP(host))->clapPlugin();
}

SpectrumWorxCLAP::SpectrumWorxCLAP(clap_host const *const host) : PluginHelper(descriptor(), host)
{
    setProgram(program_);
    parameterIDs_.reserve(ParameterCounts::maxNumberOfParameters);

    clapJuceShim_ = std::make_unique<sst::clap_juce_shim::ClapJuceShim>(this);
    clapJuceShim_->setResizable(false);
}

SpectrumWorxCLAP::~SpectrumWorxCLAP()
{
    /// \note And anything the main thread asked for that no block will now
    /// carry out. `deactivate()` empties this queue, but an editor open on a
    /// deactivated plugin goes on filling it -- a knob moved with the transport
    /// stopped -- and a plugin that was created and destroyed without ever being
    /// activated never saw a `deactivate()` at all. Every `SetSlot`, `SwapChain`
    /// and `SwapSample` still in there owns what it carries.
    ///                                       (08.08.2026.) (SW port)
    discardQueuedCommands();

    /// \note Anything the audio thread handed back and nobody collected. There
    /// is no audio thread by now -- a host destroys a deactivated plugin -- so
    /// this is the last chance to free it.
    drainEngineEvents();
    delete pSample_;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Frees what the command queue still carries, without applying any of
/// it. `[main-thread]`
///
/// \note Discarded rather than drained, which is the difference between this and
/// `drainCommands()`. Applying a slot change to an engine that is being destroyed
/// buys nothing, and the two things applying it *does* -- `chainChanged()`'s
/// rescan request and its `mark_dirty` -- are calls into a host that is midway
/// through `clap_plugin::destroy`. What is owed here is the memory and only that.
///                                           (08.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCLAP::discardQueuedCommands()
{
    Threading::ToEngine command;
    while (toEngine_.pop(command))
    {
        switch (command.kind)
        {
        /// \note One reference, transferred with the message; null empties a slot
        /// and carries nothing.
        case Threading::ToEngine::Kind::SetSlot:
            if (command.setSlot.pModule)
                intrusive_ptr_release(
                    &Engine::node(*static_cast<Module *>(command.setSlot.pModule)));
            break;

        case Threading::ToEngine::Kind::SwapChain:
            delete static_cast<AutomatedModuleChain *>(command.swapChain.pChain);
            break;

        case Threading::ToEngine::Kind::SwapSample:
            delete static_cast<Sample *>(command.swapSample.pSample);
            break;

        // Values, and nothing to free.
        case Threading::ToEngine::Kind::None:
        case Threading::ToEngine::Kind::SetBaseParameter:
        case Threading::ToEngine::Kind::MoveModule:
        case Threading::ToEngine::Kind::SetUnexportedLFOParameter:
            break;
        }
    }
}

bool SpectrumWorxCLAP::init() noexcept
{
    /// \note `clap_plugin::init` is `[main-thread]` by contract, so the thread
    /// running it is the answer for the life of the plugin -- and it is the only
    /// answer available, `clap.thread-check` being an optional extension a host
    /// need not offer. See core/threading/threadCheck.hpp.
    Threading::markMainThread();

    // The host may ask for the parameter list before activate(), and does.
    rebuildParameterIDs();
    return true;
}

bool SpectrumWorxCLAP::activate(double const sampleRate, std::uint32_t,
                                std::uint32_t const maxFrames) noexcept
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note A host that activates an already-active plugin is misbehaving, and
    /// clap-helpers catches only half of it. At this build's checking level it
    /// says so, and when the sample rate *differs* it simulates a deactivation
    /// first -- but when the rate is the same it says so and calls this anyway.
    /// The `assert( !_isActive )` standing between the two is `assert`, so in
    /// every shipped build there is nothing there at all (plugin.hxx:349-401).
    ///
    ///   `initialise()` below reallocates the entire spectral working set, and
    /// the audio thread may be inside `process()` reading it at that moment:
    /// `start_processing` was called for the first activation and nothing has
    /// cancelled it. That is a use-after-free of the shared storage, off the one
    /// entry point a host is most likely to get wrong.
    ///
    ///   Answering "yes, active" is not a lie, and it is the only answer that
    /// does not free something out from under a live callback. The rate asked for
    /// here is the rate already running -- clap-helpers has dealt with the case
    /// where it is not, and that path arrives with `engineRunning_` already false.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    if (engineRunning_)
        return true;

    /// \note Whatever the audio thread handed back and nobody has collected yet.
    /// A host that restarts the plugin need not call `on_main_thread` in between,
    /// and every one of these is a live allocation.
    drainEngineEvents();

    sampleRate_ = sampleRate;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Stereo main *and* stereo side, which is what four inputs against
    /// two outputs spells: the arguments are the **input** and **output** counts
    /// and the engine takes the side channels to be the difference
    /// (`SpectrumWorxCore::setNumberOfChannels`). Anything else waits for 5.7
    /// and the input-mode parameter; the engine supports far more, the port list
    /// above does not.
    ///
    /// \note This read `setNumberOfChannels( 2, 2 )` until 05.08.2026 -- two in,
    /// two out, and therefore **no side channels at all**. Nothing noticed,
    /// because an effect reads whichever pointer `process()` was handed and that
    /// comes from the host's port rather than from these buffers. What did not
    /// work was the one thing that needs them: `runEngine()` guards the external
    /// audio file on `buffers().numberOfSideChannels() >= channels`, which was
    /// `0 >= 2`, so **a loaded sample never reached the DSP in any format**.
    /// `setNewSample()`'s own note already said activate() "asks for two main
    /// and two side channels outright"; it now does.
    ///                                       (05.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    setNumberOfChannels(4, 2);
    setSampleRate(static_cast<float>(sampleRate));

    // The host promises never to exceed maxFrames, and SpectrumWorxCore asserts
    // exactly that against its own buffers. A shorter final block is fine.
    setBlockSize(maxFrames);

    if (!initialise())
    {
        /// \note Whatever the engine rolled back to is what the rest of the
        /// world has to be told. \see resyncSpectralParametersToEngine().
        resyncSpectralParametersToEngine();
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note A sample is decoded to the engine's rate, and a session can be
    /// restored -- sample and all -- before the host has said what that rate is.
    /// Re-read it here when they disagree; the 2016 build did not, and played
    /// the sample at the wrong pitch for the rest of the session.
    ///
    /// \note Before resume(), so that the swap is the direct one rather than a
    /// command waiting for a block that has not been asked for yet.
    ///                                       (01.08.2026.) (SW port)
    ///
    /// \note This read `(decodedSampleRate_ != 0) && (decodedSampleRate_ !=
    /// rate)`, which is dead in exactly the case it was written for. Zero is not
    /// "no sample": it is what `Sample::load` records when it was given no rate
    /// to resample to, which is precisely the construct -> stateLoad -> activate
    /// order every host restores a session in. So the guard skipped the one
    /// arrival that needs it, and the 2016 bug it was added to fix was still
    /// there -- a restored session played its sample at the file's rate for the
    /// life of the instance, while a sample loaded from the *menu* was fine.
    ///
    ///   What decides it is whether a sample is loaded at all, and then whether
    /// what it was decoded for is what the engine now runs at. Zero answers that
    /// question the same way any other mismatching rate does.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    if (!sampleFile_.empty() && (decodedSampleRate_ != static_cast<unsigned int>(sampleRate)))
    {
        /// \note No dialog on a failure here, unlike the menu's load. Nothing
        /// asked for this -- the host is opening a session -- and there is no
        /// user standing in front of it to answer one; a modal box in `activate`
        /// stops the host mid-restore. What is already loaded stays loaded, at
        /// the wrong rate, which is what happened before this ran at all.
        /// \see issue #12, "A load problem has nowhere to go but a modal box".
        [[maybe_unused]] auto const *const pErrorMessage(decodeAndPublishSample(sampleFile_));
        LE_ASSERT_MSG(!pErrorMessage, "A sample that loaded once did not load again.");
    }

    resume();
    engineRunning_ = true;
    latencyInSamples_ = engineSetup().latencyInSamples();

    /// \note An editor that opened before this point built its module knobs
    /// against an engine with no sample rate, so the ranges that quantise to a
    /// step time or a bin width could not be derived and were left alone. Now
    /// they can be. Nothing else re-ranges them -- the editor's own
    /// updateForEngineSetupChanges() was wired only to the four settings
    /// combo boxes -- and restoring a session before activate() is exactly the
    /// order a standalone starts in.
    ///                                       (29.07.2026.) (SW port)
    if (pEditor_)
        pEditor_->updateForEngineSetupChanges();

    return true;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Where a pending spectral setup lands, and the only place it may. FFT
/// size, overlap factor and window function reallocate the whole spectral
/// working set and resize every module; with the plugin deactivated there is no
/// audio thread to race and no lock needed to say so. `request_restart` is what
/// brings us here -- see drainCommands() and §5.
///
///   It is also the one point at which `clap_plugin_latency` allows the latency
/// to change, and the FFT size *is* the latency.
///                                           (02.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCLAP::deactivate() noexcept
{
    if (engineRunning_)
    {
        suspend();
        engineRunning_ = false;
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Whatever the main thread asked for and no block ever came to carry
    /// out. `suspend()` is above it, so this thread owns the engine and the
    /// commands apply here and now, exactly as `process()` would have applied
    /// them.
    ///
    ///   Both halves of this matter, and the first is a heap overrun. A preset
    /// that changes the FFT size builds its chain against the storage factors in
    /// force *when it is parsed* and hands it over through `publishChain()`,
    /// which with audio running queues it. The restart it then asks for used to
    /// arrive here and resize only the chain that was already live -- the
    /// preset's was still sitting in the ring -- so the first `process()` after
    /// the restart spliced in modules sized for the old FFT and nothing ever
    /// resized them. A larger FFT then writes per-bin channel state past the end
    /// of its block. Draining before `applyPendingSpectralSetup()` is what puts
    /// the new chain where the resize can see it.
    ///
    ///   The second is quieter: a command left in the ring is not merely late,
    /// it is *stale*. `activate()` drained nothing either, so a queued SetSlot or
    /// SwapChain replayed on top of whatever the main thread had since applied
    /// directly -- the two Program copies disagreeing with no way to notice.
    ///
    ///   A host is not obliged to call `on_main_thread` between deactivate and
    /// activate, which is why the collection below is here as well.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    drainCommands();
    drainEngineEvents();

    /// \note Before the setup is applied rather than after: this is the restart
    /// that was asked for, so anything asking again from here on is asking about
    /// a change made after it.
    restartRequested_.store(false, std::memory_order_release);
    if (spectralSetupPending())
    {
        /// \note And what it answers, which was dropped. `updateEngineSetup()`
        /// puts the FFT size and the overlap factor back when the working set
        /// cannot be allocated -- in the *engine's* Program, the only one it can
        /// see. \see resyncSpectralParametersToEngine().
        if (!applyPendingSpectralSetup())
            resyncSpectralParametersToEngine();

        auto const newLatency(engineSetup().latencyInSamples());
        if (newLatency != latencyInSamples_)
        {
            latencyInSamples_ = newLatency;
            if (_host.canUseLatency())
                _host.latencyChanged();
        }
        if (pEditor_)
            pEditor_->updateForEngineSetupChanges();
    }

    sampleRate_ = 0;
}

/// \note `reset()` is `[audio-thread & active]` (plugin.h:89) -- *not* under
/// `process()`, which is the whole point of it: a host calls it between blocks to
/// throw away the tail. So it owns the engine while it runs and has to say so,
/// exactly as `process()` does.
///
///   Nothing said so until 03.08.2026, and nothing noticed because no host in
/// this tree had ever called it: `vst3-validator` was the first, through
/// `ClapAsVst3::setProcessing(false)`, which does `stop_processing()` then
/// `reset()` off Steinberg's own call-sequence diagram. The wrapper is correct
/// and the plugin asserted anyway -- `resetChannelBuffers()` checks
/// `currentThreadMayMutateEngineState()`, which is `!engineIsRunning() ||
/// Threading::isAudioThread()`, and the second half was false because only
/// `process()` ever opened the scope.
///                                           (03.08.2026.) (SW port)
void SpectrumWorxCLAP::reset() noexcept
{
    Threading::ScopedAudioThreadEntry const audioThread;

    if (engineRunning_)
        SpectrumWorxCore::reset();
}

////////////////////////////////////////////////////////////////////////////////
// Audio ports
////////////////////////////////////////////////////////////////////////////////

std::uint32_t SpectrumWorxCLAP::audioPortsCount(bool const isInput) const noexcept
{
    return isInput ? 2 : 1;
}

bool SpectrumWorxCLAP::audioPortsInfo(std::uint32_t const index, bool const isInput,
                                      clap_audio_port_info *const info) const noexcept
{
    std::memset(info, 0, sizeof(*info));
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    /// \note Deliberately never an in-place pair. With an input gain of exactly
    /// one SpectrumWorxCore hands the host's own input pointers straight to
    /// Engine::Processor::process, and the WOLA path has not been audited for
    /// aliasing input and output. Revisit under 5.7 with a test, not by
    /// inspection.
    info->in_place_pair = CLAP_INVALID_ID;

    if (isInput && index == 0)
    {
        info->id = mainInputPort;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        std::strncpy(info->name, "Main In", CLAP_NAME_SIZE - 1);
        return true;
    }
    if (isInput && index == 1)
    {
        info->id = sideChainInputPort;
        info->flags = 0;
        std::strncpy(info->name, "Side Chain", CLAP_NAME_SIZE - 1);
        return true;
    }
    if (!isInput && index == 0)
    {
        info->id = mainOutputPort;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        std::strncpy(info->name, "Main Out", CLAP_NAME_SIZE - 1);
        return true;
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////
// Parameters
////////////////////////////////////////////////////////////////////////////////

/// \brief Every parameter the engine can ever have, once, at init().
///
/// \note Passing nullptr rather than the Program is the whole point: it asks
/// for the *maximal* list -- every slot's full complement of module and LFO
/// parameters -- instead of the list the current program happens to have.
///
///   The list has to be maximal because CLAP does not let a plugin change its
/// parameter count while it is active. ext/params.h is explicit: adding or
/// removing parameters means calling clap_host->restart() and waiting for
/// deactivate() before CLAP_PARAM_RESCAN_ALL. Doing it from process() or
/// flush() -- which is what following the Program does, since a host can swap a
/// slot's effect with an event mid-block -- is not something a host has to cope
/// with, and the ones that do not simply keep the count they first read. That
/// is why an empty session showed eleven parameters: six globals and five slot
/// selectors, with nothing for any module because no slot held an effect yet.
///
///   Nothing is lost by declaring them all. A parameter belonging to a slot
/// whose effect does not have it reads as N/A rather than as an unknown ID, so
/// a host's automation lane stays attached across an effect swap -- which is
/// the behaviour isValidParamId() was already written for.
void SpectrumWorxCLAP::rebuildParameterIDs()
{
    parameterIDs_.resize(numberOfParameters(nullptr));
    getParameterIDs({parameterIDs_.data(), parameterIDs_.size()}, nullptr);
    LE_ASSERT(parameterIDs_.size() == ParameterCounts::maxNumberOfParameters);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Every field, not just the discriminator, and this is the only place it
/// happens. All four host entry points that take a raw `clap_id` come through
/// here -- paramsValue, paramsValueToText and paramsTextToValue on the main
/// thread, handleEvent on the audio thread -- and everything downstream is
/// written on the assumption that they did: the indices reach
/// `invokeFunctorOnIndexedParameter`, whose jump tables are `cases[index]`
/// guarded by nothing stronger than LE_ASSUME. That is a `__builtin_assume` in a
/// release build, so an index one past the end is an out-of-bounds read followed
/// by an indirect call through whatever it found -- on the audio thread, for the
/// event route.
///
///   A `clap_id` is host-supplied data. A stale automation lane in an old
/// project, a host rescanning against a parameter list that has moved, or a
/// validator sweeping the id space all deliver one that decodes to nothing.
///
/// \note The padding bytes are checked for the types that have them, so that two
/// different `clap_id`s cannot name one parameter. `parameterIDFromIndex()`
/// zero-initialises, so nothing the plugin itself advertises is refused by this
/// -- `hostileParameterIDTests.cpp` holds both halves.
///
/// \note Every ParameterID that *decodes* is still valid: the model answers
/// "N/A" for a slot whose effect does not have that parameter rather than
/// pretending the id is unknown, which is what keeps a host's automation lane
/// attached across an effect swap.
///                                           (08.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

bool SpectrumWorxCLAP::isValidParamId(clap_id const id) const noexcept
{
    ParameterID const parameterID{Plugins::ParameterID{id}};

    auto const isPadding([](ParameterID::Padding const byte) { return byte == ParameterID::Zero; });

    switch (parameterID.type())
    {
    case ParameterID::GlobalParameter:
    {
        auto const &global(parameterID.value._.global);
        return (global.index < GlobalParameters::Parameters::static_size) &&
               isPadding(global.padding0) && isPadding(global.padding1);
    }

    case ParameterID::ModuleChainParameter:
    {
        auto const &moduleChain(parameterID.value._.moduleChain);
        return (moduleChain.moduleIndex < Constants::maxNumberOfModules) &&
               isPadding(moduleChain.padding0) && isPadding(moduleChain.padding1);
    }

    case ParameterID::ModuleParameter:
    {
        auto const &module(parameterID.value._.module);
        return (module.moduleIndex < Constants::maxNumberOfModules) &&
               (module.moduleParameterIndex < Constants::maxNumberOfParametersPerModule) &&
               isPadding(module.padding0);
    }

    case ParameterID::LFOParameter:
    {
        /// \note One fewer than a module has parameters: the first is Bypass and
        /// no LFO drives it. \see parameterIDFromIndex().
        auto const &lfo(parameterID.value._.lfo);
        return (lfo.moduleIndex < Constants::maxNumberOfModules) &&
               (lfo.moduleParameterIndex < Constants::maxNumberOfParametersPerModule - 1) &&
               (lfo.lfoParameterIndex < ParameterCounts::lfoExportedParameters);
    }
    }

    /// \note No LE_DEFAULT_CASE_UNREACHABLE() here, which is the whole point:
    /// the discriminator is a byte off the wire and four of its 256 values are
    /// parameters. Telling the optimiser the other 252 cannot happen is what
    /// this function exists to stop.
    return false;
}

std::uint32_t SpectrumWorxCLAP::paramsCount() const noexcept
{
    /// \note The list built at init(), not numberOfParameters(&program()). See
    /// rebuildParameterIDs(): this count is fixed for the plugin's lifetime.
    return static_cast<std::uint32_t>(parameterIDs_.size());
}

bool SpectrumWorxCLAP::paramsInfo(std::uint32_t const index,
                                  clap_param_info *const info) const noexcept
{
    if (index >= parameterIDs_.size())
        return false;

    auto const id(parameterIDs_[index]);
    ParameterID const parameterID{id};

    /// \note Two queries, and which one answers what is the whole contract.
    ///
    ///   The *fixed* description -- the maximal one, over a null Program, as in
    /// rebuildParameterIDs -- supplies every number and every flag a host may
    /// not see move: min_value, max_value, is_stepped. ext/params.h lists those
    /// three together under CLAP_PARAM_RESCAN_ALL, which is legal only while
    /// deactivated, and a slot's effect changes mid-block.
    ///
    ///   The *live* one supplies only what RESCAN_INFO explicitly covers: the
    /// name, the module path, and whether the parameter is currently used at
    /// all. Those may follow whichever effect the slot holds.
    Plugins::ParameterInformation<Protocol> fixed;
    getParameterRanges(parameterID, fixed, nullptr);

    Plugins::ParameterInformation<Protocol> live;
    getParameterProperties(parameterID, live, &programMain_);

    std::memset(info, 0, sizeof(*info));
    info->id = id.value;
    info->cookie = nullptr;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note **Nothing here is ever CLAP_PARAM_IS_HIDDEN.** Every parameter in
    /// the list is shown, whether or not a slot's effect currently owns it.
    ///
    ///   It used to be flagged hidden while unowned -- "not shown, because it is
    /// currently not used" -- which is what the flag means and would have been
    /// right if hosts re-read it. The shipped clap-wrapper maps flags once, at
    /// construction, and a VST3 RESCAN_INFO re-reads only the name; flags come
    /// back only under RESCAN_ALL, which a plugin may not send while active. So
    /// in every VST3 host the flags captured on an *empty* instance were the
    /// flags forever: an automation list of eleven rows -- six globals and five
    /// slot selectors -- out of 286, for the life of the instance, with no way to
    /// automate anything a user then loaded.
    ///
    ///   Not worked around, dropped. A flag whose whole value is that it changes
    /// is not worth having when a format this ships in cannot see it change, and
    /// the failure it causes is the worst kind: a lane a user cannot find at all.
    /// The cost of showing everything is a long list on an empty instance, which
    /// a user can read past; the cost of hiding was a parameter that did not
    /// exist as far as their DAW was concerned. An unowned parameter is still
    /// perfectly answerable -- `paramsValueToText` reads it as `N/A` and writing
    /// to it is refused -- so what a host has is a row that does nothing yet,
    /// rather than a row that is missing.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note **And nothing is ever CLAP_PARAM_REQUIRES_PROCESS either.** A slot
    /// selector used to carry it, on the reading that the flag says "this one
    /// needs a rescan afterwards". It does not. It says "any change to this
    /// parameter affects the output and must be done via `process()` if the
    /// plugin is active" (ext/params.h:196) -- a DC offset is the example given.
    /// It is a statement about *which call the host may use*, and the answer it
    /// gives is the wrong one here: it forbids the route a slot change most
    /// needs.
    ///
    ///   `paramsFlush()` applies a slot selector properly -- it drains the
    /// command queue and runs `handleEvent()`, the same two things `process()`
    /// does, on the same thread with the same ownership -- so there is nothing
    /// this flag protects. What it cost is the host with the transport parked,
    /// which has no `process()` to offer and is exactly the host that reaches
    /// for `flush()`.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    if (CLAPEdge::isNormalised(parameterID))
    {
        /// \note The 0..1 edge over a natural range that belongs to the effect.
        /// No CLAP_PARAM_IS_STEPPED either, for the same reason there is no real
        /// range: a step count is a property of the effect in the slot, and the
        /// flag is in the same RESCAN_ALL list. The enumerated ones still read
        /// as their names through paramsValueToText.
        info->min_value = 0;
        info->max_value = 1;
        info->default_value = CLAPEdge::defaultToHost(parameterID, fixed);
    }
    else
    {
        // Global and slot-selector parameters: ranges the plugin owns, and which
        // therefore never move. They keep their real values and their steps.
        info->min_value = fixed.minimum();
        info->max_value = fixed.maximum();
        info->default_value = fixed.default_();
        if (fixed.isStepped())
            info->flags |= CLAP_PARAM_IS_STEPPED;
    }

    std::strncpy(info->name, live.name(), CLAP_NAME_SIZE - 1);
    modulePathFor(parameterID, info->module);
    return true;
}

void SpectrumWorxCLAP::modulePathFor(ParameterID const parameterID,
                                     char (&path)[CLAP_PATH_SIZE]) const noexcept
{
    switch (parameterID.type())
    {
    case ParameterID::GlobalParameter:
        std::strncpy(path, "Global", CLAP_PATH_SIZE - 1);
        break;
    case ParameterID::ModuleChainParameter:
        std::snprintf(path, CLAP_PATH_SIZE, "Slot %u",
                      parameterID.value._.moduleChain.moduleIndex + 1u);
        break;
    case ParameterID::ModuleParameter:
        std::snprintf(path, CLAP_PATH_SIZE, "Slot %u", parameterID.value._.module.moduleIndex + 1u);
        break;
    case ParameterID::LFOParameter:
        std::snprintf(path, CLAP_PATH_SIZE, "Slot %u/LFO",
                      parameterID.value._.lfo.moduleIndex + 1u);
        break;
    }
}

/// \note The *live* range, not the fixed one paramsInfo advertises: normalising
/// is exactly the act of expressing a value that belongs to the effect currently
/// in the slot on an edge that does not.
///
///   A local rather than a member, at each of the four call sites below, because
/// they run on three different threads -- the host's main thread, the audio
/// thread and the UI thread -- and one shared scratch description between them
/// would be a race. It costs a clear() and a dispatch; neither allocates nor
/// formats a string, which is what getParameterRanges() is for.
///
/// \note And the Program comes from the caller for the same reason the scratch
/// does: the two main-thread callers and the one under `process()` are reading
/// two different copies of it, each on the thread that owns it.
bool SpectrumWorxCLAP::liveRanges(ParameterID const parameterID,
                                  Plugins::ParameterInformation<Protocol> &ranges,
                                  Program const &program)
{
    getParameterRanges(parameterID, ranges, &program);
    if (CLAPEdge::isPresent(ranges))
        return true;

    /// \note An empty slot has no range at all -- the model spells that as a
    /// degenerate 0..0 -- so fall back to the maximal description, which is also
    /// the one paramsInfo advertised. Nothing sensible can be normalised against
    /// 0..0, and a caller still needs a scale to work on.
    getParameterRanges(parameterID, ranges, nullptr);
    return false;
}

bool SpectrumWorxCLAP::paramsValue(clap_id const id, double *const value) noexcept
{
    if (!isValidParamId(id))
        return false;

    ParameterID const parameterID{Plugins::ParameterID{id}};
    Plugins::ParameterInformation<Protocol> ranges;

    /// \note A parameter no effect currently owns has no value of its own, and
    /// what the engine answers for one is not the default it was advertised with.
    /// It reads as that advertised default instead -- `ranges` is the maximal
    /// description by then, the same one paramsInfo used, so the two agree by
    /// construction. A host checks exactly this at init (param-default-values).
    if (!liveRanges(parameterID, ranges, programMain_))
    {
        *value = CLAPEdge::defaultToHost(parameterID, ranges);
        return true;
    }

    *value = CLAPEdge::toHost(parameterID, ranges, getParameter(parameterID, programMain_));
    return true;
}

bool SpectrumWorxCLAP::paramsValueToText(clap_id const id, double const value, char *const display,
                                         std::uint32_t const size) noexcept
{
    if (!isValidParamId(id))
        return false;

    ParameterID const parameterID{Plugins::ParameterID{id}};

    /// \note Answers about \p value, which is what CLAP asks for: the caller is
    /// usually an automation lane's tooltip asking "what would 0.25 read as"
    /// rather than "what does this read as now".
    ///
    ///   It rendered the parameter's own value and ignored the argument until
    /// 09.08.2026, and the reason was one line in the printer. Its arms for a
    /// supplied value default-constructed a `Parameter` to assign it to --
    /// `Parameter parameterValue;` -- and a detached parameter is not a valid
    /// one: construction runs `isValidValue`, and a dynamic range finds its
    /// limits by walking from its own address to the owner a temporary does not
    /// have (LFOImpl::snapPeriodScaleFromAutomation does that walk explicitly).
    /// So an ordinary what-if question asserted, in a throwaway object, having
    /// corrupted nothing -- and in a checked build an assertion ends the host,
    /// which is what a debug plugin did as soon as a rescan made a host read the
    /// list. Nothing needed the object; `print()` takes a value.
    ///
    /// \note `fromHost` rather than the raw double, so what the printer is given
    /// is a natural value on the same edge `paramsValue` answers on -- and
    /// clamped, because a host may ask about anything. \see CLAPEdge.

    /// \note A parameter no effect currently owns reads as `notAvailable`, which
    /// is the name paramsInfo gives it and the only true thing there is to say
    /// about its value: it has no effect to give it units and no range to be a
    /// point in. It used to be the empty string, which says the same thing less
    /// well and, once paramsTextToValue existed, could not be read back --
    /// clap-validator's `param-conversions` requires text_to_value for all the
    /// automatable parameters or for none, and every ID here is automatable so
    /// that a host's lane survives an effect swap.
    ///                                       (07.08.2026.) (SW port)
    Plugins::ParameterInformation<Protocol> ranges;
    if (!liveRanges(parameterID, ranges, programMain_))
    {
        std::snprintf(display, size, "%s", notAvailable);
        return true;
    }

    auto const natural(CLAPEdge::fromHost(parameterID, ranges, value));

    std::array<char, 128> text{};
    getParameterDisplay(parameterID, {text.data(), text.size()}, &natural, programMain_);

    std::array<char, 32> unit{};
    getParameterLabel(parameterID, {unit.data(), unit.size()}, &programMain_);

    std::snprintf(display, size, "%s%s", text.data(), unit.data());
    return true;
}

/// \brief paramsValueToText run backwards.
///
/// \note The 2016 code never needed this -- neither VST 2.4 nor AU asks a plugin
/// to parse a typed-in value -- so until 08.2026 nothing in the parameter system
/// inverted a display transform and this declined outright. What it had done
/// before *that* is why declining was the honest answer rather than a lazy one:
/// it ran `strtod` over the text and returned the result as if display units were
/// storage units, which clap-validator caught taking the input gain
/// `0.001` -> `"-60dB"` -> `-60.0` -> `"nandB"`, a NaN written straight into the
/// engine.
///
///   Both halves of that are now somebody's job.
/// `Parameters::DisplayValueTransformer::inverse` undoes the transform per
/// parameter, `Parameters::parse` answers `nothing` for text that is not a value
/// -- rather than the zero `strtod` answers -- and clamps what it does answer to
/// the parameter's own range. tests/clap/parameterTextTests.cpp holds every
/// parameter to the round trip.
///
/// \note `programMain_`, and the *live* ranges over it, because the units the
/// text is in belong to whichever effect the slot currently holds. Same pair of
/// reads as paramsValue().
bool SpectrumWorxCLAP::paramsTextToValue(clap_id const id, char const *const display,
                                         double *const value) noexcept
{
    if (!isValidParamId(id) || !display)
        return false;

    ParameterID const parameterID{Plugins::ParameterID{id}};

    Plugins::ParameterInformation<Protocol> ranges;

    /// \note A parameter no effect currently owns has one display and one value
    /// -- `notAvailable` and the default paramsValue answers with -- and reading
    /// the one back as the other is what keeps text_to_value answerable for
    /// *every* automatable parameter. \see paramsValueToText.
    if (!liveRanges(parameterID, ranges, programMain_))
    {
        if (std::strcmp(display, notAvailable) != 0)
            return false;
        *value = CLAPEdge::defaultToHost(parameterID, ranges);
        return true;
    }

    auto const natural(getParameterFromDisplay(parameterID, display, programMain_));
    if (!natural)
        return false;

    *value = CLAPEdge::toHost(parameterID, ranges, *natural);
    return true;
}

bool SpectrumWorxCLAP::handleEvent(clap_event_header const *const header)
{
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID)
        return false;
    if (header->type != CLAP_EVENT_PARAM_VALUE)
        return false;

    auto const *const event(reinterpret_cast<clap_event_param_value const *>(header));
    if (!isValidParamId(event->param_id))
        return false;

    ParameterID const parameterID{Plugins::ParameterID{event->param_id}};
    Plugins::ParameterInformation<Protocol> ranges;
    /// \note Dropped rather than stored when no effect in that slot owns it. The
    /// list is maximal (see rebuildParameterIDs), so a host can and does write to
    /// every ID in it, including a slot's tenth parameter while the slot holds a
    /// two-parameter effect. Reading one is safe --
    /// Automation::getAutomatedLFOParameter answers with the default -- but
    /// *writing* one is not: setAutomatedLFOParameter has no matching guard and
    /// indexes straight into module.lfo(), past the end. clap-validator's
    /// param-set-events and state-reproducibility tests both walk into it.
    ///
    ///   Dropping is also the right answer rather than merely the safe one: the
    /// value has nowhere to live, and filling the slot later brings the new
    /// effect's own default -- which is what paramsValue() reports for it in the
    /// meantime.
    ///                                       (29.07.2026.) (SW port)
    /// \note The engine's Program, this being `[audio-thread]` -- the three
    /// `[main-thread]` callers of liveRanges() read `programMain_`.
    if (!liveRanges(parameterID, ranges, program()))
        return false;

    auto const value(CLAPEdge::fromHost(parameterID, ranges, event->value));
    auto const applied(setParameter(parameterID, value));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And the main thread's copy of the same state. A module used to write
    /// a `juce::Slider` from inside the setter, on whatever thread the write
    /// arrived on -- which for a host automation event is this one. It is a
    /// message now, drained on the main thread.
    ///
    ///   Not gated on there being an editor, which it was while this was only a
    /// notification. It is how `programMain_` learns what the host did, and
    /// `paramsValue` and `stateSave` read that with the window shut.
    ///
    ///   Only when the engine took it, though: `setParameter` declines a slot
    /// selector that would leave a hole in the rack, and a copy that applied what
    /// the engine refused is a copy that disagrees with it.
    ///
    ///   `request_callback` is already asked for by `markCurrentProgramAsModified()`
    /// on this same path, so the drain happens without a second request.
    ///                                       (06.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    ///   A dropped echo leaves `programMain_` behind the engine for that
    /// parameter, permanently: this is the only thing that carries a host's write
    /// across, and `paramsValue` and `stateSave` answer from the copy that did
    /// not get it. \see pushed()
    if (applied == Plugins::ErrorCode<Protocol>::Success)
        pushed(toUI_.push(Threading::baseParameterChanged(parameterID.binaryValue, value)),
               "The echo queue is full; the main thread's Program is now behind the engine.");

    /// \note Only a module-chain parameter changes what the *other* parameters
    /// are: it decides which effect a slot holds, and so how many parameters
    /// that slot has and what they are called. Everything else is just a value.
    return parameterID.type() == ParameterID::ModuleChainParameter;
}

void SpectrumWorxCLAP::requestRescan(clap_param_rescan_flags const flags)
{
    // One callback per outstanding batch: a block that swaps every slot would
    // otherwise ask the host five times over.
    if (pendingRescan_.fetch_or(flags) == 0)
        _host.requestCallback();
}

void SpectrumWorxCLAP::paramsFlush(clap_input_events const *const in,
                                   clap_output_events const *const out) noexcept
{
    /// \note The conditional one. `clap_plugin_params::flush` is
    /// `[active ? audio-thread : main-thread]` (ext/params.h:303), and this is the
    /// only entry point whose owning thread depends on state rather than being
    /// fixed -- so the scope is taken on the same condition. Unconditionally would
    /// be wrong in the other direction: on an inactive plugin it would tell the
    /// engine the audio thread owns it while the main thread does, which is the
    /// mirror image of the bug this fixes.
    ///
    ///   What it drains and applies -- drainCommands(), handleEvent() -- is what
    /// process() does to the engine, so while active this genuinely is an audio
    /// callback and belongs under the same instrument, rtsan included.
    ///                                       (03.08.2026.) (SW port)
    std::optional<Threading::ScopedAudioThreadEntry> audioThread;
    if (isActive())
        audioThread.emplace();

    drainCommands();

    auto const size(in->size(in));
    bool effectChanged(false);
    for (std::uint32_t event(0); event < size; ++event)
        effectChanged |= handleEvent(in->get(in, event));

    if (effectChanged)
        chainChanged();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And on an *inactive* plugin, the echo those events just raised is
    /// drained here, because this is the main thread and nothing else is coming.
    /// While active the audio thread raises it and `onMainThread()` drains it,
    /// which is the ordinary path; while inactive there is no audio thread, no
    /// `process()` and -- for a host that restores a session and never opens a
    /// window -- no reason for the callback to run before `stateSave()` is asked
    /// for the state. The main thread's Program would then still be empty, and
    /// saving it would save nothing. `stateTests.cpp` walks exactly that.
    ///                                       (06.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    if (!isActive())
        drainEngineEvents();

    flushUIEdits(out);
}

////////////////////////////////////////////////////////////////////////////////
// Processing
////////////////////////////////////////////////////////////////////////////////

clap_process_status SpectrumWorxCLAP::process(clap_process const *const process) noexcept
{
    /// \note Stage 4.2, and the reason it is one line here rather than a fix to
    /// `Math::FPUDisableDenormalsGuard`: **nothing was flushing denormals at
    /// all**, on any platform. The engine's own two guards are inside
    /// `#ifdef LE_SW_SDK_BUILD`, which nothing defined, and the third is in
    /// `SpectrumWorx::process` — the 2016 host-facing class, which the CLAP does
    /// not call. The audio path is this function, `runEngine()` and
    /// `SpectrumWorxCore::process()`. So there was no working guard to rekey off
    /// the long-dead `BOOST_SIMD_HAS_SSE_SUPPORT`; there was no guard.
    ///
    ///   `FPUStateGuard` covers x86-64 (FTZ and DAZ via MXCSR) and aarch64 (FZ
    ///   via FPCR) and restores the caller's state on the way out, which a host
    ///   is entitled to expect. Here rather than in `runEngine()` because event
    ///   handling and `flushUIEdits()` convert parameter values, and because
    ///   the whole callback is the unit a host cares about. All four formats
    ///   funnel through here: clap-wrapper drives the VST3, AUv2 and standalone
    ///   off this same entry point.
    ///                                   (29.07.2026.) (SW port)
    sst::plugininfra::cpufeatures::FPUStateGuard const denormalGuard;

    /// \note Makes `Threading::isAudioThread()` true for everything below, and
    /// opens a RealtimeSanitizer realtime region so that an allocation, a lock or
    /// a syscall reached from anywhere under here is reported with a stack. Both
    /// compile away without `-fsanitize=realtime`. See cmake/sw-sanitizers.cmake.
    Threading::ScopedAudioThreadEntry const audioThread;

    /// \note Before the host's own events, so that a command the interface sent
    /// and an automation event for the same parameter resolve in the order a user
    /// would expect: the host's block wins, being newer than anything queued
    /// before the block started.
    drainCommands();

    bool effectChanged(false);
    if (auto const *const in = process->in_events)
    {
        auto const size(in->size(in));
        for (std::uint32_t event(0); event < size; ++event)
            effectChanged |= handleEvent(in->get(in, event));
    }

    /// \note Names, module paths and displayed values change; the parameter
    /// *list* does not, so this never needs CLAP_PARAM_RESCAN_ALL -- which
    /// would be illegal here, an active plugin having to go through
    /// clap_host->restart() first. See rebuildParameterIDs().
    if (effectChanged)
        chainChanged();

    if (process->out_events)
        flushUIEdits(process->out_events);

    updateLFOTiming(process);

    runEngine(process);

    /// \note After the block, once, rather than from inside the engine per LFO
    /// per module. See publishModulatedValues().
    publishModulatedValues();

    return CLAP_PROCESS_CONTINUE;
}

/// \brief Moves the LFO clock forward by one block.
///
/// \note Nothing did. Every LFO reads its phase off Engine::Processor's one
/// LFO::Timer, and the only code that ever moved that timer was
/// `SpectrumWorxSharedImpl::process()` -- the 2016 host-facing layer this class
/// stands in for and does not inherit (see the note on the class). So
/// `currentTimeInBars()` held 0 for the plugin's lifetime, and every symptom
/// followed from that one fact: each waveform returned its value at position 0
/// forever, so enabling an LFO pinned its target to one end of the range instead
/// of sweeping it; no period boundary was ever crossed, so the per-period
/// waveforms (RandomHold, RandomSlide, Dirac) never retriggered; and
/// `hasTempoInformation()` stayed false, which at the time greyed out the
/// editor's N/T/D sync buttons, printed the period in milliseconds rather than
/// note ratios, and defaulted every new LFO to Free. The first two no longer ask
/// -- there is always a tempo, the host's or an assumed 120 BPM 4/4 -- and the
/// third is the flag's last reader; see issue #11.
///
///   Three cases where 2016 had two, because a CLAP transport can be present and
/// parked:
///
///   - Playing, on a beats timeline: follow the host. An LFO is then phase-locked
///     to song position and rides a locate or a loop rather than drifting from it.
///   - Tempo known but stopped -- also a host that reports a tempo and no beats
///     timeline: keep the host's tempo and meter, and carry the phase forward
///     from where the timer already stands. An LFO keeps running, at the right
///     rate, with the transport parked, which is what Six Sines and surge-xt2 do
///     and what auditioning a patch without pressing play calls for. Continuing
///     from the timer's own position rather than a counter of our own is what
///     makes the handover in either direction seamless.
///   - No transport at all, or a tempo we cannot use: free run at the engine's
///     assumed 120 BPM 4/4, which is exactly what `updatePosition()` is.
///
/// \note Both `updatePosition()` and the three-argument
/// `updatePositionAndTimingInformation()` call `handleTimingInformationChange()`
/// themselves. The 2016 callers wrapped them in a second call of their own
/// (`SpectrumWorxSharedImpl::process()`, `SpectrumWorx::updatePosition()`), which
/// ran the period resnap twice for one change; not repeated here.
///                                       (30.07.2026.) (SW port)
void SpectrumWorxCLAP::updateLFOTiming(clap_process const *const process) noexcept
{
    auto const sampleRate(getSampleRate());
    if (sampleRate <= 0) [[unlikely]]
        return; // Not activated; nothing sensible to advance by.

    auto const *const transport(process->transport);

    constexpr std::uint32_t tempoAndMeter(CLAP_TRANSPORT_HAS_TEMPO |
                                          CLAP_TRANSPORT_HAS_TIME_SIGNATURE);

    /// \note tsig_num reaches the engine as the measure numerator, a std::uint8_t
    /// it divides by -- so a zero or an out-of-range one is not a tempo we can use.
    bool const usableTempo(transport && ((transport->flags & tempoAndMeter) == tempoAndMeter) &&
                           (transport->tempo > 0) && (transport->tsig_num >= 1) &&
                           (transport->tsig_num <= 255));
    /// \note `updatePositionAndTimingInformation` rather than the
    /// `updatePosition` that stood here: the two have the same body and only one
    /// of them says whether anything moved. Going from a host tempo to none is a
    /// timing change like any other -- the bar goes back to the assumed two
    /// seconds -- so this arm reports it too.
    if (!usableTempo)
    {
        if (updatePositionAndTimingInformation(process->frames_count).timingInfoChanged())
            timingChanged();
        return;
    }

    auto const beatsPerBar(static_cast<double>(transport->tsig_num));
    auto const barDuration(beatsPerBar * 60 / transport->tempo);

    constexpr std::uint32_t playingOnBeats(CLAP_TRANSPORT_HAS_BEATS_TIMELINE |
                                           CLAP_TRANSPORT_IS_PLAYING);

    double positionInBars;
    if ((transport->flags & playingOnBeats) == playingOnBeats)
    {
        positionInBars =
            (static_cast<double>(transport->song_pos_beats) / CLAP_BEATTIME_FACTOR) / beatsPerBar;
        // A count-in puts the song before its own start; the timer asserts >= 0.
        if (positionInBars < 0)
            positionInBars = 0;
    }
    else
    {
        auto const seconds(process->frames_count / static_cast<double>(sampleRate));
        positionInBars = lfoTimer().currentTimeInBars() + (seconds / barDuration);
    }

    if (updatePositionAndTimingInformation(static_cast<float>(positionInBars),
                                           static_cast<float>(barDuration),
                                           static_cast<std::uint8_t>(transport->tsig_num))
            .timingInfoChanged())
        timingChanged();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Does the host say every channel of \p buffer is a constant, and is
/// every one of those constants zero?
///
/// \note `constant_mask` is a *hint* -- `audio-buffer.h` says so -- and a host
/// that sets none reads as "no", which is the behaviour that was here before it
/// was consulted at all. A host that sets one has undertaken to fill the buffer
/// with the constant as well ("this implies that the buffer must be filled with
/// the constant value"), which is what makes reading sample 0 the right question
/// rather than a guess.
///
/// \note Every channel, and no partial arm. A port with one constant channel and
/// one carrying audio is carrying audio; substituting the constant for the quiet
/// half would be a third behaviour to explain and nothing has been observed
/// producing one.
///                                           (09.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
bool isDeclaredSilent(clap_audio_buffer const &buffer) noexcept
{
    /// \note `constant_mask` is 64 bits wide, so a channel past the 64th has no
    /// bit and cannot be declared anything.
    if (!buffer.data32 || (buffer.channel_count == 0) || (buffer.channel_count > 64))
        return false;

    for (std::uint32_t channel(0); channel < buffer.channel_count; ++channel)
    {
        if ((buffer.constant_mask & (std::uint64_t(1) << channel)) == 0)
            return false;
        if (!buffer.data32[channel] || (buffer.data32[channel][0] != 0))
            return false;
    }
    return true;
}
} // anonymous namespace

void SpectrumWorxCLAP::runEngine(clap_process const *const process) noexcept
{
    if ((process->audio_inputs_count == 0) || (process->audio_outputs_count == 0))
        return;

    auto const &input(process->audio_inputs[0]);
    auto &output(process->audio_outputs[0]);
    if (!input.data32 || !output.data32)
        return; // 64 bit hosts get silence rather than a crash until 5.7.

    /// \note Unchecked, for the same reason getSampleRate() is: the channel
    /// count is not one of the fields isEngineSetupUpToDate() compares, and this
    /// runs on the audio thread before SpectrumWorxCore::process() takes the
    /// processing lock. See the note on getSampleRate().
    auto const channels(uncheckedEngineSetup().numberOfChannels());
    if ((input.channel_count < channels) || (output.channel_count < channels))
        return;

    if (!engineRunning_)
        return;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The engine reads a side channel whenever the input mode calls for
    /// one, and does not check that the host connected the port. Three
    /// arrangements fall back to the main input, which is the documented
    /// behaviour for an unpatched side chain -- a Blender with nothing patched
    /// blends the signal with itself:
    ///
    ///   - no second port in the count at all;
    ///   - a second port with no `data32`;
    ///   - a second port the host declares constant and zero.
    ///
    ///   The third is `clap_audio_buffer::constant_mask` and is new as of
    /// 09.08.2026. Before it the first two were the whole test, and **no real
    /// host takes either**: every wrapper hands over a buffer it owns. So what an
    /// unpatched side chain contained was whatever the host left there, the
    /// documented behaviour was unreachable, and it is how uninitialised memory
    /// reached the FFT from an AUv2 bus.
    ///
    /// \note What the mask cannot say is *why* a channel is constant. An
    /// unpatched port and a patched send that has gone quiet are both a constant
    /// zero, and they take the same arm here: a side chain carrying no signal is
    /// the main input, whichever of the two it is. That is a real consequence and
    /// it is the chosen one -- a muted send audibly switches to blending with
    /// itself and switches back when it un-mutes.
    ///
    /// \note And it is a *hint*: `audio-buffer.h` says so in as many words, and
    /// neither clap-wrapper nor a given DAW is known to set it. Nothing here
    /// depends on it. A mask of zero is exactly the behaviour that was here
    /// before, so a host that never sets one loses nothing and a host that does
    /// gets the behaviour that was intended in 2016.
    ///
    ////////////////////////////////////////////////////////////////////////////

    bool const sideChainCarriesSignal((process->audio_inputs_count > 1) &&
                                      process->audio_inputs[1].data32 &&
                                      !isDeclaredSilent(process->audio_inputs[1]));

    float const *const *sideChannels(sideChainCarriesSignal ? process->audio_inputs[1].data32
                                                            : input.data32);

    ////////////////////////////////////////////////////////////////////////////
    // An external audio file, when one is loaded, in place of the port.
    //
    // \note A `try_lock` on the processing lock stood around this, because the
    // message thread swapped the decoded data under the reader. Nothing swaps
    // anything under this thread now -- `pSample_` is only ever exchanged by
    // `drainCommands()`, which is this thread, at the top of this callback -- so
    // there is no lock, and no block is played from the wrong source because
    // another thread happened to be busy.
    //                                        (02.08.2026.) (SW port)
    ////////////////////////////////////////////////////////////////////////////

    /// \note A Sample is always stereo, so a wider engine configuration -- which
    /// nothing produces today; activate() asks for 2 x 2 -- keeps the port.
    float const *sampleChannels[Sample::numberOfChannels];
    if (pSample_ && *pSample_ && (channels <= std::size(sampleChannels)) &&
        (buffers().numberOfSideChannels() >= channels) &&
        (process->frames_count <= buffers().blockSize()))
    {
        auto const startingPosition(pSample_->samplePosition());
        std::uint32_t position(startingPosition);
        for (std::uint8_t channel(0); channel < channels; ++channel)
        {
            // Every channel reads the same span, so each starts where the last
            // one did and the advance is taken once.
            position = startingPosition;
            sampleChannels[channel] =
                sampleChunk(pSample_->channel(channel), position, process->frames_count,
                            buffers().sideChannel(channel).begin());
        }
        pSample_->samplePosition() = position;
        sideChannels = sampleChannels;
    }

    SpectrumWorxCore::process(input.data32, sideChannels, output.data32, 1.0f,
                              process->frames_count);

    // Ports beyond what the engine is configured for are the host's to see as
    // silence, not as whatever was in the buffer.
    for (std::uint32_t channel(channels); channel < output.channel_count; ++channel)
        std::memset(output.data32[channel], 0, process->frames_count * sizeof(float));
}

void SpectrumWorxCLAP::onMainThread() noexcept
{
    drainEngineEvents();

    auto const flags(pendingRescan_.exchange(0));
    if (flags && _host.canUseParams())
        _host.paramsRescan(static_cast<clap_param_rescan_flags>(flags));

    // What the audio thread was not allowed to do itself.
    if (pendingMarkDirty_.exchange(false) && _host.canUseState())
        _host.stateMarkDirty();

    PluginHelper::onMainThread();
}

////////////////////////////////////////////////////////////////////////////////
//
// The protocol
// ------------
//
//   Two rings and a mailbox, and the two places they are drained. Every edit an
// interface or a host makes crosses one of them -- see
// doc/tech/threading_model.md §3 for which carries what, and why a coalescing
// mailbox rather than a third ring.
//
////////////////////////////////////////////////////////////////////////////////

/// \note Called from `process()` and from `paramsFlush()`, which is the same
/// arrangement `flushUIEdits` already has and is safe for the same reason: CLAP
/// forbids a host from running the two concurrently, so there is still exactly one
/// consumer. It matters that flush drains too -- a host with the transport parked
/// may not be calling `process()`, and `requestParameterFlush()` after an edit is
/// what then gets the edit applied.
void SpectrumWorxCLAP::drainCommands()
{
    Threading::ToEngine command;
    while (toEngine_.pop(command))
    {
        switch (command.kind)
        {
        case Threading::ToEngine::Kind::None:
            break;

        case Threading::ToEngine::Kind::SetBaseParameter:
        {
            /// \note The same entry point a host's parameter event reaches, and
            /// with the value in the same units: `handleEvent` converts off the
            /// CLAP edge first and then calls this. So an edit made in the
            /// interface and one made in the host's panel are the same operation
            /// arriving by two routes, applied on one thread, in a defined order.
            ParameterID const parameterID{
                Plugins::ParameterID{command.setBaseParameter.parameterID}};
            setParameter(parameterID, command.setBaseParameter.value);
            if (parameterID.type() == ParameterID::ModuleChainParameter)
                requestRescan(CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT |
                              CLAP_PARAM_RESCAN_VALUES);
            break;
        }

        case Threading::ToEngine::Kind::SetSlot:
        {
            auto *const pOutgoing(installModuleInSlot(
                command.setSlot.slot, static_cast<Module *>(command.setSlot.pModule)));
            if (pOutgoing)
                retire(Threading::ToUI::Retired::Module, pOutgoing);
            chainChanged();
            break;
        }

        case Threading::ToEngine::Kind::MoveModule:
            moveModule(command.moveModule.from, command.moveModule.to);
            chainChanged();
            break;

        case Threading::ToEngine::Kind::SwapChain:
        {
            auto *const pIncoming(static_cast<AutomatedModuleChain *>(command.swapChain.pChain));
            swapModuleChain(*pIncoming);
            /// \note The same object back, now holding what used to be live.
            retire(Threading::ToUI::Retired::Chain, pIncoming);
            chainChanged();
            break;
        }

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \note The one edit the interface cannot address by `ParameterID`, so
        /// it names an index instead and this dispatches on it -- the same
        /// `invokeFunctorOnIndexedParameter` the global parameters go through.
        /// Before this the interface wrote the LFO itself, from the message
        /// thread, on a module the audio thread reads every block.
        ///                                   (06.08.2026.) (SW port)
        ///
        ////////////////////////////////////////////////////////////////////////
        case Threading::ToEngine::Kind::SetUnexportedLFOParameter:
        {
            auto const &edit(command.setUnexportedLFOParameter);
            auto const pModule(moduleChain().moduleAs<Module>(edit.moduleIndex));
            if (pModule && (edit.moduleParameterIndex < pModule->numberOfLFOControledParameters()))
                LE::Parameters::invokeFunctorOnIndexedParameter(
                    pModule->lfo(edit.moduleParameterIndex).parameters(), edit.lfoParameterIndex,
                    RawParameterSetter{edit.value});
            break;
        }

        case Threading::ToEngine::Kind::SwapSample:
        {
            auto *const pOutgoing(
                std::exchange(pSample_, static_cast<Sample *>(command.swapSample.pSample)));
            clearSideChannelData();
            if (pOutgoing)
                retire(Threading::ToUI::Retired::Sample, pOutgoing);
            break;
        }
        }
    }

    /// \note After the batch rather than per command: a preset that moves the
    /// FFT size and the overlap factor together is one restart, not two. And
    /// `clap_host::request_restart` is `[thread-safe]`, which is what makes this
    /// legal from here at all.
    ///
    /// \note Only while there is an audio thread to defer on behalf of.
    /// `deactivate()` drains through here too, and there the pending setup is
    /// applied a few lines later rather than deferred -- asking the host for a
    /// restart in the middle of the one it is already performing would earn a
    /// second, pointless, deactivate/activate cycle.
    ///                                       (08.08.2026.) (SW port)
    if (engineIsRunning() && spectralSetupPending() &&
        !restartRequested_.exchange(true, std::memory_order_acq_rel))
        _host.requestRestart();
}

////////////////////////////////////////////////////////////////////////////////
// What the audio thread hands back. `[audio-thread]`
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
/// \note One answer to "what happens when a ring is full", where there were
/// seven. Six of the pushes did not look at the result at all, so the two
/// `Program` copies -- and, for the outgoing edits, the host -- drifted apart
/// with nothing said and no way to find out afterwards.
///
///   What this can and cannot do is worth being exact about. It counts; it does
/// not repair. `publishSlot` and `publishChain` undo their own allocation and
/// still do. An echo, an edit or a gesture that is dropped is *gone*: the other
/// side has already moved by the time the push fails, and the ring was where the
/// information to put it back would have been. So the counter is the honest
/// answer -- the plugin can at least say that it is no longer describable.
///
/// \note A lossless echo is the design answer if this is ever seen above zero
/// in the field, and it is a bigger change than a bug fix: `ValueMailbox` cannot
/// overflow and coalesces, and the note on it explains why base values were put
/// in the ring instead.
///
/// \note A counter and **not** an assertion, which is a deliberate departure
/// from the `LE_ASSERT_MSG(false, ...)` that stood at the two publish.cpp sites.
/// An assertion here answers differently in a checked build and a shipped one,
/// which is the whole family of defect this branch has been removing -- and it
/// makes the behaviour untestable, because a case that fills a ring on purpose
/// aborts instead of measuring. The counter reads the same in every
/// configuration, which is what lets "the ring never fills" stop being a belief.
///                                           (08.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

bool SpectrumWorxCLAP::pushed(bool const wasPushed, char const *const what) const
{
    if (wasPushed) [[likely]]
        return true;

    LE::Utility::ignoreUnused(what);
    droppedMessages_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

/// \note A full retire ring is a leak, and there is nothing sensible to do about
/// it here -- freeing on this thread is the one thing the ring exists to prevent.
/// 1024 deep against one entry per structural change, so it is a checked-build
/// assertion rather than a policy.
///
/// \note The push is the statement and the assert only reports on it. It was
/// written the other way round -- inside `LE_ASSERT_MSG`, which is
/// `static_cast<void>(0)` under NDEBUG -- so no shipped build retired anything
/// at all: every module a slot change displaced, every chain a preset load
/// replaced and every swapped-out sample was leaked, and the checked build the
/// suite runs in was the only one where the protocol existed.
///                                           (08.08.2026.) (SW port)
void SpectrumWorxCLAP::retire(Threading::ToUI::Retired const what, void *const pObject)
{
    if (toUI_.push(Threading::retire(what, pObject)))
        return;

    LE_ASSERT_MSG(false, "The retire queue is full; something will be leaked.");
}

/// \note And marks the state dirty, which `presetChangeEnd()` used to do for
/// itself before the chain it was announcing had been installed. Every route
/// that gets here is a structural change a session has to remember -- a preset,
/// a slot, a move -- and only the parameter-event route was telling the host so,
/// through `setParameter()`.
void SpectrumWorxCLAP::chainChanged()
{
    /// \note Dropping this leaves the rack drawing the chain that was there
    /// before, with no second announcement coming: `drainEngineEvents()` is
    /// edge-triggered on this message.
    pushed(toUI_.push(Threading::chainChanged()),
           "The echo queue is full; the module rack will not be resynchronised.");
    requestRescan(CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_VALUES);
    markCurrentProgramAsModified();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The half of the tempo story the interface never got. A synced LFO's
/// period is a fraction of the *host's* bar, so a tempo or meter change makes the
/// number the panel is showing mean a different length of time and moves the grid
/// the period snaps to. `updateForNewTimingInfo()` has always known how to redraw
/// that; what it did not have was a caller. Its 2016 one was
/// `SpectrumWorx::updatePosition()`, in a host class this port deleted, and the
/// replacement runs on the audio thread -- where touching a widget is the one
/// thing the whole model forbids.
///
///   So it arrives as a message, like every other thing the audio thread has to
/// tell the interface. Nothing travels with it: what changed is engine state the
/// main thread may read, and the news is the whole of the payload.
///
/// \note Not gated on there being an editor, deliberately -- the gate is on the
/// drain, where `pEditor_` may be read at all. Asking here would be reading a
/// main-thread member from the audio thread to save a ring slot.
///
/// \note And the coalescing, which is this message's alone. \see
/// `timingChangeQueued_` for why a tempo ramp would otherwise cost the
/// retirements. A push that fails clears it again, so a full ring costs one
/// missed redraw rather than every future one.
///                                           (09.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCLAP::timingChanged()
{
    if (timingChangeQueued_.exchange(true, std::memory_order_relaxed))
        return;

    if (!pushed(toUI_.push(Threading::timingChanged()),
                "The echo queue is full; the LFO panel will not follow the tempo."))
    {
        timingChangeQueued_.store(false, std::memory_order_relaxed);
        return;
    }

    /// \note `request_callback` is `[thread-safe]`, which is what makes this legal
    /// from here -- the same argument `markCurrentProgramAsModified()` makes.
    /// Without it the message waits for whatever else asks for a callback next,
    /// and a tempo change on its own asks for nothing.
    _host.requestCallback();
}

void SpectrumWorxCLAP::publishModulatedValues()
{
    /// \note Not gated on there being an editor. The loop is five modules by ten
    /// parameters of `enabled()` checks -- noise beside one FFT -- and only an
    /// enabled LFO does a store. Gating it would mean the one thing that reads
    /// the mailbox is also the only thing that can be tested against it.
    std::uint8_t slot(0);
    moduleChain().forEach<Module>([&](Module const &module) {
        auto const parameters(module.numberOfParameters());
        /// \note From 1: Bypass has no LFO, and the LFO index is the parameter
        /// index less it. Same convention as ModuleParameters::lfo().
        for (std::uint8_t parameter(1); parameter < parameters; ++parameter)
        {
            if (!module.lfo(static_cast<std::uint8_t>(parameter - 1)).enabled())
                continue;

            ParameterID parameterID;
            parameterID.value.type = ParameterID::ModuleParameter;
            parameterID.value._.module = {ParameterID::Zero, parameter, slot};

            /// \note The *live* value, which is the modulated one -- the whole
            /// distinction stage 3 introduced. What a host reads is the
            /// unmodulated value and does not belong here.
            auto const value(
                (parameter < Engine::ModuleParameters::numberOfBaseParameters)
                    ? module.getBaseParameter(parameter)
                    : module.getEffectParameter(
                          Engine::ModuleParameters::effectSpecificParameterIndex(parameter)));

            values_.set(parameterIndexFromBinaryID(parameterID.binaryValue), value);
        }
        ++slot;
    });
}

void SpectrumWorxCLAP::drainEngineEvents()
{
    LE_ASSERT(Threading::isMainThread() || !Threading::isAudioThread());

    bool timingChangedPending(false);

    Threading::ToUI event;
    while (toUI_.pop(event))
    {
        switch (event.kind)
        {
        case Threading::ToUI::Kind::None:
            break;

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \note The copy first and the interface second, in that order and both
        /// unconditionally. What the host wrote to the engine has to land in
        /// `programMain_` whether or not anybody is looking at it -- `paramsValue`
        /// and `stateSave` are answered from it with the window shut -- and a
        /// strip that then redraws is reading a copy that already agrees.
        ///
        ////////////////////////////////////////////////////////////////////////
        case Threading::ToUI::Kind::BaseParameterChanged:
        {
            ParameterID const parameterID{
                Plugins::ParameterID{event.baseParameterChanged.parameterID}};
            setParameterIn<Protocol>(programMain_, parameterID, event.baseParameterChanged.value);
            if (pEditor_)
                pEditor_->parameterChangedElsewhere(parameterID, event.baseParameterChanged.value);
            break;
        }

        case Threading::ToUI::Kind::ChainChanged:
            /// \note Coalesced: a preset that swaps the chain and then fills a
            /// slot is one resync, and the resync is a recomputation rather than
            /// a diff, so running it twice would only cost.
            chainChangedPending_ = true;
            break;

        /// \note Cleared here rather than after the redraw, so that a tempo that
        /// moves again while this drain runs is announced rather than swallowed.
        /// The cost of clearing early is at most one extra message.
        case Threading::ToUI::Kind::TimingChanged:
            timingChangeQueued_.store(false, std::memory_order_relaxed);
            timingChangedPending = true;
            break;

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \note The other end of §5, and the only place any of this is
        /// destroyed. A `Module` is one *reference* rather than an object -- the
        /// interface may still hold a strip pointing at it, and dropping that
        /// strip is what finally frees it.
        ///
        ////////////////////////////////////////////////////////////////////////
        case Threading::ToUI::Kind::Retire:
            switch (event.retire.what)
            {
            case Threading::ToUI::Retired::None:
                break;
            case Threading::ToUI::Retired::Module:
                intrusive_ptr_release(&Engine::node(*static_cast<Module *>(event.retire.pObject)));
                break;
            case Threading::ToUI::Retired::Chain:
                delete static_cast<AutomatedModuleChain *>(event.retire.pObject);
                break;
            case Threading::ToUI::Retired::Sample:
                delete static_cast<Sample *>(event.retire.pObject);
                break;
            }
            break;
        }
    }

    if (std::exchange(chainChangedPending_, false) && pEditor_)
        pEditor_->resyncModuleRack();

    /// \note After the rack, and only if there is a window: this is a redraw and
    /// nothing behind the interface depends on it, which is what separates it
    /// from the echo above.
    if (timingChangedPending && pEditor_)
        pEditor_->updateForNewTimingInfo();
}

////////////////////////////////////////////////////////////////////////////////
//
// Edits made in the editor
// ------------------------
//
//   Three entry points, and each does the same two things: applies the change to
// the Program this thread owns, and queues it for the engine. The editor used to
// do only the second -- `toEngine().push()` at four sites and
// `Threading::publish{Slot,ModuleMove}()` at two more -- which was right while
// the engine's Program was the only one there was.
//
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCLAP::editParameter(ParameterID const parameterID, float const value) const
{
    auto &plugin(const_cast<SpectrumWorxCLAP &>(*this));
    setParameterIn<Protocol>(plugin.programMain_, parameterID, value);
    /// \note The other half. This one is applied above before the push is
    /// attempted, so a drop is the T1.1 shape from the other side: the interface
    /// and the saved session hold the edit and the engine never hears it.
    pushed(
        toEngine_.push(Threading::setBaseParameter(parameterID.binaryValue, value)),
        "The command queue is full; an edit was applied to the interface and not to the engine.");
}

/// \note Two modules built for one slot change, and they are not
/// interchangeable: the engine's is sized against the current spectral setup by
/// `createModuleForSlot()`, and this thread's carries parameters and no storage.
/// \see ParametersOnlyModuleInitialiser.
bool SpectrumWorxCLAP::editSlot(std::uint8_t const slot, std::int8_t const effectIndex)
{
    /// \note Building it is still synchronous and still this thread's, so an
    /// effect this build does not have is still a failure the caller can be told
    /// about here. Only the *installing* is deferred.
    auto *const pModule(Threading::createModuleForSlot(*this, effectIndex, slot));
    if ((effectIndex != AutomatedModuleChain::noModule) && !pModule)
        return false;

    /// \note This thread's copy, so the destroying overload is the right one.
    /// \see AutomatedModuleChain::setParameter.
    programMain_.moduleChain().setParameter(slot, effectIndex, ParametersOnlyModuleInitialiser{});
    pushed(Threading::publishSlot(*this, toEngine_, slot, effectIndex, pModule),
           "The command queue is full; a slot change reached the interface and not the engine.");
    return true;
}

void SpectrumWorxCLAP::editModuleMove(std::uint8_t const from, std::uint8_t const to)
{
    programMain_.moduleChain().moveModule(from, to);
    pushed(Threading::publishModuleMove(*this, toEngine_, from, to),
           "The command queue is full; a module move reached the interface and not the engine.");
}

void SpectrumWorxCLAP::publishUnexportedLFOParameter(std::uint8_t const moduleIndex,
                                                     std::uint8_t const moduleParameterIndex,
                                                     std::uint8_t const lfoParameterIndex,
                                                     float const value)
{
    /// \note No `engineIsRunning()` arm, unlike the publishers in publish.cpp:
    /// the engine's module is reached by index from `drainCommands()` and there
    /// is no main-thread equivalent to apply here. With nothing processing the
    /// command simply waits, which is what `activate()` then drains.
    pushed(toEngine_.push(Threading::setUnexportedLFOParameter(moduleIndex, moduleParameterIndex,
                                                               lfoParameterIndex, value)),
           "The command queue is full; an LFO waveform or sync change was not heard.");
}

////////////////////////////////////////////////////////////////////////////////
// On their way to the host
////////////////////////////////////////////////////////////////////////////////

/// \note The push/pop pair that stood here is `Threading::SPSCQueue` now, which
/// was generalised from it. Same ordering, same drop-on-full policy, one
/// implementation for the three rings this plugin has.
///                                           (02.08.2026.) (SW port)

void SpectrumWorxCLAP::flushUIEdits(clap_output_events const *const out)
{
    UIEdit edit;
    while (uiEdits_.pop(edit))
    {
        clap_event_param_gesture gesture{};
        clap_event_param_value value{};
        clap_event_header *pHeader{nullptr};

        if (edit.kind == UIEdit::Kind::Value)
        {
            value.header.size = sizeof(value);
            value.header.time = 0;
            value.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            value.header.type = CLAP_EVENT_PARAM_VALUE;
            value.header.flags = 0;
            value.param_id = edit.id;
            value.cookie = nullptr;
            value.note_id = value.port_index = value.channel = value.key = -1;
            value.value = edit.value;
            pHeader = &value.header;
        }
        else
        {
            gesture.header.size = sizeof(gesture);
            gesture.header.time = 0;
            gesture.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            gesture.header.type = (edit.kind == UIEdit::Kind::GestureBegin)
                                      ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                      : CLAP_EVENT_PARAM_GESTURE_END;
            gesture.header.flags = 0;
            gesture.param_id = edit.id;
            pHeader = &gesture.header;
        }

        out->try_push(out, pHeader);
    }
}

////////////////////////////////////////////////////////////////////////////////
// What the editor tells the host.
//
// \note All of it queues rather than calls. These run on the UI thread, and a
// host takes parameter changes only through the output event list it hands to
// process() or flush().
////////////////////////////////////////////////////////////////////////////////

/// \note The editor works in the effect's own units throughout -- a knob knows
/// its parameter's real range -- so an edit it made is normalised here, on the
/// way out, and nowhere else.
void SpectrumWorxCLAP::HostProxy::automatedParameterChanged(
    ParameterSelector const parameter, Plugins::AutomatedParameterValue const value) const
{
    ParameterID const parameterID{parameter};
    Plugins::ParameterInformation<Protocol> ranges;
    /// \note The main thread's copy: this is the editor's own edit on its way out,
    /// and the editor runs there.
    liveRanges(parameterID, ranges, plugin_.programMain_);

    plugin_.pushed(plugin_.uiEdits_.push({parameter.value,
                                          static_cast<Plugins::AutomatedParameterValue>(
                                              CLAPEdge::toHost(parameterID, ranges, value)),
                                          UIEdit::Kind::Value}),
                   "The outgoing edit queue is full; the host was not told about an edit.");

    /// \note The same rescan handleEvent() asks for when the *host* fills a slot.
    /// A slot selector is the one parameter whose value changes what the others
    /// are called and what they mean, and it can be moved from either side; the
    /// rescan was only wired to the host's side, so a module added from the
    /// plugin's own UI left every one of that slot's parameters showing the name
    /// it was first read with.
    ///                                       (29.07.2026.) (SW port)
    if (parameterID.type() == ParameterID::ModuleChainParameter)
        const_cast<SpectrumWorxCLAP &>(plugin_).requestRescan(
            CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_VALUES);

    plugin_.markCurrentProgramAsModified();
    plugin_.requestParameterFlush();
}

void SpectrumWorxCLAP::HostProxy::automatedParameterBeginEdit(
    ParameterSelector const parameter) const
{
    /// \note A dropped gesture is the worst of these to leave silent: the pair
    /// has to balance, and a host whose lane sees a begin without an end stays
    /// latched in write mode until something else ends it.
    plugin_.pushed(plugin_.uiEdits_.push({parameter.value, 0, UIEdit::Kind::GestureBegin}),
                   "The outgoing edit queue is full; a gesture will not be balanced.");
    plugin_.requestParameterFlush();
}

void SpectrumWorxCLAP::HostProxy::automatedParameterEndEdit(ParameterSelector const parameter) const
{
    plugin_.pushed(plugin_.uiEdits_.push({parameter.value, 0, UIEdit::Kind::GestureEnd}),
                   "The outgoing edit queue is full; a gesture will not be balanced.");
    plugin_.requestParameterFlush();
}

/// \note The `canUseParams()` guard is not optional and these three had it
/// missing -- the same bug as markCurrentProgramAsModified()'s thread check, at
/// three more sites, found by the audit that note recommends rather than by a
/// test. `clap_host_params` is an *optional* extension; clap-helpers'
/// `paramsRequestFlush()` is `assert( canUseParams() ); _hostParams->request_flush( … );`.
/// A host that offers no parameters at all still gets the queued edit; it simply
/// does not get told to come and collect it, which is all it could do with the
/// news anyway.
///                                           (01.08.2026.) (SW port)
void SpectrumWorxCLAP::requestParameterFlush() const
{
    if (!_host.canUseParams())
        return;
    const_cast<SpectrumWorxCLAP &>(*this)._host.paramsRequestFlush();
}

/// \note A whole program is about to be swapped in, so the host should expect
/// every value to move at once. Nothing to announce up front -- CLAP has no
/// "hold on" call, and the rescan at the other end is what a host acts on.
void SpectrumWorxCLAP::HostProxy::presetChangeBegin() const {}

/// \note INFO as well as VALUES and TEXT. A preset replaces the module chain,
/// so what the parameters are *called* and which module path they sit under
/// both move, not only what they read -- which is RESCAN_INFO's own case. The
/// count does not move (see rebuildParameterIDs), so this is legal while the
/// plugin is active, unlike RESCAN_ALL.
///
/// \note `[main-thread]`: reached from the editor's preset browser, and the
/// editor runs on the main thread.
void SpectrumWorxCLAP::HostProxy::presetChangeEnd() const
{
    auto &plugin(const_cast<SpectrumWorxCLAP &>(plugin_));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Only when the chain is already in, which with audio running it is
    /// not. `Threading::publishChain()` installs it here and now when nothing is
    /// processing and *queues* it otherwise -- so this used to point the host at
    /// a parameter list still sitting in the command ring, and then invite it to
    /// go and read one: Bitwig calls `params.value_to_text` synchronously from
    /// inside `state.mark_dirty`, and the audio thread splices the chain in the
    /// same moment.
    ///
    ///   The engine says so itself when it installs one -- `drainCommands()`'s
    /// SwapChain case calls `chainChanged()` -- so the notification is raised
    /// from the side that knows it has happened, rather than from the side that
    /// only knows it has asked.
    ///                                       (06.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    if (!plugin.engineIsRunning())
    {
        // Deferred, and coalescing, for the reason stateLoad() gives.
        plugin.requestRescan(CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_VALUES |
                             CLAP_PARAM_RESCAN_TEXT);
        plugin_.markCurrentProgramAsModified();
    }

    /// \note A preset that changes the FFT size sets the parameter on this
    /// thread and leaves the setup where it is; this is what then asks the host
    /// for the restart that applies it. `drainCommands()` does the same for the
    /// route through the queue -- both, because a preset load does not go
    /// through the queue and a knob does.
    ///                                       (02.08.2026.) (SW port)
    if (plugin.spectralSetupPending() &&
        !plugin.restartRequested_.exchange(true, std::memory_order_acq_rel))
        plugin._host.requestRestart();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And "come and collect it", because since the six global parameters
    /// stopped being written straight into a running engine they travel by queue
    /// like every other edit -- and a queue nobody drains is a preset that never
    /// arrives. A knob gets this for free from `automatedParameterChanged`; a
    /// preset makes no per-parameter notification, by design, so it asks once
    /// here, at the end, for all of them.
    ///
    ///   It matters most for the host that has no audio thread to drain them:
    /// the transport is parked, no block is coming, and `params.flush()` is what
    /// CLAP offers instead. Without it a preset that changes the FFT size would
    /// wait for the next block to ask for the restart that applies it, and there
    /// is no next block.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    plugin_.requestParameterFlush();
}

bool SpectrumWorxCLAP::HostProxy::reportNewLatencyInSamples(unsigned int const latency) const
{
    auto &plugin(const_cast<SpectrumWorxCLAP &>(plugin_));
    plugin.latencyInSamples_ = latency;
    if (!plugin_._host.canUseLatency())
        return false;
    plugin._host.latencyChanged();
    return true;
}

/// \note `clap_host_state.mark_dirty` is `[main-thread]`, and this is reached
/// from both threads: the editor calls it on the UI thread, and a host parameter
/// event calls it from process() -- the interop layer marks the program modified
/// for *any* automated change, without knowing where the change came from.
/// clap-validator fails six of its parameter tests on exactly that.
///
///   So the audio thread only records that it wants to; onMainThread() does it.
/// The same deferral the rescan flags already use, for the same reason.
///
/// \note `canUseThreadCheck()` is not decoration either. `clap.thread-check` is
/// an *optional* extension, and clap-helpers' `HostProxy::isMainThread()` is
/// `assert( canUseThreadCheck() ); return _hostThreadCheck->is_main_thread( … );`
/// -- so asking a host that does not offer it is an assertion in a checked build
/// and a null dereference in a shipping one, on a path every parameter write
/// reaches. A host that cannot say which thread this is gets the deferral, which
/// is correct from either: `request_callback` is `[thread-safe]` and
/// `mark_dirty` then happens where it is allowed to.
///                                           (01.08.2026.) (SW port)
void SpectrumWorxCLAP::markCurrentProgramAsModified() const
{
    if (!_host.canUseState())
        return;

    auto &plugin(const_cast<SpectrumWorxCLAP &>(*this));

    if (_host.canUseThreadCheck() && _host.isMainThread())
    {
        plugin._host.stateMarkDirty();
        return;
    }

    if (!pendingMarkDirty_.exchange(true))
        plugin._host.requestCallback();
}

////////////////////////////////////////////////////////////////////////////////
// State
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
///   The preset serialisation, plus a `<dawExtraState>` block. Not a format of
/// its own, which is what this was until 02.08.2026: `SWX1` followed by 286
/// `(uint32 id, double value)` pairs, keyed on `SW::ParameterID` -- which means
/// "slot 3's 4th parameter" and not "Convolver's Wet". That survived a reload
/// and nothing else. It could not be versioned against a changing effect list,
/// and it could not hold anything that is not a parameter, which is why the
/// sample a session had loaded did not come back.
///
///   What a preset already solved and this now inherits: keys that are names,
/// so an effect list that moves does not silently re-point them; a `Format`
/// stamp; a reader for every file the plugin has ever written; and `Sample`,
/// which has been in the format since 2011.
///
/// \note Natural units, not CLAPEdge's 0..1 -- as before, and now because the
/// preset format says so rather than because this code chose it. The edge exists
/// because a *host* may not see a range move; a file has no such problem, and
/// storing natural units means the state does not encode the edge policy and so
/// survives a change to it.
///
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
/// \note A function-try-block on both halves of `clap_plugin_state`, because
/// both are `noexcept` and neither could promise it.
///
///   Every CLAP entry point is `noexcept`, so an exception crossing one is
/// `std::terminate` rather than a failed call -- the host dies instead of saying
/// it could not read the project. These two are where that is reachable: between
/// them they buffer a stream a host supplies, build the whole module chain out
/// of it, and decode whatever audio file the state names, and all of that
/// allocates. `Sample::load` is the sharpest -- a session naming a long file
/// asks for hundreds of megabytes on the way through, from inside `stateLoad`.
///
///   CLAP already has an answer for a state that will not load: return false,
/// and the host reports it. That is better than the process ending, whatever the
/// reason -- and there is nothing this plugin could usefully do about
/// `bad_alloc` anyway.
///
/// \note A function-try-block rather than a wrapper so that the bodies stay
/// where they are and this stays legible as one guarantee about two entry
/// points. Falling off the end of the handler is what `return false` is for; for
/// a non-void function it would otherwise be undefined.
///                                           (08.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

bool SpectrumWorxCLAP::stateSave(clap_ostream const *const stream) noexcept
try
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The echo first, because this is the main thread and a host may ask
    /// for the state before the callback the plugin requested has run.
    ///
    ///   A parameter the host wrote during `process()` is applied to the engine
    /// there and echoed over `ToUI`; `onMainThread()` is what normally drains it
    /// into `programMain_`, and nothing obliges a host to run that between the
    /// block and the save. `paramsFlush()` already makes the same argument for an
    /// *inactive* plugin and drains its own echo; this is the active half of it,
    /// and without it a session saved right after automation moved something
    /// stores the value from before the move.
    ///
    ///   Found by `clap-cpp-validator`'s `state-reproducibility-flush`, which
    /// sets the same parameters on two instances -- one through `flush()`, one
    /// through `process()` -- and compares the two state files. They differed.
    ///                                       (06.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    drainEngineEvents();

    /// \note `withDawExtraState`, which a `.swp` does not get. The block is
    /// empty today -- see installDawExtraStateHooks() -- and written anyway, so
    /// that "a session is a preset plus somewhere to put the rest" is a property
    /// of the bytes rather than a plan.
    auto const dawExtraState(sessionState());
    /// \note `programMain_`, this being `[main-thread]`: a host saves a session
    /// while the audio thread is running, and walking the engine's chain to do it
    /// is the same read that crashed `paramsInfo`.
    /// \note `u8string()`, and the format's own `std::string_view` overload: the
    /// sample path goes into `<p n="Sample">` as UTF-8 bytes on every platform,
    /// which is what makes a session written on one openable on another.
    auto const state(savePreset(LE::IO::pathToUTF8(sampleFile_), {}, programMain_, &dawExtraState));

    /// \note The terminator goes into the stream, because loadFrom() parses a
    /// C string and a host is free to hand back exactly what it was given with
    /// nothing after it.
    return writeFully(stream, state.c_str(), state.size() + 1);
}
catch (...)
{
    return false;
}

bool SpectrumWorxCLAP::stateLoad(clap_istream const *const stream) noexcept
try
{
    auto state(readWholeStream(stream));
    if (!state)
        return false;

    /// \note Nobody asked for this load, so nothing under it may stop to ask the
    /// user anything. \see GUI::UnattendedLoad.
    GUI::UnattendedLoad const unattended;

    /// \note `pEditor_`, which is null unless a window happens to be open. A
    /// host restores state before it ever shows an editor, and with the window
    /// shut for the rest of the session; the same call serves both because the
    /// consumer takes the editor as a pointer.
    ///
    /// \note And `ignoreExternalSample` false: the browser's toggle is a
    /// question about somebody else's preset, and this is the session's own
    /// state, where the sample is exactly the thing that has been getting lost.
    auto const dawExtraState(sessionState());
    if (!GUI::loadPreset(*this, pEditor_, state->data(), false /*ignoreExternalSample*/, nullptr,
                         nullptr, &dawExtraState))
        return false;

    /// \note Deferred through `requestRescan()` rather than announced here, even
    /// though this is already the main thread and could call straight through.
    /// `GUI::loadPreset()` above ends in `chainChanged()`, which asks for the same
    /// rescan, so a preset load used to ask the host twice within a few
    /// microseconds -- and a host acts on each one. The AU wrapper's answer is to
    /// rebuild its whole parameter tree on a queue of its own while clearing the
    /// structures that tree is being built from, so the second ask lands inside
    /// the first rebuild. `pendingRescan_` collapses the pair into one.
    ///                                       (06.08.2026.) (SW port)
    requestRescan(CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_VALUES);
    return true;
}
catch (...)
{
    return false;
}

////////////////////////////////////////////////////////////////////////////////
///
/// SpectrumWorxCLAP::sessionState()
/// --------------------------------
///
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Where session state that is not a parameter goes.
///
///   Empty, and deliberately so. The mechanism is what the state work owed; the payload
/// is a list that will grow one bullet at a time, and guessing at it now would
/// be inventing a schema for settings nobody has asked to persist yet.
///
///   The two candidates, both `[main-thread]` and neither of them a parameter:
///
///   - the preset browser's location and selection -- it does not remember where
///     it was, for the session case;
///   - the interface settings (mouse-over reaction, LFO update behaviour,
///     hide-cursor-on-knob-drag), which the CLAP build persists nowhere at all,
///     so they are back at their defaults every time the plugin is loaded. Those
///     are arguably user preferences rather than session state, and
///     sst-plugininfra's userdefaults.h is the other candidate home; the two are
///     not exclusive and surge uses both.
///
////////////////////////////////////////////////////////////////////////////////

DawExtraState SpectrumWorxCLAP::sessionState() const
{
    return {[](TiXmlElement &) {}, [](TiXmlElement const &) {}};
}

////////////////////////////////////////////////////////////////////////////////
// Editor
////////////////////////////////////////////////////////////////////////////////

/// \note The shim owns what this returns and destroys it before this plugin.
/// The editor registers and deregisters itself through EditorHost, which is why
/// this does not have to wrap it -- SpectrumWorxEditor is final anyway.
/// \note Wrapped, so the plugin shows the editor scaled: the editor lays itself
/// out in skin pixels and ZoomedEditor is what carries the transform and what
/// answers for size. Anything that wants 1:1 -- the test harness does --
/// constructs a SpectrumWorxEditor and skips the wrapper.
std::unique_ptr<juce::Component> SpectrumWorxCLAP::createEditor()
{
    return std::make_unique<GUI::ZoomedEditor>(std::make_unique<GUI::SpectrumWorxEditor>(*this));
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Brings the main thread's copy of the three spectral parameters back to
/// what the engine actually settled on, and says so. `[main-thread]`
///
/// \note For the one path where the engine declines a value it was given.
/// `updateEngineSetup()` reallocates the whole spectral working set, and when
/// that fails it puts the FFT size and the overlap factor back the way they
/// were -- in the Program it can reach, which is the engine's. The main thread's
/// copy and the host went on holding the value the user asked for, so a size the
/// machine could not allocate read back as though it had been applied: the
/// parameter said 8192, the engine ran 2048, `stateSave` wrote 8192, and
/// reopening the session tried the same allocation again.
///
/// \note A rescan rather than a message box. The user asked for something and
/// did not get it, which is worth showing -- the interface shows it, by reading
/// the value that is really in force -- but it is not worth interrupting a host
/// for, and this can run inside `deactivate()`.
///                                           (08.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCLAP::resyncSpectralParametersToEngine()
{
    using namespace GlobalParameters;

    auto const &engineParameters(parameters());
    auto &mainParameters(programMain_.parameters());

    mainParameters.set<FFTSize>(engineParameters.get<FFTSize>());
    mainParameters.set<OverlapFactor>(engineParameters.get<OverlapFactor>());
    mainParameters.set<WindowFunction>(engineParameters.get<WindowFunction>());

    requestRescan(CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
    if (pEditor_)
        pEditor_->updateForGlobalParameterChange();
}

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxCLAP::addHostParameterEntries()
// -------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note `clap_id` and `ParameterID::binaryValue` are the same number -- see
/// paramsInfo(), which writes one straight into the other. The helper asks the
/// host for its extension and adds nothing when there is none, which is why
/// there is no test for one here.
///                                           (15.08.2026.)
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCLAP::addHostParameterEntries(ParameterID const parameterID,
                                               juce::PopupMenu &menu) const
{
    sst::clap_juce_shim::populateMenuForClapParam(menu, parameterID.binaryValue, _host.host());
}

void SpectrumWorxCLAP::editorOpened(GUI::SpectrumWorxEditor &editor) { pEditor_ = &editor; }

////////////////////////////////////////////////////////////////////////////////
///
/// \note Only if it is the editor this plugin knows about, which is why the
/// editor now says which one it is. `guiCreate` and `guiDestroy` do not have to
/// balance the way a window's lifetime does -- a host may create a GUI, never
/// parent it and destroy it, and the shim can outlive that call -- so two
/// editors can exist at once for a moment. Clearing unconditionally meant the
/// *old* one going away turned off the rack resyncs and the automation
/// notifications for the *new* one, and nothing said so: the window simply
/// stopped following the engine.
///                                           (08.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCLAP::editorClosed(GUI::SpectrumWorxEditor &editor)
{
    if (pEditor_ == &editor)
        pEditor_ = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxCLAP::requestEditorSize()
// -------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   What the editor calls when it wants a column for its preset browser or its
/// settings panel, and again when it gives one back.
///
/// \note This is not `can_resize`, which stays false: the user may not drag the
/// window, and there is nothing here for `adjust_size` to negotiate. Both facts
/// are the shim's and unchanged -- a plugin may ask for one particular size
/// whether or not its editor is resizable (ext/gui.h:35-45).
///
/// \note A host that offers no `clap.gui`, or one that says no, gets a false
/// back and the editor lays the panel over the module strips instead. That is
/// the only handling this needs: nothing here is a failure worth telling the
/// user about, and a warning about a mechanism working as specified is noise.
///                                           (06.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

bool SpectrumWorxCLAP::requestEditorSize(int const width, int const height)
{
    LE_ASSERT(Threading::isMainThread() || !Threading::isAudioThread());

    if (!_host.canUseGui())
        return false;

    ///   Scaled, because the editor asks in skin pixels -- it does not know it
    /// is being drawn zoomed -- and every size crossing this boundary is in the
    /// host's window units. The same factor ZoomedEditor uses, or the window
    /// and the column it was opened for disagree by exactly the zoom.
    auto const requestedWidth(static_cast<std::uint32_t>(GUI::ZoomedEditor::scaled(width)));
    auto const requestedHeight(static_cast<std::uint32_t>(GUI::ZoomedEditor::scaled(height)));
    if (!_host.guiRequestResize(requestedWidth, requestedHeight))
        return false;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And the shim is told as well, because a host that has accepted
    /// "doesn't have to call set_size()" (ext/gui.h:221). The two clap-wrapper
    /// formats do -- VST3 goes back round through `IPlugFrame::resizeView` --
    /// so this is for the host that does not: without it the JUCE side keeps the
    /// old window size and the new column is clipped away by the holder. Both
    /// paths land on the same `desktop()->setSize()`, so a host that does call
    /// it is a second write of the size it already is.
    ///
    ////////////////////////////////////////////////////////////////////////////
    if (clapJuceShim_ && clapJuceShim_->isEditorAttached())
        clapJuceShim_->guiSetSize(requestedWidth, requestedHeight);
    return true;
}

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxCLAP::setNewSample()
// --------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note `[main-thread]`, and synchronous: it decodes the whole file here and
/// publishes the result, which is why it needs no lock (doc/tech/threading_model.md
/// §5). An MP3 of the size the factory samples are is single-digit milliseconds;
/// a long file the user picks is not, and stalling the message thread is the cost
/// of not having a loader thread. That is a deliberate deferral -- see the note on
/// the declaration -- and not something to fix here: the answer is a main-thread
/// work queue with a completion the editor can be told about, and building one for
/// the loader alone would be building it twice.
///
///   Two things the 2016 worker did that are gone with the buffers they served:
/// InputBuffers::forceSideChannel() and a resize() around the load. activate()
/// asks for two main and two side channels outright, so the side buffers this
/// writes into are already there whether a sample is loaded or not.
///
////////////////////////////////////////////////////////////////////////////////

char const *SpectrumWorxCLAP::decodeAndPublishSample(fs::path const &sampleFile)
{
    LE_ASSERT(Threading::isMainThread() || !Threading::isAudioThread());

    if (sampleFile.empty())
    {
        publishSample(nullptr);
        return nullptr;
    }

    /// \note This plugin's own rate rather than the engine's, and zero is a
    /// legal answer: a host can restore a session -- sample and all -- before it
    /// has ever activated, and Sample::load() reads a file at its own rate when
    /// it is given no other. activate() then re-reads it. Refusing would be the
    /// alternative, and it would silently lose the sample.
    auto const rate(static_cast<unsigned int>(sampleRate_));
    auto pNewSample(std::make_unique<Sample>());
    auto const *const pErrorMessage(pNewSample->load(sampleFile, rate));
    if (pErrorMessage)
        return pErrorMessage;

    publishSample(pNewSample.release());
    return nullptr;
}

char const *SpectrumWorxCLAP::setNewSample(fs::path const &newSampleFile)
{
    auto const *const pErrorMessage(decodeAndPublishSample(newSampleFile));
    if (pErrorMessage)
        return pErrorMessage;

    /// \note And now it *is* dirty. This said "deliberately no
    /// markCurrentProgramAsModified()" until 02.08.2026, because the state was
    /// `(id, value)` pairs and could not hold a file name, so telling a host the
    /// session had changed would have promised to remember something the format
    /// could not. State is the preset serialisation now and `<p n="Sample">` has
    /// been in that since 2011, so the promise is one this can keep.
    markCurrentProgramAsModified();
    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Hands \p pNewSample (owned, null clears) to whoever owns the engine.
///
/// \note The sample's half of §5, and the same shape as the chain's: swap a
/// pointer where the engine is, destroy the old one where destroying is allowed.
/// The main thread keeps its own record of the file and the rate it decoded at,
/// because those are questions the interface asks with audio running.
///                                           (02.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
/// \note The bookkeeping goes after the handover, not before it.
///
///   `sampleFile_` and `decodedSampleRate_` are the main thread's record of what
/// the engine is playing, and `stateSave` writes the first of them. They were
/// written at the top of this function, before the push that can fail -- so a
/// dropped sample load left the session naming a file the engine never received,
/// and reopening that session would load it as though it had always been there.
///
///   The same shape as `publishSlot` and `publishChain`, which already undo
/// their own half on a refusal.
///                                           (08.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCLAP::publishSample(Sample *const pNewSample)
{
    auto const recordWhatTheEngineHasNow([&] {
        sampleFile_ = pNewSample ? pNewSample->sampleFile() : fs::path();
        decodedSampleRate_ = pNewSample ? pNewSample->sampleRate() : 0;
    });

    if (!engineIsRunning())
    {
        delete std::exchange(pSample_, pNewSample);
        clearSideChannelData();
        recordWhatTheEngineHasNow();
        return;
    }

    if (pushed(toEngine_.push(Threading::swapSample(pNewSample)),
               "The command queue is full; a sample load was dropped."))
    {
        recordWhatTheEngineHasNow();
        return;
    }

    delete pNewSample;
}

bool SpectrumWorxCLAP::registerOrUnregisterTimer(clap_id &id, int const milliseconds,
                                                 bool const registering)
{
    if (!_host.canUseTimerSupport())
        return false;
    if (registering)
        _host.timerSupportRegister(milliseconds, &id);
    else
        _host.timerSupportUnregister(id);
    return true;
}

bool SpectrumWorxCLAP::registerOrUnregisterPosixFd(int const fd, clap_posix_fd_flags_t const flags,
                                                   bool const registering)
{
    if (!_host.canUsePosixFdSupport())
        return false;
    return registering ? _host.posixFdSupportRegister(fd, flags)
                       : _host.posixFdSupportUnregister(fd);
}

} // namespace LE::SW
