////////////////////////////////////////////////////////////////////////////////
///
/// sideChainSelectorTests.cpp
/// --------------------------
///
///   The box under the LFO panel, which used to be called "External audio" and
/// used to be able to say only two things: the name of a file, or nothing. It
/// is labelled "Sidechain source" as of 18.08.2026, the old name having been
/// outlines in the artwork until then.
///
///   "Nothing" was 2016's way of writing `Main` without a word for it, which is
/// why a user who cleared a file could not say what they wanted instead, and why
/// the plugin's own truth table had a corner nobody could explain. The box
/// answers "what goes into the side channel" now, and there are three answers.
/// \see doc/tech/sidechain-approach.md and issue #113.
///
/// \note Pixels rather than the widget tree, for the reason overlayPanelTests
/// gives at length: a label that holds the right string and is laid out under
/// something else answers every question but the one that matters.
///
/// \note What is *not* here is the popup menu, and it is not reachable: opening
/// one needs a message loop, which JUCE 8 compiles out of a binary with no
/// message thread. What the menu items call is `sideChainSourceSelected()`, and
/// that is driven directly.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/editorHarness.hpp"

/// \note Before anything that names SW::Module: the module chain downcasts a
/// node to it, and this is the header with the complete type.
#include "core/modules/moduleDSPAndGUI.hpp"

#include "le/spectrumworx/sideChainSource.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
//------------------------------------------------------------------------------
namespace
{
using namespace LE;
using namespace LE::SW;

using Editor = GUI::SpectrumWorxEditor;

juce::Image rendered(Editor &editor)
{
    juce::Image image(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
    juce::Graphics graphics(image);
    editor.paintEntireComponent(graphics, true);
    return image;
}

/// \brief How many pixels two same-sized renders disagree about.
std::size_t differingPixels(juce::Image const &left, juce::Image const &right)
{
    REQUIRE(left.getBounds() == right.getBounds());

    juce::Image::BitmapData const leftPixels(left, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData const rightPixels(right, juce::Image::BitmapData::readOnly);

    std::size_t different{0};
    for (int y(0); y < left.getHeight(); ++y)
        for (int x(0); x < left.getWidth(); ++x)
            different += (leftPixels.getPixelColour(x, y) != rightPixels.getPixelColour(x, y));
    return different;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("The side chain box shows which source is selected", "[gui][side-chain][issue-113]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    instance.openEditor(Editor::PanelPlacement::overlay);
    auto &editor(instance.editor());

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note A fresh editor is in `Host`, which is the plugin's default -- pinned
    /// here because it is the one place a user meets it without having chosen
    /// anything, and because the render below would otherwise be measuring a
    /// difference from an arrangement nobody starts in.
    ///
    ////////////////////////////////////////////////////////////////////////////
    CHECK(instance.sideChainSource() == defaultSideChainSource);
    CHECK(defaultSideChainSource == SideChainSource::Host);

    auto const showingHost(rendered(editor));

    editor.sideChainSourceSelected(SideChainSource::Main);
    CHECK(instance.sideChainSource() == SideChainSource::Main);
    auto const showingMain(rendered(editor));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Two things at once, and both matter. The box *draws* something
    /// different, so the selection is visible rather than merely stored -- and
    /// the editor asked its host for the change, so picking a source is not a
    /// thing the interface believes on its own.
    ///
    /// \note Some difference rather than a fraction: what moves is one line of
    /// text in a 563 x 396 editor, so a floor worth having would be tuned to the
    /// string lengths. Zero is the failure and it is the only one worth naming --
    /// a box that never re-read the source, which is exactly what an empty
    /// "External audio" field did for every state that was not a file.
    ///
    ////////////////////////////////////////////////////////////////////////////
    CHECK(differingPixels(showingHost, showingMain) > 0);

    // ...and back, so that neither string is the one it happens to start on.
    editor.sideChainSourceSelected(SideChainSource::Host);
    CHECK(instance.sideChainSource() == SideChainSource::Host);
    CHECK(differingPixels(rendered(editor), showingHost) == 0);
}
