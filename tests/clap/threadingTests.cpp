////////////////////////////////////////////////////////////////////////////////
///
/// \file threadingTests.cpp
/// ------------------------
///
///   What happens when the two threads the plugin has are both busy.
///
///   Everything else under `tests/clap/` drives one thread at a time, which is
/// the right way to test what a call *does*. This file is for the cases where
/// the answer depends on two of them overlapping: a preset arriving while blocks
/// are being rendered, a restart asked for from both sides at once, a chain
/// built against a spectral setup that changes before it is installed.
///
/// \note Most of these cannot fail deterministically, and saying so is the point
/// rather than an apology. A data race is undefined behaviour, not a wrong
/// answer that shows up once in a hundred runs -- the compiler is entitled to
/// keep a racy read in a register and it does. So each case here does two jobs:
/// it asserts the outcome that must hold however the two threads interleave, and
/// it *creates* the overlap so that a `-fsanitize=thread` build has something to
/// report. The second is what actually pins the fix; the first is what keeps the
/// case honest in the ordinary build everyone runs.
///
///   To run them as they were written to be run:
///
///     cmake -B build-tsan -D SW_SANITIZER=thread -D SW_BUILD_PLUGIN_BUNDLES=OFF
///     cmake --build build-tsan --target sw-plugin-tests
///     ./build-tsan/tests/sw-plugin-tests "[threading]"
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "clap/testHost.hpp"

#include "core/modules/moduleDSPAndGUI.hpp" // the engine's own Module, to count references to one
#include "core/spectrumWorxCore.hpp"
#include "gui/editor/presetLoading.hpp"
#include "le/spectrumworx/presetStorage.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <thread>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace SWTest;

constexpr double sampleRate{48000};
constexpr std::uint32_t blockSize{64};

/// \brief A factory preset whose FFT size is not the default one.
///
/// \note 8192 against the engine's default 2048, and named rather than swept.
/// The whole family of Tier 2 findings is about a *spectral* parameter moving
/// while audio runs -- a preset that happens to agree with the running setup
/// exercises none of it, and there are 213 of those. This one is also the
/// largest FFT the build offers, so the working set it asks for grows rather
/// than shrinks: a chain installed against the old size has too little room
/// rather than too much, which is the difference between a wrong answer and a
/// heap overrun.
std::filesystem::path presetWithABiggerFFT()
{
    std::filesystem::path const preset(std::filesystem::path(SW_PRESET_DATA_DIR) / "Gamma Shift" /
                                       "Whistle A Tune.swp");
    REQUIRE(std::filesystem::is_regular_file(preset));
    return preset;
}

unsigned int runningFFTSize(clap_plugin const &plugin)
{
    return editorHostOf(plugin).core().uncheckedEngineSetup().fftSize<unsigned int>();
}

/// \brief How many references \p module is being held by.
///
/// \note Widened, because the count is a `std::uint8_t` and Catch2 prints one as
/// a character -- so a failed comparison reads "  == 2", with the value it
/// actually found rendered as an unprintable byte.
unsigned int references(LE::SW::Module const &module)
{
    return LE::SW::Engine::node(module).referenceCount_;
}

/// \brief The description of one parameter, found by the id a host addresses it
/// by rather than by its position in the list.
clap_param_info infoFor(clap_plugin const &plugin, clap_id const id)
{
    for (auto const &info : allParameterInfo(plugin, parameters(plugin)))
        if (info.id == id)
            return info;
    FAIL("no parameter with that id");
    return {};
}

////////////////////////////////////////////////////////////////////////////////
///
/// \class AudioThread
///
/// \brief A thread rendering blocks back to back for as long as it is in scope,
/// the way a host with the transport rolling does.
///
/// \note `processStatus` rather than `process`, and no `REQUIRE` anywhere on
/// this thread: Catch2's assertion machinery writes a shared counter, so an
/// assertion here is a race in the harness that tsan reports at length while
/// saying nothing about the plugin. What this thread saw is carried out in
/// atomics and checked after the join.
///
////////////////////////////////////////////////////////////////////////////////

class AudioThread
{
  public:
    /// \param automation an event list delivered with every block, as a host with
    /// a lane running does. Null is a host that is only rendering.
    explicit AudioThread(ActivePlugin &plugin, clap_input_events const *const automation = nullptr)
        : thread_([this, &plugin, automation] {
              std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
              std::vector<float> leftOut(blockSize), rightOut(blockSize);
              while (!stop_.load(std::memory_order_acquire))
              {
                  if (plugin.processStatus(leftIn, rightIn, leftOut, rightOut, nullptr,
                                           automation) == CLAP_PROCESS_ERROR)
                      failed_.store(true, std::memory_order_release);
                  blocks_.fetch_add(1, std::memory_order_acq_rel);
              }
          })
    {
        // Nothing the case does happens before the audio thread is really going.
        while (blocks_.load(std::memory_order_acquire) == 0)
        {
        }
    }

