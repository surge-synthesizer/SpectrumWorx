# SpectrumWorx — The threading model

Which thread owns what, how the two of them talk, and what neither may do to the
other. Companion to [`parameter_system.md`](parameter_system.md), which is how
parameters are addressed, and [`streaming_format.md`](streaming_format.md),
which is what reaches a file.

Everything described here is in the tree and has tests naming it. What the model
deliberately does *not* solve is in the issue tracker.

---

## 1. What the two sides may not do to each other

Three structural rules. Each is enforced by something that fails — a link error,
an assertion, or a sanitizer report — because each was violated by the 2016
design, and an argument is not an enforcement.

**The engine holds no widget.** `SW::Module` used to own its own editor region —
a `std::optional<GUI::ModuleUI>` member, plus four virtuals whose only purpose
was to push a value into it. It was structural rather than incidental:
`Module::Impl<Effect>` *inherited* `ModuleWidgets<Effect>`, so every module the
factory allocated carried that effect's JUCE widget storage inline, sized at
compile time. `SpectrumWorxEditor` owns the strips now and each one holds a
`Utility::IntrusivePtr<Module>` — the reference runs from the interface to the
engine and never back. §6 is what keeps it that way.

**Nothing the audio thread touches is destroyed under it.**
`ModuleChainBase::forEach` holds an `IntrusivePtr` per node deliberately
(`moduleChainImpl.hpp:314-320`), so when a module leaves the chain the reference
that hits zero can be the audio thread's. Whatever comes out of the engine goes
back to the main thread as a `ToUI::Retire` and is deleted there (§5).

A strip is **not** a second reference that makes this safe by itself: strips hold
`programMain_`'s modules, which are different objects from the engine's, so the
engine's last reference is the chain's whenever no other route has taken one.
Every route that unlinks a module therefore takes one first and hands it over —
`installModuleInSlot()` and `AutomatedModuleChain::setParameter()` both, the
second since 08.08.2026. Until then a host automating a slot selector freed the
displaced module inside `process()`; the realtime sanitizer reports that as a
`free` in a real-time context, which is how it is now checked.

**The engine reports; it does not raise.** Preset loading counts its problems
into a `PresetLoadReport` and hands it back —
`PresetLoadReport::worthTellingTheUser()` decides whether any of it is the
user's business, `GUI::loadPreset` turns the survivors into **one** dialog, and
`stateLoad` says nothing at all. Loading the factory banks raises
`MissingParameter`s in bulk and not one window.

---

## 2. The rules

1. **The audio thread owns the engine.** `Program`, the module chain, the
   modules, the LFOs and `Engine::Setup` are its property while the plugin is
   activated. Nothing else dereferences them.
2. **The main thread owns a full copy of that state**, and the widgets. It is
   authoritative for what the user and the host asked for; the engine is
   authoritative for what happened.
3. **They exchange formal messages, in both directions, over two SPSC rings.**
   Ordered, all delivered, heterogeneous payloads.
4. **Continuously varying values cross in a mailbox of atomics, not the ring.**
5. **The audio thread takes no lock.** There is no `processCriticalSection_`; it
   was deleted rather than narrowed, and `Utility::CriticalSection` and
   `ConditionVariable` went with it.
6. **A parameter has a base value and a modulated value, and they are different
   things** (§4).

### Who owns the engine right now

One line, and it is rule 1 written down:

```cpp
bool currentThreadMayMutateEngineState()
{ return !engineIsRunning() || Threading::isAudioThread(); }
```

`engineIsRunning()` is `suspend()`/`resume()` — false until `activate()`, so the
main thread owns the engine before there is an audio thread and again after
there is not. Six assertion sites read it. It is not `#ifndef NDEBUG`: every
publish helper branches on it (§5).

### Which calls are the audio thread's

`Threading::isAudioThread()` answers **"is this call inside one of CLAP's
`[audio-thread]` entry points"**, not "is this thread the audio thread" — a host
with a worker pool (Bitwig and Reaper both have one) delivers successive blocks
of the same plugin on different threads, so there is no such thread to name.

