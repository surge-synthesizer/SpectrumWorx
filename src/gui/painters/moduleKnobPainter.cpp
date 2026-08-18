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
                     float const normalisedValue, bool const bipolar, bool const drawWedge,
                     bool const selected)
{
    using namespace ModuleKnobStyle;

    LE_ASSERT(bounds.getWidth() == bounds.getHeight());

    auto const centre(bounds.getCentre());
    auto const radius(bounds.getWidth() / 2);
    auto const value(juce::jlimit(0.0f, 1.0f, normalisedValue));

    KnobPainter::paintDome(graphics, bounds, innerGradientRadius);

    /// \note How far the wedge has opened, which the cap's radius follows -- and
    /// so zero when there is no wedge to follow. A cap sized off a value the
    /// knob is not showing is the one thing the LFO state could still leak.
    auto const openness(!drawWedge ? 0.0f : bipolar ? std::abs(2 * value - 1) : value);

    if (drawWedge)
    {
        // The far edge is the value; the near one is the stop it opens from.
        auto const to(KnobPainter::angleFor(value));
        auto const from(bipolar ? 0.0f : -KnobPainter::halfSweepDegrees);
        juce::Path pie;
        pie.addPieSegment(
            bounds.withSizeKeepingCentre(2 * radius * wedgeRadius, 2 * radius * wedgeRadius),
            juce::degreesToRadians(std::min(from, to)), juce::degreesToRadians(std::max(from, to)),
            0.0f);
        graphics.setColour(ColourMap::getColour(ColourMap::Accent));
        graphics.fillPath(pie);
    }

    auto const cap(capRadiusClosed + (capRadiusOpen - capRadiusClosed) * openness);
    KnobPainter::paintCap(graphics, bounds, cap, cap /*a hard edge*/,
                          ColourMap::getColour(ColourMap::ModuleKnobCap));

    if (selected)
        KnobPainter::paintFocusRing(graphics, centre, radius);
    else
        KnobPainter::paintDomeRim(graphics, bounds, rimThickness);
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