    ~AudioThread() { join(); }

    AudioThread(AudioThread const &) = delete; // makes non-copyable
    AudioThread &operator=(AudioThread const &) = delete;

    void join()
    {
        if (!thread_.joinable())
            return;
        stop_.store(true, std::memory_order_release);
        thread_.join();
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Waits until \p count further blocks have been rendered.
    ///
    /// \note For anything that asserts about what the audio thread *did* with
    /// something the main thread queued. Stopping the thread does not drain the
    /// queue -- `join()` sets the flag and the block in flight finishes -- so a
    /// case that queues and then joins is asserting on whether the scheduler
    /// happened to fit another block in. Three, not one: a block already under
    /// way when the push landed did not see it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void waitForMoreBlocks(unsigned int const count = 3) const
    {
        auto const target(blocks_.load(std::memory_order_acquire) + count);
        while (blocks_.load(std::memory_order_acquire) < target)
        {
        }
    }

    /// \note Only after join(), which is what makes reading them ordered.
    bool failed() const { return failed_.load(std::memory_order_acquire); }
    unsigned int blocks() const { return blocks_.load(std::memory_order_acquire); }

  private:
    std::atomic<bool> stop_{false};
    std::atomic<bool> failed_{false};
    std::atomic<unsigned int> blocks_{0};
    std::thread thread_;
}; // class AudioThread

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
///
/// \note T2.5. Three flags crossed threads as plain `bool`s and this is the case
/// that has two threads at all of them at once.
///
///   `restartRequested_` is the one with something to lose. It gates
/// `clap_host::request_restart`, and it is read-and-then-written at two sites --
/// `drainCommands()`'s tail on the audio thread and
/// `HostProxy::automatedParameterChanged` on the main thread, which is where a
/// preset load arrives. "Test it and set it" is not one operation on a `bool`, so
/// the two can agree to ask twice; worse, a racy read is undefined behaviour and
/// the compiler may hold it in a register, in which case the request is never
/// made at all. Nothing then applies the pending setup: the engine goes on
/// running one FFT size while every parameter readout says another, until some
/// unrelated change asks again.
///
///   What the case can assert without depending on an interleaving is the
/// outcome: whichever thread wins, the host is asked, and once it honours the
/// restart the engine is running the size the preset named. What pins the fix is
/// the tsan run -- before it, `spectralSetupPending_`, `restartRequested_` and
/// `blockAutomation_` are each named as a write/read race between the two
/// threads.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A preset that changes the FFT size while audio runs still gets its restart",
          "[clap][threading][presets]")
{
    Entry const entry;

    TestHost host(TestHost::everything());
    ActivePlugin plugin(sampleRate, blockSize, host);

    REQUIRE(runningFFTSize(*plugin) != 8192);

    auto presetData(LE::SW::readPresetFile(presetWithABiggerFFT()));
    REQUIRE(static_cast<bool>(presetData));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And a lane running while the browser is clicked, which is what makes
    /// the third of the three flags cross threads at all. `blockAutomation_` is
    /// raised by the preset loader on the main thread and was read by
    /// `SpectrumWorxCore::blockAutomation()`'s assertion, reached from
    /// `setParameter()` -- which for a host automation event is the audio thread.
    /// With no events in the block there is no reader and the flag looks
    /// single-threaded.
    ///
    ///   Adding them is what turned that assertion up: it claimed a preset load
    /// could not be in progress here, and roughly one run in ten proved otherwise
    /// by aborting inside the audio callback. A checked build did that on two
    /// ordinary user actions at once, and a shipped build did nothing whatsoever,
    /// `LE_ASSERT` being a no-op there. \see SpectrumWorxCore::blockAutomation().
    ///
    /// \note The first global parameter, whose default value is a legal one by
    /// construction, and deliberately not a spectral one: a lane fighting the
    /// preset over the FFT size would be testing which of them won rather than
    /// whether the two threads may touch these at all.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const inputGain(infoFor(*plugin, parameterID(globalType, 0, 0)));
    OneParameterEvent const automation(inputGain.id, inputGain.default_value);

    {
        AudioThread audio(plugin, &*automation);

        /// \note No editor, so nothing is reported to a user and nothing draws --
        /// this is the load itself against a running engine, which is what a
        /// browser click with the transport rolling is underneath.
        REQUIRE(LE::SW::GUI::loadPreset(editorHostOf(*plugin), nullptr, presetData.get(),
                                        true /*ignore external samples*/, nullptr, "Whistle"));

        /// \note The preset's parameters are queued, so the audio thread has to
        /// be given blocks in which to pick them up. \see waitForMoreBlocks.
        audio.waitForMoreBlocks();

        audio.join();
        CHECK_FALSE(audio.failed());
    }

    // The preset moved a spectral parameter, so somebody had to ask.
    CHECK(host.restartRequests >= 1);

    /// \note And the restart is where it lands. Until the host honours one the
    /// engine is *supposed* to still be running the old size -- that is the whole
    /// design of the deferral -- so this is the assertion that the request was not
    /// lost on the way.
    plugin.restartIfAsked();
    CHECK(runningFFTSize(*plugin) == 8192);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note T2.1, and it needs no second thread at all -- only a host that does not
/// render between the preset and the restart it asks for. Logic with the
/// transport parked is the reported one; every host does it while a track is
/// muted or the plugin is offline.
///
///   A preset is parsed into a chain built against the storage factors in force
/// at the time, and handed over through `Threading::publishChain()`, which with
/// audio running *queues* it. The restart the preset's FFT size then asks for
/// lands in `deactivate()`, which resized the live chain -- and the preset's was
/// still in the ring. So the first `process()` after the restart spliced in
/// modules sized for 2048 and ran them at 8192, writing per-bin channel state
/// past the end of the block each of them owns.
///
///   The case is written around the *observable* half of that: which chain ends
/// up live and at what size. The overrun itself needs a sanitizer to see, and an
/// `-fsanitize=address` build reports it here as a heap-buffer-overflow inside
/// the first block.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A chain queued behind a restart is resized before it is played",
          "[clap][threading][presets]")
{
    Entry const entry;

    TestHost host(TestHost::everything());
    ActivePlugin plugin(sampleRate, blockSize, host);

    auto &engine(editorHostOf(*plugin).core());
    REQUIRE(runningFFTSize(*plugin) != 8192);
    REQUIRE(engine.moduleChain().size() == 0);

    auto presetData(LE::SW::readPresetFile(presetWithABiggerFFT()));
    REQUIRE(static_cast<bool>(presetData));

    REQUIRE(LE::SW::GUI::loadPreset(editorHostOf(*plugin), nullptr, presetData.get(),
                                    true /*ignore external samples*/, nullptr, "Whistle"));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Not one block between the load and the restart, which is the whole
    /// case. The plugin is active, so `publishChain()` queued the chain instead
    /// of installing it, and nothing has drained the ring: the engine is still
    /// running the previous, empty chain at the previous FFT size.
    ///
    ////////////////////////////////////////////////////////////////////////////
    CHECK(engine.moduleChain().size() == 0);
    CHECK(runningFFTSize(*plugin) != 8192);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note What the plugin asks for is a *flush*, not a restart -- and this
    /// host declines to give it one, which is the whole point of the case. The
    /// preset's parameters and its chain are both in the queue; nothing has
    /// drained them, so nothing has yet noticed that the FFT size moved, so
    /// there is nothing to ask the host to restart *for*. A host that honoured
    /// the flush would drain both and ask on the spot -- that is the case above,
    /// where an audio thread does the draining.
    ///
    ////////////////////////////////////////////////////////////////////////////
    CHECK(host.flushRequests >= 1);
    CHECK(host.restartRequests == 0);

    /// \note So the restart here is one this host does of its own accord, which
    /// it is always free to do. `deactivate()` is where both queues are emptied
    /// and where the setup lands.
    plugin.restartIfAsked();

    // The restart is where both halves land, and they have to land together.
    CHECK(runningFFTSize(*plugin) == 8192);
    CHECK(engine.moduleChain().size() == 3);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And then the blocks that used to run the old modules at the new
    /// size. Enough of them to reach a spectral frame, which is the part that
    /// took some finding: at 8192 with an overlap factor of 4 a hop is 2048
    /// samples and a block here is 64, so **no module runs at all for the first
    /// 32 blocks**. One block after the restart looks perfectly healthy.
    ///
    ///   What it looks like when it is not: `Math::multiply` reports "Buffer
    /// sizes mismatch" from inside `PitchShifter::process`, an input of the new
    /// FFT's width against an output sized for the old one -- and then writes the
    /// input's length anyway. Only the engine's own bounds assertion sees it.
    /// ASan does not: modules are suballocated out of one `HeapSharedStorage`
    /// arena, so running off the end of a module's range lands in the next
    /// module's, which is a live part of an allocation the sanitizer knows to be
    /// live. In a shipped build the assertion is not compiled and the write is
    /// simply made.
    ///
    ////////////////////////////////////////////////////////////////////////////

    std::vector<float> leftIn(blockSize, 0.25f), rightIn(blockSize, 0.25f);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);
    for (unsigned int block(0); block < 64; ++block)
    {
        plugin.process(leftIn, rightIn, leftOut, rightOut);
        REQUIRE(std::all_of(leftOut.begin(), leftOut.end(),
                            [](float const sample) { return std::isfinite(sample); }));
    }
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note T2.6. `activate()` is `[main-thread & !active]`, and a host that ignores
/// the second half of that gets the whole spectral working set reallocated under
/// a `process()` that is reading it.
///
///   clap-helpers is not the backstop it looks like. It reports the double
/// activation, and when the sample rate differs it simulates a deactivation
/// first -- but at the *same* rate it reports and calls through, with only an
/// `assert` in between, which no shipped build compiles.
///
/// \note Against `nullHost()` deliberately, where every other case here uses a
/// `TestHost`. Offering `clap.thread-check` would turn clap-helpers' own contract
/// checking on, and this harness cannot answer it truthfully with two threads
/// live: `TestHost` tracks one audio-callback window for the whole host, so while
/// the audio thread is inside `process()` it answers "not the main thread" to the
/// main thread as well. That is the harness's limit, not the plugin's, and
/// borrowing it here would only produce a misbehaviour report about the test.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A second activate while the first is still processing changes nothing",
          "[clap][threading]")
{
#ifndef NDEBUG
    SKIP("A checked build cannot reach the plugin at all: clap-helpers' own "
         "`assert( !_isActive )` aborts first. That assertion is exactly what a shipped build "
         "does not have, which is what this is about.");
#endif // NDEBUG

    Entry const entry;

    ActivePlugin plugin(sampleRate, blockSize);
    auto const fftSizeBefore(runningFFTSize(*plugin));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note An input gain that is not unity, delivered with every block, and it
    /// is what makes the reallocation observable rather than merely wrong.
    /// `SpectrumWorxCore::process()` passes the host's own pointers straight
    /// through while the gain is 1 and copies into `buffers_` only when it is
    /// not -- so at the default gain the buffers `setBlockSize()` frees are
    /// buffers nothing is reading.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const inputGain(infoFor(*plugin, parameterID(globalType, 0, 0)));
    OneParameterEvent const loud(inputGain.id, inputGain.max_value);

    {
        AudioThread audio(plugin, &*loud);

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \note The same sample rate and a *different* maximum block size, which
        /// is the arrangement with something to reallocate. clap-helpers keys its
        /// simulated deactivation on the sample rate alone, so this walks
        /// straight through it -- and at an unchanged rate and block size
        /// `resize()` finds nothing to do, which is why activating twice with
        /// identical arguments is harmless and proves nothing.
        ///
        ///   Many, not one: the window in which a reallocation overlaps a block
        /// is the length of a `process()` call.
        ///
        ////////////////////////////////////////////////////////////////////////
        for (unsigned int attempt(0); attempt < 256; ++attempt)
            CHECK(plugin->activate(&*plugin, sampleRate, 1,
                                   (attempt % 2) ? blockSize * 8 : blockSize));

        audio.join();
        CHECK_FALSE(audio.failed());
        CHECK(audio.blocks() > 0);
    }

    // Still the plugin it was, and still able to render.
    CHECK(runningFFTSize(*plugin) == fftSizeBefore);

    std::vector<float> leftIn(blockSize, 0.25f), rightIn(blockSize, 0.25f);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);
    plugin.process(leftIn, rightIn, leftOut, rightOut);
    CHECK(std::all_of(leftOut.begin(), leftOut.end(),
                      [](float const sample) { return std::isfinite(sample); }));
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note T2.2, and the one route into the module chain that never joined the
/// retire protocol.
///
///   A slot selector is a parameter, so a host can write one, and a host writes
/// parameters inside `process()`. `AutomatedModuleChain::setParameter` unlinked
/// the module that was there and let the chain's reference go -- and with no
/// strip on screen holding one of its own, that was the last: `delete`, and a
/// `HeapSharedStorage` free, on the audio thread. Every other way a module leaves
/// the chain hands it to `retire()` and the main thread frees it (§5).
///
/// \note The case holds a reference of its own, which is what makes the
/// difference measurable rather than merely fatal: with one outstanding, the
/// unlink is not the last release either way, so what is left to observe is
/// whether anybody *else* took the module up. Retired, the count is ours and the
/// queue's; freed on the spot it would have been ours alone.
///
///   The other half of it is a realtime-sanitizer run, where the free inside the
/// callback is reported directly:
///
///     cmake -B build-rtsan -D SW_SANITIZER=realtime -D SW_BUILD_PLUGIN_BUNDLES=OFF
///           -D CMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A module the host displaces is freed on the main thread", "[clap][threading]")
{
    Entry const entry;

    TestHost host(TestHost::everything());
    ActivePlugin plugin(sampleRate, blockSize, host);

    auto &editorHost(editorHostOf(*plugin));
    auto &engine(editorHost.core());

    std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);

    // A filled slot, put there the way the interface fills one.
    REQUIRE(editorHost.editSlot(0, 0));
    plugin.process(leftIn, rightIn, leftOut, rightOut);
    plugin.pumpMainThread();
    REQUIRE(engine.moduleChain().size() == 1);

    /// \note Ours, and it has to be taken while the module is still in the chain
    /// -- afterwards there may be nothing left to take one to.
    auto const keep(engine.moduleChain().moduleAs<LE::SW::Module>(0));
    REQUIRE(keep);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And the host empties it. `min_value` rather than a literal -1: the
    /// selector's "no module" is the bottom of its advertised range, and a host
    /// writing the bottom of the range is what this is.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const selector(infoFor(*plugin, parameterID(moduleChainType, 0, 0)));
    OneParameterEvent const empty(selector.id, selector.min_value);
    plugin.process(leftIn, rightIn, leftOut, rightOut, nullptr, &*empty);

    REQUIRE(engine.moduleChain().size() == 0);

    // The chain let go, and the retire queue took it up: ours, and one more.
    CHECK(references(*keep) == 2);

    // ...which the main thread collects, and only then is the module gone.
    plugin.pumpMainThread();
    CHECK(references(*keep) == 1);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And once more holding nothing, which is the arrangement a user
    /// actually has: no window, no strip, the host's own lane moving the
    /// selector. Nothing in an ordinary build can tell the two apart -- freed by
    /// the drain or freed inside the `process()` above, the chain that is left
    /// is the same -- so this round is here for the realtime sanitizer, which
    /// sees the `free()` itself and reports the call that made it.
    ///
    ////////////////////////////////////////////////////////////////////////////

    REQUIRE(editorHost.editSlot(0, 0));
    plugin.process(leftIn, rightIn, leftOut, rightOut);
    plugin.pumpMainThread();
    REQUIRE(engine.moduleChain().size() == 1);

    plugin.process(leftIn, rightIn, leftOut, rightOut, nullptr, &*empty);
    CHECK(engine.moduleChain().size() == 0);
    plugin.pumpMainThread();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note T2.3. A preset load is `[main-thread]` and it was writing the six
