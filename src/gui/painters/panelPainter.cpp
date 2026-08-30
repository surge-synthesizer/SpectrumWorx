////////////////////////////////////////////////////////////////////////////////
///
/// \file panelPainter.cpp
/// ----------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/painters/panelPainter.hpp"

#include "gui/painters/ruleStyle.hpp"

#include "gui/colourMap.hpp"

namespace LE::SW::GUI
{

namespace
{
/// Every frame in both panels was fitted at this, and drawn in flat white.
/// \see RuleStyle::thickness -- these are the same hairline as the chassis'.
float constexpr frameThickness{RuleStyle::thickness};

/// \brief One hairline rounded outline, with nothing inside it.
///
/// \note Stroked rather than filled as an annulus, unlike FramePainter's rim:
/// there is no fill under these to share an antialiased edge with, so the seam
/// the annulus exists to avoid cannot arise. \p bounds is the *outside* of the
/// stroke, as the artwork measured it, so it is inset by half the width.
void outline(juce::Graphics &graphics, juce::Rectangle<float> const bounds, float const radius)
{
    graphics.drawRoundedRectangle(bounds.reduced(frameThickness / 2), radius - frameThickness / 2,
                                  frameThickness);
}

} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
//
// PanelPainter::paintPresetBrowser()
// ----------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The five frames are, top to bottom: where the browser says which
/// folder it is in, the row Save / Save as / Delete sit in, the list, the
/// comment box under it, and the byline under that. The sixth shape is the
/// right-hand cap of another capsule inside the first, which is what divides the
/// folder name from the button that changes it -- an arc and nothing else, so it
/// is drawn by clipping the capsule it belongs to down to its own end.
///
////////////////////////////////////////////////////////////////////////////////

void PanelPainter::paintPresetBrowser(juce::Graphics &graphics, juce::Rectangle<float> const bounds)
{
    juce::Graphics::ScopedSaveState const state(graphics);
    graphics.addTransform(juce::AffineTransform::translation(bounds.getX(), bounds.getY()));

    graphics.setColour(ColourMap::getColour(ColourMap::PanelBackground));
    graphics.fillRoundedRectangle({0.f, 2.f, 287.0f, 541.f}, cornerRadius);

    graphics.setColour(ColourMap::getColour(ColourMap::PanelFrame));

    juce::Rectangle<float> const location{10.125f, 12.735f, 265.815f, 22.53f};
    outline(graphics, location, 11.265f);
    {
        ///   The divider: the same capsule stopping short, with everything but
        /// its right-hand end clipped away.
        ///
        /// \note Two pixels further left than the artwork had it. The arc and
        /// the capsule's own end had 15.5 px between them and the arrow that
        /// goes there is 13 wide, which reads as the arrow being wedged in
        /// rather than sitting in a slot. \see issue #134.
        juce::Graphics::ScopedSaveState const clipped(graphics);
        graphics.reduceClipRegion({247, 12, 12, 26});
        outline(graphics, location.withRight(258.43f), 11.265f);
    }

    outline(graphics, {fieldInset, 42.f, 267.f, 41.f}, cornerRadius);   // the button row
    outline(graphics, {fieldInset, 114.f, 267.f, 324.f}, cornerRadius); // the list
    outline(graphics, {fieldInset, 446.f, 267.f, 47.f}, cornerRadius);  // the comment box
    outline(graphics, {fieldInset, authorFieldTop, 267.f, authorFieldHeight}, cornerRadius);
}

////////////////////////////////////////////////////////////////////////////////
//
// PanelPainter::paintSettingsPage()
// ---------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note Four one-pixel bars ran along the top edge of 17.svg and are not here.
/// They were `#2a2728` and `#22282c` against the panel's own `#1a171b` -- a lift
/// of sixteen parts in 255 over a single row -- and their four x ranges line up
/// with the first two tab pills and then with nothing at all: the third runs
/// half again past its tab and the fourth sits where there is no tab. Whatever
/// they were measured from, it was not this tab bar.
///
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// PanelPainter::paintTabStrip()
// -----------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note What is behind a tab is not one thing: at its shoulder it is the
/// gutter the panel opens into and at its foot it is the page, so the strip
/// starts in the first colour and is in the second by the time it reaches the
/// page. Drawn as one ramp rather than as a corner, which is why neither shape
/// is rounded where they meet.
///
////////////////////////////////////////////////////////////////////////////////

/// \note Deeper than the tab bar, so that it runs behind the top of the page
/// rather than up against it: two rounded rectangles meeting on a line would
/// show that line.
void PanelPainter::paintTabStrip(juce::Graphics &graphics, juce::Rectangle<float> const bounds)
{
    graphics.setColour(ColourMap::getColour(ColourMap::PanelBackground));
    graphics.fillRoundedRectangle(bounds.withY(2.f).withHeight(40.f), cornerRadius);
}

void PanelPainter::paintSettingsPage(juce::Graphics &graphics, juce::Rectangle<float> const bounds)
{
    juce::Graphics::ScopedSaveState const state(graphics);
    graphics.addTransform(juce::AffineTransform::translation(bounds.getX(), bounds.getY()));

    /// \note Square along its top left, where the tab strip stands on it and
    /// covers it. \see paintTabStrip().
    juce::Path page;
    page.addRoundedRectangle(0.f, 0.f, width, 519.f, cornerRadius, cornerRadius, false /*top left*/,
                             true /*top right*/, true /*bottom left*/, true /*bottom right*/);
    graphics.setColour(ColourMap::getColour(ColourMap::PanelBackground));
    graphics.fillPath(page);

    graphics.setColour(ColourMap::getColour(ColourMap::PanelFrame));
    outline(graphics, {fieldInset, settingsFrameTop, 267.f, 487.f}, cornerRadius);
}

} // namespace LE::SW::GUI
