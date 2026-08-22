////////////////////////////////////////////////////////////////////////////////
///
/// threadCheckTests.cpp
/// --------------------
///
///   Which thread is this, and may it do that.
///
///   Two things are being pinned. The facility itself -- that the answers are
/// per thread and that a scope really does bound them -- and, more valuable, that
/// process() is actually wrapped, which is checked through the host's own API
/// rather than by reading the source.
///
/// See doc/tech/threading_model.md.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "core/threading/threadCheck.hpp"

#include "swClapEntryImpl.hpp"

#include <clap/clap.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
namespace Threading = LE::SW::Threading;

constexpr std::uint32_t blockSize{256};
constexpr std::uint8_t channels{2};
constexpr double sampleRate{48000};

/// Runs \p f on a thread of its own and waits for it.
template <class Functor> void onAnotherThread(Functor f)
{
    std::thread thread(f);
    thread.join();
}

////////////////////////////////////////////////////////////////////////////////
// The wiring: a host that can see which thread it is being called back from.
////////////////////////////////////////////////////////////////////////////////

/// \brief What the host saw when the plugin called it back.
struct Observation
{
    std::atomic<bool> called{false};
    std::atomic<bool> wasAudioThread{false};
    std::atomic<unsigned int> markedDirty{0};
};

Observation observation;

/// \brief A host offering `clap.state` and deliberately no `clap.thread-check`.
///
/// \note That combination is what makes `markCurrentProgramAsModified()` defer:
/// it cannot ask which thread it is on, so it records the intent and calls
/// `request_callback`, which is `[thread-safe]`. Any parameter event delivered to
/// process() therefore reaches this host callback from inside the audio callback,
/// which is the observation point -- no test hook in the plugin, just the API.
clap_host const &observingHost()
{
    static clap_host_state const state{
        .mark_dirty = [](clap_host const *) { observation.markedDirty.fetch_add(1); }};

    /// \note Designated initialisers, and not for taste: `request_restart`,
    /// `request_process` and `request_callback` are three adjacent members of the
    /// same type, so a positional initialiser puts a callback in the wrong one and
    /// the compiler cannot say a word. Which is what happened writing this.
    static clap_host host{
        .clap_version = CLAP_VERSION,
        .host_data = nullptr,
        .name = "sw-tests",
        .vendor = "SpectrumWorx",
        .url = "",
        .version = "0",
        .get_extension = [](clap_host const *, char const *const id) -> void const * {
            return std::string_view(id) == CLAP_EXT_STATE ? &state : nullptr;
        },
        .request_restart = [](clap_host const *) {},
        .request_process = [](clap_host const *) {},
        .request_callback =
            [](clap_host const *) {
                observation.wasAudioThread.store(Threading::isAudioThread());
                observation.called.store(true);
            },
    };
    return host;
}

/// \brief RAII around the entry point, whose init/deinit are refcounted.
class Entry
{
  public:
    Entry() { REQUIRE(LE::SW::ClapFirst::clapInit("sw-tests")); }
    ~Entry() { LE::SW::ClapFirst::clapDeinit(); }

    Entry(Entry const &) = delete; // makes non-copyable
    Entry &operator=(Entry const &) = delete;
}; // class Entry

clap_plugin const *createPlugin()
{
    auto const *const pFactory(static_cast<clap_plugin_factory const *>(
        LE::SW::ClapFirst::getFactory(CLAP_PLUGIN_FACTORY_ID)));
    REQUIRE(pFactory != nullptr);
    auto const *const pDescriptor(pFactory->get_plugin_descriptor(pFactory, 0));
    REQUIRE(pDescriptor != nullptr);
    auto const *const pPlugin(pFactory->create_plugin(pFactory, &observingHost(), pDescriptor->id));
    REQUIRE(pPlugin != nullptr);
    return pPlugin;
}

/// \brief One parameter event, deliverable as a clap_input_events.
class OneEvent
{
  public:
    OneEvent(clap_id const id, double const value)
    {
        event_.header.size = sizeof(event_);
        event_.header.time = 0;
        event_.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event_.header.type = CLAP_EVENT_PARAM_VALUE;
        event_.header.flags = 0;
        event_.param_id = id;
        event_.cookie = nullptr;
        event_.note_id = event_.port_index = event_.channel = event_.key = -1;
        event_.value = value;

        list_.ctx = this;
        list_.size = [](clap_input_events const *) -> std::uint32_t { return 1; };
        list_.get = [](clap_input_events const *const pList,
                       std::uint32_t) -> clap_event_header const * {
            return &static_cast<OneEvent const *>(pList->ctx)->event_.header;
        };
    }

    OneEvent(OneEvent const &) = delete; // makes non-copyable
    OneEvent &operator=(OneEvent const &) = delete;

    clap_input_events const *operator&() const { return &list_; }

