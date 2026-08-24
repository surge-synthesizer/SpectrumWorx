# SpectrumWorx — How an LFO's rate and evaluation work

What `PeriodScale` holds, what `SyncTypes` changes about it, which of the two
clocks an LFO reads, and — §3 — **how often it is asked and how often the clock
moves, which are two different rates set by two different things**. Companion to
[`parameter_system.md`](parameter_system.md), which is how the parameter is
addressed, and [`streaming_format.md`](streaming_format.md), which is what
reaches a file.

Written 06.08.2026, when the rate answer changed; §3 rewritten 20.08.2026 with
issues #78 and #151, which were both that second question being answered by
accident. Everything here is in the tree and has cases naming it —
`tests/parameters/lfoTests.cpp` and `tests/clap/pluginTests.cpp` `[clap][lfo]`.

---

## 1. The rule

> **One number, one unit, one range. `SyncTypes` changes only which bar the
> number is a fraction of.**

Every LFO has a `PeriodScale` — a float, host-visible as `M<n>.<param>.LFO.T` —
and it always means *the period as a multiple of a bar*. What `SyncTypes`
decides is **whose bar**:

| `SyncTypes` | period is a fraction of | so a tempo change | and the value snaps |
|---|---|---|---|
| `Free` (0) | the **reference** bar: 120 BPM in four four, two seconds, a constant | changes nothing — the LFO keeps its rate | no, it is clamped to the range |
| anything else | the **host's** bar | changes the rate, which is the whole point of syncing | yes, to the beat divisions the mask allows |

The consequence worth stating on its own, because it is what the design is for:
**nothing the host or the user did not do moves the number.** A tempo change
alters what a synced LFO sounds like and leaves its parameter alone; it alters
neither for a free one.

What it does move is what the LFO panel should be *showing*: the same period is a
different length of time at the new tempo, and it snaps to a different grid. The
audio thread is where a tempo change is seen and a widget is the one thing it may
not touch, so that arrives on the main thread as a flag the engine raises and the
drain clears, and lands in `SpectrumWorxEditor::updateForNewTimingInfo()`. See
[`threading_model.md`](threading_model.md) §3, which is where the reason the news
travels on a flag rather than on the ring is.

`Timer::referenceBarDuration` and `Timer::referenceMeasureNumerator`
(`le/parameters/lfoImpl.hpp`) are that constant bar. It is the same 120 BPM 4/4
the engine already assumes when a host reports no transport, so a plugin in a
host with no tempo and a free LFO in a host with one are running off the same
clock, and always were.

## 2. `SyncTypes` is a bit mask

`LFO::SyncType` (`le/parameters/lfo.hpp`) is not an ordinal:

```cpp
Free = 0, Quarter = 1 << 0, Triplet = 1 << 1, Dotted = 1 << 2,
All = (Quarter | Triplet | Dotted)
```

It is the set of grids the period may snap to, and `Free` is none of them.

**The panel offers one of them at a time.** N, T and D were three independent
toggles over the mask until 18.08.2026, which is what issue #111 was: with more
than one lit, `snapSyncedPeriod()` returns whichever of the enabled grids lands
nearest to the current period, and that is the quarter grid nearly everywhere —
so T and D read as buttons that do nothing. Selecting a grid now clears the other
two, and clicking the lit one is still how `Free` is reached.

The *parameter* is still a mask, because files hold masks and the grammar is not
worth breaking over this: `Gamma Shift/Clutter Dropout` carries `sync="5"`
(`Quarter|Dotted`), `Overt Dynamics/Toybox Demons` carries `sync="7"` (`All`),
both load and snap exactly as they always did, and the panel lights both of the
first one's grids. What has gone is the panel's ability to *make* such a value.

`snapPeriodScale( value, mask )` returns the snapped value **and which grid won**
(`SnappedPeriod`), because that is what the panel labels the reading with.
`snapSyncedPeriodScale()` divides by the host's measure numerator throughout, so
the divisions available are the ones the meter actually has: in three four one
beat is a third of a bar and there is no quarter-of-a-bar to snap to.

