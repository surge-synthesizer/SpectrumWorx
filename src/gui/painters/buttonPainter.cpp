////////////////////////////////////////////////////////////////////////////////
///
/// \file buttonPainter.cpp
/// -----------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/painters/buttonPainter.hpp"

#include "gui/colourMap.hpp"
#include "gui/resources.hpp"

#include <cmath>

namespace LE::SW::GUI
{

namespace
{
using namespace ButtonStyle;

/// \brief The pill inside the widget's \p bounds.
///
/// \note A tab's pill reaches the bottom edge and stops short of the top, which
/// is what makes the row of them read as one strip with the page: the gap
/// between two of them is the page showing through, and there is no gap at all
/// where they meet it.
juce::Rectangle<float> pillWithin(juce::Rectangle<float> const bounds,
                                  ButtonPainter::Shape const shape)
{
    if (shape == ButtonPainter::Rectangular)
        return bounds.reduced(glowReach);

    return {bounds.getX() + tabSideInset, bounds.getY() + tabTopInset,
            bounds.getWidth() - 2 * tabSideInset, bounds.getHeight() - tabTopInset};
}

float radiusFor(ButtonPainter::Shape const shape)
{
    return (shape == ButtonPainter::Rectangular) ? rectangularRadius : tabRadius;
}

/// \brief The vertical ramp a button's face is filled with.
///
/// \note \p ease is 1 for a straight interpolation, which is what both grey
/// ramps are; only the selected tab's blue is curved. \see
/// ButtonStyle::selectedTabEase.
juce::ColourGradient face(juce::Rectangle<float> const pill, juce::Colour const top,
                          juce::Colour const bottom, float const ease,
                          unsigned int const intermediateStops)
{
    juce::ColourGradient ramp(top, pill.getX(), pill.getY(), bottom, pill.getX(), pill.getBottom(),
                              false);

    for (unsigned int stop(1); stop <= intermediateStops; ++stop)
    {
        auto const along(static_cast<float>(stop) / (intermediateStops + 1));
        ramp.addColour(along, top.interpolatedWith(bottom, std::pow(along, ease)));
    }

    return ramp;
}

void paintGlow(juce::Graphics &graphics, juce::Rectangle<float> const pill, float const radius)
{
    auto const white(ColourMap::getColour(ColourMap::FocusHalo));

    // Outermost first, so that each ring is laid over the fainter one beyond it
    // and what is left showing is the difference between them.
    for (unsigned int ring(glowRings); ring >= 1; --ring)
    {
        auto const outwards(static_cast<float>(ring - 1) / (glowRings - 1));
        graphics.setColour(
            white.withAlpha(glowInnerAlpha + (glowOuterAlpha - glowInnerAlpha) * outwards));
        auto const reach(static_cast<float>(ring));
        graphics.fillRoundedRectangle(pill.expanded(reach), radius + reach);
    }
}
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
//
// ButtonPainter::paint()
// ----------------------
//
////////////////////////////////////////////////////////////////////////////////

void ButtonPainter::paint(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                          Shape const shape, bool const selected, juce::String const &text)
{
    auto const pill(pillWithin(bounds, shape));
    auto const radius(radiusFor(shape));

    /// \note A tab paints no ground of its own. What shows in the notch between
    /// two pills, and under the rounded corners at the bottom of one, is the
    /// strip they stand on -- which the settings panel draws in one piece so
    /// that it has the page's corners and the page's width.
    /// \see PanelPainter::paintTabBar().
    if ((shape == Rectangular) && selected)
        paintGlow(graphics, pill, radius);

    if ((shape == Tab) && selected)
        graphics.setGradientFill(face(pill, ColourMap::getColour(ColourMap::Accent),
                                      ColourMap::getColour(ColourMap::TabFaceBottom),
                                      selectedTabEase, selectedTabStops));
    else if (shape == Tab)
        graphics.setGradientFill(face(pill, ColourMap::getColour(ColourMap::TabFaceTop),
                                      ColourMap::getColour(ColourMap::TabFaceBottom), 1.0f, 0));
    else
        graphics.setGradientFill(face(pill, ColourMap::getColour(ColourMap::ButtonFaceTop),
                                      ColourMap::getColour(ColourMap::ButtonFaceBottom), 1.0f, 0));

    graphics.fillRoundedRectangle(pill, radius);

    if ((shape == Rectangular) && selected)
    {
        graphics.setColour(ColourMap::getColour(ColourMap::Accent));
        graphics.drawRoundedRectangle(pill, radius, rimThickness);
    }

    graphics.setColour(ColourMap::getColour(ColourMap::ButtonCaption));
    graphics.setFont(font());
    graphics.drawText(text, pill.translated(0, -captionRise), juce::Justification::centred, false);
}

int ButtonPainter::widthFor(juce::String const &text, Shape const shape)
{
    auto const caption(juce::GlyphArrangement::getStringWidthInt(font(), text));
    auto const margin((shape == Rectangular) ? (captionPadding + glowReach)
                                             : (captionPadding + tabSideInset));
    return caption + 2 * juce::roundToInt(margin);
}

juce::Font ButtonPainter::font()
{
    return juce::Font(juce::FontOptions(boldTypeface()).withHeight(captionHeight));
}

} // namespace LE::SW::GUI