`Threading::ScopedAudioThreadEntry` is one line at the top of every such entry
point. It makes `isAudioThread()` true for the duration and opens a
RealtimeSanitizer realtime region, so an allocation or a lock reached from
anywhere underneath is reported with a stack.

| `clap/plugin.h` | annotation |
|---|---|
| `process` | `[audio-thread & active & processing]` |
| `reset` | `[audio-thread & active]` |
| `start_processing` | `[audio-thread & active & !processing]` |
| `stop_processing` | `[audio-thread & active & processing]` |

Plus one whose annotation is conditional: `clap_plugin_params::flush` is
`[active ? audio-thread : main-thread]` (`ext/params.h:303`), so it takes the
scope **only while active**. Opening it unconditionally would be wrong in the
other direction — telling an inactive plugin that the audio thread owns an
engine the main thread owns.

> **Why the table is worth reading rather than assuming.** The marker was called
> `ScopedAudioCallback` and documented as *"this call is under `process()`"*,
> which is not what `[audio-thread]` means. `reset()` runs *between* blocks, and
> the first host to call it there — `vst3-validator`, through
> `ClapAsVst3::setProcessing(false)` — aborted the plugin on its own mutation
> assert while behaving perfectly correctly. The narrow reading was not a
> simplification; it was a hole.

---

## 3. The three channels

```
                 main thread                              audio thread
                 ───────────                              ────────────
  editor ──┬──▶  MainThreadModel  ──▶ ToEngine ring ──▶   drained at the top of
           │     (full copy)          (commands)          process(), then the engine runs
           │
           ├──◀  MainThreadModel  ◀── ToUI ring    ◀──    base changed, retire this pointer;
           │                          (events)            chain and timing changed on flags
           │
           └──◀  ValueMailbox     ◀── atomics      ◀──    modulated values, per block,
                 (const & to editor)                      coalescing, painting only
```

| `core/threading/messages.hpp` | cases |
|---|---|
| `ToEngine` | `SetBaseParameter`, `SetSlot`, `MoveModule`, `SwapChain`, `SwapSample` |
| `ToUI` | `BaseParameterChanged` — `Retire` on a ring of its own, and chain and timing changes on flags |

Both are tagged unions, trivially copyable, owning nothing. Each case says which
side is responsible for a pointer after it lands, and that is the entire
memory-management story: no shared ownership anywhere in the protocol, and
nothing destroyed on the audio thread.

**Why two transports and not one.** The ring is for things where order and
delivery matter: `SetSlot{2, Gain}` followed by `SetSlot{2, none}` does not
coalesce to the second one, and a dropped `Retire{ptr}` is a leak. The mailbox
is for a sampled signal, where every value but the newest is dead on arrival.
`Processor::preProcess()` runs once per host block and writes every enabled
LFO's target, so at 48 kHz with a 32-sample buffer that is 1500 updates per
second per enabled LFO — up to 120,000/s for a full rack — against a UI that
draws at 30 Hz. A FIFO would spend all its bandwidth on values nobody sees; a
mailbox cannot overflow and coalesces by construction. Same message list, two
transports.

**And news is a flag, not a message.** A chain change and a timing change both
carry nothing — the main thread recomputes the rack off the model and reads the
new bar duration off the engine, so a second of two says what the first did — and
on a full ring the first was simply lost, leaving the rack drawing a chain that
was no longer there or the LFO panel showing a period in the wrong number of
seconds. A flag the engine only sets and the main thread only clears cannot be
dropped, and the callback that reads it is asked for by `requestRescan()` and
`request_callback()` rather than by the push, so the wake-up never depended on
the ring either. Both are cleared with `exchange(false)` *before* the redraw they
ask for, so a change landing during it is announced rather than swallowed.