**The mask decides the grid; it does not decide the range.** `PeriodScale`'s
bounds are constants — `(2/3)/8/4` to `1.5 × 16`, about 0.0208 to 24 reference
bars, or 42 ms to 48 s. They used to divide by the *host's* numerator, which made
the parameter's minimum a function of the time signature.

## 3. The two clocks, and the two rates

### 3.1 The clocks

`Timer` tracks one position, in the host's bars. The reference-bar position is
derived (`Timer::currentTimeInReferenceBars()`), and `LFOImpl::getValue()` picks
the pair by sync mode. There is no phase accumulator: position is recomputed
absolutely every evaluation as `frac( (phase·T + t) / T )`, with `t` and `T` in
the same units, which is why an LFO follows the playhead through a locate.

#### Why the derived clock is a rewrite and not a change

A free LFO used to be kept honest by rescaling its *period* whenever the bar
duration changed, so that `periodScale × barDuration` stayed constant. Its phase
was therefore

```
  frac( (offset + timeInBars) / periodScale )
= frac( (offset + timeInBars) × barDuration / periodInSeconds )
```

— which is exactly the reference-bar clock divided by a period expressed in
reference bars. Same phase, same locate behaviour, same output. What stops moving
is the number the host and the file hold. The golden fixtures render
bit-identically across the change, which is the measured form of that argument.

### 3.2 The call stack, and the two rates in it

An LFO is not evaluated once per host block, and the clock does not move once per
host block either. **They are two separate cadences, set by two different
numbers**, and every LFO bug the plugin has had since the port has been about the
gap between them.

```
SpectrumWorxCLAP::process(clap_process*)                   ── once per host block
└─ for (cursor = 0; cursor < frames_count; cursor += hop)  ── hop = fftSize / overlap
   ├─ applyEventsDueAt(cursor)
   ├─ updateLFOTiming(process, cursor, piece) ───────────► ■ CLOCK TICK, per piece
   │     Timer::updatePositionAndTimingInformation(...)
   │       playing on a beats timeline: song_pos_beats + cursor/sampleRate
   │       otherwise:                   carry on by `piece` samples
   └─ runEngine(process, cursor, piece)
      └─ SpectrumWorxCore::process()
         └─ Engine::Processor::process()
            ├─ preProcessedThisCall_ = false
            └─ do { processSingleChannel() } while (next channel)
               └─ while (inputSamples)
                  ├─ FIFO += samples            (tops up to windowSize)
                  └─ if (FIFO == windowSize)    ── only when a frame is complete
                     ├─ preProcessForFirstFrame() ────────► ● LFO EVALUATION
                     │    └─ preProcessAll(lfoTimer())
                     │       └─ ModuleDSP::preProcess()
                     │          └─ update{Base,Effect}ParametersFromLFOs(timer)
                     │             └─ LFOImpl::getValue(timer)
                     ├─ ... FFT / effects / IFFT ...
                     └─ moveForwardByHopSize(stepSize)
```

- **■ the clock** ticks once per *engine chunk*. A chunk is `engineChunkSize()`,
  which is `Setup::stepSize()` — the hop — or the whole block when the block is
  shorter than one hop.
- **● an evaluation** happens once per *spectral frame*, which is once per hop of
  input **consumed**. The FIFO is pre-filled to `windowSize − stepSize`
  (`Processor::reset`), so this is exact from the first block with no warm-up
  irregularity.

`preProcessedThisCall_` is why an evaluation is once per frame rather than once
per channel: the channel loop is outside the frame loop, and without the flag
channel 0's first frame would consume a `TriggerParameter` that channel 1 then
never saw.

The clock used to tick once per host **block**, by the whole `frames_count`. That
was issue #78: an LFO's resolution was the buffer size the user had picked
somewhere else — 2.7 ms at 128 and 85 ms at 4096 — so the same project sounded
different at two settings with no automation anywhere. Moving the tick inside the
chunk loop is what fixed it, and the transport arm has to take `cursor` with it,
`song_pos_beats` being the position of the block rather than of the piece.

