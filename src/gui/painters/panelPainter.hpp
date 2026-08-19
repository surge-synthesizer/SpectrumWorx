////////////////////////////////////////////////////////////////////////////////
///
/// \file panelPainter.hpp
/// ----------------------
///
///   The two overlay panels' backgrounds: the preset browser and the settings
/// pages.
///
///   Skin files 07 and 17 until 18.08.2026. Between them 2.5 KB of vector, and
/// between them six shapes: a flat rounded rectangle with a handful of white
/// hairline frames laid on it, one per field the panel holds. Nothing in either
/// is shaded, textured or lit -- they are the only two files in the skin that
/// were pure outline.
///
///   The browser is rounded on all four corners. The settings page is rounded
/// on three: the tabs stand on its top left, in a strip of their own that ends
/// at the last of them and fades up into the gutter, so neither shape shows a
/// corner where they meet.
///
/// \note Which is why the strip is here rather than in ButtonPainter. Each tab
/// used to fill its own bounds with this black, and that is three mistakes in
/// one: a corner of it outside every pill, a wedge of the page showing between
/// each pair, and the strip stopping dead after the last tab where the page
/// carried on. One shape behind all three answers all three.
///                                       (18.08.2026.)
///
/// \note The rectangles below are in skin pixels from the panel's top left, as
/// the artwork had them. They are not fractions of the panel: both panels are
/// one size, and the fields inside them are placed against controls whose
/// positions are also constants.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef panelPainter_hpp__3F80D6C5_1E47_4A92_B5D8_60C7E1938A24
#define panelPainter_hpp__3F80D6C5_1E47_4A92_B5D8_60C7E1938A24
//------------------------------------------------------------------------------
#include <juce_graphics/juce_graphics.h>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class PanelPainter
///
////////////////////////////////////////////////////////////////////////////////

class PanelPainter
{
  public:
    /// \brief The preset browser, whole: the panel and the four fields on it.
    static void paintPresetBrowser(juce::Graphics &, juce::Rectangle<float> bounds);

    /// \brief A settings page -- Engine, GUI or About, which were one file.
    static void paintSettingsPage(juce::Graphics &, juce::Rectangle<float> bounds);

    /// \brief The strip the settings tabs stand in, which runs from the panel's
    /// left edge to the right edge of the last tab and no further.
    static void paintTabStrip(juce::Graphics &, juce::Rectangle<float> bounds);

    /// The corner the panels are rounded by.
    static float constexpr cornerRadius{8.f};

    /// \brief What both keep clear of their widget's left edge.
    ///
    /// \note The artwork's, and shared so that the strip and the page below it
    /// line up: they were a pixel apart when only the page had it.
    static float constexpr sideInset{1.f};

    /// \brief The sizes the two are drawn at, which were their artwork's.
    ///
    /// \note The settings panel is this page plus ButtonStyle::tabHeight, which
    /// is what makes it the browser's 363. The two share a rectangle in the
    /// editor and always have.
    ///@{
    static int constexpr width{191};
    static int constexpr presetBrowserHeight{364};
    static int constexpr settingsPageHeight{347};
    ///@}

  public:
    PanelPainter() = delete; // a drawing, not an object
}; // class PanelPainter

} // namespace LE::SW::GUI

#endif // panelPainter_hpp
