# SpectrumWorx — The streaming format

What goes into a `.swp` file and into the session state a host hands back, how it
is keyed, and what may and may not be renamed. Companion to
[`parameter_system.md`](parameter_system.md), which is how a parameter is
addressed before it reaches a file.

Written 01–02.08.2026, as the work happened. Everything described here is in the
tree and has tests naming it.

---

## 1. The problem this starts from

A preset is keyed by *name*: modules by their effect's name, parameters by
theirs. That is a good design — it is why the effect list can be reordered
without touching a single file, and why a preset naming an effect this build does
not have degrades rather than corrupting the chain (pinned by "State naming an
effect this build does not have loads the rest").

It had one flaw, and it was invisible: **the name in the file was the name on the
knob.** `Parameters::Name<Parameter>::string_` fed the editor *and*
`RuntimeInformation::name`, and `RuntimeInformation::name` is what
`ModuleParameters::{save,load}PresetParameters` wrote and matched on. So renaming
a parameter re-keyed every file that had ever named it — and nothing said so. The
preset still loaded; that one parameter silently took its default. The same was
true of `Effect::title[]`, which is what a preset calls its modules.

The 2016 author knew, and left the warning in the one place it would be read
last — a comment beside the line, in `info()`:

```cpp
Parameters::Name<Parameter>::string_, //...mrmlj...parameter names are required for presets
```

---

## 2. The rule

> **Every string that reaches a file comes from a *streaming name*. Display names
> are free to change; streaming names are not.**

Two templates carry it, both defaulting to the display string:

| | display | streaming | default |
|---|---|---|---|
| parameter | `Parameters::Name<P>::string_` | `Parameters::streamingName<P>()` | the display name |
| effect | `Effects::effectName(i)` | `Effects::effectStreamingName(i)` | the title |

**The default is what makes this free.** Today's display names *are* the 2016
names — they are exactly the strings inside the 303 factory presets. Seeding the
streaming name from the display name means the pre-port name table exists,
complete and correct, at the point of definition, without a side table anyone has
to maintain or trust. That is only true once: every rename that happens before
the split is a silent break, and every rename after it is a decision.

### Pinning one

When a display name has to move, pin the streaming name to what files already
say:

```cpp
// in the effect's header, beside the parameter
EFFECT_PARAMETER_STREAMING_NAME(AhAh::Center, "Center (LFO me!)")

// in effectNames.cpp, above the table
LE_SW_EFFECT_STREAMING_NAME(SomeEffect, "What presets call it")
```

**A parameter pin goes in a header. This is not a style preference.**
`UI_NAME` defines an extern array — one definition, found by the linker, so a
`.cpp` is the right home. `STREAMING_NAME` specialises a class template, so every
translation unit that instantiates `Detail::info<>()` must *see* it. One that does
not gets the primary template, streams the parameter under its display name
again, and is an ODR violation besides. Written in the `.cpp` first; the snapshot
test below is what said so, immediately and by name.

### Pinned today

One, and it is the worked example rather than a real need:

| | display | streaming |
|---|---|---|
| `AhAh::Center` | "Center frequency" | "Center (LFO me!)" |

A 2011 instruction to the user wearing a parameter name. Its mangled form
`<Center_(LFO_me!)>` is also one of the element names `repairLegacyElementNames()`
exists to make parseable at all. Renaming it changed **one line** of
`parameterTable.txt` and nothing else — not `streamingNames.txt`, not
`presetCorpus.txt`, and no factory preset loads differently.

### Where the strings come from

`RuntimeInformation` (`le/parameters/runtimeInformation.hpp`) carries both, side
by side, filled by `Detail::info<Parameter>()`
(`le/spectrumworx/engine/moduleImpl.hpp`). Everything downstream reads the field
rather than the template, so the four serialisation sites are ordinary field
accesses:

- `ModuleParameters::{save,load}PresetParameters` — module parameters
- `ParametersLoader` / `ParametersSaver`'s `operator()` — globals
- `LFODataSaver` / `LFODataLoader` — the seven LFO sub-parameters
- `ParametersLoader::effectIndexFromMangledName` and
  `ParametersSaver::saveEffectModuleChain` — module elements

The LFO already worked this way and nobody had noticed: `on`, `T`, `ph`, `lbnd`,
`ubnd`, `sync`, `wfrm` (`lfoImpl.cpp:47-53`) are file keys with no resemblance to
a label. So are the globals — `In`, `Out`, `Mix`. The split was already there in
spirit for two of the four tuples; this finishes it.

---

## 3. What guards it

| | pins | moves when |
|---|---|---|
| `tests/parameters/data/streamingNames.txt` | every string that reaches a file — 57 effects, their parameters, the globals, the LFO | a key changes. **Always a break.** |
| `tests/parameters/data/parameterTable.txt` | display names, types, ranges, defaults, units, and the host-visible id space | a label changes, or a range does |
| `tests/presets/data/presetCorpus.txt` | what all 303 factory presets load into, by two routes: read directly, and read → rewritten as 3.0 → read again | a preset loads differently, or the translation into 3.0 loses something |
| `tests/presets/data/format3.swp` | the 3.0 grammar itself — hand written, read by a test that never runs the writer | the grammar moves. A rename applied to writer *and* reader passes every round-trip test and orphans every file already saved; this is what does not pass. |
| `tests/clap/stateTests.cpp` | `clap_plugin_state`: the round trip through a second instance, the bytes, the sample, and what a host may do to a stream | state stops being a preset, or stops surviving a truncated / mis-sized / hostile one |

`streamingNameTests.cpp` also asserts the mechanism itself rather than only its
output: no streaming name is null or empty (the default is a null sentinel
resolved in `streamingName()`, so a fallback that came undone would show up as a
preset written with no key on half its parameters), and every effect's streaming
name mangles and un-mangles back to its own index.

`presetCorpus.txt`'s digests are taken over a dump that now names parameters and
effects by their **streaming** names. That is deliberate: its contract is *"a row
that moves is a preset that loads differently"*, and a relabelled knob is exactly
a change that does not. Naming display strings there would have made every rename
a 303-row diff that says nothing.

### The order to trust

Run the snapshots under `ctest`. All three live in `sw-dsp-tests`, which as of
02.08.2026 is one of two test binaries.

**A leaked *tempo* no longer moves a row.** A case that established one used to
move 153 of the 303, because `adjustValueForPreset` converted a free LFO's period
through `LFO::Timer`'s process-global bar duration; the binary split hid that and
fixed nothing. The conversion reads a constant reference bar since 06.08.2026 —
[`how-lfo-rates-work.md`](how-lfo-rates-work.md) §4.

**A leaked *meter* still would**, and that one is by design rather than by
accident: a synced period snaps to the divisions the meter has, so loading the
same preset in three four genuinely produces a different period.
`snapSyncedPeriodScale()` reads `Timer::measureNumerator()`, which is still a
process-global static. Nothing in either binary drives a meter other than 4/4
outside the scope guard in `lfoTests.cpp`, so this is a hazard rather than a
symptom — see issue #14.

---

## 4. The format

Built 02.08.2026, on top of §1 — stable keys were the precondition. §4.4's
payload is empty by design; everything else is load-bearing.

### 4.1 Version, and why not `Version`

A new integer attribute, `Format`, distinct from `Version`. Absent means the
legacy grammar; greater than the current value is refused with a
`PresetProblem::FutureFormat` rather than a parse error, so "saved by a newer
SpectrumWorx" reads as itself.

`Version` cannot be the stamp because it is the **product** version
(`presets.cpp:74-91`). The corpus carries 2.6 ×269, 2.7 ×20, 2.8 ×11, 2.9 ×1,
2.93 ×2 — it tracked the format only because product and format moved together in
2011. The tree is at 3.0.0, so this build already writes `Version="3.0"` onto
2.6-shaped files. The reader that used to consult it, a pre-2.7 check gating a
default for the window-presum parameter, read that product version as a format
version; both went with the parameter on 07.08.2026.

### 4.2 The 3.0 grammar

```xml
<SpectrumWorxPreset Format="3" Version="3.0" LastModified="02.08.2026 12:00" Comment="">
	<Global>
		<p n="In" v="1" />
		<p n="FFT size" v="4096" />
		<p n="Sample" v="Carrier.mp3" />
	</Global>
	<Modules>
		<Module effect="Ah-ah">
			<p n="Bypass" v="0" />
			<p n="Center (LFO me!)" v="2000" on="1" T="500" ph="0.25" sync="0" wfrm="0" />
		</Module>
	</Modules>
	<dawExtraState />
</SpectrumWorxPreset>
```

The external sample is a `<p>` like everything else rather than an attribute on
`<Global>`, which is where 2.x kept it and where the first sketch of this put it.
It is not a parameter, but it is a global scalar keyed by name, and giving it the
same shape means the reader needs no special case for it: `getSampleFileName()`
goes through the same lookup as `In` and `FFT size`.

Against 2.x:

| 2.x | 3.0 | why |
|---|---|---|
| the element name *is* the mangled key — `<Start_frequency>`, `<1>`…`<12>`, `<Center_(LFO_me!)>` | the key is an attribute *value* — `<p n="Center (LFO me!)"/>` | anything is legal in an attribute value. 25 of the 303 factory files do not parse without `repairLegacyElementNames()`; 3.0 cannot produce a file that needs it |
| an attribute on `<Global>`, element *text* on a module parameter | always `v="…"` | one lookup, not two |
| the module element is the mangled effect title | `<Module effect="Ah-ah">` | resolved by `effectIndexFromStreamingName()` rather than 57 mangled string compares |
| `Bypass` an attribute on the module element | `<p n="Bypass" v="0"/>` | uniform |
| four decimal places (`lexicalCast.cpp:63-66`) | shortest round-trip-exact text | 4 dp on a frequency in Hz is lossy |
| — | `<dawExtraState>`, in state and **never** in a `.swp` | §4.4 |

The LFO attributes keep their names and their place, on what is now the `<p>`
element, so `LFODataLoader`/`LFODataSaver` are untouched — the reversed-order
load, the `SyncTypes`-before-`PeriodScale` ordering and `adjustValueForPreset`
are the subtlest part of the format and this goes nowhere near them.

### 4.3 Two readers, one writer

One writer, so a `.swp` and a state blob are the same grammar. Two readers: the
2.x one, unchanged and kept forever, and the 3.0 one, chosen on `Format`.
`ParametersLoader` differs between them in four private members — where a
parameter's value lives, where its LFO attributes live, how the module elements
are walked, and how an element names its effect — so it takes an element-access
strategy rather than growing a second class.

The 303 factory presets **stay 2.x**. They are the only corpus of that grammar
and the only thing keeping its reader honest.

One thing the split touches that is not a grammar: `savePreset` and
`Preset::saveTo` return a `std::string`. They wrote into a caller's
`std::span<char>` and every shipping caller passed the same
`std::array<char, 4096>` — which five TuneWorx modules breach, as a 2011 note in
`presets.cpp` records (TuneWorx being the framework's 40-parameter one at the
time, not the 18 it has here), so a preset that large simply could not be saved. The writer
builds the whole document in a string of its own regardless, so the buffer
bounded nothing except what was possible. Session state, which has no size to be
limited to, is what made keeping it indefensible.

Editing a preset's comment is deliberately *not* a rewrite: `saveDirtyComment()`
reparses the file, moves the `Comment` attribute and prints back the document it
read, so a 2.x preset stays 2.x. Changing a comment is not a reason to rewrite
somebody's file into a grammar the plugin they had it from cannot open.

### 4.4b Precision

3.0 prints floats at nine significant figures — the shortest that round-trips
every `float` — where 2.x wrote four *decimals* (`lexicalCast.cpp:63`). Four is
ample for 6000 Hz and coarse for a normalised 0..1 frequency, where it is about
fourteen bits. Reading is unaffected, since `strtof` takes whatever it is given,
so no committed preset moves.

### 4.4 `dawExtraState`

The paradigm surge and the rest of the Surge Synth Team plugins use, and the
reason state and preset can share one serialisation without becoming the same
thing: two hooks on `Preset`, mirroring
`sst::plugininfra::patch_support::PatchBase`.

```cpp
std::function<void(TiXmlElement &)>       dawExtraStateTo  {nullptr};
std::function<void(TiXmlElement const &)> dawExtraStateFrom{nullptr};
```

`savePreset` takes a flag; a `.swp` passes false and a state blob passes true.
The element is written even when the hook writes nothing into it, so an empty
`<dawExtraState/>` is a testable claim that the mechanism is there. `loadPreset`
calls the reader only when the element is present, so loading a `.swp` into a
live session is not a silent reset of session state.

**Its payload is deliberately empty for now.** The mechanism is the deliverable;
the payload accrues. The candidate is the preset browser's location and
selection, which is main-thread and is not a parameter.

The settings panel's Interface page is *not* a candidate, and it is the case that
says where the line is. Those three — mouse-over reaction, LFO update behaviour,
hide-cursor-on-knob-drag — used to persist nowhere at all (issue #61). They are
answers about how this user likes the editor to behave, not about this session,
so they are the same in every instance and in every project, and they belong in
the user's preferences file rather than in a host's state blob:
`<user folder>/SpectrumWorxUserDefaults.xml`, through
`sst::plugininfra::defaults::Provider`. \see `src/gui/preferences.hpp`. Its
format is that library's; what this tree fixes about it is that both
enumerations are streamed **by name**, so inserting a value cannot silently
change what an existing file means. `tests/gui/preferencesTests.cpp` pins the
names.

Note what does *not* need it — the loaded sample. `<Global Sample="…">` has
carried that since 2011, so putting state on the preset serialisation restores it
for free, and `setNewSample()` can finally mark the session dirty.

---

---

## 5. What the host holds

`stateSave` writes
`savePreset(currentSampleFile(), {}, programMain_, &sessionState())` and puts the
bytes on the stream, terminator included. **`programMain_`, not the engine's
`program_`**, and that is the entire point of there being two: `stateSave` is
`[main-thread]` and a host calls it with audio running, so reading the copy the
audio thread owns would be reading the chain it is splicing.

`stateLoad` reads the stream to its end, NUL-terminates it and hands it to
`GUI::loadPreset(*this, pEditor_, …)`. Three things go with that and are worth
naming, because "that is the whole of it" was not quite true:

- it opens a `GUI::UnattendedLoad` scope, which asserts that nothing raises a
  modal box underneath it — nobody asked for this load and there may be no window
  to answer one;
- the load asks the host to re-read the parameters afterwards, deferred and
  coalesced, because every value has moved at once;
- the preset may name an audio file, and loading one is synchronous. A file that
  will not load clears the sample rather than leaving the previous one in place,
  and reports a `PresetProblem` that `stateLoad` drops — there is nobody to tell.

What it replaced: `SWX1` followed by 286 `(uint32 id, double value)` pairs, keyed
on `SW::ParameterID` — which means "slot 3's 4th parameter" and never "Convolver's
Wet". It survived a reload and nothing more. It could not be versioned against a
changing effect list, and it could not carry anything that is not a parameter,
which is why a session forgot which audio file was loaded. It is **dropped, not
kept as a fallback**: nothing has shipped, so the only sessions holding one are
development sessions in this tree.

Three things fall out of state being a preset:

- **The sample travels.** `<p n="Sample">` has been in the format since 2011, so
  `setNewSample()` can call `markCurrentProgramAsModified()` — it deliberately
  did not, because marking a session dirty would have promised to remember
  something the old format could not hold.
- **The chain rebuild is not a mutation of the live chain.** `loadPreset` builds
  the whole replacement on the main thread and publishes it, and whatever it
  displaces comes back to be destroyed there — `threading_model.md` §5. The old
  `stateLoad` rebuilt the chain in place, on the main thread, while `process()`
  might be walking it.
- **All four formats move together**, because clap-wrapper's VST3, AUv2 and
  standalone all carry the CLAP blob.

`GUI::loadPreset` takes the editor as a **pointer**, and that is what makes one
code path serve both. A host restores state before it has ever shown an editor,
and with the window shut for the rest of the session; the editor's part is
building each module's UI region and moving the slot marker, both of which are
simply skipped when there is nowhere to draw.

---

## 6. Adding or changing a parameter

- **Adding one.** Nothing to do. Its display name becomes its streaming name;
  regenerate `streamingNames.txt` and `parameterTable.txt` and read the diff —
  new rows only.
- **Renaming a knob.** Change `EFFECT_PARAMETER_NAME`, add
  `EFFECT_PARAMETER_STREAMING_NAME` in the effect's **header** with the *old*
  string. `parameterTable.txt` moves; `streamingNames.txt` and
  `presetCorpus.txt` must not. If either does, the pin is not visible where the
  parameter table is built.
- **Retitling an effect.** The same, with `LE_SW_EFFECT_STREAMING_NAME` in
  `effectNames.cpp`. The `effect/NN` rows carry both columns, so the diff shows
  the title moving beside a streaming name that did not.
- **Changing a streaming name.** Don't. If there is a reason,
  `streamingNames.txt` moving is the file telling you how many presets it is
  worth.