### 3.3 What the FFT size and the buffer size do to the ratio

The hop is `fftSize / overlapFactor` and both are user-facing parameters, so the
ratio between the two cadences is chosen by the user twice over — once in the
host's audio settings and once on the plugin's own front panel. **Clock ticks per
LFO evaluation**, as the plugin stands today:

| host buffer | 512/4 (hop 128) | 2048/4 (hop 512, default) | 8192/4 (hop 2048) |
|---|---|---|---|
| 64 | 2 | 8 | 32 |
| 128 | 1 | 4 | 16 |
| 256 | 1 | 2 | 8 |
| 512 | 1 | 1 | 4 |
| 1024 | 1 | 1 | 2 |
| 2048 | 1 | 1 | 1 |

Above the hop the chunk loop cuts the block into hop-sized pieces, so a tick and
an evaluation come in pairs and the ratio is one. Below it the loop degenerates
to a single chunk of `frames_count`, several blocks go by before the FIFO
completes a frame, and the clock ticks several times per evaluation. A buffer
that is not a whole multiple of the hop is ragged rather than clean: the short
final chunk ticks the clock and completes no frame.

**Nothing in the plugin can make that column read 1 everywhere**, because a host
that hands over 64 samples at a time cannot be given a 2048-sample hop's worth of
LFO motion per block. That is the point of §3.4: the ratio is allowed to be
anything, and nothing may depend on it.

### 3.4 A period beginning is a fact about the LFO, not about the clock

Four waveforms — `Dirac`, `dIRAC`, `RandomHold`, `RandomSlide` — do all their
work at a period boundary, and `newPeriodBegun` is what tells them one has
arrived. It used to be derived from the clock:

```cpp
previousPeriodPosition   = modulo(periodOffset + previousTime, periodScale)
periodEndForPreviousTime = previousTime + (periodScale - previousPeriodPosition)
newPeriod                = currentTime > periodEndForPreviousTime
```

— *did the clock cross a boundary between its own previous tick and this one*.
Which is a question about the interval in the table above, and that interval is
neither the time since this LFO was last evaluated nor anything with a fixed
relationship to it. Issue #151, in both directions:

- **Ratio above one** (buffer below the hop). Boundaries falling in a tick that
  no evaluation looked at were never noticed. The position wrapped from one back
  to zero with the ramp's coefficients untouched, so a Sample & Glide played the
  *identical* glide again — "getting caught in a loop and parts being repeated an
  unpredictable amount of times", the reporter's words.
- **Ratio below one** — which the old per-block clock also produced, several
  frames sharing one tick. Every one of those frames answered "yes" to the same
  question, so the waveform drew a target per frame and rendered all but the last
  one frame apart: "straight up jumps happening periodically".

The rule now is that a period beginning is decided by **which period this
evaluation is in, against which period the last one was in**:

```cpp
auto const [periodIndex, positionInPeriod](
    Math::splitFloat((periodOffset + currentTime) / periodScale));
bool const newPeriod(state_.neverEvaluated || (periodIndex != state_.periodIndex));
```

`splitFloat` was already computing that integer and throwing it away. The rule is
true exactly once per period at any ratio: evaluations sharing a clock position
share an index, and a clock that jumps several periods still only begins the one
it lands in. It also survives a locate, a tempo change and a period edit, none of
which the clock-delta form did, and it took the last reader of
`Timer::previousTimeInBars()` with it.

`state_.neverEvaluated` is a flag rather than a sentinel index because
`periodOffset` is signed in a release build — `phase()` is ±0.5 and only the
debug branch takes `abs` — so index −1 is reachable and no value is out of band.
**The first evaluation announces a period**, deliberately: an LFO nothing has
asked yet is at the start of one whatever the clock reads. Before this, the whole
first period of all four waveforms was their constructed state — Dirac never
pulsed, RandomHold held a zero it had not drawn, and RandomSlide sat flat at zero
instead of gliding. `tests/parameters/data/lfoWaveforms.txt` records the change:
four rows moved and the other seven are byte-identical, which is the blast radius
stated as a fixture.

