////////////////////////////////////////////////////////////////////////////////
///
/// \file sliderThumbPainter.hpp
/// ----------------------------
///
///   The bead an LFO's range and phase sliders are dragged by.
///
///   Skin file 40, and four pixels by eight: a vertical lozenge carrying a lit
/// blue ramp, with a shadow down its left side. The last shape in the skin --
/// what is left of assets/skin after it is the editor's own background and the
/// eleven LFO waveform icons, which are pictures rather than shapes.
///
/// \note Drawn at 5/3 while its thumb is being dragged, which is the reason the
/// numbers below are fractions of the bead rather than pixels: it was a bitmap
/// blown up by Artwork::drawScaled() before, so the enlarged one was soft.
///
/// \note That method keeps one caller -- SpectrumWorxEditor::paint() stretches
/// the background's leftmost pixel column across the gutter a panel opens. The
/// commit that moved this bead said drawScaled() had none left, and was wrong.
///                                       (18.08.2026.)
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef sliderThumbPainter_hpp__A03C7E56_4F91_4B28_9D0E_3617BA5C82F4
#define sliderThumbPainter_hpp__A03C7E56_4F91_4B28_9D0E_3617BA5C82F4
//------------------------------------------------------------------------------
#include <juce_graphics/juce_graphics.h>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \namespace SliderThumbStyle
///
////////////////////////////////////////////////////////////////////////////////

namespace SliderThumbStyle
{
/// The widget the bead is drawn in, at rest. \see Theme::getSliderThumbRadius().
///@{
int constexpr width{4};
int constexpr height{8};
///@}

/// What the bead leaves clear of that, as a fraction of each side.
///@{
float constexpr sideInset{0.12f};
float constexpr endInset{0.08f};
///@}

/// \brief Where the lit ramp turns, as fractions of the bead's height.
///
/// \note The artwork had six stops and these are four of them. The two dropped
/// sit between the first and third and land within fifteen parts in 255 of the
/// line through them, which on something three pixels wide is nothing; the
/// fourth is *not* on that line and stays, because it is the second highlight
/// a lit cylinder has near its foot and the bead reads as flat without it.
///@{
float constexpr faceStop{0.6f};
float constexpr sheenStop{0.8f};
///@}

/// \brief The shadow down the bead's left side: a rim whose black runs from
/// nearly opaque at the left to nearly clear at the right.
///@{
float constexpr shadowThickness{0.06f}; ///< of the bead's width
float constexpr shadowLeftAlpha{0.90f};
float constexpr shadowRightAlpha{0.125f};
///@}
} // namespace SliderThumbStyle

////////////////////////////////////////////////////////////////////////////////
///
/// \class SliderThumbPainter
///
////////////////////////////////////////////////////////////////////////////////

class SliderThumbPainter
{
  public:
    /// \brief Draws the bead filling \p bounds, at whatever size it is asked
    /// for.
    static void paint(juce::Graphics &, juce::Rectangle<float> bounds);

  public:
    SliderThumbPainter() = delete; // a drawing, not an object
}; // class SliderThumbPainter

} // namespace LE::SW::GUI

#endif // sliderThumbPainter_hpp
