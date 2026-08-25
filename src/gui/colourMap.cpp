////////////////////////////////////////////////////////////////////////////////
///
/// \file colourMap.cpp
/// -------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "colourMap.hpp"

#include "le/utility/assert.hpp"
#include "le/utility/platformSpecifics.hpp"

namespace LE::SW::GUI
{

namespace
{
ColourMap::Palette current_{ColourMap::ClassicBlue};
std::uint32_t generation_{0};

////////////////////////////////////////////////////////////////////////////////
//
// turned()
// --------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief ClassicBlue, with its hue moved somewhere else.
///
/// \note A colour ClassicBlue left neutral is returned untouched. That is not a
/// special case being handled, it is the whole reason this works: the skin's
/// bevels, rules, ink and shadows have no hue to move, so turning the palette
/// turns exactly the things that carry the accent -- the lit ring on a knob,
/// the steel down a capsule, the wordmark's cold white -- and leaves the
/// modelling alone.
///
/// \note The brightness correction is scaled by how coloured the colour is, for
/// the same reason. A green at the blue's brightness reads far lighter than the
/// blue did and a red far darker -- green weighs 0.691 of perceived brightness
/// against blue's 0.068 -- so each palette carries a correction; applying it
/// flat would drag every grey in the skin with it.
///
////////////////////////////////////////////////////////////////////////////////

juce::Colour turned(juce::Colour const colour, Rotation const &rotation)
{
    auto const saturation(colour.getSaturation());
    if (saturation <= 0)
        return colour;

    return colour.withRotatedHue(rotation.hue)
        .withMultipliedSaturation(rotation.saturation)
        .withMultipliedBrightness(1 + saturation * (rotation.brightness - 1));
}

/// \brief ClassicBlue, with the colour taken out rather than moved.
///
/// \note By perceived brightness rather than by HSB's: the point of a grey
/// scale is that nothing changes weight, and HSB calls a saturated blue and a
/// saturated yellow equally bright.
juce::Colour drained(juce::Colour const colour)
{
    return juce::Colour::greyLevel(colour.getPerceivedBrightness())
        .withAlpha(colour.getFloatAlpha());
}

////////////////////////////////////////////////////////////////////////////////
///
/// \name Where each palette puts the accent
///
///   Classic's is #13B5EA: hue 194.8 degrees, saturation 0.92, and 0.638 of
/// perceived brightness. The hue below each is the turn from there, and the
/// other two numbers are fitted to land back on that same 0.638 -- so the
/// accent keeps the weight the artwork was drawn around, whatever colour it is.
/// Amber cannot quite be muted into place and is left where SCXT has it.
///
///@{
/// To 2 degrees: #FF6C67, perceived 0.613.
Rotation constexpr redRotation{0.4645f, 0.65f, 1.10f};
/// To 135 degrees: #33BF56, perceived 0.636.
Rotation constexpr greenRotation{0.8339f, 0.80f, 0.80f};
/// To 37 degrees, which is SCXT's accent_1a. \see ColourMap::sstDark().
Rotation constexpr amberRotation{0.5615f, 0.78f, 1.05f};
/// To 55 degrees: #CFC237, matched to amberRotation's actual brightness
Rotation constexpr yellowRotation{0.6117f, 0.8000f, 0.9733f};
/// To 280 degrees: #C68CE3, perceived 0.638.
Rotation constexpr purpleRotation{0.2367f, 0.4200f, 0.9692f};
///@}
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
//
// ColourMap::classic()
// --------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note ARGB throughout, including the opaque ones: three of these are
/// deliberately translucent and writing all of them the same way is what keeps
/// that from reading as a typo.
///
////////////////////////////////////////////////////////////////////////////////

juce::Colour ColourMap::classic(Name const name)
{
    switch (name)
    {
    case Accent:
        return juce::Colour(0xFF13B5EAu);
    case Text:
        return juce::Colour(0xFFFFFFFFu);
    case TextDimmed:
        return juce::Colour(0xFFC0C0C0u);
    case TextFaint:
        return juce::Colour(0xFF808080u);

    case EditorGradientStart:
        return juce::Colour(0xFF13B5EAu);
    case EditorKnobBevelShadow:
        return juce::Colour(0xFF7E8A8Eu);
    case EditorKnobBevelMid:
        return juce::Colour(0xFFC4C4C4u);
    case EditorKnobBevelRim:
        return juce::Colour(0xFFFDFDFDu);
    case EditorKnobRingTop:
        return juce::Colour(0xFF75B3C2u);
    case EditorKnobRingBottom:
        return juce::Colour(0xFF094756u);
    case EditorKnobCap:
        return juce::Colour(0xFF000000u);
    case EditorKnobTick:
        return juce::Colour(0xFF000000u);
    case EditorKnobRim:
        return juce::Colour(0xFFF2F2F2u);
    case EditorKnobRimOutline:
        return juce::Colour(0x9D000000u);
    ///   The accent, where the artwork had #161616. A near-black bar over a
    /// black cap and a ring that is nearly black at the bottom is a pointer
    /// that disappears over most of its travel and reappears over the rest,
    /// which is the one thing this mark exists not to do. \see issue #134.
    case EditorKnobPointer:
        return juce::Colour(0xFF000000u);

    case ModuleKnobDomeCentre:
        return juce::Colour(0xFFB8B6B6u);
    case ModuleKnobDomeRim:
        return juce::Colour(0xFF0A0909u);
    case ModuleKnobCap:
        return juce::Colour(0xFF000000u);

    case FocusHalo:
        return juce::Colour(0xFFFFFFFFu);

    case ButtonFaceTop:
        return juce::Colour(0xFFFFFFFFu);
    case ButtonFaceBottom:
        return juce::Colour(0xFF0F0F0Fu);
    case ButtonCaption:
        return juce::Colour(0xFF231F20u);
    case TabFaceTop:
        return juce::Colour(0xFFF7F6F7u);
    case TabFaceBottom:
        return juce::Colour(0xFF231F20u);

    case PanelBackground:
        return juce::Colour(0xFF1A171Bu);
    case PanelFrame:
        return juce::Colour(0xFFFFFFFFu);

    case CapsuleBodyLeft:
        return juce::Colour(0xFF2B2829u);
    case CapsuleBodyRight:
        return juce::Colour(0xFF7EA4B4u);
    case CapsuleRim:
        return juce::Colour(0xFF6D6E70u);
    case CapsuleRimLit:
        return juce::Colour(0xFF3A4E56u);

    case ThumbHighlight:
        return juce::Colour(0xFFBAD0DBu);
    case ThumbFace:
        return juce::Colour(0xFF228BC0u);
    case ThumbSheen:
        return juce::Colour(0xFF448FBEu);
    case ThumbFoot:
        return juce::Colour(0xFF3F7194u);
    case ThumbShadow:
        return juce::Colour(0xFF000000u);

    case EditorSurround:
        return juce::Colour(0xFFE6E6E6u);
    case EditorPanel:
        return juce::Colour(0xFF231F20u);
    case EditorWell:
        return juce::Colour(0xFF1B1919u);
    case EditorWellFace:
        return juce::Colour(0xFF000000u);
    case EditorRule:
        return juce::Colour(0xFFFFFFFFu);
    case Wordmark:
        return juce::Colour(0xFFFFFFFFu);

    case EjectFace:
        return juce::Colour(0xFF6F777Bu);

    ///   White, which is what all eleven files stroked in. \see the note on the
    /// declaration for why this one may not be given a hue.
    case LFOWaveform:
        return juce::Colour(0xFFFFFFFFu);

    case ComboBackground:
        return juce::Colour(0xFF1A1A1Au);
    case ModuleBackground:
        return juce::Colour(0xFF231F20u);

    case MouseOverGlow:
        return juce::Colour(0x80FFFFFFu);
    case MouseOverShade:
        return juce::Colour(0x66555555u);

    case MenuBackground:
        return juce::Colour(0xFF101012u);
    case MenuOutline:
        return juce::Colour(0xFF252535u);

    case Ground:
        return juce::Colour(0xFF000000u);
    case FieldBackground:
        return juce::Colour(0x88000000u);
    case ListBackground:
        return juce::Colour(0x11000000u);
    case ListOutline:
        return juce::Colour(0xAA000000u);
    case ListHighlight:
        return juce::Colour(0x40FFFFFFu);
    case SliderTrack:
        return juce::Colour(0xFFFFFFFFu);
    case ScrollBarThumb:
        return juce::Colour(0xFFFFFFFFu);
    case AlertBackground:
        return juce::Colour(0xFF555555u);

    case Transparent:
        return juce::Colour(0x00000000u);

    // NEVER change this one!!! It's only used for recoloring icon_links.svg!
    case AboutIconDefault:
        return juce::Colour(0xFFFFFFFFu);

    /// \note No `default:`, deliberately: -Wswitch is then what says a new
    /// enumerator has been added without a colour to answer it with, at compile
    /// time and in this file, rather than a black widget at run time.
    case numberOfColours:
        break;
    }

    LE_UNREACHABLE_CODE();
}

////////////////////////////////////////////////////////////////////////////////
//
// ColourMap::sstDark()
// --------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   Shortcircuit XT's wireframe-dark, from its themes/wireframe-dark.json: an
/// amber accent over three flat greys, with the content in four weights of
/// neutral.
///
/// \note The one palette with a `default:`, and it is the difference between
/// this and ClassicRed. SST Dark is a statement about the *chassis* -- what the
/// editor's grounds and its ink are -- and it has nothing to say about the
/// second highlight near the foot of a slider bead or the shade a knob's bevel
/// turns over at. Those keep the artwork's own modelling, turned to amber. So
/// what is listed below is exactly what SCXT has an opinion about.
///
////////////////////////////////////////////////////////////////////////////////

juce::Colour ColourMap::sstDark(Name const name, Rotation const rotation)
{
    switch (name)
    {
    ///   accent_1b rather than accent_1a. SCXT uses the brighter amber for
    /// small marks; here the accent is also what every panel's gradient is
    /// lifted *towards*, and #FFB949 at 0.78 of perceived brightness turns that
    /// wash into the loudest thing on the page. This one sits at 0.62, which is
    /// where ClassicBlue's blue sits, so the chassis keeps the weight it was drawn
    /// with. \see BackgroundStyle::Ramp::lift.
    case Accent:
        return turned(classic(Accent), rotation).withMultipliedBrightness(0.85); // accent_1b

    ///   The chassis, inverted. ClassicBlue's surround is a light grey with dark
    /// panels on it; every ground here is one of the three darks.
    case EditorGradientStart:
        return turned(classic(EditorGradientStart), rotation)
            .withMultipliedBrightness(0.8263)
            .withMultipliedAlpha(0.565f); // accent_1b
    case EditorSurround:
        return juce::Colour(0xFF1B1D20u); // bg_main
    case EditorPanel:
        return juce::Colour(0xFF262A2Fu); // bg_2
    case EditorWell:
        return juce::Colour(0xFF1B1D20u); // bg_1
    case EditorWellFace:
        return juce::Colour(0xFF141619u); // under bg_main, as ClassicBlue's is under its own
    case PanelBackground:
        return juce::Colour(0xFF262A2fu);
    case ModuleBackground:
        return juce::Colour(0xFF1B1D20u);
    case ComboBackground:
        return juce::Colour(0xFF1B1D20u);
    case MenuBackground:
        return juce::Colour(0xFF1B1D20u);
    case Ground:
        return juce::Colour(0xFF141619u);

    ///   The rules. White hairlines round every panel would be the loudest
    /// thing on a ground this dark, so they go to the grey SCXT divides panels
    /// with.
    case EditorRule:
        return juce::Colour(0xFF777777u); // generic_content_low
    case PanelFrame:
        return juce::Colour(0xFF777777u); // generic_content_low
    case MenuOutline:
        return juce::Colour(0xFF777777u); // generic_content_low

    /// The ink, in SCXT's four weights.
    case Text:
        return juce::Colour(0xFFFFFFFFu); // generic_content_highest
    case TextDimmed:
        return juce::Colour(0xFFC0C0C0u); // generic_content_high
    case TextFaint:
        return juce::Colour(0xFF777777u); // generic_content_low
    case Wordmark:
        return juce::Colour(0xFFFFFFFFu); // generic_content_medium

    ///   A button. ClassicBlue's runs from white to black with dark ink on it,
    /// which on this chassis would be a slab of daylight; SCXT's buttons are
    /// the mid grey against the panel behind them.
    case ButtonFaceBottom:
        return juce::Colour(0xFF333333u);
    case TabFaceBottom:
        return juce::Colour(0xFF333333u);

    default:
        return turned(classic(name), rotation);
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// ColourMap::getColour()
// ----------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note An HSB round trip per colour asked for, on the palettes that turn one.
/// Measured against the alternative -- a table filled on setPalette() -- it is
/// not worth the invalidation: the editor asks for a few hundred colours a
/// frame, thirty times a second, and each one is a dozen floating-point
/// operations.
///
////////////////////////////////////////////////////////////////////////////////

juce::Colour ColourMap::getColour(Name const name)
{
    switch (current_)
    {
    case ClassicBlue:
        return classic(name);
    case ClassicRed:
        return turned(classic(name), redRotation);
    case ClassicGreen:
        return turned(classic(name), greenRotation);
    case ClassicYellow:
        return turned(classic(name), yellowRotation);
    case ClassicAmber:
        return turned(classic(name), amberRotation);
    case ClassicPurple:
        return turned(classic(name), purpleRotation);
    case ClassicGray:
        return drained(classic(name));
    case DarkBlue:
        return sstDark(name, Rotation());
    case DarkRed:
        return sstDark(name, redRotation);
    case DarkGreen:
        return sstDark(name, greenRotation);
    case DarkYellow:
        return sstDark(name, yellowRotation);
    case DarkAmber:
        return sstDark(name, amberRotation);
    case DarkPurple:
        return sstDark(name, purpleRotation);
    case DarkGray:
        return drained(sstDark(name, Rotation()));

    /// \note No `default:` here either, for the reason getColour()'s has none.
    case numberOfPalettes:
        break;
    }

    LE_UNREACHABLE_CODE();
}

ColourMap::Palette ColourMap::palette() { return current_; }

std::uint32_t ColourMap::generation() { return generation_; }

void ColourMap::setPalette(Palette const palette)
{
    LE_ASSERT(palette < numberOfPalettes);
    if (palette == current_)
        return;

    current_ = palette;
    ++generation_;
}

char const *ColourMap::nameOf(Palette const palette)
{
    switch (palette)
    {
    case ClassicBlue:
        return "ClassicBlue";
    case ClassicRed:
        return "ClassicRed";
    case ClassicGreen:
        return "ClassicGreen";
    case ClassicYellow:
        return "ClassicYellow";
    case ClassicAmber:
        return "ClassicAmber";
    case ClassicPurple:
        return "ClassicPurple";
    case ClassicGray:
        return "ClassicGray";
    case DarkBlue:
        return "DarkBlue";
    case DarkRed:
        return "DarkRed";
    case DarkGreen:
        return "DarkGreen";
    case DarkYellow:
        return "DarkYellow";
    case DarkAmber:
        return "DarkAmber";
    case DarkPurple:
        return "DarkPurple";
    case DarkGray:
        return "DarkGray";
    case numberOfPalettes:
        break;
    }
    LE_UNREACHABLE_CODE();
}

} // namespace LE::SW::GUI
