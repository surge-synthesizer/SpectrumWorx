////////////////////////////////////////////////////////////////////////////////
///
/// \file ejectPainter.cpp
/// ----------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/painters/ejectPainter.hpp"

#include "gui/colourMap.hpp"

namespace LE::SW::GUI
{

namespace
{
using namespace EjectStyle;

/// \brief The tongue: square along its top, where the strip is, and rounded
/// away to nothing along its bottom.
juce::Path tongue(juce::Rectangle<float> const bounds, float const inset)
{
    auto const shape(
        juce::Rectangle<float>(bounds.getX() + sideInset + inset, bounds.getY() + topInset + inset,
                               bounds.getWidth() - 2 * (sideInset + inset),
                               bounds.getHeight() - topInset - bottomInset - 2 * inset));
    juce::Path path;
    path.addRoundedRectangle(shape.getX(), shape.getY(), shape.getWidth(), shape.getHeight(),
                             shape.getWidth() / 2, footDepth - inset, false /*top left*/,
                             false /*top right*/, true /*bottom left*/, true /*bottom right*/);
    return path;
}
} // anonymous namespace

void EjectPainter::paint(juce::Graphics &graphics, juce::Rectangle<float> const bounds)
{
    graphics.setColour(ColourMap::getColour(ColourMap::Blue));
    graphics.fillPath(tongue(bounds, 0.0f));

    graphics.setColour(ColourMap::getColour(ColourMap::EjectFace));
    graphics.fillPath(tongue(bounds, rimThickness));

    /// \note Round caps, which at two pixels is most of what the cross is: with
    /// butt ends it reads as four spikes rather than as one mark.
    juce::Path cross;
    auto const centre(juce::Point<float>(bounds.getCentreX(), bounds.getY() + crossCentreY));
    cross.startNewSubPath(centre.translated(-crossHalfWidth, -crossHalfHeight));
    cross.lineTo(centre.translated(crossHalfWidth, crossHalfHeight));
    cross.startNewSubPath(centre.translated(crossHalfWidth, -crossHalfHeight));
    cross.lineTo(centre.translated(-crossHalfWidth, crossHalfHeight));

    graphics.setColour(ColourMap::getColour(ColourMap::Blue));
    graphics.strokePath(cross, juce::PathStrokeType(crossThickness, juce::PathStrokeType::mitered,
                                                    juce::PathStrokeType::rounded));
}

void EjectPainter::tint(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                        juce::Colour const colour)
{
    graphics.setColour(colour);
    graphics.fillPath(tongue(bounds, 0.0f));
}

} // namespace LE::SW::GUI
