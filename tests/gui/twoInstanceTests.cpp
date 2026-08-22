////////////////////////////////////////////////////////////////////////////////
///
/// twoInstanceTests.cpp
/// --------------------
///
///   What two instances of the plugin in one process share, and must not.
///
///   SpectrumWorx deadlocks in Logic and in Bitwig with two instances open, and
/// the first cause found for that was that closing one editor tore JUCE down
/// under the other: `ReferenceCountedGUIInitializationGuard` called
/// `juce::shutdownJuce_GUI()` directly, outside the `numScopedInitInstances`
/// counter `juce::ScopedJuceInitialiser_GUI` uses -- so
/// `DeletedAtShutdown::deleteAll()` and `MessageManager::deleteInstance()` ran
/// while the CLAP shim, and the other instance, still held live references.
/// The counter and both singletons are one per binary, which is to say shared by
/// every instance of this plugin the host has loaded.
///
///   The editors are constructed directly rather than through the shim, as
/// tools/show-ui does: what is being tested is our own lifetime bookkeeping, and
/// the shim's half is stood in for by the ScopedJuceInitialiser_GUI each case
/// holds -- which is exactly the reference the old code deleted out from under.
///
/// See doc/tech/threading_model.md.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/editorHarness.hpp"
#include "presets/presetHarness.hpp"

#include "core/automatedModuleChain.hpp"

/// \note Before anything that names SW::Module: loadPreset() is a template over
/// its consumer and downcasts a chain node to it, so this translation unit needs
/// the complete type -- and this is the header that has it.
#include "core/modules/moduleDSPAndGUI.hpp"

#include "gui/theme.hpp"

#include "le/spectrumworx/presetStorage.hpp"
#include "le/spectrumworx/presets.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
using namespace LE;
using namespace LE::SW;

/// \note Instance, SilentNotifications and HostSideJuce were defined here until
/// moduleControlFocusTests.cpp needed an editor too; they are in
/// gui/editorHarness.hpp now, unchanged.
using SWTest::HostSideJuce;
using SWTest::Instance;

using Editor = GUI::SpectrumWorxEditor;
} // anonymous namespace

TEST_CASE("Closing one editor leaves JUCE standing for the other", "[gui][two-instances]")
{
    HostSideJuce const juceIsUp;
    REQUIRE(juce::MessageManager::getInstanceWithoutCreating() != nullptr);

    Instance a, b;

    /// \note Overlay placement, which is not the default: this case is about
    /// JUCE's lifetime and wants a known editor size to check the survivor
    /// against. The default builds a preset browser into every editor, which is
    /// a second thing to go wrong in a case about the first.
    a.openEditor(Editor::PanelPlacement::overlay);
    b.openEditor(Editor::PanelPlacement::overlay);
    CHECK(GUI::Theme::haveSingleton());

    a.closeEditor();

    // The decisive one. Against the previous implementation this is null: our own
    // reference count reached zero and called shutdownJuce_GUI(), deleting the
    // MessageManager that `juceIsUp` -- standing in for the shim -- still holds.
    CHECK(juce::MessageManager::getInstanceWithoutCreating() != nullptr);
    CHECK(GUI::Theme::haveSingleton());

    // ...and the survivor is still a working editor.
    CHECK(b.editor().getWidth() == GUI::SpectrumWorxEditor::estimatedWidth);

    // Reopening the one that closed is the case a host meets when a user toggles
    // a window, and it is where the old teardown produced a MessageManager that
    // nobody had declared a thread for.
    a.openEditor();
    CHECK(juce::MessageManager::getInstanceWithoutCreating() != nullptr);

    b.closeEditor();
    a.closeEditor();

    // Both gone, and JUCE is still up because something above us holds it.
    CHECK(juce::MessageManager::getInstanceWithoutCreating() != nullptr);
    CHECK(!GUI::Theme::haveSingleton());
}

TEST_CASE("Selection and the active control belong to an editor, not to the process",
          "[gui][two-instances]")
{
    HostSideJuce const juceIsUp;

    Instance a, b;
    a.openEditor();
    b.openEditor();

    // They were one pointer each, file-scope, shared by every instance in the
    // host. That they are addressable per editor at all is the change; that both
    // start empty and stay independent is what a headless case can say without a
    // window server to give something focus.
    CHECK(a.editor().selectedModule() == nullptr);
    CHECK(b.editor().selectedModule() == nullptr);
    CHECK(a.editor().activeControl() == nullptr);
    CHECK(b.editor().activeControl() == nullptr);

    a.closeEditor();

    // Nothing about b changed because a went away. Against the shared statics,
    // whatever a had selected would have been b's answer too -- and freed.
    CHECK(b.editor().selectedModule() == nullptr);
    CHECK(b.editor().activeControl() == nullptr);

    b.closeEditor();
}