**And the retirements have a ring of their own.** They rode the same one as the
echoes until a fuzzer showed what that costs: a host writing every parameter
between two main-thread turns is ~700 echoes against a handful of retirements —
60:1, measured — so the ring filled with the kind whose loss is a stale reading
and had nothing left for the kind whose loss is a leak. They are the same message
type and nothing else: opposite failure modes on a full ring is the whole reason
to separate them. The main thread drains the echoes first and the retirements
after, which is the order the engine made them in — a module is announced gone
before the reference it left is dropped.

**The ring refuses when full.** `SPSCQueue<Message, Capacity>`
(`core/threading/spscQueue.hpp`) keeps free-running counters, masked only on the
way into the array, and `push` declines rather than clobbering the unread tail.
That is the one place it differs from `sst::cpputils::SimpleRingBuffer`, and it
is the whole reason for having our own.

**The mailbox sweeps with `exchange`.** `core/threading/valueMailbox.hpp` is a
value per dense parameter index plus a bitset of what moved, so a write landing
mid-sweep is carried into the next sweep rather than lost.

**The timing change is where that rule was learned.** A host ramping the tempo
reports a new bar duration on every block, some hundreds a second, and a ring it
fills has dropped somebody's echo, which leaves the main thread's Program behind
the engine for that parameter. It rode the ring behind a sender-side flag that
capped it at one outstanding *message* rather than at none — so it still spent a
slot, and a push that failed cleared its own flag and gave up, which at a fixed
tempo meant the panel never caught up. As a flag it costs nothing and cannot be
dropped. `_host.requestCallback()` is what stays: a tempo change on its own asks
the host for nothing, and without it the flag would wait for whatever asks next.

`updateForNewTimingInfo()` is the editor's redraw and it had no caller for the
life of this port — its 2016 one was in a host class the port deleted, and the
CLAP equivalent (`updateLFOTiming()`) runs on the audio thread, where touching a
widget is exactly rule 1.

**Where they are drained.** `drainCommands()` at the top of `process()`, before
the host's own events so a block's automation wins over anything queued before
it began — and also from `paramsFlush()`, because a host with the transport
parked may not be calling `process()` at all. Two callers is safe because CLAP
forbids a host from running flush and process concurrently, so there is still
one consumer. `drainEngineEvents()` runs in `onMainThread()`.

**And at `deactivate()`, which is the third.** Both queues are emptied there,
after `suspend()` and before the pending spectral setup is applied — the main
thread owns the engine at that point, so the commands apply exactly as
`process()` would have applied them. Without it a chain queued behind a restart
was still in the ring when the restart resized the chain, so the first block
after it spliced in modules built for the *previous* FFT size and ran them at the
new one. A host is not obliged to render, or to run a main-thread callback,
between the change and the restart it asks for; Logic with the transport parked
is the reported one. What is left in the command queue at destruction is freed
rather than applied — `discardQueuedCommands()`, since applying it would call
into a host that is midway through `clap_plugin::destroy`.

**Nothing else drains them, so a queued edit has to be asked for.** A preset load
queues its six global parameters like any other edit and then calls
`clap_host_params::request_flush` once, at the end. A knob gets that for free
from `automatedParameterChanged`; a preset makes no per-parameter notification by
design, and a queue nobody drains is a preset that never arrives.

**Ownership: `SpectrumWorxCLAP` owns all three — not the editor.** `paramsValue`,
`paramsValueToText` and `stateSave` are `[main-thread]` calls that happen with
the window shut, so the main-thread model has to outlive the editor and be there
when there has never been one. The editor is handed references at construction
through two `EditorHost` virtuals; the mailbox as `const &`, since the UI only
ever reads it. A consequence worth having: the host's parameter reads do not
touch engine memory at all.

**One more ring, in the other direction.** `SpectrumWorxCLAP::UIEdits` is the
plugin→host leg — gestures and value changes on their way to
`clap_host_params`. It predates the rest and was already correct; `SPSCQueue` is
generalised from it.

---

## 4. Base versus modulated

