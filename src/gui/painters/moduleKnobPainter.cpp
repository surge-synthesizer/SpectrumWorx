////////////////////////////////////////////////////////////////////////////////
///
/// \file moduleKnobPainter.cpp
/// ---------------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/painters/moduleKnobPainter.hpp"

#include "gui/colourMap.hpp"

#include "le/utility/assert.hpp"

#include <algorithm>
#include <cmath>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
//
// paintModuleKnob()
// -----------------
//
////////////////////////////////////////////////////////////////////////////////

void paintModuleKnob(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                     float const normalisedValue, bool const bipolar, Highlight const highlight,
                     std::optional<juce::Range<float>> const lfoRange)
{
    using namespace ModuleKnobStyle;

    LE_ASSERT(bounds.getWidth() == bounds.getHeight());

    auto const centre(bounds.getCentre());
    auto const radius(bounds.getWidth() / 2);
    auto const value(juce::jlimit(0.0f, 1.0f, normalisedValue));

    KnobPainter::paintDome(graphics, bounds, innerGradientRadius);

    /// \note How far the wedge has opened, which the cap's radius follows.
    auto const openness(bipolar ? std::abs(2 * value - 1) : value);

    auto const fillArc([&](float const one, float const other, ColourMap::Name const colour) {
        juce::Path pie;
        pie.addPieSegment(
            bounds.withSizeKeepingCentre(2 * radius * wedgeRadius, 2 * radius * wedgeRadius),
            juce::degreesToRadians(std::min(one, other)),
            juce::degreesToRadians(std::max(one, other)), 0.0f);
        graphics.setColour(ColourMap::getColour(colour));
        graphics.fillPath(pie);
    });

    if (lfoRange)
        // between the two bounds, where the value would be from a stop to itself
        fillArc(KnobPainter::angleFor(juce::jlimit(0.0f, 1.0f, lfoRange->getStart())),
                KnobPainter::angleFor(juce::jlimit(0.0f, 1.0f, lfoRange->getEnd())),
                ColourMap::Accent);
    else
        // the far edge is the value; the near one is the stop it opens from
        fillArc(KnobPainter::angleFor(value), bipolar ? 0.0f : -KnobPainter::halfSweepDegrees,
                ColourMap::Accent);

    /// \note And the cap stays shut under a range. It grows with the *value* so
    /// that the accent keeps a roughly even thickness as it lengthens, and a
    /// range has no length to follow -- one that grew with the span would draw a
    /// wide travel thinner than a narrow one, which is backwards.
    auto const cap(lfoRange ? capRadiusLFO
                            : capRadiusClosed + (capRadiusOpen - capRadiusClosed) * openness);
    auto const color(lfoRange ? ColourMap::getColour(ColourMap::ModuleKnobDomeCentre)
                              : ColourMap::getColour(ColourMap::ModuleKnobCap));
    KnobPainter::paintCap(graphics, bounds, cap, cap /*a hard edge*/, color);

    switch (highlight)
    {
    case Highlight::Selected:
        return KnobPainter::paintFocusRing(graphics, centre, radius);
    case Highlight::Hovered:
        return KnobPainter::paintFocusRing(graphics, centre, radius, hoverStrength);
    case Highlight::None:
        return KnobPainter::paintDomeRim(graphics, bounds, rimThickness);
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// paintTriggerButton()
// --------------------
//
////////////////////////////////////////////////////////////////////////////////

void paintTriggerButton(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                        bool const on)
{
    using namespace TriggerButtonStyle;

    KnobPainter::paintDome(graphics, bounds, ModuleKnobStyle::innerGradientRadius);
    KnobPainter::paintCap(graphics, bounds, capRadius, capEdgeRadius,
                          ColourMap::getColour(ColourMap::ModuleKnobCap));
    if (on)
        KnobPainter::paintCap(graphics, bounds, litRadius, litEdgeRadius,
                              ColourMap::getColour(ColourMap::Accent));
    KnobPainter::paintDomeRim(graphics, bounds, ModuleKnobStyle::rimThickness);
}

} // namespace LE::SW::GUI
