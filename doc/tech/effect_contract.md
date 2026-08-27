# SpectrumWorx — The Effect Contract

What a spectral effect is in this tree, what the engine hands it, what it must
hand back, and what it costs to add one. Written for somebody about to write a
new effect, or to revive one of the twenty-one that are in the tree and in no
build.

Written 07.08.2026 against `define-cleanup`. Companions:
[`parameter_system.md`](parameter_system.md) (what happens to a parameter once
it leaves the effect), [`streaming_format.md`](streaming_format.md) (what
happens to it once it reaches a file), [`threading_model.md`](threading_model.md)
(who is allowed to call what).

The normative statement of the contract is the comment block at
`src/le/spectrumworx/effects/effects.hpp:48-124`. This document is that comment
with the machinery attached, the traps named, and the tree measured against it.

---

## Contents

1. [The contract](#1-the-contract) — the two classes, parameters, `setup`/`process`, channel state, registration
2. [The inner DSP](#2-the-inner-dsp) — what a frame actually is, and four effects taken apart
3. [The inventory](#3-the-inventory) — 57 shipped, 4 orphaned, 17 unfinished

---

# 1. The contract

## 1.1 One effect is two classes in three files

    src/le/spectrumworx/effects/<folder>/
        <module>.hpp        the base class      — parameters, title, description
        <module>Impl.hpp    the implementation  — setup(), process(), ChannelState
        <module>Impl.cpp    the definitions     — title[], description[], the DSP

`<folder>` is `snake_case`, `<module>` is `camelCase`; `talking_wind/talkingWind.hpp`.
The split is not cosmetic. `effects.hpp:115-120` says why:

> The Base-Impl separation is required to facilitate easier extraction of effects
> into the SW SDK without duplication and without disclosing internal
> implementation details.

The practical consequence today: **everything a host, a preset or the GUI needs
to know about an effect lives in the base header**, and nothing there mentions
how the effect works. `allEffects.hpp` includes the 46 base headers;
`allEffectImpls.hpp` includes the 46 Impl headers. The parameter table, the
menus, and the preset keys are built from the first list alone.

### The base class

```cpp
namespace LE::SW::Effects
{
struct Gain
{
    static char const title[];
    static char const description[];
};
} // namespace LE::SW::Effects
```

That is the whole of `gain/gain.hpp`, and it is a legal effect. Required:

| Member | What it is |
|---|---|
| `title[]` | The menu entry and **the preset key**. Defined in the `Impl.cpp`. Must be unique across all 57 — `effectsListTests.cpp:97` checks. |
| `description[]` | One sentence, shown in the UI. Defined in the `Impl.cpp`. |
| `Parameters` | Optional. A `LE_DEFINE_PARAMETERS(...)` list. Absent means the effect has no user controls. |

There is no member declaring whether the effect reads the side chain. There used
to be, and §1.8 says what happened to it.

### The implementation class

```cpp
class GainImpl : public NoParametersEffectImpl<Gain>
{
  public: // LE::Effect interface.
    static void setup  ( IndexRange const &, Engine::Setup const & ) {}
    static void process( Engine::ChannelData_AmPh const &, Engine::Setup const & ) {}
};
```

Derive from `EffectImpl<Base>` when the effect has parameters and from
`NoParametersEffectImpl<Base>` when it does not
(`effects.hpp:132`, `:161`). The only thing the helper does is hold the
`Base::Parameters` instance and expose it as `parameters()` — that is what makes
`parameters().get<Threshold>()` work, and what lets the engine find the parameter
block by pointer arithmetic (`moduleImpl.hpp:158-165`).

Required of the Impl:

- default constructible
- `void setup( IndexRange const &, Engine::Setup const & )`
- `void process( [ChannelState &,] <a ChannelData flavour>, Engine::Setup const & ) const`
- neither may throw
- optionally a nested `ChannelState` type (§1.7)

Both may be `static` when they use nothing — `GainImpl` above is the whole of
`Gain`; the audible gain comes from the base `Gain` parameter every module gets
for free (§1.4), so the effect itself is a placeholder for it.

## 1.2 `setup()` and `process()`: the division of labour

```
main thread                    audio thread, at the first frame of a block
─────────────                  ──────────────────────────────────────────
parameter write     ─────►     preProcess()  ─► LFOs advance
                                              ─► ModuleDSP::setup()
                                                   ├─ workingRange_ ← Start/StopFrequency
                                                   └─ effect().setup( workingRange, engineSetup )
                               process( channel 0 ) ─► effect().process( state[0], data, setup )
                               process( channel 1 ) ─► effect().process( state[1], data, setup )
```

`ModuleDSP::preProcess` (`module.cpp:29-36`) advances the LFOs, recomputes the
working range from `StartFrequency`/`StopFrequency`, and calls your `setup()`.
Then `ModuleDSP::process` (`module.cpp:76-103`) runs your `process()` once per
channel, on the same parameter values.

**It runs once per audio block *that produces a frame*, immediately before the
first one** — `Processor::preProcessForFirstFrame`. A block and a spectral frame
are unrelated quantities: frames arrive every `fftSize / overlapFactor` samples,
so a large FFT under a small host block produces frames more rarely than blocks
arrive, and a small FFT produces several per block. Sampling the parameters at
the block boundary meant sampling them on blocks where nothing would read the
result — which is free for a value that is merely read, and fatal for one that is
*consumed*.

**`setup()` is where every parameter → DSP conversion belongs.** dB → linear,
Hz → bin, ms → frames. `process()` reads only the members `setup()` cached.
`effects.hpp:101-105` states this as a requirement, and the engine guarantees
what makes it safe: *the effect's parameters and the `Engine::Setup` passed to
`process()` are unchanged from the previous `setup()` call.*

`process()` is `const`. That is the enforcement — the only mutable thing it can
reach is the `ChannelState &` it was handed, and one of those exists per
channel. This is what decouples an effect from multichannel processing entirely:
you write mono and the engine runs you N times.

**A `TriggerParameter` may be consumed in `setup()`.** `consumeValue()` reads a
trigger and disarms it, so it must not be called where nothing will act on the
answer — which is the whole reason `setup()` is tied to the first frame rather
than to the block. `FreezeImpl` and `ConvolverImpl` both do it, and neither has
to know any of the above.

What is *not* guaranteed is one `setup()` per `process()`: a block holding four
frames still samples the parameters once, so `process()` runs four times per
channel on one set of values. An effect that needs "has this arm already been
acted on" still keeps that per channel — `FreezeImpl::ChannelState::
previousFreezeFlag`/`previousMeltFlag` and `ConvolverImpl::ChannelState::
frozenFlagConsumed` — because the effect's own members are shared by every
channel and the channel loop is outside the frame loop.

> **Trap.** Do not move a `consumeValue()` into `process()`. It is called per
> channel, so the first channel would take the trigger and the rest would never
> see it — the effect would freeze one side of a stereo pair.

## 1.3 What `process()` is handed

The parameter's *type* is how you choose. `ModuleDSP::ChannelDataProxy`
(`module.hpp:57-76`) has a conversion operator for each flavour, and overload
resolution on your `process()` declaration picks one. There is no flag and no
registration — **declaring `MainSideChannelData_AmPh` is how an effect asks for
the side chain**, and declaring `ChannelData_ReIm` is how it asks for the
rectangular form.

| Declare | You get | Cost |
|---|---|---|
| `Engine::ChannelData_AmPh` | main channel, amplitude + phase, working range | none — this is the engine's native form |
| `Engine::ChannelData_ReIm` | main channel, real + imaginary | an AmPh → ReIm conversion if the previous module left AmPh |
| `Engine::MainSideChannelData_AmPh` | `.main()` and `.side()`, both AmPh | none |
| `Engine::MainSideChannelData_ReIm` | `.main()` and `.side()`, both ReIm | conversion |
| `Engine::ChannelData_AmPh2ReIm` | `.input` AmPh (read), `.output` ReIm (write) | for effects that must *sum* voices rather than replace bins — Octaver |
| `Engine::ChannelData_ReIm2AmPh` | the same with the roles swapped | |

`ChannelData` (`channelData.hpp:104`) keeps both domains and a freshness counter
each side, and converts on demand: if module 2 leaves ReIm and module 3 asks for
AmPh, the conversion happens between them and nobody writes a line for it. The
cost is real, so a chain that alternates domains pays per module.

Every flavour gives you:

```cpp
data.amps()      // DataRange over the working range
data.phases()
data.size()      // bins in the working range
data.beginBin()  // where the working range starts in the full spectrum
data.endBin()
data.full()      // the whole spectrum, ignoring the working range
++data           // advance every sub-range by one bin
if ( data )      // false when exhausted
```

`data.full()` is the escape hatch, and §1.5 says when you need it.

## 1.4 Parameters

Three macros, all used in the **base header**:

```cpp
struct Shifter
{
    LE_ENUMERATED_PARAMETER( ShiftTarget, Magnitudes, Phases, Both );
    LE_ENUMERATED_PARAMETER( Tail, Leave, Clear, Circular );

    LE_DEFINE_PARAMETER( Offset, SymmetricFloat, MaximumOffset<10>, Unit<" bw%"> );

    LE_DEFINE_PARAMETERS( ShiftTarget, Offset, Tail );

    static char const title[];
    static char const description[];
};
```

`LE_DEFINE_PARAMETER( name, Type, Traits... )` (`parameter.hpp:311`) declares one
parameter class. `LE_ENUMERATED_PARAMETER( name, values... )`
(`enumerated/parameter.hpp:95`) declares an enumerated one, generating both the
scoped counting enum and the unscoped `value_type` you compare against.
`LE_DEFINE_PARAMETERS( ... )` (`factoryMacro.hpp:55`) collects them into the
`Parameters` container — **and its order is the order.** Automation addresses by
index; presets serialise by name. Reordering the list moves every host automation
lane assigned to that slot.

Types (imported into `Effects` by `parameters.hpp:52-61`) and traits (`:70-75`):

| Type | Traits it takes |
|---|---|
| `Boolean` | — |
| `TriggerParameter` | — |
| `LinearFloat`, `LinearSignedInteger`, `LinearUnsignedInteger` | `Minimum<>`, `Maximum<>`, `Default<>`, `Unit<"">`, `ValuesDenominator<>` |
| `SymmetricFloat`, `SymmetricInteger` | `MaximumOffset<N>` — min −N, max +N, default 0 — plus `Unit<"">` |
| enumerated (via the macro) | — |

`ValuesDenominator<1000>` is how a float parameter with a sub-unit range is
declared in integer traits: `Freqverb::Time60dB` is
`Minimum<100>, Maximum<20000>, Default<2500>, ValuesDenominator<1000>, Unit<" s">`
— 0.1 s to 20 s, default 2.5 s.

### Names go in the header, next to the parameter

```cpp
EFFECT_PARAMETER_NAME( Shifter::Offset, "Offset" )

EFFECT_ENUMERATED_PARAMETER_STRINGS( Shifter, Tail,
    { Leave,    "Leave"    },
    { Clear,    "Clear"    },
    { Circular, "Circular" } )
```

These are explicit specialisations (`uiElements.hpp:309`, `:273`, `:350`), so
every translation unit that builds the parameter table has to have **seen**
them. One that has not is ill-formed, no diagnostic required. Since 04.08.2026
they live in the header beside the parameter and the build makes the warning
fatal; `parameter_system.md` §9 has the full argument. A missing
`EFFECT_PARAMETER_NAME` is a compile error. A missing
`EFFECT_PARAMETER_STREAMING_NAME` or `DisplayValueTransformer` is **silent** —
the parameter streams under the wrong key or prints without its unit.

Use `EFFECT_PARAMETER_STREAMING_NAME` only when a display name has to move after
files have been written naming the old one. A new effect's parameters have no
history; their display name is their streaming name.

`EFFECT_ENUMERATED_PARAMETER_SHORT_STRINGS` is the fourth of these and is
optional: the same values again, abbreviated to what a sixty-pixel combo box can
show, used by that box and by nothing else. Reach for it when a value string does
not fit — `Vaxateer::Mode` is the shipped example. `parameter_system.md`, "A value
may be read differently from how it is listed", has the rule.

`EFFECT_ENUMERATED_PARAMETER_MENU_ORDER` is the fifth and is also optional: the
same values a third time, bare and in the order the menu should offer them.

```cpp
EFFECT_ENUMERATED_PARAMETER_MENU_ORDER( CommonParameters, Mode,
    Magnitudes, Phases, Both )
```

Declaration order is ABI — a value is its own index, and presets and automation
lanes are written in it — so this is the only way to move a row. Each row carries
its own value, so nothing but the menu changes. The list must be a permutation of
the parameter's values and a compile error says so if it is not.
`parameter_system.md`, "An enumerated parameter's rows are not its values".

### How many parameters you get

Thirteen, as of 22.08.2026 (issue #156). `maxNumberOfParametersPerModule` is 18
(`src/configuration/constants.hpp`) and `numberOfParameters()` is
`effectSpecific + numberOfBaseParameters` (`moduleParameters.hpp:59-62`), where
the base block is the five below. Of the 57 shipped effects the largest is
TuneWorx at thirteen; the next is Octaver at five.

**Do not read that as headroom.** Thirteen is what the widest effect declares
and the ceiling is set to fit it exactly, because a module parameter costs far
more than one row: `maxNumberOfModules` of them, each with the seven LFO
parameters that drive it, so one more is forty more in a host's automation list.

Past the ceiling a parameter still runs and still reaches a preset, and only its
automation goes: it has no `ParameterID`, so no lane can address it and
`automatedParameterChanged` drops the change (`plugin2Host.cpp`). That failure is
silent, which is how TuneWorx shipped from 2011 to 2026 with `Semi05`…`Semi12`
the engine ran and no DAW could see — found in the field, not by a test. The
`static_assert` on `largestEffectParameterCount` in `factory.cpp` is what makes
it a compile error now, so an effect wider than the ceiling stops the build and
the ceiling is raised deliberately.

### What every effect gets for free

`baseParameters.hpp:47-52` — five parameters on every module, which you never
declare and never handle:

| Parameter | Range | Applied by |
|---|---|---|
| `Bypass` | boolean | `ModuleDSP::process` — your `process()` is not called at all |
| `Gain` | ±20 dB | `channelData.amplifyCurrentData()` **after** your `process()` |
| `Wet` | 0–100 % | `channelData.blendWithPreviousData()` after your `process()` |
| `StartFrequency` | 0–1 normalised | → the `IndexRange` handed to `setup()` |
| `StopFrequency` | 0–1 normalised | → the same |

Dry/wet and per-module gain are the engine's job (`module.cpp:84-101`). Do not
implement either. `Start`/`Stop` are why `process()` sees a sub-range at all.

### The GUI is generated

You write no UI code. `Detail::FoldWidgets` (`moduleUI.hpp:578-591`) walks the
`Parameters` list at compile time and builds one widget per parameter, chosen by
the parameter's tag (`moduleUI.hpp:277-295`):

| Parameter tag | Widget |
|---|---|
| boolean | `ModuleLEDTextButton` |
| enumerated | `DiscreteParameter` |
| trigger | `TriggerButton` |
| everything else | `ModuleKnob` |

`tools/show-ui` then renders one screenshot test per effect off the same table
that registers it, so a new effect acquires a UI test by being listed.

## 1.5 The working range

`IndexRange` (`indexRange.hpp:42`) is a half-open `[begin, end)` pair of bin
indices, computed once per block from the two base frequency parameters:

```cpp
workingRange_.setNewRange
(
    engineSetup.normalisedFrequencyToBin( std::min( left, right ) ),
    engineSetup.normalisedFrequencyToBin( right )
);
```
— `module.cpp:46-48`.

> **It is half-open, and its setter is not.** `begin()`, `end()` and `size()` are
> half-open; `first()` and `last()` are inclusive (`last()` is `end() - 1`); and
> `setNewRange( begin, last )` / `setLast( last )` take an **inclusive** last and
> store `last + 1` (`indexRange.cpp:29-40`). The call above therefore ends the
> range one bin past the bin `StopFrequency` lands in.

Every `ChannelData` flavour is already clipped to it. An effect that only reads
and writes `data.amps()` and `data.phases()` honours the range by construction
and need not mention it.

You need the range explicitly in three cases:

1. **Bin arithmetic in `setup()`.** Hz → bin conversions must be clamped into
   `workingRange`, because the user can move the range under you. `AhAh` is the
   cleanest example.
2. **Effects that deliberately work outside it.** `Bandpass` attenuates
   everything *outside* the range — it is the one documented inversion, and its
   header says so.
3. **History-based effects.** Anything keeping a per-bin history must keep it
   for the *whole* spectrum, or the history goes incoherent the moment the user
   moves the range. `freqverbImpl.cpp:90-92` states the rule; `Reverser` and
   `Frecho` use `data.copySkippedRanges( Engine::DataPair::Amps, buffer )` to
   carry the untouched bins across.

The PVD markers (`PhaseVocoderAnalysis`, `PhaseVocoderSynthesis`) ignore the
range on purpose and call `data.full()`: a domain transition has to be
whole-spectrum or the two halves disagree about what "phase" means.

## 1.6 `Engine::Setup` — the frame's geometry

Passed to both `setup()` and `process()`, read-only (`setup.hpp:50`). The parts
an effect uses:

```cpp
setup.fftSize<std::uint16_t>()            setup.numberOfBins()
setup.windowOverlappingFactor<float>()    setup.sampleRate<float>()
setup.stepSize<T>()                       setup.frequencyRangePerBin<float>()
setup.stepTime()                          setup.stepsPerSecond()
setup.maximumAmplitude()                  setup.wolaGain()

setup.frequencyInHzToBin( hz )            setup.normalisedFrequencyToBin( 0..1 )
setup.frequencyPercentageToBin( pct )     setup.milliSecondsToSteps( ms )
setup.secondsToSteps( s )                 setup.hasSideChannel()
```

`maximumAmplitude()` is the one to know: amplitudes are not normalised to 1, so
a dB threshold becomes `setup.maximumAmplitude() * Math::dB2NormalisedLinear( dB )`.
`Freqnamics` is the two-line demonstration.

`stepTime()` and `stepsPerSecond()` are how a time constant becomes a per-frame
constant. An effect that counts frames without dividing by these changes speed
when the user changes the overlap factor.

## 1.7 `ChannelState` — the only mutable thing

If `process()` needs to remember anything between frames, declare a nested
`ChannelState`. The engine allocates one per channel, hands you yours, and never
lets two channels see each other's. `Detail::has_ChannelState`
(`moduleImpl.hpp:260-263`) detects the nested type and selects the holder; you
get the two-argument `process()` if it exists and the one-argument form if it
does not.

Four tiers, in increasing order of ceremony:

**None.** Half the tree. Cached scalars go in the Impl as members, written by
`setup()`, read by the `const process()`.

**`StaticChannelState`** — per-channel scalars, no engine storage:

```cpp
struct ChannelState : StaticChannelState
{
    float phaseShift_;
    float time_;
    void reset() { phaseShift_ = 0; time_ = 0; }
};
```
— `phasevolutionImpl.hpp`. `reset()` is mandatory; the base supplies a
`requiredStorage()` of 0 and an empty `resize()`.

**`ModuloCounterChannelState`** (`effects.hpp:238`) — a `StaticChannelState`
holding one `ModuloCounter frameCounter`, whose `nextValueFor( modulus )`
returns `{ value, wrapped }`. Everything that fires every N frames uses it.

**`DynamicChannelState_<Self>`** (`channelStateDynamic.hpp:81`) — members that
size themselves out of the engine's storage block:

```cpp
struct ChannelState : DynamicChannelState_<ChannelState>
{
    PhaseVocoderShared::PitchShifter::ChannelState pv1;
    PhaseVocoderShared::PitchShifter::ChannelState pv2;
    auto members() { return std::tie( pv1, pv2 ); }
};
```
— `octaverImpl.hpp`. Declare the members, return them from `members()`, and
`requiredStorage()`, `resize()` and `reset()` are all generated from that one
`std::tie`. Legal member types are anything with the trio:
`Engine::HalfFFTBuffer<T>`, `Engine::FFTBuffer<T>`, `HistoryBuffer<T, ms>`,
another channel state.

To add plain scalars to a dynamic state, derive again and write `reset()` by
hand — `ConvolverImpl` and `FreezeImpl` both do, and `Freeze`'s deliberately does
*not* chain to the base (frozen spectra need not be cleared, only the phase
vocoder state does).

`CompoundChannelState<A, B, ...>` (`channelStateDynamic.hpp:112`) glues several
together; `Burrito` uses `CompoundChannelState<ModuloCounterChannelState, DynamicChannelState>`.
`PitchShifterBasedEffect` assembles one for you.

### Randomness lives here too

**An effect that draws random numbers holds a `Math::Rng` in its `ChannelState`,
and nowhere else.** There is no global generator to call; `Math::rangedRand()`
and friends are gone.

```cpp
struct ChannelState : StaticChannelState
{
    Math::Rng rng;
    static void reset() {}
    void seed( std::uint64_t const seed ) { rng.seed( seed ); }
};
```
— `whispererImpl.hpp`, which grew a `ChannelState` for exactly this and nothing
else. Declare `seed( std::uint64_t )` and the engine finds it: `callSeed()`
(`moduleImpl.hpp`) detects it with a `requires` and deals one stream per channel
out of the instance's seed source at every `reset()`, so nothing else is asked to
grow an empty override.

Two rules follow from where it sits:

- **Keep it out of `members()`.** It owns no engine storage, so a dynamic state
  declares it as a plain member beside the tie. `Freqverb` and `Burrito` both do.
- **`reset()` must not reseed.** A stream that restarted from the top on every
  transport stop is a repeating noise pattern, not a reset one. Seeding is the
  engine's, at a moment it chooses.

Why it is per channel rather than per effect: the engine finishes every hop of
channel 0 before channel 1 begins, so one generator shared between them makes the
output depend on **how many times `process()` was called** — which is the host's
block size, not anything a listener asked for. That was a real bug in Freqverb,
Whisperer and Burrito. `tests/core/chunkTransparencyTests.cpp` is what holds it
shut; issue #86 is the history.

The same reasoning puts one on each `LFOImpl` (`LFOImpl::WaveformState::rng`),
because an LFO modulates a parameter and a parameter has no channel.

> **`reset()` means "the transport moved"**, not "construct". It is called on
> resize and on engine reset, and it must leave the state as if no audio had ever
> been seen. A state that leaks the previous session's spectrum through a
> transport jump is the failure this exists to prevent.

## 1.8 The side channel

Ask for it by declaring a `MainSideChannelData_*` flavour. `data.side()` is
`const` — `channelData.hpp:98-101` says why, and §1 of the contract comment
restates it: **all processing is in place, side-channel data is read only.**

`Engine::Setup::hasSideChannel()` tells you at run time whether anything is
plugged in. Silence is what you get when it is not, so an effect that multiplies
by the side chain goes silent rather than misbehaving.

**Which of three sources fills it is the patch's, and an effect cannot tell them
apart** — `data.side()` is the same span whichever one it came from. The user
picks a file, the main input, or the host's second port;
[`sidechain-approach.md`](sidechain-approach.md) is the whole of it, including
what an old preset's `Input_mode` migrates to. Nothing about it reaches an effect,
which is the point: side-chain routing is not a DSP concern.

**What the engine is handed when the selected source has nothing behind it is the
main input, not silence** — so a Blender in a host with no side chain blends the
signal with itself, and one in `Main` does so always. For the host's port that is
two arrangements, both structural: no second port at all, and a second port with
no `data32`. Past those the port is read, whatever is in it, so a port a user has
patched nothing into is heard as the silence the host put there. The plugin used
to read `clap_audio_buffer::constant_mask` to guess otherwise and no longer does;
[`sidechain-approach.md`](sidechain-approach.md) §2 says why, and issue #117 is
where it went.

> **Nothing declares that an effect reads the side chain, and nothing should.**
> The `process()` overload is the declaration: take a `MainSideChannelData` and
> you read the side chain, do not and you cannot. There is no second place to
> say so and therefore no second place to be wrong.
>
> There was one until 08.08.2026 — a `static bool const usesSideChannel` on every
> effect class, documented as part of this contract. No code ever read it, and by
> the time it was measured it named **seven** effects where the engine's
> behaviour said **fifteen**: `convolver.hpp` declared `false` while
> `convolverImpl.hpp` took `MainSideChannelData_AmPh`. It was deleted rather than
> corrected, because a constant that has to agree with an overload signature is a
> second answer to a question that already has one.
> `tests/effects/sideChainTests.cpp` holds the measured set of fifteen.

## 1.9 Registering it

Six files, and two of them are load-bearing: the first, which is where the effect
comes into existence, and the last, which is where it becomes reachable.

**1. `src/le/spectrumworx/effects/configuration/effectsList.hpp`** — the single
source of truth. Append one line:

```cpp
    x( my_effect,  myEffect,  MyEffect )
//    folder       module     class
```

and bump `LE_SW_NUMBER_OF_EFFECTS`. Consumers expand this table over a
three-argument macro to build the impl tuple (`indexToEffectImplMapping.hpp:28`)
and the name arrays (`effectNames.cpp:48`, `:54`).

> A fourth column named the effect's menu group until 18.08.2026, and the menu
> was a walk over this table that started a sub-menu wherever that column
> changed — so the menu's order *was* this table's order, the one order that may
> never move. Grouping is now `gui/editor/moduleMenuLayout.cpp` (issue #121, and
> step 6 below). The `/* … */` markers left in the table are a reading aid and
> nothing parses them.

> **The order is ABI.** Presets resolve an effect by name but automation
> addresses a slot's *content* by index, and `effectsListTests.cpp:117` pins the
> first, last and two interior entries against reordering. **Append. Never
> insert, never remove, never reorder.** One folder may contribute several
> entries — `bandpass` gives `Bandpass` and `Bandstop`, `eximploder` gives four.
>
> The table is bracketed by `// clang-format off` / `on` for a reason worth
> knowing: `tools/show-ui/CMakeLists.txt:140` parses it with
> `string(REGEX MATCHALL "\n[ \t]*x\\([^\n]*\\)" …)` — the first `x(...)` after
> each newline. Reflowed, the parse silently dropped to 40 of 57 and the UI suite
> lost seventeen tests **without failing**. It did, on 05.08.2026. It now
> `message(FATAL_ERROR …)` when the parse count and `LE_SW_NUMBER_OF_EFFECTS`
> disagree (`:171-176`), so a bad layout fails the configure step. One entry per
> line, and let clang-format nowhere near it.

**2. `allEffects.hpp`** — add the base header include, alphabetically.

**3. `allEffectImpls.hpp`** — add the Impl header include, alphabetically.

**4. `src/dsp.cmake`** — add `le/spectrumworx/effects/my_effect/myEffectImpl.cpp`
to the `add_library( sw-dsp STATIC … )` list, in the block headed
`# le/spectrumworx/effects — one per shipped effect, per effectsList.hpp`.
It is an explicit list; there is no `file( GLOB )` anywhere in `dsp.cmake` and no
`CMakeLists.txt` under `effects/`. **A file not named here is not compiled, and
nothing tells you.** That is exactly how the four effects in §3.2 rotted.

**5. `tests/effects/effectsListTests.cpp:53`** — the count assertions
(`static_assert( numberOfEffects == 57 )`) are deliberate tripwires and will not
compile otherwise. Lines 122-126 additionally pin `names.front()`,
`names.back()`, `names[7]` and `names[8]` against reordering.

**6. `src/gui/editor/moduleMenuLayout.cpp`** — put the effect in a menu group, by
its **streaming name**, at the position in that group's array where you want it
listed. This one is not optional: the layout is checked on the first menu and an
effect in no group **terminates the plugin**, because a menu missing an entry is
an effect no user can reach and nothing else would notice. See issue #121, and
`tests/gui/moduleMenuTests.cpp`, which asks the same question in the form a test
can survive.

> Adding a group is one line in the same file's `layout[]` table, in the position
> the menu should draw it. **That order is the menu's order**, and moving a line
> is all moving a group takes — Phase Vocoder sits after Phase because somebody
> moved that line, which is what issue #121 was opened for. The nine groups are
> Pitch, Timbre, Time, Space, Phase, Phase Vocoder, Loudness, Combine and
> Miscellaneous; read `layout[]` for the order rather than this sentence, and
> note that no test restates it.
>
> The names are streaming names rather than titles: nine effects were retitled
> after presets had named them, so the table says `"PVD start"` where the menu
> shows "To PV". Keying it this way is what makes the next retitling leave the
> layout alone.

Note the 46-to-57 gap: one `.cpp` can back several rows. Nine folders contribute
more than one entry — `eximploder` contributes four, the other eight two each.

### What you must *not* edit

All of these are derived from the table and update themselves:
`constants.hpp`, `includedEffects.hpp`, `indexToEffectImplMapping.hpp`,
`effectNames.cpp`, the factory's runtime-index → compile-time-type dispatch
(`factory.cpp:156-189`), the parameter table, the GUI widgets, and the `show-ui`
render test. **The module menu is not among them** — it has its own table, which
is step 6 above.

`effectNames.cpp`'s pin table in particular: **a new effect adds nothing there.**
`LE_SW_EFFECT_STREAMING_NAME` exists only for an effect whose title moved after
presets had been written naming the old one. A new effect has no history and its
title is its streaming name.

## 1.10 What tests a new effect joins by being listed

Almost all of them enumerate `Constants::numberOfEffects`, so appending a row
enrols the effect and — for the two snapshot files — obliges you to regenerate.

| | What it does to your effect | Regenerate? |
|---|---|---|
| `effects/effectsListTests.cpp` | count, uniqueness of `title[]`, index↔name round trip, distinct impl type, group membership | no — but bump the count asserts |
| `effects/sideChainTests.cpp` | renders **all 57** with and without a side chain and checks whether the output moves | see the trap below |
| `effects/amplifyingEffectsTests.cpp` | finite/bounded output, determinism (same render twice), bypass equals the empty chain — plus hand-written per-effect properties | no |
| `effects/silentDefaultsTests.cpp` | pins the effects legitimately silent at defaults and asserts that set may not grow | **yes if yours is silent** — and prefer fixing the defaults |
| `goldens/goldenTests.cpp` | 8 rows per effect — 2 FFT/overlap configurations × 4 signals — into `goldens/data/goldens.txt`. 500 ms each, except the effects `needsALongRender()` names, which get 2 s | **yes** — `SW_GOLDEN_UPDATE=1`, and read the diff before committing it |
| `parameters/parameterTableTests.cpp` | puts every effect into slot 0 in turn and dumps its parameter table | **yes** — `SW_PARAMETER_TABLE_UPDATE=1` |
| `parameters/streamingNameTests.cpp` | effect and per-parameter streaming names, non-empty and round-tripping | **yes** — `SW_STREAMING_NAMES_UPDATE=1` |
| `parameters/parameterLayoutTests.cpp` | writes one parameter through the offset table, reads every other back | no |
| `presets/presetRoundTripTests.cpp` | save → reload → compare, per effect | no |
| `clap/parameterModelTests.cpp` | id-count agreement, slot-swap renaming, nameability | no |
| `tools/show-ui` | one `show-ui-renders-module-<Effect>` offscreen render, registered from `effectsList.hpp` and driven by `SW_SHOW_UI_EFFECT` | no |

Two of these carry **hand-maintained name lists** a new effect can fall silently
outside of:

> **A side-chain effect with no row in `sideChainTests.cpp:171-219` fails
> bit-exactly.** The sweep partitions the 57; an effect not in the list is held to
> the *deaf* control — `render( effect, mainSignal ) == render( effect, sideSignal )`
> — and an effect that reads the side chain does not satisfy it. The count check
> at `:400` still passes, so the failure arrives as a mystery. Add the row, and if
> your effect does not listen at its defaults, add the `engage` lambda too.

The other: `goldenTests.cpp`'s `amplifiesRounding` lists the ten effects whose
decisions amplify one-ulp differences and are therefore held to a looser
tolerance — and its own note asks you to establish that a new entry is a decision
boundary rather than a bug before adding one.

> **The two fixture cases render in Release only.** `Golden fixtures` and
> `Side-chain fixtures` skip under `#ifndef NDEBUG` because the hash column is a
> same-build contract and a checked build does not render those bits. The rest of
> the suite runs in both, `Every effect leaves the output finite and bounded`
> included. Verify in both build directories.

## 1.11 What the layering forbids

- **No JUCE, and no host.** Everything under `src/le/` and `src/core/` (bar
  `core/host_interop/`) is inside `tests/checkNoJuceInDSP.cmake`'s roots, and it
  fails the `engine-links-no-juce` ctest if any of those translation units is
  compiled with `-DJUCE_MODULE_AVAILABLE_…` on its command line. It checks the
  *command*, not the includes, because the point is that a JUCE header **cannot
  be reached from here** rather than that nobody has reached for one yet. Anything
  needing `juce::File` or `juce::String` belongs in `sw-io` or above. `sw-dsp-tests`
  links no JUCE either, so the test for your effect cannot reach above the layer
  it tests.
- **No allocation in `process()`.** Storage comes from the engine's block via
  `ChannelState::resize()`, or from the stack via
  `LE_ALIGNED_SCOPED_STACK_BUFFER( name, type, count )`. `Colorifer`, `Smoother`,
  `Shifter`, `Octaver` and `Frecho` all use the latter.
- **No exceptions.** `effects.hpp:100` — the functions "must be no-fail". There
  is no `noexcept` on them because none was thought necessary; the requirement is
  the same.
- **No locks, no atomics, no messaging.** The audio thread owns the whole engine
  while it is activated — see `threading_model.md`. An effect that wants to tell
  the UI something has no channel to do it on, and inventing one is a change to
  that document, not to this one.

---

# 2. The inner DSP

## 2.1 What a frame is

`Processor::processSingleChannel` (`processor.cpp:295-430`) is the loop the whole
plugin is:

```
input FIFO fills to windowSize
  └─ copy + analysis window + FFT       ── channelBuffers.setCurrentDataToChannelData()
  └─ for each module: module.process()  ── your effect runs here
  └─ IFFT + synthesis window + overlap-add
  └─ scale by outputGain / wolaGain
  advance by stepSize
```

So `process()` sees **one windowed DFT frame of one channel**, and sees it again
`overlapFactor` times per `fftSize` samples of input. The numbers:

| | Meaning |
|---|---|
| `fftSize` | 128 … 8192, default 2048 (`engine/configuration.hpp:39-41`), user-selectable. Frame and window size. |
| `numberOfBins` | `fftSize / 2 + 1` — real FFT, DC through Nyquist inclusive |
| `stepSize` | `fftSize / overlapFactor` — how far the window moves per frame |
| `stepTime()` | `stepSize / sampleRate`, seconds between frames |
| `frequencyRangePerBin()` | `sampleRate / fftSize`, Hz per bin |

Bin *k* is centred on `k * sampleRate / fftSize` Hz. That is the whole of
"binning": there is no other resolution, and an effect asking for a frequency
gets the bin it lands in. `frequencyInHzToBin()` does the division.

Amplitudes are **not** normalised. `setup.maximumAmplitude()` is the reference,
and it moves with the FFT size.

Between the two windows the engine has already solved WOLA for you
(`processor.cpp:456-640`): Hann above overlap 2, `sqrt` below, and
Hann-divided-by-analysis as the general fallback. An effect never touches a
window and never compensates for one.

### The phase vocoder domain

Two of the 57 effects are not effects but mode switches. `To PV`
(`phase_vocoder_analysis/`) runs `PhaseVocoderShared::analysis` over the full
spectrum, which — per bin — takes the phase difference from the last frame,
subtracts the expected per-hop advance, unwraps to (−π, π], scales, and
**overwrites the phase array with the resulting true frequency**. `From PV`
(`phase_vocoder_synthesis/`) integrates it back into a phase.

The two were titled `PVD start` and `PVD stop` until 17.08.2026, and every
preset still names them that way — see `streaming_format.md` §6.

Between the two markers, `data.phases()` holds frequencies. That is the whole of
the PVD, and it is why there are `PitchShifter` / `PVPitchShifter` twins: the PVD
variant skips its own analysis and synthesis because the markers already did
them, which is much cheaper when several are chained.

`phase_vocoder/shared.hpp` is the shared machinery and the file to read before
writing anything pitch- or frequency-related:

| | What it gives you |
|---|---|
| `analysis()` / `synthesis()` | the transforms above, taking an `AnalysisChannelState` / `SynthesisChannelState` |
| `pitchShiftAndScale()` | resamples the bin array by a scale factor, scaling stored frequencies with it |
| `BaseParameters` | caches `freqPerBin`, `expctRate`, `deviationFactor` off `Engine::Setup` |
| `PitchShiftParameters` | `scaleFromSemiTonesAndCents()`, `skipProcessing()` |
| `PitchShifter` | analysis → shift → synthesis in one `process()`, with its `ChannelState` |
| `PVPitchShifter` | shift only, for inside the markers |
| `PitchShifterBasedEffect<Base, Helper>` | builds the PVD and non-PVD twins of one effect from one body |
| `StandaloneEffect<PVDEffect, SDKBase>` | wraps any PVD effect in analysis/synthesis to publish it standalone |

## 2.2 Taken apart: Bandstop

One parameter, one line of DSP, and everything else supplied. Base header —
`bandpass/bandpass.hpp`:

```cpp
namespace Detail
{
struct BandGain
{
    LE_DEFINE_PARAMETER( Attenuation, LinearFloat, Minimum<0>, Maximum<60>,
                                      Default<0>, Unit<" dB"> );
    LE_DEFINE_PARAMETERS( Attenuation );
};
}

struct Bandstop : Detail::BandGain
{
    static char const title[];
    static char const description[];
};
```

Note the shape: the shared parameter lives on `Detail::BandGain`, and `Bandpass`
and `Bandstop` are two structs with nothing but their own strings. That is how
one folder becomes two menu entries.

`setup()` does the one conversion:

```cpp
float const attenuation( dB2NormalisedLinear( -parameters().get<Attenuation>() ) );
...
attenuation_ = attenuation;
```

and `process()`, with the debug-only window path elided, is one line:

```cpp
void BandstopImpl::process( Engine::ChannelData_AmPh data, Engine::Setup const & ) const
{
    Math::multiply( data.amps(), attenuation_ );
}
```

Everything worth noticing is in what is absent. No loop over bins — `data.amps()`
is already the working range, and `Math::multiply` is vectorised. No dry/wet, no
gain, no bypass — the engine applies all three. No channel handling — the engine
runs this once per channel. No frequency arithmetic — `StartFrequency` and
`StopFrequency` became the range before `setup()` was called.

`Bandpass` is the same effect with the range inverted, and it is the one place
the inversion is deliberate:

```cpp
Math::multiply( attenuation_, data.full().amps().begin(), data.amps().begin() );
Math::multiply( attenuation_, data.amps().end(), data.full().amps().end() );
```

— attenuate from the start of the full spectrum up to the range, and from the
end of the range to the end of the spectrum. This is the canonical use of
`.full()`.

## 2.3 Taken apart: Shifter — bin translation

`Shifter` moves the spectrum along the frequency axis, which in a bin
representation is `memmove`. It is the clearest example of `setup()` doing the
thinking and `process()` doing the work.

`setup()` (`shifterImpl.cpp:39-53`) turns a percentage-of-bandwidth into a bin
count *relative to the working range*, and unpacks the mode into two booleans:

```cpp
int const offset( convert<int>( percentage2NormalisedLinear( parameters().get<Offset>() )
                              * convert<float>( workingRange.size() ) ) );
shiftLength_    = abs( offset );
positiveOffset_ = offset > 0;

ShiftTarget::value_type const mode( parameters().get<ShiftTarget>() );
magnitudes_ = ( mode == ShiftTarget::Both ) | ( mode == ShiftTarget::Magnitudes );
phases_     = ( mode == ShiftTarget::Both ) | ( mode == ShiftTarget::Phases );
```

`process()` is then three lines and a guard, and `shift()` is a `Math::move` plus
one of three tail policies (`Leave`, `Clear`, `Circular` — the last using a
`LE_ALIGNED_SCOPED_STACK_BUFFER` to hold the bins that wrap around).

Two things a new author should take from it:

- **The amplitude and phase arrays are independent.** `shift()` is called once
  per array and neither knows about the other. Shifting only phases is a
  different effect from shifting both, and the header notes that shifting both
  can cancel to silence at some offsets — a real property of moving sinusoids off
  their bin centres, not a bug.
- **The unit is "bw%", not Hz**, and the conversion uses `workingRange.size()`.
  So the same parameter value means a different number of bins when the user
  narrows the range, which is the intended behaviour and the reason `setup()`
  takes the range at all.

## 2.4 Taken apart: Blender — the side chain in one call

`blenderImpl.cpp`:

```cpp
void BlenderImpl::setup( IndexRange const &, Engine::Setup const & )
{
    amount_ = 1.0f - Math::percentage2NormalisedLinear( parameters().get<Amount>() );
}

void BlenderImpl::process( Engine::MainSideChannelData_ReIm data, Engine::Setup const & ) const
{
    auto const main( data.main().jointView() );
    auto const side( data.side().jointView() );
    Math::mix( main.begin(), side.begin(), main.begin(), amount_,
               static_cast<unsigned int>( main.size() ) );
}
```

`MainSideChannelData_ReIm` in the signature is the entire declaration that this
effect wants a side chain. `jointView()` (`buffers.hpp:220`) returns one span
covering both halves of the pair — legal here because the reals and imaginaries
are contiguous and the operation is identical on both, which is exactly why
`Blender` works in ReIm rather than AmPh: a linear crossfade of amplitudes and
phases is not a crossfade of the signal, and a crossfade of the rectangular form
is.

`data.side()` has no non-const accessor. There is no way to write to it.

## 2.5 Taken apart: Freqverb — history, and the whole-spectrum rule

`Freqverb` is the effect to read when you need memory. Its state is a single-tier
dynamic one:

```cpp
struct ChannelState : DynamicChannelState_<ChannelState>
{
    Engine::HalfFFTBuffer<> feedbackSumReals;
    Engine::HalfFFTBuffer<> feedbackSumImags;
    PhaseVocoderShared::PitchShifter::ChannelState ps;
    auto members() { return std::tie( feedbackSumReals, feedbackSumImags, ps ); }
};
```

Three declarations and one `std::tie`, and the engine sizes, places, clears and
resets all three.

The DSP is a per-bin feedback comb — each bin's feedback sum decays by
`dB2NormalisedLinear( -60 * secondsPerStep / time60dB )`, with an extra
per-bin ramp so highs decay faster — and `freqverbImpl.cpp:90-92` carries the
rule that every history effect needs:

> the feedback runs over the **full** spectrum while only the working range is
> written to the output.

Do it the other way round and moving `StartFrequency` mid-note leaves the
reverb tail holding bins that were never updated. `Reverser`, `Frecho` and
`Slicer` — the only three users in the tree — solve the same problem with
`data.copySkippedRanges()`, which copies the out-of-range bins into a
full-spectrum scratch buffer so the history stays whole.

## 2.6 The idioms, collected

| You want | Write | Seen in |
|---|---|---|
| a vector op on the working range | `Math::multiply( data.amps(), k )` | Bandstop |
| a per-bin loop | `while ( data ) { data.main().amps().front() …; ++data; }` | Denoiser, Merger, Burrito |
| a range-for | `for ( auto & amp : data.amps() )` | Freqnamics, Quiet Boost |
| every Nth bin | a `setup()`-computed offset + stride, one strided call | Phlip |
| chunked traversal | `amps.advance_begin( chunk )` in a loop | Quantizer |
| scratch memory | `LE_ALIGNED_SCOPED_STACK_BUFFER( buf, Engine::real_t, n )` | Smoother, Colorifer, Octaver |
| a scratch spectrum | `Engine::ChannelData_AmPhStorage` over such a buffer | Frecho, Octaver |
| per-channel memory | a nested `ChannelState` | §1.7 |
| fire every N frames | `ModuloCounterChannelState` + `nextValueFor( period )` | Burrito, Pitch Spring |
| frame history | `HistoryBuffer<float, ms>` / `ReversedHistoryChannelState<ms>` | Reverser, Frecho |
| to keep history coherent outside the range | `data.copySkippedRanges( DataPair::Amps, buf )` | Reverser, Frecho, Slicer |
| dB → amplitude | `setup.maximumAmplitude() * Math::dB2NormalisedLinear( dB )` | Freqnamics, Merger |
| Hz → bin | `setup.frequencyInHzToBin( hz )`, clamped to the range | AhAh, Smoother |
| ms → frames | `setup.milliSecondsToSteps( ms )` | Pitch Spring, Slicer |
| pitch shift | inherit `PhaseVocoderShared::PitchShifter` or hold one as a member | Armonizer, Octaver |
| two menu entries, one body | `Detail::XBase` + two structs with their own `title[]` | Bandpass, Tonal, ExImploder |
| PVD and non-PVD twins | `PitchShifterBasedEffect<Base, PitchShifter\|PVPitchShifter>` | Pitch Follower, Pitch Magnet |
| mark a hot path | `LE_HOT` + `LE_OPTIMIZE_FOR_SPEED_BEGIN/END()`, `LE_COLD` on `setup()` | Colorifer |

### Three traps with receipts

**Exact zeros arrive.** A preceding Bandpass, Sharper or Denoiser leaves
amplitudes at exactly 0. `Exaggerator` raises amplitudes to a user-chosen
exponent; `pow( 0, negative )` is `+inf`, which poisoned its normaliser to NaN,
and `pow( 0, 0 )` is 1, which un-muted bins that had been silenced. Four factory
presets rendered NaN. The fix and the comment explaining it are at
`exaggeratorImpl.cpp` (02.08.2026) and are the model for documenting an edge
case.

**Running sums drift.** `Math::symmetricMovingAverage` carries a sum across
thousands of bins, and a bin below the ulp of that sum vanishes into it and is
then subtracted back out — leaving a residue no later term removes, so a
non-negative input comes back negative and stays that way for the rest of the
buffer. Pass `forcePositive` whenever the input is a magnitude, as `Smoother`,
`Sharper` and `Vocoder` do. See issue #84.

**A stride is not a count.** `Math::negate( range, stride )` is the only strided
primitive and `Phlip`'s Even and Odd modes are its only caller. On Apple it
forwards to `vDSP_vneg`, which wants the number of *elements* to negate, and the
range's float count was passed instead — so a stride of 2 wrote a range's worth
past the end, into the next buffer of the shared storage, where the side
channel's amplitudes live. `Ethereal` copied those back over the main channel's
and a checked build asserted. An overrun inside one arena is the class a
sanitizer cannot see; what caught it was two factory presets. The two backends
had disagreed since 2011 and only Apple was wrong. See issue #10.


---

# 3. The inventory

Three populations, and the difference between them is what it costs to touch one.

| | Count | In `effectsList.hpp` | In `dsp.cmake` | What a change to one costs |
|---|---:|---|---|---|
| [Shipped](#31-the-57-shipped-effects) | 57 | yes | yes | the compiler and the whole suite check you |
| [Orphaned](#32-the-four-in-the-tree-and-in-no-target) | 4 | no | no | **nothing checks you** — no compiler has seen them |
| [`_unfinished/`](#33-_unfinished--sixteen-effects-and-a-matlab-script) | 17 | no | no | nothing checks you, and none of them would compile |

## 3.1 The 57 shipped effects

In `effectsList.hpp` order, which is the index order, which is ABI. 46 `.cpp`
files back 57 entries: nine folders contribute more than one.

Columns: **P** = effect-specific parameters (plus the five base ones every module
has). **State** = per-channel memory — `—` none, `S` static, `MC` modulo counter,
`D` dynamic (engine-allocated buffers), `PV` phase-vocoder state, `PD` pitch
detector. **SC** = reads the side chain, per the measured set in
`sideChainTests.cpp` — measured by rendering, not read off a declaration (§1.8).

### Pitch — indices 0–6

| # | Title | Folder | P | State | SC | What it does |
|---:|---|---|---:|---|:-:|---|
| 0 | Pitch Shifter | `pitch_shifter` | 2 | PV | | Semitones + cents through the full analysis/shift/synthesis chain. Declares only `setup()`; `process()` is inherited from `PhaseVocoderShared::PitchShifter`. |
| 1 | Pitch Follower | `pitch_follower` | 1 | PD+PV | ✔ | Detects the pitch of main and side, shifts main to match, slew-limited by `Speed` semitones/second. |
| 2 | TuneWorx | `tune_worx` | 13 | PD+PV | | Snaps the detected pitch to a user-selected chromatic scale. The only effect past the ten-parameter host ceiling — see the note below. |
| 3 | Pitch Magnet | `pitch_magnet` | 2 | PD+PV | | Pitch Follower with a constant target frequency instead of a detected one. Carries a `\todo` about the duplication. |
| 4 | Sumo Pitch | `sumo_pitch` | 2 | PD+PV(×2) | ✔ | Detects both channels' pitch, drags each toward their mean, mixes. The only effect that builds a whole scratch `ChannelData_AmPhStorage` on the stack. |
| 5 | Pitch Spring | `pitch_spring` | 3 | MC+PV | | Sinusoidal pitch LFO via the `VibratoEffect` mixin. |
| 6 | Octaver | `octaver` | 5 | D(PV×2) | | Two independently gained pitch-shifted voices summed in ReIm, then low-passed. |

### Timbre — indices 7–14

| # | Title | Folder | P | State | SC | What it does |
|---:|---|---|---:|---|:-:|---|
| 7 | Bandpass | `bandpass` | 1 | — | | Attenuates everything **outside** the working range. The one deliberate inversion. |
| 8 | Bandstop | `bandpass` | 1 | — | | Attenuates everything inside it. Two lines. |
| 9 | Ah-ah | `ah_ah` | 3 | — | | A half-sine gain bump of `Width` Hz centred on `Center` Hz. The clean example of Hz→bin clamping. |
| 10 | Smoother | `smoother` | 1 | — | | Symmetric moving average of the amplitudes. One parameter, one call — and the effect that asserts in debug (§2.6). |
| 11 | Sharper | `sharper` | 3 | — | | The inverse: amplitude + `Intensity` × (amplitude − smoothed), clamped. |
| 12 | Centroid | `centroid_extractor` | 3 | PD | | Adaptive bandpass around the spectral centroid, the strongest peak, or the detected pitch. |
| 13 | Tonal | `tonal` | 4 | — | | `PeakDetector::attenuateNonPeaks` — keeps the partials, kills the rest. |
| 14 | Atonal | `tonal` | 4 | — | | The same detector, `attenuatePeaks`. Redefines three of the four defaults. |

### Time — indices 15–20

| # | Title | Folder | P | State | SC | What it does |
|---:|---|---|---:|---|:-:|---|
| 15 | Freeze | `freeze` | 3 | D+PV | | Trigger-driven four-state machine cross-fading between live, newly frozen and previously frozen spectra. Manual `analysis()`/`synthesis()` bracketing. |
| 16 | Slicer | `slicer` | 3 | D+MC | ✔ | Chops on a duty cycle and fills the gap with a held frame, silence, or the side channel. **`Mode` defaults to `Hold`, so it does not touch the side chain at defaults.** |
| 17 | Wobbler | `wobbler` | 3 | MC | | One sinusoidal gain per frame. The cheapest possible stateful effect — no per-bin work at all. |
| 18 | Reverser | `reverser` | 1 | history | | Ping-pongs a step counter through a history buffer to play frames backwards, negating phases to reverse each frame. **The reference history effect.** |
| 19 | Imploder | `eximploder` | 4 | D+PV | | Accumulator that decays magnitudes and glissandos frequencies, taking the live bin when it is louder. |
| 20 | Exploder | `eximploder` | 4 | D+PV | | The same with the threshold comparison inverted and three defaults changed. |

### Space — indices 21–23

| # | Title | Folder | P | State | SC | What it does |
|---:|---|---|---:|---|:-:|---|
| 21 | Frecho | `frecho` | 3 | D+MC+PV | | Frequency-domain echo: a history ring, pitch-shifted per repeat. |
| 22 | Frevcho | `frecho` | 3 | Frecho + reversed history | | Frecho with the history traversed backwards. Declares no parameters of its own. |
| 23 | Freqverb | `freqverb` | 4 | D+PV | | Per-bin feedback comb with an HF-decay ramp, then pitch shift and phase randomisation. **The clean single-tier `DynamicChannelState_`.** |

### Phase — indices 24–27

| # | Title | Folder | P | State | SC | What it does |
|---:|---|---|---:|---|:-:|---|
| 24 | Robotizer | `robotizer` | 0 | — | | `Math::clear( data.phases() )`. One line. |
| 25 | Whisperer | `whisperer` | 0 | S | | Randomises every phase. Three lines. **The smallest `ChannelState` there is: a `Math::Rng` and nothing else.** |
| 26 | Phasevolution | `phasevolution` | 1 | S | | An accelerating phase ramp added to every bin. The clean `StaticChannelState`. |
| 27 | Phlip | `phlip` | 1 | — | | Negates the phase of every, every even, or every odd bin — one strided call, parity computed in `setup()`. |

### Loudness — indices 28–32

| # | Title | Folder | P | State | SC | What it does |
|---:|---|---|---:|---|:-:|---|
| 28 | Gain | `gain` | 0 | — | | **Nothing.** Both functions are empty; the audible gain is the base `Gain` parameter. The minimal legal effect. |
| 29 | Exaggerator | `exaggerator` | 1 | — | | Raises normalised amplitudes to an exponent, renormalises. Read its zero-guard comment (§2.6). |
| 30 | Denoiser | `denoiser` | 2 | — | ✔ | Spectral subtraction against a footprint taken from main, side, or their mean. **`Mode` defaults to `Main`.** |
| 31 | Quiet Boost | `quiet_boost` | 3 | — | | Upward expansion between a noise gate and a threshold. Has an empty `setup()` and does its dB conversions in the loop — its own `\todo` says that is a mistake. |
| 32 | Freqnamics | `freqnamics` | 2 | — | | `amp = (amp < gate) ? 0 : min( amp, limit )`. The whole body. **The smallest effect that has a parameter and does work** — 62 lines of `.cpp`, and the skeleton to copy. |

### Combine — indices 33–42

Ten effects, nine of which read the side chain.

| # | Title | Folder | P | State | SC | What it does |
|---:|---|---|---:|---|:-:|---|
| 33 | Talking Wind | `talking_wind` | 2 | — | ✔ | Cepstral vocoder: main is the modulator, side the carrier. **The reference side-chain effect**, and it says its polarity in a comment. |
| 34 | Convolver | `convolver` | 3 | D | ✔ | Multiplies main by the live or trigger-frozen side spectrum. **Defaults to the non-side mode.** |
| 35 | Ethereal | `ethereal` | 3 | — | ✔ | Replaces main bins with side bins where a level comparison holds. Pre-selects sources outside the loop to stay branchless. |
| 36 | Vaxateer | `vaxateer` | 3 | — | ✔ | Eight comparison modes against an RMS threshold, decoded into four range cursors before the loop. |
| 37 | Shapeless | `shapeless` | 1 | — | ✔ | Transfers the side channel's spectral shape onto main, chunk by chunk. |
| 38 | Colorifer | `colorifer` | 3 | — | ✔ | Block-wise energy ratio transfer with optional square/sqrt/exp preprocessing. The `LE_HOT`/`LE_COLD` reference. |
| 39 | Merger | `merger` | 2 | — | ✔ | Six conditions collapsed to two range pointers and one branchless loop. |
| 40 | Blender | `blender` | 1 | — | ✔ | One `Math::mix` over a `jointView()` of the ReIm pair. §2.4. |
| 41 | Inserter | `inserter` | 4 | — | ✔ | Blits a band of the side spectrum into main at a chosen destination. |
| 42 | Burrito | `burrito` | 4 | MC+D | ✔ | Replaces or sums side bins at randomly chosen positions, re-rolled every `Period`. **The positions only change once the period wraps** — and at the default `Period` of 250 ms and `Replace` mode, a fixture shorter than that with side == main sees nothing at all. |

### Phase Vocoder — indices 43–51

Nine entries. Two are domain markers; the other seven are the PVD twins of
effects that also ship standalone, and they exist because inside the markers the
analysis and synthesis have already been paid for.

| # | Title | Folder | P | State | SC | What it does |
|---:|---|---|---:|---|:-:|---|
| 43 | To PV | `phase_vocoder_analysis` | 0 | analysis | | `PhaseVocoderShared::analysis` over `data.full()`. After it, `phases()` holds frequencies. |
| 44 | Pitch Shifter (PV) | `pitch_shifter` | 2 | — | | Shift only. No channel state at all. |
| 45 | Pitch Follower (PV) | `pitch_follower` | 1 | PD | ✔ | |
| 46 | TuneWorx (PV) | `tune_worx` | 13 | PD | | |
| 47 | Pitch Magnet (PV) | `pitch_magnet` | 2 | PD | | |
| 48 | Pitch Spring (PV) | `pitch_spring` | 3 | MC | | |
| 49 | Imploder (PV) | `eximploder` | 4 | D | | The standalone Imploder is this wrapped in `StandaloneEffect`. |
| 50 | Exploder (PV) | `eximploder` | 4 | D | | |
| 51 | From PV | `phase_vocoder_synthesis` | 0 | synthesis | | `PhaseVocoderShared::synthesis` over `data.full().phases()`. |

### Miscellaneous — indices 52–56

| # | Title | Folder | P | State | SC | What it does |
|---:|---|---|---:|---|:-:|---|
| 52 | Armonizer | `armonizer` | 1 | PV | | A pitch shifter in intervals. Declares no `process()` at all — 46 lines of `.cpp`. |
| 53 | Slew Limiter | `slew_limiter` | 2 | D | | Clamps each bin's frame-to-frame gain. The canonical one-frame history buffer. |
| 54 | Shifter | `shifter` | 3 | — | | Translates the spectrum along the frequency axis. §2.3. |
| 55 | Swappah | `swappah` | 4 | — | | Permutes three bands of the **main** channel. Despite the name, nothing to do with the side chain. |
| 56 | Quantizer | `quantizer` | 2 | — | | Staircases the amplitude envelope in fixed-width blocks, `Origami` interpolating between the steps. |

### Two notes on the shipped set

**TuneWorx is the cut-down edition, and the implementation is what says so.**
It declares `Key` and `Semi01`…`Semi12`, which is exactly what `setup()` and
`findNewPitchScale()` read. The framework's Tune Worx had 21 more — `SpringType`
("Direction"), twelve `BypassSemi*`, the vibrato section, the pitch range,
`TuneTolerance`, `RetuneTime` and `PitchShift` — behind `LE_SIMPLE_TUNEWORX` in
the header and `LE_SW_SDK_BUILD` in the implementation, neither of which the
plugin ever set. Between 27.07 and 11.08.2026 the header's half came across and
the implementation's did not, and the result shipped: 21 parameters no DSP reads,
a second family of controls named "Bypass" next to the module's own, and
`Direction` at index 6 costing `Semi04` its automation. \see `tuneWorx.hpp`.

What remains from that edition is still visible in the code: `findNewPitchScale()`
opens with the literal placeholders `unsigned int const vibratoPitch(1); unsigned int const pitchShift_(1);`,
hardcodes the pitch search to 70 Hz over five octaves, and passes
`userBypassedTones(0)`. TuneWorx is also the only effect that exceeds the
ten-parameter host ceiling (§1.4): `Semi05`…`Semi12` work and cannot be
automated.

**Four of the fifteen side-chain effects hear nothing at their defaults.**
Slicer, Denoiser and Convolver each have an enumerated `Mode` whose *first*
enumerator — and therefore its default — is the one that ignores the side chain,
and Burrito's positions are only chosen once its 250 ms period wraps.
`sideChainTests.cpp` carries the smallest parameter change that makes each listen.
If you write a side-chain effect, put the side-chain mode first or accept that
your own test at default values cannot fail.

## 3.2 The four in the tree and in no target

`vocoder`, `synth`, `talk_box` and `dissonancizer` sit alongside the shipping
effects, in no build target and in no list. 12 files, 1,859 lines.

**Not port leftovers.** The 2016 `effectsList.cmake` already ended its Misc block
with three of them commented out:

```cmake
  #"synth,synth,Synth,Misc"
  #"talk_box,talkBox,TalkBox,Misc"
  #"vocoder,vocoder,Vocoder,Misc"
```

`dissonancizer` was never listed at all. What the port did to all four is textual
— clang-format, nested namespaces, the Boost.Preprocessor-free parameter macros,
and a by-hand MATLAB strip that deleted 47 lines across `synthImpl.cpp` and
`vocoderImpl.cpp`. **No compiler has seen the result.**

They divide sharply, and the divide is what makes them worth separating from
`_unfinished/`.

### `vocoder`, `synth`, `talk_box` — modern shape, never wired up

All three already have the `x.hpp` / `xImpl.hpp` / `xImpl.cpp` split, the current
parameter macros, the current signatures and the current channel-state types.
Finishing one means steps 1–6 of §1.9 and nothing else — plus whatever a compiler
says the first time one sees them.

| | Title / description | Parameters | Shape |
|---|---|---|---|
| `vocoder/` | "Vocoder" — *"Classic vocoding."* | `EnvelopeBorder`, `NoiseIntensity`, `FilterMethod` (7 estimators) | `MainSideChannelData_AmPh`, no channel state. Main is the envelope, side the carrier. Seven selectable envelope estimators: three cepstral windowings, a moving average, and two RC one-pole followers run forward-then-backward across the bins. **Talking Wind is essentially this file's shipped subset.** |
| `synth/` | "Synth" — description is a copy-paste of Colorifer's and describes nothing it does | `Frequency`, `HarmonicSlope`, `FlangeIntensity`, `FlangeOffset` | `MainSideChannelData_AmPh`. An **instrument**: additive synthesis written *into* the side channel through a `const_cast`. `setup()` FFTs one windowed sine to capture a 3-tap leakage kernel; `process()` deposits each harmonic smeared by that kernel, with per-bin phase continuity in a hand-rolled `ChannelState` (an array member, so `reset()`/`resize()`/`requiredStorage()` are written out by hand). |
| `talk_box/` | "Talk Box" — *"Classic vocoding with a synthesized carrier."* | `ExternalCarrier`, `BaseFrequency`, `CutOff` + three re-exported from Synth | `ChannelData_ReIm2AmPh`. **An effect built out of two other effects**: holds a `SynthImpl` and a `VocoderImpl` as members and pushes its own parameters into their `parameters()`. Its `ChannelState` is `SynthImpl::ChannelState`, with a commented-out `CompoundChannelState<Vocoder::ChannelState, SynthImpl::ChannelState>` above it — a visible loose end, next to a live `#if 0` branch. |

**One of them will not compile, and the reason is §1.4.** `vocoder`'s
`EFFECT_PARAMETER_NAME` and `EFFECT_ENUMERATED_PARAMETER_STRINGS` are in
**`vocoderImpl.hpp:53-64`**, not in `vocoder.hpp`. It is the only `*Impl.hpp` in
the tree that carries them; the other 45 effects put them in the base header,
and `allEffects.hpp` — which is what the parameter-table translation unit
(`plugin2Host.cpp`) includes — pulls base headers only. So the specialisations
would not be visible where they are needed, which the baseline makes a hard
error. Moving four lines up into `vocoder.hpp` fixes it, and it is the first
thing a compiler would have said if one had ever been pointed at the file.

Two more things a reviver must decide rather than type:

- **Both `vocoder` and `synth` write to `data.side()` through a `const_cast`,**
  breaking the read-only rule in §1.8. `vocoderImpl.cpp` admits it in a comment —
  the noise injection "does not play nice with downstream effects that also use
  the side chain". `synth` does it structurally, because generating into the side
  bus is what the effect *is*. Neither is legal under the contract as written;
  one of the two has to give.
- **`talk_box` is the argument for the other two.** It is the only consumer of
  `synth`, and it is the composition pattern nothing shipped demonstrates.
  Reviving `vocoder` and `synth` without it revives two halves of one idea.

### `dissonancizer` — legacy, and a full port

`dissonancizer.hpp` + `dissonancizer.cpp`, and **no Impl split**: one class,
`Dissonancizer`, with its DSP in the `.cpp`. That is not an oversight — it
predates the convention. It is written against the 2009 `LE::Algorithms` API —
`DISCRETE_VALUES_PARAMETER`, `DEFINE_PARAMETERS`, `canUseTwoInputs`,
`setup( EngineSetup const &, Parameters const & )`, `process( ChannelData_AmPh & )`,
and four of its five includes name paths that exist nowhere. Even the peak
detector it wants is still there (`le/analysis/peak_detector/`, and in the build)
but every name under it has moved: it calls
`findPeeksAndStrengthSortAndEstimateFrequency`, `getNumPeeks`, `getPeek` and
`Peak::start_pos`, against today's `findPeaksAndEstimateFrequency`, `getNumPeaks`,
`getPeak` and `Peak::startPos`. It also hardcodes 44100.

**Not one line of it compiles.** It is the only file outside `_unfinished/` in
that state, it is indistinguishable from what is in there, and reviving it is the
§3.3 port rather than the §3.2 one. Moving the directory would be honest.

The DSP: peak-detect, then copy each peak's bin block to
`start + 2.27·start^0.477 + 0.5` — the critical-band offset for maximum
roughness — summing, averaging or replacing per a `TargetCreation` mode. Its own
`\todo` names what is missing: *"Implement phase correction according to
Laroche&Dolson. Without phase correction this algo is not complete."*

One file in these four directories is not C++ and not build input.
`vocoder/cepstrum.m` is the design sketch and the visual oracle for
`lowPassSpectrum_cepstrum`: it builds and plots the same four liftering windows
the C++ implements, including the doubled one-sided window from Zölzer's *DAFX*
that `FilterMethod::CepstrumUdoBrick` is named after. It uses `wavread` and
`vec2mat`, so it predates MATLAB R2015 and would need updating to run.
`_unfinished/phase_locked_vocoder/` is the same kind of artefact, and the only
other one in the tree.

> **Nothing compiles these, so nothing checks them.** Their MATLAB scaffolding
> was removed by hand and no compiler has seen the result. Whoever revives one
> starts by getting it into a target — add the `dsp.cmake` line and build before
> reading another word of the DSP.

## 3.3 `_unfinished/` — sixteen effects and a MATLAB script

3,908 lines across 33 files. `old/initial_scan.md` says read before deleting.

**None of them is a "compiles-ready but unregistered" effect.** All sixteen C++
ones are written against the same dead 2009–2010 API `dissonancizer` uses, and
git records only four commits over the whole directory — `Move to the target
repository layout`, `Convert to UTF-8, relicense…`, `Replace the mechanical Boost
usage…`, `Spell nested namespaces the C++17 way…`. All four are mechanical
sweeps. **Nobody has touched the DSP.**

So the useful question is not "how far along is it" — they are all equally far
from compiling — but "how much real DSP is in the file, and what did the author
say was wrong with it".

### The port tax, paid once per effect

| What it says | What it must become |
|---|---|
| `namespace LE::Algorithms` | `namespace LE::SW::Effects` |
| one class | `X` in `x.hpp` + `XImpl : EffectImpl<X>` in `xImpl.hpp` |
| `DEFINE_PARAMETERS( (( Name )( float )( MinimumValue<0> )…) )` | `LE_DEFINE_PARAMETER( Name, LinearFloat, Minimum<0>, … )` + one `LE_DEFINE_PARAMETERS( … )` |
| `DISCRETE_VALUES_PARAMETER` / `DISCRETE_VALUE_STRING` | `LE_ENUMERATED_PARAMETER` + `EFFECT_ENUMERATED_PARAMETER_STRINGS` — **no mechanical equivalent; read a shipped one** |
| `UIElements<X>::name_[]`, or `UI_NAME( X ) = "…"` | `EFFECT_PARAMETER_NAME( X, "…" )` — note the two arguments |
| `canUseTwoInputs` / `canSwapChannels` | nothing — a `MainSideChannelData` in the `process()` signature (§1.8) |
| `setup( EngineSetup const &, Parameters const & )` | `setup( IndexRange const &, Engine::Setup const & )` |
| `process( ChannelData_AmPh & )` | `process( [ChannelState &,] Engine::ChannelData_AmPh, Engine::Setup const & ) const` |
| `ChannelState::clear()` | `reset()`, plus `resize()` / `requiredStorage()` |
| `Common::SSEAlignedHalfFFTBuffer` | `Engine::HalfFFTBuffer<float>` |
| `InclusiveIndexRange` | `Effects::IndexRange` — **and it is half-open now** |
| `LE_CONFIGURATION_MAX_FFT_SIZE` | gone; size out of `StorageFactors` instead |
| `ydsp.h`, `fastmath.h`, `fwSignal.h`, `Mult_Vec_Scal`, `PowApprox_Vec_Vec`, `fwsAddC_32f_I`, `fastMath::magnitude3`, `LIMIT0` | reimplement in `le/math`, or drop |

Eight of them declare `canUseTwoInputs = false` while reading
`data.sideChannelAmplitudes`. Mislabelled side-chain effects are the norm here,
as they are in the shipped set (§1.8).

### Delete rather than port — 2

| | Why |
|---|---|
| `pv_pitchshift` | **Already shipped.** `effectsList.hpp:105` is this effect: `x( pitch_shifter, pitchShifter, PVPitchShifter, PVD )`. Two one-line functions, nothing to recover. |
| `old_phase_vocoder` | **Superseded.** `effects/phase_vocoder/shared.{hpp,cpp}` is its successor, is in the build, and is what every shipped PVD effect uses. Also has a real bug: `pitchShiftAndScale()` asserts against two uncleared stack buffers and `memcpy`s uninitialised bins out. |

### Substantially complete DSP, needing only the port — 5

The port tax and a review; the algorithm is all there.

| | Title / what it does | The catch |
|---|---|---|
| `operations` | "Operations" — Add / Sub / InvSub of main and side in ReIm, with a side gain | The smallest port here. Functionally overlapped by the shipped Merger/Blender/Inserter. One `\todo`: *"Vectorize!!!"* |
| `pv_imploder_side` | "Side Imploder (pvd)" — a two-input Imploder with independent decay and glissando per channel | No `\todo` anywhere. Its own banner names a file that does not exist (`pvAccumulator.cpp`). |
| `sim_octaver` | "Sim Octaver" — dry + one octave down + one octave up, low-passed | Constructs **two full `ChannelData_AmPh` temporaries on the audio-thread stack inside `process()`** — the thing its own siblings' comments warn about. |
| `sub_octaver` | "Sub Octaver" — dry + −12 and −24 semitones | Near-duplicate of `sim_octaver`, **cloned without renaming**: its include guard carries `sim_octaver`'s GUID and its `#endif` names a third file. Also bakes in the 350 Hz output low-pass that issue #15 criticises in the shipped Octaver. |
| `mirror` | "Mirror" — reflects bands across the spectrum: `abcdefgh…` → `abbaeffe…` | **`Mode` is used but never declared.** The parameter list names it, `process()` switches on `Mode::Magnitudes`/`Phases`/`Both`, and no declaration exists — today that is `CommonParameters::Mode` + `UnpackedMagPhaseMode`. `mirror()` is also off by one and self-overlapping. |

### Complete DSP, but the author documented a problem — 5

| | Title / what it does | What the author said |
|---|---|---|
| `morphological_mutations` | "Mormut" — six morphological mutation modes between main and side | Two blocking `\todo`s, both Danijel Domazet, 11.03.2010: *"Do some kind of normalization here to avoid ovreflow&underflow"* and *"Handle phase here in some way"* — **it never touches phases at all.** `normalize()` is defined and both call sites are commented out, and it needs `fwsAddC_32f_I` from FrameWave. An inline note admits the trade-off: *"If Mj is saved here, then it's without normalization - unstable!"* Also compares the side against the *main* channel's history, which looks like a bug. |
| `randomizer` | "Randomizer" — random per-block amplification | A class-level `\todo` saying the whole approach is wrong: *"Randomizer would make sense in a 'New phase-vocoder technique…' presented by Laroche and Dolson… where only peaks would be shifted to random locations."* **And it hangs**: the outer loop's only increment is inside the inner one, so `BlockSize` at its minimum of 0 never terminates. |
| `reverbator` | "Reverbator" — two reverb modes, one named after `takuramu` | **The reference formula is commented out and an ad-hoc one is live.** The DAFX line `y(n) = -g·x(n) + x(n-D) + g·y(n-D)` sits disabled above the substitute that runs. Magnitudes only, phases untouched. Header has a duplicated `private:` label. |
| `tai` | "Tai" — product of two frequency-domain compressed signals | No `\todo`, and two hard numerical faults: `20·log10( amp / maxAmplitude_ )` with no epsilon, so a silent bin gives −inf and propagates; and the final step multiplies two absolute amplitudes with no renormalisation, so output scales as amplitude². The gain staging was never designed. |
| `transients_extractor` | "Extrans" — transient extraction, provenance possibly Loris | `\todo Find out how "fastMath::magnitude3 works.` — and `fastMath::magnitude3` does not exist in this tree, so it is both a `\todo` and a dependency to reimplement. Carries a bare `// Why?` under its own complex-product derivation, and the author's note: *"I can't remember where I took it from, perhaps from emails with Loris author."* |

### The spec and the code disagree — 2

These need a decision before a line is typed: there are two designs in the
directory and only one of them is implemented.

| | Title | The disagreement |
|---|---|---|
| `morpheus` | "Morpheus" — adaptive spectral morphing; algorithmically the most complete file here (peak sort, then blend around the strongest bins) | Its 30-line note from Danijel Domazet (21.01.2010) specifies **seven modes** and a `Fastest` quality switch. The code ships two orthogonal enums (6 × 3) and no `Fastest`. The note also ends mid-research: *"I read in dafx papers, that people doing Neural network for harmonic extraction. I'll provide you guys with papers and research that i didn't finished on this area."* |
| `rand_blend` | "Rand Blend" — but Alex's note calls it **"Turnupeer"** | The note describes an adaptive effect that *"builds a special grid based on spectral bin relations"* and updates those relations during processing. **`process()` builds no grid and updates nothing** — it re-rolls `Math::rangedRand( num_bins_ )` fresh every frame, so it is memoryless noise. The note claims five modes; three are implemented. |

### Skeletons — 2

| | Title | State |
|---|---|---|
| `slow_motion` | "Slow Motion" | The control flow is mid-debug: a whole disabled `if` block, two `else` branches whose only content is a comment, and a `reached[]` flag set `true` on one path, `false` on another and **never reset on the third**. `clear()` does not clear `reached[]` either, so it is indeterminate on the first block. `Step`'s meaning is undocumented. Overlaps the shipped Slew Limiter. |
| `takuramu` | "Takuramu" — *"Hideki's Takuramu"*, attributed to Lars Hamre | **Genuinely non-functional.** `basePitchScale_` is read in the inner loop and **never written** — computing it is the missing function. `pitch_` is stored by `setup()` and never used, so the `Pitch` knob does nothing. The whole "find base frequency" block computes three values and uses none of them. Also reads one element past the end of the amplitude array. Carries a bare `\todo Fix this.` and four more, including *"Thoroughly rewrite all this. Its very bad…"* |

### Not C++ at all — 1

`phase_locked_vocoder/phase_locked_vocoder.m` — a 130-line MATLAB prototype of
Dolson's phase-locked vocoder: peak detection by descending magnitude, regions of
influence between adjacent peaks, spline interpolation, phase coherence via
`dw = (scale - 1)·(peakbin - 1)/n·2π`. No class, no title, no parameters —
`overlap = 4`, `n = 1024`, `scale = 1.6` are hardcoded, and it opens
`c:/le/media/chaotica.wav`. Two degenerate cases are marked with a debug print
rather than handled.

It is the only `_unfinished` file referenced from anywhere outside the directory:
`/.clang-format-ignore:3` exempts it, because clang-format mangles `.m` files.

There is no C++ to port. Whoever wants this writes the effect; the script is the
paper's algorithm in runnable form and that is its whole value.

---

## Where to start

| If you are | Read, in this order |
|---|---|
| writing a new stateless effect | `effects.hpp:48-124`, `freqnamics/`, `bandpass/`, then §1.9 |
| writing one with per-channel memory | `phasevolution/` (static), `wobbler/` (counter), `slew_limiter/` (one buffer), `octaver/` (dynamic) |
| writing a side-chain effect | `talking_wind/`, then `blender/`, then §1.8 and `sideChainTests.cpp` |
| writing anything pitch- or frequency-related | `phase_vocoder/shared.hpp` **first**, then `pitch_follower/` for the PVD/non-PVD twin pattern |
| writing an effect that needs history | `historyBuffer.hpp`, `reverser/`, `freqverb/`, and the whole-spectrum rule in §1.5 |
| reviving one of the four orphans | get it into `dsp.cmake` and build it. Nothing else until it compiles. |
| reviving one from `_unfinished/` | check §3.3 for whether it is already shipped, then pay the port tax in one commit and read the DSP in the next |
