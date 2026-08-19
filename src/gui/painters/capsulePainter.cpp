////////////////////////////////////////////////////////////////////////////////
///
/// \file capsulePainter.cpp
/// ------------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/painters/capsulePainter.hpp"

#include "gui/colourMap.hpp"

namespace LE::SW::GUI
{

void CapsulePainter::paint(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                           CapsuleStyle const &style, bool const lit)
{
    auto const capsule(juce::Rectangle<float>(0, 0, style.width, style.height)
                           .withCentre(bounds.getCentre().translated(0.0f, style.verticalOffset)));
    auto const radius(style.height / 2);

    auto const white(ColourMap::getColour(ColourMap::FocusHalo));
    auto const blue(ColourMap::getColour(ColourMap::Accent));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Outermost first, so that every ring is laid over the ones beyond it
    /// and the light falls off with how many cover a pixel rather than with any
    /// one of their alphas. \see CapsuleStyle.
    ///
    ////////////////////////////////////////////////////////////////////////////
    /// The rings against the rim, which carry the whole of the brightness.
    unsigned int constexpr hotRings{3};

    auto const reach(lit ? style.litGlowReach : style.darkGlowReach);
    for (unsigned int ring(reach); ring >= 1; --ring)
    {
        bool const hot(ring <= hotRings);
        auto const alpha(hot ? (lit ? style.litGlowHotAlpha : style.darkGlowHotAlpha)
                             : (lit ? style.litGlowAlpha : style.darkGlowAlpha));
        auto const colour(!lit ? white : ((hot && style.litHotIsWhite) ? white : blue));
        graphics.setColour(colour.withAlpha(alpha));
        auto const out(static_cast<float>(ring));
        graphics.fillRoundedRectangle(capsule.expanded(out), radius + out);
    }

    if (lit)
        graphics.setColour(blue);
    else
        graphics.setGradientFill(
            juce::ColourGradient(ColourMap::getColour(ColourMap::CapsuleBodyLeft), capsule.getX(),
                                 capsule.getY(), ColourMap::getColour(ColourMap::CapsuleBodyRight),
                                 capsule.getRight(), capsule.getY(), false));
    graphics.fillRoundedRectangle(capsule, radius);

    /// \note Over the body rather than around a hole in it, which is what the
    /// artwork did: the rim's inner edge then blends into the body it is on
    /// instead of sharing an antialiased edge with it. \see the note in
    /// FramePainter, where a fill goes down first and the seam has to be
    /// stepped around instead.
    juce::Path rim;
    rim.setUsingNonZeroWinding(false); // even-odd, so the body shows through
    rim.addRoundedRectangle(capsule, radius);
    rim.addRoundedRectangle(capsule.reduced(style.rimThickness), radius - style.rimThickness);

    graphics.setColour(
        ColourMap::getColour(lit ? ColourMap::CapsuleRimLit : ColourMap::CapsuleRim));
    graphics.fillPath(rim);
}

} // namespace LE::SW::GUI
