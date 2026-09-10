# SpectrumWorx — mono ports

How wide the plugin is, who decides it, and why a request that moves only some
of the ports is refused.

Written 31.08.2026, revised 09.09.2026. Everything here is in the tree and has
tests naming it.

---

## 1. One width, every port

SpectrumWorx has **a single channel width** — 1 or 2 — and all three ports carry
it: main in, side chain in, main out. There is no arrangement in which they
differ, and that is a property of the engine rather than a simplification of the
host interface:

- `Engine::Setup::setNumberOfChannels()` asserts `numberOfSideChannels <=
  numberOfMainChannels`. A stereo side chain against a mono main is not
  representable.
- `SpectrumWorxCore::checkChannelConfiguration()` accepts only `in == out` and
  `in == 2 * out`. The engine takes the side channels to be the *difference*
  between the input and output counts, which is why `activate()` asks for
  `setNumberOfChannels(2 * width, width)`.
- An effect reads the two spectra channel for channel — `effect_contract.md` §1.8
  — so a side channel with a channel the main one has not got has nowhere to go.

So the whole layout is one number, `SpectrumWorxCLAP::channelWidth_`, and
`audioPortsInfo()` reports it for every port along with `CLAP_PORT_MONO` or
`CLAP_PORT_STEREO`.

**Two ports in, one out, at both widths.** Mono does not remove the side chain,
it halves it. `audioPortsCount()` is unchanged and does not consult the width.

### 1.1 A layout that would leave the ports disagreeing is refused

`widthRequestedBy()` starts from the current widths, applies whatever the
requests name, and refuses unless all three agree. **Including a request that
does not name the side chain at all**: the extension says a port the requests do
not name keeps what it has, so asking a stereo plugin for a mono main and saying
nothing about the side chain still describes a mono/stereo pair.

Refusing is the only honest answer, and it is the *point*:

> Once the configuration is successfully applied … it isn't necessary for the
> host to scan the audio ports.
> — `clap/ext/configurable-audio-ports.h`

That is the whole guarantee the extension makes. A `true` from
`can_apply_configuration` tells the host it need not look again, so answering yes
and then producing some other layout leaves it holding a picture it has been told
to trust. This plugin did exactly that until 09.09.2026 — it ignored the
side-chain request and took every port to the main width — and clap-validator's
`layout-configurable-audio-ports` is what caught it.

## 2. Stereo until a host says otherwise

`channelWidth_` starts at 2 and nothing in a patch moves it. A host that never
asks gets exactly the plugin that existed before this document, which is what the
`[audio-ports]` case *A plugin nobody has configured is stereo throughout* holds.

**It is not streamed.** Bus topology is a handshake between the plugin, the host
and the track, not a thing a preset gets an opinion about — the same reasoning
that keeps the side-chain *source* out of the parameter set for the opposite
reason (`sidechain-approach.md` §5). A session that reloads gets the width from
the host asking again, which every host that asked once does.

## 3. The one thing that moves it

`clap.configurable-audio-ports`, which is a *push*: the host names ports and
channel counts, and the plugin says yes or no to the set as a whole.
`can_apply_configuration` and `apply_configuration` are both `[main-thread &
!active]`, so the layout can only change between activations and there is no
audio thread to race — the same window `deactivate()` applies a pending FFT size
in, and for the same reason.

Nothing is announced. The extension says a host that gets a `true` need not
rescan the ports, and `request_restart()` would be asking for a deactivation the
plugin is already inside.

### The rule

`SpectrumWorxCLAP::widthRequestedBy()` starts from the widths the ports have,
applies whatever the requests name, and answers with the width they agree on — or
with nothing:

```
a port index that does not exist    -> refused
a count outside 1..2                -> refused
any two ports disagreeing after it  -> refused
a request naming only the main pair -> refused, the side chain keeps what it has
all three agreeing                  -> that width, applied exactly
```

### Why refusing is what makes the AU work

At `PostConstructor` clap-wrapper walks a grid of *main* bus counts, and for every
other port it fills in the count that port currently has — `wrapasauv2.cpp`,
`mainBusConfigurationRequests`. Nothing in AUv2 tells it what the side chain
should be: `AUChannelInfo` describes main busses only, so the probe has no opinion
about the side chain and expresses that as "the same as now". The VST3 wrapper is
not in that position; `setBusArrangements` hands it an arrangement for *every*
bus, and it relays them.

So "I have no opinion about this port" and "I require this port to be stereo" are
the same bytes on the wire, and a plugin can only read the second. Going from
stereo, "please go mono" therefore arrives as **1 / 2 / 1** — a layout this engine
does not have.

Refusing it is correct and is not the end of the exchange: the probe sends a
second shape per candidate, with the non-main ports taken along, and **1 / 1 / 1**
is the one that gets a yes. That second shape is what puts mono on the AU's menu.
A wrapper that only ever sent the first would offer no mono at all — which is why
this branch needs the clap-wrapper change that adds it, and why accepting
1 / 2 / 1 to work around a wrapper that asks only once is paying for it in a lie
to every other host.

`apply_configuration` then reports what it settled on through `audioPortsInfo()`,
which is what the AUv2 wrapper re-reads immediately afterwards and what it resets
the AU element stream formats from.

The case *Every port carries the same width or the layout is refused* is the one
that would silently cost the validator, and *A request that names only the main
ports is refused* is the one that pins the unnamed-port rule.

## 4. What mono changes at render time

Almost nothing, because `runEngine()` never knew how many channels there were: it
reads `uncheckedEngineSetup().numberOfChannels()` and loops. Three consequences
worth stating.

