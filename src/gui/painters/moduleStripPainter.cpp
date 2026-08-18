////////////////////////////////////////////////////////////////////////////////
///
/// \file moduleStripPainter.cpp
/// ----------------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/painters/moduleStripPainter.hpp"

#include "gui/colourMap.hpp"

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
//
// paintModuleStrip()
// ------------------
//
////////////////////////////////////////////////////////////////////////////////

void paintModuleStrip(juce::Graphics &graphics, juce::Rectangle<float> const bounds,
                      bool const selected)
{
    FramePainter::paint(graphics, bounds, moduleStripFrame, ColourMap::getColour(ColourMap::Blue),
                        ColourMap::getColour(ColourMap::ModuleBackground), selected);
}

} // namespace LE::SW::GUI
