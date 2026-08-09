# SpectrumWorx — Tech debt

A running list, appended to as work happens. Companion to
[`todo.md`](todo.md), which is the work queue.

**What belongs here and what does not.** `todo.md` tracks *work*: things somebody
will sit down and do, in an order, with a size next to them. This tracks what is
left behind by work that is otherwise finished — the half-fix, the
correct-but-unsatisfying answer, the finding that has no owner because it is not
big enough to be an item and not small enough to be a `\todo`. A thing that is
squarely inside a numbered item is that item's; it does not need a bullet here.

The test for a bullet: **if `todo.md` were executed exactly as written, would
this still be true at the end?** If yes, it belongs here.

**A remediated entry comes out.** Not struck through, not marked done — removed,
because the point of this file is what is still true. If the reasoning behind a
closed entry was worth keeping, it goes into whichever "how it works" document
owns the mechanism.

Every entry carries the date it was written and, where there is one, the item or
section it fell out of — because half of these are true only until someone
touches the file, and a bullet with no provenance is unverifiable a month later.
New entries go at the top of their area.

---

## Build and platform

- **Windows Debug is out of the test matrix, and the reason is a guess.**
  (06.08.2026, from the CI item) The job did not fail — it hung, running to the
  step timeout and reporting nothing, so what was removed is a red square nobody
  could read rather than a diagnosis. The standing explanation is that a checked
  build of this suite wants more time or more memory than that runner has, and
  **nothing has reproduced it**: no Windows machine here can, which is the same
  reason the MSVC warning baseline is where it is.

  What goes with it is the ~1200 asserts, on Windows only. macOS and Linux still
  run both configurations, so a checked build is exercised on every push — but a
  Windows-*specific* assertion failure now has nowhere to fire, and MSVC is the
  compiler most likely to have one, being the one whose warnings nobody reads.
  The exclusion carries the cost in a comment (`build-plugin.yml:96-109`) so that
  putting it back is a decision somebody makes rather than one that gets lost.

  Still excluded as of run `31287823776` (08.08.2026), where the five remaining
  legs are green and `Test - windows-x64 Release` is the only Windows one.
  **Asked and deferred on 08.08.2026**: retrying it is one job and a raised
  timeout, and it is not worth a release slipping for.

- **The factory samples decode to different lengths on different macOS
  versions.** (05.08.2026) `MW-Metallica1.mp3` holds, by `afinfo`, "21454 valid
  frames + 576 priming + 1010 remainder = 23040" at 44.1 kHz. At 48 kHz it
  decodes to 25077 frames on macOS 26 and to 23351 on a GitHub `macos-latest`
  runner — exactly the padded and the unpadded lengths — so the two decoders
  disagree about whether LAME's encoder delay is theirs to strip.

  It surfaced as a test that asserted the padded length (`sampleTests.cpp`, now
  a floor rather than a duration), but the difference is not confined to the
  test: the side-chain sample feed plays whatever the decoder returned, so on
  one platform a factory sample begins with ~33 ms of encoder priming and on
  another it does not. Nobody has listened for it and no preset depends on the
  alignment. If one ever does, the fix is to trim by the gapless metadata
  ourselves rather than to ask which decoder answered.

- **The include-what-you-use sweep did not happen, and the obvious tool is wrong
  about this tree.** (04.08.2026, from stage 7) The mechanical half of that item
  did land — `LE_IMPL_NAMESPACE_BEGIN` is gone, and with it the 47 files that
  used a macro without declaring where it came from — but no include was removed
  on the strength of an analysis. clangd's include-cleaner cannot see through
  this codebase's macros: it reports `symmetric/parameter.hpp` as unused in
  `pitchShifter.hpp`, where removing it produces nine errors, because
  `LE_DEFINE_PARAMETER(SemiTones, SymmetricFloat, …)` names the type inside a
  macro argument. Every effect header is that shape.

  The other half of the reason was that this platform could not answer the
  question that matters — a header macOS gets transitively and Windows does not is
  invisible from here. That half is gone as of 06.08.2026: the matrix compiles
  every commit on MSVC and on two GCCs, so a removed include that only Windows
  needed now fails a job rather than somebody's afternoon. What is left is the
  sweep itself, and a tool that can see through `LE_DEFINE_PARAMETER`.

