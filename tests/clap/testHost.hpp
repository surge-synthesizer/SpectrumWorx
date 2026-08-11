////////////////////////////////////////////////////////////////////////////////
///
/// \file testHost.hpp
/// ------------------
///
///   The host side of the CLAP tests: what a DAW offers a plugin, what it asks
/// it, and what it hears back.
///
///   There were three hand-rolled `clap_host`s in `tests/clap/` and each one
/// offered exactly the extensions its own file needed, which is how
/// a null dereference on the path every parameter write takes survived a green
/// suite -- invisible because no test host offered `clap.state`.
/// One configurable host instead, so that "what does this case's host offer" is a
/// line of the case rather than a hundred lines of boilerplate.
///
/// \note Three of the four things it offers are optional extensions and the
/// interesting hosts are the ones that withhold them -- a plugin may not assume
/// any of them. TestHost::Offers is therefore explicit at every construction and
/// has no default that quietly offers everything.
///
/// \note `clap.thread-check` is the one that changes the most. Answering it turns
/// on clap-helpers' own contract checking -- `ensureMainThread`/`ensureAudioThread`
/// return immediately when `!canUseThreadCheck()` (plugin.hxx:2262-2287) -- so a
/// host that answers is also what makes the *harness* prove it calls `process()`
/// and `activate()` from where it claims to. Which is why the answer comes from
/// this host's own bookkeeping rather than from the plugin's `Threading::`
/// facility: a host knows which of its threads is its audio thread because it
/// started it, and taking the answer from the code under test would be circular.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef testHost_hpp__6B1E9F30_74A2_4C58_9E63_2D0A7B4F81C5
#define testHost_hpp__6B1E9F30_74A2_4C58_9E63_2D0A7B4F81C5
//------------------------------------------------------------------------------
#include "swClapEntryImpl.hpp"

#include "gui/editor/editorHost.hpp"
#include "spectrumWorxCLAP.hpp"

#include <clap/clap.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace SWTest
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class CapturedStandardError
///
/// \brief `std::cerr` for the length of a scope, kept rather than printed.
///
///   clap-helpers' `checkMainThread()` and `checkAudioThread()` write straight to
/// `std::cerr` (plugin.hxx:2219, :2233) instead of going through
/// `hostMisbehaving()`, so nothing routed through `clap.log` can see them --
/// which is the same limitation `isFlushValidatingItself()` records from the
/// other end. A case that asserts "nobody misbehaves" over the log alone is
/// therefore asserting over the reports that *can* be intercepted, and one of
/// them emitted such a line and passed.
///
/// \note A `rdbuf` swap, which is the whole implementation and is as portable as
/// the stream is. Nesting works and is used: an inner scope takes the writes and
/// the outer one never sees them, which is how a case separates the noise it
/// expects from the silence it is asserting.
///
/// \note **One thread's worth.** `std::stringbuf` written from two threads at
/// once is a race, and `std::cerr`'s own is process-wide -- so this is for the
/// ordinary single-threaded case rather than for the ones in threadingTests.cpp
/// that run an audio thread of their own. In a clean run nothing writes to it at
/// all, which is the point.
///                                           (09.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

class CapturedStandardError
{
  public:
    CapturedStandardError() : pPrevious_(std::cerr.rdbuf(&buffer_)) {}
    ~CapturedStandardError() { std::cerr.rdbuf(pPrevious_); }

    CapturedStandardError(CapturedStandardError const &) = delete; // makes non-copyable
    CapturedStandardError &operator=(CapturedStandardError const &) = delete;

    /// Everything written so far, one entry per line and no empty tail.
    std::vector<std::string> lines() const
    {
        std::vector<std::string> result;
        std::istringstream text(buffer_.str());
        for (std::string line; std::getline(text, line);)
            if (!line.empty())
                result.push_back(line);
        return result;
    }

  private:
    std::stringbuf buffer_;
    std::streambuf *const pPrevious_;
}; // class CapturedStandardError

////////////////////////////////////////////////////////////////////////////////
// The hosts
////////////////////////////////////////////////////////////////////////////////

