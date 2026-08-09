# SpectrumWorx — What is left to do

The work queue. Everything here is open; when an item closes it comes out of this
file rather than being marked done, and whatever it left behind goes to
[`tech_debt.md`](tech_debt.md).

Sizes are estimates and have been wrong in both directions. The rule that has
held: **the estimate is usually right and the value is usually somewhere else**
— five of the last seven items were worth more for what they found than for what
they were scoped to do. The CI matrix is the clearest case yet: it was scoped as
plumbing and it found a wrong `log2` that had shipped since 2016, seventeen
tests that had silently stopped being registered, and the reason three effects
cannot be held to a number off the machine that minted their fixtures.

---

## Where this stands

| | |
|---|---|
| Builds | CLAP, VST3, AUv2, standalone, on every push: macOS universal, Windows x64 under MSVC 19.51, Linux x64 under GCC 12.4, and again in an Ubuntu 20 / GCC 11 container for the glibc a released binary needs. |
| Runs | **In DAWs, on macOS, Windows and Linux, driven by testers rather than by us** (07.08.2026). The deadlocks that motivated the threading redesign are gone, and the plugin works. That closes the question the redesign was an argument about: it is an observation now. |
| Tests | **Green on 08.08.2026** — 371 registered cases, Debug and Release. Two binaries, `sw-dsp-tests` and `sw-plugin-tests`, plus 66 `sw-show-ui` renders. Goldens run in Release only. The count went 333 → 371 over the response to the code review in [`old/fable_review.md`](old/fable_review.md); the goldens and the 303-preset corpus digests did not move. |
| Validators | `clap-cpp-validator` **22 of 22 with zero failures** and `vst3-validator` **47/47**, both re-run on 07.08.2026 against `text_to_value`. `auval` was 5 runs of 5 on 06.08.2026 and **has not been re-run since**: it reads `~/Library/Audio/Plug-Ins/Components`, so running it means installing over whatever is there. The one `scan-time` warning comes and goes with the page cache (below). All by hand on this machine; CI runs none of them. |
| CI | `.github/workflows/build-plugin.yml`. **Green on 08.08.2026** — ten jobs (gates, five test legs, four builds) over three platforms, run `31287823776`, which is also what verifies the `LE_` macro strip on MSVC. Windows Debug is the sixth test leg and is excluded; `tech_debt.md` says why. |
| Warnings | **Two**, both deliberate `#pragma message` build banners. Our own sources compile under `-Wall -Wextra -Werror` on Apple Clang, GCC 12.4 and GCC 11 — CI passes `-DSW_WERROR=ON` to every leg. MSVC gets nothing and compiles warning-blind — `tech_debt.md`. |
| Sanitizers | **tsan, ASan and rtsan all clean over `[threading]` as of 08.08.2026**, with one expected rtsan report: `ModuleFactory::create` allocating inside `process()`, which is the concession [`tech_debt.md`](tech_debt.md) records. Each of those three instruments pinned a different Tier 2 fix by reversion — see the table at the end of [`old/fable_review.md`](old/fable_review.md). The full tsan sweep of both binaries was clean on 06.08.2026 and has not been re-run since; note that the rtsan recipe now needs `-D CMAKE_OSX_DEPLOYMENT_TARGET=14.0` for that tree only, Homebrew's libc++ 22 having dropped 10.15 ([`threading_model.md`](threading_model.md) §8). |

---

## There is no ordered item left

Shipping was the last one and it is a decision now rather than a task: the
installers build and are signed, notarised and stapled on every push to `main`,
the `.pkg` has been installed on a machine that did not build it, the licence is
settled and shipped, and every bundle carries an identifier under
`org.surge-synth-team.spectrumworx`. The user manual — the Word document and the
PDF in [`doc/manual`](../manual) — is updated when there is a release to
describe and does not gate one.

What is below is the smaller work, in no order.

---

## Smaller work, not in the order

None of these blocks anything, and most are under a day.

### Let a user type a value into a knob

The plugin can read its own display back now: `DisplayValueTransformer` has an
`inverse` beside its `transform`, `Parameters::parse` mirrors
`Parameters::print` tag for tag, and `SpectrumWorxCLAP::paramsTextToValue`
answers. `parameter_system.md` §8 is the mechanism, and
`tests/clap/parameterTextTests.cpp` holds every parameter of every one of the 57
effects to the round trip.