An LFO does not overwrite its parameter. `Engine::ModuleParameters` keeps one
float per LFO-able parameter — **unmodulated** in the code, *base* here, because
"base parameters" already means the five shared ones — sized and indexed exactly
as the LFO storage is.

| | base | modulated |
|---|---|---|
| written by | user edit, host automation, preset load | the LFO, once per block |
| `paramsValue`, `stateSave`, `savePreset` | ✅ | ✗ |
| ToUI ring — latchable, ordered | ✅ | ✗ |
| ValueMailbox — coalescing, painting | ✗ | ✅ |

`setBaseParameter`/`setEffectParameter` — what a user, the host and a preset go
through — write both. `set*ParameterFromLFOAux` calls `set*ParameterLive` and
writes only the live one, and **emits no events at all**: the plugin publishes
what the LFOs did after the block, and the editor sweeps the mailbox on a 30 Hz
`juce::Timer`.

Two things this buys, and both were bugs before it: a host's generic panel no
longer polls the sweep, and saving a preset while an LFO is running no longer
freezes that LFO's instantaneous output into the file.

The main-thread model carries **both** values, so dragging the base with the LFO
active is a future UI change rather than a future re-plumbing.

**And switching the LFO off applies it.** A sweep that stops leaves the live
value wherever it happened to be, which is a value nobody chose and which
neither the host nor the file has ever agreed with, so
`Automation::setAutomatedLFOParameter` watches `Enabled` go false and calls
`restoreUnmodulatedParameter()`. The interface does the same to the widget --
what the sweep put there arrived down the mailbox, and that channel says nothing
about where the parameter is. \see issue #204.

**Not the same thing as depth.** `lowerBound`/`upperBound` are absolute values in
the parameter's own units and every preset since 2011 stores them that way.
"Base" here means *the value that applies when the LFO is off, remembered while
it is on*.

---

## 5. Lifetime: publish and retire

Structural change — a slot's effect, a preset, a session, a sample — does not
mutate live engine state. The main thread builds the replacement, publishes one
pointer, and the audio thread hands the old one back to be destroyed:

```
main thread                                  audio thread
───────────                                  ────────────
Program *next = build(…);                    // ← allocation, formatting, file IO
push(SwapChain{next})              ───────▶  live_ = next;              // one store
                                   ◀───────  push(Retire{old})
delete old;                                  // never destroyed under the callback
```

The engine side is two functions, both pure pointer surgery and both legal
inside `process()`: `installModuleInSlot()` — which takes a reference out before
relinking, because unlinking would otherwise call the deleter on the audio
thread — and `swapModuleChain()`, three splices of a circular list with no
destruction.

`AutomatedModuleChain::setParameter()` is a third way into the chain and is held
to the same rule: it hands the displaced module out with a reference on it, and
the caller either retires it (the audio thread) or releases it (either Program on
the main thread). A destroying overload exists for the second case and says in
its own name that it is never to be called from `process()`.

**`Threading::publish{Slot,ModuleMove,Chain}()` is where the branch lives**
(`core/threading/publish.hpp`), so that no caller has to know which side of
`activate()` it is on: with nothing processing they apply the change directly,
and with audio running they queue it. That is what keeps every headless test
working without a message pump, and what makes the preset loader one code path.

**Modules are built on the main thread.** `Threading::createModuleForSlot()`
does the allocation and the audio thread only links the result. The one
exception is a *host* writing a slot selector: that arrives as a parameter event
inside `process()`, and deferring it would mean a round trip to the main thread
and back — so it still allocates. Recorded as issue #9. It is an
allocation and **only** an allocation: the module it displaces leaves by the
retire route like every other, and a realtime-sanitizer run over
`tests/clap/threadingTests.cpp` reports the one and not the other.

**The rack is a function of the main thread's chain.** `resyncModuleRack()` drops
strips whose module has gone, builds strips for modules that have none, and puts
every one of them where `programMain_`'s chain says. It is a recomputation rather
than a diff, because between a click and the engine applying it the rack is what
the user asked for and the engine's chain is what is playing.

