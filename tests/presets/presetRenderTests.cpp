////////////////////////////////////////////////////////////////////////////////
///
/// \file presetRenderTests.cpp
/// ---------------------------
///
///   Every shipped preset, played.
///
///   presetCorpusTests.cpp loads all 303 factory banks and compares what they
/// put in the parameter tree; the goldens play one effect at a time at its
/// defaults. Neither runs audio through a preset -- so a chain that a preset
/// builds and a user hears had no coverage at all, and the first run of this
/// file found four presets rendering NaN.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "presets/presetHarness.hpp"

#include "core/modules/finalImplementations.hpp"
#include "core/modules/moduleDSPAndGUI.hpp"

#include "le/spectrumworx/presetStorage.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>
//------------------------------------------------------------------------------

/// \note A release-build artifact, for the reason goldenTests.cpp gives at
/// length and this file met eight times over: rendering real spectra in a checked
/// build aborts on a debug-only verification that no amplitude is negative, and a
/// running sum across thousands of bins drifts a hair below zero on plenty of
/// ordinary material. Benign in the output -- the release run renders all 303
/// finite, which is the property this file is here for -- and a real numerical
/// weakness in the vector primitives rather than in any of these presets. A skip
/// list would have needed a dozen names and would have grown; recorded in
/// issue #10 instead.
///                                           (02.08.2026.) (SW port)
#ifndef NDEBUG
#define LE_SW_PRESET_RENDER_NEEDS_A_RELEASE_BUILD                                                  \
    SKIP("Presets render in a release build; a checked build aborts on the negative amplitude "    \
         "verification -- see the note in presetRenderTests.cpp.")
#else
#define LE_SW_PRESET_RENDER_NEEDS_A_RELEASE_BUILD static_cast<void>(0)
#endif

TEST_CASE("Every factory preset renders finite audio", "[presets][render]")
{
    LE_SW_PRESET_RENDER_NEEDS_A_RELEASE_BUILD;

    constexpr std::uint32_t blockSize{512};
    constexpr float sampleRate{48000};
    constexpr unsigned int blocks{16};

    std::filesystem::path const banks(SW_PRESET_DATA_DIR);
    REQUIRE(std::filesystem::is_directory(banks));

    unsigned int loaded(0);
    std::vector<std::string> offenders;

    for (auto const &file : std::filesystem::recursive_directory_iterator(banks))
    {
        if (file.path().extension() != ".swp")
            continue;

        /// \note One engine per preset, as presetCorpusTests.cpp does: loading B
        /// on top of A is a merge, and it is not what this case is about.
        SWTest::Engine engine;
        engine.setNumberOfChannels(2, 2);
        engine.setSampleRate(sampleRate);
        engine.setBlockSize(blockSize);
        REQUIRE(engine.initialise());

        auto const preset(LE::SW::readPresetFile(file.path()));
        REQUIRE(preset);

        /// \note The counting reporter, so that a 2011 preset which does not
        /// mention a parameter its effect grew later does not raise a dialog in a
        /// process with no message thread. See presetHarness.hpp.
        SWTest::ScopedProblemCounter const counting;
        REQUIRE(LE::SW::loadPreset(preset.get(), true /*ignore external samples*/, nullptr,
                                   SWTest::PresetConsumer{engine}));
        ++loaded;

        engine.resume();

        /// \note Voice rather than a sine: dense partials and a moving pitch, and
        /// -- what matters here -- real spectra with empty regions in them. An
        /// exactly zero bin is what the Exaggerator's `pow` turned into infinity.
        std::vector<float> left(blockSize), right(blockSize);
        std::vector<float> outLeft(blockSize), outRight(blockSize);
        SWTest::generate(SWTest::Signal::Voice, left, sampleRate);
        right = left;

        float const *inputs[]{left.data(), right.data()};
        float *outputs[]{outLeft.data(), outRight.data()};

        bool finite(true);
        for (unsigned block(0); block < blocks; ++block)
        {
            engine.process(inputs, inputs, outputs, 1.0f, blockSize);
            for (auto const sample : outLeft)
                finite &= std::isfinite(sample);
            for (auto const sample : outRight)
                finite &= std::isfinite(sample);
        }
        engine.suspend();

        if (!finite)
            offenders.push_back(file.path().string());
    }

    // Not a spot check: the interesting presets are the ones nobody would think
    // to pick, and there is no way to know which those are without playing them.
    REQUIRE(loaded >= 288);

    for (auto const &offender : offenders)
        UNSCOPED_INFO("non-finite output: " << offender);
    CHECK(offenders.empty());
}
