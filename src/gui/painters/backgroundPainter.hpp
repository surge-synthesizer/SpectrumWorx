////////////////////////////////////////////////////////////////////////////////
///
/// \file backgroundPainter.hpp
/// ---------------------------
///
///   The editor's chassis: the two panels, the column of boxes between them,
///   the three knob wells, the six labels and the mark.
///
///   Skin file 01 until 18.08.2026, and 22 KB of it -- 52 paths and six
/// gradients, which was 45 % of everything left in assets/skin. What it was
/// made of turned out to be one idea repeated: **every panel is its own dark,
/// held flat down the first half of a 45 degree line and then eased into the
/// skin's blue by an amount of its own.** \see Ramp, whose three numbers
/// reproduce all six of the file's gradients to within two parts in 255.
///
///   Two things changed on the way in, both asked for:
///
///   The bevel is gone. Each panel carried a shadow down its edges and two
/// white inner frames, and each box a halo of one to seven per cent -- the 2010
/// way of saying "this is raised". They are a rule now, which is the same thing
/// the boxes inside them already said with one line.
///
///   The mark is assets/LOGO.svg, drawn in place rather than traced into this
/// file, and the trademark sign beside it is gone. \see logoArtwork().
///
/// \note What has *not* changed is a single coordinate. Every rectangle here is
/// the artwork's, to the hundredth of a pixel, because the editor's widgets are
/// placed by constants of their own in the same 845 x 564 system and the two
/// have to agree. The three exceptions are noted where they are: the knob
/// wells, the labels over them, and the wells' rims, all of which were a
/// regular grid the trace had wandered off.
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef backgroundPainter_hpp__F62A18D9_7C34_4E05_B8A1_5D93E074C2B6
#define backgroundPainter_hpp__F62A18D9_7C34_4E05_B8A1_5D93E074C2B6
//------------------------------------------------------------------------------
#include "gui/colourMap.hpp"
#include "gui/painters/ruleStyle.hpp"

#include <juce_graphics/juce_graphics.h>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \namespace BackgroundStyle
///
////////////////////////////////////////////////////////////////////////////////

