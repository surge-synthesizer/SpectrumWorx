////////////////////////////////////////////////////////////////////////////////
///
/// \file backgroundPainter.cpp
/// ---------------------------
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/painters/backgroundPainter.hpp"

#include "gui/painters/glyphPainter.hpp"
#include "gui/resources.hpp"

#include <cmath>

namespace LE::SW::GUI
{

namespace
{
using namespace BackgroundStyle;

juce::Rectangle<float> rectangleOf(Panel const &panel)
{
    return {panel.x, panel.y, panel.right - panel.x, panel.bottom - panel.y};
}

/// \brief The rule, drawn just *inside* \p shape's edge.
///
/// \note Inside rather than centred on it, so that a whole-pixel rule on a
/// whole-pixel rectangle covers exactly one column of pixels. Centred, it would
/// straddle two and be half a rule on each. \see BackgroundStyle::ruleThickness.
void paintRule(juce::Graphics &graphics, juce::Rectangle<float> const shape, float const radius,
               ColourMap::Name const colour)
{
    graphics.setColour(ColourMap::getColour(colour));
    graphics.drawRoundedRectangle(shape.reduced(ruleThickness / 2), radius - ruleThickness / 2,
                                  ruleThickness);
}

////////////////////////////////////////////////////////////////////////////////
//
// fillOf()
// --------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The stops are laid down here rather than being two colours and a
/// curve, because juce::ColourGradient interpolates straight between whatever
/// it is given: the ease is only in the *positions*, so the curve has to be
/// sampled. Eight samples over the ramp is well inside the two parts in 255 the
/// artwork's own dozen stops sat at.
///
////////////////////////////////////////////////////////////////////////////////

juce::ColourGradient fillOf(Ramp const &ramp)
{
    auto const dark(ColourMap::getColour(ramp.darkening));
    auto const lit(
        dark.interpolatedWith(ColourMap::getColour(ColourMap::EditorGradientStart), ramp.lift));

    juce::ColourGradient gradient(dark, ramp.fromX, ramp.fromY, lit, ramp.toX, ramp.toY, false);
    gradient.addColour(ramp.flatStop, dark);

    unsigned int constexpr samples{8};
    for (unsigned int sample(1); sample < samples; ++sample)
    {
        auto const along(static_cast<float>(sample) / samples);
        gradient.addColour(ramp.flatStop + along * (1 - ramp.flatStop),
                           dark.interpolatedWith(lit, std::pow(along, ramp.ease)));
    }
    return gradient;
}

/// \brief A rounded rectangle with its ramp in it and a rule round it.
void paintPanel(juce::Graphics &graphics, Panel const &panel, Ramp const &ramp)
{
    auto const shape(rectangleOf(panel));
    graphics.setGradientFill(fillOf(ramp));
    graphics.fillRoundedRectangle(shape, panel.cornerRadius);

    paintRule(graphics, shape, panel.cornerRadius, ColourMap::EditorRule);
}

/// \brief The same, flat-filled, which is what the three boxes at the top of the
/// centre column are.
void paintBox(juce::Graphics &graphics, Panel const &panel, ColourMap::Name const fill,
              ColourMap::Name const rule)
{
    auto const shape(rectangleOf(panel));
    graphics.setColour(ColourMap::getColour(fill));
    graphics.fillRoundedRectangle(shape, panel.cornerRadius);

    paintRule(graphics, shape, panel.cornerRadius, rule);
}

////////////////////////////////////////////////////////////////////////////////
//
// lfoBoxPath()
// ------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The one shape in the chassis that is not a rounded rectangle. Its top
/// edge runs low across the left, turns up through a pair of opposed arcs and
/// runs high across the right, which is the step the word LFO sits in.
///
////////////////////////////////////////////////////////////////////////////////

juce::Path lfoBoxPath()
{
    auto const shape(rectangleOf(lfoBox));
    auto const r(lfoBox.cornerRadius);
    auto const n(lfoNotchRadius);

    juce::Path path;
    path.startNewSubPath(shape.getX() + r, lfoNotchBottom);
    path.lineTo(lfoNotchRight - n, lfoNotchBottom);
    //   Down-turning then up-turning, so the step reads as one S rather than as
    // two corners: the first arc is convex to the box and the second concave.
    path.quadraticTo(lfoNotchRight, lfoNotchBottom, lfoNotchRight, lfoNotchBottom - n);
    path.lineTo(lfoNotchRight, shape.getY() + n);
    path.quadraticTo(lfoNotchRight, shape.getY(), lfoNotchRight + n, shape.getY());

    path.lineTo(shape.getRight() - r, shape.getY());
    path.quadraticTo(shape.getRight(), shape.getY(), shape.getRight(), shape.getY() + r);
    path.lineTo(shape.getRight(), shape.getBottom() - r);
    path.quadraticTo(shape.getRight(), shape.getBottom(), shape.getRight() - r, shape.getBottom());
    path.lineTo(shape.getX() + r, shape.getBottom());
    path.quadraticTo(shape.getX(), shape.getBottom(), shape.getX(), shape.getBottom() - r);
    path.lineTo(shape.getX(), lfoNotchBottom + r);
    path.quadraticTo(shape.getX(), lfoNotchBottom, shape.getX() + r, lfoNotchBottom);
    path.closeSubPath();
    return path;
}

/// \brief One knob well: the disc a knob sits in, and the rule round it.
///
/// \note \p knobTop is the widget's, so the well is concentric with the knob
/// whatever the knob's own placement says. \see the note in the header.
void paintWell(juce::Graphics &graphics, float const knobLeft, float const knobTop,
               float const knobDiameter)
{
    auto const centre(juce::Point<float>(knobLeft + knobDiameter / 2, knobTop + knobDiameter / 2));
    auto const radius(knobDiameter / 2 + wellMargin);
    auto const disc(juce::Rectangle<float>(2 * radius, 2 * radius).withCentre(centre));

    graphics.setColour(ColourMap::getColour(ColourMap::EditorWellFace));
    graphics.fillEllipse(disc);

    graphics.setColour(
        ColourMap::getColour(ColourMap::EditorRule).withMultipliedAlpha(wellRuleAlpha));
    graphics.drawEllipse(disc.reduced(wellRuleThickness / 2), wellRuleThickness);
}

////////////////////////////////////////////////////////////////////////////////
//
// paintLabel()
// ------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note Placed by its *ink*, which is how the artwork's positions were
/// measured and the only way two typesetters agree: a juce::Font's origin is a
/// baseline and its ascent counts room no glyph in "mix" reaches into. The
/// arrangement is laid out at zero and then moved by the difference.
///
////////////////////////////////////////////////////////////////////////////////

void paintLabel(juce::Graphics &graphics, juce::String const &text, juce::Font const &font,
                float const inkX, float const inkY, ColourMap::Name const colour)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0, 0);