#### Why #78 was not a fix for #151

Because it changes the clock's granularity and the trigger's problem was that it
depended on the granularity at all. Measured, retriggers over 60 s of a 1000 ms
LFO where 60 is correct, **with #78 applied and #151 not**:

| fft/overlap | hop | 64 | 128 | 256 | 512 | 1024 | 2048 |
|---|---|---|---|---|---|---|---|
| 512/4 | 128 | 44 | 60 | 60 | 60 | 59 | 59 |
| 1024/4 | 256 | 23 | 29 | 59 | 59 | 59 | 59 |
| 2048/4 | 512 | 11 | 16 | 29 | 59 | 59 | 59 |
| 2048/2 | 1024 | 5 | 7 | 16 | 30 | 59 | 59 |

Below the hop, #78 is a literal no-op — `chunk` is `min(hop, frames_count)`, so
"advance by the chunk" *is* "advance by the block", the same value on the same
line. At the default FFT size that is every common buffer setting. The
period-index rule reads 60 in every cell of that grid, with or without #78.

#### Why the suite could not see any of it

Every case in `lfoTests.cpp` drove the timer exactly once per `getValue()`, and
every `[clap][lfo]` case ran at a 512 block — which at the default 2048/4 setup
*is* the hop. One ratio, and the one ratio at which neither bug exists. The two
cases that close the gap are `driven()` in `lfoTests.cpp`, which takes
`clockTicksPerEvaluation` and `evaluationsPerTick` as arguments and so can ask
for a ratio rather than inheriting one, and `An LFO moves inside a block, so the
buffer size does not change its sweep` in `pluginTests.cpp`, which renders one
span as a single 4096 block and as eight 512 ones.

### 3.5 What an LFO is, then

A **frame-rate** modulator: it steps once per hop, which is 2.7 ms at 512/4 and
10.7 ms at the default 2048/4, and it is not a per-sample one. Its *phase* is
sampled from a clock that moves at up to the same rate and never faster, and its
*period boundaries* are counted from its own position and so are exact whatever
either rate happens to be.

## 4. What reaches a file

The `T` attribute is **milliseconds for a free LFO and bars for a synced one**,
and has been since 2011 — `adjustValueForPreset` / `adjustValueFromPreset`. The
grammar is unchanged and 2.x files load as they always did.

What changed is that the conversion uses the reference bar rather than
`Timer::basePeriod()`. It used to read the process-global bar duration, so the
same preset loaded at 140 BPM produced a different `PeriodScale` than at 120, and
the same session saved at two tempi wrote two different files. That is what made
`[preset-corpus]` fail about one run in three when the whole suite ran in one
process — a good half of the rows — and it is why the suite was split into two
binaries. **Splitting the binaries fixed nothing; the constant is the fix.**

The *meter* is a different matter and is not fixed, because it is not broken: a
synced period snaps to the divisions the meter has, so the same preset genuinely
loads to a different period in three four. `snapSyncedPeriodScale()` reads
`Timer::measureNumerator()` and that is still a process-global static, which
makes it a hazard for a test binary in the way the tempo used to be. Every case
that drives another meter therefore puts 4/4 back on the way out — the
`ScopedHostTiming` guard in `lfoTests.cpp`, and a block with no transport at all
in the two CLAP cases.

The meters those cases drive are 3/4, 6/8 and 5/4, which is issue #14. What each
of them can hold falls out of "a whole number of beats that divides the bar":

| meter | synced periods, in bars |
|---|---|
| 4/4 | ¼, ½, 1 |
| 3/4 | ⅓, 1 |
| 6/8 | ⅙, ⅓, ½, 1 |
| 5/4 | ⅕, 1 |

