////////////////////////////////////////////////////////////////////////////////
///
/// \file colourMap.hpp
/// -------------------
///
///   Every colour the editor paints with, in one place and asked for by name.
///
///   The artwork is drawn in code, so its palette has to live somewhere, and "a
/// constant beside the drawing that uses it" is how a tree ends up with the
/// accent blue spelled five slightly different ways. So: one enumerator per
/// colour the skin *chooses*, and a switch that answers it.
///
///   Transparency is not in here -- it is an absence rather than a choice, and
/// juce::Colours::transparentBlack says so better than a name would.
///
/// \note A switch rather than a table so that the answer can grow a condition
/// without every call site learning about it. Five palettes need that now, and
/// not one of the two hundred call sites changed to get them.
///
///   Only ClassicBlue is written out. It is traced from artwork and every tint in
/// it leans on the accent's hue, so ClassicRed and ClassicGreen are that hue turned -- and
/// a colour the artwork left neutral has no hue to turn, which is what keeps
/// the greys grey without a list of exceptions. ClassicGray is the same trick with
/// the colour taken out rather than moved. SST Dark is the one that is not a
/// recolour: it inverts the chassis, so it names what it changes and turns the
/// rest.
///
/// \note Sits in the same layer as theme.hpp and below everything else in
/// src/gui, and depends on juce_graphics and nothing of ours.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef colourMap_hpp__DD5E31D8_98D4_41FF_A352_7AA31EFA1DA1
#define colourMap_hpp__DD5E31D8_98D4_41FF_A352_7AA31EFA1DA1
//------------------------------------------------------------------------------
#include <juce_graphics/juce_graphics.h>

#include <cstdint>

namespace LE::SW::GUI
{

struct Rotation
{
    float hue{0.0f};        ///< turns of the colour wheel, added
    float saturation{1.0f}; ///< multiplied
    float brightness{1.0f}; ///< multiplied, in full at full saturation and not at all at none
}; // struct Rotation

////////////////////////////////////////////////////////////////////////////////
///
/// \class ColourMap
///
////////////////////////////////////////////////////////////////////////////////

class ColourMap
{
  public:
    /// \brief What a colour is *for*, which is what a call site knows.
    ///
    ///   Named after the thing on screen rather than after the pigment, so that
    /// retuning one does not need every use of it re-read: `ModuleKnobDomeRim`
    /// says where it goes, `0xFF0A0909` does not.
    enum Name
    {
        ////////////////////////////////////////////////////////////////////////
        /// \name The skin's own three
        ///
        ///   The accent means one thing throughout: *this is the
        /// value*, *this one is selected*, *this is on*. Everything that is not
        /// the accent is text on a dark ground, at one of three weights.
        ////////////////////////////////////////////////////////////////////////
        ///@{
        Accent,
        Text,       ///< what a label or a value is written in
        TextDimmed, ///< present, but not what is being looked at
        TextFaint,  ///< there because leaving it out would be a lie
        ///@}

        ////////////////////////////////////////////////////////////////////////
        /// \name The editor's knob
        ///
        /// \see EditorKnobStyle in gui.hpp for the geometry these are laid on.
        ////////////////////////////////////////////////////////////////////////
        ///@{
        EditorKnobBevelShadow, ///< the bevel in its own shade
        EditorKnobBevelMid,    ///< and coming up out of it
        EditorKnobBevelRim,    ///< to white where it turns over
        EditorKnobRingTop,     ///< the teal ring, lit from above
        EditorKnobRingBottom,  ///< and in shadow below
        EditorKnobCap,         ///< the cap the value is printed on
        EditorKnobTick,        ///< the eight fixed marks
        EditorKnobRim,         ///< the bright edge past the bevel
        EditorKnobRimOutline,  ///< and the dark line around it
        EditorKnobPointer,     ///< the bar that turns, in the accent
        ///@}

        ////////////////////////////////////////////////////////////////////////
        /// \name A module's knob
        ///
        /// \note Its wedge is not here: the wedge is the accent, so it asks for
        /// Accent. \see ModuleKnobStyle in modules/moduleUI.hpp.
        ////////////////////////////////////////////////////////////////////////
        ///@{
        ModuleKnobDomeCentre, ///< the dome where the cap ends
        ModuleKnobDomeRim,    ///< the dome at its rim, and the hairline on it
        ModuleKnobCap,        ///< the cap over the wedge's inside

