////////////////////////////////////////////////////////////////////////////////
///
/// \file ejectPainter.hpp
/// ----------------------
///
///   The tab at the top of a module strip that takes the effect out of it.
///
///   Skin file 16: a tongue hanging off the top of the strip -- square where it
/// meets it, and rounded away to nothing at the bottom -- in the skin's blue
/// around a flat grey, with a blue cross on it.
///
/// \note The bottom is an *elliptical* pair of corners rather than a circular
/// one: eleven and a half pixels across against five deep, meeting in the
/// middle, which is what makes the shape read as a tongue rather than as a
/// capsule cut in half. juce::Path::addRoundedRectangle takes the two radii
/// separately, which is the whole of the drawing.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef ejectPainter_hpp__74C2A5E1_9B06_4D3F_82A9_16E5D0B7384C
#define ejectPainter_hpp__74C2A5E1_9B06_4D3F_82A9_16E5D0B7384C
//------------------------------------------------------------------------------
#include <juce_graphics/juce_graphics.h>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \namespace EjectStyle
///
////////////////////////////////////////////////////////////////////////////////

/// \brief The tongue and the cross on it, in skin pixels inside a 30 x 18
/// widget.
namespace EjectStyle
{
int constexpr widgetWidth{30};
int constexpr widgetHeight{18};

/// What the tongue leaves clear of its widget. The top is where the strip is,
/// so nothing is left there -- the shape starts a little below the widget's own
/// top because the strip's rim is above it.
///@{
float constexpr sideInset{4.04f};
float constexpr topInset{4.15f};
float constexpr bottomInset{1.94f};
///@}

/// How deep the elliptical bottom is; its width is half the tongue's.
float constexpr footDepth{5.0f};

float constexpr rimThickness{1.25f};

/// The cross, as a half-width and half-height about the tongue's middle, and
/// the pen it is drawn with.
///@{
float constexpr crossHalfWidth{2.85f};
float constexpr crossHalfHeight{3.2f};
float constexpr crossCentreY{9.7f};
float constexpr crossThickness{2.0f};
///@}
} // namespace EjectStyle

////////////////////////////////////////////////////////////////////////////////
///
/// \class EjectPainter
///
////////////////////////////////////////////////////////////////////////////////

class EjectPainter
{
  public:
    /// \brief Draws the tongue and its cross, filling \p bounds.
    static void paint(juce::Graphics &, juce::Rectangle<float> bounds);

    /// \brief Washes the tongue -- and only the tongue -- in \p colour.
    ///
    /// \note What a BitmapButton's `overlayColourWhenOver` did: fill the
    /// artwork's alpha with a colour, so the shape darkens under the pointer
    /// and the strip behind it does not. A pale glyph on a dark strip cannot be
    /// lifted any further, which is why this one is washed down.
    /// \see ColourMap::MouseOverShade.
    static void tint(juce::Graphics &, juce::Rectangle<float> bounds, juce::Colour);

  public:
    EjectPainter() = delete; // a drawing, not an object
}; // class EjectPainter

} // namespace LE::SW::GUI

#endif // ejectPainter_hpp
