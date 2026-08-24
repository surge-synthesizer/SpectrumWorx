////////////////////////////////////////////////////////////////////////////////
///
/// paletteTests.cpp
/// ----------------
///
///   The five colour schemes: that the user's choice survives in the preferences
/// file, that an editor opens in it, and that changing it reaches an editor that
/// is already open.
///
/// \note Nothing here asserts what any palette *looks like*. A case that pinned
/// a colour would be a case that has to be edited every time one is retuned, and
/// a palette is a thing to look at rather than a thing to assert. What is pinned
/// is the mechanism -- that ClassicGray drains, that every scheme moves the accent,
/// that a change reaches the screen -- and the skin's own blue is asked for at
/// run time rather than spelled, so retuning ClassicBlue moves the tests with it.
///
/// \note The last of those is the point of the file, and it is measured in
/// pixels. "Does getColour() answer differently" is a question the broken build
/// answered correctly: a widget that took a juce::Colour into a member when it
/// was constructed goes on painting the palette it was born in, and there were
/// three of them -- a static array of text descriptors initialised before any
/// palette had been chosen at all, an arrow button's mouse-over tint, and the
/// two text editors in the preset browser. Nothing about the map itself was
/// wrong in any of those cases. What was wrong was what was on the screen.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/editorHarness.hpp"

