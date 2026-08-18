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
/// placed by constants of their own in the same 563 x 376 system and the two
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
int constexpr width{563};
int constexpr height{376};
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
Panel constexpr leftColumn{9.0f, 7.0f, 200.0f, 369.0f, 7.80f};
Ramp constexpr leftColumnRamp{30.0f,   -30.0f, -177.0f, 177.0f,
                              0.5145f, 1.06f,  0.98f,   ColourMap::EditorPanel};

Panel constexpr moduleRack{210.0f, 7.0f, 555.0f, 369.0f, 8.28f};
Ramp constexpr moduleRackRamp{-70.0f,  70.0f, 264.5f, -264.5f,
                              0.4484f, 1.96f, 0.932f, ColourMap::EditorPanel};

/// \brief The two blocks that join them, which is why the gap between the
/// columns is not a gap all the way down.
///@{
Panel constexpr upperJoin{197.0f, 65.0f, 211.0f, 86.0f, 0.00f};
Panel constexpr lowerJoin{197.0f, 306.0f, 211.0f, 327.0f, 0.00f};
///@}
///@}

////////////////////////////////////////////////////////////////////////////////
/// \name The column of boxes between them
////////////////////////////////////////////////////////////////////////////////
///@{
Panel constexpr centreColumn{71.0f, 11.0f, 195.0f, 330.0f, 8.56f};
Ramp constexpr centreColumnRamp{30.0f,   -30.0f, -177.0f, 177.0f,
                                0.5145f, 1.26f,  0.332f,  ColourMap::EditorWell};

/// The name of the module whose controls are showing.
Panel constexpr moduleNameBox{75.0f, 14.0f, 191.0f, 34.0f, 7.99f};

/// The control being edited -- the one box with a blue rule, because it is the
/// one that says *this is what the knobs below are*.
Panel constexpr activeControlBox{75.0f, 38.0f, 191.0f, 75.0f, 9.32f};

/// And its value, under it.
Panel constexpr controlValueBox{75.0f, 79.0f, 191.0f, 157.0f, 8.12f};

/// \brief The LFO's box, which is the one shape here that is not a rectangle:
/// its top edge steps up on the right so that the word LFO sits in the notch.
///@{
Panel constexpr lfoBox{75.0f, 162.0f, 191.0f, 289.0f, 8.31f};
float constexpr lfoNotchBottom{173.76f}; ///< where the low half of the top edge runs
float constexpr lfoNotchRight{126.29f};  ///< and where it turns up
float constexpr lfoNotchRadius{6.12f};
///@}
Ramp constexpr lfoBoxRamp{30.0f,   -30.0f, -177.0f, 177.0f,
                          0.5145f, 2.0f,   0.213f,  ColourMap::EditorWell};

/// The pill inside it that the waveform button sits in.
Panel constexpr lfoWaveformWell{146.0f, 251.0f, 178.0f, 271.0f, 8.41f};

/// What is feeding the side channel.
Panel constexpr sideChainSourceBox{75.0f, 306.0f, 191.0f, 327.0f, 7.93f};
Ramp constexpr sideChainSourceRamp{30.0f,   -30.0f, -177.0f, 177.0f,
                                   0.5145f, 1.0f,   0.05f,   ColourMap::EditorWell};

