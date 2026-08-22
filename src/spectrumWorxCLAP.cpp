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
// finalImplementations.hpp defines Module::Impl<> and needs Module complete
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/modules/finalImplementations.hpp"

// the interop .inl templates call through to a module's UI on a slot change
#include "gui/modules/moduleUI.hpp"

#include "core/host_interop/clapParameterEdge.hpp"
#include "core/host_interop/host2PluginImpl.inl"
#include "core/host_interop/plugin2HostImpl.inl"
#include "core/host_interop/programWrite.hpp"
#include "core/threading/publish.hpp"

#include "core/threading/threadCheck.hpp"

#include "gui/gui.hpp" // warningMessageBox()

// loadPreset() takes the editor by pointer so that this can call it with none
// \see doc/tech/streaming_format.md
#include "gui/editor/presetLoading.hpp"
#include "io/jucePath.hpp"
#include "le/spectrumworx/presetStorage.hpp"
#include "le/spectrumworx/presetStorage.hpp" // maximumPresetSize

#include "le/math/vector.hpp" // Math::copy(), for the sample's wrap

#include <clapwrapper/auv2.h> // the AUv2 parameter ordering, \see issue #159

#include <sst/clap_juce_shim/menu_helper.h> // the host's own parameter menu
#include <sst/plugininfra/cpufeatures.h>
#include <sst/plugininfra/version_information.h>

#include <algorithm>
#include <numeric>
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
/// \note The whole stream before any of it is parsed: the document is not
/// self-delimiting, so a host may hand back any number of bytes.
///
/// \note A `read` of 0 is the end and a negative is an error. A truncated read
/// reported as failure would be indistinguishable from an empty state.
///
////////////////////////////////////////////////////////////////////////////////

