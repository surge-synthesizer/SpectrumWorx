////////////////////////////////////////////////////////////////////////////////
///
/// \file editorKnobPainter.hpp
/// ---------------------------
///
///   The in, out and mix knobs along the editor's left edge.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef editorKnobPainter_hpp__2D91F4A6_08C7_4B3E_9A56_C1F0873B2E4D
#define editorKnobPainter_hpp__2D91F4A6_08C7_4B3E_9A56_C1F0873B2E4D
//------------------------------------------------------------------------------
#include "gui/painters/knobPainter.hpp"

#include <juce_graphics/juce_graphics.h>

#include <cstdint>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \namespace EditorKnobStyle
///
////////////////////////////////////////////////////////////////////////////////

/// \brief Everything the editor's in, out and mix knobs are drawn from.
///
///   Until 15.08.2026 this knob was skin file 02, a film strip of 127 square
/// frames that were all the same picture at a different angle -- 140 KB of PNG
/// to rotate one bar. It is the numbers below now, for the reasons in
/// ModuleKnobStyle: 127 steps of resolution, and a knob made of pixels cannot
/// follow the editor's zoom.
///
///   The layers, in the order they go down: a bevel disc whose shading is *not*
/// concentric -- it is lit from above left, so its gradient is centred a little
/// down and to the right of the knob -- then a teal ring under a black cap with
/// a soft edge, then eight ticks and the bright rim and its dark outline, and
/// last the pointer, the one thing that moves. \see ColourMap for what each of
/// those is painted in.
///
/// \note Radii, widths and gradient stops are all fractions of the knob's own
/// radius. There is only one editor knob size, so nothing turns on that; it is
/// how ModuleKnobStyle reads and the two are meant to be read together. What
/// they do share -- the travel, and the two gradients they are built from -- is
/// KnobPainter's. \see knobPainter.hpp.
namespace EditorKnobStyle
{
/// \note Geometry only. The colours are ColourMap::EditorKnob*, the skin's
/// palette being one file rather than one namespace per drawing.
float constexpr bevelRadius{0.898f};
/// How far off centre the bevel is lit from, as (x, y) -- down and to the right.
float constexpr bevelShadingX{0.091f};
float constexpr bevelShadingY{0.039f};
/// The bevel's gradient reaches its far edge from that point, not from the
/// middle: bevelRadius + the distance the shading is offset by.
float constexpr bevelReach{0.997f};
float constexpr bevelShadowStop{0.606f}; ///< fractions of bevelReach, not of the knob
float constexpr bevelMidStop{0.725f};
float constexpr bevelRimStop{0.916f};

float constexpr ringRadius{0.688f};
float constexpr capRadius{0.568f};      ///< where the cap has faded out entirely
float constexpr capSolidRadius{0.423f}; ///< and where it starts to

float constexpr tickInnerRadius{0.685f};
float constexpr tickOuterRadius{0.829f};
float constexpr tickHalfWidth{0.015f};
unsigned int constexpr numberOfTicks{8};

float constexpr rimHighlightInner{0.853f}; ///< a hard edge, hidden under the bevel
float constexpr rimHighlightSolid{0.907f}; ///< out to here, then fading to
float constexpr rimHighlightOuter{0.952f};
float constexpr rimOutlineRadius{0.972f};
float constexpr rimOutlineThickness{0.033f};

float constexpr pointerInnerRadius{0.25f};
float constexpr pointerOuterRadius{0.83f};
float constexpr pointerInnerHalfWidth{0.058f}; ///< very slightly tapered
float constexpr pointerOuterHalfWidth{0.061f};
} // namespace EditorKnobStyle

/// \brief Draws an editor knob into the square \p bounds, its pointer at
/// \p normalisedValue.
void paintEditorKnob(juce::Graphics &, juce::Rectangle<float> bounds, float normalisedValue);

} // namespace LE::SW::GUI

#endif // editorKnobPainter_hpp
