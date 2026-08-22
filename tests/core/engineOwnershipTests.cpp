////////////////////////////////////////////////////////////////////////////////
///
/// engineOwnershipTests.cpp
/// ------------------------
///
///   Who owns the engine, and what follows from it.
///
///   This was processLockTests.cpp, and everything it pinned was a property of a
/// lock that no longer exists: that `SpectrumWorxCore::process()` took it with
/// `try_lock`, that it returned false when it could not have it, and that the
/// CLAP wrote silence when that happened. The lock is gone (stage 6), so those
/// are not weaker properties now -- they are the wrong questions. The right ones
/// are here.
///
///   The invariant that replaced the lock: **the audio thread owns the engine
/// while one exists, and the main thread owns it while one does not**. Nothing
/// waits, nothing is dropped, and the two never touch the same thing at once.
/// `SpectrumWorxCore::currentThreadMayMutateEngineState()` is that sentence, and
/// six assertions in the engine are written against it.
///
/// See doc/tech/threading_model.md §2 and §5.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "goldens/engineHarness.hpp"

#include "core/automatedModuleChain.hpp"
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/threading/publish.hpp"
#include "core/threading/threadCheck.hpp"

#include "le/spectrumworx/engine/parameters.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <new>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
constexpr std::uint32_t blockSize{256};
constexpr std::uint8_t channels{2};
constexpr float sampleRate{48000};

/// An engine taken as far as a host takes it before the first block.
class RunningEngine
{
  public:
    RunningEngine()
    {
        engine_.setNumberOfChannels(channels, channels);
        engine_.setSampleRate(sampleRate);
        engine_.setBlockSize(blockSize);
        REQUIRE(engine_.initialise());
        engine_.resume();
    }

    ~RunningEngine() { engine_.suspend(); }

    RunningEngine(RunningEngine const &) = delete; // makes non-copyable
    RunningEngine &operator=(RunningEngine const &) = delete;

    SWTest::Engine &operator*() { return engine_; }
    SWTest::Engine *operator->() { return &engine_; }

  private:
    SWTest::Engine engine_;
}; // class RunningEngine

/// One block of whatever \p input holds, into \p output, through the engine.
///
/// \note Under a ScopedAudioThreadEntry, which is what a host's process() opens and
/// what makes `Threading::isAudioThread()` true -- so the engine's own
/// assertions see what they would see in a DAW rather than what a test happens
/// to be doing.
void runOneBlock(SWTest::Engine &engine, std::vector<std::vector<float>> &input,
                 std::vector<std::vector<float>> &output)
{
    std::vector<float const *> inputPointers(channels);
    std::vector<float *> outputPointers(channels);
    for (std::uint8_t channel(0); channel < channels; ++channel)
    {
        inputPointers[channel] = input[channel].data();
        outputPointers[channel] = output[channel].data();
    }

    LE::SW::Threading::ScopedAudioThreadEntry const audioCallback;
    engine.process(inputPointers.data(), inputPointers.data(), outputPointers.data(), 1.0f,
                   blockSize);
}

/// A buffer pattern no engine output can be mistaken for.
constexpr float sentinel{-12345.0f};

bool isUntouched(std::vector<std::vector<float>> const &output)
{
    return std::all_of(output.begin(), output.end(), [](std::vector<float> const &channel) {
        return std::all_of(channel.begin(), channel.end(),
                           [](float const sample) { return sample == sentinel; });
    });
}

