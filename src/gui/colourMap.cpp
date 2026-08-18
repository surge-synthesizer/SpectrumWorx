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

#include "le/utility/platformSpecifics.hpp"

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
//
// ColourMap::getColour()
// ----------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note ARGB throughout, including the opaque ones: three of these are
/// deliberately translucent and writing all of them the same way is what keeps
/// that from reading as a typo.
///
////////////////////////////////////////////////////////////////////////////////

juce::Colour ColourMap::getColour(Name const name)
{
    switch (name)
    {
    ///   19, 181, 234, which is what Theme said and what the skin's vectors
    /// were traced to within three parts in 255. \see the note in the header.
    case Blue:
        return juce::Colour(0xFF13B5EAu);
    case Text:
        return juce::Colour(0xFFFFFFFFu);
    case TextDimmed:
        return juce::Colour(0xFFD3D3D3u);
    case TextFaint:
        return juce::Colour(0xFF808080u);

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
    case EditorKnobPointer:
        return juce::Colour(0xFF161616u);

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
        return juce::Colour(0xFFE1EFF3u);

    case EjectFace:
        return juce::Colour(0xFF6F777Bu);

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
        return juce::Colour(0x88FFFFFFu);
    case SliderTrack:
        return juce::Colour(0xFFFFFFFFu);
    case ScrollBarThumb:
        return juce::Colour(0xFFFFFFFFu);
    case AlertBackground:
        return juce::Colour(0xFF555555u);

    case Transparent:
        return juce::Colour(0x00000000u);

    /// \note No `default:`, deliberately: -Wswitch is then what says a new
    /// enumerator has been added without a colour to answer it with, at compile
    /// time and in this file, rather than a black widget at run time.
    case numberOfColours:
        break;
    }

    LE_UNREACHABLE_CODE();
}

} // namespace LE::SW::GUI
