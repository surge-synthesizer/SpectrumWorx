////////////////////////////////////////////////////////////////////////////////
///
/// engineSetupTests.cpp
/// --------------------
///
///   `Engine::Setup`'s conversions on a plugin the host has never activated.
///
///   That is a state a host puts the plugin in as a matter of course: create it,
/// hand back the project's bytes with `stateLoad`, activate only afterwards --
/// which tests/clap/stateTests.cpp already covers from the other side. There is
/// no spectral setup then: `fftSize` is 0 and `stepSize` is 0 with it, and three
/// of the conversions divide by one or the other. For an integer that is
/// undefined behaviour, which arm64 renders as a silent zero and x86 as a
/// hardware trap -- so this case reads as a formality here and is a crash
/// somewhere else. \see issue #81.
///
/// \note Which is also why the case is worth more than it looks: nothing walked
/// these conversions on an unconfigured setup, so the hazard was found by
/// reading rather than by running, and only an x86 leg would ever have said so.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "le/spectrumworx/engine/setup.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using LE::SW::Engine::Setup;

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("An unconfigured setup converts without dividing by zero", "[engine][setup]")
{
    Setup const setup;

    /// \note What the constructor leaves behind, and the whole of the hazard: an
    /// overlapping factor of 1 was already chosen to keep `stepSize()` itself
    /// from dividing by zero, and the FFT size deliberately is not -- it is how
    /// `updateEngineSetup()` knows it has not run yet.
    REQUIRE(setup.fftSize<unsigned int>() == 0);
    REQUIRE(setup.windowOverlappingFactor<unsigned int>() == 1);
    REQUIRE(setup.stepSize<std::uint16_t>() == 0);
    REQUIRE(setup.sampleRate<unsigned int>() != 0); // ...there is always one of these

    // no FFT, so no bin to spread the spectrum across
    CHECK(setup.frequencyRangePerBin<unsigned int>() == 0);
    CHECK(setup.frequencyRangePerBin<float>() == 0.0f);

    // and no step, so nothing takes a whole one
    CHECK(setup.milliSecondsToSteps(100) == 0);
    CHECK(setup.secondsToSteps(1) == 0.0f);
    CHECK(setup.stepsPerSecond() == 0.0f);

    // the conversions that never divided by either, for the walk
    CHECK(setup.numberOfBins() == 1);
    CHECK(setup.latencyInSamples() == 0);
    CHECK(setup.latencyInMilliseconds() == 0.0f);
    CHECK(setup.frameTime() == 0.0f);
    CHECK(setup.stepTime() == 0.0f);
    CHECK(setup.frequencyInHzToBin(1000) == 0);
    CHECK(setup.normalisedFrequencyToBin(0.5f) == 0);
    CHECK(setup.frequencyPercentageToBin(std::uint8_t{50}) == 0);
}

/// \note The anchor: the same conversions once the host has activated the
/// plugin, so that the zeros above are read as "not configured yet" rather than
/// as an arithmetic that answers zero to everything.
TEST_CASE("A configured setup converts the way the numbers say", "[engine][setup]")
{
    Setup setup;
    setup.setSampleRate(48000u);
    setup.setFFTSize(1024u);
    setup.setOverlappingFactor(4u);

    REQUIRE(setup.stepSize<std::uint16_t>() == 256);

    CHECK(setup.frequencyRangePerBin<float>() == Catch::Approx(46.875));
    CHECK(setup.frequencyRangePerBin<unsigned int>() == 46);
    CHECK(setup.milliSecondsToSteps(100) == 19); // 18.75 steps, rounded up
    CHECK(setup.secondsToSteps(1) == Catch::Approx(187.5));
    CHECK(setup.stepsPerSecond() == Catch::Approx(187.5));
    CHECK(setup.numberOfBins() == 513);
    CHECK(setup.latencyInSamples() == 1024);
}