        /// The halo around whichever control has the focus.
        FocusHalo,

        ////////////////////////////////////////////////////////////////////////
        /// \name Buttons and tabs
        ///
        ///   A button's face is a vertical ramp from a lit top edge to black,
        /// and its caption is dark ink on that. A tab is the same shape in
        /// slightly softer greys, or in the accent when it is the one showing.
        ///
        /// \note The selected tab's ramp runs from Accent to TabFaceBottom, so it
        /// has no top colour of its own. \see buttonPainter.hpp.
        ////////////////////////////////////////////////////////////////////////
        ///@{
        ButtonFaceTop,
        ButtonFaceBottom,
        ButtonCaption, ///< the text on either, which is dark on both
        TabFaceTop,
        TabFaceBottom,
        ///@}

        /// \brief The settings panel and the preset browser, behind everything
        /// they hold -- and drawn up behind the tab bar, which is what makes a
        /// tab meet its page rather than sit above it.
        PanelBackground,

        /// \brief The hairline around a field on a panel -- the preset list,
        /// the comment box, the folder the browser is in.
        PanelFrame,

        ////////////////////////////////////////////////////////////////////
        /// \name The capsule that says something is running
        ///
        /// \note Lit, it is the accent with a halo going from white to it as it
        /// spreads. Dark, it is a ramp across its own width -- almost black at
        /// the left and a cold steel at the right, which is the whole of the
        /// sheen on a shape five pixels tall. \see capsulePainter.hpp.
        ////////////////////////////////////////////////////////////////////////
        ///@{
        CapsuleBodyLeft,
        CapsuleBodyRight,
        CapsuleRim,    ///< the hairline around a dark one
        CapsuleRimLit, ///< and around a lit one, darker so the blue reads
        ///@}

        ////////////////////////////////////////////////////////////////////
        /// \name The bead an LFO slider is dragged by
        ///
        /// \note A lit cylinder in four colours, and a duller blue than the
        /// skin's accent -- it is furniture to be held rather than a value to
        /// be read. \see sliderThumbPainter.hpp.
        ////////////////////////////////////////////////////////////////////////
        ///@{
        ThumbHighlight, ///< where the light lands, along the top
        ThumbFace,      ///< the body of it
        ThumbSheen,     ///< the second, weaker highlight near its foot
        ThumbFoot,
        ThumbShadow, ///< down its left side, at an alpha the painter sets
        ///@}

        ////////////////////////////////////////////////////////////////////
        /// \name The editor's own chassis
        ///
        /// \note Two darks and a rule. Every panel in the editor is one of the
        /// darks eased into Accent by an amount of its own, which is the whole of
        /// what its six gradients were. \see backgroundPainter.hpp.
        ////////////////////////////////////////////////////////////////////////
        ///@{
        EditorGradientStart, ///< accent color of the background diagonal gradients
        EditorSurround,      ///< the flat grey the rounded body sits on
        EditorPanel,         ///< the two big panels, before they are lifted
        EditorWell,          ///< and the boxes between them, which start a shade darker
        EditorWellFace,      ///< inside the disc a knob sits in
        EditorRule,          ///< the hairline round every one of them
        Wordmark,            ///< "Spectrum Worx" down the left edge
        ///@}

        /// \brief Inside the tongue that ejects an effect, behind its cross.
        EjectFace,

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \brief The eleven LFO waveform marks, in the menu and beside the LFO.
        ///
        /// \note Neutral, and it has to stay neutral. These are drawn once into
        /// a cached Artwork (\see loadWaveform()) and that cache is only emptied
        /// when the Theme is destroyed, not when the palette turns -- so a
        /// waveform given a colour with a hue would keep whichever palette was
        /// live the first time it was drawn. A neutral is returned untouched by
        /// every palette, which is what makes caching it correct rather than
        /// merely unnoticed.
        ///
        ////////////////////////////////////////////////////////////////////////
        LFOWaveform,

        /// \brief Inside a combo box, behind the name of what is selected.
        ComboBackground,

        /// \brief Inside a module's frame, behind its controls.
        ///
        /// \note The rim around it is Accent and the halo on the focused one is
        /// FocusHalo, so this is the whole of what a strip adds to the palette.
        /// \see ModuleStripStyle in modules/moduleUI.hpp.
        ModuleBackground,