Five is prime, so 5/4 has a beat and a bar and nothing between them. And a bar of
3/4 and a bar of 6/8 are both six eighth notes, but only one of them can hold a
half-bar period — which is the shortest statement of why the numerator is not a
detail.

**The denominator reaches nothing.** `updateLFOTiming()` builds the bar out of
`tsig_num` beats of `60 / tempo` seconds, and CLAP's tempo is quarter notes per
minute, so a 6/8 bar is three seconds at 120 BPM where a musician counting it
would say a second and a half. The *ratios* a 6/8 LFO snaps to are right; the bar
they are ratios of is twice as long as the written one. Nothing depends on
changing it and nothing has asked, so it is written down rather than fixed.

Two more things the format does that are easy to trip over:

- **`sync` is the only LFO attribute written unconditionally.** Every other
  sub-parameter is omitted when equal to its default, on a 2011 note about a
  4096-byte preset limit. So a wrong sync default shows up on all 225 LFO
  parameters at once rather than on none — which is how
  `clap-cpp-validator` found the one below.
- **The LFO attributes load in reverse declaration order**, so that `SyncTypes`
  is set before `PeriodScale`: the period's conversion branches on the mask, so
  the mask has to be there first. `LE_DEFINE_PARAMETERS`' order is load-bearing
  for the file format, not only for the id space.

## 5. What the host sees

Module and LFO parameters cross the CLAP edge **normalised to 0..1**
(`core/host_interop/clapParameterEdge.hpp`), because `min_value`, `max_value` and
`is_stepped` are in CLAP's `RESCAN_ALL` set — deactivated-only — and a slot's
effect changes mid-block. `clap_param_info` therefore reports a literal 0 and 1
for `LFO.T`, for the plugin's lifetime.

The range it is normalised *against* is `PeriodScale`'s, and that is why §2's
last paragraph matters: while those bounds read the host's time signature, the
meaning of the host's 0..1 moved when the meter did, and so did
`clap_param_info.default_value`. CLAP has no rescan flag that means "the default
changed", so there was no way to tell a host about it either. Constant bounds
make the question go away rather than answer it.

### What a host reads, as against what it writes