Three things ask for it. **Whatever changed the main thread's chain says so** —
`addUserAddedModule()`, `moduleAdded()`/`moduleRemoved()` and `GUI::loadPreset()`
each call `refreshModuleRackAsync()`. **The engine's echo says so**, through
`chainChangedPending_`, for the changes that originate on the audio
thread — a host writing a slot selector inside `process()`. And that echo is
acted on **synchronously**, from `drainEngineEvents()` in `onMainThread()`, which
is worth stating because it is a precondition for everything below: a strip can
be destroyed inside a host callback, between one paint and the next.

Which is why `resyncModuleRack()` opens by dismissing any open menu, and why
`detachFrom()` decides what to drop by asking each widget what it is pointing at
rather than by asking the editor what is currently selected. The LFO display and
the shared module controls are children of the *editor*, each holding a raw
pointer into a strip, and deactivation deliberately leaves them alive while
clearing the editor's records — so the two questions have different answers
exactly when a strip is being freed.

The first is not redundant. A preset load fills `programMain_` outright and only
*queues* the engine's copy, so waiting for the echo makes the picture depend on
the host calling `process()` — which Logic does not do for an AudioUnit on a
track that is neither playing nor monitored. That was a live bug: the browser
changed the preset and the strips stayed on the previous one until something made
a block of audio happen. Pinned by *"A preset reaches the rack with no audio
thread running"* (`tests/clap/pluginTests.cpp`), which never calls `process()`.

**The sample is a pointer.** `SpectrumWorxCLAP` holds `Sample *pSample_`,
swapped by `SwapSample`, plus its own `sampleFile_`/`decodedSampleRate_` so that
`currentSampleFile()` and `activate()`'s re-read answer without touching the
audio thread's copy. `Sample::load()` takes no lock: it decodes into an object
nobody else can see.

### The spectral setup is the exception, and it uses CLAP

Changing the FFT size, the overlap factor or the window function reallocates the
whole working set and resizes every module — too much to publish as a pointer,
and `calculateWindowAndWOLAGain()` rewrites the very windows the WOLA path is
reading. So `setGlobalParameter` records `spectralSetupPending_` instead;
`drainCommands()` and `presetChangeEnd()` ask for `clap_host::request_restart()`;
and `deactivate()` applies it, where the audio thread definitionally is not
running.

That also keeps a contract the plugin was breaking. `clap_plugin_latency` says
latency may only change while the plugin is deactivated, and **FFT size *is* the
latency** — `activate()` caches `engineSetup().latencyInSamples()`. A preset that
changes the spectral setup therefore takes the restart with it and installs its
chain in `activate()`.

> `request_restart` has still not been *observed* being answered by a real host.
> The test hosts in `tests/clap/` implement it as a no-op observer, which is
> enough to say the plugin asks and not enough to say a DAW answers. The DAW pass
> did not settle it either way, because it takes a specific act to see: change
> the FFT size from the host's own generic panel while audio runs. A host that
> ignores the request leaves that parameter reading one thing and the engine
> running another — visible, harmless, and not what anyone asked for. It is on
> the by-hand checklist for that reason.

---

## 6. The layering: `sw-dsp` links no JUCE

The first rule of §1, expressed as a link line rather than as a convention.

**What `sw-dsp` links:** `sst-cpputils`, `sst-plugininfra`, `tinyxml`,
`sw::assets`, and Accelerate or pffft. No JUCE, and nothing that pulls it.

Two things enforce it, and they fail differently:

- **`checkNoJuceInDSP.cmake`** (`ctest -R engine-links-no-juce`) reads
  `compile_commands.json` and fails if any engine translation unit is compiled
  with `-DJUCE_MODULE_AVAILABLE_`. It catches a JUCE module arriving on
  `sw-dsp`'s link line *before* anything includes a header — which is the shape
  this rots back into, because a `target_link_libraries` line looks harmless
  until six months later.