        ////////////////////////////////////////////////////////////////////////
        /// \name Under the pointer
        ///
        ///   A button says "the mouse is on me" by filling its shape with one
        /// of these. Which one depends on what the shape is: a pale glyph on a
        /// dark strip cannot be lifted any further, so the eject tongue is
        /// washed down instead.
        ///
        /// \note Both carry their own alpha. That is the whole of the effect --
        /// a tint at full opacity would replace the drawing rather than shade
        /// it -- so it belongs with the colour rather than at the call site.
        ////////////////////////////////////////////////////////////////////////
        ///@{
        MouseOverGlow,  ///< the lift an arrow gets
        MouseOverShade, ///< and the wash the eject cross gets instead
        ///@}

        ////////////////////////////////////////////////////////////////////////
        /// \name Menus
        ////////////////////////////////////////////////////////////////////////
        ///@{
        MenuBackground,
        MenuOutline,
        ///@}

        ////////////////////////////////////////////////////////////////////////
        /// \name The furniture: fields, lists, sliders, scroll bars
        ////////////////////////////////////////////////////////////////////////
        ///@{
        Ground,          ///< the flat black under a combo box, a button, the build stamp
        FieldBackground, ///< and behind something being typed into, which is lighter
        ListBackground,
        ListOutline,
        ListHighlight,  ///< the row the mouse is choosing
        SliderTrack,    ///< the line an LFO's thumbs run along
        ScrollBarThumb, ///< \see Theme::drawScrollbar
        AlertBackground,
        ///@}

        /// \brief No colour at all.
        ///
        ///   Named because it has to be said out loud a dozen times -- a JUCE
        /// LookAndFeel turns a piece of furniture off by painting it in nothing,
        /// and this skin turns a lot of furniture off. \see the note in
        /// theme.cpp's constructor.
        ///
        /// \note Where a *gradient* fades a colour out, ask that colour for
        /// `withAlpha( 0.0f )` rather than reaching for this: a gradient
        /// interpolates the channels as well as the alpha, so ending on
        /// something else\'s black is how a white highlight picks up a grey
        /// cast on its way out.
        Transparent,

        AboutIconDefault,

        numberOfColours
    }; // enum Name

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Which set of answers getColour() gives.
    ///
    /// \note Streamed by name into the preferences file, like every other
    /// enumeration the user picks from, so the order here is free. \see
    /// nameOf() and GUI::Preferences::palette().
    ///
    ////////////////////////////////////////////////////////////////////////////

    enum Palette
    {
        ClassicBlue,
        ClassicRed,
        ClassicGreen,
        ClassicYellow,
        ClassicAmber,
        ClassicPurple,
        ClassicGray,
        DarkBlue,
        DarkRed,
        DarkGreen,
        DarkYellow,
        DarkAmber,
        DarkPurple,
        DarkGray,

        numberOfPalettes
    }; // enum Palette

    static juce::Colour getColour(Name);

    static Palette palette();

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Repaints nothing. `[main-thread]`
    ///
    ///   The palette is process-wide, so a second plugin instance in the same
    /// host has just had its colours changed by a settings page it does not
    /// know about. Telling it is generation()'s job.
    ///
    ////////////////////////////////////////////////////////////////////////////

    static void setPalette(Palette);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Bumped by every setPalette() that changes anything.
    ///
    ///   What a live editor compares against to find out that the palette moved
    /// under it. Only ever compared for inequality -- so the wrap at 2^32 is
    /// not a case to think about -- and only ever from the message thread,
    /// which is what makes a plain counter enough. \see
    /// SpectrumWorxEditor::applyPaletteIfChanged().
    ///
    ////////////////////////////////////////////////////////////////////////////

    static std::uint32_t generation();

    /// \brief The enumerator's own spelling, for the preferences file and for
    /// SW_SHOW_UI_PALETTE. What a user *sees* is the settings page's business.
    static char const *nameOf(Palette);

  private:
    ////////////////////////////////////////////////////////////////////////////
    /// \name The two palettes that name colours rather than derive them
    ///
    /// \note Members rather than file statics only so that the switches inside
    /// them can say `Accent` instead of `ColourMap::Accent` fifty-six times.
    ///@{
    static juce::Colour classic(Name);
    static juce::Colour sstDark(Name, Rotation);
    ///@}

  public:
    ColourMap() = delete; // a namespace with a nested enum, not an object
}; // class ColourMap

} // namespace LE::SW::GUI

#endif // colourMap_hpp
