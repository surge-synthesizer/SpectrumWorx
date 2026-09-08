# SpectrumWorx — MIDI input

What arrives on the note port, where it goes, and the one thing it costs.

Written 01.09.2026. Everything here is in the tree and has tests naming it.

---

## 1. What this is, and what it is not

A note arrives on the port, is parsed once, and lands in two places that look
alike and are not: `Threading::MIDIMonitor`, which the interface may draw, and
`Engine::MIDINoteStatus`, which an effect's `setup()` may read. §4 is why they are
two things.

**No shipped effect reads a note yet**, and no pixel shows one — the overlay that
answered "does a host route notes here at all" is behind `SW_MIDI_OVERLAY` and off
(§5). What is here is the route and the seam at the end of it: the port, the
parser, the two mailboxes, and the one line of an effect's declaration that asks
for the notes. `effect_contract.md` §1.9 is the contract an effect satisfies to
get them.

So the acceptance test is a sentence rather than a number: build with
`SW_MIDI_OVERLAY`, run the standalone, play, and the notes appear on screen. That
is how the route was established and it is how it would be re-established.

## 2. The port

One input, none out (`SpectrumWorxCLAP::notePortsCount`). Nothing here makes
notes, and a plugin that declares an output port it never writes is asking hosts
to draw a cable to nowhere.

**Both dialects, and neither is redundant:**

```
supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI
preferred_dialect  = CLAP_NOTE_DIALECT_CLAP
```

A note arrives as `CLAP_EVENT_NOTE_ON` under the CLAP dialect, which is the one
worth preferring — it carries a note id and a real velocity. A **controller has
no CLAP note event at all**: a CC only ever arrives as a raw `CLAP_EVENT_MIDI`.
So a plugin that took the CLAP dialect alone would see every note and never a
knob, which is exactly half of what this is for and would look like working.

## 3. The parser

`SpectrumWorxCLAP::handleNoteEvent()`, called from `handleEvent()` before the
parameter path and returning "this was a note" so that nothing downstream tries
to read a note as a parameter write. Channel is not read: every channel writes
the same slot, which is what "any channel" means.

Four cases, and two of them are the ones that leave a key stuck if they are read
as a press:

| | |
|---|---|
| `NOTE_ON` / `NOTE_OFF` | the CLAP dialect, straight through |
| `NOTE_CHOKE` | **lifts the key.** A voice cut short rather than released — a track stopping mid-note — and the alternative is a key drawn as held that nothing will ever lift |
| `MIDI` `0x90` with velocity 0 | **a release.** This is how a great many keyboards and hosts spell note-off, and reading `0x90` as an unconditional press is the same stuck key |
| `MIDI` `0xB0` | the controller, number and value |

A CLAP note-on with zero velocity is still a note-on. The zero-velocity
convention belongs to the MIDI dialect and is applied only there.

## 4. The two mailboxes

The parser writes both. They carry nearly the same thing and are deliberately not
one class, because they cross different boundaries.

### The engine's — `engine/midiNoteStatus.hpp`

128 velocities, 128 controller values, and nothing else: no atomics, no counter,
no ordering. It is written by `handleNoteEvent()` and read by an effect's
`setup()`, and **both of those are the audio thread**, so there is no edge to
synchronise across and paying for one would be paying for nothing.

Zero velocity is how it spells "up". That is why a CLAP note-on carrying no
velocity is stored as 1 rather than 0 — that note is *down*, and the
zero-velocity release convention belongs to the MIDI dialect alone (§3).

`Processor::reset()` clears it, which is the host's reset and every resume. A key
still down across a transport stop would come back sounding with nothing holding
it.

It is handed to an effect **`const`**, for the reason side-channel data is: every
module in the chain is given the same one, so a module that consumed a note would
take it from every later slot.

### The interface's — `core/threading/midiMonitor.hpp`

128 note flags, 128 controller values, 128 stamps saying when each controller last
moved, and one change counter. The audio thread writes, the interface reads.

**Deliberately not a snapshot.** Each slot is its own atomic, so a reader can
land between the two writes of a block that moved two controllers and see one of
them. That is fine for something drawn at 30 Hz and would not be fine for
anything the engine acted on — which is why this is not the ring in
`spscQueue.hpp`, where order and delivery are guaranteed and paid for.
`threading_model.md` §3 is where the two are told apart.

The change counter is the only thing read in the common case: one acquire load
per tick, and nothing else happens unless it moved. A controller's stamp is the
counter value at which it was written, which is what lets the display tell "moved
during this window" from "moved four minutes ago".

## 5. The display, which is off

**`SW_MIDI_OVERLAY` is not defined**, so none of this is built. It is declared,
commented out, at the top of `spectrumWorxEditor.hpp`; define it there or on the
command line and the rest of this section describes what you get.

It was a bring-up aid and it did its job. Nothing in it is for a user: it answers
*does a host route notes here at all*, which is a question no validator asks, and
once the answer is yes there is nothing left to read. The monitor underneath it is
**not** behind the switch — `Threading::MIDIMonitor` and `EditorHost::midiMonitor()`
are always built and always tested, because they are the seam an interface that
did something with notes would read.

