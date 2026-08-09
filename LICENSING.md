# Licensing

**The source in this repository is GPL-3.0-or-later. A released SpectrumWorx
binary is AGPL-3.0-or-later**, because it is linked against JUCE 8, which is
AGPLv3-or-commercial.

Those are two different answers to two different questions, and both are correct.
This file says why, and what follows from it.

---

## The source

Every file this project wrote or inherited carries

```
SPDX-License-Identifier: GPL-3.0-or-later
```

and [`LICENSE`](LICENSE) is the GPL-3.0 text those headers refer to. That
includes the 2016 sources under `src/le/`, whose
`Copyright (c) 2010 - 2016. Little Endian Ltd.` lines are attribution and stay:
the copyright holder is not the licence.

Cloning this repository and reading, modifying or redistributing the *source*
therefore puts you under GPL-3.0-or-later and nothing else. No dependency
changes that, because a licence attaches to a work, and this repository is not
the combined work — the binary is.

## The binary

The four shipped formats — CLAP, VST3, AUv2 and the standalone — all link JUCE 8,
which is **dual-licensed AGPLv3 or commercial** (`libs/JUCE/LICENSE.md`). This
project holds no commercial JUCE licence, so it takes the AGPL arm, and the
result is a combined work made of GPL-3.0 parts and AGPL-3.0 parts.

That combination is explicitly permitted, and both licences say so in the same
words. GPL-3.0 §13:

> Notwithstanding any other provision of this License, you have permission to
> link or combine any covered work with a work licensed under version 3 of the
> GNU Affero General Public License into a single combined work, and to convey
> the resulting work. The terms of this License will continue to apply to the
> part which is the covered work, but the special requirements of the GNU Affero
> General Public License, section 13, concerning interaction through a network
> will apply to the combination as such.

AGPL-3.0 §13 is the mirror image of it. So:

- the SpectrumWorx source stays GPL-3.0-or-later, in the combined work as much as
  out of it — that clause says so directly;
- **the combined work — the plugin you can download and load in a DAW — is
  conveyed under AGPL-3.0-or-later**, and AGPL §13's network-interaction
  requirement applies to it.

Practically, distributing a binary means offering the corresponding source of the
whole combination, on AGPL-3.0 terms.

### If you hold a commercial JUCE licence

Then JUCE is not AGPL to you, there is no AGPL part in what you built, and your
binary is simply GPL-3.0-or-later. The SpectrumWorx half is unaffected either
way. This is worth knowing before anyone reaches for a `sed` over 452 file
headers: **the headers are right as they stand**, and the AGPL result is a
property of one particular build rather than of this source.

---

## What is in the binary, and under what

Verified 03.08.2026 against the four link lines under
`build-release/src/clap-first/`, not from the dependency list.

| | Licence | Why it is here |
|---|---|---|
| **JUCE 8** | **AGPLv3 or commercial** | `juce_audio_basics`, `juce_audio_formats`, `juce_core`, `juce_data_structures`, `juce_events`, `juce_graphics`, `juce_gui_basics`. **This is the one that makes the binary AGPL.** |
| CLAP | MIT | The plugin format |
| clap-helpers | MIT | The `clap::helpers::Plugin` base |
| clap-wrapper | MIT | The VST3, AUv2 and standalone builds |
| VST3 SDK | MIT | Fetched by clap-wrapper. Steinberg relicensed it from GPL-3.0-or-proprietary in 2025, so it no longer constrains anything here. |
| AudioUnitSDK | Apache-2.0 | Fetched by clap-wrapper, for the AUv2 |
| RtAudio, RtMidi | MIT-style | Fetched by clap-wrapper, standalone only |
| sst-plugininfra | MIT, plus zlib for the vendored tinyxml and strnatcmp | Paths, version stamping, the preset XML reader |
| sst-cpputils | MIT | |
| sst-clap-helpers | MIT | |
| pffft | BSD-3-clause (FFTPACK / UCAR) | The FFT on platforms without Accelerate |
| simde | MIT | |
| fmt | MIT | |

Everything but JUCE is permissive, so nothing else in that table changes the
answer.

**Not in the binary**, and so not part of this:

- **Catch2** (BSL-1.0) — `sw-dsp-tests` and `sw-plugin-tests` only.
- **Boost** (BSL-1.0) — fetched by a dependency's build and on none of the four
  plugin link lines. `scripts/check_boost_allowlist.sh` is what keeps it that
  way from our side.
- **sst-basic-blocks** (GPL-3.0) — a submodule because sst-cpputils probes for
  the target by name. Nothing under `src/`, `tests/` or `tools/` includes it.

---

## What the installer shows

`assets/installer/License.txt` — the same file for the macOS `.pkg` and the
Windows Inno Setup `.exe`, and copied into the Linux zip. It is the AGPL-3.0
text verbatim, because AGPL-3.0-or-later is what the thing being installed is
under. Nothing is prepended to it and nothing is appended: the file is
byte-comparable against <https://www.gnu.org/licenses/agpl-3.0.txt>, which is
the property that makes it checkable. The reasoning for *why* the binary is
AGPL while the source is GPL is this document's job, not the installer's.