- **Four GCC 15 fixes are compiled by two GCCs, neither of which is a GCC 15.**
  (04.08.2026, measured again 06.08.2026) A Linux build of `00383f6` reported 469
  warnings from four causes, and all four are fixed here: `<ciso646>` deleted from
  the force-included header (294 `-Wcpp`), `valueOffsetGetter()` rewritten to take
  the difference between two addresses of a real object instead of dereferencing
  null (142 `-Wnonnull`, and the source had been calling it UB since 2016),
  `SpectrumWorxCore::Module` moved above its first unqualified use (29
  `-Wchanges-meaning`), and the VST3 SDK's `std::wstring_convert` suppressed on
  `base-sdk-vst3` (4, not ours).

  CI closes most of this: `ubuntu-latest` is GCC 12.4 and the release container is
  GCC 11, and both compile our sources under `-Wall -Wextra -Werror` on every
  push. What neither of them is is the compiler that produced the 469 — the two
  bulk causes are diagnostics both of those GCCs have, but `-Wchanges-meaning` is
  a newer one, so that fix is still checked by reading. `build-gcc/` here is
  configured for Homebrew's `g++-15` and is the local way to ask.

- **The warning baseline stops at the MSVC line.** (04.08.2026, still true
  06.08.2026) `-Wall -Wextra -Wno-unused-parameter -Wno-unknown-pragmas` is on our
  own sources on every compiler that takes those spellings, and `-Werror` with
  them on Apple and wherever CI passes `-DSW_WERROR=ON` — which is now every leg,
  so Apple Clang, GCC 12.4 and GCC 11 are all held to it. MSVC gets nothing:
  `SW_WERROR` is a no-op there by construction (`sw-our-sources.cmake:75`), so the
  one platform in the matrix that compiles warning-blind is the one nobody here
  can run a compiler for.

  The reason not to has expired. `/W4 /WX` was "a few hundred warnings delivered
  to somebody else's afternoon" when Windows arrived as a build log; the matrix
  now runs MSVC 19.51 on every push, so turning it on is a line in
  `sw-our-sources.cmake` and one red square that lists them. It is still a
  decision — the count is unknown and could be large — but it is no longer a
  decision that costs anybody a day to *learn the size of*. **Asked and deferred
  on 08.08.2026**: worth doing, not worth doing before a release.

  `-Wno-unknown-pragmas` covers 288 `#pragma warning(...)` lines — 3772 of the
  3902 warnings the baseline first produced, all of them MSVC diagnostic control
  and inert off MSVC by design. It costs the detection of a misspelled pragma,
  which is worth knowing about because the tree contained one: a
  `#pragma warning(push)` where a `pop` was meant (`assertionHandler.cpp`).
  `-Wunknown-pragmas` had not caught it and could not — both spellings are
  equally unknown to clang. It was found by reading.

- **A source that misses the force-included ODR header now builds.**
  (04.08.2026) Measured: all 148 of our translation units compile with
  `-include leConfigurationAndODRHeader.h` removed. That is new, and it is worse
  rather than better — until `LE_IMPL_NAMESPACE_BEGIN` was written out, a file
  that missed the header failed to compile, confusingly but loudly. What the
  header still decides is `NDEBUG`, which decides whether the ~1200 asserts exist
  and whether `ModuleNode` has a virtual, which decides the layout of every
  module object. So the failure mode went from a wall of errors to a silent ABI
  disagreement. `tests/checkODRHeaderScope.cmake` is the only thing standing
  there, and it only runs under the Ninja and Makefile generators.

- **A `%g` fallback means a very wide value does not round-trip at full
  precision.** (08.08.2026) `lexical_cast` takes the buffer it may write to now,
  so the size it is bounded by is the size the caller actually has. A value too
  wide for that buffer at the precision asked for prints as `%g` — the right
  number, but six significant digits rather than nine.

  For a display that is the correct answer. For `presets.hpp`'s `makeString`,
  which writes the number into a file, it would be a loss, so that one sizes its
  buffer with `RequiredStringStorage` — 321 bytes for a `double`, which is what
  `%.9f` of one actually needs. Nothing in the shipping banks is anywhere near
  that wide and the corpus digests did not move, so this is a property to keep in
  mind rather than a defect: a parameter whose values ever reach 1e300 wants its
  own printer, not a wider buffer.

