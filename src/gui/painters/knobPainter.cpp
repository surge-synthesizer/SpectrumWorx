////////////////////////////////////////////////////////////////////////////////
///
/// \file knobPainter.cpp
/// ---------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/painters/knobPainter.hpp"

#include "gui/colourMap.hpp"

#include "le/utility/assert.hpp"

namespace LE::SW::GUI
{

juce::ColourGradient KnobPainter::radialAbout(juce::Point<float> const centre, float const radius,
                                              juce::Colour const inner, juce::Colour const outer)
{
    return juce::ColourGradient(inner, centre, outer, centre.translated(radius, 0), true);
}

void KnobPainter::fillRing(juce::Graphics &graphics, juce::Point<float> const centre,
                           float const inner, float const outer,
                           juce::ColourGradient const &gradient)
{
    LE_ASSERT(inner < outer);

    juce::Path ring;
    ring.setUsingNonZeroWinding(false); // even-odd, so the inner disc is a hole
    ring.addEllipse(centre.x - outer, centre.y - outer, 2 * outer, 2 * outer);
    ring.addEllipse(centre.x - inner, centre.y - inner, 2 * inner, 2 * inner);

    graphics.setGradientFill(gradient);
    graphics.fillPath(ring);
}

////////////////////////////////////////////////////////////////////////////////
//
// KnobPainter::paintDome()
// ------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note A radial juce::ColourGradient runs from its first point outwards to its
/// second, so the centre colour is repeated at \p flatRadius to hold it flat
/// under the cap and start the ramp where the artwork's did.
///
////////////////////////////////////////////////////////////////////////////////

void KnobPainter::paintDome(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                            float const flatRadius)
{
    LE_ASSERT(bounds.getWidth() == bounds.getHeight());

    auto const centre(ColourMap::getColour(ColourMap::ModuleKnobDomeCentre));
    auto dome(radialAbout(bounds.getCentre(), bounds.getWidth() / 2, centre,
                          ColourMap::getColour(ColourMap::ModuleKnobDomeRim)));
    dome.addColour(flatRadius, centre);
    graphics.setGradientFill(dome);
    graphics.fillEllipse(bounds);
}

void KnobPainter::paintDomeRim(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                               float const thickness)
{
    graphics.setColour(ColourMap::getColour(ColourMap::ModuleKnobDomeRim));
    graphics.drawEllipse(bounds.reduced(thickness / 2), thickness);
}

void KnobPainter::paintCap(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                           float const solidRadius, float const edgeRadius,
                           juce::Colour const colour)
{
    auto const radius(bounds.getWidth() / 2);
    auto const disc([&](float const r) { return bounds.withSizeKeepingCentre(2 * r, 2 * r); });

    if (edgeRadius <= solidRadius)
    {
        graphics.setColour(colour);
        graphics.fillEllipse(disc(solidRadius * radius));
        return;
    }

    auto cap(radialAbout(bounds.getCentre(), edgeRadius * radius, colour, colour.withAlpha(0.0f)));
    cap.addColour(solidRadius / edgeRadius, colour);
    graphics.setGradientFill(cap);
    graphics.fillEllipse(disc(edgeRadius * radius));
}

void KnobPainter::paintFocusRing(juce::Graphics &graphics, juce::Point<float> const centre,
                                 float const radius)
{
    auto const edge(ColourMap::getColour(ColourMap::FocusHalo));
    auto const reach(radius + focusGlow);
    auto halo(radialAbout(centre, reach, edge.withAlpha(0.0f), edge.withAlpha(0.0f)));
    halo.addColour(radius / reach, edge);
    fillRing(graphics, centre, radius - focusGlow, reach, halo);
}

} // namespace LE::SW::GUI
