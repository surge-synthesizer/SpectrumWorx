# SpectrumWorx — the tech documents

Six documents, and every one of them describes the tree **as it is now** — how
something works, not what is left to do about it. Nothing here is a plan being
executed or a record of how the port got where it is; that is in [`old/`](old/).

## How it works

| | What it is |
|---|---|
| [`threading_model.md`](threading_model.md) | Which thread owns the engine, the three channels they talk over, and what the layering forbids. |
| [`effect_contract.md`](effect_contract.md) | What an effect must declare and implement, what the engine hands it per frame, the six edits that register one, and an inventory of the 57 shipped, the 4 orphaned and the 17 unfinished. |
| [`parameter_system.md`](parameter_system.md) | How the plugin addresses, names and exports its 286 parameters, and why the answer is "dynamic" in a way that constrains the format choice. |
| [`streaming_format.md`](streaming_format.md) | What goes into a preset and into session state: the on-disk names, the snapshot tests that pin them, and the rules for changing any of it. |
| [`latency.md`](latency.md) | Why the delay is one FFT window, why it cannot be less, the two FIFO primings that hold it constant, and the block-splitting bug that happened without the second one. |
| [`how-lfo-rates-work.md`](how-lfo-rates-work.md) | What an LFO's period holds, which bar it is a fraction of, and what tempo sync does and does not move. |
| [`sidechain-approach.md`](sidechain-approach.md) | The three things that can feed the side channel, why the choice is a *source* and not a bus topology, and what an old preset's `Input_mode` turns into. |

## What is left — the issue tracker

**Three documents used to live here and none of them does any more.** `todo.md`
was the work queue, `tech_debt.md` was what finished work left behind, and
`future_items_to_revive.md` was what had been deliberately deleted rather than
carried. All three are now
[issues](https://github.com/surge-synthesizer/SpectrumWorx/issues), which is
where somebody looking for something to do will actually look, and which can be
assigned, closed and argued with.

So: **if it is a claim about how the tree behaves, it belongs in one of the six
documents above. If it is something somebody should do about it, it is an
issue.** The rule that made the old files worth reading still applies to both —
a claim carries its date and its evidence — and the one that made them work
still applies here: when something closes it leaves, rather than being struck
through in place. If the reasoning behind a closed issue was worth keeping, it
moves into whichever document owns the mechanism, stated as a property of the
design rather than as a story.

Source comments refer to issues by number (`\see issue #12`). Those numbers do
not move, which is what makes them safe to write down and is why the file names
they replaced were not.

## [`old/`](old/) — the path, kept for the reasoning

None of these describes this tree. All three are a record of how it was read —
the first two before it built, the third against the built tree — each contains
claims the work has since disproved, and each is kept because it is the only
account of *why* several decisions went the way they did.

They also refer to `todo.md` and `tech_debt.md`, by section and by link, and
those files no longer exist. **The references are left as they were**: these are
a record of what was written at the time, and repointing them at issues that did
not exist then would make them a worse record rather than a better one. A link
that does not resolve is the honest form of a document about a tree that has
moved.

| | Read it for |
|---|---|
| [`old/initial_scan.md`](old/initial_scan.md) | The analysis pass on the 2016 snapshot, before anything was touched. Its inventory of what that tree contained is not repeated anywhere else. |
| [`old/implementation_sequence.md`](old/implementation_sequence.md) | The nine-stage plan the port was executed against, plus the per-stage "done when" each commit was measured against. |
| [`old/fable_review.md`](old/fable_review.md) | A full read of `src/` against these documents, ranked by severity, and what each finding turned into. Every one of them is fixed; it is here for the two claims that did not survive contact, the near miss, the inventory of what was checked and found sound, and the account of which instrument pinned which fix. |

---

## Conventions these documents share

- **A claim carries its date and its evidence.** "As of 02.08.2026" and a file
  and line, or a test name, or a measured number. A bullet with no provenance is
  unverifiable a month later, which is the same as being false.
- **A document says what is true, not what happened.** If the reasoning behind a
  closed issue was worth keeping — and often it is, because several of the most
  useful paragraphs here are about something that turned out to be wrong — it
  moves into whichever "how it works" document owns the mechanism, stated as a
  property of the design rather than as a story.
- **Test counts are stated as of a date.** They move every time a case lands, so
  a number in one of these documents is a measurement rather than a status. CI is
  the authority for how many tests there are; a local `ctest` number means the
  build directory it came from was reconfigured first.
- **The two test binaries are `sw-dsp-tests` and `sw-plugin-tests`**, and `ctest`
  runs both. There is no `sw-tests`; it split on 02.08.2026 so that the engine's
  cases could link without JUCE, which is what proves the engine does not need
  it.