/// global parameters straight into the engine -- the ones `process()` reads on
/// every block, from the other thread, with nothing between them. A user
/// browsing presets with the transport rolling is not an exotic case; it is the
/// case. Thread sanitizer names it as a write/read race on the parameter itself.
///
///   They travel by queue now, which is what makes the timing visible and worth
/// asserting: for the length of one drain the engine is legitimately still
/// running the previous preset's gain. That is exactly the arrangement the
/// module chain has always been in -- `publishChain()` queues it too -- and the
/// two now arrive together instead of one going round the other.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A preset's global parameters reach a running engine through the queue",
          "[clap][threading][presets]")
{
    using LE::SW::GlobalParameters::OutputGain;

    Entry const entry;

    TestHost host(TestHost::everything());
    ActivePlugin plugin(sampleRate, blockSize, host);

    auto &engine(editorHostOf(*plugin).core());

    /// \note The preset's output gain is 2, and the default is 1 -- a global
    /// that is not spectral, so what is being watched is the parameter itself
    /// rather than the setup it triggers.
    REQUIRE_THAT(float(engine.parameters().get<OutputGain>()),
                 Catch::Matchers::WithinAbs(1.0, 0.001));

    auto presetData(LE::SW::readPresetFile(presetWithABiggerFFT()));
    REQUIRE(static_cast<bool>(presetData));
    REQUIRE(LE::SW::GUI::loadPreset(editorHostOf(*plugin), nullptr, presetData.get(),
                                    true /*ignore external samples*/, nullptr, "Whistle"));

    // Queued, not written: the audio thread has not been given a chance yet.
    CHECK_THAT(float(engine.parameters().get<OutputGain>()),
               Catch::Matchers::WithinAbs(1.0, 0.001));

    /// \note And the plugin asked to be drained, which is the half a parked host
    /// depends on: with no block coming, `params.flush()` is the only thing that
    /// will ever apply this preset.
    CHECK(host.flushRequests >= 1);

    plugin.flush();

    CHECK_THAT(float(engine.parameters().get<OutputGain>()),
               Catch::Matchers::WithinAbs(2.0, 0.001));
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note And the encoding those edits cross in, which T2.3 turned up rather than
/// caused. `EditorHost::editParameter` takes the value the *host automation
/// edge* speaks, and `FullRangeAutomatedParameter` carries a power-of-two
/// parameter as its exponent -- so an FFT size crosses as 11, not as 2048.
/// Everything on the interface side holds 2048.
///
///   Nothing converted. The settings page handed `queueGlobalParameter` a raw
/// 2048, which in a checked build asserted inside
/// `convertLinearRange2PowerOfTwo` -- its six-wide source range is 7 to 13 --
/// and in a shipped build produced whatever that arithmetic makes of a number
/// two orders of magnitude outside it. The three *knobs* were fine, being
/// linear, which is why it survived: the fault was in the three combo boxes
/// nothing drives headlessly.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A global parameter crosses to the engine in the units the edge speaks",
          "[clap][threading]")
{
    using LE::SW::GlobalParameters::FFTSize;
    using LE::SW::GlobalParameters::Parameters;

    Entry const entry;

    ActivePlugin plugin(sampleRate, blockSize);
    auto &engine(editorHostOf(*plugin).core());

    REQUIRE(unsigned(engine.parameters().get<FFTSize>()) != 1024u);

    // What the settings page does when the user picks 1024 from the list.
    editorHostOf(*plugin).editGlobalParameter(LE::Parameters::IndexOf<Parameters, FFTSize>::value,
                                              1024);

    std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);
    plugin.process(leftIn, rightIn, leftOut, rightOut);

    // 1024, and not the 2048 an unconverted value would have left in place.
    CHECK(unsigned(engine.parameters().get<FFTSize>()) == 1024u);

    plugin.restartIfAsked();
    CHECK(runningFFTSize(*plugin) == 1024);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The gap a "closed" debt entry left behind. The N/T/D buttons were