- **MP3 decoding is a different decoder on macOS than on Windows and Linux, and
  which one answers is decided by registration order.** (01.08.2026)
  `registerBasicFormats()` gives `CoreAudioFormat` on macOS,
  `WindowsMediaAudioFormat` on Windows and **nothing** on Linux, so `sw-dsp`
  defines `JUCE_USE_MP3AUDIOFORMAT=1` — JUCE's own decoder, behind a flag because
  it carries a patent disclaimer for patents that expired in 2017. Every one of
  the seventeen factory samples is an MP3, so without it a Linux build ships
  content it cannot open.

  What that flag actually did is worth reading off
  `juce_AudioFormatManager.cpp:63-87` rather than off the flag's name.
  `createReaderFor` walks `knownFormats` **in registration order** and takes the
  first that accepts the stream, and `MP3AudioFormat` is registered *before*
  `WindowsMediaAudioFormat` and *after* `CoreAudioFormat`. So:

  | | answers for MP3 |
  |---|---|
  | macOS | `CoreAudioFormat` |
  | Windows | `MP3AudioFormat` — **not** `WindowsMediaAudioFormat`, which the flag displaced |
  | Linux | `MP3AudioFormat` |

  Two decoders, then, not one and not three — and setting the flag quietly
  changed Windows from the platform decoder to JUCE's. That is probably the
  better outcome (it is the same code as Linux, so two platforms agree by
  construction) but it was not the intent and nothing says so.
  - The cheap fix is to stop shipping MP3: the samples are 1.4 MB as MP3 and the
    only reason for the format is that 2016 chose it. FLAC is registered on every
    platform, ahead of all of these, and would delete this entry and the
    decode-length one above it. **Asked and deferred on 08.08.2026**: it is a
    re-encode of seventeen files and a size increase, and nothing is waiting on
    it.
  - The `sampleTests.cpp` cases would catch a decoder that fails outright. They
    would not catch one that is a few samples out of alignment with another
    platform's — encoder delay is exactly where MP3 decoders disagree — which is
    what would actually happen.

- **The build directories in this tree are stale, and a stale one is exactly the
  kind of thing that gets trusted.** (01.08.2026, measured again 06.08.2026)
  `build-asan/` was the first: configured from an older CMake, it registers three
  of the nine GUI tests, so a sanitiser run over it is quietly a third of the
  coverage a normal run has. Superseded in principle by `SW_SANITIZER` — one cache
  variable rather than a pair of blessed build directories,
  `threading_model.md` §8 — but nothing owns the directory itself.

  It is not one directory any more. `build/` and `build-release/` both list **291
  tests** where CI runs **308**, which is the seventeen `sw-show-ui` renders that
  had stopped being registered plus what has landed since — so both of the trees
  a local `ctest` is normally pointed at would report a green suite that is
  missing cases. The counting rule that follows: **CI is the authority for how
  many tests there are**, and a local number means the directory it came from was
  reconfigured first.

## Threading

- **A "closed" entry can be closed for one of the two things it named.**
  (06.08.2026) Kept as the shape of a mistake rather than as an open debt. The
  Waveform/SyncTypes routing was recorded closed earlier the same day, on
  `ca9029d`, whose message says "an LFO's waveform **and sync mode**". Only the
  waveform had a route: the waveform popup goes through
  `updateParameterAndNotifyHost<>`, which queues
  `ToEngine::SetUnexportedLFOParameter`, and the N/T/D branch of
  `LFODisplay::buttonClicked()` called `LFO::addSyncType()` on the strip's own
  LFO — `programMain_`'s — and queued nothing. So a sync-mode change stayed
  silently inaudible for the rest of the day, under an entry that said it was
  fixed.

  Both halves are routed now and `tests/gui/lfoDisplayTests.cpp` covers them.
  What is worth keeping is why nothing caught it: **no case in the suite had ever
  read the engine's side of an LFO after a UI edit.** The display, `paramsValue`,
  `stateSave` and the preset writer all answer from the main thread's copy, so
  every existing case agreed with a change the audio thread never received.

- **A host writing a slot selector allocates on the audio thread.** (02.08.2026,
  narrowed 08.08.2026) The one exception to "modules are built on the main
  thread" (`threading_model.md` §5). Every other route — the interface, a preset,
  a session — builds its modules on the main thread and hands the engine a
  pointer to link. A host's parameter event arrives inside `process()`, and
  deferring it means a round trip to the main thread and back before the slot
  changes, which is a latency a generic panel would notice.

  It used to *free* on that thread as well, which was the unrecorded half and was
  not a concession anybody had made: unlinking dropped the chain's last reference
  to the displaced module and ran its deleter under the callback. That is fixed —
  `AutomatedModuleChain::setParameter()` hands the module back for the retire
  queue — so the allocation is now the only thing a realtime-sanitizer run over
  `[threading]` reports, which is what makes the entry checkable rather than
  merely stated.

- **Rendering real spectra trips the negative-amplitude verification.**
  (02.08.2026, from `presetRenderTests.cpp`) `goldenTests.cpp` already records
  this for one effect — a running sum across thousands of bins drifts a hair
  below zero and the next module reads it as an amplitude — and playing the
  factory banks shows it is not one effect: **at least eight of the 303 presets**
  abort a checked build on it, and the iteration was stopped rather than
  finished. Benign in the output, which is why both files are release-build
  artifacts and why the release run renders all 303 finite. It is a weakness in
  the vector primitives, and a skip list would need a dozen names and would grow.