- **`sw-dsp-tests`** links `sw-dsp` and Catch2, full stop. `otool -L` lists
  Foundation, Accelerate and libc++; `nm -u` finds no JUCE symbol at all. It
  catches the other direction — a *test* reaching above the layer it is testing
  — and does so as a link error.

`sw-plugin-tests` is the rest: the CLAP cases, the editor cases and the decoder.
Two live there that look like they belong below — `threadCheckTests` drives a
real `clap_plugin` through its C entry point, and `parameterTableTests`
exercises the host-facing parameter enumeration.

Between them sits **`sw-io`**: `external_audio/sample.cpp`, the audio file
decoder. Not `sw-gui`, because it draws nothing — a plugin reads a session's
sample with no editor open.

Preset files are *not* up here. `presetStorage.cpp` opens them with
`std::filesystem::path` and `<fstream>` and is part of `sw-dsp` itself, so the
whole read-and-parse path speaks no JUCE and the preset tests link without it.
Whatever needs a `juce::File` converts at its own edge, through
`io/jucePath.hpp`.

---

## 7. Where the cross-thread state is

The inventory the model is measured by.

| What | Shared how | State |
|---|---|---|
| Parameter edits, interface → engine | `ToEngineQueue`, drained at the top of `process()`, in `paramsFlush()` and in `deactivate()`; freed unapplied at destruction | ✅ |
| Base-value changes, engine → interface | `ToUIQueue`, drained in `onMainThread()` and in `deactivate()` | ✅ |
| Modulated values, for painting | `ValueMailbox`, written per block, swept at 30 Hz | ✅ |
| Plugin → host notifications | `UIEdits` ring | ✅ |
| Which thread is which | `Threading::{isMainThread,isAudioThread}` | ✅ |
| Who owns the engine | `engineIsRunning()`: the audio thread while activated, the main thread otherwise | ✅ |
| The module chain | `ToEngine::{SetSlot,MoveModule,SwapChain}` + `ToUI::Retire`; built on the main thread, linked on the audio thread, and every route out of it retires | ✅ |
| The six global parameters | `ToEngine::SetBaseParameter` while audio runs, written directly when it does not; `setGlobalParameter()` asserts which. The other direction — a host automating one, or the engine declining a spectral size — reaches the knobs through `parameterChangedElsewhere()` and never from the audio thread | ✅ |
| `Engine::Setup` and the spectral storage | `spectralSetupPending_` (`std::atomic`) + `clap_host::request_restart()`, applied in `deactivate()` after the queue is drained | ✅ |
| An outstanding restart request | `restartRequested_`, `std::atomic`, test-and-set through `exchange` — both threads reach it | ✅ |
| The `Sample` | `ToEngine::SwapSample` + `ToUI::Retire`; the main thread keeps the file name and the decoded rate | ✅ |
| The module rack | recomputed from `programMain_`'s chain, by whatever changed it and by `chainChangedPending_` | ✅ |
| The LFO panel's tempo | `timingChangedPending_` (`std::atomic`), raised by the engine and cleared by `drainEngineEvents()` before the redraw | ✅ |
| Editor selection and active control | `SpectrumWorxEditor::{pSelectedModule_,pActiveControl_}`, per editor — and **not** what decides whether a widget is let go of; see §5 | ✅ |
| The LFO display and the shared module controls | children of the editor holding a raw `ModuleUI *`; dropped by `detachFrom()` on their own pointer | ✅ |
| `PopupMenu::menuActive_` | a member, per menu and therefore per editor; menus are dismissed before a strip, a chain or a program is replaced | ✅ |
| `Host2PluginInteropControler::blockAutomation_` | the interface's own, main thread only, since the assertion that read it from the audio thread went | ✅ |
| The editor pointer the plugin holds | `pEditor_`, set and cleared by the editor naming itself, so an unparented editor's teardown cannot clear a live one's | ✅ |
| **`LFOImpl::Timer`'s tempo** | three process-wide statics, `std::atomic` — no longer a race, still shared between instances | issue #11 |
| The random number generator | **per instance now, and per channel below that.** `Math::Rng` is a value; `SpectrumWorxCore::seedSource_` deals one to every LFO and to every channel of every effect that draws, at `reset()`. It was two file-scope `std::uint64_t`s that any number of audio threads read-modify-wrote unsynchronised, and that also made Freqverb, Whisperer and Burrito depend on the host's block size — one stream shared by both channels, advanced in whichever order the engine visited them. See issue #86 | ✅ |
| `SkinLifetime::liveEditors_` | a process-wide count, main thread only — every editor is built and destroyed there | ✅ |
| `PresetLoadReport` | a file-scope report the loader counts into and the caller takes; main thread only, and `stateLoad` drops it | ✅ |
| `GUI::preferences()` | process-wide, and correct: these are the user's application preferences, shared by every instance and backed by one file. Main thread only — everything that reads them runs under the host's message thread, which is also what makes `setPreferencesFolder()` safe for the tests | ✅ |

