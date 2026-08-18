# SpectrumWorx — The side chain

What feeds the engine's second input, who decides it, and what an old patch means.

Written 18.08.2026. Everything here is in the tree and has tests naming it.

---

## 1. The insight this rests on

**Bus topology is not a user setting.** How many ports a plugin has, and how many
channels each carries, is a handshake between the plugin, the host and the track.
A control that claimed to decide it would be claiming something it does not get
to decide — and a control that *described* it would be describing something the
user cannot act on.

What the user actually decides is **what goes into the side channel**. There are
three answers and there is no fourth:

| | `SideChainSource` | fed from |
|---|---|---|
| a file | `File` | a decoded audio file, read forwards, wrapping at its end |
| yourself | `Main` | the main input, so an effect side-chains against itself |
| the host | `Host` | the host's second input port |

That is the whole model. `src/le/spectrumworx/sideChainSource.hpp` is where it
lives, and it is deliberately *not* a parameter — see §5.

## 2. The rule at render time

`SpectrumWorxCLAP::runEngine()`, three lines:

```
File, and a sample is loaded?     -> the sample's chunks
Host, and the port carries signal -> process->audio_inputs[ 1 ]
otherwise                         -> the main input
```

**Every fallback is the main input, never silence.** A Blender with nothing
patched blends the signal with itself; that is the documented behaviour and
`effect_contract.md` §1.8 is where an effect author meets it. An effect cannot
tell the three apart — `data.side()` is the same span whichever filled it — which
is what makes the selection a routing decision rather than a DSP one.

`Host` has three ways to carry no signal, and the third is the only one a real
DAW produces: no second port in the count, a second port with no `data32`, and a
second port whose channels the host declares constant and zero through
`clap_audio_buffer::constant_mask`. The mask is a *hint* and a host that sets none
is indistinguishable from one carrying real silence — issue #13 is still open on
whether any host sets one at all. **That unreliability is now confined to one
arm.** It decides between the port and the main input, and it can no longer
decide against a file the user loaded, which is what it did until 18.08.2026.

### Two states that cannot occur

- **`File` with no sample loaded.** Refused at both doors:
  `SpectrumWorxCLAP::setSideChainSource()` leaves `Main` selected instead, and
  `resolveSideChainSource()` does the same for a patch being loaded. So the
  selector never shows a file that is not there. `runEngine()` still tests
  `pSample_` rather than trusting it, because a source the engine cannot honour
  should fall back rather than dereference null.
- **A source and a sample the audio thread disagrees about.** They travel in one
  `ToEngine::SwapSample` message, so no block can be rendered with a new source
  and the sample the previous one named. Its `replacesSample` flag separates
  "here is a new sample, or none" from "only the source has changed" — the second
  is what restores a patch that names a file, where the sample has just been
  published and must not be cleared by the source arriving behind it.
- **A file loaded and unheard.** Selecting `Main` or `Host` *discards* it.
  Keeping it was tried and reverted on 18.08.2026: it left the box's three
  answers hiding a fourth piece of state, so a patch could carry audio nothing
  would ever play and `stateSave` would write a `Sample=` its own source
  contradicted. The cost is a second decode when a user switches back, and it is
  the right one — the alternative is a plugin that quietly remembers.

## 3. Selecting it

The box under the LFO panel, which used to be labelled `External audio`. It is
**always populated**: "nothing" is not one of the three answers.

```
Main as sidechain
Host sidechain
── Audio file ──
Load audio file...
<the seventeen factory samples>
```

`No external audio` is gone. It named the *absence* of one source rather than the
presence of another, which is why a user who cleared a file had no way to say
what they wanted instead — and it was disabled in exactly the state a user most
wanted to read it. The right mouse button, which used to clear the file, selects
`Main`.

Loading a file *is* selecting it as the source, and selecting either of the other
two discards it. There is no arrangement in which a file is loaded and silently
unheard, in either direction.

## 4. What an old patch means

No 2.x file records a source. All 288 shipped presets record an `Input_mode`,
written by a 2016 parameter that was compiled out behind
`LE_SW_ENGINE_INPUT_MODE`, never present in any build this port produced, and
deleted on 04.08.2026 — so for a fortnight the plugin was discarding the only
thing the format had ever said about a patch's side chain.

`Input_mode` was a **bus** setting: `(Stereo)(StereoSideChain)(Mono)(MonoSideChain)`,
driving `ioChannels()` and nothing else. Crossed with "is a file loaded" it
produced this, which is what the 2016 plugin did:

