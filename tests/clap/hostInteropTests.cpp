////////////////////////////////////////////////////////////////////////////////
///
/// \file hostInteropTests.cpp
/// --------------------------
///
///   What the plugin says to its host, and what it does with what the host says
/// back.
///
///   Everything here needs a host that answers rather than one that merely
/// exists, which is why none of it could be written before `TestHost`. Three
/// things it reaches that nothing did:
///
///   - **The main-thread arm of the deferral.** `markCurrentProgramAsModified()`
///     branches on `_host.canUseThreadCheck() && _host.isMainThread()`
///     (spectrumWorxCLAP.cpp:1329). No test host offered `clap.thread-check`, so
///     only the else was ever taken and the branch was half dead code.
///
///   - **The output event list.** Every case in the tree hands `process()` and
///     `flush()` a list that throws away what it is given, so what the *editor*
///     tells the host -- the value events and the gesture pair around them -- has
///     never been looked at.
///
///   - **clap-helpers' own contract checking.** `ensureMainThread` and
///     `ensureAudioThread` return immediately unless the host answers a thread
///     check (plugin.hxx:2262-2287), and `CheckingLevel::Maximal` is what this
///     plugin is built at. Offering `clap.log` catches what they report.
///
/// See doc/tech/threading_model.md §2 for which calls owe a
/// `ScopedAudioThreadEntry`, and §8 for what else pins it.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "clap/testHost.hpp"

#include "core/host_interop/plugin2Host.hpp"
#include "core/parameterID.hpp"
#include "core/threading/threadCheck.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace SWTest;

/// \brief The first parameter a host is shown that belongs to no slot.
///
/// \note A global rather than a module parameter, for two reasons: it exists
/// whatever the slots hold -- an event for a parameter no effect owns is dropped
/// by handleEvent() before it reaches anything this file is about -- and it keeps
/// its own range at the host edge, so a value in the effect's own units and a
/// value the host would write are the same number. The three main knobs are
/// globals, so this is also what a real drag moves.
clap_param_info firstGlobalParameter(clap_plugin const &plugin, clap_plugin_params const &params)
{
    for (auto const &info : allParameterInfo(plugin, params))
        if (std::strcmp(info.module, "Global") == 0)
            return info;
    FAIL("no global parameter");
    return {};
}

/// Somewhere in the parameter's range that is not where it already is.
double aDifferentValue(clap_plugin const &plugin, clap_plugin_params const &params,
                       clap_param_info const &info)
{
    double current{0};
    REQUIRE(params.get_value(&plugin, info.id, &current));
    auto const middle((info.min_value + info.max_value) / 2);
    return (current == middle) ? info.max_value : middle;
}

/// Everything the host was told, in one string, so a failure says what.
std::string joined(std::vector<std::string> const &lines)
{
    std::string all;
    for (auto const &line : lines)
        all += "\n  " + line;
    return all;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
// Which thread the plugin thinks it is on
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A host that answers the thread check is marked dirty where it stands", "[clap][host]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The arm that had never run. `clap_host_state::mark_dirty` is
    /// `[main-thread]`, so a plugin that cannot establish which thread it is on
    /// has to defer -- which is what every host in this tree forced, and what
    /// pluginTests.cpp's "A host with state and no thread check" case pins. A DAW
    /// offers the thread check; against one, the mark belongs *here*, in the call,
    /// and a deferral would be a pointless round trip through the message loop.
    ///
    /// \note An edit made in the *editor* is the main-thread route, and very
    /// nearly the only one. `params.flush()` is `[active ? audio-thread :
    /// main-thread]` (ext/params.h:303) and this plugin is active, so a host
    /// event is an audio-thread event whether it arrives through flush() or
    /// through process(); the case below is that half. What is left on the main
    /// thread is the window: a knob, a preset load, a session restore.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;
    TestHost host{TestHost::everything()};
    ActivePlugin plugin(48000, 512, host);

    auto const &params(parameters(*plugin));
    auto const global(firstGlobalParameter(*plugin, params));
    auto const wanted(aDifferentValue(*plugin, params, global));

    LE::SW::ParameterID target;
    target.binaryValue = global.id;

    unsigned const callbacksBefore(host.mainThreadCallbacks);

    editorHostOf(*plugin).automation().automatedParameterChanged(
        target,
        {static_cast<float>(wanted),
         static_cast<float>((wanted - global.min_value) / (global.max_value - global.min_value))});

    // It asked...
    CHECK(host.threadChecks > 0);
    // ...and acted on the answer, without a callback to come back on.
    CHECK(host.dirtyMarks > 0);
    CHECK(host.mainThreadCallbacks == callbacksBefore);
}