namespace BackgroundStyle
{
/// The chassis, and the coordinate system every offset in the editor is in.
///@{
int constexpr width{845};
int constexpr height{564};
///@}

/// \brief What every panel in the editor is filled with.
///
///   A line across the editor -- in *editor* coordinates, not the panel's, so
/// that one light direction runs through all of them -- with the panel's dark
/// held down the first \c flatStop of it and then eased into the skin's blue.
///
/// \note \c lift is how far toward the blue it gets by the far end: 0.98 for
/// the left column, which reaches it, and 0.05 for the strip under the LFO,
/// which barely warms. \c ease is 1 for a straight ramp and 2 for one that
/// holds its dark and then falls away -- the two big panels differ in this and
/// nothing else, and the difference is visible.
struct Ramp
{
    float fromX, fromY; ///< the line, in editor coordinates
    float toX, toY;
    float flatStop; ///< how far down it the dark is held
    float ease;
    float lift;                ///< 0 stays dark, 1 arrives at ColourMap::Accent
    ColourMap::Name darkening; ///< which dark it starts from
};

/// \brief One of the rounded rectangles the chassis is made of.
struct Panel
{
    float x, y, right, bottom;
    float cornerRadius;
};

////////////////////////////////////////////////////////////////////////////////
/// \name The two big panels
///
///   Their ramps run in opposite directions -- the left column brightens
/// towards its bottom left, the module rack towards its top right -- which is
/// what puts the blue in diagonally opposite corners of the editor.
////////////////////////////////////////////////////////////////////////////////
///@{
Panel constexpr leftColumn{14.0f, 11.0f, 300.0f, 554.0f, 11.70f};
Ramp constexpr leftColumnRamp{45.0f,   -45.0f, -265.5f, 265.5f,
                              0.5145f, 1.06f,  0.98f,   ColourMap::EditorPanel};

Panel constexpr moduleRack{315.0f, 11.0f, 833.0f, 554.0f, 12.42f};
Ramp constexpr moduleRackRamp{-105.0f, 105.0f, 396.75f, -396.75f,
                              0.4484f, 1.96f,  0.932f,  ColourMap::EditorPanel};

/// \brief The two blocks that join them, which is why the gap between the
/// columns is not a gap all the way down.
///@{
Panel constexpr upperJoin{298.0f, 98.0f, 317.0f, 130.0f, 0.00f};
Panel constexpr lowerJoin{298.0f, 458.0f, 317.0f, 490.0f, 0.00f};
///@}
///@}

////////////////////////////////////////////////////////////////////////////////
/// \name The column of boxes between them
////////////////////////////////////////////////////////////////////////////////
///@{
Panel constexpr centreColumn{107.0f, 17.0f, 293.0f, 495.0f, 12.84f};
Ramp constexpr centreColumnRamp{45.0f,   -45.0f, -265.5f, 265.5f,
                                0.5145f, 1.26f,  0.332f,  ColourMap::EditorWell};

/// The name of the module whose controls are showing.
Panel constexpr moduleNameBox{113.0f, 21.0f, 287.0f, 51.0f, 11.99f};

/// The control being edited -- the one box with a blue rule, because it is the
/// one that says *this is what the knobs below are*.
Panel constexpr activeControlBox{113.0f, 57.0f, 287.0f, 113.0f, 13.98f};

/// And its value, under it.
Panel constexpr controlValueBox{113.0f, 119.0f, 287.0f, 236.0f, 12.18f};

/// \brief The LFO's box, which is the one shape here that is not a rectangle:
/// its top edge steps up on the right so that the word LFO sits in the notch.
///@{
Panel constexpr lfoBox{113.0f, 243.0f, 287.0f, 434.0f, 12.47f};
float constexpr lfoNotchBottom{260.64f}; ///< where the low half of the top edge runs
float constexpr lfoNotchRight{189.44f};  ///< and where it turns up
float constexpr lfoNotchRadius{9.18f};
///@}
Ramp constexpr lfoBoxRamp{45.0f,   -45.0f, -265.5f, 265.5f,
                          0.5145f, 2.0f,   0.213f,  ColourMap::EditorWell};

/// \brief The pill inside it that the waveform button sits in.
///
/// \note Drawn by the LFO strip rather than with the box it is cut into. \see
/// BackgroundPainter::paintLFOWaveformWell().
Panel constexpr lfoWaveformWell{219.0f, 377.0f, 267.0f, 407.0f, 12.62f};

/// What is feeding the side channel.
Panel constexpr sideChainSourceBox{113.0f, 459.0f, 287.0f, 491.0f, 11.90f};
Ramp constexpr sideChainSourceRamp{45.0f,   -45.0f, -265.5f, 265.5f,
                                   0.5145f, 1.0f,   0.05f,   ColourMap::EditorWell};

/// And the strip the Presets and Settings buttons sit in, at the foot.
Panel constexpr buttonRowBox{107.0f, 501.0f, 293.0f, 549.0f, 12.56f};
Ramp constexpr buttonRowRamp{45.0f,   -45.0f, -265.5f, 265.5f,
                             0.5145f, 2.0f,   0.213f,  ColourMap::EditorWell};
///@}

////////////////////////////////////////////////////////////////////////////////
///
/// \name The knob wells
///
/// \note Centred on their knobs and all one size, which the artwork's were not:
/// its three discs wandered a pixel and a half off the knobs they sit under and
/// disagreed about their radius by one. A knob is placed by a constant, three
/// of them share it, and a well that is not concentric with the knob in it is a
/// mistake nobody would have made on purpose.
///
////////////////////////////////////////////////////////////////////////////////
///@{
/// \brief The three knobs, as the editor places them.
///
/// \note Duplicated from SpectrumWorxEditor's own constructor, which is the
/// only way a painter two layers below the editor can put a well under a knob.
/// The editor asserts they agree -- \see its constructor.
///@{
struct KnobWell
{
    float x, y;
    char const *label;
};

float constexpr knobDiameter{83.0f};
///@}

/// What the well shows around the knob it holds.
float constexpr wellMargin{3.375f};

/// \note Was three concentric arcs at 1.2, 2.6 and 4.6 wide and 40, 18 and 3
/// per cent -- a soft lift down the lower left of each well, and the same
/// stacked-stroke trick the bevel used. One rule now, all the way round.
float constexpr wellRuleThickness{RuleStyle::thickness};
float constexpr wellRuleAlpha{0.30f};
///@}

////////////////////////////////////////////////////////////////////////////////
/// \name The rules
///
/// \note A whole number of pixels, drawn just inside a whole-pixel edge --
/// which is why every rectangle above is on integers. The artwork's frames were
/// 0.69 wide and its panels sat on halves, so at one to one every edge landed
/// across two pixels and read as a soft grey seam rather than as a line. That
/// was the one thing about it that could not be fixed by redrawing it.
///
/// \note \see RuleStyle::thickness for why it is two rather than one, and for
/// why the number is named once rather than in each painter that draws a line.
////////////////////////////////////////////////////////////////////////////////
///@{
float constexpr ruleThickness{RuleStyle::thickness};
///@}

////////////////////////////////////////////////////////////////////////////////
///
/// \name The labels
///
///   Six pieces of copy that were outlines in the artwork, so that renaming one
/// meant redrawing the file.
///
/// \note The sizes are fitted, not guessed: every one reproduces the artwork's
/// ink box to within a third of a pixel in both directions. in, out and mix
/// came back 9.9, 10.0 and 10.2 and are one size.
///
////////////////////////////////////////////////////////////////////////////////
///@{
char constexpr inLabel[]{"In"};
char constexpr outLabel[]{"Out"};
char constexpr mixLabel[]{"Mix"};
char constexpr lfoLabel[]{"LFO"};
/// \note "External audio" until 18.08.2026, which the box under it stopped
/// being two changes ago: it answers "what goes into the side channel" and has
/// three answers, of which a file is one. \see issue #113 and
/// tests/gui/sideChainSelectorTests.cpp. The old name was outlines in the
/// artwork, so this is the first release in which it could be said.
char constexpr sideChainSourceLabel[]{"Sidechain Source"};
char constexpr productLabel[]{"Spectrum"};
char constexpr productLabelSecondLine[]{"Worx"};

KnobWell constexpr knobWells[]{{27u, 56u, inLabel}, {27u, 165u, outLabel}, {27u, 278u, mixLabel}};

/// \note Twelve, which is Theme::labelFont() -- the size the live text beside
/// them is set at. The artwork's were 8 to 10, sized for a 2010 screen, and
/// against a module name at 14 and a combo box at 12 they read as small print.
float constexpr knobLabelHeight{18.0f};
/// \note Centred over the knob and a constant above it, which the artwork's
/// three were to within a pixel and a half. \see the note on the wells.
///
/// \note Three pixels further up than the artwork had it: a caption whose
/// descender line sits on the circle below it reads as attached to it, and at
/// twelve rather than ten it did.
float constexpr knobLabelRise{21.0f};

float constexpr lfoLabelHeight{16.f};
float constexpr lfoLabelX{118.f};
float constexpr lfoLabelY{241.f};

float constexpr sideChainSourceLabelHeight{16.f};
float constexpr sideChainSourceLabelY{439.f};

/// \brief What the padlock beside that label leaves between itself and it.
///
/// \note The lock and the words are centred over the box *as one*, so the label
/// moves right by half of what the lock and this gap take. Issue #44 moves the
/// preset browser's "Ignore external audio" toggle here: what it says is "this
/// preset does not get to bring its own audio file with it", which is a
/// statement about the sidechain source and belongs beside it rather than in a
/// row of preset navigation.
float constexpr sideChainLockGap{6.0f};

float constexpr productLabelHeight{18.f};
float constexpr productLabelCentreX{60.f};
float constexpr productLabelY{507.f};
float constexpr productLabelSecondLineY{525.f};
///@}
///@}

float constexpr logoX{41.f};
float constexpr logoY{447.f};
float constexpr logoWidth{45.f};
float constexpr logoHeight{45.f};
///@}
} // namespace BackgroundStyle

