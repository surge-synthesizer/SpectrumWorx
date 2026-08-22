////////////////////////////////////////////////////////////////////////////////
///
/// \file engineSetupStalenessTests.cpp
/// -----------------------------------
///
///   What the audio thread may read while the UI thread is mid-change.
///
///   `SpectrumWorxCore::engineSetup()` asserts that the setup still agrees with
/// the *spectral* parameters -- `isEngineSetupUpToDate()` compares the FFT size,
/// the overlap factor and the window size factor, and nothing else. Between a
/// parameter moving and `updateEngineSetup()` running, it does not, and that
/// window is real: `setGlobalParameter(FFTSize&, …)` sets the value and then
/// updates, under the processing lock.
///
///   `SpectrumWorxCLAP::process()` reads the sample rate through
/// `getSampleRate()` and the channel count through the setup, on every block,
/// *before* `SpectrumWorxCore::process()` takes that lock. Neither field is one
/// the assertion compares, so both were asserting about a staleness that cannot
/// affect the value they return -- and loading a factory preset that changes the
/// FFT size fired it every time audio was running.
///
/// \note This does not test the race, which needs two threads and is 5.8's to
/// fix properly. It tests the property that makes the race harmless to these two
/// readers: a stale setup still answers them, and answers correctly.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "goldens/engineHarness.hpp"

#include "le/spectrumworx/engine/parameters.hpp"
#include "le/spectrumworx/engine/setup.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace LE;
using namespace LE::SW;

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("A stale engine setup still answers the audio thread", "[engine-setup]")
{
    SWTest::Engine engine;
    engine.setNumberOfChannels(2, 2);
    engine.setSampleRate(48000);
    engine.setBlockSize(512);
    REQUIRE(engine.initialise());

    REQUIRE(engine.getSampleRate() == 48000);
    auto const channels(engine.uncheckedEngineSetup().numberOfChannels());
    REQUIRE(channels == 2);

    /// \note The parameter, not setGlobalParameter(): going through the setter
    /// would update the setup, which is exactly the state being avoided here.
    /// This is what the engine looks like from another thread partway through
    /// that call.
    auto const staleFFTSize(static_cast<GlobalParameters::FFTSize::param_type>(
        engine.uncheckedEngineSetup().fftSize<unsigned int>() * 2));
    engine.parameters().get<GlobalParameters::FFTSize>().setValue(staleFFTSize);

    // i.e. the setup no longer agrees with the parameters.
    REQUIRE(engine.uncheckedEngineSetup().fftSize<unsigned int>() != staleFFTSize);

    /// \note Both of these asserted here, which in a debug plugin is a break in
    /// the audio callback. Neither reads a field the staleness touches.
    CHECK(engine.getSampleRate() == 48000);
    CHECK(engine.uncheckedEngineSetup().numberOfChannels() == channels);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The four numbers the settings panel's Engine page prints, against the
/// 2.x plugin's own answers for the same settings.
///
///   Issue #142 reported them as disagreeing with the shipped plugin, with an
/// off-by-one suspected in the overlap factor. They do not: the recording in the
/// issue has the two plugins on *different* overlap settings, because this one's
/// lines were stale -- which is the bug the issue names in its title. The rows
/// below are read off that recording's 2.x window frame by frame, and every one
/// of them matches. They are here so that the next person to wonder has a number
/// rather than an argument.
///
/// \note In this file because `Engine::Setup` is what it is about, which is also
/// what the case above is about. What the *page* does with these is
/// tests/gui/overlayPanelTests.cpp.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The engine information matches what the 2.x plugin printed", "[engine-setup]")
{
    using namespace LE::SW::GlobalParameters;
    using LE::SW::Engine::Constants::Window;

    struct Row
    {
        unsigned int fftSize;
        unsigned int overlapFactor; ///< the factor, not the percentage: 4 is 75 %
        Window window;
        double ripplePercent;
        double frequencyResolutionHz;
        double timeResolutionMs;
        double latencyMs;
    };

    /// \note At 48 kHz, which is the rate the recording was made at -- three of
    /// the four numbers are a function of it.
    Row const rows[]{
        // 2.x read: "0% (excellent), 23.4 Hz, 10.7 ms, 42.7 ms"
        {2048, 4, Window::Hann, 0.0, 23.4375, 10.6667, 42.6667},
        // 2.x read: "240.07% (poor), 46.9 Hz, 10.7 ms, 21.3 ms"
        {1024, 2, Window::FlatTop, 240.067, 46.875, 10.6667, 21.3333},
        // 2.x read: "7.24% (poor), 46.9 Hz, 5.3 ms, 21.3 ms"
        {1024, 4, Window::FlatTop, 7.24106, 46.875, 5.33333, 21.3333},
    };

    for (auto const &row : rows)
    {
        CAPTURE(row.fftSize, row.overlapFactor);

        SWTest::Engine engine;
        engine.setNumberOfChannels(2, 2);
        engine.setSampleRate(48000);
        engine.setBlockSize(512);
        REQUIRE(engine.initialise());

        REQUIRE(engine.set<FFTSize>(row.fftSize));
        REQUIRE(engine.set<OverlapFactor>(row.overlapFactor));
        REQUIRE(engine.set<WindowFunction>(row.window));

        auto const &setup(engine.uncheckedEngineSetup());

        /// \note To the precision the page prints at and no further: two decimals
        /// on the percentage, one on the other three. A tighter margin would be
        /// pinning this build's float arithmetic rather than what a user reads.
        CHECK(setup.wolaRippleFactor() * 100.0f == Catch::Approx(row.ripplePercent).margin(0.005));
        CHECK(setup.frequencyRangePerBin<float>() ==
              Catch::Approx(row.frequencyResolutionHz).margin(0.05));
        CHECK(setup.stepTime() * 1000 == Catch::Approx(row.timeResolutionMs).margin(0.05));
        CHECK(setup.latencyInMilliseconds() == Catch::Approx(row.latencyMs).margin(0.05));
    }
}