  private:
    clap_event_param_value event_{};
    clap_input_events list_{};
}; // class OneEvent
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
// The facility
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("An audio callback scope is bounded, and is per thread", "[core][threading]")
{
    /// \note Observed inside the scope, asserted outside it. `ScopedAudioThreadEntry`
    /// opens a RealtimeSanitizer realtime region, and Catch2's `CHECK` allocates
    /// -- so under `-fsanitize=realtime` a case that asserted in here would abort
    /// on its own expression macro. Which is rtsan being right: this is a scope in
    /// which allocating is forbidden, and a test is not exempt.
    bool insideScope{false};
    bool insideNested{false};
    bool afterNested{false};
    bool onAnotherThreadInside{true};

    CHECK(!Threading::isAudioThread());
    {
        Threading::ScopedAudioThreadEntry const audioCallback;
        insideScope = Threading::isAudioThread();

        {
            Threading::ScopedAudioThreadEntry const nested;
            insideNested = Threading::isAudioThread();
        }
        afterNested = Threading::isAudioThread();
    }
    CHECK(!Threading::isAudioThread());

    CHECK(insideScope);
    CHECK(insideNested);
    CHECK(afterNested);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note A host with a worker pool calls one plugin from several threads, so
    /// the answer has to be about this *call* rather than about the process.
    ///
    ///   The observer is started before the scope opens, not inside it: creating
    /// a thread is itself something a callback may not do, and the whole point of
    /// this file is that the scope means it. The handshake is two relaxed atomics
    /// and a bare spin -- `std::this_thread::yield()` is a syscall.
    ///
    ////////////////////////////////////////////////////////////////////////////
    std::atomic<bool> scopeIsOpen{false};
    std::atomic<bool> hasObserved{false};

    std::thread observer([&] {
        while (!scopeIsOpen.load(std::memory_order_acquire))
        {
        }
        onAnotherThreadInside = Threading::isAudioThread();
        hasObserved.store(true, std::memory_order_release);
    });

    {
        Threading::ScopedAudioThreadEntry const audioCallback;
        scopeIsOpen.store(true, std::memory_order_release);
        while (!hasObserved.load(std::memory_order_acquire))
        {
        }
    }
    observer.join();

    CHECK(!onAnotherThreadInside);
    CHECK(!Threading::isAudioThread());
}

TEST_CASE("The main thread is the one that said so", "[core][threading]")
{
    Threading::forgetMainThread();

    // Nothing has claimed it: the honest answer for a process that was never told,
    // which is every one of these tests and sw-show-ui.
    CHECK(!Threading::isMainThread());
    onAnotherThread([] { CHECK(!Threading::isMainThread()); });

    Threading::markMainThread();
    CHECK(Threading::isMainThread());
    onAnotherThread([] { CHECK(!Threading::isMainThread()); });

    Threading::forgetMainThread();
    CHECK(!Threading::isMainThread());
}

////////////////////////////////////////////////////////////////////////////////
// The wiring
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The plugin calls its host back from inside the audio callback",
          "[core][threading][clap]")
{
    // The point of the case: it proves ScopedAudioThreadEntry is really at the top of
    // process(), and it does so through the host API rather than by reading the
    // source. Delete the guard from process() and this goes red.
    Entry const entry;
    auto const *const pPlugin(createPlugin());

    REQUIRE(pPlugin->init(pPlugin));

    // init() is [main-thread] by contract, and the plugin records it there.
    CHECK(Threading::isMainThread());

    REQUIRE(pPlugin->activate(pPlugin, sampleRate, 1, blockSize));
    REQUIRE(pPlugin->start_processing(pPlugin));

    auto const *const pParams(
        static_cast<clap_plugin_params const *>(pPlugin->get_extension(pPlugin, CLAP_EXT_PARAMS)));
    REQUIRE(pParams != nullptr);

    // Index 0 is a global parameter, so it exists whatever is in the slots -- a
    // parameter no effect owns is dropped by handleEvent() before it reaches the
    // host callback this is watching for.
    clap_param_info info{};
    REQUIRE(pParams->get_info(pPlugin, 0, &info));
    OneEvent const event(info.id, info.default_value);

    std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
    std::vector<float> leftOut(blockSize, 0.0f), rightOut(blockSize, 0.0f);
    float *inputChannels[]{leftIn.data(), rightIn.data()};
    float *outputChannels[]{leftOut.data(), rightOut.data()};
    clap_audio_buffer input{&inputChannels[0], nullptr, channels, 0, 0};
    clap_audio_buffer output{&outputChannels[0], nullptr, channels, 0, 0};

    clap_process process{};
    process.steady_time = -1;
    process.frames_count = blockSize;
    process.audio_inputs = &input;
    process.audio_inputs_count = 1;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1;
    process.in_events = &event;

    observation.called.store(false);
    CHECK(pPlugin->process(pPlugin, &process) != CLAP_PROCESS_ERROR);

    REQUIRE(observation.called.load());
    CHECK(observation.wasAudioThread.load());

    // Nothing was marked dirty from inside the callback: `mark_dirty` is
    // [main-thread] and the plugin has no way to ask this host which thread it is
    // on, so it deferred -- and the deferral is only correct if it really does
    // arrive later.
    CHECK(observation.markedDirty.load() == 0);
    pPlugin->on_main_thread(pPlugin);
    CHECK(observation.markedDirty.load() == 1);

    pPlugin->stop_processing(pPlugin);
    pPlugin->deactivate(pPlugin);
    pPlugin->destroy(pPlugin);

    Threading::forgetMainThread();
}