- **The LFO panel does not follow the host's tempo.** (02.08.2026)
  `SpectrumWorxEditor::updateForNewTimingInfo()` is correct and unreachable: its
  one caller was `SpectrumWorx::updatePosition()` in the 2016 host class, which
  is deleted. The CLAP's equivalent is `updateLFOTiming()`, on the audio thread,
  and reaching a widget from there is what the whole threading model forbids —
  so the answer is a `ToUI` message, and the function is where it lands. Visible
  as an LFO panel showing the old period after a tempo change.

- **`LFOImpl::Timer`'s tempo is one value for every instance in the process.**
  (02.08.2026, narrowed 06.08.2026) `std::atomic`, so it is no longer a data race
  — but two tracks at two tempi still see one tempo. Making it per-instance means
  threading a timer through `snapPeriodScale()` and the period-scale bounds, all
  of them static and all called from the parameter layer and the editor; that is
  the LFO parameter interface's redesign rather than the threading model's.

  **What this used to reach, and no longer does.** `[preset-corpus]` failed about
  one run in three when the whole suite ran bare in one process — 153 of the 303
  rows, the ones with a tempo-synced LFO — because `adjustValueForPreset`
  converted a Free LFO's period through the global bar duration, so once a
  `[clap][lfo]` transport case had told the plugin a tempo, every later preset
  load in that process converted differently. The split into two binaries hid the
  symptom and fixed nothing. The conversion reads
  `Timer::referenceBarDuration` now — a constant — so a preset loads to the same
  numbers from any tempo, and `[preset-corpus]` is no longer order-dependent.
  Same for the third static, `hasTempoInformation_`, which is deleted.

  What is left is the honest remainder: two instances at two tempi share one
  `barDuration_`, so a synced LFO in one of them snaps to the other's grid. That
  needs a real per-instance timer and nothing forces it yet — every host this has
  been run in has one tempo at a time. See
  [`how-lfo-rates-work.md`](how-lfo-rates-work.md) §7.

- **`UIEdits` drops on full, and that is wrong for gestures.** (01.08.2026,
  amended 08.08.2026) The ring is otherwise correct. Dropping a `Kind::Value` is
  right — the next one supersedes it. Dropping a `GestureBegin` whose
  `GestureEnd` survives leaves the host holding an unbalanced gesture, which some
  hosts never recover from. The drop is at least *counted* now —
  `SpectrumWorxCLAP::droppedMessages()` — so it is no longer silent; what is
  still owed is not dropping it.

- **A dropped ring message is counted and cannot be repaired.** (08.08.2026)
  `SpectrumWorxCLAP::pushed()` records every message a full ring throws away,
  which is what makes the failure sayable rather than silent. It is not a fix.
  Each of these pushes happens *after* the same change has been applied to the
  other side, so a drop is a divergence — the main thread's `Program` behind the
  engine, the engine behind the interface, or the host not told about an edit —
  and the ring was where the information to undo it would have been.

  The design answer for the *echo* leg specifically is a second `ValueMailbox`
  rather than a ring: a mailbox cannot overflow, and coalescing is correct for
  an echo because its job is to make the two copies equal rather than to replay
  a sequence. `parameterIDFromIndex`'s layout even orders a sweep the right way
  round — slot selectors sort before the parameters of those slots. The note on
  `valueMailbox.hpp` argues the opposite for base values and would need amending,
  which is why this is a decision rather than a patch. The other legs — a slot
  change, a chain, a move, a sample — are commands with an order and a mailbox is
  wrong for them; those want either a deeper ring or backpressure, and
  backpressure on the main thread is what the 2016 lock was.

  Nothing has been observed above zero. `tests/clap/publishProtocolTests.cpp`
  fills each ring on purpose and pins what a drop costs, so the cost is now
  measured rather than assumed.

## Host interface

- **`paramsValueToText` ignores the value it is given.** (01.08.2026) It prints
  the parameter's *current* value whatever it was asked about, which is visible
  in every automation-lane tooltip in every host — a host asking "what would 0.25
  read as" is told what the knob reads as now. The fix is an
  `AutomatedParameterPrinter` arm that takes a value *and* the live parameter, so
  that a dynamic range has an owner to ask; the source `\todo` has the argument.

  This used to be half of an entry that also covered `paramsTextToValue`
  returning false. That half closed on 07.08.2026 — `parameter_system.md` §8 —
  and it closed *without* needing this: parsing never constructs a parameter, so
  it never had the dynamic-range problem that keeps the printer one-armed. Which
  is why what is left here is smaller than it looks, and why nothing new depends
  on it.

