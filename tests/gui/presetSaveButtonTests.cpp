////////////////////////////////////////////////////////////////////////////////
///
/// presetSaveButtonTests.cpp
/// -------------------------
///
///   What the browser's two Save buttons offer, and when. Issue #177.
///
///   They used to follow the *listing*: Save was lit whenever a user preset row
/// was selected and Save As whenever the browser was anywhere in the user tree,
/// whether or not there was anything to save. A user who edited a factory preset
/// -- the common way to arrive at a sound worth keeping -- had neither.
///
///   They follow the preset that is *playing* now, and whether it has been
/// edited since it arrived.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/editorHarness.hpp"

/// \note Before anything that names SW::Module, as elsewhere.
#include "core/modules/moduleDSPAndGUI.hpp"

#include "gui/preset_browser/presetBrowser.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using LE::SW::GUI::PanelState;
using LE::SW::GUI::PresetBrowser;

/// \brief An editor with the preset browser showing.
PresetBrowser &browserOf(SWTest::Instance &instance)
{
    instance.openEditor();
    instance.editor().showPresetBrowser(true);
    auto *const pBrowser(instance.editor().presetBrowser());
    REQUIRE(pBrowser != nullptr);
    pBrowser->updateSaveButtons();
    return *pBrowser;
}

/// \brief What loading \p name from \p location leaves behind. \see
/// PresetBrowser::rememberLoadedPreset(), which is the one that runs for real.
void pretendLoaded(SWTest::Instance &instance, juce::String const &name,
                   PanelState::PresetLocation const location, std::filesystem::path const &file)
{
    auto &loaded(instance.loadedPreset());
    loaded.loaded(name, location);
    loaded.file = file;
}

void edit(SWTest::Instance &instance)
{
    instance.loadedPreset().modified.store(true, std::memory_order_relaxed);
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("Neither Save button is offered until something is edited", "[gui][presets]")
{
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    auto &browser(browserOf(instance));

    CHECK_FALSE(browser.saveIsOffered());
    CHECK_FALSE(browser.saveAsIsOffered());

    ////////////////////////////////////////////////////////////////////////////
    /// \note And a preset that has only been *listened* to is not an edit. This
    /// is the case the old rule got wrong in the other direction: Save was lit
    /// for any selected user preset, so the button was always available and
    /// never meant anything.
    ////////////////////////////////////////////////////////////////////////////
    pretendLoaded(instance, "Robokid", PanelState::PresetLocation::user, "/tmp/Robokid.swp");
    browser.updateSaveButtons();

    CHECK_FALSE(browser.saveIsOffered());
    CHECK_FALSE(browser.saveAsIsOffered());
}

TEST_CASE("An edited factory preset offers Save As and not Save", "[gui][presets]")
{
    ////////////////////////////////////////////////////////////////////////////
    /// \note There is nothing in the binary to overwrite, so Save has nowhere to
    /// go -- but the edit is exactly the thing a user wants to keep, and Save As
    /// is how they keep it. \see LoadedPreset::canBeOverwritten().
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    auto &browser(browserOf(instance));

    pretendLoaded(instance, "Robokid", PanelState::PresetLocation::factory, {});
    edit(instance);
    browser.updateSaveButtons();

    CHECK(browser.saveAsIsOffered());
    CHECK_FALSE(browser.saveIsOffered());
}

TEST_CASE("An edited user preset offers both", "[gui][presets]")
{
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    auto &browser(browserOf(instance));

    pretendLoaded(instance, "Mine", PanelState::PresetLocation::user, "/tmp/Mine.swp");
    edit(instance);
    browser.updateSaveButtons();

    CHECK(browser.saveAsIsOffered());
    CHECK(browser.saveIsOffered());
}

TEST_CASE("A user preset with no file behind it offers only Save As", "[gui][presets]")
{
    ////////////////////////////////////////////////////////////////////////////
    /// \note What a fresh instance is: nothing has been loaded, so there is no
    /// file to write back to, and everything the user has built is an edit.
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    auto &browser(browserOf(instance));

    edit(instance);
    browser.updateSaveButtons();

    CHECK(browser.saveAsIsOffered());
    CHECK_FALSE(browser.saveIsOffered());
}

TEST_CASE("The editor's timer keeps the Save buttons current", "[gui][presets]")
{
    ////////////////////////////////////////////////////////////////////////////
    /// \note The flag is set by any parameter write, host automation included,
    /// and nothing about that marks a pixel of the panel dirty -- so the panel
    /// polls, as it does for the engine information lines. A case that called
    /// updateSaveButtons() by hand everywhere would not notice that going away.
    /// \see SpectrumWorxEditor::timerCallback() and issue #142.
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    auto &browser(browserOf(instance));
    REQUIRE_FALSE(browser.saveAsIsOffered());

    edit(instance);
    instance.editor().updateSaveButtonsIfShowing();

    CHECK(browser.saveAsIsOffered());
}
