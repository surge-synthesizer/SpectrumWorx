////////////////////////////////////////////////////////////////////////////////
///
/// \file presetRenderTests.cpp
/// ---------------------------
///
///   Every shipped preset, played.
///
///   presetCorpusTests.cpp loads every factory bank and compares what they
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

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
/// \brief The presets a checked build still cannot play, by name.
///
/// \note Phlip followed by Ethereal leaves a negative amplitude; neither does it
/// alone, and Ethereal copies the side channel's magnitudes over the main
/// channel's. \see issue #10.
///
///   A rename drops a preset off this list rather than muting it silently.
bool skipped([[maybe_unused]] std::filesystem::path const &preset,
             [[maybe_unused]] std::filesystem::path const &banks)
{
#ifndef NDEBUG
    constexpr std::string_view abortsInACheckedBuild[]{
        "Overt Dynamics/Digi Shuffler.swp",
        "Overt Dynamics/Transformer Crunch.swp",
    };
    auto const relative(std::filesystem::relative(preset, banks).generic_string());
    return std::ranges::find(abortsInACheckedBuild, relative) != std::end(abortsInACheckedBuild);
#else
    return false;
#endif // NDEBUG
}
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("Every factory preset renders finite audio", "[presets][render]")
{
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
        if (skipped(file.path(), banks))
            continue;

        /// \note One engine per preset, as presetCorpusTests.cpp does: loading B
        /// on top of A is a merge, and it is not what this case is about.
        SWTest::Engine engine;
        engine.setNumberOfChannels(2, 2);
        engine.setSampleRate(sampleRate);
        engine.setBlockSize(blockSize);
        REQUIRE(engine.initialise());

        auto const preset(LE::SW::readPresetFile(file.path()));
        REQUIRE(static_cast<bool>(preset));

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
    //
    // \note A floor of "not none" rather than of the number that happened to
    // ship. What this has to catch is a sweep that found no files at all -- a
    // moved directory, a broken SW_PRESET_DATA_DIR -- and adding or removing a
    // preset is content work rather than a regression.
    REQUIRE(loaded > 0);

    for (auto const &offender : offenders)
        UNSCOPED_INFO("non-finite output: " << offender);
    CHECK(offenders.empty());
}
