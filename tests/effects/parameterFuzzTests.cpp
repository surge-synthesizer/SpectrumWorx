////////////////////////////////////////////////////////////////////////////////
///
/// \file parameterFuzzTests.cpp
/// ----------------------------
///
///   Every effect at randomised parameter and LFO values, the way a host's
/// fuzzer drives it. The goldens render every effect at its *defaults*, so a
/// defect needing two parameters at unusual values is invisible to them --
/// which is how the four in issue #190 shipped.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "goldens/engineHarness.hpp"

#include "le/parameters/lfoImpl.hpp"
#include "le/spectrumworx/effects/configuration/constants.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/engine/moduleParameters.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>
//------------------------------------------------------------------------------
namespace
{

using SWTest::Slot;

constexpr std::uint8_t channels{2};
constexpr std::uint32_t sampleRate{44100};

// not the goldens' 512/4: the smallest FFT is where a working range clamps
// hardest, which is where three of the four defects lived
constexpr std::array<SWTest::RenderSetup, 3> setups{
    SWTest::RenderSetup{128, 4, channels, sampleRate, 256},
    SWTest::RenderSetup{512, 4, channels, sampleRate, 256},
    SWTest::RenderSetup{2048, 8, channels, sampleRate, 256},
};

/// \brief Every parameter and LFO of a module, at values from its own ranges.
void randomise(LE::SW::Engine::ModuleParameters &module, SWTest::Rng &rng)
{
    auto const value([&rng](LE::SW::ParameterInfo const &info) {
        auto const position((rng.nextBipolar() + 1.0f) / 2.0f);
        return info.minimum + position * (info.maximum - info.minimum);
    });

    for (std::uint8_t index(0); index < module.numberOfParameters(); ++index)
    {
        auto const &info(module.parameterInfo(index));
        if (index < LE::SW::Engine::ModuleParameters::numberOfBaseParameters)
            module.setBaseParameter(index, value(info));
        else
            module.setEffectParameter(
                LE::SW::Engine::ModuleParameters::effectSpecificParameterIndex(index), value(info));
    }

    // and every LFO: it rewrites its parameter every frame, so it reaches values
    // no single set of writes does. phase is drawn from a list rather than
    // uniformly because it was the negative half that broke.
    using LFO = LE::Parameters::LFOImpl;
    constexpr std::array<float, 5> phases{-0.5f, -0.25f, -0.05f, 0.0f, 0.5f};

    for (std::uint8_t index(0); index < module.numberOfLFOControledParameters(); ++index)
    {
        auto &lfo(module.lfo(index));
        auto &parameters(lfo.parameters());
        parameters.set<LFO::Enabled>(true);
        parameters.set<LFO::Waveform>(static_cast<LE::Parameters::LFO::Waveform>(
            rng.next() % LE::Parameters::LFO::NumberOfWaveforms));
        parameters.set<LFO::Phase>(phases[rng.next() % phases.size()]);
    }
}

} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("No effect reads out of bounds at randomised parameter values", "[fuzz]")
{
    // Bypass is put back after randomising: a bypassed module proves nothing
    for (std::uint32_t seed(1); seed <= 8; ++seed)
    {
        for (auto const &setup : setups)
        {
            for (std::int8_t effect(0);
                 effect < static_cast<std::int8_t>(LE::SW::Effects::Constants::numberOfEffects);
                 ++effect)
            {
                SWTest::Rng rng(seed * 2654435761u + static_cast<std::uint32_t>(effect));

                INFO("effect " << LE::SW::Effects::effectName(static_cast<std::uint8_t>(effect))
                               << ", fft " << setup.fftSize << ", seed " << seed);

                std::array<Slot, 1> const chain{
                    Slot{effect, [&rng](LE::SW::Engine::ModuleParameters &module) {
                             randomise(module, rng);
                             module.setBaseParameter(0 /*Bypass*/, 0);
                         }}};

                auto const rendered(
                    SWTest::renderChain(setup, chain, SWTest::Signal::PinkNoise, 4096));
                CHECK(rendered.size() > 0);
            }
        }
    }
}
