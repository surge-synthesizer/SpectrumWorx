////////////////////////////////////////////////////////////////////////////////
///
/// \file engineHarness.hpp
/// -----------------------
///
///   Drives the DSP core without a host. SpectrumWorxCore is abstract only in
/// the abstract sense -- it has no pure virtuals -- but its constructor is
/// protected and Engine::Processor::modules() downcasts to it, so the engine
/// cannot be instantiated except through a derived class. This is that class,
/// plus the deterministic signal generators the goldens are rendered from.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef engineHarness_hpp__1C0F5A3E_74B2_49D6_BC81_2E9A05F3D7B4
#define engineHarness_hpp__1C0F5A3E_74B2_49D6_BC81_2E9A05F3D7B4
//------------------------------------------------------------------------------
#include "core/automatedModuleChain.hpp"
#include "core/spectrumWorxCore.hpp"
#include "core/threading/threadCheck.hpp"

#include "le/spectrumworx/effects/configuration/constants.hpp"
#include "le/spectrumworx/engine/moduleParameters.hpp"
#include "le/spectrumworx/engine/parameters.hpp"
#include "le/spectrumworx/sideChainSource.hpp"
#include "le/utility/buffers.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

namespace SWTest
{

/// \brief The engine, instantiable, with its own Program.
class Engine : public LE::SW::SpectrumWorxCore
{
  public:
    using Core = LE::SW::SpectrumWorxCore;
    using Program = LE::SW::Program;

    Engine() { setProgram(program_); }

    /// SpectrumWorxCore::process is protected; a host reaches it through the
    /// per-format wrapper.
    using Core::process;

    /// Likewise protected, and likewise something only a test asks directly.
    /// Six assertions in the engine depend on it; engineOwnershipTests.cpp is
    /// where that is pinned. See doc/tech/threading_model.md.
    using Core::applyPendingSpectralSetup;
    using Core::currentThreadMayMutateEngineState;
    using Core::spectralSetupPending;

    Program &program() { return program_; }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief One global parameter, straight into the engine.
    ///
    /// \note In the audio thread's role, which is what a test standing in for a
    /// host parameter event actually is: `setGlobalParameter` asserts that the
    /// calling thread owns the engine, and while this one is running that means
    /// the audio thread. Saying so is the same discipline `TestHost::AudioCallback`
    /// imposes on the CLAP cases -- a harness that mutates a running engine
    /// without declaring which role it is playing is not reproducing anything a
    /// host does.
    ///
    ////////////////////////////////////////////////////////////////////////////
    template <class Parameter> bool set(typename Parameter::param_type const value)
    {
        LE::SW::Threading::ScopedAudioThreadEntry const audioThread;
        return Core::setGlobalParameter<Parameter>(*this, value);
    }

    LE::SW::GlobalParameters::Parameters &parameters() { return program_.parameters(); }

    /// \brief What a loaded preset said feeds the side channel.
    ///
    /// \note Recorded rather than acted on: this harness has no sample loader and
    /// no host port, so what it can test about the side chain is the *format's*
    /// half -- which is where the 2.x migration lives. \see PresetLoader.
    LE::SW::SideChainSource sideChainSource() const { return sideChainSource_; }
    void setSideChainSource(LE::SW::SideChainSource const source) { sideChainSource_ = source; }

  private:
    Program program_;
    LE::SW::SideChainSource sideChainSource_{LE::SW::defaultSideChainSource};
}; // class Engine

//------------------------------------------------------------------------------
// Naming an effect
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The effect a preset would name, by the name a preset names it with.
///
/// \note Streaming name and not title, everywhere a test names an effect. A
/// title is a display string and `doc/tech/streaming_format.md` says outright
/// that it is free to move; a streaming name is the one spelling the project
/// has promised never to move, because a file on someone's disk depends on it.
/// Naming effects by title is how a retitle -- "PVD start" to "To PV" -- broke
/// six cases that had nothing to do with it.
///
/// \note The two spellings coincide for every effect that has never been
/// retitled, which is fifty of the fifty-seven, so most call sites read the
/// same as they always did. `Effects::effectStreamingName()` defaults to the
/// title precisely so that they can.
///
////////////////////////////////////////////////////////////////////////////////
std::int8_t effectByStreamingName(std::string_view streamingName);

//------------------------------------------------------------------------------
// Deterministic test signals
//------------------------------------------------------------------------------

/// \note Everything here is generated rather than loaded. The plan asked for
/// "one short real excerpt" as a fourth signal; a licence-clean one is not
/// something this repository has, so Voice below stands in for it -- a
/// harmonic stack with formant shaping and vibrato, which exercises the same
/// machinery (dense partials, a moving pitch) without shipping audio.
enum struct Signal : std::uint8_t
{
    Impulse,
    Sweep,
    PinkNoise,
    Voice
};

constexpr char const *name(Signal const signal)
{
    switch (signal)
    {
    case Signal::Impulse:
        return "impulse";
    case Signal::Sweep:
        return "sweep";
    case Signal::PinkNoise:
        return "pink";
    case Signal::Voice:
        return "voice";
    }
    return "";
}

/// \brief xorshift32. Deterministic across platforms and standard libraries,
/// which std::mt19937 is too but the distributions on top of it are not.
class Rng
{
  public:
    explicit constexpr Rng(std::uint32_t const seed) noexcept : state_(seed | 1u) {}

    constexpr std::uint32_t next() noexcept
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    /// Uniform in [-1, 1), exactly representable, no library dependence.
    constexpr float nextBipolar() noexcept
    {
        return static_cast<float>(static_cast<std::int32_t>(next() >> 8)) / 8388608.0f - 1.0f;
    }