std::vector<std::vector<float>> makeInput()
{
    std::vector<std::vector<float>> input(channels, std::vector<float>(blockSize));
    SWTest::generate(SWTest::Signal::Sweep, input[0], sampleRate);
    input[1] = input[0];
    return input;
}
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
///
/// \note Constructed over deliberately dirtied storage, because that is the only
/// way to fail deterministically. Program has no constructor, so the question is
/// whether its members carry default member initialisers; a plain
/// `std::array<char, 24> name_;` is default-initialised and its bytes are
/// whatever was in the memory. On a first instance in a fresh process that is
/// almost always a zeroed page, which is why the symptom -- a garbage name in
/// the editor's Save-As field -- was reported once and then would not
/// reproduce.
///
///   Only a preset load writes this array, so every instance that has not
/// loaded one depends on the initialiser. Nothing here reads uninitialised
/// memory: the placement-new default-initialises, and reading a char array that
/// the initialiser was supposed to have zeroed is exactly the thing under test.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A program that has loaded no preset has an empty name", "[core][program]")
{
    alignas(LE::SW::Program) std::byte storage[sizeof(LE::SW::Program)];
    std::memset(storage, 0x5A, sizeof(storage));

    auto *const pProgram(new (static_cast<void *>(storage)) LE::SW::Program);

    CHECK(pProgram->name().front() == '\0');
    CHECK(std::all_of(pProgram->name().begin(), pProgram->name().end(),
                      [](char const character) { return character == '\0'; }));

    pProgram->~Program();
}

TEST_CASE("Who may mutate the engine follows from whether anything is processing",
          "[core][ownership]")
{
    SWTest::Engine engine;

    // Nothing is running, so this thread owns it outright -- which is how a
    // preset load, a slot change and activate() itself are legal at all.
    CHECK(!engine.engineIsRunning());
    CHECK(engine.currentThreadMayMutateEngineState());

    engine.setNumberOfChannels(channels, channels);
    engine.setSampleRate(sampleRate);
    engine.setBlockSize(blockSize);
    REQUIRE(engine.initialise());
    engine.resume();

    // Now something is, and this thread is not it.
    CHECK(engine.engineIsRunning());
    CHECK(!engine.currentThreadMayMutateEngineState());

    // Unless it is: inside a process callback, the audio thread is the owner.
    {
        LE::SW::Threading::ScopedAudioThreadEntry const audioCallback;
        CHECK(engine.currentThreadMayMutateEngineState());
    }
    CHECK(!engine.currentThreadMayMutateEngineState());

    engine.suspend();
    CHECK(engine.currentThreadMayMutateEngineState());
}

TEST_CASE("Every block is processed and written", "[core][ownership]")
{
    // The property the lock cost us. `process()` returned false whenever another
    // thread held the processing lock, and every caller then had to decide what
    // the host heard instead -- silence, in the end, because the alternative was
    // the previous plugin's output at full level. There is no such case now:
    // nothing else can be inside the engine while this runs.
    RunningEngine engine;

    auto input(makeInput());
    std::vector<std::vector<float>> output(channels, std::vector<float>(blockSize, sentinel));

    runOneBlock(*engine, input, output);

    // Silence would be a legitimate first block -- the engine has latency -- but
    // the sentinel would not, and that is the whole question here.
    CHECK(!isUntouched(output));
}

TEST_CASE("A spectral change while running waits, and lands when it stops", "[core][ownership]")
{
    // The FFT size was the worst path in the old model: it took the processing
    // lock, blocking, on the message thread, and reallocated the entire spectral
    // working set under it while audio ran. It is a deferral now -- the
    // parameter moves, the setup does not, and the plugin asks the host to
    // restart. See SpectrumWorxCLAP::deactivate().
    using namespace LE::SW::GlobalParameters;

    RunningEngine engine;

    auto const originalFFTSize(engine->uncheckedEngineSetup().fftSize<unsigned int>());
    REQUIRE(originalFFTSize != 1024);
    CHECK(!engine->spectralSetupPending());

    CHECK(engine->set<FFTSize>(1024));

    // The parameter is what the user asked for...
    CHECK(engine->parameters().get<FFTSize>() == 1024);
    // ...and the engine is still running what it was running.
    CHECK(engine->uncheckedEngineSetup().fftSize<unsigned int>() == originalFFTSize);
    CHECK(engine->spectralSetupPending());

    // And blocks keep coming through, at the old size, rather than being
    // dropped for as long as the message thread holds a lock.
    auto input(makeInput());
    std::vector<std::vector<float>> output(channels, std::vector<float>(blockSize, sentinel));
    runOneBlock(*engine, input, output);
    CHECK(!isUntouched(output));

    // The restart. This is what clap_plugin::deactivate does.
    engine->suspend();
    CHECK(engine->applyPendingSpectralSetup());

    CHECK(!engine->spectralSetupPending());
    CHECK(engine->engineSetup().fftSize<unsigned int>() == 1024);
}