**JUCE has one owner.** `SkinLifetime` builds the `Theme` and installs it as the
default LookAndFeel for as long as at least one editor exists, and touches
JUCE's lifetime not at all — the shim's `juce::ScopedJuceInitialiser_GUI` is the
only thing counting it. Closing an editor used to call `shutdownJuce_GUI()`
against a counter JUCE's own initialiser never saw, which with two instances is
one of them tearing down the message loop the other is running on.
`isThisTheGUIThread()` and `isGUIInitialised()` ask JUCE through
`getInstanceWithoutCreating()` rather than asking a count of *our editors*
whether JUCE was up.

---

## 8. How this is checked

**Assertions.** Ten sites read `currentThreadMayMutateEngineState()` — the
tenth is `setGlobalParameter()`, which had none until 08.08.2026 and is how a
preset load came to write the engine's six globals from the main thread with
audio running. Every unimplemented message case asserts, so a caller that sends a
message nobody handles says so rather than dropping it.

**And an assertion is not a check.** `LE_ASSERT`/`LE_ASSERT_MSG` compile to
nothing under `NDEBUG`, so anything a shipped build must not do needs something
else. Three of them were the only thing standing in front of a real fault and
have been replaced by one: a bound on the chain walk that finds a module's index,
a range check in the preset browser's row selection, and the ownership question
`AutomatedModuleChain::setParameter()` now answers by handing the module back.
Two more were asserting something untrue and were deleted rather than satisfied.

**Sanitizers.** One `SW_SANITIZER` cache variable rather than a pair of blessed
build directories, applied before `add_subdirectory(libs)` so that it reaches
the dependencies too. It link-tests the flag and refuses with a diagnostic
naming the compiler, because `-fsanitize=realtime` *compiles* on Apple clang and
fails to link:

```
cmake -B build-rtsan -D SW_SANITIZER=realtime \
      -D SW_BUILD_PLUGIN_BUNDLES=OFF \
      -D CMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
      -D CMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
```