TEST_CASE("The same host is not marked dirty from inside the audio callback", "[clap][host]")
{
    // The other arm, against a host that *can* be asked -- which is what makes
    // this different from the deferral a thread-check-less host gets. The plugin
    // asks, is told this is not the main thread, and defers on the strength of
    // the answer rather than on the absence of one.
    Entry const entry;
    TestHost host{TestHost::everything()};
    ActivePlugin plugin(48000, 512, host);

    auto const &params(parameters(*plugin));
    auto const global(firstGlobalParameter(*plugin, params));

    OneParameterEvent const edit(global.id, aDifferentValue(*plugin, params, global));

    std::vector<float> leftIn(512, 0.0f), rightIn(512, 0.0f);
    std::vector<float> leftOut(512), rightOut(512);
    plugin.process(leftIn, rightIn, leftOut, rightOut, nullptr, &*edit);

    CHECK(host.threadChecks > 0);
    CHECK(host.dirtyMarks == 0);
    CHECK(host.mainThreadCallbacks > 0);

    // ...and the deferral is only correct if it really does arrive later.
    plugin->on_main_thread(&*plugin);
    CHECK(host.dirtyMarks == 1);
}

////////////////////////////////////////////////////////////////////////////////
// What the editor sends out
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A knob drag reaches the host as a balanced gesture around its value", "[clap][host]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note What a host does with an unbalanced pair is latch the parameter's
    /// automation lane in write mode until something else ends it, which is a
    /// user-visible fault with no failure anywhere near it. `flushUIEdits()` is
    /// the only thing that can produce one and nothing has ever read its output.
    ///
    /// \note Through `automation()`, which is the interface the editor holds --
    /// `SpectrumWorxEditor::host()` is exactly this reference. A test stands in
    /// for the knob; the three calls are the ones a drag makes.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;
    TestHost host{TestHost::everything()};
    ActivePlugin plugin(48000, 512, host);

    auto const &params(parameters(*plugin));
    auto const global(firstGlobalParameter(*plugin, params));
    auto const wanted(aDifferentValue(*plugin, params, global));

    LE::SW::ParameterID target;
    target.binaryValue = global.id;

    auto &automation(editorHostOf(*plugin).automation());
    unsigned const flushRequestsBefore(host.flushRequests);

    automation.automatedParameterBeginEdit(target);
    automation.automatedParameterChanged(
        target,
        {static_cast<float>(wanted),
         static_cast<float>((wanted - global.min_value) / (global.max_value - global.min_value))});
    automation.automatedParameterEndEdit(target);

    // Nothing has been sent yet: the editor queues and asks the host to come and
    // collect, because a host takes parameter changes only through the output
    // list it hands to process() or flush().
    CHECK(host.flushRequests > flushRequestsBefore);

    RecordedOutputEvents recorded;
    plugin.flush(nullptr, &*recorded);

    auto const mine(recorded.forParameter(global.id));
    REQUIRE(mine.size() == 3);
    CHECK(mine[0].type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
    CHECK(mine[1].type == CLAP_EVENT_PARAM_VALUE);
    CHECK(mine[2].type == CLAP_EVENT_PARAM_GESTURE_END);

    // Balanced across the whole list, not just this parameter's slice.
    CHECK(recorded.count(CLAP_EVENT_PARAM_GESTURE_BEGIN) ==
          recorded.count(CLAP_EVENT_PARAM_GESTURE_END));

    // And the value is the one that was dragged to, in the units this parameter
    // advertises rather than the ones the editor works in.
    CHECK(mine[1].value >= global.min_value);
    CHECK(mine[1].value <= global.max_value);
    CHECK(mine[1].value != 0);

    // Drained, not copied: a second flush has nothing left to send.
    recorded.clear();
    plugin.flush(nullptr, &*recorded);
    CHECK(recorded.events().empty());
}

