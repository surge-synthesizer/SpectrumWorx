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
int constexpr width{6};
int constexpr height{12};
///@}

/// \brief What it grows to: while it is being dragged, and for good on a slider
/// whose thumbs stand for parameters of their own.
///
/// \note The frequency range is the only one of those, and it is drawn among the
/// module knobs -- so its beads are the chunky one throughout and say which of
/// them is the parameter with a halo instead. \see SliderWithSelectedThumb and
/// issue #220.
float constexpr enlargement{5.0f / 3};

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

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The halo, which is `glowRings` ellipses a pixel apart, each one out
/// from the bead's edge and each covered by the brighter one inside it -- so the
/// light falls off with how many of them a pixel is under.
///
/// \note The rings rather than KnobPainter::paintFocusRing(): a bead is twice as
/// tall as it is wide, and the radial gradient that draws a knob's ring is
/// circular -- it would peak along a circle through a lozenge rather than along
/// its edge. Same idiom, and the same two numbers, as FrameStyle's glow.
///
/// \note Two rings at the least, the falloff being written as the first and the
/// last.
///
////////////////////////////////////////////////////////////////////////////////
///@{
unsigned int constexpr glowRings{3};
float constexpr glowInnerAlpha{0.35f}; ///< at the ring against the bead
float constexpr glowOuterAlpha{0.10f}; ///< and at the last one
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
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Draws the bead filling \p bounds, at whatever size it is asked
    /// for.
    ///
    /// \param haloStrength how strongly it wears the halo, 0 for not at all:
    /// full for the thumb of the selected control and half for one merely under
    /// the pointer, which is what a knob's ring says at the same two strengths.
    /// \see hoverStrength and issue #220.
    ///
    ////////////////////////////////////////////////////////////////////////////
    static void paint(juce::Graphics &, juce::Rectangle<float> bounds, float haloStrength = 0.0f);

  public:
    SliderThumbPainter() = delete; // a drawing, not an object
}; // class SliderThumbPainter

} // namespace LE::SW::GUI

#endif // sliderThumbPainter_hpp
