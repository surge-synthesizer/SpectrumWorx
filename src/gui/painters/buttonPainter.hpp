////////////////////////////////////////////////////////////////////////////////
///
/// \file buttonPainter.hpp
/// -----------------------
///
///   The skin's one button, drawn rather than blitted.
///
///   Fourteen skin files were this shape: a rounded pill filled with a vertical
/// ramp, a dark caption on it, and some way of saying it is the one that is on.
/// Four for Presets and Settings (08 to 11), six for the settings tabs (21 to
/// 24, 27, 28) and six for the preset browser's Save, Save as and Delete (30 to
/// 35). Two drawings, then, at four sizes -- and every caption baked in, so
/// renaming a tab meant redrawing it.
///
///   The two differ in where the pill sits and in what "selected" looks like. A
/// **rectangular** button floats in its own rectangle and says it is on by
/// lighting up: the skin's blue around the rim, and a soft halo outside it. A
/// **tab** runs down into the page below it -- flush at the bottom, rounded at
/// the top, on a strip of the page's own dark -- and says it is on by filling
/// with blue instead of grey. Neither reads correctly as the other, which is why
/// there are two rather than a flag.
///
/// \note Numbers below are in skin pixels rather than fractions, unlike
/// ModuleKnobStyle and EditorKnobStyle. A knob is one drawing at whatever size
/// it is asked for; these are drawn at four widths and two heights and the
/// corner radius, the caption and the halo are the same in all of them -- a
/// tab whose corners grew with its caption would be a different tab.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef buttonPainter_hpp__6C1B0F24_7A85_4E30_9D6B_1F0A4C52E7B8
#define buttonPainter_hpp__6C1B0F24_7A85_4E30_9D6B_1F0A4C52E7B8
//------------------------------------------------------------------------------
#include "gui/painters/ruleStyle.hpp"

#include <juce_graphics/juce_graphics.h>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \namespace ButtonStyle
///
////////////////////////////////////////////////////////////////////////////////

/// \brief Everything a button is drawn from that is not a colour.
///
/// \see ColourMap for the colours -- Button*, Tab*, Accent and FocusHalo.
namespace ButtonStyle
{
////////////////////////////////////////////////////////////////////////////////
/// \name The rectangular button
////////////////////////////////////////////////////////////////////////////////
///@{
/// \brief The room the pill leaves itself inside the widget, for the halo.
///
/// \note Which is what made 08.svg a 57x24 canvas holding a 50x17 pill. A
/// button that fills its own bounds has nowhere to put a glow, so this is the
/// difference between the size a caller asks for and the size that gets drawn.
float constexpr glowReach{5.f};

float constexpr rectangularRadius{8.f};
float constexpr rimThickness{RuleStyle::thickness}; ///< the blue line on a lit button's edge

/// \brief The halo: \p glowRings rounded rectangles, each a pixel further out
/// and fainter than the last, laid down before the pill covers their middles.
///
/// \note Stacked rather than blended into one gradient, which is what the
/// artwork did and what makes the falloff read as light rather than as banding.
/// The outermost ring falls outside the widget and is clipped, in the artwork
/// as here.
unsigned int constexpr glowRings{6};
float constexpr glowInnerAlpha{0.5f}; ///< at the ring against the pill
float constexpr glowOuterAlpha{0.f};  ///< and at the last one
///@}

////////////////////////////////////////////////////////////////////////////////
/// \name The tab
////////////////////////////////////////////////////////////////////////////////
///@{
/// \note What separates one tab's pill from the next, and the artwork's 0.5 at
/// the size the skin is drawn at now. It is also what the settings panel takes
/// off PanelPainter::fieldInset when it places the bar, so that the leftmost
/// pill's edge and the frame below it stand on one line. \see
/// SpectrumWorxEditor::Settings::resized().
float constexpr tabSideInset{1.f};
float constexpr tabTopInset{1.f};
float constexpr tabRadius{8.f};

/// \brief The depth of the settings panel's tab bar.
///
/// \note Was `resourceArtwork<SettingsEngineOn>().getHeight()`, asked in two
/// places -- the bar's depth and the panel's overall height. \see
/// SpectrumWorxEditor::Settings.
int constexpr tabHeight{24};

/// \brief How the selected tab's blue falls into shadow.
///
///   Not linearly: the artwork's ramp holds the blue three fifths of the way
/// down and then drops away, which a straight interpolation between its ends
/// misses by 44 parts in 255 at the middle -- enough to read as grey. The six
/// stops it was traced with are a plain interpolation whose parameter has been
/// raised to this power, fitted to within two parts in 255.
float constexpr selectedTabEase{1.75f};
unsigned int constexpr selectedTabStops{4};
///@}

////////////////////////////////////////////////////////////////////////////////
/// \name The caption
////////////////////////////////////////////////////////////////////////////////
///@{
/// \note The bold face, and the only thing in the skin that still asks for it
/// besides the About page's title. Issue #76 took the weight off the labels,
/// which are light text on a dark ground; this is the other way round and small
/// with it, and the regular face at ten pixels of dark ink on a lit pill does
/// not hold together. Measured off the artwork, which had it baked in.
float constexpr captionHeight{15.0f};

float constexpr captionPadding{9.0f}; ///< either side of the text, inside the pill

/// \brief What the caption is lifted by, off the pill's middle.
///
/// \note The ramp is bright at the top and black at the bottom, so text centred
/// by arithmetic sits low: there is more dark under it than light over it. The
/// artwork put it here.
float constexpr captionRise{0.75f};
///@}
} // namespace ButtonStyle

////////////////////////////////////////////////////////////////////////////////
///
/// \class ButtonPainter
///
////////////////////////////////////////////////////////////////////////////////

class ButtonPainter
{
  public:
    enum Shape
    {
        Rectangular, ///< a pill floating in its own rectangle
        Tab          ///< the same pill, run down into the page below it
    };

    /// \brief Draws \p text on a button filling \p bounds.
    ///
    /// \param selected whether this is the one that is on -- a lit rim and a
    /// halo for a Rectangular button, a blue face for a Tab.
    static void paint(juce::Graphics &, juce::Rectangle<float> bounds, Shape, bool selected,
                      juce::String const &text);

    /// \brief The width a button of this shape needs to hold \p text.
    ///
    /// \note For a Rectangular button that includes the room the halo wants, so
    /// it is wider than the pill it draws. \see ButtonStyle::glowReach.
    static int widthFor(juce::String const &text, Shape);

    /// The face and size a caption is set in.
    static juce::Font font();

  public:
    ButtonPainter() = delete; // a drawing, not an object
}; // class ButtonPainter

} // namespace LE::SW::GUI

#endif // buttonPainter_hpp