TEST_CASE("Edits made while audio runs come out of process(), not flush()", "[clap][host]")
{
    // The same queue, collected from the other end. A host that is playing never
    // calls flush() -- ext/params.h says so: "flush() will not be called while
    // the plugin is processing" -- so the edits have to leave through the output
    // list process() is handed, or a knob moved during playback is silently lost.
    Entry const entry;
    TestHost host{TestHost::everything()};
    ActivePlugin plugin(48000, 512, host);

    auto const &params(parameters(*plugin));
    auto const global(firstGlobalParameter(*plugin, params));
    auto const wanted(aDifferentValue(*plugin, params, global));

    LE::SW::ParameterID target;
    target.binaryValue = global.id;

    auto &automation(editorHostOf(*plugin).automation());
    automation.automatedParameterBeginEdit(target);
    automation.automatedParameterChanged(
        target,
        {static_cast<float>(wanted),
         static_cast<float>((wanted - global.min_value) / (global.max_value - global.min_value))});
    automation.automatedParameterEndEdit(target);

    RecordedOutputEvents recorded;
    std::vector<float> leftIn(512, 0.0f), rightIn(512, 0.0f);
    std::vector<float> leftOut(512), rightOut(512);
    plugin.process(leftIn, rightIn, leftOut, rightOut, nullptr, nullptr, &*recorded);

    auto const mine(recorded.forParameter(global.id));
    REQUIRE(mine.size() == 3);
    CHECK(mine[0].type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
    CHECK(mine[2].type == CLAP_EVENT_PARAM_GESTURE_END);
}

////////////////////////////////////////////////////////////////////////////////
// The contract clap-helpers checks when it can
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Driven the way a DAW drives it, nobody misbehaves", "[clap][host]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note This case is three assertions wearing one coat, and all three are
    /// worth having. clap-helpers reports the *plugin* misbehaving -- a callback
    /// run off the main thread, a lifecycle call out of order -- and the *host*
    /// misbehaving, which here means this harness: `ensureAudioThread` fires when
    /// `process()` was called with no TestHost::AudioCallback around it. A test
    /// harness that drives the audio entry points from nowhere in particular is
    /// not reproducing what a DAW does, and until a host answered the thread
    /// check there was no way to find out that it was not.
    ///
    ///   Both are invisible without `clap.log`: the reports go to `std::cerr`
    /// otherwise (host-proxy.hxx:91-107), where a green run swallows them.
    ///
    ///   The third is what this used to overclaim about.
    /// `checkMainThread()`/`checkAudioThread()` do **not** go through
    /// `hostMisbehaving()` at all -- they write to `std::cerr` directly
    /// (plugin.hxx:2219, :2233), so `clap.log` cannot see them and neither could
    /// this case, which said "nobody misbehaves" while emitting one of them. It
    /// is asserted now, by capturing the stream.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;
    TestHost host{TestHost::everything()};

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Two captures, and the nesting is the whole trick: the inner one
    /// takes the writes and the outer one never sees them. So `expected` holds
    /// the one line this run is known to produce and `unexpected` holds anything
    /// else, and the assertion below needs no filter on the wording -- which
    /// matters, because a *genuine* main-thread violation is worded identically
    /// and a filter would hide it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    CapturedStandardError const unexpected;

    {
        ActivePlugin plugin(48000, 512, host);
        auto const &params(parameters(*plugin));

        // The whole of what a host asks a plugin about its parameters.
        auto const infos(allParameterInfo(*plugin, params));
        for (auto const &info : infos)
        {
            double value{0};
            CHECK(params.get_value(&*plugin, info.id, &value));
            std::array<char, 128> text{};
            CHECK(params.value_to_text(&*plugin, info.id, value, text.data(), text.size()));
        }

        // An effect in a slot, a rescan collected, and audio through it.
        OneParameterEvent const fill(parameterID(moduleChainType, 0), 0);
        {
            ////////////////////////////////////////////////////////////////////
            ///
            /// \note The known one, and it is clap-helpers arguing with itself
            /// rather than anything about this plugin -- the same
            /// self-contradiction `TestHost::isFlushValidatingItself()` filters
            /// out of the log, seen from the side that cannot reach the log.
            /// `clapParamsFlush` guards itself correctly as
            /// `[active ? audio : main]` and then validates each event through
            /// `getParamInfoForParamId`, which opens with `checkMainThread()`.
            ///
            ////////////////////////////////////////////////////////////////////
            CapturedStandardError const expected;
            plugin.flush(&*fill);

            /// \note One line as of clap-helpers today, and the bound rather than
            /// the count: a version that stops emitting it leaves this passing
            /// and makes the inner capture redundant, which is the direction a
            /// third-party workaround should age in. A version that emits *more*
            /// is worth being told about.
            INFO("what a flush wrote to stderr:" << joined(expected.lines()));
            CHECK(expected.lines().size() <= 1);
            for (auto const &line : expected.lines())
                CHECK(line == "thread-error: this code must be running on the main thread");
        }
        plugin->on_main_thread(&*plugin);

        std::vector<float> leftIn(512, 0.0f), rightIn(512, 0.0f);
        std::vector<float> leftOut(512), rightOut(512);
        for (unsigned block(0); block < 8; ++block)
            plugin.process(leftIn, rightIn, leftOut, rightOut);

        // And a restart, which is deactivate/activate with the audio entry points
        // on the audio thread either side of it.
        plugin.restartIfAsked();
        for (unsigned block(0); block < 8; ++block)
            plugin.process(leftIn, rightIn, leftOut, rightOut);
    }
    // ...including the teardown, which is where "host forgot to deactivate the
    // plugin before destroying it" would be reported.

    INFO("what the plugin was reported for:" << joined(host.pluginMisbehaviours()));
    CHECK(host.pluginMisbehaviours().empty());

    /// \note And the harness, which is the half that has already earned its keep:
    /// this went red the first time it ran, on `params.flush()` being called from
    /// the main thread against an active plugin. See TestHost::flush().
    INFO("what this harness was reported for:" << joined(host.hostMisbehaviours()));
    CHECK(host.hostMisbehaviours().empty());

    /// \note And the half neither of those can see. Everything but the one flush
    /// above, and nothing may write a word.
    INFO("what nobody could see before:" << joined(unexpected.lines()));
    CHECK(unexpected.lines().empty());
}

