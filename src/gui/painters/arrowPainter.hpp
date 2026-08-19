////////////////////////////////////////////////////////////////////////////////
///
/// \file arrowPainter.hpp
/// ----------------------
///
///   The two triangles in the editor, both pointing right and both the skin's
/// blue.
///
///   Skin files 6 and 57: the big one down the right of the module rack that
/// adds an effect, and the small one on the LFO strip and in the preset browser
/// that steps to the next of something. Same shape at two sizes, and the only
/// difference between them is that the big one fades in from its base -- which
/// is what keeps a triangle a third of the editor tall from reading as an
/// arrowhead pointing at nothing.
///
/// \note The artwork's base is very slightly slanted -- a pixel out of
/// nineteen, in both files and in the same direction. It is a trace of the
/// original bitmap's antialiasing rather than a drawing, so these are straight.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef arrowPainter_hpp__E518CA37_2D40_49B6_B7F1_86C09A24E5D3
#define arrowPainter_hpp__E518CA37_2D40_49B6_B7F1_86C09A24E5D3
//------------------------------------------------------------------------------
#include <juce_graphics/juce_graphics.h>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \namespace ArrowStyle
///
////////////////////////////////////////////////////////////////////////////////

namespace ArrowStyle
{
/// The one that adds a module, down the right of the rack.
///@{
int constexpr addModuleWidth{29};
int constexpr addModuleHeight{56};
///@}

/// And the one that steps to the next waveform, or the next folder.
///@{
int constexpr stepWidth{11};
int constexpr stepHeight{17};
///@}

/// \brief What the base keeps clear of the left edge, as a fraction of the
/// width -- the artwork's, averaged over its slant.
float constexpr baseInset{0.07f};
} // namespace ArrowStyle

////////////////////////////////////////////////////////////////////////////////
///
/// \class ArrowPainter
///
////////////////////////////////////////////////////////////////////////////////

class ArrowPainter
{
  public:
    /// \brief Draws a triangle pointing right, filling \p bounds.
    ///
    /// \param fadeFromBase whether it comes up out of nothing at its base
    /// rather than being flat blue all through. \see the note in the header.
    static void paint(juce::Graphics &, juce::Rectangle<float> bounds, bool fadeFromBase);

    /// \brief Fills the same triangle with \p colour, which is how a
    /// BitmapButton said the pointer was on it. \see EjectPainter::tint().
    static void tint(juce::Graphics &, juce::Rectangle<float> bounds, juce::Colour);

  public:
    ArrowPainter() = delete; // a drawing, not an object
}; // class ArrowPainter

} // namespace LE::SW::GUI

#endif // arrowPainter_hpp
