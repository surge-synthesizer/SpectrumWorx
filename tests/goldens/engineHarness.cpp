////////////////////////////////////////////////////////////////////////////////
///
/// engineHarness.cpp
/// -----------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "engineHarness.hpp"

#include "core/modules/factory.hpp"
// \note Order matters and is not alphabetical: finalImplementations.hpp defines
// Module::Impl<> and needs Module complete. factory.cpp includes them in this
// order for the same reason.
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/modules/finalImplementations.hpp"

#include "le/math/math.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/utility/ignoreUnused.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace SWTest
{

namespace
{
constexpr float pi{std::numbers::pi_v<float>};
} // anonymous namespace

void generate(Signal const signal, std::span<float> const mono, float const sampleRate)
{
    auto const frames(mono.size());
    switch (signal)
    {
    case Signal::Impulse:
    {
        std::fill(mono.begin(), mono.end(), 0.0f);
        ////////////////////////////////////////////////////////////////////////
        ///
        /// \note A quarter of the way in, rounded down onto the analysis grid.
        ///
        ///   Not at zero, because the engine's latency means an impulse in the
        /// first frame is still inside the first analysis window when the tail
        /// is measured. And not at an arbitrary offset, because an impulse that
        /// straddles a hop boundary is split across two analysis windows, and
        /// the bin decisions the spectral effects make off that split sit on a
        /// knife edge.
        ///
        ///   Measured, one machine, Accelerate against pffft: Atonal's peak
        /// agrees to 1e-7 at every offset that is a whole number of hops and
        /// differs by 8 % one sample off it. Octaver does the same, and does it
        /// at 21.5 hops -- which is why the alignment has to cover the *hop*
        /// rather than the FFT size. The golden matrix runs 512/4 and 2048/8, so
        /// hops of 128 and 256; 1024 covers both with room for a wider matrix.
        ///
        /// \note Rounding down rather than to nearest keeps every power-of-two
        /// render exactly where it was -- 16384/4 is already 4096 -- so this
        /// moves the impulse only for the lengths that are not powers of two.
        ///
        ////////////////////////////////////////////////////////////////////////
        constexpr std::size_t hopAlignment{1024};
        auto const quarter(frames / 4);
        mono[(quarter >= hopAlignment) ? (quarter / hopAlignment * hopAlignment) : quarter] = 1.0f;
        break;
    }

    case Signal::Sweep:
    {
        // Logarithmic sweep, 20 Hz to Nyquist/2, constant amplitude.
        auto const f0(20.0);
        auto const f1(static_cast<double>(sampleRate) / 4);
        auto const duration(static_cast<double>(frames) / sampleRate);
        auto const k(std::log(f1 / f0));
        for (std::size_t frame(0); frame < frames; ++frame)
        {
            auto const t(static_cast<double>(frame) / sampleRate);
            auto const phase(2 * std::numbers::pi * f0 * duration / k *
                             (std::exp(k * t / duration) - 1));
            mono[frame] = static_cast<float>(0.5 * std::sin(phase));
        }
        break;
    }

    case Signal::PinkNoise:
    {
        // Voss-McCartney, fixed seed. Pink rather than white because spectral
        // effects behave very differently across a flat versus a 1/f spectrum.
        Rng rng(0x5EED1234u);
        constexpr unsigned int rows{8};
        float row[rows]{};
        float runningSum{0};
        std::uint32_t counter{0};
        for (std::size_t frame(0); frame < frames; ++frame)
        {
            auto const changed(counter ^ (counter + 1));
            ++counter;
            for (unsigned int bit(0); bit < rows; ++bit)
            {
                if (changed & (1u << bit))
                {
                    runningSum -= row[bit];
                    row[bit] = rng.nextBipolar();
                    runningSum += row[bit];
                }
            }
            mono[frame] = (runningSum + rng.nextBipolar()) / (rows + 1) * 0.7f;
        }
        break;
    }

    case Signal::Voice:
    {
        // A harmonic stack with three fixed formants and 5 Hz vibrato: dense
        // partials over a moving pitch, which is what the pitch and phase
        // vocoder effects actually have to cope with.
        constexpr float f0{110.0f};
        constexpr float formants[]{700.0f, 1220.0f, 2600.0f};
        constexpr float bandwidth{120.0f};
        double phase{0};
        for (std::size_t frame(0); frame < frames; ++frame)
        {
            auto const t(static_cast<float>(frame) / sampleRate);
            auto const pitch(f0 * (1 + 0.03f * std::sin(2 * pi * 5 * t)));
            phase += 2 * std::numbers::pi * pitch / sampleRate;
            float sample{0};
            for (unsigned int harmonic(1); harmonic <= 40; ++harmonic)
            {
                auto const frequency(pitch * harmonic);
                if (frequency > sampleRate / 2)
                    break;
                float gain{0.02f};
                for (auto const formant : formants)
                {
                    auto const distance((frequency - formant) / bandwidth);
                    gain += std::exp(-0.5f * distance * distance);
                }
                sample += gain * static_cast<float>(std::sin(phase * harmonic)) / harmonic;
            }
            mono[frame] = 0.4f * sample;
        }
        break;
    }
    }
}

/// \note Throws rather than asserts, and that is the whole point of it being a
/// function. `LE_ASSERT` does not survive NDEBUG and the goldens render in
/// Release only, so a misspelt name under an assert would look up -1, quietly
/// render the *bypassed chain* and mint a fixture for it. Catch2 reports the
/// throw as a failure of whichever case asked, in either build type.
std::int8_t effectByStreamingName(std::string_view const streamingName)
{
    auto const index(LE::SW::Effects::effectIndexFromStreamingName(streamingName));
    if (index < 0)
        throw std::invalid_argument("No effect streams under the name \"" +
                                    std::string(streamingName) + "\".");
    return index;
}

std::vector<float> render(RenderSetup const &setup, std::int8_t const effectIndex,
                          Signal const signal, std::uint32_t const frames)
{
    Slot const slots[]{{effectIndex, {}}};
    return renderChain(setup, slots, signal, frames);
}

std::vector<float> renderWithSideChain(RenderSetup const &setup, std::int8_t const effectIndex,
                                       Signal const main, Signal const side,
                                       std::uint32_t const frames)
{
    std::vector<float> mainInput(frames), sideInput(frames);
    generate(main, mainInput, static_cast<float>(setup.sampleRate));
    generate(side, sideInput, static_cast<float>(setup.sampleRate));

    Slot const slots[]{{effectIndex, {}}};
    return renderChain(setup, slots, mainInput, sideInput);
}

std::vector<float> renderChain(RenderSetup const &setup, std::span<Slot const> const slots,
                               Signal const signal, std::uint32_t const frames)
{
    std::vector<float> input(frames);
    generate(signal, input, static_cast<float>(setup.sampleRate));
    return renderChain(setup, slots, input);
}

std::vector<float> renderChain(RenderSetup const &setup, std::span<Slot const> const slots,
                               std::span<float const> const monoInput,
                               std::span<float const> const monoSideInput)
{
    using namespace LE::SW;

    auto const frames(static_cast<std::uint32_t>(monoInput.size()));

    Engine engine;

    engine.setNumberOfChannels(setup.numberOfChannels, setup.numberOfChannels);
    engine.setSampleRate(static_cast<float>(setup.sampleRate));
    engine.setBlockSize(setup.blockSize);
    engine.set<GlobalParameters::FFTSize>(setup.fftSize);
    engine.set<GlobalParameters::OverlapFactor>(setup.overlapFactor);

    bool const initialised(engine.initialise());
    LE_ASSERT_MSG(initialised, "Test engine failed to initialise.");
    LE::Utility::ignoreUnused(initialised);

    LE_ASSERT_MSG(slots.size() <= Constants::maxNumberOfModules, "More slots than the chain has.");

    /// Kept so that Slot::duringRender can reach them between blocks.
    std::vector<LE::SW::Module *> modules(slots.size(), nullptr);

    for (std::uint8_t slot(0); slot < slots.size(); ++slot)
    {
        if (slots[slot].effectIndex < 0)
            continue;
        auto const inserted(engine.program().moduleChain().setParameter(
            slot, slots[slot].effectIndex, engine.moduleInitialiser()));
        LE_ASSERT_MSG(inserted.second == slots[slot].effectIndex,
                      "Test engine failed to insert the effect.");
        modules[slot] = inserted.first ? &*inserted.first : nullptr;
        if (inserted.first && slots[slot].configure)
            slots[slot].configure(*inserted.first);
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note **Before resume(), and after the modules are in the chain.** The
    /// seed is what `reset()` deals every generator in the engine from, and
    /// `resume()` is the reset that does the dealing -- so a module inserted
    /// after this point would still be seeded, but a seed set after it would
    /// not have reached the ones already there.
    ///
    ///   Without it the engine takes the clock and a stack address, which is
    /// what keeps two instances apart in a session and what makes a render
    /// unrepeatable. Freqverb, Whisperer and Burrito draw, as does any random
    /// LFO waveform.
    /// seeds the engine, not a global
    ///
    ////////////////////////////////////////////////////////////////////////////
    engine.setRandomSeed(0xA5A5C3C3u);

    engine.resume();

    auto const channels(setup.numberOfChannels);

    // Every channel gets the same signal: what is being rendered is a mono
    // source through a stereo engine, and nothing here is a stereo property.
    std::vector<std::vector<float>> inputChannels(
        channels, std::vector<float>(monoInput.begin(), monoInput.end()));
    std::vector<std::vector<float>> outputChannels(channels, std::vector<float>(frames, 0.0f));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The side chain, and until 05.08.2026 this passed `inputPointers`
    /// twice -- the comment here claimed the side-chain effects "are driven
    /// separately" and nothing did. A side-chain effect fed its own main signal
    /// renders identically to one that drops the side chain on the floor, so
    /// fourteen effects had 112 fixtures apiece that could not fail.
    ///
    ///   Empty still means "the main signal", because that is what a host hands
    /// over when nothing is patched into the port -- `runEngine()` falls back to
    /// `input.data32` -- and it is what every fixture in `goldens.txt` was minted
    /// under.
    ///
    ////////////////////////////////////////////////////////////////////////////
    bool const separateSideChain(!monoSideInput.empty());
    LE_ASSERT_MSG(!separateSideChain || (monoSideInput.size() == monoInput.size()),
                  "The side chain must be as long as the main input.");
    std::vector<std::vector<float>> sideChannels(
        separateSideChain ? channels : 0,
        std::vector<float>(monoSideInput.begin(), monoSideInput.end()));

    std::vector<float const *> inputPointers(channels);
    std::vector<float const *> sidePointers(separateSideChain ? channels : 0);
    std::vector<float *> outputPointers(channels);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Two loops, host block outside and engine call inside, which is
    /// `SpectrumWorxCLAP::process()`'s own shape -- including its remainder: the
    /// last call of a block is `blockSize % callSize` and is therefore *smaller*
    /// than the rest whenever the two do not divide. That remainder is not an
    /// edge case to be avoided here; it is what the plugin does on every block
    /// under most host block sizes, and a harness that quietly rounded it away
    /// could not see it.
    ///
    ///   `callSize` is the block size unless a case asked otherwise, so the inner
    /// loop runs once and this is the single loop it replaced. \see
    /// RenderSetup::callSize.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const framesPerCall(setup.framesPerCall());
    LE_ASSERT_MSG(framesPerCall <= setup.blockSize, "A call larger than the engine was sized for.");

    for (std::uint32_t offset(0); offset < frames; offset += setup.blockSize)
    {
        auto const block(std::min<std::uint32_t>(setup.blockSize, frames - offset));

        /// \note Before the block, in the audio thread's role, and once per
        /// *host* block rather than per call -- a host event arrives with the
        /// block. \see Slot::duringRender.
        {
            Threading::ScopedAudioThreadEntry const audioThread;
            for (std::size_t slot(0); slot < slots.size(); ++slot)
                if (modules[slot] && slots[slot].duringRender)
                    slots[slot].duringRender(offset, *modules[slot]);
        }

        for (std::uint32_t cursor(0); cursor < block; cursor += framesPerCall)
        {
            auto const call(std::min<std::uint32_t>(framesPerCall, block - cursor));
            auto const at(offset + cursor);

            for (std::uint8_t channel(0); channel < channels; ++channel)
            {
                inputPointers[channel] = inputChannels[channel].data() + at;
                outputPointers[channel] = outputChannels[channel].data() + at;
                if (separateSideChain)
                    sidePointers[channel] = sideChannels[channel].data() + at;
            }
            engine.process(inputPointers.data(),
                           separateSideChain ? sidePointers.data() : inputPointers.data(),
                           outputPointers.data(), 1.0f, call);
        }
    }

    engine.suspend();

    std::vector<float> interleaved(static_cast<std::size_t>(frames) * channels);
    for (std::uint32_t frame(0); frame < frames; ++frame)
        for (std::uint8_t channel(0); channel < channels; ++channel)
            interleaved[static_cast<std::size_t>(frame) * channels + channel] =
                outputChannels[channel][frame];
    return interleaved;
}

} // namespace SWTest
