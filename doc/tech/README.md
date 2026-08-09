# SpectrumWorx — the tech documents

Eight documents, and every one of them describes the tree **as it is now**. Five
say how something works, one says what is left to do, one says what finished work
left behind, one says what was deliberately not built. Nothing here is a plan
being executed or a record of how the port got where it is; that is in
[`old/`](old/).

## How it works

| | What it is |
|---|---|
| [`threading_model.md`](threading_model.md) | Which thread owns the engine, the three channels they talk over, and what the layering forbids. |
| [`effect_contract.md`](effect_contract.md) | What an effect must declare and implement, what the engine hands it per frame, the six edits that register one, and an inventory of the 57 shipped, the 4 orphaned and the 17 unfinished. |
| [`parameter_system.md`](parameter_system.md) | How the plugin addresses, names and exports its 286 parameters, and why the answer is "dynamic" in a way that constrains the format choice. |
| [`streaming_format.md`](streaming_format.md) | What goes into a preset and into session state: the on-disk names, the snapshot tests that pin them, and the rules for changing any of it. |
| [`how-lfo-rates-work.md`](how-lfo-rates-work.md) | What an LFO's period holds, which bar it is a fraction of, and what tempo sync does and does not move. |

## What is left

| | What it is |
|---|---|
| [`todo.md`](todo.md) | **The work queue.** No ordered item left — shipping was the last and is a decision now — and a page of smaller things. An item that closes comes out of the file. |
| [`tech_debt.md`](tech_debt.md) | What finished work left behind — the half-fix, the correct-but-unsatisfying answer, the finding with no owner. An entry that is remediated comes out too. |
| [`future_items_to_revive.md`](future_items_to_revive.md) | Capabilities deliberately deleted rather than carried, and what reviving one would cost. The cost is usually compatibility, not code. |

The line between the three: `todo.md` is work somebody will sit down and do;
`tech_debt.md` is what would still be true if all of it were done exactly as
written; `future_items_to_revive.md` is what nobody is going to do next, recorded
so that the reasoning survives the deletion.

## [`old/`](old/) — the path, kept for the reasoning

None of these describes this tree. All three are a record of how it was read —
the first two before it built, the third against the built tree — each contains
claims the work has since disproved, and each is kept because it is the only
account of *why* several decisions went the way they did.

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
- **A document says what is true, not what happened.** When something closes it
  leaves the todo list or the debt list rather than being struck through in
  place. If the reasoning behind it was worth keeping — and often it is, because
  several of the most useful paragraphs here are about something that turned out
  to be wrong — it moves into whichever "how it works" document owns the
  mechanism, stated as a property of the design rather than as a story.
- **Test counts are stated as of a date.** They move every time a case lands, so
  only the count in `todo.md`'s status table is meant to be current.
- **The two test binaries are `sw-dsp-tests` and `sw-plugin-tests`**, and `ctest`
  runs both. There is no `sw-tests`; it split on 02.08.2026 so that the engine's
  cases could link without JUCE, which is what proves the engine does not need
  it.