/// \brief The smallest host that answers everything clap::helpers::Plugin asks at
/// construction. Every extension is absent, which is legal and is the interesting
/// case: it means the plugin may not assume any of them.
inline clap_host const &nullHost()
{
    static clap_host host{CLAP_VERSION,
                          nullptr,
                          "sw-tests",
                          "SpectrumWorx",
                          "",
                          "0",
                          [](clap_host const *, char const *) -> void const * { return nullptr; },
                          [](clap_host const *) {},
                          [](clap_host const *) {},
                          [](clap_host const *) {}};
    return host;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \class TestHost
///
/// \brief A host offering whichever of `clap.params`, `clap.state`,
/// `clap.thread-check` and `clap.log` it is asked to, counting everything it is
/// told and answering the thread check truthfully.
///
/// \note No singleton and no scoping guard, unlike the three it replaces. Those
/// existed because "the C callbacks carry no context of their own"; they do --
/// `clap_host::host_data` is the *host's* pointer (host.h:12), where
/// `clap_plugin::plugin_data` is the plugin's. So a case may hold two of these at
/// once, which is what the two-instance cases need.
///
////////////////////////////////////////////////////////////////////////////////

class TestHost
{
  public:
    /// What this host offers. A DAW offers all of it; every field here being
    /// false is also a host a plugin has to survive.
    struct Offers
    {
        bool params{false};
        bool state{false};
        bool threadCheck{false};
        /// \note Without this, clap-helpers writes its misbehaviour reports to
        /// `std::cerr` and nothing can assert about them (host-proxy.hxx:91-107).
        /// With it they land in misbehaviours() and a case can require there were
        /// none.
        bool log{false};
        /// \note All five of `clap_host_gui`'s entry points or none: clap-helpers
        /// calls a partially implemented one host misbehaviour and then refuses
        /// the extension outright (host-proxy.hxx:449-459).
        bool gui{false};
    }; // struct Offers

    /// A host with everything, which is what a DAW is.
    static Offers everything() { return {true, true, true, true, true}; }

    explicit TestHost(Offers const offers)
        : offers_(offers), mainThread_(std::this_thread::get_id()),
          params_{[](clap_host const *const pHost, clap_param_rescan_flags const flags) {
                      self(pHost).rescanFlags |= flags;
                  },
                  [](clap_host const *, clap_id, clap_param_clear_flags) {},
                  [](clap_host const *const pHost) { ++self(pHost).flushRequests; }},
          state_{[](clap_host const *const pHost) { ++self(pHost).dirtyMarks; }},
          threadCheck_{[](clap_host const *const pHost) {
                           ++self(pHost).threadChecks;
                           return self(pHost).isMainThread();
                       },
                       [](clap_host const *const pHost) {
                           ++self(pHost).threadChecks;
                           return self(pHost).isAudioThread();
                       }},
          log_{[](clap_host const *const pHost, clap_log_severity const severity,
                  char const *const message) { self(pHost).logged(severity, message); }},
          // resize_hints_changed, request_resize, request_show, request_hide, closed.
          gui_{[](clap_host const *) {},
               [](clap_host const *const pHost, std::uint32_t const width,
                  std::uint32_t const height) {
                   auto &host(self(pHost));
                   host.resizeRequests.emplace_back(width, height);
                   return host.grantResizes;
               },
               [](clap_host const *) { return true; }, [](clap_host const *) { return true; },
               [](clap_host const *, bool) {}},
          host_{CLAP_VERSION, this, "sw-tests", "SpectrumWorx", "", "0", &getExtension,
                // request_restart, request_process, request_callback -- in that
                // order; the last is the one a deferred rescan asks for.
                [](clap_host const *const pHost) { ++self(pHost).restartRequests; },
                [](clap_host const *const pHost) { ++self(pHost).processRequests; },
                [](clap_host const *const pHost) { ++self(pHost).mainThreadCallbacks; }}
    {
    }

    TestHost(TestHost const &) = delete; // host_data points at this
    TestHost &operator=(TestHost const &) = delete;

    clap_host const &operator*() const { return host_; }

    ////////////////////////////////////////////////////////////////////////////
    // What it was asked
    ////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Atomic, because a real host is counting these from whichever of its
    /// threads called in and so is this one. `request_restart`, `request_flush`
    /// and `params.rescan` are all reachable from `process()`, and the cases in
    /// threadingTests.cpp have an audio thread of their own -- so a plain
    /// `unsigned` here is a data race in the *harness*, which tsan reports at
    /// length while saying nothing about the plugin. It said exactly that about
    /// `restartRequests` the first time anything drove two threads at it.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    std::atomic<clap_param_rescan_flags> rescanFlags{0};
    std::atomic<unsigned> flushRequests{0};
    std::atomic<unsigned> mainThreadCallbacks{0};
    std::atomic<unsigned> restartRequests{0};
    std::atomic<unsigned> processRequests{0};
    std::atomic<unsigned> dirtyMarks{0};
    /// How often the plugin asked which thread it was on, which is the evidence
    /// that it asks at all rather than always taking the deferral.
    std::atomic<unsigned> threadChecks{0};

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Every window size the plugin asked for, in order.
    ///
    /// \note And whether this host grants them, which is not a detail: a
    /// `request_resize` is a request, and a host with a fixed-width plugin pane
    /// really does answer no. `grantResizes = false` is that host, and what the
    /// editor does about it is the point of testing against one.
    ///
    ////////////////////////////////////////////////////////////////////////////

    std::vector<std::pair<std::uint32_t, std::uint32_t>> resizeRequests;
    bool grantResizes{true};

    ////////////////////////////////////////////////////////////////////////////
    // What it heard
    ////////////////////////////////////////////////////////////////////////////

    /// \brief What clap-helpers said the *plugin* did wrong -- a callback run off
    /// the main thread, a lifecycle call answered out of order.
    std::vector<std::string> const &pluginMisbehaviours() const { return pluginMisbehaviours_; }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief What it said the *host* did wrong, which here means this harness.
    ///
    ///   `ensureAudioThread` fires when `process()` was called with no
    /// AudioCallback around it, and `ensureMainThread` when `activate()` was
    /// called inside one. Both are worth failing over: a harness that drives the
    /// audio entry points from nowhere in particular is not reproducing what a
    /// DAW does, and until a host answered the thread check there was no way to
    /// find out that it was not.
    ///
    /// \note One shape of report is dropped, and it is clap-helpers arguing with
    /// itself rather than anything about this plugin.  `clapParamsFlush` validates
    /// each event it is given through `checkValidFlushEvent`, which reads the
    /// parameter's description by calling clap-helpers' **own**
    /// `clap_plugin_params.info` C entry point (plugin.hxx:1199 and :1346) -- and
    /// that entry point opens with `ensureMainThread`. A flush against an active
    /// plugin is `[audio-thread]` (ext/params.h:303). So the two contracts are
    /// incompatible inside one call and the report is about neither party.
    ///
    ///   Matched on the exact wording, deliberately: a clap-helpers that reworded
    /// it stops being filtered and this goes red, which is the direction a
    /// third-party workaround should fail in.
    ///
    /// \note The same self-contradiction also prints one bare
    /// "thread-error: this code must be running on the main thread" per flush,
    /// from `getParamInfoForParamId`'s `checkMainThread()` (plugin.hxx:1195,
    /// :2219). That one writes to `std::cerr` directly rather than through
    /// `log()`, so `clap.log` cannot see it and this filter cannot reach it.
    /// `CapturedStandardError` above is what does, and free-audio/clap-helpers#98
    /// is the upstream report.
    ///                                       (03.08.2026, amended 09.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    std::vector<std::string> const &hostMisbehaviours() const { return hostMisbehaviours_; }

    /// Both of the above, which is what a case that expects neither asserts on.
    std::vector<std::string> misbehaviours() const
    {
        auto all(pluginMisbehaviours_);
        all.insert(all.end(), hostMisbehaviours_.begin(), hostMisbehaviours_.end());
        return all;
    }

    /// Every message, whatever its severity and including the filtered ones.
    std::vector<std::string> const &logLines() const { return logLines_; }

    ////////////////////////////////////////////////////////////////////////////
    // Which thread this is
    ////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \class TestHost::AudioCallback
    ///
    /// \brief The window in which this host considers the calling thread its
    /// audio thread.
    ///
    /// \note A DAW knows this because it started the thread. A test drives
    /// `process()` from whichever thread it happens to be on -- usually the same
    /// one it does everything else on -- so the window is what distinguishes the
    /// two roles, and `is_main_thread` has to answer *false* inside it even
    /// though the OS thread is the same one.
    ///
    ////////////////////////////////////////////////////////////////////////////

    class AudioCallback
    {
      public:
        explicit AudioCallback(TestHost &host) : host_(host)
        {
            host_.audioThread_.store(std::this_thread::get_id(), std::memory_order_release);
            host_.inAudioCallback_.store(true, std::memory_order_release);
        }

        ~AudioCallback()
        {
            host_.inAudioCallback_.store(false, std::memory_order_release);
            host_.audioThread_.store(std::thread::id(), std::memory_order_release);
        }

        AudioCallback(AudioCallback const &) = delete; // makes non-copyable
        AudioCallback &operator=(AudioCallback const &) = delete;

      private:
        TestHost &host_;
    }; // class TestHost::AudioCallback

    bool isMainThread() const
    {
        return !inAudioCallback_.load(std::memory_order_acquire) &&
               (std::this_thread::get_id() == mainThread_);
    }

    bool isAudioThread() const
    {
        return inAudioCallback_.load(std::memory_order_acquire) &&
               (std::this_thread::get_id() == audioThread_.load(std::memory_order_acquire));
    }

  private:
    /// \note No `REQUIRE` here, on either pointer. These callbacks run from
    /// inside `process()` -- Catch2's assertion machinery allocates and writes a
    /// shared counter, so asserting here is a data race in the harness and a
    /// realtime violation in a `-fsanitize=realtime` run. It is also called often
    /// enough that the assertion *count* stops meaning anything: 1.3 million of
    /// them for five cases. A null here segfaults, which says the same thing.
    static TestHost &self(clap_host const *const pHost)
    {
        return *static_cast<TestHost *>(pHost->host_data);
    }

    static void const *getExtension(clap_host const *const pHost, char const *const id)
    {
        auto const &host(self(pHost));
        if (host.offers_.params && (std::strcmp(id, CLAP_EXT_PARAMS) == 0))
            return &host.params_;
        if (host.offers_.state && (std::strcmp(id, CLAP_EXT_STATE) == 0))
            return &host.state_;
        if (host.offers_.threadCheck && (std::strcmp(id, CLAP_EXT_THREAD_CHECK) == 0))
            return &host.threadCheck_;
        if (host.offers_.log && (std::strcmp(id, CLAP_EXT_LOG) == 0))
            return &host.log_;
        if (host.offers_.gui && (std::strcmp(id, CLAP_EXT_GUI) == 0))
            return &host.gui_;
        return nullptr;
    }

    void logged(clap_log_severity const severity, char const *const message)
    {
        logLines_.emplace_back(message ? message : "");
        auto const &line(logLines_.back());

        if (severity == CLAP_LOG_PLUGIN_MISBEHAVING)
            pluginMisbehaviours_.emplace_back(line);
        else if ((severity == CLAP_LOG_HOST_MISBEHAVING) && !isFlushValidatingItself(line))
            hostMisbehaviours_.emplace_back(line);
    }

    /// \see hostMisbehaviours()
    static bool isFlushValidatingItself(std::string const &line)
    {
        return (line == "Host called the method clap_plugin_params.info() on wrong thread! It must "
                        "be called on main thread!") ||
               (line ==
                "Host called the method clap_plugin_params.count() on wrong thread! It must "
                "be called on main thread!");
    }

    Offers const offers_;

    std::thread::id const mainThread_;
    std::atomic<std::thread::id> audioThread_{};
    std::atomic<bool> inAudioCallback_{false};

    std::vector<std::string> logLines_;
    std::vector<std::string> pluginMisbehaviours_;
    std::vector<std::string> hostMisbehaviours_;

    clap_host_params params_;
    clap_host_state state_;
    clap_host_thread_check threadCheck_;
    clap_host_log log_;
    clap_host_gui gui_;
    clap_host host_;
}; // class TestHost

////////////////////////////////////////////////////////////////////////////////
// Event lists
////////////////////////////////////////////////////////////////////////////////

inline clap_input_events const &noInputEvents()
{
    static clap_input_events events{
        nullptr, [](clap_input_events const *) -> std::uint32_t { return 0; },
        [](clap_input_events const *, std::uint32_t) -> clap_event_header const * {
            return nullptr;
        }};
    return events;
}

inline clap_output_events const &discardedOutputEvents()
{
    static clap_output_events events{
        nullptr, [](clap_output_events const *, clap_event_header const *) { return true; }};
    return events;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \class RecordedOutputEvents
///
/// \brief The output list a host hands to `process()` and `params.flush()`, kept
/// rather than dropped.
///
///   `discardedOutputEvents()` above is what every case used, so nothing has ever
/// looked at what the plugin sends *out*: the value events an editor edit becomes
/// and the gesture pair that has to bracket them. An unbalanced gesture leaves a
/// host's automation lane latched in write mode, which is a user-visible fault
/// with no test.
///
/// \note Flattened to what the three event types have in common rather than kept
/// as headers -- a `clap_event_header const *` is only valid for the duration of
/// the `try_push` that carried it, so keeping the pointers would be keeping
/// dangling ones.
///
////////////////////////////////////////////////////////////////////////////////

class RecordedOutputEvents
{
  public:
    struct Event
    {
        std::uint16_t type;
        clap_id id;
        double value; // meaningless for the two gesture types
    }; // struct Event

    RecordedOutputEvents() : list_{this, &push} {}

    RecordedOutputEvents(RecordedOutputEvents const &) = delete; // self-referential ctx
    RecordedOutputEvents &operator=(RecordedOutputEvents const &) = delete;

    clap_output_events const &operator*() const { return list_; }

    std::vector<Event> const &events() const { return events_; }
    void clear() { events_.clear(); }

    /// Every event about \p id, in the order it arrived.
    std::vector<Event> forParameter(clap_id const id) const
    {
        std::vector<Event> mine;
        for (auto const &event : events_)
            if (event.id == id)
                mine.push_back(event);
        return mine;
    }

    std::size_t count(std::uint16_t const type) const
    {
        std::size_t total{0};
        for (auto const &event : events_)
            total += (event.type == type);
        return total;
    }

  private:
    static bool push(clap_output_events const *const pList, clap_event_header const *const pHeader)
    {
        auto &self(*static_cast<RecordedOutputEvents *>(const_cast<void *>(pList->ctx)));

        switch (pHeader->type)
        {
        case CLAP_EVENT_PARAM_VALUE:
        {
            auto const &event(*reinterpret_cast<clap_event_param_value const *>(pHeader));
            self.events_.push_back({pHeader->type, event.param_id, event.value});
            break;
        }
        case CLAP_EVENT_PARAM_GESTURE_BEGIN:
        case CLAP_EVENT_PARAM_GESTURE_END:
        {
            auto const &event(*reinterpret_cast<clap_event_param_gesture const *>(pHeader));
            self.events_.push_back({pHeader->type, event.param_id, 0});
            break;
        }
        default:
            self.events_.push_back({pHeader->type, CLAP_INVALID_ID, 0});
            break;
        }
        return true;
    }

    std::vector<Event> events_;
    clap_output_events list_;
}; // class RecordedOutputEvents

////////////////////////////////////////////////////////////////////////////////
///
/// \brief An input event list holding exactly one parameter value, which is how a
/// host delivers an edit to flush() or process().
///
////////////////////////////////////////////////////////////////////////////////

class OneParameterEvent
{
  public:
    OneParameterEvent(clap_id const id, double const value) : list_{this, size, get}, event_{}
    {
        event_.header.size = sizeof(event_);
        event_.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event_.header.type = CLAP_EVENT_PARAM_VALUE;
        event_.param_id = id;
        event_.note_id = event_.port_index = event_.channel = event_.key = -1;
        event_.value = value;
    }

    OneParameterEvent(OneParameterEvent const &) = delete; // self-referential ctx
    OneParameterEvent &operator=(OneParameterEvent const &) = delete;

    clap_input_events const &operator*() const { return list_; }

  private:
    static std::uint32_t size(clap_input_events const *) { return 1; }
    static clap_event_header const *get(clap_input_events const *self, std::uint32_t)
    {
        return &static_cast<OneParameterEvent const *>(self->ctx)->event_.header;
    }

    clap_input_events list_;
    clap_event_param_value event_;
}; // class OneParameterEvent

////////////////////////////////////////////////////////////////////////////////
// Driving the plugin the way a host does
////////////////////////////////////////////////////////////////////////////////

inline clap_plugin_factory const &factory()
{
    auto const *const pFactory(static_cast<clap_plugin_factory const *>(
        LE::SW::ClapFirst::getFactory(CLAP_PLUGIN_FACTORY_ID)));
    REQUIRE(pFactory != nullptr);
    return *pFactory;
}

/// \brief RAII around the entry point, whose init/deinit are refcounted and must
/// bracket every factory call.
class Entry
{
  public:
    Entry() { REQUIRE(LE::SW::ClapFirst::clapInit("sw-tests")); }
    ~Entry() { LE::SW::ClapFirst::clapDeinit(); }

    Entry(Entry const &) = delete; // makes non-copyable
    Entry &operator=(Entry const &) = delete;
}; // class Entry

/// \brief A plugin taken all the way to "processing", and torn down in order.
class ActivePlugin
{
  public:
    /// \param beforeActivate run against the initialised-but-inactive plugin,
    /// which is when a host restores state and when params.flush() is legal but
    /// the engine has no sample rate yet.
    ActivePlugin(double const sampleRate, std::uint32_t const blockSize,
                 clap_host const &host = nullHost(),
                 std::function<void(clap_plugin const &)> const &beforeActivate = {})
        : sampleRate_(sampleRate), blockSize_(blockSize)
    {
        create(host, beforeActivate);
    }

    /// \brief The same, against a host that can be asked which thread this is.
    ///
    /// \note Which obliges the harness to say. `start_processing`, `process` and
    /// `stop_processing` are `[audio-thread]`, and clap-helpers checks it the
    /// moment a host answers -- so those three are bracketed by
    /// TestHost::AudioCallback here and everything else deliberately is not.
    ActivePlugin(double const sampleRate, std::uint32_t const blockSize, TestHost &host,
                 std::function<void(clap_plugin const &)> const &beforeActivate = {})
        : pTestHost_(&host), sampleRate_(sampleRate), blockSize_(blockSize)
    {
        create(*host, beforeActivate);
    }

    ~ActivePlugin()
    {
        {
            auto const audioThread(scopedAudioCallback());
            pPlugin_->stop_processing(pPlugin_);
        }
        pPlugin_->deactivate(pPlugin_);
        pPlugin_->destroy(pPlugin_);
    }

    ActivePlugin(ActivePlugin const &) = delete; // makes non-copyable
    ActivePlugin &operator=(ActivePlugin const &) = delete;

    clap_plugin const &operator*() const { return *pPlugin_; }
    clap_plugin const *operator->() const { return pPlugin_; }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Patches something into the second input port, as a host does when
    /// a user routes a track into the side chain.
    ///
    /// \note Off by default, and that is not laziness: a host which offers no
    /// side chain hands over `audio_inputs_count == 1`, `runEngine()` falls back
    /// to the main input for it, and that is the arrangement most hosts will
    /// actually use. Both have to work, so the connected one is opt-in per case
    /// and the fallback is what everything else here keeps exercising.
    ///
    /// \note The buffers are held by reference for the plugin's lifetime, the
    /// way a host's own do not move between blocks. Pass a null pair to
    /// `connectSideChainWithNoBuffers()` for the third arrangement: a port that
    /// is present in the count and carries no `data32`, which `runEngine()` also
    /// has to fall back from.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void connectSideChain(std::vector<float> &left, std::vector<float> &right)
    {
        pSideChainLeft_ = &left;
        pSideChainRight_ = &right;
        sideChainPortPresent_ = true;
    }

    /// A second port in the count, with no buffers behind it. \see connectSideChain
    void connectSideChainWithNoBuffers()
    {
        pSideChainLeft_ = pSideChainRight_ = nullptr;
        sideChainPortPresent_ = true;
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief What the host declares about the side-chain port's channels, as a
    /// `clap_audio_buffer::constant_mask`.
    ///
    /// \note Zero -- no claim at all -- is the default, and is what most hosts
    /// and the shipped clap-wrapper are assumed to hand over: the mask is a hint
    /// and nothing obliges anyone to set it. A case that sets one is testing the
    /// arm that reads it. Note that a host setting a bit has undertaken to fill
    /// the channel with the constant, so a case must fill the buffer to match.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void declareSideChainConstant(std::uint64_t const mask) { sideChainConstantMask_ = mask; }

    void disconnectSideChain()
    {
        pSideChainLeft_ = pSideChainRight_ = nullptr;
        sideChainPortPresent_ = false;
        sideChainConstantMask_ = 0;
    }

    /// Runs one block of stereo audio through, in place of a host's callback.
    ///
    /// \param transport what the host reports, nullptr being a host that reports
    /// nothing -- which is the default because most of these cases do not care,
    /// and because it is the harder half of the LFO timing contract.
    void process(std::vector<float> &leftIn, std::vector<float> &rightIn,
                 std::vector<float> &leftOut, std::vector<float> &rightOut,
                 clap_event_transport const *const transport = nullptr,
                 clap_input_events const *const events = nullptr,
                 clap_output_events const *const out = nullptr)
    {
        REQUIRE(processStatus(leftIn, rightIn, leftOut, rightOut, transport, events, out) !=
                CLAP_PROCESS_ERROR);
    }

    /// \brief The same, without asserting.
    ///
    /// \note For the cases that call this from a thread of their own. Catch2's
    /// assertion machinery is not thread safe -- `RunContext::handleExpr` writes
    /// a shared counter -- so a `REQUIRE` on a worker thread is a data race in
    /// the *harness*, and tsan says so at length while saying nothing about the
    /// plugin.
    ///                                       (02.08.2026.) (SW port)
    clap_process_status processStatus(std::vector<float> &leftIn, std::vector<float> &rightIn,
                                      std::vector<float> &leftOut, std::vector<float> &rightOut,
                                      clap_event_transport const *const transport = nullptr,
                                      clap_input_events const *const events = nullptr,
                                      clap_output_events const *const out = nullptr)
    {
        float *inputChannels[]{leftIn.data(), rightIn.data()};
        float *sideChannels[]{pSideChainLeft_ ? pSideChainLeft_->data() : nullptr,
                              pSideChainRight_ ? pSideChainRight_->data() : nullptr};
        float *outputChannels[]{leftOut.data(), rightOut.data()};

        /// \note Two buffers always built, one or two of them declared. The
        /// count is what tells the plugin whether the port is there at all, and
        /// `runEngine()` branches on it *and* on the second buffer's `data32` --
        /// so a port with no buffers is a third arrangement rather than a
        /// nonsense one.
        clap_audio_buffer inputs[]{
            {&inputChannels[0], nullptr, 2, 0, 0},
            {pSideChainLeft_ ? &sideChannels[0] : nullptr, nullptr, 2, 0, sideChainConstantMask_}};
        clap_audio_buffer output{&outputChannels[0], nullptr, 2, 0, 0};

        clap_process process{};
        process.steady_time = -1;
        process.frames_count = blockSize_;
        process.transport = transport;
        process.audio_inputs = &inputs[0];
        process.audio_inputs_count = sideChainPortPresent_ ? 2u : 1u;
        process.audio_outputs = &output;
        process.audio_outputs_count = 1;
        process.in_events = events ? events : &noInputEvents();
        process.out_events = out ? out : &discardedOutputEvents();

        auto const audioThread(scopedAudioCallback());
        return pPlugin_->process(pPlugin_, &process);
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief params.flush() as a host calls it against an *active* plugin,
    /// which is on the audio thread.
    ///
    /// \note `[active ? audio-thread : main-thread]` (ext/params.h:303), and this
    /// plugin is active for its whole lifetime here -- so the window is open.
    /// That is not a detail: `paramsFlush()` calls `drainCommands()` and
    /// `handleEvent()`, the same two things `process()` does to the engine, and
    /// it is allowed to only because the thread it runs on is the one that owns
    /// the engine. Reaching the *main*-thread half of anything therefore means
    /// flushing before `activate()` -- `beforeActivate` above -- or going through
    /// the editor, not calling this.
    ///
    ///   Found by TestHost offering `clap.log`: clap-helpers reported "Host
    /// called the method clap_plugin_params.flush() on wrong thread" the first
    /// time anything was listening.
    ///                                       (03.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    void flush(clap_input_events const *const events = nullptr,
               clap_output_events const *const out = nullptr) const
    {
        auto const *const pParams(static_cast<clap_plugin_params const *>(
            pPlugin_->get_extension(pPlugin_, CLAP_EXT_PARAMS)));
        REQUIRE(pParams != nullptr);
        {
            auto const audioThread(scopedAudioCallback());
            pParams->flush(pPlugin_, events ? events : &noInputEvents(),
                           out ? out : &discardedOutputEvents());
        }

        /// \note And then the callback the plugin asked for, because a real host
        /// runs one. A parameter delivered here is applied to the engine on this
        /// thread-in-the-audio-role and echoed to the main thread; until the
        /// echo is drained the main thread's Program -- which is what
        /// `paramsValue` and `paramsInfo` answer from -- is one drain behind.
        /// Every case that flushes a value and then reads it back depends on
        /// this, and none of them had a reason to say so before there were two
        /// copies to keep level.
        ///                                   (06.08.2026.) (SW port)
        pumpMainThread();
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief `clap_plugin::on_main_thread`, which a host calls after the plugin
    /// asks through `request_callback`.
    ///
    /// \note Nothing here answers that request on its own -- `TestHost` counts it
    /// and `nullHost()` drops it -- so a case that needs the *main thread's* copy
    /// of the model brought level has to say when. That is where a host parameter
    /// write, applied to the engine on the audio thread, is echoed into
    /// `programMain_`; until it runs, the two are legitimately one drain apart.
    ///                                       (06.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    void pumpMainThread() const { pPlugin_->on_main_thread(pPlugin_); }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief reset() as a host calls it: `[audio-thread & active]`, and
    /// **between blocks rather than under one**.
    ///
    /// \note Which is the whole point of it, and the reason it is its own helper
    /// rather than something process() does. Nothing in this tree called reset()
    /// at all until 03.08.2026; `vst3-validator` was the first thing that did,
    /// through `ClapAsVst3::setProcessing(false)`, and it found that the plugin
    /// only declared itself to own the engine underneath process(). See
    /// `Threading::ScopedAudioThreadEntry`.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void reset() const
    {
        auto const audioThread(scopedAudioCallback());
        pPlugin_->reset(pPlugin_);
    }

    /// \brief Deactivates and reactivates, as a host does when the plugin asks.
    ///
    /// \note Unconditionally rather than on `request_restart`, because the null
    /// host cannot report one and because a restart the plugin did not ask for is
    /// something a host is free to do anyway. It is also where a pending spectral
    /// setup lands, so a case that changes the FFT size and never comes through
    /// here is running the old one.
    void restartIfAsked()
    {
        {
            auto const audioThread(scopedAudioCallback());
            pPlugin_->stop_processing(pPlugin_);
        }
        pPlugin_->deactivate(pPlugin_);
        REQUIRE(pPlugin_->activate(pPlugin_, sampleRate_, 1, blockSize_));
        auto const audioThread(scopedAudioCallback());
        REQUIRE(pPlugin_->start_processing(pPlugin_));
    }

    static char const *descriptorID()
    {
        auto const *const pDescriptor(factory().get_plugin_descriptor(&factory(), 0));
        REQUIRE(pDescriptor != nullptr);
        return pDescriptor->id;
    }

  private:
    void create(clap_host const &host,
                std::function<void(clap_plugin const &)> const &beforeActivate)
    {
        pPlugin_ = factory().create_plugin(&factory(), &host, descriptorID());
        REQUIRE(pPlugin_ != nullptr);
        REQUIRE(pPlugin_->init(pPlugin_));
        if (beforeActivate)
            beforeActivate(*pPlugin_);
        REQUIRE(pPlugin_->activate(pPlugin_, sampleRate_, 1, blockSize_));
        auto const audioThread(scopedAudioCallback());
        REQUIRE(pPlugin_->start_processing(pPlugin_));
    }

    /// The audio-thread window, or nothing at all against a host that cannot be
    /// asked which thread this is and therefore has nothing to be told.
    std::unique_ptr<TestHost::AudioCallback> scopedAudioCallback() const
    {
        return pTestHost_ ? std::make_unique<TestHost::AudioCallback>(*pTestHost_) : nullptr;
    }

    TestHost *pTestHost_{nullptr};
    clap_plugin const *pPlugin_{nullptr};
    double sampleRate_;
    std::uint32_t blockSize_;

    /// \see connectSideChain
    std::vector<float> *pSideChainLeft_{nullptr};
    std::vector<float> *pSideChainRight_{nullptr};
    bool sideChainPortPresent_{false};
    std::uint64_t sideChainConstantMask_{0};
}; // class ActivePlugin

inline clap_plugin_params const &parameters(clap_plugin const &plugin)
{
    auto const *const params(
        static_cast<clap_plugin_params const *>(plugin.get_extension(&plugin, CLAP_EXT_PARAMS)));
    REQUIRE(params != nullptr);
    return *params;
}

/// Every parameter's description, indexed the way the host reads them.
inline std::vector<clap_param_info> allParameterInfo(clap_plugin const &plugin,
                                                     clap_plugin_params const &params)
{
    std::vector<clap_param_info> infos(params.count(&plugin));
    for (std::uint32_t index(0); index < infos.size(); ++index)
        REQUIRE(params.get_info(&plugin, index, &infos[index]));
    return infos;
}

////////////////////////////////////////////////////////////////////////////////
// Naming a parameter the way the host addresses it
////////////////////////////////////////////////////////////////////////////////

/// \brief An LFO period -- the `.T` of some slot's LFO.
///
/// \note The family `clap-cpp-validator` fails on, every one of them: its four
/// state cases disagree only about `M*.*.LFO.T`. Found by name rather than by a
/// hardcoded id because which LFO-able parameters a slot has depends on the
/// effect in it.
inline clap_param_info lfoPeriodParameter(clap_plugin const &plugin,
                                          clap_plugin_params const &params)
{
    for (auto const &info : allParameterInfo(plugin, params))
    {
        std::string const name(info.name);
        if ((name.size() > 2) && (name.compare(name.size() - 2, 2, ".T") == 0))
            return info;
    }
    FAIL("no LFO period parameter -- is there an effect in slot 1?");
    return {};
}

/// \brief A host's transport, at \p tempo in \p beatsPerBar / \p beatUnit,
/// parked at \p positionInBeats.
///
/// \note Shared rather than per file since 03.08.2026: `pluginTests.cpp` had it,
/// and the LFO cases need one too -- and need it at a tempo that is *not* the
/// engine's assumed 120, which is where the periods stop being rescaled.
///
/// \note The meter is a parameter rather than a hardcoded 4/4 since 10.08.2026,
/// which is issue #14: `tsig_num` is what reaches the engine as the measure
/// numerator, and every division a synced LFO period can land on is a divisor of
/// it -- so an arm of `updateForNewTimingInformation()` and the meter half of
/// `Timer::establishedChange()` had nothing in the suite driving them. The
/// default is still 4/4, so a caller that does not care reads as it always did.
///
/// \note `beatUnit` reaches nothing: the engine's bar is `tsig_num` beats of
/// `60 / tempo` seconds, and CLAP's tempo is in quarter notes per minute
/// whatever the denominator says. It is set anyway because a transport that
/// announced `CLAP_TRANSPORT_HAS_TIME_SIGNATURE` with a zero denominator would
/// be describing no meter at all -- 6/8 has to arrive as 6/8 rather than as 6/4
/// for the case naming it to be about anything.
inline clap_event_transport transportAt(double const tempo, double const positionInBeats,
                                        std::uint32_t const extraFlags,
                                        std::uint16_t const beatsPerBar = 4,
                                        std::uint16_t const beatUnit = 4)
{
    clap_event_transport transport{};
    transport.header.size = sizeof(transport);
    transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    transport.header.type = CLAP_EVENT_TRANSPORT;
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_TIME_SIGNATURE |
                      CLAP_TRANSPORT_HAS_BEATS_TIMELINE | extraFlags;
    transport.tempo = tempo;
    transport.tsig_num = beatsPerBar;
    transport.tsig_denom = beatUnit;
    transport.song_pos_beats = static_cast<clap_beattime>(positionInBeats * CLAP_BEATTIME_FACTOR);
    return transport;
}

/// \note ParameterID's members are laid out in reverse so that the hex reads
/// naturally on a little-endian machine: the type is the top byte and the module
/// index the one below it. See core/parameterID.hpp.
///
/// \note Taken from the plugin's own enum rather than restated. These were four
/// literals until 07.08.2026, when the discriminator moved off zero so that no
/// valid ID could be `0x00000000` -- and a hand-copied table agrees with the
/// thing it copies only until somebody changes it.
enum ParameterType : clap_id
{
    globalType = LE::SW::ParameterID::GlobalParameter,
    moduleChainType = LE::SW::ParameterID::ModuleChainParameter,
    moduleType = LE::SW::ParameterID::ModuleParameter,
    lfoType = LE::SW::ParameterID::LFOParameter
};

inline clap_id parameterID(ParameterType const type, unsigned const moduleIndex = 0,
                           unsigned const parameterIndex = 0)
{
    return (static_cast<clap_id>(type) << 24) | (moduleIndex << 16) | parameterIndex;
}

inline bool isNormalisedType(clap_id const id)
{
    auto const type(id >> 24);
    return (type == moduleType) || (type == lfoType);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The plugin as the editor sees it -- the queue and the mailbox.
///
/// \note A public base of SpectrumWorxCLAP, which is how the editor reaches them
/// without sw-gui naming the plugin. A test stands in for the editor here, and
/// reaching it needs the C++ object out of `clap_plugin::plugin_data` -- a test
/// knowing more than the C API says, because what is being driven is the thread
/// the C API has no name for.
///
////////////////////////////////////////////////////////////////////////////////

inline LE::SW::GUI::EditorHost &editorHostOf(clap_plugin const &plugin)
{
    auto *const pHelper(static_cast<LE::SW::PluginHelper *>(plugin.plugin_data));
    REQUIRE(pHelper != nullptr);
    return *static_cast<LE::SW::SpectrumWorxCLAP *>(pHelper);
}

} // namespace SWTest

#endif // testHost_hpp
