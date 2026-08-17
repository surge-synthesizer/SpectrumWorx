# SpectrumWorx

SpectrumWorx is an awesome spectral effect originally developed and released by Little Endian,
where development ended in 2016 and the plugin was open sourced in 2024. You can find
the original source code dump here: https://github.com/LittleEndianLtd/SpectrumWorx

As with all folks who have decided to open source great commercial products at the end
of their development, we are very grateful to Little Endian for making this decision.

In 2026, when a KvR thread brought this to our attention, we grabbed it and started modernizing.
This involved heavy use of Claude Opus 5 and Fable 5 to port the product to modern standards,
including:

- Moving from VST2 Windows and macOS only to CLAP, clap-wrapper for Windows, macOS and Linux
- Setting up reliable GitHub action pipelines and binary builds
- Modernizing the code, including removing old libraries (JUCE 2, Boost...)
- Making substantial improvements to the threading and ownership model
- Adding tests to cover the engine
- Vectorizing the skin
- Inferring technical documentation

That was a heavy two weeks of plan/iterate/generate/test/repeat cycle using machine tools almost entirely
to generate the ported code, while preserving the DSP code and operating model.

Right now, this is a bit of a work-in-prgoress as we figure out if we can move it from
a two week sprint to an official 3.0 release from the team.

## Licence ⚖️
The source in this repository is **GPL-3.0-or-later**. A released binary links
JUCE 8 under its AGPLv3 arm, so the plugin you can download is
**AGPL-3.0-or-later**. [LICENSING.md](./LICENSING.md) has the reasoning, the
per-dependency table and what it means if you hold a commercial JUCE licence.
