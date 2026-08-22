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

#include "le/spectrumworx/factoryPresets.hpp"

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

////////////////////////////////////////////////////////////////////////////////
///
/// \note Issue #180. The comment box was enabled only while a *user* preset was
/// selected, so a note about a factory preset -- or about a sound built from
/// nothing -- could not be typed at all. It is always editable now, and what is
/// typed is an edit of the session like any other.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The comment area is editable with nothing selected", "[gui][presets]")
{
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    auto &browser(browserOf(instance));

    CHECK(browser.comment().isEnabled());
}

TEST_CASE("Typing a comment is an edit of the session", "[gui][presets]")
{
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    auto &browser(browserOf(instance));

    pretendLoaded(instance, "Robokid", PanelState::PresetLocation::factory, {});
    browser.updateSaveButtons();
    REQUIRE_FALSE(browser.saveAsIsOffered());
    auto const before(instance.stateModifications);

    /// \note The text, and then the callback by hand: juce::TextEditor *posts*
    /// its text-change message and a test binary has no message loop to deliver
    /// it. \see PresetBrowser::commentChanged().
    browser.comment().setText("a note about this sound", juce::dontSendNotification);
    browser.commentChanged();

    // The host has been told...
    CHECK(instance.stateModifications > before);
    // ...the plugin is carrying it...
    CHECK(instance.loadedPreset().comment == "a note about this sound");
    // ...and Save As is the way to keep it, even for a factory preset.
    CHECK(browser.saveAsIsOffered());
    CHECK_FALSE(browser.saveIsOffered());
}

TEST_CASE("Retyping the same comment is not an edit", "[gui][presets]")
{
    ////////////////////////////////////////////////////////////////////////////
    /// \note juce::TextEditor::setText notifies whether or not the text moved,
    /// and so does selecting a preset, which fills the box from the file. A
    /// browser that marked the session dirty for that would mean opening a
    /// preset counted as editing it.
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    auto &browser(browserOf(instance));

    browser.comment().setText("unchanged", juce::dontSendNotification);
    browser.commentChanged();
    auto const after(instance.stateModifications);

    browser.commentChanged();
    CHECK(instance.stateModifications == after);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Issue #177's other half. A session restored opens the browser where it
/// was left (issue #129) and said nothing about which preset was *playing*, so a
/// project reopened showed the right folder with nothing selected in it.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A restored session highlights the preset it is playing", "[gui][presets]")
{
    SWTest::HostSideJuce const juceIsUp;

    auto const banks(LE::SW::FactoryPresets::banks());
    REQUIRE_FALSE(banks.empty());
    auto const bank(juce::String(banks.front()));

    auto const presets(LE::SW::FactoryPresets::presets(banks.front()));
    REQUIRE(presets.size() > 1);
    /// \note Not the first row, so that "selected" and "whatever the list opened
    /// on" cannot be the same answer.
    auto const preset(juce::String(presets.back()));

    SWTest::Instance instance;

    ////////////////////////////////////////////////////////////////////////////
    /// \note What `stateLoad` leaves behind: the panel's place, and the preset
    /// the session was playing. Set before the editor is built, which is the
    /// order a host uses -- state first, window later.
    ////////////////////////////////////////////////////////////////////////////
    auto &panel(instance.panelState());
    panel.presetLocation = PanelState::PresetLocation::factory;
    panel.presetBank = bank;

    auto &loaded(instance.loadedPreset());
    loaded.loaded(preset, PanelState::PresetLocation::factory);
    loaded.bank = bank;

    auto &browser(browserOf(instance));

    CHECK(browser.selectedPresetName() == preset);

    ////////////////////////////////////////////////////////////////////////////
    /// \note And it was highlighted rather than *loaded*: a session may carry
    /// edits nobody has saved, and reloading the file would throw them away.
    /// \see PresetBrowser::highlightLoadedPreset().
    ////////////////////////////////////////////////////////////////////////////
    CHECK_FALSE(loaded.modified.load());
}

TEST_CASE("A preset from another bank is not highlighted", "[gui][presets]")
{
    ////////////////////////////////////////////////////////////////////////////
    /// \note A user who loaded a preset and then went browsing elsewhere is
    /// looking at somewhere they chose. The listing is theirs; nothing in it is
    /// the preset that is playing.
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    auto const banks(LE::SW::FactoryPresets::banks());
    REQUIRE(banks.size() > 1);

    SWTest::Instance instance;

    auto &panel(instance.panelState());
    panel.presetLocation = PanelState::PresetLocation::factory;
    panel.presetBank = juce::String(banks.front());

    auto const elsewhere(LE::SW::FactoryPresets::presets(banks.back()));
    REQUIRE_FALSE(elsewhere.empty());

    auto &loaded(instance.loadedPreset());
    loaded.loaded(juce::String(elsewhere.front()), PanelState::PresetLocation::factory);
    loaded.bank = juce::String(banks.back());

    auto &browser(browserOf(instance));

    CHECK(browser.selectedPresetName().isEmpty());
}