- **The second output channel is not written.** The zero-fill at the end of
  `runEngine()` covers ports wider than the engine; at width 1 against a mono
  port there is nothing to cover. The `[audio-ports]` render cases fill the
  second output buffer with a sentinel and require it back untouched.
- **A file side chain feeds its first channel.** `runEngine()` fills
  `sampleChannels[channel]` for `channel < channels`, so a stereo file in a mono
  instance is heard as its left channel rather than as a fold. Summing is issue
  #114's other half and belongs with it.
- **The saving is real but is not the saving #114 asks for.** Half the spectral
  work goes away because there is half the audio, on a track that genuinely has
  half the audio. Issue #114 §1 is a different thing — folding L+R inside a
  *stereo* plugin as a user-chosen CPU saving — and this does not implement it.

## 5. What the three formats make of it

Measured 31.08.2026, against clap-wrapper `next` at "Merge pull request #532 from
defiantnerd/request-process".

| | what it does |
|---|---|
| **CLAP** | The extension itself. A host calls it or does not. |
| **AUv2** | Every layout the `PostConstructor` probe accepts becomes an `AUChannelInfo`, so the width the plugin will take *is* the AU's advertised capability. `auval -v aufx SWrx SSTx` reports `Reported Channel Capabilities (explicit): [2, 2] [1, 1]` and passes, its `1 Channel Test` included. It needed a clap-wrapper change to work in a real host; §5.1. |
| **VST3** | `setBusArrangements` forwards *every* bus, main and side, so a VST3 host can ask for the mono triple exactly. The 3.8.0 validator runs `In: Mono: 1 Channels, Out: Mono: 1 Channels` and passes 47/47. |

**`audio-ports-activation` is not needed and is not implemented.** It was the
fallback if VST3 could not see the layouts; it can. The extension answers a
different question anyway — *is this port connected* — which is the one
`sidechain-approach.md` §2 leaves open and issue #115 tracks.

### 5.1 What AUv2 could not express, and what was changed

**A bus that is not the main one cannot be renegotiated in AUv2**, and the mono
layout moves the side chain. `AUChannelInfo` describes main busses only, so
clap-wrapper's probe varied only those, and `WrapAsAUV2::ValidFormat` had nothing
to vet a non-main bus against and pinned it to the count snapshotted at
`PostConstructor` — stereo. The AU therefore advertised `[1,1]`, a host took it,
and the side chain bus then refused the very width that layout had just moved.
Worse, the wrapper reconfigures the ports inside `Initialize` regardless, so the
host and the plugin were left disagreeing about that bus.

The fix is in clap-wrapper, not here: the probe now **applies** each candidate
and reads every port back, so a layout is recorded whole rather than by its main
busses, and `ValidFormat` answers per bus. `can_apply_configuration` cannot
supply this — it answers yes or no without saying what the ports would become.

1/2/1 is still the shape the probe's first request describes, and it is refused.
What makes mono reachable is the *second* shape the probe now sends, which takes
the non-main ports along.

### The receipt that separates "it works" from "it was already like that"

The VST3 validator passes 47/47 either way: a refused `setBusArrangements` makes
its mono case pass trivially rather than fail. What distinguishes them is an
informational line. Against the build before this change:

```
Info:      Mono Input-SpeakerArrangement is not supported. Plug-in suggests: Stereo.
Info:      Mono Output-SpeakerArrangement is not supported. Plug-in suggests: Stereo.
```

Against this one, none — `getBusArrangement` answers mono on all three busses.
A future change that quietly loses the VST3 half would still show 47/47, so that
is the line to grep for.

**auval is blind to the same class of thing, and worse.** It sets the *main*
busses and initialises; it never touches the side chain, and it hosts the unit in
process. The §5.1 failure passed auval, `AU VALIDATION SUCCEEDED` and the
`1 Channel Test` included, while the plugin was unusable in Logic. What sees it
is a host that does what a DAW does — sets every bus, out of process, through the
v3 bridge that a `sandboxSafe` unit is loaded by:

```
instantiating OUT of process, width 1
  set in[0] = 1 ch -> ok
  set in[1] = 1 ch -> FAILED: OSStatus error -10868      # kAudioUnitErr_FormatNotSupported
  set out[0] = 1 ch -> ok
--- after allocate, what the bridge now believes ---
  in[0] = 1 ch
  in[1] = 2 ch                                          # the plugin's port says 1
  out[0] = 1 ch
```

So: **neither format's validator can be trusted about bus layout.** Both pass a
plugin that refuses the layout it advertises. The two lines above — VST3's
"Plug-in suggests" and the AU's out-of-process `set in[1]` — are what actually
distinguish a working build.

## 6. What guards it

| | holds |
|---|---|
| `tests/clap/audioPortsTests.cpp` `[audio-ports]` | the default layout; which widths are accepted over a 0..4 × 0..4 grid; that a layout leaving any two ports disagreeing is refused, including one that names only the main pair; that every port ends up carrying exactly what was asked for, which is the contract the validator checks; that a request naming a port that is not there is refused and changes nothing; that mono moves the ports *and* the engine and comes back; that a mono block renders and leaves the second output buffer alone; and that the mono side-chain port still reaches the DSP |
| `tests/clap/testHost.hpp` | `ActivePlugin` renders at whatever width the ports declare, and `whileDeactivated()` is what keeps the `[!active]` calls legal — clap-helpers writes the violation to `std::cerr` rather than refusing, so a case that asked an active plugin would pass while telling it something no host says |

The engine's own arithmetic is asserted rather than inferred: `engineChannels()`
reaches through `clap_plugin::plugin_data` for the main and side counts, because
"four in, two out" and "two main, two side" are the same fact said two ways and
only one of them is visible from the C API.