TEST_CASE("A spectral change with nothing running is applied at once", "[core][ownership]")
{
    // The other half, and the reason there is no queue in it: with no audio
    // thread the main thread owns the engine and simply does the work. Which is
    // what every headless test, and a host setting parameters before activate(),
    // relies on.
    using namespace LE::SW::GlobalParameters;

    SWTest::Engine engine;
    engine.setNumberOfChannels(channels, channels);
    engine.setSampleRate(sampleRate);
    engine.setBlockSize(blockSize);
    REQUIRE(engine.initialise());

    CHECK(engine.set<FFTSize>(1024));
    CHECK(!engine.spectralSetupPending());
    CHECK(engine.engineSetup().fftSize<unsigned int>() == 1024);
}

TEST_CASE("A published chain comes back holding what it displaced", "[core][ownership]")
{
    // Publish and retire, in the one line that matters: the caller hands over a
    // chain it built and gets the same object back full of the old modules, to
    // destroy wherever it likes. Nothing is freed where the swap happened.
    using LE::SW::AutomatedModuleChain;
    using LE::SW::Threading::createModuleForSlot;
    using LE::SW::Threading::publishChain;

    SWTest::Engine engine;
    engine.setNumberOfChannels(channels, channels);
    engine.setSampleRate(sampleRate);
    engine.setBlockSize(blockSize);
    REQUIRE(engine.initialise());

    LE::SW::Threading::ToEngineQueue toEngine;

    // One module, installed the ordinary way.
    LE::SW::Threading::publishSlot(engine, toEngine, 0, 0, createModuleForSlot(engine, 0, 0));
    REQUIRE(engine.moduleChain().size() == 1);
    auto const *const pFirst(engine.moduleChain().moduleAs<LE::SW::Module>(0).get());
    REQUIRE(pFirst != nullptr);

    // A whole chain over the top of it.
    AutomatedModuleChain replacement;
    auto *const pSecond(createModuleForSlot(engine, 1, 0));
    REQUIRE(pSecond != nullptr);
    replacement.push_back(LE::SW::Engine::node(*pSecond));
    intrusive_ptr_release(&LE::SW::Engine::node(*pSecond)); // the chain owns it now

    publishChain(engine, toEngine, replacement);

    CHECK(engine.moduleChain().size() == 1);
    CHECK(engine.moduleChain().moduleAs<LE::SW::Module>(0).get() == pSecond);

    // And the caller's chain is holding the module that came out, still alive
    // and still this thread's to destroy.
    REQUIRE(replacement.size() == 1);
    CHECK(&LE::SW::Engine::actualModule<LE::SW::Module>(*replacement.module(0)) == pFirst);
}