////////////////////////////////////////////////////////////////////////////////
// What the host is allowed to write
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A parameter written outside its range is clamped, not asserted", "[clap][host]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Found by `clap-cpp-validator validate`, whose `param-range-robustness`
    /// took the plugin down with `Abort trap: 6` -- `Parameter::setValue`'s
    /// `isValidValue(value)` assert, from a global gain ranged 0.001..2. A release
    /// build has no assert and *stores* the out-of-range value instead, which is
    /// the worse half.
    ///
    ///   `CLAPEdge::fromHost` clamped the normalised parameters across their fixed
    /// 0..1 edge and passed everything else through as the host sent it. Both
    /// branches clamp now.
    ///
    /// \note Every parameter rather than the one that aborted: the two branches of
    /// fromHost() divide this list, a slot selector is stepped where a gain is not,
    /// and an ID no effect currently owns takes a third path out of handleEvent()
    /// before any of this. One case cannot tell those apart.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;
    TestHost host{TestHost::everything()};
    ActivePlugin plugin(48000, 512, host);

    auto const &params(parameters(*plugin));
    auto const all(allParameterInfo(*plugin, params));
    REQUIRE(!all.empty());

    std::vector<float> leftIn(512, 0.0f), rightIn(512, 0.0f);
    std::vector<float> leftOut(512), rightOut(512);

    for (auto const &info : all)
    {
        for (double const wanted : {info.min_value - 1000, info.max_value + 1000})
        {
            OneParameterEvent const event(info.id, wanted);
            plugin.process(leftIn, rightIn, leftOut, rightOut, nullptr, &*event);

            double reported{0};
            REQUIRE(params.get_value(&*plugin, info.id, &reported));

            CAPTURE(info.name, info.module, wanted, info.min_value, info.max_value, reported);
            CHECK(reported >= info.min_value);
            CHECK(reported <= info.max_value);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Which calls own the engine
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Resetting between blocks may still mutate the engine", "[clap][host]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The case `vst3-validator` was. `clap_plugin::reset` is
    /// `[audio-thread & active]` (plugin.h:89) and is called *between* blocks --
    /// `ClapAsVst3::setProcessing(false)` does `stop_processing()` then `reset()`,
    /// off Steinberg's own call-sequence diagram. Nothing in this tree had ever
    /// called it, so `Threading::ScopedAudioThreadEntry` being opened only by
    /// `process()` went unnoticed for as long as `process()` was the only
    /// `[audio-thread]` entry point anything drove, and
    /// `SpectrumWorxCore::resetChannelBuffers()` aborted on
    /// `currentThreadMayMutateEngineState()` against a host doing nothing wrong.
    ///
    /// \note What failure looks like without the fix is an **abort, not a failed
    /// assertion** -- the guard is `LE_ASSERT`, so a checked build dies inside
    /// reset(). `catch_discover_tests` runs each case as its own ctest test, so
    /// that lands as this test failing rather than as the binary taking the suite
    /// with it. Checked by reverting the scope: this case aborts, the rest pass.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;
    TestHost host{TestHost::everything()};
    ActivePlugin plugin(48000, 512, host);

    std::vector<float> leftIn(512, 0.25f), rightIn(512, -0.25f);
    std::vector<float> leftOut(512), rightOut(512);

    // A tail to throw away, so reset() has something to do.
    for (unsigned block(0); block < 4; ++block)
        plugin.process(leftIn, rightIn, leftOut, rightOut);

    plugin.reset();

    // And the engine still runs afterwards, which is what says reset() left it
    // usable rather than merely survived.
    for (unsigned block(0); block < 4; ++block)
        plugin.process(leftIn, rightIn, leftOut, rightOut);

    CHECK(!LE::SW::Threading::isAudioThread()); // the scope closed behind it

    INFO("what the plugin was reported for:" << joined(host.pluginMisbehaviours()));
    CHECK(host.pluginMisbehaviours().empty());
    INFO("what this harness was reported for:" << joined(host.hostMisbehaviours()));
    CHECK(host.hostMisbehaviours().empty());
}

TEST_CASE("Flushing is an audio-thread call only while the plugin is active", "[clap][host]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `clap_plugin_params::flush` is `[active ? audio-thread :
    /// main-thread]` (ext/params.h:303) -- the one entry point whose owning
    /// thread is decided by state rather than fixed, and so the one place a
    /// scope taken unconditionally would be wrong in the *other* direction.
    ///
    /// \note **What this case does not prove.** That the inactive flush declines
    /// to claim the audio thread is not observable from outside the plugin:
    /// nothing the host can see differs, because claiming it wrongly breaks no
    /// contract the host checks -- it only makes the engine's own ownership
    /// predicate lie. What would catch it is rtsan, which the scope also opens:
    /// under `-fsanitize=realtime` an inactive flush that took the scope would
    /// report every allocation on the main thread. That is a sanitizer build's
    /// job, not this one's, and it is not pretended at here. What is pinned
    /// below is the half that *is* visible:
    /// both flushes are legal, neither is reported, and neither leaks the scope.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;
    TestHost host{TestHost::everything()};

    bool flushedWhileInactive{false};
    ActivePlugin plugin(48000, 512, host, [&](clap_plugin const &inactive) {
        // Legal, and main-thread: the plugin is initialised
        // and not yet activated. This is the arm a host takes
        // when it restores a session before saying what the
        // sample rate is.
        auto const *const pParams(static_cast<clap_plugin_params const *>(
            inactive.get_extension(&inactive, CLAP_EXT_PARAMS)));
        REQUIRE(pParams != nullptr);
        pParams->flush(&inactive, &noInputEvents(), &discardedOutputEvents());
        flushedWhileInactive = true;
        CHECK(!LE::SW::Threading::isAudioThread());
    });

    CHECK(flushedWhileInactive);

    // And the active arm, which is the audio thread by contract.
    plugin.flush();
    CHECK(!LE::SW::Threading::isAudioThread());

    INFO("what the plugin was reported for:" << joined(host.pluginMisbehaviours()));
    CHECK(host.pluginMisbehaviours().empty());
    INFO("what this harness was reported for:" << joined(host.hostMisbehaviours()));
    CHECK(host.hostMisbehaviours().empty());
}

TEST_CASE("Learning the host's tempo does not move the LFO periods", "[clap][host]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note All four of `clap-cpp-validator`'s state failures, and none of them
    /// was about state. Its first instance randomises the parameters **through
    /// process() on an activated plugin** and reads them back while active; its
    /// second is only `init()`ed and never processes. So the two sides read the
    /// same stored period either side of one event -- the host announcing its
    /// tempo -- and `LFOImpl::updateForNewTimingInformation()` rescales every
    /// *Free* LFO's period by the bar-duration ratio, to hold its period constant
    /// in seconds. Against a real tempo change that is right. Against the engine's
    /// assumed 120 BPM it is a parameter moving under a host that never wrote it.
    ///
    /// \note **The transport has to disagree with 120.** Four earlier attempts at
    /// this case passed -- writing the period, flush against process, activation
    /// on its own, a whole randomised save/reload round trip -- because every one
    /// of them drove the plugin at 120 BPM, where the ratio is exactly one and the
    /// bug is invisible. That is why the tempo here is 140 and why it is the first
    /// thing to check if this case ever stops failing on a revert.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Entry const entry;
    TestHost host{TestHost::everything()};

    clap_id periodID{};
    double beforeAnyBlock{-1};
    ActivePlugin plugin(48000, 512, host, [&](clap_plugin const &inactive) {
        auto const *const pParams(static_cast<clap_plugin_params const *>(
            inactive.get_extension(&inactive, CLAP_EXT_PARAMS)));
        REQUIRE(pParams != nullptr);

        OneParameterEvent const fill(parameterID(moduleChainType, 0), 0);
        pParams->flush(&inactive, &*fill, &discardedOutputEvents());

        periodID = lfoPeriodParameter(inactive, *pParams).id;
        REQUIRE(pParams->get_value(&inactive, periodID, &beforeAnyBlock));
    });

    auto const &params(parameters(*plugin));

    std::vector<float> leftIn(512, 0.0f), rightIn(512, 0.0f);
    std::vector<float> leftOut(512), rightOut(512);

    // Not 120: the engine assumes 120 BPM 4/4 until a host says otherwise, and at
    // 120 the ratio is one whether or not this is fixed.
    auto const playing(transportAt(140, 0, CLAP_TRANSPORT_IS_PLAYING));
    plugin.process(leftIn, rightIn, leftOut, rightOut, &playing);

    double afterLearningTheTempo{-1};
    REQUIRE(params.get_value(&*plugin, periodID, &afterLearningTheTempo));

    // ...and it stays put over the blocks that follow, which is the half that
    // says the first block established the tempo rather than merely deferring the
    // rescale by one.
    for (unsigned block(0); block < 4; ++block)
        plugin.process(leftIn, rightIn, leftOut, rightOut, &playing);

    double afterMoreBlocks{-1};
    REQUIRE(params.get_value(&*plugin, periodID, &afterMoreBlocks));

    CAPTURE(beforeAnyBlock, afterLearningTheTempo, afterMoreBlocks);
    CHECK(afterLearningTheTempo == beforeAnyBlock);
    CHECK(afterMoreBlocks == beforeAnyBlock);
}

////////////////////////////////////////////////////////////////////////////////
// What counts as the chain having changed
////////////////////////////////////////////////////////////////////////////////

namespace
{
/// \brief The slot selector for module \p slot, as the host is shown it.
clap_param_info slotSelector(clap_plugin const &plugin, clap_plugin_params const &params,
                             std::uint8_t const slot)
{
    for (auto const &info : allParameterInfo(plugin, params))
    {
        LE::SW::ParameterID parameterID;
        parameterID.binaryValue = info.id;
        if ((parameterID.type() == LE::SW::ParameterID::ModuleChainParameter) &&
            (parameterID.value._.moduleChain.moduleIndex == slot))
            return info;
    }
    FAIL("no slot selector for that module");
    return {};
}
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
///
/// \note The regression this file exists to hold on to. `handleEvent()` decides
/// whether a block changed the *shape* of the parameter list, and it used to
/// answer that question by asking what type the parameter was rather than
/// whether anything had happened -- so a host writing a slot back to the value
/// it already held was answered as a chain change, and paid for with a full
/// `CLAP_PARAM_RESCAN_INFO`.
///
///   In Ardour that closed a loop, because a rescan carrying `INFO` is what makes
/// it write the parameter set back. \see issue #172 and the note on the return
/// value in `handleEvent()`.
///
/// \note `rescanFlags` and not `mainThreadCallbacks`, which cannot tell these
/// cases apart: `markCurrentProgramAsModified()` asks for a callback on the same
/// path, so *any* parameter event arms one. What is being pinned is narrower --
/// that the callback, when it runs, has no rescan to deliver.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A slot written back the value it already holds is not a chain change",
          "[clap][host][parameters]")
{
    Entry const entry;
    TestHost host{TestHost::everything()};
    ActivePlugin plugin(48000, 512, host);

    auto const &params(parameters(*plugin));
    auto const selector(slotSelector(*plugin, params, 0));

    std::vector<float> leftIn(512, 0.0f), rightIn(512, 0.0f);
    std::vector<float> leftOut(512), rightOut(512);

    /// Delivers \p value for the slot in one block, and runs the callback the
    /// plugin asks for -- which is where a rescan, if there is one, is handed
    /// over. Answers what the host was told to rescan.
    auto const writeSlot([&](double const value) {
        host.rescanFlags = 0;
        OneParameterEvent const edit(selector.id, value);
        plugin.process(leftIn, rightIn, leftOut, rightOut, nullptr, &*edit);
        plugin.pumpMainThread();
        return host.rescanFlags.load();
    });

    // The slot starts empty; min_value is `noModule` and the next value up is the
    // first effect, so this genuinely fills it.
    auto const firstEffect(selector.min_value + 1);

    ////////////////////////////////////////////////////////////////////////////
    // Filling the slot is a change, and has to stay one.
    ////////////////////////////////////////////////////////////////////////////

    auto const onTheChange(writeSlot(firstEffect));
    CHECK((onTheChange & CLAP_PARAM_RESCAN_INFO) != 0);

    ////////////////////////////////////////////////////////////////////////////
    // Writing it again is not.
    ////////////////////////////////////////////////////////////////////////////

    CHECK(writeSlot(firstEffect) == 0);

    // And not merely the second time: a host in the loop this comes from repeats
    // the write on every block, so once quiet it has to stay quiet.
    for (unsigned block(0); block < 8; ++block)
        CHECK(writeSlot(firstEffect) == 0);

    ////////////////////////////////////////////////////////////////////////////
    // Moving it really does still speak up, which is what says the guard reports
    // changes rather than merely says less.
    ////////////////////////////////////////////////////////////////////////////

    auto const onEmptyingIt(writeSlot(selector.min_value));
    CHECK((onEmptyingIt & CLAP_PARAM_RESCAN_INFO) != 0);
}