    auto const ink(glyphs.getBoundingBox(0, -1, false));
    glyphs.moveRangeOfGlyphs(0, -1, inkX - ink.getX(), inkY - ink.getY());

    graphics.setColour(ColourMap::getColour(colour));
    glyphs.draw(graphics);
}

/// \brief The same, centred horizontally on \p centreX rather than placed by its
/// left edge -- which is what the three knob labels are.
void paintCentredLabel(juce::Graphics &graphics, juce::String const &text, juce::Font const &font,
                       float const centreX, float const inkY, ColourMap::Name const colour)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0, 0);
    auto const ink(glyphs.getBoundingBox(0, -1, false));
    paintLabel(graphics, text, font, centreX - ink.getWidth() / 2, inkY, colour);
}

juce::Font boldFont(float const height)
{
    return juce::Font(juce::FontOptions(boldTypeface()).withHeight(height));
}
juce::Font regularFont(float const height)
{
    return juce::Font(juce::FontOptions(regularTypeface()).withHeight(height));
}

/// \brief How wide \p text comes out in \p font, measured over the ink rather
/// than over the advance -- which is what everything centred here is placed by.
float inkWidth(juce::String const &text, juce::Font const &font)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0, 0);
    return glyphs.getBoundingBox(0, -1, false).getWidth();
}

/// \brief What the sidechain source label and the lock beside it take together,
/// and where their left edge therefore falls.
float sideChainLockLeft()
{
    auto const pair(GlyphStyle::lockWidth + sideChainLockGap +
                    inkWidth(sideChainSourceLabel, boldFont(sideChainSourceLabelHeight)));
    return rectangleOf(sideChainSourceBox).getCentreX() - pair / 2;
}
} // anonymous namespace

juce::Colour BackgroundPainter::gutterColour()
{
    return ColourMap::getColour(ColourMap::EditorSurround);
}

juce::Rectangle<float> BackgroundPainter::sideChainLockBounds()
{
    return {sideChainLockLeft(), sideChainSourceLabelY, GlyphStyle::lockWidth,
            GlyphStyle::lockHeight};
}

