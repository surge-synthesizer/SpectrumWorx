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

#include "external_audio/sample.hpp"
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

juce::Component *findSampleArea(Editor &editor)
{
    for (auto *const pChild : editor.mainArea().getChildren())
        if (dynamic_cast<Editor::SampleArea *>(pChild))
            return pChild;
    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief One wheel row over \p component: +1 away from the user, -1 toward.
///
/// \note Just over a fifth rather than discreteParameterTests' 0.3, because the
/// wheel keeps what a notch leaves over: at 0.3 every other notch would be two
/// rows, and these cases walk the list a row at a time.
///
////////////////////////////////////////////////////////////////////////////////

void scroll(juce::Component &component, int const direction)
{
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = static_cast<float>(direction) * 0.2001f;

    auto const centre(component.getLocalBounds().getCentre().toFloat());
    juce::MouseEvent const event(juce::Desktop::getInstance().getMainMouseSource(), centre,
                                 juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component,
                                 &component, juce::Time(), centre, juce::Time(), 1, false);
    component.mouseWheelMove(event, wheel);
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
    /// text in an 845 x 564 editor, so a floor worth having would be tuned to the
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

////////////////////////////////////////////////////////////////////////////////
///
/// \note The strings rather than the pixels, and directly rather than through the
/// menu. `hostPortName()` exists precisely so that this is reachable: the popup
/// cannot be opened here -- JUCE 8 compiles the message loop out of a binary with
/// no message thread, as the note at the top of this file says -- so a wording
/// that lived inside `showCenteredBelow` could not be tested at all, and the box
/// and the menu held two copies of it.
///
/// \note A file is not one of them. It is named by its file at every width, which
/// is why `hostPortName()` asserts rather than answering for it.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The two host ports are named for the width they carry", "[gui][side-chain][audio-ports]")
{
    using Editor = GUI::SpectrumWorxEditor;

    CHECK(std::string(Editor::hostPortName(SideChainSource::Main, 2)) == "Main Input (1+2)");
    CHECK(std::string(Editor::hostPortName(SideChainSource::Host, 2)) == "Sidechain Input (3+4)");

    // a mono layout has no pairs to be, so the parenthetical names the mode
    CHECK(std::string(Editor::hostPortName(SideChainSource::Main, 1)) == "Main Input (Mono)");
    CHECK(std::string(Editor::hostPortName(SideChainSource::Host, 1)) == "Sidechain Input (Mono)");
}

// a session keeps the name of a file that would not load, so the box has to say so
TEST_CASE("The side chain box says when its file did not load", "[gui][side-chain][issue-12]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    instance.openEditor(Editor::PanelPlacement::overlay);
    auto &editor(instance.editor());

    instance.nameSample("Missing.wav", true /*loaded*/);
    editor.updateSampleName();
    auto const loaded(rendered(editor));

    instance.nameSample("Missing.wav", false /*loaded*/);
    editor.updateSampleName();

    CHECK(differingPixels(rendered(editor), loaded) > 0);
}

/// \note And that the box asks. The name is built from the host's width rather
/// than held from whenever the source was last picked, so a layout that changes
/// under an open editor is a box that re-reads.
TEST_CASE("The side chain box follows the port width", "[gui][side-chain][audio-ports]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    instance.openEditor(Editor::PanelPlacement::overlay);
    auto &editor(instance.editor());

    editor.sideChainSourceSelected(SideChainSource::Host);
    auto const stereo(rendered(editor));

    instance.setChannelWidth(1);
    editor.updateSampleName();
    auto const mono(rendered(editor));

    CHECK(differingPixels(stereo, mono) > 0);

    instance.setChannelWidth(2);
    editor.updateSampleName();
    CHECK(differingPixels(rendered(editor), stereo) == 0);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The menu's order with "Load..." left out: the wheel goes from the
/// host's port straight to the first factory sample and back, and never opens a
/// file dialog. It stops at the ends rather than wrapping, as a ComboBox does.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The wheel steps the side chain box through its sources", "[gui][side-chain][wheel]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    instance.openEditor(Editor::PanelPlacement::overlay);
    auto &editor(instance.editor());

    auto const samples(Sample::factorySamples());
    REQUIRE(samples.size() >= 2);

    auto *const pArea(findSampleArea(editor));
    REQUIRE(pArea != nullptr);
    auto &area(*pArea);

    editor.sideChainSourceSelected(SideChainSource::Main);

    scroll(area, -1);
    CHECK(instance.sideChainSource() == SideChainSource::Main);

    scroll(area, +1);
    CHECK(instance.sideChainSource() == SideChainSource::Host);

    scroll(area, +1);
    CHECK(instance.sideChainSource() == SideChainSource::File);
    CHECK(instance.currentSampleFile() == samples.front());

    scroll(area, +1);
    CHECK(instance.currentSampleFile() == samples[1]);

    for (std::size_t notch(0); notch < samples.size() + 2; ++notch)
        scroll(area, +1);
    CHECK(instance.sideChainSource() == SideChainSource::File);
    CHECK(instance.currentSampleFile() == samples.back());

    for (std::size_t notch(0); notch < samples.size() - 1; ++notch)
        scroll(area, -1);
    CHECK(instance.currentSampleFile() == samples.front());

    scroll(area, -1);
    CHECK(instance.sideChainSource() == SideChainSource::Host);
}

/// \note A file of the user's own stands where "Load..." does, so one notch
/// either way leaves it for its neighbours.
TEST_CASE("The wheel leaves a user's file for its neighbours", "[gui][side-chain][wheel]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    instance.openEditor(Editor::PanelPlacement::overlay);
    auto &editor(instance.editor());

    auto const samples(Sample::factorySamples());
    REQUIRE(!samples.empty());

    auto *const pArea(findSampleArea(editor));
    REQUIRE(pArea != nullptr);

    instance.nameSample("Mine.wav", true /*loaded*/);
    scroll(*pArea, +1);
    CHECK(instance.currentSampleFile() == samples.front());

    instance.nameSample("Mine.wav", true /*loaded*/);
    scroll(*pArea, -1);
    CHECK(instance.sideChainSource() == SideChainSource::Host);
}
