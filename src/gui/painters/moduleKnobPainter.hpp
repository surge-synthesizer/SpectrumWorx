////////////////////////////////////////////////////////////////////////////////
///
/// \file moduleKnobPainter.hpp
/// ---------------------------
///
///   The two round controls on a module strip: a knob and a trigger button.
/// They are one dome with two different middles, which is why they are one file
/// -- the trigger reads the knob's own numbers for everything but its cap.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef moduleKnobPainter_hpp__47E0B93C_5A18_4D72_BC69_0E3F81A5D264
#define moduleKnobPainter_hpp__47E0B93C_5A18_4D72_BC69_0E3F81A5D264
//------------------------------------------------------------------------------
#include "gui/painters/knobPainter.hpp"

#include <juce_graphics/juce_graphics.h>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \namespace ModuleKnobStyle
///
////////////////////////////////////////////////////////////////////////////////

/// \brief Everything a module knob is drawn from: five colours and the radii and
/// angles they are laid out at.
///
///   Until 14.08.2026 a module knob was a film strip -- 127 square frames
/// stacked into one sheet (skin files 03, 12, 63 and 64), picked by
/// `126 * proportion` and blitted. That is four assets for what is one drawing
/// at two sizes and two polarities, it quantises the value to 127 steps, and it
/// is the reason the knob could not follow the editor's zoom: the frames are
/// pixels. This is the same drawing as paint calls, so it resolves at whatever
/// the graphics context is and every number below is in one place.
///
///   The shape, from the middle out: a black cap, then the value wedge in the
/// skin's blue out to `wedgeRadius`, then a dome that ramps from
/// ColourMap::ModuleKnobDomeCentre where the cap ends to ModuleKnobDomeRim at
/// the rim. The wedge opens clockwise from `-halfSweepDegrees` for a unipolar
/// parameter and from twelve o'clock for a bipolar one, and the cap grows with
/// it -- which is what the artwork did, and what keeps the blue a band of
/// roughly even thickness rather than a lengthening spike.
///
/// \note Radii are fractions of the knob's own radius, so they hold at both the
/// 51 px module knob and the 23 px shared one. The rim and the focus halo are in
/// pixels instead: they are hairlines at both sizes rather than something that
/// scales with them.
namespace ModuleKnobStyle
{
/// \note The five colours stood here and are ColourMap's now -- three of its
/// ModuleKnob* entries, plus Blue for the wedge and FocusHalo for the ring. The
/// wedge's was 0xFF13B7EA against Theme's 0xFF13B5EA, which is the sort of
/// two-parts-in-255 drift a central palette exists to stop.
///                                       (18.08.2026.)
float constexpr innerGradientRadius{0.26f}; ///< the dome holds its centre colour in to here
float constexpr wedgeRadius{0.717f};
float constexpr capRadiusClosed{0.22f}; ///< with the wedge shut
float constexpr capRadiusOpen{0.48f};   ///< with it fully open

/// \note The wedge's travel is KnobPainter::halfSweepDegrees, shared with the
/// editor knob's pointer -- see the note there.

float constexpr rimThickness{1.0f}; ///< px
} // namespace ModuleKnobStyle

////////////////////////////////////////////////////////////////////////////////
///
/// \namespace TriggerButtonStyle
///
////////////////////////////////////////////////////////////////////////////////

/// \brief Everything a trigger button is drawn from that a module knob is not.
///
///   Which is the cap and nothing else. Skin files 13 and 14 until 18.08.2026,
/// and one radial gradient each: they shared every stop from the cap outward,
/// and what made them two files was the five inside it -- black in one, blue in
/// the other. \see KnobPainter, whose dome and rim are the rest of it.
///
/// \note The cap is a shadow on the dome rather than a disc on it, at both
/// radii, which is what the second number in each pair is for. A module knob's
/// is hard edged, and stays that way.
namespace TriggerButtonStyle
{
float constexpr capRadius{0.201f};     ///< the black cap, out to where it is solid
float constexpr capEdgeRadius{0.260f}; ///< and where it has faded into the dome
float constexpr litRadius{0.113f};     ///< the blue eye that says it is on
float constexpr litEdgeRadius{0.172f}; ///< which fades out inside the cap

/// \note Both pairs are a straight fade fitted to the artwork's stops, and the
/// fit is good: the cap's own ramp read 0.92, 0.63 and 0.375 opaque at the three
/// radii between, against 1.0, 0.67 and 0.33 for a line. Taking the solid edge
/// at the *first* of those rather than at the last fully black stop is what
/// makes it so -- the artwork holds black a little past where the cap ends.

/// The face, in pixels. \see TriggerButton, which is a caption wider than this.
unsigned int constexpr diameter{51};
} // namespace TriggerButtonStyle

/// \brief Draws a trigger button's face into the square \p bounds.
///
/// \param on whether the trigger is firing, which is the blue eye in the middle
/// of its cap.
void paintTriggerButton(juce::Graphics &, juce::Rectangle<float> bounds, bool on);

/// \brief Draws a module knob into the square \p bounds.
///
/// \param normalisedValue where the value sits in its range, 0 to 1; for a
/// \p bipolar knob 0.5 is the centre the wedge opens from.
/// \param drawWedge false while an LFO drives the parameter, when the knob's own
/// value says nothing -- what the ModuleKnobLFOed bitmap used to be.
/// \param selected whether it has the keyboard focus.
void paintModuleKnob(juce::Graphics &, juce::Rectangle<float> bounds, float normalisedValue,
                     bool bipolar, bool drawWedge, bool selected);

} // namespace LE::SW::GUI

#endif // moduleKnobPainter_hpp