/// recorded as fixed on the day the waveform popup was, and only the waveform
/// had a route: the sync-mode branch wrote `LFO::addSyncType()` on the strip's
/// own LFO -- `programMain_`'s -- and queued nothing, so a sync change moved the
/// display and the saved state and nothing anybody could hear.
///
///   **Nothing in the suite had ever read the engine's side of an LFO after a UI
/// edit.** The display, `paramsValue`, `stateSave` and the preset writer all
/// answer from the main thread's copy, so every case there was agreed with a
/// change the audio thread never received; `lfoDisplayTests.cpp` closed half of
/// that by asserting the *message* was queued. These two read the far end.
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
using EngineLFO = LE::Parameters::LFOImpl;

/// \brief The plugin with an effect in slot 0 and both copies of it in hand.
///
/// \note Both, and reached separately, because a case that reads only one of
/// them is the case that missed this. `programMain()` is the editor's; `core()`
/// is the engine's, and nothing writes it but `drainCommands()`.
struct BothLFOs
{
    explicit BothLFOs(ActivePlugin const &plugin, std::uint8_t const lfoable = 0)
    {
        auto &editorHost(editorHostOf(*plugin));

        REQUIRE(editorHost.editSlot(0, 0));
        plugin.flush();

        auto const engineModule(editorHost.core().moduleChain().moduleAs<LE::SW::Module>(0));
        auto const mainModule(editorHost.programMain().moduleChain().moduleAs<LE::SW::Module>(0));
        REQUIRE(engineModule);
        REQUIRE(mainModule);
        REQUIRE(engineModule.get() != mainModule.get()); // two modules, not one
        REQUIRE(lfoable < engineModule->numberOfLFOControledParameters());

        pEngine = &engineModule->lfo(lfoable);
        pMain = &mainModule->lfo(lfoable);
    }