TEST_CASE("Adding and removing modules keeps the rack and the chain in step", "[gui][modules]")
{
    // Ejecting a module had no headless coverage of any kind, and the first thing
    // it did after the strips became the editor's was assert "Invalid downcast":
    // nothing dropped a strip when its module left the chain, so the strip kept
    // the removed module alive and on screen, and the next removal walked off the
    // end of the chain and onto its sentinel node. Both faults are pinned here --
    // the walk, and the rack agreeing with the chain afterwards.
    HostSideJuce const juceIsUp;

    Instance instance;
    instance.openEditor();
    auto &editor(instance.editor());
    auto &chain(instance.core().program().moduleChain());

    /// \note In the plugin this arrives as a posted message, because removal is
    /// reached from inside the strip's own button callback and the strip cannot
    /// be destroyed under it. A test has no message loop to pump -- the shim
    /// builds JUCE with modal loops disabled -- and what is worth pinning is the
    /// resync's effect rather than JUCE's delivery of it.
    auto const settle([&] { editor.resyncModuleRack(); });

    for (std::uint8_t effect(0); effect < 3; ++effect)
        editor.addUserAddedModule(static_cast<std::int8_t>(effect));
    settle();

    REQUIRE(chain.size() == 3);
    REQUIRE(editor.regionInSlot(0) != nullptr);
    REQUIRE(editor.regionInSlot(1) != nullptr);
    REQUIRE(editor.regionInSlot(2) != nullptr);

    // The middle one, so the walk below has something to shuffle.
    editor.removeModule(*editor.regionInSlot(1));
    settle();

    CHECK(chain.size() == 2);

    // The rack matches the chain: two strips, in slots 0 and 1, and the one that
    // was in slot 2 has moved down rather than been left where it was.
    REQUIRE(editor.regionInSlot(0) != nullptr);
    REQUIRE(editor.regionInSlot(1) != nullptr);
    CHECK(editor.regionInSlot(0)->getX() == GUI::ModuleUI::horizontalOffset);
    CHECK(editor.regionInSlot(1)->getX() ==
          GUI::ModuleUI::horizontalOffset + GUI::ModuleUI::width + GUI::ModuleUI::distance);

    // ...and the strips that are left are the modules that are left.
    CHECK(&editor.regionInSlot(0)->module() == chain.moduleAs<SW::Module>(0).get());
    CHECK(&editor.regionInSlot(1)->module() == chain.moduleAs<SW::Module>(1).get());

    // Emptying the rack from the front, one at a time, is the case that overran.
    editor.removeModule(*editor.regionInSlot(0));
    settle();
    CHECK(chain.size() == 1);
    editor.removeModule(*editor.regionInSlot(0));
    settle();
    CHECK(chain.size() == 0);
    CHECK(editor.regionInSlot(0) == nullptr);

    /// \note No control of a dropped strip is left as the active one. Destroying
    /// a strip makes JUCE move the focus and deliver the loss to whichever
    /// control had it, which re-enters the editor -- see
    /// SpectrumWorxEditor::detachFrom(). Offscreen JUCE refuses keyboard focus
    /// (`juce_Component.cpp:2752`), so this checks the state that re-entrancy
    /// needs rather than the re-entrancy itself.
    CHECK(editor.activeControl() == nullptr);

    instance.closeEditor();
}