So a **host's** generic panel can be typed into. The plugin's own editor still
cannot: right-click a knob and type a value, which is what the whole inverse was
wanted for. That is `EditorKnob` and the module parameter widgets, and it needs
nothing new underneath — `Plugin2HostPassiveInteropImpl::getParameterFromDisplay`
is the call, and it takes the `Program` the caller owns.

Worth doing beside it: the knob's own value display already goes through
`Parameters::print`, so typing and showing would finally be the same pair of
functions rather than two spellings of the same table.

### Consider the cpputils ring buffer

`src/core/threading/spscQueue.hpp` is ours, hand written, and carries the
engine's two channels. `sst-cpputils` is already a dependency and ships a ring
buffer; if it fits, the queue that everything else in this design rests on stops
being code we maintain and start being code somebody else tests. Worth an hour
to compare the two interfaces before deciding — `threading_model.md` §3
describes what the channels actually need, which is less than a general queue
offers.

### Run the tests under rtsan again

`reset()` and `paramsFlush()` went into the realtime region on 03.08.2026 and
have not been under a realtime sanitizer since; neither has `runEngine()`'s
sample branch, which was guarded on side buffers `activate()` never asked for
until 05.08.2026, so twenty lines of per-block work — `sampleChunk()`, a wrapping
read, a copy into the side buffers — have never executed under one at all.
`sampleFeedTests.cpp` drives them. Expect at least the slot-selector allocation
`tech_debt.md` records. `threading_model.md` §8 has the configure line.

This outlived the DAW pass it was filed under: a tester exercising the plugin
confirms it works, which is not the same as confirming it allocates nothing on
the audio thread.

### `scan-time` is over the limit and unmeasured

`clap-cpp-validator` wants 100 ms and got 301, then 18, then 274 across three
runs of the same binary — dominated by whether the bundle and its dependencies
are in the page cache. Whatever happens at scan time is worth reducing, and the
measurement needs a cold-cache protocol before it can say so in either
direction.

### Collapse `vector.cpp`'s three interfaces to one

The `LE_` macro strip is finished: 184 names down to 83, around 8,700 lines
deleted over two branches, goldens bit-identical throughout, and verified on
every compiler in the matrix — the MSVC legs of run `31287823776` build and test
the whole strip at head, which is what the assertion handler's
`OutputDebugStringA` write and the six `float &LE_RESTRICT` sites were waiting
on. What survives is what C++20 genuinely cannot express: the assert family,
`LE_RESTRICT`, `LE_ASSUME`, the alloca buffers, and the effect and resource
X-macro tables.

One macro is left, and it is a refactor rather than a sweep.
`LE_MATH_NATIVE_POINTER_SIZE_INTERFACE`, 37 sites in `vector.cpp`, is not a dead
arm: the file publishes every primitive three ways — Span, `(begin, end)` and
`(pointer, count)` — and exactly one of the latter two can hold the
implementation while the other forwards, or they recurse. The macro picks which.
It is defined on Apple, where the vDSP and vvv calls are naturally
`(pointer, count)`, and undefined elsewhere, where the `(begin, end)` forms hold
the loops. Both arms have a live reason, which is why the NT2 strip did not
settle it.

**The decision is taken: collapse to Span plus one forwarding pair.** There is
one vectorised backend now, so the three-way interface is describing a choice
nobody makes any more. That takes about a third of the file's surface — it is
1,126 lines since the NT2 arm went — and the last live configuration macro in
the math layer with it. It is the vector math the whole engine runs on and the
goldens are the net, which is why it is its own piece of work and not a line in
a macro commit.

---

## One thing that is not a task

**Write down what the first five minutes of using SpectrumWorx should be**, and
check the plugin against it by hand: loading a preset, putting an effect in a
slot, turning a knob, saving, closing the editor, reopening it, saving the
session, reloading it. Week one's four bugs were all found that way and none of
them by a test, and the validators' four were found the same way by machines.

`doc/manual/SpectrumWorx test procedure.doc` is Little Endian's own version of
that list and has been sitting unread since stage 0.5 moved it. Converting it is
worth more now rather than less: there are testers to hand a checklist to.

Two things belong on it that nothing else will catch:

- **Change the FFT size from the host's own generic panel while audio runs**, and
  watch whether the change takes. That is the only way to see whether a real host
  honours `request_restart` — `threading_model.md` §5 explains why every spectral
  setup change rides on it, and the test hosts answer it as a no-op.
- **Load an external audio file and hear it.** It never worked in this port until
  05.08.2026, and no human has heard it since it was fixed.