    EngineLFO const *pEngine{nullptr};
    EngineLFO *pMain{nullptr};
}; // struct BothLFOs
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
///
/// \note The two an LFO *is*. They had no `ParameterID` until issue #159 and
/// travelled down a channel of their own -- `SetUnexportedLFOParameter`,
/// addressed by index, with the interface writing its own copy in a separate
/// call, which is exactly how the two came apart. They are ordinary parameters
/// now and `editParameter` moves both copies in one call.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("An LFO's sync and waveform reach the engine", "[clap][threading][lfo]")
{
    using LE::Parameters::IndexOf;
    using LE::Parameters::LFO;

    constexpr std::uint8_t syncTypesIndex(
        IndexOf<EngineLFO::Parameters, EngineLFO::SyncTypes>::value);
    constexpr std::uint8_t lfoable{0};

    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);
    auto &editorHost(editorHostOf(*plugin));

    BothLFOs const lfos(plugin, lfoable);

    /// \brief The ID the panel builds. \see LFODisplay::queueLFOParameter().
    auto const idFor([](std::uint8_t const lfoParameterIndex) {
        LE::SW::ParameterID parameterID;
        parameterID.value.type = LE::SW::ParameterID::LFOParameter;
        parameterID.value._.lfo = {lfoParameterIndex, lfoable, 0};
        return parameterID;
    });

    // Quarter is the default, so Free is the smallest real edit -- and it is the
    // one the N button makes.
    REQUIRE(lfos.pEngine->syncTypes() == LFO::Quarter);
    REQUIRE(lfos.pMain->syncTypes() == LFO::Quarter);

    editorHost.editParameter(idFor(syncTypesIndex), static_cast<float>(LFO::Free));

    // The interface's copy is moved by the call itself; the engine's waits.
    CHECK(lfos.pMain->syncTypes() == LFO::Free);
    CHECK(lfos.pEngine->syncTypes() == LFO::Quarter);

    plugin.flush();
    CHECK(lfos.pEngine->syncTypes() == LFO::Free);

    // And back up, through the whole mask, which is what each of N/T/D sends.
    editorHost.editParameter(idFor(syncTypesIndex), static_cast<float>(LFO::All));
    plugin.flush();
    CHECK(lfos.pEngine->syncTypes() == LFO::All);

    ////////////////////////////////////////////////////////////////////////////
    /// \note The waveform, which the GUI case deliberately cannot reach -- its
    /// only entry point is a popup menu, and a menu is one of the things a
    /// headless editor cannot drive.
    ////////////////////////////////////////////////////////////////////////////
    constexpr std::uint8_t waveformIndex(
        IndexOf<EngineLFO::Parameters, EngineLFO::Waveform>::value);

    auto const shape(static_cast<float>(LFO::Waveform::Square));
    REQUIRE(float(lfos.pEngine->parameters().get<EngineLFO::Waveform>()) != shape);
    editorHost.editParameter(idFor(waveformIndex), shape);
    plugin.flush();
    CHECK(float(lfos.pEngine->parameters().get<EngineLFO::Waveform>()) == shape);
}

