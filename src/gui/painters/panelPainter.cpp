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

#include "gui/colourMap.hpp"

namespace LE::SW::GUI
{

namespace
{
/// Every frame in both panels was fitted at this, and drawn in flat white.
float constexpr frameThickness{1.02f};

/// \brief The strip the tabs stand in: how far below the top of the tab row its
/// ground starts, how far below that the ramp into the page's black is done,
/// and how far past the row it runs so that the two share no seam.
///@{
float constexpr stripRise{5.0f};
float constexpr stripFade{7.0f};
float constexpr stripOverlap{1.0f};
///@}

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
/// \note The four frames are, top to bottom: where the browser says which
/// folder it is in, the row Save / Save as / Delete sit in, the list, and the
/// comment box under it. The fifth shape is the right-hand cap of a *fifth*
/// capsule inside the first, which is what divides the folder name from the
/// button that changes it -- an arc and nothing else, so it is drawn by
/// clipping the capsule it belongs to down to its own end.
///
////////////////////////////////////////////////////////////////////////////////

void PanelPainter::paintPresetBrowser(juce::Graphics &graphics, juce::Rectangle<float> const bounds)
{
    juce::Graphics::ScopedSaveState const state(graphics);
    graphics.addTransform(juce::AffineTransform::translation(bounds.getX(), bounds.getY()));

    graphics.setColour(ColourMap::getColour(ColourMap::PanelBackground));
    graphics.fillRoundedRectangle({0.75f, 0.5f, 190.0f, 361.25f}, 8.05f);

    graphics.setColour(ColourMap::getColour(ColourMap::PanelFrame));

    juce::Rectangle<float> const location{6.75f, 8.49f, 177.21f, 15.02f};
    outline(graphics, location, 7.51f);
    {
        //   The divider: the same capsule stopping short, with everything but
        // its right-hand end clipped away.
        juce::Graphics::ScopedSaveState const clipped(graphics);
        graphics.reduceClipRegion({166, 8, 8, 17});
        outline(graphics, location.withRight(173.62f), 7.51f);
    }

    outline(graphics, {6.75f, 26.75f, 177.25f, 29.0f}, 8.78f);  // the button row
    outline(graphics, {6.75f, 76.0f, 177.25f, 241.25f}, 8.89f); // the list
    outline(graphics, {6.75f, 322.0f, 177.25f, 31.75f}, 8.69f); // the comment box
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
///                                       (18.08.2026.)
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

void PanelPainter::paintTabStrip(juce::Graphics &graphics, juce::Rectangle<float> const bounds)
{
    auto const top(bounds.getY() + stripRise);

    graphics.setGradientFill(juce::ColourGradient::vertical(
        ColourMap::getColour(ColourMap::EditorSurround), top,
        ColourMap::getColour(ColourMap::PanelBackground), top + stripFade));
    graphics.fillRect(juce::Rectangle<float>(bounds.getX() + sideInset, top,
                                             bounds.getWidth() - 2 * sideInset,
                                             bounds.getHeight() - stripRise + stripOverlap));
}

void PanelPainter::paintSettingsPage(juce::Graphics &graphics, juce::Rectangle<float> const bounds)
{
    juce::Graphics::ScopedSaveState const state(graphics);
    graphics.addTransform(juce::AffineTransform::translation(bounds.getX(), bounds.getY()));

    /// \note Square along its top left, where the tab strip stands on it and
    /// covers it. \see paintTabStrip().
    juce::Path page;
    page.addRoundedRectangle(sideInset, 0.0f, 190.0f, 346.5f, cornerRadius, cornerRadius,
                             false /*top left*/, true /*top right*/, true /*bottom left*/,
                             true /*bottom right*/);
    graphics.setColour(ColourMap::getColour(ColourMap::PanelBackground));
    graphics.fillPath(page);

    graphics.setColour(ColourMap::getColour(ColourMap::PanelFrame));
    outline(graphics, {7.25f, 11.5f, 177.25f, 326.0f}, 8.31f);
}

} // namespace LE::SW::GUI