  private:
    std::uint32_t state_;
}; // class Rng

void generate(Signal, std::span<float> mono, float sampleRate);

//------------------------------------------------------------------------------
// Rendering
//------------------------------------------------------------------------------

struct RenderSetup
{
    std::uint16_t fftSize;
    std::uint8_t overlapFactor;
    std::uint8_t numberOfChannels;
    std::uint32_t sampleRate;
    std::uint32_t blockSize;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief How many frames to hand `process()` at a time, where `blockSize` is
    /// what the engine is *configured* for. Zero -- the default -- means "the
    /// block size", which is what every caller before this did and is why nothing
    /// had to change.
    ///
    ///   The two were one field, and that made a whole class of question
    /// unaskable: `blockSize` reaches `Engine::setBlockSize()`, so rendering
    /// twice with different block sizes compares two differently *configured*
    /// engines -- different buffer sizing, different latency -- rather than one
    /// engine called two ways. An attempt to probe chunking transparency that way
    /// reported a worst-sample difference of ~1.0 and meant nothing. \see issue
    /// #79 and core/chunkTransparencyTests.cpp.
    ///
    /// \note A call larger than `blockSize` is a caller error, not a wider test:
    /// the engine sized its buffers for `blockSize` and the harness asserts.
    ///
    ////////////////////////////////////////////////////////////////////////////
    std::uint32_t callSize{0};

    std::uint32_t framesPerCall() const { return callSize ? callSize : blockSize; }
};

/// \brief Runs one effect over one signal and returns the interleaved output.
///
/// \param effectIndex index into Effects::effectsList.hpp, or -1 for a bypassed
///        chain (which the goldens use to pin the engine's own WOLA path).
///
/// \note The side chain is fed the main signal, which is what every fixture
/// minted before 05.08.2026 was rendered under. See renderWithSideChain().
std::vector<float> render(RenderSetup const &, std::int8_t effectIndex, Signal,
                          std::uint32_t frames);

/// \brief The same, with the side chain fed a *different* signal.
///
/// \note The only arrangement in which a side-chain effect can be told apart
/// from one that ignores its side chain entirely: with side == main the two
/// produce the same render, and fourteen shipping effects were pinned only that
/// way. `sideChainTests.cpp` is what says which fourteen and what they do.
std::vector<float> renderWithSideChain(RenderSetup const &, std::int8_t effectIndex, Signal main,
                                       Signal side, std::uint32_t frames);

//------------------------------------------------------------------------------
// Chains
//------------------------------------------------------------------------------

/// \brief One slot of a chain to render: which effect, and what to set on it.
///
/// \note `configure` runs after the module is in the chain and before the first
/// block, which is all a test needs: ModuleDSP::preProcess() calls the effect's
/// setup() every block, so a parameter written here is in force from the first
/// one. Indices are into the effect's own LE_DEFINE_PARAMETERS order and values
/// are in the parameter's own units -- Hz, dB, cents, or an enumerator.
///
/// \note The base parameters (Bypass, Gain, Wet, StartFrequency, StopFrequency)
/// are reachable the same way through setBaseParameter(), and are what the
/// transparency properties are written against.
struct Slot
{
    std::int8_t effectIndex{-1};
    std::function<void(LE::SW::Engine::ModuleParameters &)> configure{};

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Called before every block, where `configure` is called once before
    /// the first -- for a property about something that happens *during* a
    /// render rather than to its settings.
    ///
    ///   A trigger is the case that needs it: Freeze and Melt are events, and an
    /// event set before the first block is indistinguishable from an effect that
    /// is always frozen. What says the trigger works is the signal changing at
    /// the moment it is fired and not before.
    ///
    /// \param frameOffset where the block about to be rendered starts, so a case
    ///        can say "half a second in" rather than counting blocks.
    ///
    /// \note Called inside a `ScopedAudioThreadEntry`, because that is what it
    /// is: a host's parameter event arrives on the audio thread, and the engine
    /// asserts that whoever mutates it while it runs is that thread. \see
    /// Engine::set(), which says the same thing about a global.
    ///
    ////////////////////////////////////////////////////////////////////////////
    std::function<void(std::uint32_t frameOffset, LE::SW::Engine::ModuleParameters &)>
        duringRender{};
};

/// \brief Runs a whole chain, one effect per slot, and returns the interleaved
/// output.
///
/// The single-effect render() above is this with one default-configured slot;
/// what this adds is the two things a property needs and a golden does not --
/// several effects at once (PVD start, something, PVD stop) and a parameter set
/// to something other than its default.
std::vector<float> renderChain(RenderSetup const &, std::span<Slot const>, Signal,
                               std::uint32_t frames);

/// \brief The same, over a signal the caller generated.
///
/// The four Signal generators are what the goldens need: broadband, dense,
/// deliberately awkward. A property about *pitch* wants the opposite -- one
/// partial, so that "the dominant frequency moved" has an unambiguous subject --
/// and a property about an envelope wants a note that starts and stops. Both are
/// three lines at the call site and neither belongs in the golden enum.
///
/// \param monoSideInput what the side chain gets, or empty for the main signal
///        -- which is what a host does when nothing is patched into the side
///        chain port, and what every caller did before there was a parameter.
std::vector<float> renderChain(RenderSetup const &, std::span<Slot const>,
                               std::span<float const> monoInput,
                               std::span<float const> monoSideInput = {});

} // namespace SWTest

#endif // engineHarness_hpp