TEST_CASE("A displaced module may outlive the chain it came out of", "[core][ownership]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Found by loading a preset with the editor open, and it is the case
    /// the one above deliberately does not cover: there, nothing else owned the
    /// displaced module, so destroying the retired chain destroyed it too. Here
    /// something does -- which is every preset load with a window open, because
    /// the editor's strips hold a counted reference to each module they draw.
    ///
    ///   `unlink()` writes the *neighbours*' links and leaves the removed node's
    /// own pointing where they were, so an ex-member still holds a counted
    /// pointer at the chain's root. That was free for as long as a chain outlived
    /// every module that had been in it; a retired chain does not, and the root
    /// was freed with those pointers still aimed at it. What follows is the
    /// module's `IntrusivePtr` decrementing a refcount in freed memory the next
    /// time a strip is dropped.
    ///
    ///   Remove the two `reset()` calls from `~ModuleChainBase` and this reddens
    /// on its own assertion, before ASan has to be asked.
    ///
    ////////////////////////////////////////////////////////////////////////////
    using LE::SW::AutomatedModuleChain;
    using LE::SW::Threading::createModuleForSlot;
    using LE::SW::Threading::publishChain;

    SWTest::Engine engine;
    engine.setNumberOfChannels(channels, channels);
    engine.setSampleRate(sampleRate);
    engine.setBlockSize(blockSize);
    REQUIRE(engine.initialise());

    LE::SW::Threading::ToEngineQueue toEngine;

    LE::SW::Threading::publishSlot(engine, toEngine, 0, 0, createModuleForSlot(engine, 0, 0));
    REQUIRE(engine.moduleChain().size() == 1);

    /// \note A strip's reference, in the one respect that matters here: somebody
    /// other than the chain owns the module.
    LE::Utility::IntrusivePtr<LE::SW::Module> const pStripsModule(
        engine.moduleChain().moduleAs<LE::SW::Module>(0));
    REQUIRE(pStripsModule);

    {
        AutomatedModuleChain replacement;
        auto *const pSecond(createModuleForSlot(engine, 1, 0));
        REQUIRE(pSecond != nullptr);
        replacement.push_back(LE::SW::Engine::node(*pSecond));
        intrusive_ptr_release(&LE::SW::Engine::node(*pSecond));

        publishChain(engine, toEngine, replacement);
        REQUIRE(replacement.size() == 1);
    } // the retired chain dies here, and the displaced module does not

    // Still usable, and holding nothing that has been freed.
    CHECK(pStripsModule->effectTypeIndex() == 0);
    CHECK(engine.moduleChain().size() == 1);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note What `SpectrumWorxCLAP::editSlot` does to `programMain_`, which is the
/// chain the editor's strips are built from -- `SpectrumWorxEditor::moduleChain()`
/// is `editorHost_.programMain().moduleChain()`. The engine's copy is a different
/// chain holding different module objects, so the retire ring cannot be what a
/// strip is looking at: whatever frees a strip's module has to happen here.
///
////////////////////////////////////////////////////////////////////////////////

struct ParametersOnly
{
    using Module = LE::SW::Module;
    bool operator()(Module &, std::uint8_t) const { return true; }
}; // struct ParametersOnly

TEST_CASE("Emptying a slot leaves the strip drawing it alive", "[core][ownership]")
{
    LE::SW::Program programMain;

    programMain.moduleChain().setParameter(0, 0, ParametersOnly{});
    REQUIRE(programMain.moduleChain().size() == 1);

    /// \note A strip's reference: ModuleUI holds an IntrusivePtr to the module it
    /// draws, for its whole life.
    LE::Utility::IntrusivePtr<LE::SW::Module> const pStripsModule(
        programMain.moduleChain().moduleAs<LE::SW::Module>(0));
    REQUIRE(pStripsModule);

    programMain.moduleChain().setParameter(0, LE::SW::noModule, ParametersOnly{});
    CHECK(programMain.moduleChain().size() == 0);

    /// \note The strip outlives the slot: it is dropped by a *posted* message
    /// (`refreshModuleRackAsync`), so between the edit and the rebuild it is still
    /// parented and still painted -- and painting a knob reads exactly this.
    CHECK(pStripsModule->effectTypeIndex() == 0);
    CHECK(pStripsModule->numberOfParameters() > 0);
    CHECK(pStripsModule->numberOfLFOControledParameters() > 0);
}

TEST_CASE("A slot change while running is a command rather than a mutation", "[core][ownership]")
{
    // With audio running the same call queues instead, and nothing happens to
    // the chain until something drains it -- which is what makes the chain the
    // audio thread's alone.
    using LE::SW::Threading::createModuleForSlot;

    RunningEngine engine;
    LE::SW::Threading::ToEngineQueue toEngine;

    auto *const pModule(createModuleForSlot(*engine, 0, 0));
    REQUIRE(pModule != nullptr);
    LE::SW::Threading::publishSlot(*engine, toEngine, 0, 0, pModule);

    CHECK(engine->moduleChain().size() == 0);

    // The drain, as process() does it.
    LE::SW::Threading::ToEngine command{};
    REQUIRE(toEngine.pop(command));
    CHECK(command.kind == LE::SW::Threading::ToEngine::Kind::SetSlot);
    {
        LE::SW::Threading::ScopedAudioThreadEntry const audioCallback;
        CHECK(engine->installModuleInSlot(command.setSlot.slot,
                                          static_cast<LE::SW::Module *>(command.setSlot.pModule)) ==
              nullptr);
    }
    CHECK(engine->moduleChain().size() == 1);
}