TEST_CASE("An LFO parameter the host can see reaches the engine too", "[clap][threading][lfo]")
{
    ////////////////////////////////////////////////////////////////////////////
    /// One of the bounds, which has always had a `ParameterID`. Here because
    /// the case above is about *reading the engine's side*, and none of the
    /// seven sub-parameters had ever been read there.
    ////////////////////////////////////////////////////////////////////////////
    using LE::Parameters::IndexOf;

    constexpr std::uint8_t lowerBoundIndex(
        IndexOf<EngineLFO::Parameters, EngineLFO::LowerBound>::value);
    constexpr std::uint8_t lfoable{0};

    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);
    auto &editorHost(editorHostOf(*plugin));

    BothLFOs const lfos(plugin, lfoable);

    REQUIRE_THAT(float(lfos.pEngine->lowerBound()), Catch::Matchers::WithinAbs(0.0, 1e-6));

    /// \note The ID the panel builds: an LFO parameter is addressed by the
    /// *LFO-able* module parameter index, which is the module parameter index
    /// minus the Bypass the list starts with. \see LFODisplay::queueLFOParameter.
    LE::SW::ParameterID parameterID;
    parameterID.value.type = LE::SW::ParameterID::LFOParameter;
    parameterID.value._.lfo = {lowerBoundIndex, lfoable, 0};

    editorHost.editParameter(parameterID, 0.25f);

    // The interface's copy is moved by the call itself; the engine's waits.
    CHECK_THAT(float(lfos.pMain->lowerBound()), Catch::Matchers::WithinAbs(0.25, 1e-5));
    CHECK_THAT(float(lfos.pEngine->lowerBound()), Catch::Matchers::WithinAbs(0.0, 1e-6));

    plugin.flush();

    CHECK_THAT(float(lfos.pEngine->lowerBound()), Catch::Matchers::WithinAbs(0.25, 1e-5));
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The LFO panel following the host's tempo. `updateForNewTimingInfo()` has
/// been correct and unreachable since the port began: its 2016 caller was in a
/// host class that is in no target, and the CLAP's equivalent runs on the audio
/// thread, where reaching a widget is the one thing the model forbids. So the
/// engine raises a flag and asks for the callback that reads it.
///
///   With no window there is nothing to look at, so what these assert is the two
/// properties the news has to have to be usable at all: it is raised when the
/// timing moves and not when it does not, and a tempo *ramp* -- which moves it on
/// every block -- costs no ring traffic at all. The second is not tidiness: the
/// ring carries the retirements, where a drop is a leak.
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
/// \brief The C++ object behind the C entry point, for the counter on it.
LE::SW::SpectrumWorxCLAP const &implementationOfPlugin(clap_plugin const &plugin)
{
    auto *const pHelper(static_cast<LE::SW::PluginHelper *>(plugin.plugin_data));
    REQUIRE(pHelper != nullptr);
    return *static_cast<LE::SW::SpectrumWorxCLAP *>(pHelper);
}
} // anonymous namespace