#include "gui/colourMap.hpp"
#include "gui/preferences.hpp"
#include "gui/theme.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <fstream>
#include <string>
//------------------------------------------------------------------------------
namespace
{
using namespace LE;
using namespace LE::SW;

using ColourMap = GUI::ColourMap;
using Editor = GUI::SpectrumWorxEditor;

/// Every enumerator of a scoped-by-convention enum, as a range would give it.
template <typename Body> void forEachColour(Body const &body)
{
    for (unsigned int index(0); index < ColourMap::numberOfColours; ++index)
        body(static_cast<ColourMap::Name>(index));
}

template <typename Body> void forEachPalette(Body const &body)
{
    for (unsigned int index(0); index < ColourMap::numberOfPalettes; ++index)
        body(static_cast<ColourMap::Palette>(index));
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Puts the palette back when a case is done with it.
///
/// \note ctest runs a process per case, so this is belt and braces -- but the
/// palette is a process-wide static and a case that left it turned would be a
/// case that broke every other case in its file, silently and by colour.
///
////////////////////////////////////////////////////////////////////////////////

class PaletteRestored
{
  public:
    ~PaletteRestored() { ColourMap::setPalette(was_); }

  private:
    ColourMap::Palette const was_{ColourMap::palette()};
}; // class PaletteRestored

/// The skin's own accent, which is what every other palette has to have moved.
juce::Colour skinBlue()
{
    PaletteRestored const restored;
    ColourMap::setPalette(ColourMap::ClassicBlue);
    return ColourMap::getColour(ColourMap::Accent);
}

/// \brief The editor as an image, which is what a user sees. \see
/// gui/overlayPanelTests.cpp, where the same helper measures a different thing.
juce::Image rendered(Editor &editor)
{
    juce::Image image(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
    juce::Graphics graphics(image);
    editor.paintEntireComponent(graphics, true);
    return image;
}

std::size_t pixelsOfExactly(juce::Image const &image, juce::Colour const wanted)
{
    std::size_t found(0);
    juce::Image::BitmapData const pixels(image, juce::Image::BitmapData::readOnly);
    for (int y(0); y < image.getHeight(); ++y)
        for (int x(0); x < image.getWidth(); ++x)
            found += (pixels.getPixelColour(x, y).getARGB() == wanted.getARGB());
    return found;
}

/// \brief The preset browser's comment box, wherever the editor has parented it.
///
/// \note Found by walking the tree for the multi-line editor rather than asked
/// for: PresetBrowser::comment() is private and widening it for one case would
/// be the test changing the code to suit itself. The browser holds two text
/// editors and only the comment box is multi-line.
juce::TextEditor *commentBoxIn(juce::Component &component)
{
    for (auto *const child : component.getChildren())
    {
        if (auto *const editor = dynamic_cast<juce::TextEditor *>(child))
            if (editor->isMultiLine())
                return editor;
        if (auto *const found = commentBoxIn(*child))
            return found;
    }
    return nullptr;
}

fs::path caseFolder(char const *const name)
{
    auto const folder(fs::path(SW_TEST_OUTPUT_DIR) / "palettes" / name);
    fs::remove_all(folder);
    return folder;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
// The map
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("ClassicGray answers in greys")
{
    PaletteRestored const restored;
    ColourMap::setPalette(ColourMap::ClassicGray);

    forEachColour([](ColourMap::Name const name) {
        auto const colour(ColourMap::getColour(name));
        UNSCOPED_INFO("colour " << static_cast<unsigned int>(name) << " is "
                                << colour.toDisplayString(true).toStdString());
        CHECK(colour.getRed() == colour.getGreen());
        CHECK(colour.getGreen() == colour.getBlue());
    });
}

TEST_CASE("Every palette moves the accent")
{
    PaletteRestored const restored;
    auto const blue(skinBlue());

    forEachPalette([&](ColourMap::Palette const palette) {
        if (palette == ColourMap::ClassicBlue)
            return;

        ColourMap::setPalette(palette);
        UNSCOPED_INFO("palette: " << ColourMap::nameOf(palette));
        CHECK(ColourMap::getColour(ColourMap::Accent) != blue);
    });
}

TEST_CASE("Changing the palette moves the generation")
{
    PaletteRestored const restored;
    ColourMap::setPalette(ColourMap::ClassicBlue);

    auto const before(ColourMap::generation());

    ColourMap::setPalette(ColourMap::ClassicBlue);
    CHECK(ColourMap::generation() == before); // nothing changed, nothing to tell

    ColourMap::setPalette(ColourMap::ClassicRed);
    CHECK(ColourMap::generation() != before);
    CHECK(ColourMap::palette() == ColourMap::ClassicRed);
}

////////////////////////////////////////////////////////////////////////////////
// The preference
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The palette survives the preferences file")
{
    auto const folder(caseFolder("round-trip"));

    forEachPalette([&](ColourMap::Palette const palette) {
        {
            GUI::Preferences written(folder);
            written.setPalette(palette);
        }
        GUI::Preferences const read(folder);
        UNSCOPED_INFO("palette: " << ColourMap::nameOf(palette));
        CHECK(read.palette() == palette);
    });
}

TEST_CASE("A palette this build does not have reads as ClassicBlue")
{
    auto const folder(caseFolder("unknown"));
    fs::create_directories(folder);

    ///   The file is the user's to edit and this one names a scheme from
    /// somewhere else. \see the note on streaming by name in preferences.hpp:
    /// an unrecognised name reads as the default, which is what keeps a file
    /// from a future build from painting a black editor.
    {
        std::ofstream stream(folder / "SpectrumWorxUserDefaults.xml");
        stream << "<?xml version = \"1.0\" encoding = \"UTF-8\" ?>\n"
                  "<defaults version=\"1\">\n"
                  "  <default key=\"palette\" value=\"Mauve\" type=\"1\"/>\n"
                  "</defaults>\n";
    }

    GUI::Preferences const read(folder);
    CHECK(read.palette() == ColourMap::ClassicBlue);
}

////////////////////////////////////////////////////////////////////////////////
// The screen
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("An editor opens in the palette the user chose")
{
    PaletteRestored const restored;
    GUI::setPreferencesFolder(caseFolder("opens-in"));
    GUI::preferences().setPalette(ColourMap::ClassicRed);

    SWTest::Instance instance;
    instance.openEditor();

    //   Not one pixel of the skin's blue, which is a stronger claim than "the
    // map answers in red": a colour taken into a member at construction -- or,
    // as this once was, at static-initialisation time before any palette
    // existed -- would still be on the screen.
    CHECK(pixelsOfExactly(rendered(instance.editor()), skinBlue()) == 0);
}

TEST_CASE("Changing the palette reaches an editor that is already open")
{
    PaletteRestored const restored;
    GUI::setPreferencesFolder(caseFolder("already-open"));

    SWTest::Instance instance;
    instance.openEditor();

    auto const blue(skinBlue());
    REQUIRE(pixelsOfExactly(rendered(instance.editor()), blue) > 0); // it is a blue skin

    auto const wasInTheTheme(
        GUI::Theme::singleton().findColour(juce::CaretComponent::caretColourId));

    instance.editor().setPalette(ColourMap::ClassicGreen);

    ////////////////////////////////////////////////////////////////////////////
    /// \note By hand, because this is what the editor's 30 Hz timer calls and
    /// there is no message loop in a test binary to turn it. \see
    /// SpectrumWorxEditor::applyPaletteIfChanged(), which says so.
    ////////////////////////////////////////////////////////////////////////////
    instance.editor().applyPaletteIfChanged();

    CHECK(pixelsOfExactly(rendered(instance.editor()), blue) == 0);

    /// \note And the LookAndFeel, which is the half a repaint cannot do: these
    /// are copied out of the map once and go stale in place.
    CHECK(GUI::Theme::singleton().findColour(juce::CaretComponent::caretColourId) != wasInTheTheme);

    CHECK(GUI::preferences().palette() == ColourMap::ClassicGreen);
}

TEST_CASE("An editor left alone does not reload its colours")
{
    PaletteRestored const restored;
    GUI::setPreferencesFolder(caseFolder("left-alone"));

    SWTest::Instance instance;
    instance.openEditor();

    auto const before(rendered(instance.editor()));

    instance.editor().applyPaletteIfChanged();

    auto const after(rendered(instance.editor()));
    CHECK(pixelsOfExactly(after, skinBlue()) == pixelsOfExactly(before, skinBlue()));
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The one thing a repaint cannot fix. juce::TextEditor::setColour()
/// overrides the LookAndFeel for good, so the browser's two editors have to be
/// *told*, and telling them is what SpectrumWorxEditor::applyPaletteIfChanged()
/// asks for by calling sendLookAndFeelChange() rather than repaint(). A plain
/// repaint passes every other case in this file and leaves these two behind.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A palette change reaches the widgets that hold their own colours")
{
    PaletteRestored const restored;
    GUI::setPreferencesFolder(caseFolder("holds-colours"));

    SWTest::Instance instance;
    instance.openEditor();
    instance.editor().showPresetBrowser(true);

    auto *const pComment(commentBoxIn(instance.editor()));
    REQUIRE(pComment != nullptr);

    auto const before(pComment->findColour(juce::TextEditor::textColourId));
    REQUIRE(before == skinBlue()); // the comment box writes in the accent

    instance.editor().setPalette(ColourMap::ClassicRed);
    instance.editor().applyPaletteIfChanged();

    CHECK(pComment->findColour(juce::TextEditor::textColourId) != before);
    CHECK(pComment->findColour(juce::TextEditor::textColourId) ==
          ColourMap::getColour(ColourMap::Accent));
}