The number is a multiple of a bar; the *reading* is not, and until 21.08.2026 the
host was handed the number. `value_to_text` now answers what the panel has drawn
since 2011 (issue #158):

| parameter | reads as | example |
|---|---|---|
| `LFO.T`, synced | the note value the period is snapped to, labelled with its grid | `1/4 bars`, `1/8T bars`, `2/1 bars` |
| `LFO.T`, free | milliseconds against the **reference** bar, so a tempo change does not move it | `2000.0 ms` |
| `LFO.ph` | a percentage of a period, ±50 | `-12.5 %` |

`LFOImpl::print{,Synced}PeriodScale()` and `LFOImpl::parsePeriodScale()` are the
pair, and they live beside the parameter rather than in the panel that draws them
— the panel is JUCE and `plugin2Host.cpp` may not link it, and two spellings of
one format is how a DAW's automation lane and the plugin's own strip come to
disagree. `periodRatioString()` in the editor calls the same function.

`1/8T bars` is text no general-purpose parser reads — `strtof` stops at the slash
— so `text_to_value` has an arm of its own for it, and what it answers is snapped
to the grids the mask allows. A user who types a note value the meter cannot hold
gets the nearest one it can, which is what a drag on the panel does.

**The skew is undone on the way in.** `PeriodScale` crosses the normalised edge
*linearised* (`LFOParameterGetter`, so that a bar sits in the middle of a host's
fader), and the generic printer's normalised arm did not know that — a *supplied*
value printed through it named a different period from the one the same number
would have set. `unlinearisePeriodScale()` in the printer's own arm is that fixed;
`tests/clap/parameterTextTests.cpp` holds the round trip.

The phase's percentage is a `DisplayValueTransformer`, which is where a display
unit belongs. `ValuesDenominator<100>` on the declaration says the same thing to
nobody: nothing in the printer reads that trait.

`SyncTypes` and `Waveform` are exported like the other five, and have been since
22.08.2026 (issue #159): `ParameterCounts::lfoExportedParameters` is 7. They were
the last two in declaration order and the first five were the exported set, so
they reached the engine as `ToEngine::SetUnexportedLFOParameter` — addressed by
`(moduleIndex, moduleParameterIndex, lfoParameterIndex)` because they had no
`ParameterID` to be addressed by. That message is gone with the exception.
**Everything the panel edits has to go through
`updateParameterAndNotifyHost<>`**: the N/T/D buttons wrote the LFO directly
until 06.08.2026, and since the editor is bound to `programMain_` that meant a
sync-mode change moved the display and the saved state and nothing the audio
thread could hear. `tests/gui/lfoDisplayTests.cpp` is what stops that returning.

## 6. A parameter's default is a property of the parameter

`SyncTypes` defaults to `Quarter`, full stop.

It was `hasTempoInformation() ? Quarter : Free` from 2011 until 06.08.2026 — a
default that read a process-global, sticky flag, so the answer depended on when
you asked and on what some other instance had been told. It needs two
constructions at two moments to be visible, which is why it took a validator to
find:

- The engine's module is built inside `process()` when the slot event is handled
  (`spectrumWorxCLAP.cpp`, the event loop), and `updateLFOTiming()` runs *after*
  it in the same block.
- The main thread's is built when the `ToUI` echo is drained, arbitrarily later.
- `stateSave` reads the main thread's.

So `clap-cpp-validator`'s `state-reproducibility-flush` — which never activates
its first instance and drives its second through `process()` with a transport —
got `sync="0"` from one and `sync="1"` from the other, on all 225 of them: same
length, 1782 bytes, and every byte of the difference in one attribute.

The flag is gone with its only reader. A host that reports no tempo gets 120 BPM
in four four, which is an answer rather than an absence, and the panel stopped
asking the question in August 2026.

## 7. What is still true and unfixed

`Timer`'s bar duration and measure numerator are **process-wide statics** —
`std::atomic`, so not a data race, but two tracks at two tempi still see one
tempo. Issue #11 has it. The parameter layer no longer reads them for
anything but the snap grid, which is what made the corpus digests order-dependent
and is the half that mattered most.

**A meter change resnaps one of the two Programs.**
`Processor::updateModuleLFOs()` walks the modules the audio thread owns;
`programMain_` — what `paramsValue` and `stateSave` answer from, and what the LFO
panel draws — is not among them and nothing else resnaps it. So after a meter
change the engine runs a period the host cannot read and the host reads a period
the meter cannot hold: a session saved then stores the old number, and loading it
back is what finally reconciles them. Measured by `A host that opens in five four
does not move the period it was given` (`tests/clap/pluginTests.cpp`), which pins
it as behaviour rather than endorsing it.

## 8. Reading order

1. `le/parameters/lfo.hpp` — the enums, and the SDK-facing interface
2. `le/parameters/lfoImpl.hpp` — the seven parameters, the traits and `Timer`
3. `le/parameters/lfoImpl.cpp` — `getValue()`, `snapSyncedPeriodScale()`, and the
   two preset conversions
4. `spectrumWorxCLAP.cpp` — `process()` and `updateLFOTiming()`, which are the
   chunk loop and the clock tick in §3.2
5. `le/spectrumworx/engine/processor.cpp` — `preProcessForFirstFrame()` and the
   frame loop, which is the other cadence
6. `tests/parameters/lfoTests.cpp` — the waveform table, the snapping, the clock,
   the four meters, and `driven()` for the two rates
7. `tests/clap/pluginTests.cpp`, `[clap][lfo]` — the same clock with the meter
   arriving as a `clap_event_transport`, and the sweep at two buffer sizes
8. `tests/gui/lfoDisplayTests.cpp` — that an edit in the panel reaches the engine
