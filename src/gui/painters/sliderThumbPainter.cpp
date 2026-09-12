////////////////////////////////////////////////////////////////////////////////
///
/// \file sliderThumbPainter.cpp
/// ----------------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/painters/sliderThumbPainter.hpp"

#include "gui/colourMap.hpp"

namespace LE::SW::GUI
{

void SliderThumbPainter::paint(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                               float const haloStrength)
{
    using namespace SliderThumbStyle;

    auto const bead(bounds.reduced(sideInset * bounds.getWidth(), endInset * bounds.getHeight()));

    if (haloStrength > 0)
    {
        //   Outermost first, each ring covered in turn by the brighter one
        // inside it, so what shows is the difference between them.
        auto const white(ColourMap::getColour(ColourMap::FocusHalo));
        for (unsigned int ring(glowRings); ring >= 1; --ring)
        {
            auto const outwards(static_cast<float>(ring - 1) / (glowRings - 1));
            graphics.setColour(white.withAlpha(
                haloStrength * (glowInnerAlpha + (glowOuterAlpha - glowInnerAlpha) * outwards)));
            graphics.fillEllipse(bead.expanded(static_cast<float>(ring)));
        }
    }

    juce::ColourGradient lit(ColourMap::getColour(ColourMap::ThumbHighlight), bead.getX(),
                             bead.getY(), ColourMap::getColour(ColourMap::ThumbFoot), bead.getX(),
                             bead.getBottom(), false);
    lit.addColour(faceStop, ColourMap::getColour(ColourMap::ThumbFace));
    lit.addColour(sheenStop, ColourMap::getColour(ColourMap::ThumbSheen));

    graphics.setGradientFill(lit);
    graphics.fillEllipse(bead);

    /// \note An annulus over the bead rather than around a hole in it, so its
    /// inner edge blends into what it is on. \see CapsulePainter, which has the
    /// same rim in the same order and the note that says why.
    auto const thickness(shadowThickness * bounds.getWidth());
    juce::Path shadow;
    shadow.setUsingNonZeroWinding(false); // even-odd, so the bead shows through
    shadow.addEllipse(bead);
    shadow.addEllipse(bead.reduced(thickness));

    auto const black(ColourMap::getColour(ColourMap::ThumbShadow));
    graphics.setGradientFill(juce::ColourGradient(black.withAlpha(shadowLeftAlpha), bead.getX(),
                                                  bead.getY(), black.withAlpha(shadowRightAlpha),
                                                  bead.getRight(), bead.getY(), false));
    graphics.fillPath(shadow);
}

} // namespace LE::SW::GUI
