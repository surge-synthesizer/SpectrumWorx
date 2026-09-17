////////////////////////////////////////////////////////////////////////////////
///
/// \file glyphPainter.cpp
/// ----------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/painters/glyphPainter.hpp"

namespace LE::SW::GUI
{

namespace
{
using namespace GlyphStyle;

/// \brief \p ink centred in \p bounds, which is where every mark here is drawn.
juce::Rectangle<float> centred(juce::Rectangle<float> const bounds, float const width,
                               float const height)
{
    return juce::Rectangle<float>(width, height).withCentre(bounds.getCentre());
}
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
//
// GlyphPainter::paintFolderUp()
// -----------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note One stroked path for the stem and the foot, with a curved joint where
/// they meet, and a filled triangle on top of it. The stem's top end is butted
/// rather than rounded because it does not end there -- the arrowhead's base
/// sits on it, and a round cap would push a bulge out past that base.
///
////////////////////////////////////////////////////////////////////////////////

void GlyphPainter::paintFolderUp(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                                 juce::Colour const colour)
{
    auto const ink(centred(bounds, upWidth, upHeight));

    /// The stem is centred under the arrowhead, so the head's half width is
    /// also how far in from the left the whole vertical run stands.
    auto const stemX(ink.getX() + upHeadWidth / 2);
    auto const headBase(ink.getY() + upHeadHeight);

    juce::Path bend;
    bend.startNewSubPath(ink.getRight(), ink.getBottom() - upStroke / 2);
    bend.lineTo(stemX, ink.getBottom() - upStroke / 2);
    bend.lineTo(stemX, headBase);

    graphics.setColour(colour);
    graphics.strokePath(bend, juce::PathStrokeType(upStroke, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::butt));

    juce::Path head;
    head.startNewSubPath(stemX, ink.getY());
    head.lineTo(stemX + upHeadWidth / 2, headBase);
    head.lineTo(stemX - upHeadWidth / 2, headBase);
    head.closeSubPath();
    graphics.fillPath(head);
}

void GlyphPainter::paintSave(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                             juce::Colour const colour)
{
    auto const ink(centred(bounds, saveWidth, saveHeight));

    auto const trayTop(ink.getBottom() - saveTrayHeight);
    auto const point(trayTop - saveTrayGap);
    auto const headBase(point - saveHeadHeight);

    graphics.setColour(colour);
    graphics.fillRect(ink.getCentreX() - saveStemWidth / 2, ink.getY(), saveStemWidth,
                      headBase - ink.getY());

    juce::Path head;
    head.startNewSubPath(ink.getCentreX() - saveHeadWidth / 2, headBase);
    head.lineTo(ink.getCentreX() + saveHeadWidth / 2, headBase);
    head.lineTo(ink.getCentreX(), point);
    head.closeSubPath();
    graphics.fillPath(head);

    graphics.fillRect(ink.getX(), trayTop, saveWidth, saveTrayHeight);
}

/// \note Both paths are open at the top, where the lid covers them: a closed
/// rectangle would put a bar under the lid as well as on it.
void GlyphPainter::paintTrash(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                              juce::Colour const colour)
{
    auto const ink(centred(bounds, trashWidth, trashHeight));

    auto const lidTop(ink.getY() + trashHandleHeight);
    auto const bodyTop(lidTop + trashLidHeight);

    // placed by the line down the middle of the pen, and the joins round the
    // corners -- there is no radius here that is not half a stroke
    juce::PathStrokeType const pen(trashStroke, juce::PathStrokeType::curved,
                                   juce::PathStrokeType::butt);
    auto const handleHalf((trashHandleWidth - trashStroke) / 2);
    auto const bodyHalf((trashBodyWidth - trashStroke) / 2);

    juce::Path handle;
    handle.startNewSubPath(ink.getCentreX() - handleHalf, lidTop);
    handle.lineTo(ink.getCentreX() - handleHalf, ink.getY() + trashStroke / 2);
    handle.lineTo(ink.getCentreX() + handleHalf, ink.getY() + trashStroke / 2);
    handle.lineTo(ink.getCentreX() + handleHalf, lidTop);

    juce::Path body;
    body.startNewSubPath(ink.getCentreX() - bodyHalf, bodyTop);
    body.lineTo(ink.getCentreX() - bodyHalf, ink.getBottom() - trashStroke / 2);
    body.lineTo(ink.getCentreX() + bodyHalf, ink.getBottom() - trashStroke / 2);
    body.lineTo(ink.getCentreX() + bodyHalf, bodyTop);

    graphics.setColour(colour);
    graphics.strokePath(handle, pen);
    graphics.strokePath(body, pen);
    graphics.fillRect(ink.getX(), lidTop, trashWidth, trashLidHeight);
}

void GlyphPainter::paintUser(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                             juce::Colour const colour)
{
    auto const ink(centred(bounds, userBodyWidth, userHeadDiameter + userGap + userBodyHeight));

    graphics.setColour(colour);
    graphics.fillEllipse(ink.getCentreX() - userHeadDiameter / 2, ink.getY(), userHeadDiameter,
                         userHeadDiameter);
    graphics.fillRoundedRectangle(ink.getX(), ink.getBottom() - userBodyHeight, userBodyWidth,
                                  userBodyHeight, userBodyRadius);
}

void GlyphPainter::paintJog(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                            bool const pointsRight, juce::Colour const colour)
{
    auto const ink(centred(bounds, jogTriangle, jogTriangle));

    auto const apex(pointsRight ? ink.getRight() : ink.getX());
    auto const base(pointsRight ? ink.getX() : ink.getRight());

    juce::Path triangle;
    triangle.startNewSubPath(apex, ink.getCentreY());
    triangle.lineTo(base, ink.getY());
    triangle.lineTo(base, ink.getBottom());
    triangle.closeSubPath();

    graphics.setColour(colour);
    graphics.fillPath(triangle);
}

////////////////////////////////////////////////////////////////////////////////
//
// GlyphPainter::paintUndoArrow()
// -----------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The head decides the layout rather than the bar does: it is centred on
/// the top bar and is wider than the bar is thick, so where that bar can sit is
/// set by half the head's width, and the bend's radius is whatever is left
/// between the two bars.
///
/// \note Redo is undo through a mirror rather than a second drawing. A pair that
/// had drifted apart would read as a bug rather than as a choice, and the two
/// are the same arrow.
///
////////////////////////////////////////////////////////////////////////////////

void GlyphPainter::paintUndoArrow(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                                  bool const pointsLeft, juce::Colour const colour)
{
    auto const ink(centred(bounds, undoWidth, undoHeight));

    auto const topLine(ink.getY() + undoHeadWidth / 2);
    auto const bottomLine(ink.getBottom() - undoStroke / 2);
    auto const radius((bottomLine - topLine) / 2);

    auto const headBase(ink.getX() + undoHeadLength);
    auto const bendX(ink.getRight() - radius);

    //   Up and round from the bottom of the bend, then back along the top. One
    // subpath, so the bend meets the bar without a join to antialias twice.
    juce::Path turn;
    turn.addCentredArc(bendX, (topLine + bottomLine) / 2, radius, radius, 0.0f,
                       juce::MathConstants<float>::pi, 0.0f, true /*starts the subpath*/);
    turn.lineTo(headBase, topLine);

    juce::Path head;
    head.startNewSubPath(ink.getX(), topLine);
    head.lineTo(headBase, topLine - undoHeadWidth / 2);
    head.lineTo(headBase, topLine + undoHeadWidth / 2);
    head.closeSubPath();

    auto const mirrored(
        pointsLeft ? juce::AffineTransform()
                   : juce::AffineTransform::scale(-1.0f, 1.0f, ink.getCentreX(), ink.getCentreY()));

    graphics.setColour(colour);
    graphics.strokePath(
        turn,
        juce::PathStrokeType(undoStroke, juce::PathStrokeType::curved, juce::PathStrokeType::butt),
        mirrored);
    graphics.fillPath(head, mirrored);
}

////////////////////////////////////////////////////////////////////////////////
//
// GlyphPainter::paintFolder()
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note Body and tab as one path rather than two overlapping rounded
/// rectangles: they are drawn in one colour at one alpha, and two shapes sharing
/// an edge antialias against the ground twice over -- which shows as a seam
/// down a mark nine pixels tall.
///
////////////////////////////////////////////////////////////////////////////////

void GlyphPainter::paintFolder(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                               juce::Colour const colour)
{
    auto const ink(centred(bounds, folderWidth, folderHeight));

    juce::Path folder;
    folder.addRoundedRectangle(ink.getX(), ink.getY(), folderTabWidth,
                               folderTabRise + 2 * folderCornerRadius, folderCornerRadius,
                               folderCornerRadius, true /*top left*/, true /*top right*/,
                               false /*bottom left*/, false /*bottom right*/);
    folder.addRoundedRectangle(ink.getX(), ink.getY() + folderTabRise, folderWidth,
                               folderHeight - folderTabRise, folderCornerRadius);

    graphics.setColour(colour);
    graphics.fillPath(folder);
}

void GlyphPainter::paintLock(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                             juce::Colour const colour)
{
    auto const ink(centred(bounds, lockWidth, lockHeight));
    auto const bodyTop(ink.getBottom() - lockBodyHeight);

    /// The shackle's wire is stroked, so its path is the line down the middle of
    /// it: half a stroke inside the ink at the top, and a radius that is half
    /// the span between the two legs.
    auto const radius(lockShackleWidth / 2);
    auto const arcCentreY(ink.getY() + lockShackleStroke / 2 + radius);

    juce::Path shackle;
    shackle.startNewSubPath(ink.getCentreX() - radius, bodyTop);
    shackle.lineTo(ink.getCentreX() - radius, arcCentreY);
    shackle.addCentredArc(ink.getCentreX(), arcCentreY, radius, radius, 0.0f,
                          -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
    shackle.lineTo(ink.getCentreX() + radius, bodyTop);

    graphics.setColour(colour);
    graphics.strokePath(shackle, juce::PathStrokeType(lockShackleStroke));
    graphics.fillRoundedRectangle(ink.getX(), bodyTop, lockWidth, lockBodyHeight, lockCornerRadius);
}

} // namespace LE::SW::GUI