`SpectrumWorxEditor::paintOverChildren()`, armed by `pumpMIDIMonitor()` from the
same 30 Hz tick that sweeps the modulation mailbox. Anything arriving re-arms it
for two seconds; keys held are listed by name, controllers touched during the
window by number and value.

**Counted in ticks, not against a clock** — `midiOverlaySeconds *
modulationRefreshHz` — so a headless case runs the two seconds out by pumping
rather than by sleeping for them.

`pumpMIDIMonitor()` returns whether it asked for a repaint, which is the same
shape `updateEngineInformationIfChanged()` has and for the same reason: a
picture cannot tell "drew the right thing" from "asked to draw it", because
`paintEntireComponent()` repaints whatever it is handed.

The plate behind the text is not decoration. The overlay lands wherever the panel
column happens to have put something, and red text over the preset browser's own
chrome is unreadable — which is what the first render of it showed.

**It is loud and it is temporary.** `MIDIMonitorText` and `MIDIMonitorPlate` are
in the palette because the project's gate requires every colour to be, but they
are the same in every skin on purpose: what they report is not part of the
instrument. They stay in the palette with the switch off — two rows that cost
nothing and would otherwise have to be re-added to turn it back on.

## 6. What it cost: `aufx` became `aumf`

`swClapEntryImpl.cpp` now names the AU type instead of leaving it to be derived.
Derivation reads `features[0]`, which is audio-effect, and gives `aufx` — an AU
that takes audio and no notes, which **Logic routes no MIDI to**. The note port
would be declared and unreachable in the host it matters most for.

**The type is part of an AU's identity, so this is not a setting.**
`aufx/SWrx/SSTx` and `aumf/SWrx/SSTx` are two different components, and auval is
`auval -v aumf SWrx SSTx` from here on.

### The old identity is kept resolvable

A session saved against the `aufx` would have opened without the plugin, so the
retired identity is declared through `clap.plugin-factory-info-as-auv2-legacy`
(`swClapEntryImpl.cpp`) and clap-wrapper writes it into the bundle as a **second
AudioComponents entry**, identical to the first but for the triple. Both pass
auval; the `aufx` instance reports the same `[2,2] [1,1]` layouts and builds its
editor. Verified 01.09.2026 by opening `SpectrumWorxSCTest.logicx`, saved against
3.0's `aufx`.

That works because the wrapper no longer bakes the AU type into the generated
entry point: one factory serves both entries and reads its type from the
component description the host instantiated it with. A build-time constant could
not have answered for two identities.

**The entry is visible, and `kAudioComponentFlag_Unsearchable` is the trap.** It
is documented to keep a component out of wildcard enumeration while leaving it
resolvable by a fully specified `AudioComponentFindNext`, which is exactly what a
retired identity wants — and it does not work. Logic restores a session against
the registry its own AU scan builds, and that scan enumerates. Measured: with the
flag set, `AudioComponentFindNext` found the entry, instantiated it, initialised
it and drew its editor, Logic's scan log recorded one SpectrumWorx rather than
two, and the session would not open. The plugin's own `ProjectData` names the
right triple either way:

```
78 54 53 53  ->  "SSTx"   manufacturer
78 66 75 61  ->  "aufx"   type
78 72 57 53  ->  "SWrx"   subtype
```

Listing it costs nothing: a host filters a slot by component type, so the `aufx`
appears in an effect slot and the `aumf` in an instrument slot, never together.

`identityTests.cpp` pins the type **and** the retired triple — spelt out rather
than derived, because a legacy identity that followed a rename would not be one.

## 7. What guards it

| | holds |
|---|---|
| `tests/clap/midiInputTests.cpp` `[midi]` | the port's shape and both dialects; a note reaching **both** mailboxes and lifting from each; the choke and the zero-velocity note-on that would otherwise stick; a note-on of no velocity landing as a key that is *down*; a controller arriving with its value on any channel; that a reset lifts every key; and that a note in the same event list does not disturb the parameter path. With `SW_MIDI_OVERLAY` it also drives the editor arming, drawing and expiring — one tick short of the window and then the tick that closes it, because a display that never expired and one that expired immediately both pass a case that only looks at the end |
| `tests/effects/midiConsumersTests.cpp` `[midi]` | that the `setup()` signature is what decides a consumer — the static, non-static and by-value forms are, a mistyped third parameter is not — and that the derived table names exactly the effects that read notes, which is none of them |
| `tests/clap/identityTests.cpp` | the AU type and the retired triple, so §6 does not happen twice |

**What is not guarded is the host end.** No validator asks whether a host routes
notes, and neither auval nor the VST3 validator sends any. The standalone with a
keyboard is the test, which is why §1 states it as one.

**And the engine's dispatch has no shipped consumer.** `ConsumesMIDI` decides
between the two- and three-argument `setup()` at `moduleImpl.hpp`, and with no
effect declaring the third parameter the true branch of that `if constexpr` is
never instantiated. That is a compile error the moment one does, rather than a
silent wrong answer — the two forms are mutually exclusive, so an effect that gets
the signature wrong satisfies neither and fails to build. It is still a branch no
test executes, and it is the first thing the first note-consuming effect proves.