TEST_CASE("A tempo change is announced to the interface, and a steady tempo is not",
          "[clap][threading][lfo]")
{
    Entry const entry;

    TestHost host(TestHost::everything());
    ActivePlugin plugin(sampleRate, blockSize, host);

    std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);

    /// \note 120 BPM in 4/4 is what the engine assumes when a host reports
    /// nothing, so the first block at that tempo changes the timing not at all --
    /// which makes it the right thing to settle on before measuring.
    auto transport(transportAt(120, 0, 0));
    plugin.process(leftIn, rightIn, leftOut, rightOut, &transport);
    plugin.pumpMainThread();

    auto const settled(host.mainThreadCallbacks.load());

    // Steady: nothing to say, several blocks running.
    for (unsigned block(0); block < 8; ++block)
    {
        transport = transportAt(120, block * 0.5, 0);
        plugin.process(leftIn, rightIn, leftOut, rightOut, &transport);
    }
    CHECK(host.mainThreadCallbacks.load() == settled);

    // And a real change asks for the callback that carries it.
    transport = transportAt(90, 4, 0);
    plugin.process(leftIn, rightIn, leftOut, rightOut, &transport);
    CHECK(host.mainThreadCallbacks.load() > settled);

    /// \note And the flag is cleared by the drain rather than left standing, so
    /// the *next* change is announced too. A one-shot would have passed
    /// everything above.
    plugin.pumpMainThread();
    auto const afterTheFirst(host.mainThreadCallbacks.load());

    transport = transportAt(140, 8, 0);
    plugin.process(leftIn, rightIn, leftOut, rightOut, &transport);
    CHECK(host.mainThreadCallbacks.load() > afterTheFirst);

    // None of which was a message the plugin had to throw away.
    CHECK(implementationOfPlugin(*plugin).droppedMessages() == 0);
}

