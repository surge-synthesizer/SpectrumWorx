# SpectrumWorx — How an LFO's rate works

What `PeriodScale` holds, what `SyncTypes` changes about it, and which of the two
clocks an LFO reads. Companion to
[`parameter_system.md`](parameter_system.md), which is how the parameter is
addressed, and [`streaming_format.md`](streaming_format.md), which is what
reaches a file.

Written 06.08.2026, when the answer changed. Everything here is in the tree and
has cases naming it — `tests/parameters/lfoTests.cpp`.

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
not touch, so that arrives on the main thread as `ToUI::TimingChanged` and lands
in `SpectrumWorxEditor::updateForNewTimingInfo()`. See
[`threading_model.md`](threading_model.md) §3, which is also where the reason
that one message is coalesced by its sender is.

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

## 3. The two clocks

`Timer` tracks one position, in the host's bars. The reference-bar position is
derived (`Timer::currentTimeInReferenceBars()`), and `LFOImpl::getValue()` picks
the pair by sync mode. There is no phase accumulator: position is recomputed
absolutely every block as `frac( (phase·T + t) / T )`, with `t` and `T` in the
same units, which is why an LFO follows the playhead through a locate.

The LFO is evaluated **once per host block** — `Processor::preProcess()` — so it
is a block-rate modulator, not a per-sample one.

### Why the derived clock is a rewrite and not a change

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

## 4. What reaches a file

The `T` attribute is **milliseconds for a free LFO and bars for a synced one**,
and has been since 2011 — `adjustValueForPreset` / `adjustValueFromPreset`. The
grammar is unchanged and 2.x files load as they always did.

What changed is that the conversion uses the reference bar rather than
`Timer::basePeriod()`. It used to read the process-global bar duration, so the
same preset loaded at 140 BPM produced a different `PeriodScale` than at 120, and
the same session saved at two tempi wrote two different files. That is what made
`[preset-corpus]` fail about one run in three when the whole suite ran in one
process — 153 of the 303 rows — and it is why the suite was split into two
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

`SyncTypes` and `Waveform` are **not** exported —
`ParameterCounts::lfoExportedParameters` is 5, and the exported set is the first
five in declaration order. They reach the engine as
`ToEngine::SetUnexportedLFOParameter`, addressed by
`(moduleIndex, moduleParameterIndex, lfoParameterIndex)` because they have no
`ParameterID` to be addressed by. **Everything the panel edits has to go through
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
4. `tests/parameters/lfoTests.cpp` — the waveform table, the snapping, the clock
   and the four meters
5. `tests/clap/pluginTests.cpp`, `[clap][lfo]` — the same clock with the meter
   arriving as a `clap_event_transport`
6. `tests/gui/lfoDisplayTests.cpp` — that an edit in the panel reaches the engine