/// \note A rule and nothing else: what shows inside it is the LFO box it is cut
/// into, which the chassis has already drawn.
void BackgroundPainter::paintLFOWaveformWell(juce::Graphics &graphics,
                                             juce::Point<int> const origin)
{
    paintRule(graphics, rectangleOf(lfoWaveformWell).translated(-origin.x, -origin.y),
              lfoWaveformWell.cornerRadius, ColourMap::EditorRule);
}

////////////////////////////////////////////////////////////////////////////////
//
// BackgroundPainter::paint()
// --------------------------
//
////////////////////////////////////////////////////////////////////////////////

void BackgroundPainter::paint(juce::Graphics &graphics, juce::Rectangle<float> const bounds)
{
    juce::Graphics::ScopedSaveState const state(graphics);
    graphics.addTransform(juce::AffineTransform::translation(bounds.getX(), bounds.getY()));

    graphics.fillAll(ColourMap::getColour(ColourMap::EditorSurround));

    paintPanel(graphics, leftColumn, leftColumnRamp);
    paintPanel(graphics, moduleRack, moduleRackRamp);
    paintPanel(graphics, centreColumn, centreColumnRamp);

    // module joiners
    graphics.setColour(ColourMap::getColour(ColourMap::EditorPanel));
    graphics.fillRect(rectangleOf(upperJoin));
    graphics.fillRect(rectangleOf(lowerJoin));

    // top and bottom strokes of the joiners
    graphics.setColour(ColourMap::getColour(ColourMap::EditorRule));
    graphics.fillRect(rectangleOf(upperJoin).withHeight(2.f));
    graphics.fillRect(rectangleOf(upperJoin).withTrimmedTop(29.f));
    graphics.fillRect(rectangleOf(lowerJoin).withHeight(2.f));
    graphics.fillRect(rectangleOf(lowerJoin).withTrimmedTop(29.f));

    paintBox(graphics, moduleNameBox, ColourMap::EditorWell, ColourMap::EditorRule);
    paintBox(graphics, activeControlBox, ColourMap::EditorWell, ColourMap::EditorRule);
    paintBox(graphics, controlValueBox, ColourMap::EditorWell, ColourMap::EditorRule);

    {
        auto const path(lfoBoxPath());
        graphics.setGradientFill(fillOf(lfoBoxRamp));
        graphics.fillPath(path);
        graphics.setColour(ColourMap::getColour(ColourMap::EditorRule));
        graphics.strokePath(path, juce::PathStrokeType(ruleThickness));
    }
    /// \note The waveform button's well is not here. \see
    /// paintLFOWaveformWell(), which the LFO strip calls.

    paintPanel(graphics, sideChainSourceBox, sideChainSourceRamp);
    paintPanel(graphics, buttonRowBox, buttonRowRamp);

    ////////////////////////////////////////////////////////////////////////////
    /// \note The wells and their labels are placed from the knobs, which are
    /// placed by SpectrumWorxEditor's own constants -- so the two cannot drift.
    ////////////////////////////////////////////////////////////////////////////
    for (auto const &knob : knobWells)
    {
        paintWell(graphics, knob.x, knob.y, knobDiameter);
        paintCentredLabel(graphics, knob.label, boldFont(knobLabelHeight),
                          knob.x + knobDiameter / 2, knob.y - knobLabelRise, ColourMap::Accent);
    }

    paintLabel(graphics, lfoLabel, boldFont(lfoLabelHeight), lfoLabelX, lfoLabelY, ColourMap::Text);

    /// \note Placed by its left edge rather than centred, because what is
    /// centred over the box is the label *and* the lock in front of it.
    /// \see sideChainLockBounds(), which is where the editor puts the widget.
    paintLabel(graphics, sideChainSourceLabel, boldFont(sideChainSourceLabelHeight),
               sideChainLockLeft() + GlyphStyle::lockWidth + sideChainLockGap,
               sideChainSourceLabelY, ColourMap::Text);

    paintCentredLabel(graphics, productLabel, regularFont(productLabelHeight), productLabelCentreX,
                      productLabelY, ColourMap::Wordmark);
    paintCentredLabel(graphics, productLabelSecondLine, regularFont(productLabelHeight),
                      productLabelCentreX, productLabelSecondLineY, ColourMap::Wordmark);

    logoArtwork().drawWithin(graphics, {logoX, logoY, logoWidth, logoHeight});
}

} // namespace LE::SW::GUI