```
audio file loaded
   2x2 -> the file
   4x2 -> the file
no audio file loaded
   2x2 -> the main input
   4x2 -> the host's side chain
```

Read as a bus setting that table has a corner nobody can explain. Read as
*sources* it has none: the file wins because the file **is** the source, and the
2x2/no-file corner is `Main` under another name. So the migration is exact —
`resolveSideChainSource()`:

```
the patch records a source?  -> that, unless it is File with no sample
a sample was applied?        -> File
Input_mode odd (1 or 3)      -> Host
otherwise (0, 2, or absent)  -> Main
```

Three things worth knowing about it:

- **"A sample was applied", not "the patch names one".** A patch loaded with the
  preset browser's `Ignore external audio` on names a file and gets none, so it
  migrates to `Input_mode`'s answer instead — which is what a user who asked not
  to be given somebody else's audio meant. A named file that will not decode is
  reported and cleared, and lands in the same place.
- **Mono is not a source.** `Input_mode` 2 and 3 are the mono arrangements, and
  they say how many channels each source carries rather than which one is
  selected — hence the odd/even test. Mono itself is issue #114.
- **`Input_mode` is read and never written.** It is migration input, not a
  parameter, and a file that omits it is not missing anything.

`presetCorpusTests.cpp`'s `[side-chain]` case walks all 288 shipped presets and
holds the outcome: the `Sidechainables` bank is exactly the set that comes back
`Host`, and it is the whole bank.

## 5. Why it is not a parameter

It streams beside `<p n="Sample">` in the `<Global>` block, as
`<p n="Side chain source" v="host"/>`, and is handled by the same special-cased
path rather than by the parameter machinery. Three reasons, in order of weight:

1. **It is the file selector's value.** "Load this file" and "take the host's
   port" are one act and one control. A parameter and a file path that had to be
   kept consistent would be two answers to one question.
2. **Atomicity.** Publishing it with the decoded sample is one message; two
   independent publications would leave a window in which the audio thread had a
   new source and the previous sample.
3. It does not need automating, and a host automating "which file" is not a thing
   it can do anyway.

**Streamed by name — `file`, `main`, `host` — not by ordinal.** A fourth value
appended later then cannot change what an existing file means. Every *parameter*
in this tree streams as a number, so this is the exception and it is a deliberate
one; the preferences file already follows the same rule for its two enumerations
(`streaming_format.md` §4.4).

## 6. What is not decided here

**The port layout.** The plugin declares two stereo input ports and one stereo
output port unconditionally, in every source, and `activate()` asks the engine for
`setNumberOfChannels(4, 2)` regardless. A host therefore shows four inputs
whatever the patch selected. That is the handshake, not a setting, and issue #114
is where it changes — for mono, which is the case where it genuinely has to.

One consequence worth stating, since it is a cost rather than a nicety: the
engine analyses the side channel every hop even when it is a byte-for-byte copy of
the main input. `ProcessParameters::haveSideChannel()` is "the pointer is
non-null" and the fallback pointer never is, so `Main` pays for a redundant
transform. Only a build whose engine channel count follows the layout could skip
it — issue #114 again, and it is not free: with no side channel at all the side
spectrum is never filled, so a Blender would go *silent* rather than blend with
itself.

## 7. What guards it

| | holds |
|---|---|
| `tests/external_audio/sampleFeedTests.cpp` | each of the three reaches the DSP, and **only** the selected one does — three `==` pairs with a file loaded and a port patched at once. Plus the two fallbacks, and that `File` with no file is refused rather than stored |
| `tests/clap/pluginTests.cpp` | a patched, signal-carrying port is bit-identical to no port at all under `Main`; an untouched patch reads it, which is the default's audible consequence |
| `tests/presets/presetCorpusTests.cpp` | the migration, over all 288 shipped presets |
| `tests/presets/presetRoundTripTests.cpp` | the 3.0 fixture, built so that only one reading passes: it records `main` (not the default) *and* `Input mode="1"` (which migrates to `host`), so `main` is reachable only by reading the recorded source and letting it win |
| `tests/gui/sideChainSelectorTests.cpp` | the box draws the selection and the editor asks its host for it |
| `tests/presets/data/presetFixtures.txt` | the source is inside the hashed dump, so a shipped preset whose `Input_mode` stopped being read moves its row |