TEST_CASE("A strip whose module has already gone does not walk off the chain", "[gui][modules]")
{
    // The reported assertion, reproduced. Removing a module leaves its strip on
    // screen until the posted resync runs -- it is still a child of the editor
    // and still clickable -- and that strip's module is no longer in the chain.
    // Ejecting it walks the intrusive list from an unlinked node, which used to
    // step onto the chain's sentinel root and downcast it to a Module:
    //
    //   LE SDK assertion failure, ERROR: Invalid downcast.
    //     dynamic_cast<Target>(pSource) == pSource
    //     Target = LE::SW::Module *, Source = LE::SW::Engine::ModuleNode
    //
    // Remove the isEnd() guard from moveModules() and this comes back.
    HostSideJuce const juceIsUp;

    Instance instance;
    instance.openEditor();
    auto &editor(instance.editor());
    auto &chain(instance.core().program().moduleChain());

    for (std::uint8_t effect(0); effect < 3; ++effect)
        editor.addUserAddedModule(static_cast<std::int8_t>(effect));
    REQUIRE(chain.size() == 3);

    /// \note A strip appears when the rack is resynced, not when the module is
    /// created: the two are one step apart now, because with audio running the
    /// engine answers a slot change a block later. See
    /// SpectrumWorxEditor::resyncModuleRack().
    editor.resyncModuleRack();

    /// \note The *last* strip specifically. Its slot index is the highest, so
    /// after the removal `nextAvailableModuleSlot_` drops below it -- and
    /// `lastModuleIndex - firstModuleIndex` is then -1 in a `std::uint8_t`, which
    /// is 255 walks along a chain of two.
    auto *const pStrip(editor.regionInSlot(2));
    REQUIRE(pStrip != nullptr);
    /// \note A strip knows which slot it is drawn in, because that is what the
    /// removal below is judged against; nothing recovers it from `getX()` any
    /// more. `ModuleUI::moveToSlot()` did not in fact record it until this case
    /// asked, and `slot()` had no other reader to notice.
    CHECK(int(pStrip->slot()) == 2);

    // No resync in between: this is the window the plugin is in between the
    // removal and the posted refresh, and the strip is still on screen.
    editor.removeModule(*pStrip);
    REQUIRE(chain.size() == 2);

    // The same strip again, whose module is no longer in the chain. This is the
    // click that asserted.
    editor.removeModule(*pStrip);

    // It declined, rather than walking: the chain is untouched, and the rack can
    // be brought back into agreement with it.
    CHECK(chain.size() == 2);
    editor.resyncModuleRack();
    for (std::uint8_t slot(0); slot < chain.size(); ++slot)
        CHECK(editor.regionInSlot(slot) != nullptr);

    instance.closeEditor();
}

TEST_CASE("A preset load counts its problems instead of raising dialogs",
          "[gui][two-instances][presets]")
{
    // Symptom 1: the preset load test leaks a juce object. The default reporter
    // was GUI::warningMessageBox, one per problem, and a 2011 preset does not
    // mention the parameters its effect grew later -- 806 across the factory
    // banks. This case installs no reporter of its own on purpose: if the default
    // still raised dialogs, JUCE's leak detector would fail the run at exit.
    std::filesystem::path const bank(std::filesystem::path(SW_PRESET_DATA_DIR) / "Voices");
    REQUIRE(std::filesystem::is_directory(bank));

    takePresetLoadReport(); // whatever an earlier case left

    // The whole bank rather than one file: how many parameters a preset fails to
    // mention depends on which effects it uses and when it was written, and
    // plenty of them mention everything. What matters is that a run over real
    // 2011 files produces a count at all.
    unsigned int loaded(0);
    for (auto const &entry : std::filesystem::directory_iterator(bank))
    {
        if (entry.path().extension() != ".swp")
            continue;

        auto const presetData(readPresetFile(entry.path()));
        REQUIRE(static_cast<bool>(presetData));
        std::string_view const text(presetData.get());
        std::vector<char> buffer(text.begin(), text.end());
        buffer.push_back('\0'); // the parse is destructive and wants a terminator

        /// \note One engine per preset, as presetCorpusTests.cpp does: loading B
        /// on top of A is a merge, and it is not what this case is about.
        SWTest::Engine engine;
        engine.setNumberOfChannels(2, 2);
        engine.setSampleRate(48000);
        engine.setBlockSize(512);
        REQUIRE(engine.initialise());

        REQUIRE(LE::SW::loadPreset(buffer.data(), true /*ignore external samples*/, nullptr,
                                   SWTest::PresetConsumer{engine}));
        ++loaded;
    }
    REQUIRE(loaded > 0);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `missingParameters > 0` was checked here until 14.08.2026, to say
    /// the run had produced a count at all. It made tidying the bank a failure:
    /// re-save these presets from this build and every one of them mentions
    /// every parameter its effect has, the count goes to zero, and the test
    /// reddens for an improvement.
    ///
    ///   What the case is actually about is the reporter -- that a problem is
    /// *counted* rather than raised as a dialog -- and the proof of that is the
    /// absence of a leak at exit, which no assertion here can spell because JUCE
    /// reports it from its own destructor. `failures == 0` is the rest.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const report(takePresetLoadReport());
    INFO("over " << loaded << " presets, " << report.missingParameters << " missing parameters");
    CHECK(report.failures == 0);

    // And taking it clears it, so the next load's summary is the next load's.
    CHECK(takePresetLoadReport().total() == 0);
}