- **A load problem has nowhere to go but a modal box.** (08.08.2026) A session
  restore is a load nobody asked for: the host is opening a project, there may be
  no window yet, and a modal dialog in the middle of it stops the host to ask a
  question about something the user has not finished opening. `PresetProblem`
  exists so the preset layer reports rather than interrupts and the *caller*
  decides — and `GUI::loadPreset` still decides by asking whether an editor is
  open, which says who is watching only by accident. A host restoring a session
  while the window happens to be open still gets `reportToTheUser`'s summary.

  Marked rather than fixed: `GUI::UnattendedLoad` is a scope `stateLoad` enters
  and `warningMessageBox` asserts against, so a checked build says so and a
  release build behaves as before. Two things are wanted before it can be more
  than an assert. The load needs to carry *who asked* rather than infer it —
  which is a parameter through both `loadPreset` overloads — and a plugin needs
  somewhere non-modal to put "the audio file this project names is missing", the
  honest answer to which is a line in the editor rather than a dialog at all.

  The sample's own dialog is gone as of this entry's date: `setNewSample` returns
  the reason it failed instead of showing it, and the only caller that raises a
  box is the editor's file menu, where a user picked the file a moment ago.

- **An unconnected side-chain port is indistinguishable from a connected one.**
  (03.08.2026) `runEngine` falls back to the main input only when
  `audio_inputs[1].data32` is *null* (`spectrumWorxCLAP.cpp:876`), and no real
  host gives us null — every wrapper hands over a buffer it owns. So what an
  unpatched side chain contains is whatever the host put there, and the plugin
  cannot tell "silence" from "not connected" from "never written".

  Two consequences. The documented intended behaviour — **an unpatched side chain
  is the main input, so a Blender with nothing patched blends the signal with
  itself** — is therefore effectively dead code, undocumented anywhere a user
  would look and unreachable anywhere it would matter. And it is how uninitialised
  memory reached the FFT from an AU host: clap-wrapper handed every channel of an
  unconnected AUv2 bus a buffer it had allocated and never zeroed, `auval` aborted
  5 runs of 5 on a NaN in `rectangular2polar`, and the plugin had no way to know
  the port was not really connected. Fixed upstream (clap-wrapper #498) rather
  than here. CLAP's own mechanism is `clap_audio_buffer::constant_mask`, which
  nothing here reads.

  Still open, and not closed by the tests added on 05.08.2026 — those drive all
  three arms `runEngine` can distinguish (no second port, a second port with no
  `data32`, a second port with audio) and confirm the first two are the same
  fallback. What they cannot do is give the plugin a way to tell a *connected*
  port carrying silence from an unpatched one carrying whatever the host left
  behind, which is the entry.

- **The engine's guards are finiteness guards, and garbage is usually finite.**
  (03.08.2026) Every `LE_MATH_VERIFY_VALUES` on the input path tests `Invalid`
  (NaN/infinity) or denormals. Uninitialised memory read as float is
  overwhelmingly *huge and finite* — the measured value was 2.9e33 — so it passes
  `time2DFT`'s checks on the time domain, on the window and on both FFT outputs,
  and only becomes NaN when squared inside `vDSP_zvabs`. The assert that fires is
  therefore three layers away from where the bad data entered, which is why the
  entry above cost a day. A magnitude bound on the incoming block would have named
  it immediately; whether the engine should carry one in a release build is a real
  question and not obviously yes.

## Parameters and LFOs

- **The measure-numerator half of `establishedChange()` is reasoned, not
  measured.** (03.08.2026) It reports no change for the meter as well as for the
  bar duration, on the same argument — there was nothing to change *from*. The
  synced arm of `updateForNewTimingInformation()` resnaps the period when the
  numerator changes, so the same class of bug exists there. But **nothing in the
  suite drives a meter other than 4/4**, so that arm is unexercised in both
  directions. A case at 3/4 would settle it.

## DSP and effects

- **The Exaggerator's behaviour next to an empty bin is a cliff.**
  (02.08.2026, from `presetRenderTests.cpp`) Its intensity maps to an exponent
  over [-1, 4] and it raises every normalised bin to it, so with a negative
  intensity the gain applied to a bin grows without bound as that bin approaches
  zero. The NaN this produced is fixed — `pow( 0, negative )` is `+inf`, one
  infinity zeroed the normaliser and the whole spectrum followed, in four shipped
  presets — but the fix is "an empty bin stays empty", which is a discontinuity
  rather than a rounding of one: a bin at 1e-30 is still boosted enormously and a
  bin at exactly zero is not boosted at all. A floor on the input would be the
  honest shape, and choosing one is a DSP decision with an audible answer.
  The unexplained `/ 2` in its normaliser is worth the same look. Deferred with
  the three parameters below; see that entry.

- **A phase-vocoder pitch shift's accuracy depends on the FFT size, and not
  monotonically.** (01.08.2026) Measured, with Pitch Magnet asked to
  move a 220 Hz partial to 880 Hz and the output's dominant frequency read back:

  | FFT size | lands at | error |
  |---|---|---|
  | 1024 | 707.9 Hz | **−377 cents** (the target is the *second* loudest thing present) |
  | 2048 | 880.1 Hz | +0.2 cents |
  | 4096 | 922.8 Hz | **+82 cents** |

  110 Hz and 330 Hz targets are within 0.2 cents at both 2048 and 4096, so it is
  the large upward shift that degrades. "More bins are better" is not the shape
  of this and nobody has looked at why. `exImploderImpl.cpp` carries a 2012
  `\todo` from Domagoj Saric saying the pitch shift there "is not the correct way
  to do it (although Dobson does it that way)", which may or may not be the same
  finding. The property test pins 2048 and says so; that is a test choosing a
  setting where the measurement is unambiguous, not a fix.

- **Three effect parameters have ranges most of which do nothing useful.**
  (01.08.2026) All three were found by writing property tests and all
  three read as bugs to a user:
  - **Slew Limiter's rise starts from `FLT_EPSILON`**, which the implementation
    floors the previous amplitude to so that a bin can leave silence at all.
    That is 138 dB below unity, so a rise-limited bin has to climb 138 dB before
    it is audible: **at 3 dB/s that is 46 seconds**, and anything below about
    28 dB/s — the bottom tenth of the 0–300 dB/s range — takes more than five.
    That part of the knob is a mute with extra steps.
  - **The Exploder's "Limit" does not limit.** Reaching it *resets* the
    accumulator to whatever the input is doing, so the level is a sawtooth
    rather than a ramp to a ceiling. Defensible as an effect; the parameter is
    named for the other behaviour.
  - **The Octaver's cutoff defaults to 350 Hz**, and it is a low pass over the
    effect's *output*. So an Octaver dropped into a slot removes most of what it
    just added: the up-octave of anything above F3 (175 Hz) is cut, which is
    most of what anyone plays. This is the one that most reads as "the effect is
    broken".

  None of these is a regression — all three are 2016 behaviour, now pinned by
  tests. Changing any of them changes what a 2011 preset sounds like, which is
  why none of them is in a plan. **Asked and deferred on 08.08.2026**, together
  with the Exaggerator's cliff below: the four of them are the standing list of
  effects that behave defensibly and read as broken, and a release is the worst
  moment to silently change what an existing preset sounds like. Documenting the
  Octaver's cutoff and the Exploder's Limit is worth more than moving either.

- **The goldens skip in a checked build because `Smoother` asserts.**
  (01.08.2026, from `goldenTests.cpp:331`) `Math::symmetricMovingAverage` carries
  a running sum across thousands of bins and over pink noise the accumulated
  rounding drifts a hair below zero, so `Smoother` hands `amph2DFT()` a negative
  "amplitude". Benign in the output, real as a numerical weakness. The
  consequence is structural: **Release is the only configuration that renders
  DSP**, so a debug-only regression in the engine has nothing to catch it. The
  effect property tests are what a checked build has instead, and as of
  05.08.2026 they reach all 57: nine in `amplifyingEffectsTests.cpp`, four in
  `silentDefaultsTests.cpp`, and every one of the 57 in `sideChainTests.cpp` —
  which has to name Smoother as its single exception for exactly this reason.

- **Four side-chain effects are indistinguishable from deaf at their defaults.**
  (05.08.2026, from `sideChainTests.cpp`) Which is why the side-chain fixtures
  configure them, and it is worth reading as a *user*-facing observation rather
  than a test one — dropping any of these four into a slot and patching a signal
  into the side chain port does nothing until a second parameter is moved:
  - **Slicer**, **Denoiser** and **Convolver** each have an enumerated mode whose
    default is its *first* enumerator, and in all three cases that enumerator is
    the one that ignores the side channel (`Hold`, `Main`, `Triggered`).
  - **Convolver**'s is the strongest form of it: `Triggered` means the impulse
    response is grabbed on a button press, so until then the effect renders
    **silence**. Eight of the 25 identically-hashed golden fixtures are
    Convolver's, and they are not a quiet render — they are an unarmed one. See
    "25 golden fixtures render silence" under Tests.
  - **Burrito** chooses its replacement positions only when its frame counter
    wraps `Period`, which defaults to 250 ms, so nothing at all happens for the
    first quarter second whatever is on the port.

  None of this is a regression and all of it is 2016 behaviour. It is recorded
  because "the side chain does nothing" is a plausible bug report against four of
  the fifteen, and the answer is a parameter rather than a fix.

## GUI and skin

- **The knobs are 127 pictures of a knob.** (07.08.2026, from the skin
  vectorisation) `02`, `03`, `12`, `63` and `64` are film strips: one tall image
  holding 127 frames, indexed by `Knob::paintFilmStrip` from the value. They are
  vectors now, which fixed their size — 415 KB of PNG became 121 KB of SVG — but
  a tall SVG of 127 frames is still 127 frames, and it keeps what the
  arrangement costs:

  - **The value moves in 127 steps.** `pictureIndex` is
    `126 * valueToProportionOfLength(value)`, so a slow drag visibly stairsteps.
    No amount of resolution in the artwork changes that: the quantisation is in
    the frame index, not in the pixels.
  - **It is the only artwork that cannot be drawn at an arbitrary size**, because
    a frame's height is baked into the sheet.

  **They are not all the same problem, which is worth knowing before anyone
  plans the fix.** Measuring the frames turned up two different animations:

  - `02` is a rotating knob — a pointer swept through 270°, everything else
    fixed. This one is replaced by drawing the knob once and rotating the
    pointer by the value at paint time.
  - `03`, `12`, `63` and `64` **do not rotate at all**. The frame-to-frame change
    is a *shape*: a blue wedge opening from a fixed edge (`03`/`63` clockwise
    from about 7:30, `12`/`64` from twelve o'clock in either direction) whose
    inner radius grows as it opens, with a black cap whose radius *is* that inner
    radius. A transform cannot express that; it needs a wedge generated from the
    value.

  Either way the change is to `Knob` and `ModuleKnob` — they would ask for "the
  knob at proportion p" rather than "frame n", which means a second kind of
  artwork (a knob, not a picture) — and not to anything in `assets/skin`. That
  is why it is here and not in the conversion: converting the strips does not
  get to it, and nothing in `todo.md` does either.

- **JUCE's SVG parser reads `xlink:href` and not `href`.** (07.08.2026)
  `getLinkedID` (`juce_SVGParser.cpp`) looks only at the namespaced spelling, so
  a `<use href="#id">` — the modern form, which every browser and `rsvg-convert`
  accept — silently draws nothing under JUCE while verifying perfectly against
  the renderer used to check these files. Anything in `assets/skin` that uses
  `<use>` must say `xlink:href`. There is no test that would catch the other
  spelling, because the comparison harness is `rsvg-convert`.

## Tests

- **Three effects now carry no numeric contract at all off the machine that
  minted the fixtures.** (06.08.2026, from the CI item) Ethereal, Vaxateer and
  Merger take `SWTest::Tolerances::sameBuildOnly()` — infinities on all four
  bounds — in both `goldenTests.cpp` and `sideChainTests.cpp`. What still holds
  them anywhere is the non-finite count and `Digest::peak == 0`; what holds them
  numerically is the bit-exact hash, and only on the machine that wrote the file.

  The mechanism is sound and is written up at both call sites: all three branch
  per bin on a comparison **between two computed spectra** and then substitute
  the side channel's value for the main one, so a flipped bin does not move a
  value, it swaps in a different one — and where the phase goes with it,
  neighbouring bins line up and the peak moves far more than the rms. Measured
  macOS/arm64 against Linux/x86_64, Ethereal reaches a relative peak of 0.99 and
  111 dB in a band, which no bound could admit and stay a test.

  Two things are debt rather than design:

  - **Merger was exempted by reading, not by measurement.** It is inside
    `amplified()` on macOS/arm64 and on Linux/x86_64 and was put outside it
    because *Windows* failed on it — so two platforms lose cover they had, to
    describe a third. The alternative was waiting for a fourth runner to name
    the next one.
  - **The list was built one runner at a time and gave two answers on one
    architecture.** Linux/x86_64 named Ethereal and Vaxateer; Windows/x86_64 —
    same architecture, same pffft, GCC against MSVC — left those alone and named
    Merger. A list assembled that way is a description of which machines have
    run, not of which effects are sensitive.

  What would restore real cover is a behavioural case per effect, the way
  `amplifyingEffectsTests.cpp` answered the nine it covers. Nothing owns
  that. And renders that disagree by 99 % between two architectures are worth a
  look on their own account: this entry says only that a fixture cannot referee
  them.

- **25 golden fixtures render silence, and none of it is about external audio.**
  (05.08.2026, from `silentDefaultsTests.cpp`) A silent render hashes identically
  on every platform and has zeroes in every numeric column, so those rows agree
  with any build ever made — 25 of 464 fixtures that cannot fail. `todo.md` put
  it down to the sample loader having been compiled out and proposed feeding a
  fixture a factory sample; **both halves are wrong**. `Sample` is built on
  `juce::File` and the goldens live in `sw-dsp-tests`, which links no JUCE at all
  by design, so such a fixture would not link. And the four causes are ordinary
  parameter defaults:
  - **Convolver** (8 rows) defaults to `Triggered`, so there is no impulse
    response until Grab IR is pressed. Unarmed rather than quiet.
  - **Frecho and Frevcho** (16) default to 100 m, and the delay is the round trip
    at 343 m/s: 583 ms against a 371 ms fixture. These sixteen are a statement
    about the *fixture length* — a two second matrix would never have had them.
  - **Freqnamics** (1) gates at −60 dB, and one impulse spread over 2048 bins is
    quieter than that per bin. The same render at 512 bins is not, which is why
    only one of its eight rows is silent.

  All four are 2016 behaviour and moving any of them changes what a preset sounds
  like, so none is a fix. The count is now held by a case and can only fall.

- **`LFOImpl::Timer::setPosition( float )` asserts two things that are both
  false, and is dead.** (01.08.2026) `lfoImpl.cpp:753-755` reads
  `LE_ASSUME( barDuration_ == 4 )` and
  `LE_ASSUME( measureNumerator_ == 60.0f / 120 * 4 )` — the two values swapped
  between them. The initialisers three hundred lines up are
  `barDuration_( 60.0f / 120 * 4 )`, which is 2, and `measureNumerator_( 4 )`, so
  both assumptions are false as written. Nothing has noticed because nothing
  calls it: its only caller is `Engine::Processor::setPosition`, and that has no
  callers at all in the CLAP path. Harmless while dead; `LE_ASSUME` is
  `__builtin_assume` in a shipping build, so reviving the caller without fixing
  the pair would hand the optimiser two false facts about live values.

- **A slot filled from the editor needs its rack resynced by hand, and two
  harnesses do it and one did not.** (03.08.2026)
  `addUserAddedModule` ends in `refreshModuleRackAsync()`, so a caller with no
  message loop has to call `resyncModuleRack()` itself; `pluginTests.cpp` did and
  `tools/show-ui`'s editor-module page did not. For a month that page rendered an
  editor with a highlighted empty slot and no module in it — the page whose whole
  purpose is proving a module's widgets can be built. It was invisible until the
  sweep that drew all 57 effects made them produce *byte-identical* PNGs. Fixed
  there, but the shape of it is not: "build the thing, then pump the async step by
  hand" is an unwritten rule that three harnesses now follow separately, and the
  next one will not know either.

- **`ctest -LE slow` skips nothing.** (01.08.2026) No test in the repo sets
  `LABELS`; the one labelled case went with `check_gui_flag_parity.py`. Either
  re-establish the label or stop recommending the flag — several documents do.

- **clap-helpers' flush validation calls its own `[main-thread]` entry point
  from the audio thread.** (03.08.2026) `clapParamsFlush` guards
  itself correctly with `ensureFlushThread` — `active ? audio : main`, matching
  ext/params.h:303 — and then validates each event through
  `checkValidFlushEvent` → `getParamInfoForParamId`, which opens with
  `checkMainThread()` and then calls `clapParamsInfo`, whose C entry point opens
  with `ensureMainThread`. So a correct host flushing a correct plugin is
  reported for misbehaving, twice per event, at `CheckingLevel::Maximal`.
  Upstream's, and it is only visible to a host that answers `clap.thread-check`,
  which is why nothing had seen it. `TestHost` filters the two `clap.log`
  messages by exact wording; the third — `getParamInfoForParamId`'s own
  `checkMainThread()` — goes straight to `std::cerr` and cannot be intercepted at
  all, so it is one line of noise per flush on the test output. Worth a
  clap-helpers issue; worth nothing local.

- **`ActivePlugin` is the only harness that flushes on the thread CLAP says to.**
  (03.08.2026) `params.flush()` is `[active ? audio-thread :
  main-thread]`, and every `params.flush(&*plugin, …)` call site in
  `pluginTests.cpp` — several dozen — makes it from the main thread against an
  active plugin. Harmless there, because those cases use hosts that offer no
  thread check and so nothing asks; but it means the flush path has only ever
  been driven from the wrong thread, and `paramsFlush()` calls `drainCommands()`
  and `handleEvent()`, which are the two things `process()` does to the engine.
  Converting them to `plugin.flush(…)` is mechanical and would put the whole
  parameter-event path on the audio thread under tsan and rtsan, where it belongs.

- **A `checkMainThread()` failure is invisible to `TestHost`, and one case
  overclaims because of it.** (03.08.2026) clap-helpers'
  `checkMainThread`/`checkAudioThread` write to `std::cerr` directly
  (plugin.hxx:2219, 2233) rather than through `hostMisbehaving`, so nothing that
  routes through `clap.log` can see them — which is the same limitation the flush
  entry above records, seen from the other end. The consequence is that
  `hostInteropTests.cpp`'s "Driven the way a DAW drives it, **nobody
  misbehaves**" asserts only over the reports that *can* be intercepted: that
  case emits one such line to stderr and passes. Either capture stderr in
  `TestHost` for the duration of a case and assert on it too, or rename the case
  to what it actually checks. The first is worth more and is not hard.