////////////////////////////////////////////////////////////////////////////////
///
/// \class BackgroundPainter
///
////////////////////////////////////////////////////////////////////////////////

class BackgroundPainter
{
  public:
    /// \brief Draws the whole chassis into \p bounds.
    static void paint(juce::Graphics &, juce::Rectangle<float> bounds);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The pill the LFO's waveform button sits in, for a widget whose
    /// top left is at \p origin in the chassis' own coordinates.
    ///
    ///   Not part of paint(), and that is the point: the LFO strip is only on
    /// screen while a control is selected, and everything in that box comes and
    /// goes with it. This one did not -- an empty LFO box with a lone pill in
    /// the corner of it is what issue #134 reports -- because it was drawn with
    /// the chassis rather than with what it holds.
    ///
    /// \note \p origin rather than the widget's bounds, so the rectangle stays
    /// written where the artwork measured it. \see
    /// BackgroundStyle::lfoWaveformWell.
    ///
    ////////////////////////////////////////////////////////////////////////////

    static void paintLFOWaveformWell(juce::Graphics &, juce::Point<int> origin);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Where the padlock beside the sidechain source label goes.
    ///
    ///   The editor places a widget there; this is what places it, because what
    /// the pair is centred on is the label's *ink*, and how wide that is depends
    /// on the copy and on the typeface. Asking here is what keeps the button and
    /// the words that the button belongs to from drifting apart.
    ///
    ////////////////////////////////////////////////////////////////////////////

    static juce::Rectangle<float> sideChainLockBounds();

    /// \brief What fills the gutter an overlay panel opens beside itself.
    ///
    /// \note SpectrumWorxEditor::paint() stretched the artwork's leftmost pixel
    /// column across it -- one pixel of the surround, blown up. This is that
    /// pixel, said out loud.
    static juce::Colour gutterColour();

  public:
    BackgroundPainter() = delete; // a drawing, not an object
}; // class BackgroundPainter

} // namespace LE::SW::GUI

#endif // backgroundPainter_hpp
