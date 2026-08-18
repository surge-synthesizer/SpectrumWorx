////////////////////////////////////////////////////////////////////////////////
///
/// \file knobPainter.hpp
/// ---------------------
///
///   What the editor's round controls are drawn from.
///
///   Three of them: a module knob (a grey dome with a blue wedge), a trigger
/// button (the same dome, with a cap that turns blue instead of a wedge) and an
/// editor knob (a bevel, a teal ring and a rotating pointer, which shares
/// nothing with the other two but its travel).
///
///   So what is here is the mechanism rather than the drawings -- the two
/// gradients they are all made of, and the dome, cap, rim and focus ring the
/// first two are made of. The numbers stay where each control is defined: \see
/// ModuleKnobStyle and TriggerButtonStyle in modules/moduleUI.hpp, and
/// EditorKnobStyle in gui.hpp.
///
/// \note Two things are here rather than there, because two controls sitting in
/// the same strip cannot disagree about them and be right. The travel --
/// halfSweepDegrees -- was fitted independently off two film strips and came
/// back the same both times. The focus ring is what says which control the
/// editor is showing, and a module knob and a trigger button are stacked one
/// above the other.
///
///   Radii below are fractions of the control's own radius, so the same numbers
/// hold at the 51 px module knob and the 23 px shared one.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef knobPainter_hpp__9A47C2E8_63B1_4F05_A8D2_71E3B94C6F50
#define knobPainter_hpp__9A47C2E8_63B1_4F05_A8D2_71E3B94C6F50
//------------------------------------------------------------------------------
#include <juce_graphics/juce_graphics.h>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class KnobPainter
///
////////////////////////////////////////////////////////////////////////////////

class KnobPainter
{
  public:
    ////////////////////////////////////////////////////////////////////////////
    /// \name The travel
    ////////////////////////////////////////////////////////////////////////////
    ///@{
    /// Half of it, in degrees clockwise from twelve o'clock.
    static float constexpr halfSweepDegrees{135.0f};

    /// \brief Where \p normalisedValue points: -135 at the bottom of the range,
    /// 0 straight up, +135 at the top.
    static constexpr float angleFor(float const normalisedValue)
    {
        return (2 * normalisedValue - 1) * halfSweepDegrees;
    }
    ///@}

    ////////////////////////////////////////////////////////////////////////////
    /// \name The two gradients everything round is made of
    ////////////////////////////////////////////////////////////////////////////
    ///@{
    /// \brief A radial gradient about \p centre whose parameter runs 0 to 1 over
    /// \p radius, so that every stop can be written as the fraction of the
    /// control's radius it was measured at.
    static juce::ColourGradient radialAbout(juce::Point<float> centre, float radius,
                                            juce::Colour inner, juce::Colour outer);

    /// \brief Paints \p gradient through the annulus between \p inner and
    /// \p outer, both about \p centre.
    ///
    /// \note A ring rather than a disc because these all have something to
    /// protect: the editor knob's bright rim would cover its own teal, and a
    /// focus halo its wedge. Filling the whole disc with a gradient that happens
    /// to be transparent in the middle does work, and needs two stops spent
    /// saying so -- an even-odd path says it once and cannot be got subtly
    /// wrong.
    static void fillRing(juce::Graphics &, juce::Point<float> centre, float inner, float outer,
                         juce::ColourGradient const &);
    ///@}

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \name The dome
    ///
    ///   A module knob and a trigger button are the same lit grey disc with the
    /// same black cap in the middle. Skin files 13 and 14 were the trigger
    /// button's two states and were one radial gradient each, sharing every stop
    /// from the cap outward -- what made them two files was the five stops
    /// inside it, black in one and blue in the other.
    ///
    /// \note The dome is the *knob's*, which the trigger button now takes. Its
    /// own ran some eight parts in 255 brighter across the face and carried a
    /// crest just outside the cap that the knob has never had. Two controls a
    /// centimetre apart in the same strip, drawn from one gradient with two
    /// fits: the difference was never a decision.
    ///                                       (18.08.2026.)
    ///
    ////////////////////////////////////////////////////////////////////////////
    ///@{
    /// \brief The lit grey disc filling \p bounds.
    ///
    /// \param flatRadius how far out the dome holds its centre colour before it
    /// starts falling away -- under the cap, for both of the controls that have
    /// one.
    static void paintDome(juce::Graphics &, juce::Rectangle<float> bounds, float flatRadius);

    /// \brief The hairline that closes the dome's edge, in pixels rather than
    /// fractions: it is a hairline at every size the dome is drawn at.
    ///
    /// \note Not drawn under a focus ring, which reaches past where it would be
    /// and reads as a hard outline through it.
    static void paintDomeRim(juce::Graphics &, juce::Rectangle<float> bounds, float thickness);

    /// \brief The flat disc in the middle.
    ///
    /// \param solidRadius out to where it is opaque, and \p edgeRadius where it
    /// has faded out entirely. Equal for a hard edge, which is what a module
    /// knob's cap has; a trigger button's is a shadow on the dome rather than a
    /// disc on it, and so are both of its.
    static void paintCap(juce::Graphics &, juce::Rectangle<float> bounds, float solidRadius,
                         float edgeRadius, juce::Colour);
    ///@}

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \name The focus ring
    ///
    ////////////////////////////////////////////////////////////////////////////
    ///@{
    /// \brief The ring that says a round control has the keyboard focus: white
    /// on \p radius and gone \c focusGlow either side of it.
    ///
    /// \note Skin file 58 until 18.08.2026 -- a 53 x 53 disc carrying a radial
    /// gradient whose ten stops were all the same white and differed only in
    /// opacity. A plain stroke reads as a hard outline against the dome's black
    /// edge, which is not what this looked like.
    static void paintFocusRing(juce::Graphics &, juce::Point<float> centre, float radius);

    /// In pixels either side of the rim, and the reason a knob is laid out with
    /// a margin around it. \see ModuleKnob::marginForGlow.
    static float constexpr focusGlow{2.0f};
    ///@}

  public:
    KnobPainter() = delete; // a drawing, not an object
}; // class KnobPainter

} // namespace LE::SW::GUI

#endif // knobPainter_hpp
