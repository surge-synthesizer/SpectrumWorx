////////////////////////////////////////////////////////////////////////////////
///
/// \file arrowPainter.cpp
/// ----------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/painters/arrowPainter.hpp"

#include "gui/colourMap.hpp"

namespace LE::SW::GUI
{

namespace
{
juce::Path arrow(juce::Rectangle<float> const bounds)
{
    auto const base(bounds.getX() + ArrowStyle::baseInset * bounds.getWidth());

    juce::Path triangle;
    triangle.startNewSubPath(base, bounds.getY());
    triangle.lineTo(bounds.getRight(), bounds.getCentreY());
    triangle.lineTo(base, bounds.getBottom());
    triangle.closeSubPath();
    return triangle;
}
} // anonymous namespace

void ArrowPainter::paint(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                         bool const fadeFromBase)
{
    auto const base(bounds.getX() + ArrowStyle::baseInset * bounds.getWidth());
    auto const blue(ColourMap::getColour(ColourMap::Blue));

    if (fadeFromBase)
        graphics.setGradientFill(juce::ColourGradient(blue.withAlpha(0.0f), base, bounds.getY(),
                                                      blue, bounds.getRight(), bounds.getY(),
                                                      false));
    else
        graphics.setColour(blue);

    graphics.fillPath(arrow(bounds));
}

void ArrowPainter::tint(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                        juce::Colour const colour)
{
    graphics.setColour(colour);
    graphics.fillPath(arrow(bounds));
}

} // namespace LE::SW::GUI