std::optional<std::vector<char>> readWholeStream(clap_istream const *const stream)
{
    constexpr std::size_t chunk{1u << 12};

    std::vector<char> buffer;
    std::size_t used(0);
    for (;;)
    {
        // a stream that never says it is done would grow this until the
        // allocation threw, and stateLoad is noexcept
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
///
/// \brief One block of a looped sample channel, advancing \p position past it.
///
/// \return the sample's own data where a whole block is contiguously available,
/// which is every block but the one that wraps; \p workBuffer, filled, where it
/// is not. So the common case costs nothing and only the wrap copies.
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

/// \note The strings come from src/CMakeLists.txt, because the bundle
/// identifiers are made of the same ones. SW_CLAP_ID is also the VST3 class id,
/// by way of a SHA-1 in clap-wrapper.
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
    // deactivate() empties this, but an editor open on a deactivated plugin
    // goes on filling it; one never activated saw no deactivate() at all
    discardQueuedCommands();

    // last chance to free what the audio thread handed back
    drainEngineEvents();
    delete pSample_;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Frees what the command queue still carries, without applying any of
/// it. `[main-thread]`
///
/// \note Discarded rather than drained, which is the difference between this and
/// `drainCommands()`: applying a slot change would call `chainChanged()` into a
/// host midway through `clap_plugin::destroy`.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCLAP::discardQueuedCommands()
{
    Threading::ToEngine command;
    while (toEngine_.pop(command))
    {
        switch (command.kind)
        {
        // one reference, transferred with the message; null empties a slot
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
            break;
        }
    }
}

bool SpectrumWorxCLAP::init() noexcept
{
    // clap_plugin::init is [main-thread] by contract, and clap.thread-check is
    // an optional extension a host need not offer
    Threading::markMainThread();

    // a prefix, not a match: clap-wrapper appends " (CLAP-as-VST3)" to the name
    isArdour_ = _host.host()->name && (std::strncmp(_host.host()->name, "Ardour", 6) == 0);

    // The host may ask for the parameter list before activate(), and does.
    rebuildParameterIDs();
    return true;
}

bool SpectrumWorxCLAP::activate(double const sampleRate, std::uint32_t,
                                std::uint32_t const maxFrames) noexcept
{
    // clap-helpers guards a double activate with an assert alone, and
    // initialise() would reallocate the spectral working set under a live
    // process(). The rate asked for here is the rate already running: a
    // differing one arrives with engineRunning_ already false.
    if (engineRunning_)
        return true;

    // a host that restarts the plugin need not call on_main_thread in between
    drainEngineEvents();

    sampleRate_ = sampleRate;

    // input and output counts: the engine takes the side channels to be the
    // difference, so stereo main and stereo side spells four against two
    setNumberOfChannels(4, 2);
    setSampleRate(static_cast<float>(sampleRate));

    // the host promises never to exceed maxFrames; a shorter block is fine
    setBlockSize(maxFrames);

    if (!initialise())
    {
        // the engine rolled back, and the rest of the world has to be told
        resyncSpectralParametersToEngine();
        return false;
    }

    // a session restores its sample before the host names a rate, so zero is
    // "Sample::load was given no rate", not "no sample loaded"
    //
    // before resume(), so the swap is the direct one rather than a queued command
    if (!sampleFile_.empty() && (decodedSampleRate_ != static_cast<unsigned int>(sampleRate)))
    {
        // no dialog, unlike the menu's load: a modal box in activate() stops
        // the host mid-restore, and no user asked for this. \see issue #12
        [[maybe_unused]] auto const *const pErrorMessage(decodeAndPublishSample(sampleFile_));
        LE_ASSERT_MSG(!pErrorMessage, "A sample that loaded once did not load again.");
    }

    resume();
    engineRunning_ = true;

    // clap_host_latency::changed is [being-activated]: announcing it while
    // deactivating re-enters Ardour's non-recursive lock. \see issue #172
    //
    // not on the first activation -- the host reads latencyGet() as part of
    // activating, and zero cannot mean "told nothing" because it is a real
    // latency, the one an overlap factor of one has
    auto const previousLatency(std::exchange(latencyInSamples_, engineSetup().latencyInSamples()));
    if (hostKnowsLatency_ && (latencyInSamples_ != previousLatency) && _host.canUseLatency())
        _host.latencyChanged();
    hostKnowsLatency_ = true;

    // a plugin brought up mid-transport counts its first block as a start
    transportWasPlaying_ = false;

    // knobs built before the engine had a sample rate could not derive the
    // ranges that quantise to a step time or a bin width; now they can
    if (pEditor_)
        pEditor_->updateForEngineSetupChanges();

    return true;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The only place a pending spectral setup may land: FFT size, overlap
/// factor and window function reallocate the whole working set, and with the
/// plugin deactivated there is no audio thread to race. It is also the one
/// point at which `clap_plugin_latency` allows the latency to change, and the
/// FFT size *is* the latency. \see drainCommands(), threading_model.md §5.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCLAP::deactivate() noexcept
{
    if (engineRunning_)
    {
        suspend();
        engineRunning_ = false;
    }

    // suspend() is above, so this thread owns the engine and the commands
    // apply here and now, exactly as process() would have applied them
    //
    // before applyPendingSpectralSetup(): a preset that moves the FFT size
    // queues its chain, and the resize must see the new one or the modules it
    // splices in stay sized for the old FFT and overrun their blocks
    drainCommands();
    drainEngineEvents();

    // the restart arrived after all. \see the fallback in process()
    blocksAwaitingRestart_ = 0;

    // cleared before the setup is applied: a later ask is about a later change
    restartRequested_.store(false, std::memory_order_release);
    if (spectralSetupPending())
    {
        // updateEngineSetup() rolls the FFT size and overlap factor back in
        // the engine's Program, the only one it can see
        if (!applyPendingSpectralSetup())
            resyncSpectralParametersToEngine();

        // the new latency is applied, not announced; activate() tells the host
        if (pEditor_)
            pEditor_->updateForEngineSetupChanges();
    }

    sampleRate_ = 0;
}

/// \note `reset()` is `[audio-thread & active]` (plugin.h:89) but not under
/// `process()` -- a host calls it between blocks to throw away the tail -- so it
/// owns the engine while it runs and has to say so.
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
    // never an in-place pair: at unity input gain the host's own input
    // pointers reach Engine::Processor::process, and the WOLA path has not been
    // audited for aliasing input against output
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
        std::strncpy(info->name, "Sidechain", CLAP_NAME_SIZE - 1);
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

/// \brief Every parameter the engine can ever have, once, at init().
///
/// \note Passing nullptr rather than the Program asks for the *maximal* list --
/// every slot's full complement of module and LFO parameters -- and it has to be
/// maximal because CLAP does not let a plugin change its parameter count while
/// active: adding or removing means restart() and a deactivate() before
/// CLAP_PARAM_RESCAN_ALL, which a host can be swapping a slot's effect mid-block.
///
/// \note Nothing is lost by declaring them all. A parameter belonging to a slot
/// whose effect does not have it reads as N/A rather than as an unknown ID, so a
/// host's automation lane stays attached across an effect swap.
void SpectrumWorxCLAP::rebuildParameterIDs()
{
    parameterIDs_.resize(numberOfParameters(nullptr));
    getParameterIDs({parameterIDs_.data(), parameterIDs_.size()}, nullptr);
    LE_ASSERT(parameterIDs_.size() == ParameterCounts::maxNumberOfParameters);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Every field, not just the discriminator, and this is the only place it
/// happens: all four host entry points that take a raw `clap_id` come through
/// here, and everything downstream assumes they did. The indices reach
/// `invokeFunctorOnIndexedParameter`, whose jump tables are guarded by nothing
/// stronger than LE_ASSUME -- a `__builtin_assume` in release, so an index one
/// past the end is an out-of-bounds read followed by an indirect call through
/// whatever it found, on the audio thread for the event route. A `clap_id` is
/// host-supplied data.
///
/// \note The padding bytes are checked for the types that have them, so that two
/// different `clap_id`s cannot name one parameter.
///
/// \note Every ParameterID that *decodes* is still valid: the model answers
/// "N/A" for a slot whose effect does not have that parameter rather than
/// pretending the id is unknown, which is what keeps a host's automation lane
/// attached across an effect swap.
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
        // one fewer than a module has parameters: the first is Bypass, undriven
        auto const &lfo(parameterID.value._.lfo);
        return (lfo.moduleIndex < Constants::maxNumberOfModules) &&
               (lfo.moduleParameterIndex < Constants::maxNumberOfParametersPerModule - 1) &&
               (lfo.lfoParameterIndex < ParameterCounts::lfoExportedParameters);
    }
    }

    // no LE_DEFAULT_CASE_UNREACHABLE(): the discriminator is a byte off the
    // wire and only four of its 256 values are parameters
    return false;
}

/// \note One, and it is clap-wrapper's rather than CLAP's: an AUv2 host keys
/// automation on where a parameter *sits*, so the order is stated rather than
/// left to fall out of the ids. \see auv2ParameterOrder()
namespace
{
clap_plugin_auv2_param_ordering_t const auv2ParameterOrdering{
    [](clap_plugin_t const *const plugin, std::size_t *const order,
       std::size_t const parameterCount) -> bool {
        return static_cast<SpectrumWorxCLAP const *>(plugin->plugin_data)
            ->auv2ParameterOrder(order, parameterCount);
    }};
} // anonymous namespace

void const *SpectrumWorxCLAP::extension(char const *const id) noexcept
{
    if (std::strcmp(id, CLAP_PLUGIN_AUV2_PARAM_ORDERING) == 0)
        return &auv2ParameterOrdering;
    return nullptr;
}

/// \brief The AUv2 parameter order: what shipped first, in the order it shipped
/// in, and what was added since after it.
///
/// \note `order[auv2Position] = clapIndex`, which is how clap-wrapper reads it.
///
/// \note By id within a release rather than by whatever order `parameterIDs_`
/// is in: the wrapper's default without this extension *is* the id order, so
/// saying it explicitly is what makes the version-zero block provably the layout
/// that shipped rather than one that merely looks like it.
bool SpectrumWorxCLAP::auv2ParameterOrder(std::size_t *const order,
                                          std::size_t const parameterCount) const noexcept
{
    if (parameterCount != parameterIDs_.size())
        return false;

    std::iota(order, order + parameterCount, std::size_t(0));

    std::sort(order, order + parameterCount, [this](std::size_t const a, std::size_t const b) {
        auto const idA(parameterIDs_[a].value);
        auto const idB(parameterIDs_[b].value);

        auto const versionA(CLAPEdge::parameterVersion(ParameterID{parameterIDs_[a]}));
        auto const versionB(CLAPEdge::parameterVersion(ParameterID{parameterIDs_[b]}));

        return (versionA != versionB) ? (versionA < versionB) : (idA < idB);
    });
    return true;
}

std::uint32_t SpectrumWorxCLAP::paramsCount() const noexcept
{
    // the list built at init(); this count is fixed for the plugin's lifetime
    return static_cast<std::uint32_t>(parameterIDs_.size());
}

bool SpectrumWorxCLAP::paramsInfo(std::uint32_t const index,
                                  clap_param_info *const info) const noexcept
{
    if (index >= parameterIDs_.size())
        return false;

    auto const id(parameterIDs_[index]);
    ParameterID const parameterID{id};

    // two queries: the fixed description, over a null Program, supplies every
    // number and flag a host may not see move -- min, max, is_stepped, all in
    // the RESCAN_ALL list -- and the live one only what RESCAN_INFO covers
    Plugins::ParameterInformation<Protocol> fixed;
    getParameterRanges(parameterID, fixed, nullptr);

    Plugins::ParameterInformation<Protocol> live;
    getParameterProperties(parameterID, live, &programMain_);

    std::memset(info, 0, sizeof(*info));
    info->id = id.value;
    info->cookie = nullptr;
    // all but the three that rebuild the spectral setup, each of which ends a
    // change made while active in a request_restart
    info->flags = CLAPEdge::isAutomatable(parameterID) ? CLAP_PARAM_IS_AUTOMATABLE : 0;

    // nothing here is ever CLAP_PARAM_IS_HIDDEN. clap-wrapper maps flags once
    // at construction and VST3 re-reads them only under RESCAN_ALL, illegal
    // while active, so hiding the unowned would freeze an empty instance's
    // eleven automatable rows out of 286 for the life of it
    //
    // and never CLAP_PARAM_REQUIRES_PROCESS: it says a change must go through
    // process() (ext/params.h:196), which forbids the flush() route a slot
    // change most needs, and paramsFlush() applies one just as process() does
    if (auto const choices = CLAPEdge::choiceCount(parameterID); choices != 0)
    {
        // a real stepped range where every other module and LFO parameter hides
        // behind 0..1: these two are the plugin's own choices rather than the
        // slot effect's, so the count never moves
        info->min_value = 0;
        info->max_value = choices - 1;
        info->default_value = CLAPEdge::defaultToHost(parameterID, fixed);
        info->flags |= CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
    }
    else if (CLAPEdge::isNormalised(parameterID))
    {
        // a 0..1 edge over a natural range the effect owns. No
        // CLAP_PARAM_IS_STEPPED either: a step count is a property of whichever
        // effect the slot holds, and that flag is in the same RESCAN_ALL list
        info->min_value = 0;
        info->max_value = 1;
        info->default_value = CLAPEdge::defaultToHost(parameterID, fixed);
    }
    else
    {
        // global and slot-selector ranges: the plugin owns them, so they never
        // move and keep their real values and steps
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
        std::snprintf(path, CLAP_PATH_SIZE, "Module %u",
                      parameterID.value._.moduleChain.moduleIndex + 1u);
        break;
    case ParameterID::ModuleParameter:
        std::snprintf(path, CLAP_PATH_SIZE, "Module %u",
                      parameterID.value._.module.moduleIndex + 1u);
        break;
    case ParameterID::LFOParameter:
        std::snprintf(path, CLAP_PATH_SIZE, "Module %u/LFO",
                      parameterID.value._.lfo.moduleIndex + 1u);
        break;
    }
}

/// \note The *live* range, not the fixed one paramsInfo advertises: normalising
/// expresses a value belonging to the slot's current effect on an edge that does
/// not.
///
/// \note The scratch description and the Program both come from the caller. The
/// four call sites run on three different threads, so one shared member would be
/// a race, and each thread reads the Program copy it owns.
bool SpectrumWorxCLAP::liveRanges(ParameterID const parameterID,
                                  Plugins::ParameterInformation<Protocol> &ranges,
                                  Program const &program)
{
    getParameterRanges(parameterID, ranges, &program);
    if (CLAPEdge::isPresent(ranges))
        return true;

    // an empty slot's range is a degenerate 0..0, which nothing can be
    // normalised against; fall back to the maximal description paramsInfo used
    getParameterRanges(parameterID, ranges, nullptr);
    return false;
}

bool SpectrumWorxCLAP::paramsValue(clap_id const id, double *const value) noexcept
{
    if (!isValidParamId(id))
        return false;

    ParameterID const parameterID{Plugins::ParameterID{id}};
    Plugins::ParameterInformation<Protocol> ranges;

    // a parameter no effect owns reads as its advertised default: `ranges` is
    // the maximal description by then, the same one paramsInfo used, so the two
    // agree by construction. A host checks this at init (param-default-values)
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

    // answers about \p value, not about the parameter's own: the caller is
    // usually an automation lane's tooltip asking "what would 0.25 read as"
    //
    // fromHost rather than the raw double, so the printer is given a natural
    // value on the edge paramsValue answers on -- and clamped, because a host
    // may ask about anything. \see CLAPEdge

    // a parameter no effect owns reads as `notAvailable`, and must read back:
    // clap-validator's param-conversions wants text_to_value for all the
    // automatable parameters or for none, and every ID here is automatable
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
/// \note `Parameters::DisplayValueTransformer::inverse` undoes the transform per
/// parameter, and `Parameters::parse` answers `nothing` for text that is not a
/// value -- rather than the zero `strtod` answers -- and clamps what it does
/// answer to the parameter's own range. tests/clap/parameterTextTests.cpp holds
/// every parameter to the round trip.
///
/// \note `programMain_`, and the *live* ranges over it, because the units the
/// text is in belong to whichever effect the slot currently holds.
bool SpectrumWorxCLAP::paramsTextToValue(clap_id const id, char const *const display,
                                         double *const value) noexcept
{
    if (!isValidParamId(id) || !display)
        return false;

    ParameterID const parameterID{Plugins::ParameterID{id}};

    Plugins::ParameterInformation<Protocol> ranges;

    // one display and one value -- `notAvailable` and the default paramsValue
    // answers with -- which is what keeps text_to_value answerable for every
    // automatable parameter. \see paramsValueToText
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
    // dropped when no effect in that slot owns it: the list is maximal, so a
    // host writes to every ID in it, and setAutomatedLFOParameter has no guard
    // and indexes past the end of module.lfo(). The value has nowhere to live
    // anyway -- filling the slot later brings the new effect's own default
    //
    // the engine's Program, this being [audio-thread]; the three [main-thread]
    // callers of liveRanges() read programMain_
    if (!liveRanges(parameterID, ranges, program()))
        return false;

    // what the slot held before the write, read from the chain rather than
    // inferred from \p value: setParameter declines a slot change it cannot
    // carry out and leaves the slot holding what it already had
    bool const isSlotSelector(parameterID.type() == ParameterID::ModuleChainParameter);
    auto const slot(parameterID.value._.moduleChain.moduleIndex);
    auto const effectBefore(isSlotSelector ? moduleChain().getParameterForIndex(slot).getValue()
                                           : noModule);

    auto const value(CLAPEdge::fromHost(parameterID, ranges, event->value));
    auto const applied(setParameter(parameterID, value));

    // the host moving a parameter is an edit; drainCommands() applying what the
    // interface queued is not, the interface having already reported it
    markCurrentProgramAsModified();

    // the main thread's copy of the same state, and not gated on there being an
    // editor: paramsValue and stateSave read programMain_ with the window shut
    //
    // only when the engine took it -- setParameter declines a slot selector
    // that would leave a hole in the rack
    //
    // a dropped echo leaves programMain_ behind the engine for that parameter
    // permanently, this being the only thing that carries a host's write across
    if (applied == Plugins::ErrorCode<Protocol>::Success)
        pushed(toUI_.push(Threading::baseParameterChanged(parameterID.binaryValue, value)),
               "The echo queue is full; the main thread's Program is now behind the engine.");

    // only a module-chain parameter changes what the *other* parameters are: it
    // decides which effect a slot holds, and so how many that slot has and what
    // they are called
    //
    // and only when the slot actually moved. chainChanged() asks for a
    // RESCAN_INFO, which Ardour answers by writing the whole parameter set back
    // from inside restartComponent -- slot included, which would ask again.
    // \see issue #172
    auto const effectAfter(isSlotSelector ? moduleChain().getParameterForIndex(slot).getValue()
                                          : noModule);

    return isSlotSelector && (effectBefore != effectAfter);
}

void SpectrumWorxCLAP::requestRescan(clap_param_rescan_flags const flags)
{
    // one callback per outstanding batch: a block that swaps every slot would
    // otherwise ask the host five times over
    if (pendingRescan_.fetch_or(flags) == 0)
        _host.requestCallback();
}

void SpectrumWorxCLAP::paramsFlush(clap_input_events const *const in,
                                   clap_output_events const *const out) noexcept
{
    // the conditional one: clap_plugin_params::flush is
    // [active ? audio-thread : main-thread] (ext/params.h:303), the only entry
    // point whose owning thread depends on state. Taking the scope
    // unconditionally would tell an inactive plugin's engine that the audio
    // thread owns it while the main thread does
    std::optional<Threading::ScopedAudioThreadEntry> audioThread;
    if (isActive())
        audioThread.emplace();

    drainCommands();

    auto const size(in->size(in));
    bool effectChanged(false);
    for (std::uint32_t event(0); event < size; ++event)
        effectChanged |= handleEvent(in->get(in, event));

    if (effectChanged)
        chainChanged(ChainChange::userEdited);

    // on an inactive plugin nothing else is coming: no audio thread, no
    // process(), and for a host that restores a session without ever opening a
    // window no callback before stateSave() is asked for the state
    if (!isActive())
        drainEngineEvents();

    flushUIEdits(out);
}

clap_process_status SpectrumWorxCLAP::process(clap_process const *const process) noexcept
{
    // FTZ and DAZ via MXCSR on x86-64, FZ via FPCR on aarch64, and the caller's
    // state restored on the way out. Here rather than in runEngine() because
    // event handling and flushUIEdits() convert parameter values too, and all
    // four formats funnel through this one entry point
    sst::plugininfra::cpufeatures::FPUStateGuard const denormalGuard;

    // makes Threading::isAudioThread() true below, and opens a
    // RealtimeSanitizer realtime region so an allocation, a lock or a syscall
    // under here is reported with a stack. Both compile away without the flag
    Threading::ScopedAudioThreadEntry const audioThread;

    // before the host's own events, so the host's block wins over anything the
    // interface queued before the block started
    drainCommands();

    // the restart that never came: Ardour answers restartComponent( kIoChanged
    // | kLatencyChanged ) by re-reading the latency with the plugin still
    // active, so this thread is the only one that can apply a pending setup
    //
    // it allocates, hence both guards -- Ardour only, and only with the
    // transport parked, where the click lands on monitoring rather than a take.
    // \see issue #172 and tracker.ardour.org/view.php?id=10470

    // parked blocks to wait first, so a host that would restart still can
    static constexpr std::uint32_t blocksBeforeGivingUpOnTheRestart{4};

    bool const transportRolling(process->transport &&
                                ((process->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0));

    if (isArdour() && !transportRolling && spectralSetupPending() &&
        restartRequested_.load(std::memory_order_acquire))
    {
        if (++blocksAwaitingRestart_ >= blocksBeforeGivingUpOnTheRestart)
        {
            auto const applied(applyPendingSpectralSetup());

            blocksAwaitingRestart_ = 0;
            restartRequested_.store(false, std::memory_order_release);
            appliedWithoutARestart_.store(applied ? WithoutARestart::Applied
                                                  : WithoutARestart::Failed,
                                          std::memory_order_release);

            _host.requestCallback(); // `[thread-safe]`; the announcement is not.
        }
    }
    else
    {
        blocksAwaitingRestart_ = 0;
    }

    // the block is rendered in pieces and a parameter event takes effect at the
    // piece its timestamp falls in; clap_event_header::time is a sample offset
    //
    // the piece is the hop -- fftSize / overlapFactor -- rather than the event:
    // a spectral effect only ever acts on whole frames, so quantising to the hop
    // discards nothing, while splitting per event would make the engine call
    // count a function of how busy the host's automation is
    //
    // events apply when they come due -- time <= cursor -- so a value never
    // takes effect before the sample the host asked for. The remainder goes
    // after the last piece: those events belong to the boundary itself

    auto const *const events(process->in_events);
    auto const numberOfEvents(events ? events->size(events) : 0);
    std::uint32_t nextEvent(0);
    bool effectChanged(false);

    auto const applyEventsDueAt([&](std::uint32_t const sample) {
        while ((nextEvent < numberOfEvents) && (events->get(events, nextEvent)->time <= sample))
            effectChanged |= handleEvent(events->get(events, nextEvent++));
    });

    restartSampleOnTransportStart(process->transport);

    // the LFO clock advances once per piece rather than once for the block: a
    // piece is one hop, the rate the engine samples an LFO at, and so the finest
    // resolution the clock can usefully have. \see issue #78
    auto const chunk(engineChunkSize());
    for (std::uint32_t cursor(0); cursor < process->frames_count; cursor += chunk)
    {
        applyEventsDueAt(cursor);
        auto const piece(std::min(chunk, process->frames_count - cursor));
        updateLFOTiming(process, cursor, piece);
        runEngine(process, cursor, piece);
    }

    // every event timed at or past the end of the block, so the next one starts
    // from the state the host asked for
    applyEventsDueAt(process->frames_count);

    // names, module paths and displayed values change; the parameter list does
    // not, so this never needs the CLAP_PARAM_RESCAN_ALL an active plugin may
    // not send
    if (effectChanged)
        chainChanged(ChainChange::userEdited);

    if (process->out_events)
        flushUIEdits(process->out_events);

    // once after the block, rather than per LFO per module inside the engine
    publishModulatedValues();

    return CLAP_PROCESS_CONTINUE;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Sends the side chain's file back to its start when the host's
/// transport starts. \see issue #143.
///
/// \note It is what makes a bounce reproducible: the file is read straight
/// through and wrapped at its end, so the same project rendered twice did not
/// sound the same.
///
/// \note A rising edge rather than "while stopped": a user auditioning with the
/// transport parked still hears the file run on, which is the point of a looped
/// side chain.
///
/// \note And not a locate. Nothing ties the file's position to the song's --
/// `sidechain-approach.md` §2, a loop of audio fed into a channel rather than a
/// clip on the timeline -- so there is no position for a locate to move it to.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCLAP::restartSampleOnTransportStart(
    clap_event_transport const *const transport) noexcept
{
    bool const playing(transport && ((transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0));
    bool const started(playing && !transportWasPlaying_);

    transportWasPlaying_ = playing;

    // whatever the source, and whether or not the sample is being heard: a user
    // switching back to File mid-song should find it where the transport left it
    if (started && pSample_)
        pSample_->restart();
}

/// \brief Moves the LFO clock forward by one piece of the block.
///
/// \note Three cases, because a CLAP transport can be present and parked:
///
///   - Playing, on a beats timeline: follow the host, so an LFO is phase-locked
///     to song position and rides a locate or a loop rather than drifting.
///   - Tempo known but stopped, or a tempo with no beats timeline: keep the
///     host's tempo and meter and carry the phase forward from where the timer
///     already stands, so an LFO keeps running at the right rate with the
///     transport parked. Continuing from the timer rather than a counter of our
///     own is what makes the handover seamless in either direction.
///   - No transport at all, or a tempo we cannot use: free run at the engine's
///     assumed 120 BPM 4/4, which is exactly what `updatePosition()` is.
///
/// \note Both `updatePosition()` and the three-argument
/// `updatePositionAndTimingInformation()` call `handleTimingInformationChange()`
/// themselves; a second call here would run the period resnap twice.
void SpectrumWorxCLAP::updateLFOTiming(clap_process const *const process,
                                       std::uint32_t const offset,
                                       std::uint32_t const frames) noexcept
{
    auto const sampleRate(getSampleRate());
    if (sampleRate <= 0) [[unlikely]]
        return; // not activated; nothing sensible to advance by

    auto const *const transport(process->transport);

    constexpr std::uint32_t tempoAndMeter(CLAP_TRANSPORT_HAS_TEMPO |
                                          CLAP_TRANSPORT_HAS_TIME_SIGNATURE);

    // tsig_num reaches the engine as the measure numerator, a std::uint8_t it
    // divides by, so a zero or an out-of-range one is not a tempo we can use
    bool const usableTempo(transport && ((transport->flags & tempoAndMeter) == tempoAndMeter) &&
                           (transport->tempo > 0) && (transport->tsig_num >= 1) &&
                           (transport->tsig_num <= 255));
    // going from a host tempo to none is a timing change like any other -- the
    // bar goes back to the assumed two seconds -- so this arm reports it too
    if (!usableTempo)
    {
        if (updatePositionAndTimingInformation(frames).timingInfoChanged())
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
        // song_pos_beats is absolute, and it is the position of the *block*, so
        // without the piece's own offset the clock would snap back to the
        // block's start every piece and the LFO would stand still inside it
        positionInBars =
            (static_cast<double>(transport->song_pos_beats) / CLAP_BEATTIME_FACTOR) / beatsPerBar;
        positionInBars += (offset / static_cast<double>(sampleRate)) / barDuration;
        // a count-in puts the song before its own start; the timer asserts >= 0
        if (positionInBars < 0)
            positionInBars = 0;
    }
    else
    {
        // incremental rather than absolute, so it needs no offset: it carries
        // on from wherever the timer stands, by one piece
        auto const seconds(frames / static_cast<double>(sampleRate));
        positionInBars = lfoTimer().currentTimeInBars() + (seconds / barDuration);
    }

    if (updatePositionAndTimingInformation(static_cast<float>(positionInBars),
                                           static_cast<float>(barDuration),
                                           static_cast<std::uint8_t>(transport->tsig_num))
            .timingInfoChanged())
        timingChanged();
}

/// \see the note on the declaration.
std::uint32_t SpectrumWorxCLAP::engineChunkSize() const noexcept
{
    auto const &setup(uncheckedEngineSetup());
    auto const hop(setup.stepSize<std::uint32_t>());
    // never zero: a setup not yet built would turn the loop in process() into a
    // spin, and one whole block is honest when no frames are produced anyway
    return (hop > 0) ? hop : std::numeric_limits<std::uint32_t>::max();
}

void SpectrumWorxCLAP::runEngine(clap_process const *const process, std::uint32_t const offset,
                                 std::uint32_t const frames) noexcept
{
    if (frames == 0)
        return;

    if ((process->audio_inputs_count == 0) || (process->audio_outputs_count == 0))
        return;

    auto const &input(process->audio_inputs[0]);
    auto &output(process->audio_outputs[0]);
    if (!input.data32 || !output.data32)
        return; // a 64 bit host gets silence rather than a crash

    // unchecked, as getSampleRate() is: the channel count is not one of the
    // fields isEngineSetupUpToDate() compares, and this runs before the lock
    auto const channels(uncheckedEngineSetup().numberOfChannels());
    if ((input.channel_count < channels) || (output.channel_count < channels))
        return;

    if (!engineRunning_)
        return;

    // the engine reads a side channel whenever the input mode calls for one and
    // does not check that the host connected the port, so two arrangements fall
    // back to the main input: no second port in the count, and one with no data32
    //
    // beyond those, what the port holds is the host's answer, whatever it is.
    // Whether a port is connected is a routing fact for the host to state rather
    // than ours to infer from the samples: constant_mask is a hint, and an
    // unpatched port and a muted send are the same constant zero. \see issue #117
    //
    // whether the port is consulted at all is the patch's answer, through
    // SideChainSource -- the audio-file selector's, not a claim about bus
    // topology. Host is the only value that reads port 1.
    // \see doc/tech/sidechain-approach.md and issue #113
    //
    // sideChainSource_ is the engine's copy, set by drainCommands() in the same
    // message that swaps the sample, so a block is never rendered with a new
    // source and the sample the previous one named
    bool const hostSideChainWanted(sideChainSource_ == SideChainSource::Host);

    bool const hostSideChainReadable(hostSideChainWanted && (process->audio_inputs_count > 1) &&
                                     process->audio_inputs[1].data32);

    float const *const *sideChannels(hostSideChainReadable ? process->audio_inputs[1].data32
                                                           : input.data32);

    // the decoded audio file, when that is what the patch selected: the three
    // sources are exclusive, so this is a selection rather than a precedence
    //
    // pSample_ is tested rather than trusted. Nothing should be able to leave
    // File selected with no sample loaded -- both the setter and the preset
    // loader refuse it -- and a source the engine cannot honour falls back to
    // the main input rather than to a null dereference
    float const *sampleChannels[Sample::numberOfChannels];
    bool sideIsScratch(false);
    if ((sideChainSource_ == SideChainSource::File) && pSample_ && *pSample_ &&
        (channels <= std::size(sampleChannels)) && (buffers().numberOfSideChannels() >= channels) &&
        (frames <= buffers().blockSize()))
    {
        auto const startingPosition(pSample_->samplePosition());
        std::uint32_t position(startingPosition);
        for (std::uint8_t channel(0); channel < channels; ++channel)
        {
            // every channel reads the same span, so each starts where the last
            // one did and the advance is taken once
            position = startingPosition;
            sampleChannels[channel] = sampleChunk(pSample_->channel(channel), position, frames,
                                                  buffers().sideChannel(channel).begin());
        }
        pSample_->samplePosition() = position;
        sideChannels = sampleChannels;
        sideIsScratch = true;
    }

    // the host's buffers seen from \p offset: a call renders one chunk of the
    // block, so every pointer handed to the engine starts where this chunk does
    //
    // except the side chain when it is the decoded file -- sampleChunk has just
    // filled that scratch with this chunk's samples, so it already begins right
    std::array<float const *, Sample::numberOfChannels> mainAt;
    std::array<float const *, Sample::numberOfChannels> sideAt;
    std::array<float *, Sample::numberOfChannels> outAt;
    LE_ASSERT(channels <= mainAt.size());
    for (std::uint8_t channel(0); channel < channels; ++channel)
    {
        mainAt[channel] = input.data32[channel] + offset;
        sideAt[channel] = sideIsScratch ? sideChannels[channel] : sideChannels[channel] + offset;
        outAt[channel] = output.data32[channel] + offset;
    }

    SpectrumWorxCore::process(mainAt.data(), sideAt.data(), outAt.data(), 1.0f, frames);

    // ports beyond what the engine is configured for are the host's to see as
    // silence, not as whatever was in the buffer
    for (std::uint32_t channel(channels); channel < output.channel_count; ++channel)
        std::memset(output.data32[channel] + offset, 0, frames * sizeof(float));
}

void SpectrumWorxCLAP::onMainThread() noexcept
{
    drainEngineEvents();

    // the other half of the fallback in process(): resync for the failure case,
    // in the order deactivate() does it, and announce so Ardour hears
    switch (appliedWithoutARestart_.exchange(WithoutARestart::Nothing, std::memory_order_acq_rel))
    {
    case WithoutARestart::Nothing:
        break;

    case WithoutARestart::Failed:
        resyncSpectralParametersToEngine();
        [[fallthrough]];

    case WithoutARestart::Applied:
    {
        auto const previousLatency(
            std::exchange(latencyInSamples_, uncheckedEngineSetup().latencyInSamples()));
        if ((latencyInSamples_ != previousLatency) && _host.canUseLatency())
            _host.latencyChanged();

        if (pEditor_)
            pEditor_->updateForEngineSetupChanges();
        break;
    }
    }

    auto const flags(pendingRescan_.exchange(0));
    if (flags && _host.canUseParams())
        _host.paramsRescan(static_cast<clap_param_rescan_flags>(flags));

    // what the audio thread was not allowed to do itself
    if (pendingMarkDirty_.exchange(false) && _host.canUseState())
        _host.stateMarkDirty();

    PluginHelper::onMainThread();
}

// two rings and a mailbox, drained in two places; every edit an interface or a
// host makes crosses one of them. \see doc/tech/threading_model.md §3

/// \note Called from `process()` and from `paramsFlush()`: CLAP forbids a host
/// from running the two concurrently, so there is still exactly one consumer. It
/// matters that flush drains too -- a host with the transport parked may not be
/// calling `process()` at all.
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
            // the same entry point a host's parameter event reaches, in the
            // same units: an interface edit and a host-panel edit are one
            // operation arriving by two routes, applied on one thread
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
            chainChanged(ChainChange::userEdited);
            break;
        }

        case Threading::ToEngine::Kind::MoveModule:
            moveModule(command.moveModule.from, command.moveModule.to);
            chainChanged(ChainChange::userEdited);
            break;

        case Threading::ToEngine::Kind::SwapChain:
        {
            auto *const pIncoming(static_cast<AutomatedModuleChain *>(command.swapChain.pChain));
            swapModuleChain(*pIncoming);
            // the same object back, now holding what used to be live
            retire(Threading::ToUI::Retired::Chain, pIncoming);
            chainChanged(ChainChange::presetArrived);
            break;
        }

        case Threading::ToEngine::Kind::SwapSample:
        {
            // the source always; the sample only when one travelled, so
            // picking Main or Host leaves a loaded file where it is
            sideChainSource_ = static_cast<SideChainSource>(command.swapSample.source);
            if (!command.swapSample.replacesSample)
                break;
            auto *const pOutgoing(
                std::exchange(pSample_, static_cast<Sample *>(command.swapSample.pSample)));
            clearSideChannelData();
            if (pOutgoing)
                retire(Threading::ToUI::Retired::Sample, pOutgoing);
            break;
        }
        }
    }

    // once per batch rather than per command: a preset that moves the FFT size
    // and the overlap factor together is one restart, not two
    //
    // and only while there is an audio thread to defer on behalf of --
    // deactivate() drains through here and applies the setup itself
    if (engineIsRunning() && spectralSetupPending() &&
        !restartRequested_.exchange(true, std::memory_order_acq_rel))
        _host.requestRestart();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The one answer to "what happens when a ring is full".
///
/// \note It counts; it does not repair. A dropped echo, edit or gesture is
/// *gone*: the other side has already moved by the time the push fails, and the
/// ring was where the information to put it back would have been.
///
/// \note A counter and not an assertion, so it reads the same in a checked build
/// and a shipped one, and a case that fills a ring on purpose can measure rather
/// than abort.
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

/// \note A full retire ring is a leak, and freeing on this thread is the one
/// thing the ring exists to prevent. 1024 deep against one entry per structural
/// change, so it is a checked-build assertion rather than a policy.
///
/// \note The push is the statement and the assert only reports on it --
/// `LE_ASSERT_MSG` is `static_cast<void>(0)` under NDEBUG.
void SpectrumWorxCLAP::retire(Threading::ToUI::Retired const what, void *const pObject)
{
    if (toUI_.push(Threading::retire(what, pObject)))
        return;

    LE_ASSERT_MSG(false, "The retire queue is full; something will be leaked.");
}

/// \note And says the session needs saving. Whether it is an *edit* is what the
/// argument answers: a whole chain arriving is a preset, and only a slot filled,
/// emptied or moved is somebody changing the sound.
void SpectrumWorxCLAP::chainChanged(ChainChange const what)
{
    // dropping this leaves the rack drawing the chain that was there before,
    // with no second announcement coming: the drain is edge-triggered on it
    pushed(toUI_.push(Threading::chainChanged()),
           "The echo queue is full; the module rack will not be resynchronised.");
    requestRescan(CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_VALUES);

    // a whole chain arriving is not an edit: with audio running the chain a
    // preset load publishes is queued, and the audio thread installs it a block
    // after the browser has recorded the load and called it unedited
    if (what == ChainChange::userEdited)
        markCurrentProgramAsModified();
    else
        markSessionAsUnsaved();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note A synced LFO's period is a fraction of the *host's* bar, so a tempo or
/// meter change moves both the length the panel's number means and the grid the
/// period snaps to. It arrives as a message because the change is noticed on the
/// audio thread, where touching a widget is what the model forbids, and nothing
/// travels with it -- what changed is engine state the main thread may read.
///
/// \note Not gated on there being an editor: the gate is on the drain, where
/// `pEditor_` may be read at all. Asking here would read a main-thread member
/// from the audio thread to save a ring slot.
///
/// \note The coalescing is this message's alone -- \see `timingChangeQueued_`.
/// A push that fails clears it again, so a full ring costs one missed redraw
/// rather than every future one.
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

    // request_callback is [thread-safe], and without it the message would wait
    // for whatever asks next -- a tempo change on its own asks for nothing
    _host.requestCallback();
}

void SpectrumWorxCLAP::publishModulatedValues()
{
    // not gated on there being an editor: the loop is five modules by ten
    // enabled() checks, noise beside one FFT, and gating it would leave the one
    // thing that reads the mailbox as the only thing able to test it
    std::uint8_t slot(0);
    moduleChain().forEach<Module>([&](Module const &module) {
        auto const parameters(module.numberOfParameters());
        // from 1: Bypass has no LFO, and the LFO index is the parameter index
        // less it -- the same convention as ModuleParameters::lfo()
        for (std::uint8_t parameter(1); parameter < parameters; ++parameter)
        {
            if (!module.lfo(static_cast<std::uint8_t>(parameter - 1)).enabled())
                continue;

            ParameterID parameterID;
            parameterID.value.type = ParameterID::ModuleParameter;
            parameterID.value._.module = {ParameterID::Zero, parameter, slot};

            // the *live* value, which is the modulated one; what a host reads
            // is the unmodulated one and does not belong here
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

        // the copy first and the interface second, both unconditionally:
        // paramsValue and stateSave answer from programMain_ with the window
        // shut, and a strip that then redraws reads a copy that already agrees
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
            // coalesced: a preset that swaps the chain and then fills a slot is
            // one resync, and a resync recomputes rather than diffs
            chainChangedPending_ = true;
            break;

        // cleared here rather than after the redraw, so a tempo that moves
        // again during the drain is announced rather than swallowed
        case Threading::ToUI::Kind::TimingChanged:
            timingChangeQueued_.store(false, std::memory_order_relaxed);
            timingChangedPending = true;
            break;

        // the only place any of this is destroyed. A Module is one *reference*
        // rather than an object: the interface may still hold a strip pointing
        // at it, and dropping that strip is what finally frees it
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

    // after the rack, and only with a window: this is a redraw, and nothing
    // behind the interface depends on it
    if (timingChangedPending && pEditor_)
        pEditor_->updateForNewTimingInfo();
}

// the three editor entry points each do the same two things: apply the change to
// the Program this thread owns, and queue it for the engine

void SpectrumWorxCLAP::editParameter(ParameterID const parameterID, float const value) const
{
    auto &plugin(const_cast<SpectrumWorxCLAP &>(*this));
    setParameterIn<Protocol>(plugin.programMain_, parameterID, value);
    // applied above before the push is attempted, so a drop leaves the
    // interface and the saved session holding an edit the engine never heard
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
    // building it is synchronous and this thread's, so an effect the build does
    // not have is a failure the caller hears about here; only installing defers
    auto *const pModule(Threading::createModuleForSlot(*this, effectIndex, slot));
    if ((effectIndex != AutomatedModuleChain::noModule) && !pModule)
        return false;

    // this thread's copy, so the destroying overload is the right one
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

// what the editor tells the host, all of it queued rather than called: these run
// on the UI thread, and a host takes parameter changes only through the output
// event list it hands to process() or flush()

/// \note The editor works in the effect's own units throughout -- a knob knows
/// its parameter's real range -- so an edit it made is normalised here, on the
/// way out, and nowhere else.
void SpectrumWorxCLAP::HostProxy::automatedParameterChanged(
    ParameterSelector const parameter, Plugins::AutomatedParameterValue const value) const
{
    ParameterID const parameterID{parameter};
    Plugins::ParameterInformation<Protocol> ranges;
    // the main thread's copy: this is the editor's own edit, and it runs there
    liveRanges(parameterID, ranges, plugin_.programMain_);

    plugin_.pushed(plugin_.uiEdits_.push({parameter.value,
                                          static_cast<Plugins::AutomatedParameterValue>(
                                              CLAPEdge::toHost(parameterID, ranges, value)),
                                          UIEdit::Kind::Value}),
                   "The outgoing edit queue is full; the host was not told about an edit.");

    // the same rescan handleEvent() asks for when the *host* fills a slot: a
    // slot selector changes what the other parameters are called and what they
    // mean, and it can be moved from either side
    if (parameterID.type() == ParameterID::ModuleChainParameter)
        const_cast<SpectrumWorxCLAP &>(plugin_).requestRescan(
            CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_VALUES);

    plugin_.markCurrentProgramAsModified();
    plugin_.requestParameterFlush();
}

void SpectrumWorxCLAP::HostProxy::automatedParameterBeginEdit(
    ParameterSelector const parameter) const
{
    // a dropped gesture is the worst of these to leave silent: the pair has to
    // balance, and a lane that sees a begin without an end stays latched
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

/// \note `clap_host_params` is an *optional* extension, and clap-helpers'
/// `paramsRequestFlush()` asserts `canUseParams()` before calling through. A host
/// that offers no parameters still gets the queued edit; it simply is not told to
/// come and collect it.
void SpectrumWorxCLAP::requestParameterFlush() const
{
    if (!_host.canUseParams())
        return;
    const_cast<SpectrumWorxCLAP &>(*this)._host.paramsRequestFlush();
}

/// \note Nothing to announce up front: CLAP has no "hold on" call, and the
/// rescan at the other end is what a host acts on.
void SpectrumWorxCLAP::HostProxy::presetChangeBegin() const {}

/// \note INFO as well as VALUES and TEXT: a preset replaces the module chain, so
/// what the parameters are *called* and which module path they sit under both
/// move. The count does not, so this is legal while active, unlike RESCAN_ALL.
///
/// \note `[main-thread]`: reached from the editor's preset browser.
void SpectrumWorxCLAP::HostProxy::presetChangeEnd() const
{
    auto &plugin(const_cast<SpectrumWorxCLAP &>(plugin_));

    // only when the chain is already in, which with audio running it is not:
    // publishChain() queues it then, and drainCommands() raises chainChanged()
    // when it installs one -- from the side that knows it has happened rather
    // than the side that only knows it has asked
    if (!plugin.engineIsRunning())
    {
        // deferred, and coalescing, for the reason stateLoad() gives
        plugin.requestRescan(CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_VALUES |
                             CLAP_PARAM_RESCAN_TEXT);
        /// \note The host, and not the loaded preset's edited flag: this is the
        /// end of a *load*. \see chainChanged() and issue #177.
        plugin_.markSessionAsUnsaved();
    }

    // a preset that changes the FFT size sets the parameter on this thread and
    // leaves the setup where it is; this asks for the restart that applies it.
    // drainCommands() does the same for the queued route, which a knob takes
    if (plugin.spectralSetupPending() &&
        !plugin.restartRequested_.exchange(true, std::memory_order_acq_rel))
        plugin._host.requestRestart();

    // and "come and collect it": the globals travel by queue like every other
    // edit, and a preset makes no per-parameter notification, so it asks once
    // here for all of them. It matters most for the host with no audio thread to
    // drain them -- transport parked, no block coming, flush() is CLAP's answer
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

/// \note `clap_host_state.mark_dirty` is `[main-thread]` and this is reached
/// from both: the editor calls it on the UI thread, and a host parameter event
/// calls it from `process()`, the interop layer knowing nothing of where the
/// change came from. So the audio thread only records that it wants to and
/// `onMainThread()` does it -- the deferral the rescan flags already use.
///
/// \note `canUseThreadCheck()` is not decoration. `clap.thread-check` is an
/// *optional* extension and clap-helpers' `isMainThread()` asserts it before
/// calling through, so asking a host that does not offer it is a null
/// dereference in a shipping build, on a path every parameter write reaches. A
/// host that cannot say gets the deferral, which is correct from either thread.
void SpectrumWorxCLAP::markCurrentProgramAsModified() const
{
    /// \note Before the host question and not behind it: the browser's two Save
    /// buttons read this, and whether a host offers `clap.state` has nothing to
    /// do with whether the user has edited the preset. \see issue #177.
    const_cast<SpectrumWorxCLAP &>(*this).loadedPreset_.modified.store(true,
                                                                       std::memory_order_relaxed);

    markSessionAsUnsaved();
}

void SpectrumWorxCLAP::markSessionAsUnsaved() const
{
    auto &plugin(const_cast<SpectrumWorxCLAP &>(*this));

    if (!_host.canUseState())
        return;

    if (_host.canUseThreadCheck() && _host.isMainThread())
    {
        plugin._host.stateMarkDirty();
        return;
    }

    if (!pendingMarkDirty_.exchange(true))
        plugin._host.requestCallback();
}

////////////////////////////////////////////////////////////////////////////////
///
///   The state is the preset serialisation plus a `<dawExtraState>` block, so
/// the keys are names rather than positions and an effect list that moves does
/// not silently re-point them. \see doc/tech/streaming_format.md.
///
/// \note Natural units, not CLAPEdge's 0..1, because the preset format says so.
/// The edge exists because a *host* may not see a range move; a file has no such
/// problem, and natural units keep the state from encoding the edge policy.
///
/// \note A function-try-block on both halves of `clap_plugin_state`, because
/// both are `noexcept` and neither could promise it: an exception crossing a
/// CLAP entry point is `std::terminate` rather than a failed call, and between
/// them these two buffer a host's stream, build the whole module chain from it
/// and decode whatever audio file the state names. `Sample::load` is the
/// sharpest -- a session naming a long file asks for hundreds of megabytes from
/// inside `stateLoad`.
///
/// \note Falling off the end of the handler is what `return false` is for; for a
/// non-void function it would otherwise be undefined.
///
////////////////////////////////////////////////////////////////////////////////

bool SpectrumWorxCLAP::stateSave(clap_ostream const *const stream) noexcept
try
{
    // the echo first: a parameter the host wrote during process() is echoed
    // over ToUI, and nothing obliges a host to run the callback that drains it
    // into programMain_ between the block and the save
    drainEngineEvents();

    // withDawExtraState, which a .swp does not get. The block is empty today and
    // written anyway, so that "a session is a preset plus somewhere to put the
    // rest" is a property of the bytes rather than a plan
    auto const dawExtraState(sessionState());
    // programMain_, this being [main-thread]: a host saves a session while the
    // audio thread runs, and walking the engine's chain is the read that
    // crashed paramsInfo
    //
    // pathToUTF8, so <p n="Sample"> holds the same bytes on every platform and a
    // session written on one opens on another
    auto const state(savePreset(LE::IO::pathToUTF8(sampleFile_), sideChainSourceMain_, {},
                                programMain_, &dawExtraState));

    // the terminator goes into the stream: loadFrom() parses a C string, and a
    // host may hand back exactly what it was given with nothing after it
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

    // nobody asked for this load, so nothing under it may stop to ask the user
    GUI::UnattendedLoad const unattended;

    // pEditor_ is null unless a window happens to be open, and the same call
    // serves both because the consumer takes the editor as a pointer
    //
    // ignoreExternalSample false: the browser's toggle is a question about
    // somebody else's preset, and this is the session's own state
    auto const dawExtraState(sessionState());
    if (!GUI::loadPreset(*this, pEditor_, state->data(), false /*ignoreExternalSample*/, nullptr,
                         nullptr, &dawExtraState))
        return false;

    // the edited flag the block carried, applied now the load is over:
    // GUI::loadPreset ends in presetChangeEnd, which marks the *session*
    // modified, and that is not what this one means. \see issue #177
    loadedPreset_.modified.store(restoredPresetModified_, std::memory_order_relaxed);

    // deferred rather than announced straight through: GUI::loadPreset() above
    // ends in chainChanged(), which asks for the same rescan, and a host acts on
    // each one -- the AU wrapper by rebuilding its whole parameter tree, so a
    // second ask lands inside the first rebuild. pendingRescan_ collapses them
    requestRescan(CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_VALUES);
    return true;
}
catch (...)
{
    return false;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Where session state that is not a parameter goes.
///
///   Where the user was in the panel column, which is a place they were rather
/// than a sound the plugin makes: which of the two panels was up, which tab the
/// settings one was on, and where the browser was pointing. `GUI::PanelState` is
/// the struct, and issue #129 the ask.
///
/// \note The settings panel's Interface page is not here. Zoom, mouse-over
/// reaction, LFO update behaviour and hide-cursor-on-knob-drag are answers about
/// how this user likes the editor to behave rather than about this session, so
/// they go to the user preferences file instead. \see gui/preferences.hpp.
///
/// \note **The names below are on disk: do not rename them.** They are the
/// block's grammar in the same sense a parameter's streaming name is (\see
/// streaming_format.md §2), and the two enumerations are written by name rather
/// than by ordinal so that inserting a value cannot change what an existing
/// session means.
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
constexpr char panelAttribute[]{"panel"};
constexpr char settingsPageAttribute[]{"settingsPage"};
constexpr char presetLocationAttribute[]{"presetLocation"};
constexpr char presetBankAttribute[]{"presetBank"};
constexpr char presetFolderAttribute[]{"presetFolder"};

/// \note Which preset is *playing* and whether it has been edited, which is a
/// different question from where the browser was last looking. \see issue #177.
constexpr char loadedPresetAttribute[]{"loadedPreset"};
constexpr char loadedLocationAttribute[]{"loadedPresetLocation"};
constexpr char loadedBankAttribute[]{"loadedPresetBank"};
constexpr char loadedFileAttribute[]{"loadedPresetFile"};
constexpr char loadedModifiedAttribute[]{"loadedPresetModified"};

/// \note The comment the user typed, which for a factory preset has no file to
/// live in. \see issue #180.
constexpr char loadedCommentAttribute[]{"loadedPresetComment"};

constexpr char presetsPanel[]{"presets"};
constexpr char settingsPanel[]{"settings"};
constexpr char factoryLocation[]{"factory"};
constexpr char userLocation[]{"user"};

/// \note An unrecognised name reads as the default rather than as a failure: the
/// block is the user's to edit, and a value this build does not know is not a
/// corrupt session.
template <typename Value>
void readNamed(TiXmlElement const &element, char const *const attribute, Value &value,
               char const *const name, Value const named)
{
    if (auto const *const pText = element.Attribute(attribute);
        pText && (std::strcmp(pText, name) == 0))
        value = named;
}
} // anonymous namespace

DawExtraState SpectrumWorxCLAP::sessionState()
{
    return {[this](TiXmlElement &element) {
                auto const &state(panelState_);

                element.SetAttribute(panelAttribute,
                                     (state.panel == GUI::PanelState::Panel::settings)
                                         ? settingsPanel
                                         : presetsPanel);
                element.SetAttribute(settingsPageAttribute, static_cast<int>(state.settingsPage));

                element.SetAttribute(presetLocationAttribute,
                                     (state.presetLocation == GUI::PanelState::PresetLocation::user)
                                         ? userLocation
                                         : factoryLocation);
                element.SetAttribute(presetBankAttribute, state.presetBank.toStdString());
                // UTF-8 bytes on every platform, as the sample path is: a
                // session written on one has to open on another
                element.SetAttribute(presetFolderAttribute, IO::pathToUTF8(state.presetFolder));

                auto const &loaded(loadedPreset_);
                element.SetAttribute(loadedPresetAttribute, loaded.name.toStdString());
                element.SetAttribute(loadedLocationAttribute,
                                     (loaded.location == GUI::PanelState::PresetLocation::user)
                                         ? userLocation
                                         : factoryLocation);
                element.SetAttribute(loadedBankAttribute, loaded.bank.toStdString());
                element.SetAttribute(loadedFileAttribute, IO::pathToUTF8(loaded.file));
                element.SetAttribute(loadedModifiedAttribute,
                                     loaded.modified.load(std::memory_order_relaxed) ? 1 : 0);
                element.SetAttribute(loadedCommentAttribute, loaded.comment.toStdString());
            },
            [this](TiXmlElement const &element) {
                auto &state(panelState_);

                readNamed(element, panelAttribute, state.panel, settingsPanel,
                          GUI::PanelState::Panel::settings);
                readNamed(element, panelAttribute, state.panel, presetsPanel,
                          GUI::PanelState::Panel::presets);

                // a missing attribute leaves the member where it was --
                // QueryUnsignedAttribute only writes through on success -- so a
                // state written by an older build resets nothing
                element.QueryUnsignedAttribute(settingsPageAttribute, &state.settingsPage);

                readNamed(element, presetLocationAttribute, state.presetLocation, userLocation,
                          GUI::PanelState::PresetLocation::user);
                readNamed(element, presetLocationAttribute, state.presetLocation, factoryLocation,
                          GUI::PanelState::PresetLocation::factory);

                if (auto const *const pBank = element.Attribute(presetBankAttribute))
                    state.presetBank = juce::String::fromUTF8(pBank);
                if (auto const *const pFolder = element.Attribute(presetFolderAttribute))
                    state.presetFolder = IO::utf8ToPath(pFolder);

                auto &loaded(loadedPreset_);
                if (auto const *const pName = element.Attribute(loadedPresetAttribute))
                    loaded.name = juce::String::fromUTF8(pName);
                readNamed(element, loadedLocationAttribute, loaded.location, userLocation,
                          GUI::PanelState::PresetLocation::user);
                readNamed(element, loadedLocationAttribute, loaded.location, factoryLocation,
                          GUI::PanelState::PresetLocation::factory);
                if (auto const *const pBank = element.Attribute(loadedBankAttribute))
                    loaded.bank = juce::String::fromUTF8(pBank);
                if (auto const *const pFile = element.Attribute(loadedFileAttribute))
                    loaded.file = IO::utf8ToPath(pFile);
                if (auto const *const pComment = element.Attribute(loadedCommentAttribute))
                    loaded.comment = juce::String::fromUTF8(pComment);

                /// \note Into a plain member rather than straight into the atomic:
                /// this runs while the block is being parsed, and the load it is
                /// part of ends in `presetChangeEnd` -> `markCurrentProgramAsModified`,
                /// which would set the flag back to true a moment later. `stateLoad`
                /// applies it once the load is over. \see issue #177.
                int modified{restoredPresetModified_ ? 1 : 0};
                element.QueryIntAttribute(loadedModifiedAttribute, &modified);
                restoredPresetModified_ = (modified != 0);
            }};
}

/// \note The shim owns what this returns and destroys it before this plugin. The
/// editor registers and deregisters itself through EditorHost.
///
/// \note Wrapped, so the plugin shows the editor scaled: the editor lays itself
/// out in skin pixels, and ZoomedEditor carries the transform and answers for
/// size. Anything wanting 1:1 -- the test harness does -- skips the wrapper.
std::unique_ptr<juce::Component> SpectrumWorxCLAP::createEditor()
{
    return std::make_unique<GUI::ZoomedEditor>(std::make_unique<GUI::SpectrumWorxEditor>(*this));
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Brings the main thread's copy of the three spectral parameters back to
/// what the engine actually settled on, and says so. `[main-thread]`
///
/// \note For the one path where the engine declines a value it was given:
/// `updateEngineSetup()` puts the FFT size and the overlap factor back when the
/// working set cannot be allocated, in the Program it can reach, which is the
/// engine's. Without this the parameter reads 8192 while the engine runs 2048,
/// and `stateSave` writes 8192 for the next session to try again.
///
/// \note A rescan rather than a message box: the interface shows it by reading
/// the value really in force, and this can run inside `deactivate()`.
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

/// \note `clap_id` and `ParameterID::binaryValue` are the same number -- see
/// paramsInfo(), which writes one straight into the other. The helper asks the
/// host for its extension and adds nothing when there is none.

void SpectrumWorxCLAP::addHostParameterEntries(ParameterID const parameterID,
                                               juce::PopupMenu &menu) const
{
    sst::clap_juce_shim::populateMenuForClapParam(menu, parameterID.binaryValue, _host.host());
}

void SpectrumWorxCLAP::editorOpened(GUI::SpectrumWorxEditor &editor) { pEditor_ = &editor; }

/// \note Only if it is the editor this plugin knows about. `guiCreate` and
/// `guiDestroy` need not balance the way a window's lifetime does -- a host may
/// create a GUI, never parent it and destroy it -- so two can exist at once for a
/// moment, and clearing unconditionally would leave the new one no longer
/// following the engine.

void SpectrumWorxCLAP::editorClosed(GUI::SpectrumWorxEditor &editor)
{
    if (pEditor_ == &editor)
        pEditor_ = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief What the editor calls when it wants a column for its preset browser or
/// its settings panel, and again when it gives one back.
///
/// \note Not `can_resize`, which stays false: the user may not drag the window,
/// and a plugin may ask for one particular size whether or not its editor is
/// resizable (ext/gui.h:35-45).
///
/// \note A host that offers no `clap.gui`, or one that says no, gets a false back
/// and the editor lays the panel over the module strips instead. Nothing here is
/// a failure worth telling the user about.
///
////////////////////////////////////////////////////////////////////////////////

bool SpectrumWorxCLAP::requestEditorSize(int const width, int const height)
{
    LE_ASSERT(Threading::isMainThread() || !Threading::isAudioThread());

    if (!_host.canUseGui())
        return false;

    // the editor asks in skin pixels and every size crossing this boundary is
    // in the host's window units, so scale by the zoom the user asked for --
    // read from the same preference ZoomedEditor reads
    auto const requestedWidth(
        static_cast<std::uint32_t>(GUI::ZoomedEditor::scaledForCurrentZoom(width)));
    auto const requestedHeight(
        static_cast<std::uint32_t>(GUI::ZoomedEditor::scaledForCurrentZoom(height)));
    if (!_host.guiRequestResize(requestedWidth, requestedHeight))
        return false;

    // the shim is told as well, because a host does not have to call set_size()
    // (ext/gui.h:221): without this the JUCE side keeps the old window size and
    // the new column is clipped away by the holder
    if (clapJuceShim_ && clapJuceShim_->isEditorAttached())
        clapJuceShim_->guiSetSize(requestedWidth, requestedHeight);
    return true;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief What the editor calls when it has already changed size and the window
/// has to catch up -- the zoom, and nothing else so far.
///
/// \note The shim first, the host second. `guiGetSize()` is answered out of the
/// shim's holder, so until the holder has the new size the plugin tells any host
/// that asks that it is still the old one -- including one that answers
/// `request_resize` by turning round and asking.
///
/// \note And `guiSetSize` unconditionally, where requestEditorSize() reaches it
/// only past two `return false`s: a host with no `clap.gui` still has an editor
/// that has just changed size, and a refusal is not a statement that the window
/// stayed put.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCLAP::editorSizeChanged(int const width, int const height)
{
    LE_ASSERT(Threading::isMainThread() || !Threading::isAudioThread());

    auto const newWidth(static_cast<std::uint32_t>(GUI::ZoomedEditor::scaledForCurrentZoom(width)));
    auto const newHeight(
        static_cast<std::uint32_t>(GUI::ZoomedEditor::scaledForCurrentZoom(height)));

    // not gated on isEditorAttached(), which is about a parent window rather
    // than about there being an editor: the shim's components exist from
    // guiCreate(), and only an editor that is up calls this
    if (clapJuceShim_)
        clapJuceShim_->guiSetSize(newWidth, newHeight);

    if (_host.canUseGui())
        _host.guiRequestResize(newWidth, newHeight);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note `[main-thread]`, and synchronous: it decodes the whole file here and
/// publishes the result, which is why it needs no lock
/// (doc/tech/threading_model.md §5). A long file the user picks stalls the
/// message thread, which is the cost of having no loader thread.
///
/// \note `activate()` asks for two main and two side channels outright, so the
/// side buffers this writes into are there whether a sample is loaded or not.
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

    // this plugin's own rate rather than the engine's, and zero is legal: a
    // host can restore a session before ever activating, and Sample::load()
    // reads a file at its own rate when given no other -- activate() re-reads it
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

    // the state holds the file name -- <p n="Sample"> -- so a host told the
    // session changed is being promised something the format can keep
    markCurrentProgramAsModified();
    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Hands \p pNewSample (owned, null clears) to whoever owns the engine.
///
/// \note The same shape as the chain's handover (threading_model.md §5): swap a
/// pointer where the engine is, destroy the old one where destroying is allowed.
/// The main thread keeps its own record of the file and the rate it decoded at,
/// because those are questions the interface asks with audio running.
///
/// \note The bookkeeping goes after the handover, not before it. `sampleFile_`
/// is what `stateSave` writes, so recording it ahead of a push that can fail
/// would leave the session naming a file the engine never received.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCLAP::publishSideChain(Sample *const pNewSample, bool const replacesSample,
                                        SideChainSource const source)
{
    auto const recordWhatTheEngineHasNow([&] {
        if (replacesSample)
        {
            sampleFile_ = pNewSample ? pNewSample->sampleFile() : fs::path();
            decodedSampleRate_ = pNewSample ? pNewSample->sampleRate() : 0;
        }
        sideChainSourceMain_ = source;
    });

    if (!engineIsRunning())
    {
        sideChainSource_ = source;
        if (replacesSample)
        {
            delete std::exchange(pSample_, pNewSample);
            clearSideChannelData();
        }
        recordWhatTheEngineHasNow();
        return;
    }

    if (pushed(toEngine_.push(Threading::swapSample(pNewSample, replacesSample,
                                                    static_cast<std::uint8_t>(source))),
               "The command queue is full; a side chain change was dropped."))
    {
        recordWhatTheEngineHasNow();
        return;
    }

    delete pNewSample;
}

/// \note Loading a file *is* selecting it as the source, and clearing one selects
/// the main input, so no file is ever loaded and unheard by accident. A user who
/// wants the host's port with a file still loaded says so through
/// `setSideChainSource()`. \see doc/tech/sidechain-approach.md.

void SpectrumWorxCLAP::publishSample(Sample *const pNewSample)
{
    publishSideChain(pNewSample, true,
                     pNewSample ? SideChainSource::File
                                : ((sideChainSourceMain_ == SideChainSource::File)
                                       ? SideChainSource::Main
                                       : sideChainSourceMain_));
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note **Selecting `Main` or `Host` discards the loaded file**, rather than
/// leaving it loaded and unheard: one selection, one thing selected. Keeping it
/// would let a patch carry an audio file nothing plays and a `Sample=` the
/// source contradicts. The cost is a second decode if a user switches back.
///
/// \note `File` is the exception and clears nothing -- it is reached with a
/// sample already published, from `Loader::setSideChain()` restoring a patch that
/// names one. With no sample it is refused outright: the selector would show a
/// file that is not there and the engine would hold a source it cannot honour.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxCLAP::setSideChainSource(SideChainSource const source)
{
    LE_ASSERT(Threading::isMainThread() || !Threading::isAudioThread());

    if (source == SideChainSource::File)
    {
        if (sampleFile_.empty())
            return setSideChainSource(SideChainSource::Main);
        return publishSideChain(nullptr, false, SideChainSource::File);
    }

    publishSideChain(nullptr, true /*the file goes with it*/, source);
    markCurrentProgramAsModified();
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
