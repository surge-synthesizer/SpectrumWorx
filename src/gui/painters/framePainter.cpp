////////////////////////////////////////////////////////////////////////////////
///
/// \file framePainter.cpp
/// ----------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/painters/framePainter.hpp"

#include "gui/colourMap.hpp"

namespace LE::SW::GUI
{

namespace
{
/// \brief How far the fill reaches under the rim, so that the two do not share
/// an antialiased edge.
///
/// \note Half coverage over half coverage composites to three quarters, not to
/// one, and on a rim only a pixel wide that seam eats half of it -- which
/// measures, against the artwork it replaced, as a rim at half strength. Same
/// phenomenon as the note on paintEditorKnob()'s rim.
float constexpr seamOverlap{0.75f};
} // anonymous namespace

juce::Rectangle<float> FramePainter::rimWithin(juce::Rectangle<float> const bounds,
                                               FrameStyle const &style)
{
    return {bounds.getX() + style.sideInset, bounds.getY() + style.topInset,
            bounds.getWidth() - 2 * style.sideInset,
            bounds.getHeight() - style.topInset - style.bottomInset};
}

////////////////////////////////////////////////////////////////////////////////
//
// FramePainter::paint()
// ---------------------
//
////////////////////////////////////////////////////////////////////////////////

void FramePainter::paint(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                         FrameStyle const &style, juce::Colour const rim, juce::Colour const fill,
                         bool const halo)
{
    auto const frame(rimWithin(bounds, style));
    auto const inside(frame.reduced(style.rimThickness));
    auto const insideRadius(style.cornerRadius - style.rimThickness);

    if (halo && (style.glowRings > 0))
    {
        //   Outermost first, each ring covered in turn by the brighter one
        // inside it, so what shows is the difference between them.
        auto const white(ColourMap::getColour(ColourMap::FocusHalo));
        for (unsigned int ring(style.glowRings); ring >= 1; --ring)
        {
            auto const outwards(
                style.glowRings > 1 ? static_cast<float>(ring - 1) / (style.glowRings - 1) : 0.0f);
            graphics.setColour(white.withAlpha(
                style.glowInnerAlpha + (style.glowOuterAlpha - style.glowInnerAlpha) * outwards));
            auto const reach(static_cast<float>(ring));
            graphics.fillRoundedRectangle(frame.expanded(reach), style.cornerRadius + reach);
        }
    }

    graphics.setColour(fill);
    graphics.fillRoundedRectangle(inside.expanded(seamOverlap), insideRadius + seamOverlap);

    /// \note An annulus rather than a filled rectangle with a smaller one over
    /// it, which is what the artwork was: nothing here is translucent, so
    /// nothing is gained by painting the middle twice. Same idiom as
    /// KnobStyle::fillRing.
    juce::Path outline;
    outline.setUsingNonZeroWinding(false); // even-odd, so the inside is a hole
    outline.addRoundedRectangle(frame, style.cornerRadius);
    outline.addRoundedRectangle(inside, insideRadius);

    graphics.setColour(rim);
    graphics.fillPath(outline);
}

} // namespace LE::SW::GUI