TEST_CASE("A tempo ramp does not flood the echo ring", "[clap][threading][lfo]")
{
    ////////////////////////////////////////////////////////////////////////////
    // A host ramping the tempo reports a different bar duration on every block,
    // and nothing here pumps the main thread -- a host slow to run the callback
    // it was asked for, which is the arrangement publishProtocolTests.cpp fills
    // the rings with. More blocks than the ring holds, and nothing may be lost.
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;

    TestHost host(TestHost::everything());
    ActivePlugin plugin(sampleRate, blockSize, host);

    std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);

    constexpr unsigned blocks{LE::SW::Threading::ToUIQueue::capacity + 16};
    for (unsigned block(0); block < blocks; ++block)
    {
        auto const transport(transportAt(60 + (block % 120), block * 0.25, 0));
        plugin.process(leftIn, rightIn, leftOut, rightOut, &transport);
    }

    CHECK(implementationOfPlugin(*plugin).droppedMessages() == 0);

    // ...and the flag that was standing cost the ring nothing to carry.
    plugin.pumpMainThread();
    CHECK(implementationOfPlugin(*plugin).droppedMessages() == 0);
}

TEST_CASE("A tempo change survives a full echo ring", "[clap][threading][lfo]")
{
    ////////////////////////////////////////////////////////////////////////////
    // What the flag buys over the message it replaced. As a message it took a
    // ring slot, and a push that failed cleared its own coalescing flag and gave
    // up -- so with a host automating hard enough to fill the ring the panel
    // went on showing a period in the wrong number of seconds until the tempo
    // moved again, which at a fixed tempo is never.
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;

    TestHost host(TestHost::everything());
    ActivePlugin plugin(sampleRate, blockSize, host);

    std::vector<float> leftIn(blockSize, 0.0f), rightIn(blockSize, 0.0f);
    std::vector<float> leftOut(blockSize), rightOut(blockSize);

    // no transport at all, so nothing in here is a timing change: the engine
    // assumes the 120 BPM 4/4 it starts on
    auto const id(parameterID(globalType, 0));
    for (unsigned block(0); block < LE::SW::Threading::ToUIQueue::capacity + 1; ++block)
    {
        OneParameterEvent const event(id, 0.25 + (0.5 * block) / 1024);
        plugin.process(leftIn, rightIn, leftOut, rightOut, nullptr, &*event);
    }
    REQUIRE(implementationOfPlugin(*plugin).droppedMessages() > 0);

    auto const settled(host.mainThreadCallbacks.load());

    auto const transport(transportAt(90, 4, 0));
    plugin.process(leftIn, rightIn, leftOut, rightOut, &transport);

    CHECK(host.mainThreadCallbacks.load() > settled);
}