/// And the strip the Presets and Settings buttons sit in, at the foot.
Panel constexpr buttonRowBox{71.0f, 334.0f, 195.0f, 366.0f, 8.37f};
Ramp constexpr buttonRowRamp{30.0f,   -30.0f, -177.0f, 177.0f,
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
///                                       (18.08.2026.)
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

float constexpr knobDiameter{55.0f};
///@}

/// What the well shows around the knob it holds.
float constexpr wellMargin{2.25f};

/// \note Was three concentric arcs at 1.2, 2.6 and 4.6 wide and 40, 18 and 3
/// per cent -- a soft lift down the lower left of each well, and the same
/// stacked-stroke trick the bevel used. One rule now, all the way round.
float constexpr wellRuleThickness{1.0f};
float constexpr wellRuleAlpha{0.30f};
///@}

////////////////////////////////////////////////////////////////////////////////
/// \name The rules
///
/// \note A whole pixel, drawn just inside a whole-pixel edge -- which is why
/// every rectangle above is on integers. The artwork's frames were 0.69 wide
/// and its panels sat on halves, so at one to one every edge landed across two
/// pixels and read as a soft grey seam rather than as a line. That was the one
/// thing about it that could not be fixed by redrawing it.
///                                       (18.08.2026.)
////////////////////////////////////////////////////////////////////////////////
///@{
float constexpr ruleThickness{1.0f};
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
char constexpr inLabel[]{"in"};
char constexpr outLabel[]{"out"};
char constexpr mixLabel[]{"mix"};
char constexpr lfoLabel[]{"LFO"};
/// \note "External audio" until 18.08.2026, which the box under it stopped
/// being two changes ago: it answers "what goes into the side channel" and has
/// three answers, of which a file is one. \see issue #113 and
/// tests/gui/sideChainSelectorTests.cpp. The old name was outlines in the
/// artwork, so this is the first release in which it could be said.
char constexpr sideChainSourceLabel[]{"Sidechain source"};
char constexpr productLabel[]{"Spectrum"};
char constexpr productLabelSecondLine[]{"Worx"};

KnobWell constexpr knobWells[]{{18u, 37u, inLabel}, {18u, 110u, outLabel}, {18u, 185u, mixLabel}};

/// \note Twelve, which is Theme::labelFont() -- the size the live text beside
/// them is set at. The artwork's were 8 to 10, sized for a 2010 screen, and
/// against a module name at 14 and a combo box at 12 they read as small print.
///                                       (18.08.2026.)
float constexpr knobLabelHeight{12.0f};
/// \note Centred over the knob and a constant above it, which the artwork's
/// three were to within a pixel and a half. \see the note on the wells.
///
/// \note Three pixels further up than the artwork had it: a caption whose
/// descender line sits on the circle below it reads as attached to it, and at
/// twelve rather than ten it did.
float constexpr knobLabelRise{14.0f};

float constexpr lfoLabelHeight{12.0f};
float constexpr lfoLabelX{79.7f};
float constexpr lfoLabelY{163.0f};

float constexpr sideChainSourceLabelHeight{12.0f};

/// \note Centred over the box it names, which the artwork's was not: it sat ten
/// pixels to the left of it, and at a length nobody may change again that was a
/// placement rather than a decision.
///                                       (18.08.2026.)
float constexpr sideChainSourceLabelY{294.8f};

/// \note The regular face, not a lighter one -- there is no lighter one. The
/// wordmark measures 0.126 of stem to cap height against Vera Roman's 0.115 and
/// Vera Bold's 0.230, and it reads light because everything around it is bold.
/// Bitstream Vera ships Roman, Bold and their obliques and nothing between.
///                                       (18.08.2026.)
///@{
/// \note Both lines centred on one axis, which is what the artwork's stagger
/// was: it set them at 16.4 and 26.9 and their ink centres came out 38.1 and
/// 38.65. Eleven and a half rather than twelve is what fits between the panel's
/// edge and the column of boxes.
float constexpr productLabelHeight{11.5f};
float constexpr productLabelCentreX{38.4f};
float constexpr productLabelY{330.5f};
float constexpr productLabelSecondLineY{346.5f};
///@}
///@}

/// \brief Where the mark goes. \see logoArtwork(), which is assets/LOGO.svg.
///
/// \note The trademark sign that sat to its right is not reproduced.
///@{
float constexpr logoX{21.06f};
float constexpr logoY{297.5f};
float constexpr logoWidth{29.31f};
float constexpr logoHeight{26.31f};
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