The deployment target is raised **for that tree only**: Homebrew's libc++ 22
refuses to compile against the 10.15 the plugin ships to ("The selected platform
is no longer supported by libc++"), and a sanitizer tree is not a shipping one.
Leave `CMAKE_OSX_DEPLOYMENT_TARGET` alone everywhere else — 10.15 is what decides
that `std::to_chars` for floating point is unavailable, which two files depend on.

`SW_BUILD_PLUGIN_BUNDLES=OFF` because clap-wrapper fetches the VST3 and
AudioUnit SDKs over the network at *configure* time, and a sanitizer tree wants
the test binaries and nothing else.

**A clean sanitizer run and an inactive one look identical**, so check the
instrument by reversion: a `std::malloc` planted in `runEngine()` is reported
with a stack naming the file and line. A `delete new int` is **not** — the
optimiser removes that pair — which is worth knowing before trusting a null
result.

**The cases that carry it.**

| | |
|---|---|
| `tests/core/threadCheckTests.cpp` | thread identity, driven through the C entry point rather than read off the source |
| `tests/core/engineOwnershipTests.cpp` | who may mutate the engine and when; that every block is written; that a spectral change waits and then lands; that a published chain comes back holding what it displaced |
| `tests/core/protocolTests.cpp` | refusal when full, survival past the end of the storage, and two cases that run real threads — 100k messages through an eight-slot ring arriving once and in order, and a mailbox swept while a writer runs flat out |
| `tests/gui/twoInstanceTests.cpp` | closing one editor leaves the other's `MessageManager` alive; selection is independent; ejecting a module and then its ghost |
| `tests/clap/hostInteropTests.cpp` | `reset()` between blocks; flush conditional on `isActive()`; both arms of every `canUseThreadCheck()` branch |
| `tests/clap/pluginTests.cpp` | *"A full rack with LFOs running and an editor open processes cleanly"* and *"Two instances process while their editors come and go"* — the latter with **two real audio threads** and a message thread opening and closing both windows underneath them |
| `tests/clap/threadingTests.cpp` | the cases that need two threads at once: a preset arriving while blocks are rendered, a chain queued behind a restart, a second `activate()` under a live callback, a host emptying a slot from inside `process()`; and the two `[lfo]` cases that read the *engine's* copy after an interface edit |

These are ordinary functional tests that *become* the acceptance test when the
tree is built with a sanitizer, and `threadingTests.cpp` says so at the top: each
case asserts the outcome that must hold whatever the interleaving, and separately
*creates* the overlap so that a `-fsanitize=thread` build has something to
report. A data race is undefined behaviour rather than a wrong answer that turns
up once in a hundred runs, so the second job is the one that pins the fix.

**One report is expected and is not a fault**: `ModuleFactory::create` allocating
inside `process()`, which is the recorded concession above. Nothing else in
`[threading]` is reported under `thread`, `address` or `realtime`.

**Under tsan, mind the harness.** `REQUIRE` from a worker thread writes Catch2's
shared assertion counter and is reported as a race in
`Catch::RunContext::handleExpr`. `ActivePlugin` has a non-asserting
`processStatus()` for exactly this: the workers record and the main thread
asserts.

**And flush through the harness rather than the extension.** A flush against an
active plugin is `[audio-thread]`, so `ActivePlugin::flush()` brackets it in a
`TestHost::AudioCallback` and then runs the callback a real host would;
`flushWithoutPump()` is the same call for the two cases whose subject is the
deferral. Reaching for `parameters( *plugin ).flush( &*plugin, … )` instead
calls it from the main thread, and a host that offers `clap.thread-check` says
so — which is how the case in `hostInteropTests.cpp` that asserts
`hostMisbehaviours().empty()` found it.

**A case that reads one copy is a case that cannot see this class of bug.** The
display, `paramsValue`, `stateSave` and the preset writer all answer from the
main thread's `Program`, so a test built out of any of them agrees with an edit
the audio thread never received — and one that asserts a *message was queued*
only moves the blind spot one step along. The LFO panel is where that was
demonstrated: all seven sub-parameters go through `editParameter()`, which moves
both copies — five of them always did, and the other two took a
`ToEngine::SetUnexportedLFOParameter` of their own until issue #159 gave them a
`ParameterID` — but the N/T/D buttons wrote
`LFO::addSyncType()` on the strip's own LFO and queued nothing, so a sync-mode
change moved the display and the saved state and nothing anybody could hear. It
was recorded as fixed on the day the waveform popup beside it was.

So an edit made in the interface is asserted **at the far end**: `[clap][lfo]` in
`threadingTests.cpp` holds both copies of one module's LFO, checks that the
engine's still reads the old value while the command is queued, drives a flush,
and reads it again. Both routes, and the waveform popup too — which the GUI case
cannot reach at all, a menu being one of the things a headless editor cannot
drive.

**And what no headless case substitutes for.** Hosts, by hand. Testers ran the
plugin in DAWs on macOS, Windows and Linux on 07.08.2026 and the deadlocks this
model was designed against are gone — which is the observation the redesign had
been an argument for.
