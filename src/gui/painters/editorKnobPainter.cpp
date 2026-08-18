////////////////////////////////////////////////////////////////////////////////
///
/// \file editorKnobPainter.cpp
/// ---------------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/painters/editorKnobPainter.hpp"

#include "gui/colourMap.hpp"

#include "le/utility/assert.hpp"

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
//
// paintEditorKnob()
// -----------------
//
////////////////////////////////////////////////////////////////////////////////

void paintEditorKnob(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                     float const normalisedValue)
{
    using namespace EditorKnobStyle;

    LE_ASSERT(bounds.getWidth() == bounds.getHeight());

    auto const centre(bounds.getCentre());
    auto const radius(bounds.getWidth() / 2);
    auto const value(juce::jlimit(0.0f, 1.0f, normalisedValue));
    auto const disc([&](float const r) { return bounds.withSizeKeepingCentre(2 * r, 2 * r); });

    /// \note The bevel is lit from above left, so its shading is concentric with
    /// a point below and right of the knob rather than with the knob. Fitting
    /// that offset is what took the residual in this ring from 18/255 to 1/255 --
    /// a concentric gradient cannot describe a lit surface.
    auto const shading(centre.translated(bevelShadingX * radius, bevelShadingY * radius));
    auto const shadow(ColourMap::getColour(ColourMap::EditorKnobBevelShadow));
    auto const rimOfBevel(ColourMap::getColour(ColourMap::EditorKnobBevelRim));
    auto bevel(KnobPainter::radialAbout(shading, bevelReach * radius, shadow, rimOfBevel));
    bevel.addColour(bevelShadowStop, shadow);
    bevel.addColour(bevelMidStop, ColourMap::getColour(ColourMap::EditorKnobBevelMid));
    bevel.addColour(bevelRimStop, rimOfBevel);
    graphics.setGradientFill(bevel);
    graphics.fillEllipse(disc(bevelRadius * radius));

    // The teal ring: a plain vertical ramp, the same grey added to every channel.
    graphics.setGradientFill(
        juce::ColourGradient(ColourMap::getColour(ColourMap::EditorKnobRingTop),
                             centre.translated(0, -ringRadius * radius),
                             ColourMap::getColour(ColourMap::EditorKnobRingBottom),
                             centre.translated(0, ringRadius * radius), false));
    graphics.fillEllipse(disc(ringRadius * radius));

    // The cap, whose edge is soft: it is a shadow on the teal, not a disc on it.
    auto const capFill(ColourMap::getColour(ColourMap::EditorKnobCap));
    auto cap(
        KnobPainter::radialAbout(centre, capRadius * radius, capFill, capFill.withAlpha(0.0f)));
    cap.addColour(capSolidRadius / capRadius, capFill);
    graphics.setGradientFill(cap);
    graphics.fillEllipse(disc(capRadius * radius));

    graphics.setColour(ColourMap::getColour(ColourMap::EditorKnobTick));
    for (unsigned int t(0); t < numberOfTicks; ++t)
    {
        juce::Path mark;
        mark.addRectangle(-tickHalfWidth * radius, -tickOuterRadius * radius,
                          2 * tickHalfWidth * radius, (tickOuterRadius - tickInnerRadius) * radius);
        mark.applyTransform(juce::AffineTransform::rotation(
            juce::degreesToRadians(360.0f * t / numberOfTicks), 0, 0));
        graphics.fillPath(mark, juce::AffineTransform::translation(centre));
    }

    /// \note The rim's inner edge deliberately sits under the bevel rather than
    /// against it. Two antialiased edges meeting exactly leave a seam -- half
    /// coverage over half coverage composites to three quarters, not to one.
    auto const bright(ColourMap::getColour(ColourMap::EditorKnobRim));
    auto rim(KnobPainter::radialAbout(centre, rimHighlightOuter * radius, bright,
                                      bright.withAlpha(0.0f)));
    rim.addColour(rimHighlightSolid / rimHighlightOuter, bright);
    KnobPainter::fillRing(graphics, centre, rimHighlightInner * radius, rimHighlightOuter * radius,
                          rim);

    graphics.setColour(ColourMap::getColour(ColourMap::EditorKnobRimOutline));
    graphics.drawEllipse(disc(rimOutlineRadius * radius), rimOutlineThickness * radius);

    // The pointer, and the only thing here that moves.
    juce::Path bar;
    bar.startNewSubPath(-pointerInnerHalfWidth * radius, -pointerInnerRadius * radius);
    bar.lineTo(-pointerOuterHalfWidth * radius, -pointerOuterRadius * radius);
    bar.lineTo(pointerOuterHalfWidth * radius, -pointerOuterRadius * radius);
    bar.lineTo(pointerInnerHalfWidth * radius, -pointerInnerRadius * radius);
    bar.closeSubPath();
    bar.applyTransform(juce::AffineTransform::rotation(
        juce::degreesToRadians(KnobPainter::angleFor(value)), 0, 0));
    graphics.setColour(ColourMap::getColour(ColourMap::EditorKnobPointer));
    graphics.fillPath(bar, juce::AffineTransform::translation(centre));
}

} // namespace LE::SW::GUI
