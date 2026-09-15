////////////////////////////////////////////////////////////////////////////////
///
/// \file framePainter.hpp
/// ----------------------
///
///   The skin's other repeated drawing: a rounded box with a hairline rim
/// around a flat fill, and a soft halo outside it.
///
///   A module strip is one, at 68 x 358 with a blue rim and a halo when it has
/// the focus. A combo box is the same thing at 60 x 18 and at 150 x 21, with a
/// halo always and a rim that is blue at rest and white when it has the focus.
/// The preset browser's panel and the settings page are two more, nested.
///
///   They were eight skin files and they are eight numbers each now. What is
/// *not* shared is the numbers: the two combo sizes disagree about their corner
/// radius by a pixel and a third and about their insets by half a pixel, and
/// pretending otherwise would move controls to no purpose. Same arrangement as
/// ModuleKnobStyle and EditorKnobStyle -- one drawing, a style per place it is
/// drawn.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef framePainter_hpp__5B2E9A17_C043_4D6E_8F71_92C4A0E5D3B6
#define framePainter_hpp__5B2E9A17_C043_4D6E_8F71_92C4A0E5D3B6
//------------------------------------------------------------------------------
#include <juce_graphics/juce_graphics.h>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \struct FrameStyle
///
////////////////////////////////////////////////////////////////////////////////

/// \brief Where one frame's rim sits inside its widget, and how far its halo
/// reaches. All in skin pixels.
///
/// \note Top and bottom separately because none of the artwork was centred:
/// every one of these boxes sits half a pixel to a pixel below the middle of
/// its widget, consistently enough between files to be where it was drawn
/// rather than where it was traced.
struct FrameStyle
{
    float sideInset; ///< widget edge to the *outside* of the rim
    float topInset;
    float bottomInset;
    float cornerRadius; ///< also at the outside of the rim
    float rimThickness;

    /// \brief The halo: \p glowRings rounded rectangles, each a pixel further
    /// out and fainter than the last, laid down before the frame covers their
    /// middles. The outermost falls outside the widget and is clipped, in the
    /// artwork as here.
    ///@{
    unsigned int glowRings;
    float glowInnerAlpha; ///< at the ring against the rim
    float glowOuterAlpha; ///< and at the last one
    ///@}
}; // struct FrameStyle

////////////////////////////////////////////////////////////////////////////////
///
/// \class FramePainter
///
////////////////////////////////////////////////////////////////////////////////

class FramePainter
{
  public:
    /// \brief Draws a frame filling \p bounds.
    ///
    /// \param haloStrength what to multiply the glow's opacity by, zero for no
    /// glow at all. A module strip asks for the full one only when it is the
    /// selected one and a fraction of it under the pointer; a combo box always
    /// has it and says which one is selected with \p rim instead.
    static void paint(juce::Graphics &, juce::Rectangle<float> bounds, FrameStyle const &,
                      juce::Colour rim, juce::Colour fill, float haloStrength);

    /// \brief Where the rim's outer edge lands, for a caller that has to put
    /// something else against it.
    static juce::Rectangle<float> rimWithin(juce::Rectangle<float> bounds, FrameStyle const &);

    /// a hover halo inside a rule already drawn, so a widget no bigger than its box can draw it
    static void paintInnerGlow(juce::Graphics &, juce::Rectangle<float> shape, float cornerRadius,
                               float ruleThickness, float strength);

  public:
    FramePainter() = delete; // a drawing, not an object
}; // class FramePainter

} // namespace LE::SW::GUI

#endif // framePainter_hpp
