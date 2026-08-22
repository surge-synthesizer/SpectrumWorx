////////////////////////////////////////////////////////////////////////////////
///
/// \file moduleStripPainter.hpp
/// ----------------------------
///
///   The frame a module's controls sit in, which is one FrameStyle and a call.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef moduleStripPainter_hpp__B6D5210F_9C34_4E8A_A017_58D2E6B4739C
#define moduleStripPainter_hpp__B6D5210F_9C34_4E8A_A017_58D2E6B4739C
//------------------------------------------------------------------------------
#include "gui/painters/framePainter.hpp"
#include "gui/painters/ruleStyle.hpp"

#include <juce_graphics/juce_graphics.h>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The frame a module's controls sit in.
///
///   Skin files 55 and 56 until 18.08.2026, and between them 26 KB of vector
/// describing a rounded rectangle: a blue hairline around a dark fill, plus
/// three rings of white outside it for the one strip that has the focus.
///
/// \note The rectangle is half a pixel below the middle of the widget -- top
/// inset 3.21, bottom 2.19 -- which is where the artwork had it, and there is
/// no reason to move it while everything else in the strip is placed from the
/// same drawing. \see framePainter.hpp, which is this drawing and three others.
///
////////////////////////////////////////////////////////////////////////////////

FrameStyle constexpr moduleStripFrame{
    /* insets */ 2.595f,
    4.815f,
    3.285f,
    /* corner */ 12.6f,
    /* rim    */ RuleStyle::thickness,
    /* halo   */ 5u,
    0.1709f,
    0.0235f,
};

/// \brief Draws the frame a module's controls sit in, into \p bounds.
///
/// \param selected whether this is the strip whose controls the editor is
/// showing, which is the halo and nothing else.
///
/// \note `graphics.setOpacity( 0.5f )` stood at the call site and dimmed an
/// unselected strip's frame. It has done nothing since the artwork became a
/// vector -- juce::Drawable::draw() takes an opacity of its own and Artwork
/// passes 1, where drawImageTransformed had honoured the context's -- so no
/// build in months has drawn a faded strip, and reviving it here would be a
/// change of appearance rather than a port. It also would not have been an
/// improvement: the knobs, the name and the rule are children and full strength
/// whatever the frame does, and the halo already says which strip is live.
void paintModuleStrip(juce::Graphics &, juce::Rectangle<float> bounds, bool selected);

} // namespace LE::SW::GUI

#endif // moduleStripPainter_hpp
