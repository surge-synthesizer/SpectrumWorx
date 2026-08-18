////////////////////////////////////////////////////////////////////////////////
///
/// \file capsulePainter.hpp
/// ------------------------
///
///   The little lozenge that says something is running.
///
///   Four skin files: an LFO's switch and a module strip's bypass, each in a lit
/// and a dark state (41, 42, 4, 5). One drawing at two sizes -- a capsule with a
/// hairline rim, a body that is the skin's blue when lit and a steel ramp when
/// not, and a halo outside it.
///
/// \note They were traced one file at a time and it shows: the lit module
/// capsule came back a fifth taller than its own dark state, and the two LEDs
/// disagreed about their rim by a quarter of a pixel. A capsule that changes
/// size when it lights is not something anyone drew on purpose, so there is one
/// geometry per widget and only the colours and the halo answer to \c lit.
///                                       (18.08.2026.)
///
/// \note Close kin to FramePainter -- a rim around a body with a glow outside --
/// and deliberately not it. This one is fully round, its body carries a
/// gradient, and its halo takes the skin's blue rather than white. Three
/// differences in a drawing this small is a different drawing.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef capsulePainter_hpp__0C74E9B2_A365_41D8_8E2F_5B90D3164A7E
#define capsulePainter_hpp__0C74E9B2_A365_41D8_8E2F_5B90D3164A7E
//------------------------------------------------------------------------------
#include <juce_graphics/juce_graphics.h>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \struct CapsuleStyle
///
////////////////////////////////////////////////////////////////////////////////

/// \brief One capsule's size and how far its halo carries, in skin pixels.
struct CapsuleStyle
{
    float width; ///< the capsule itself; its corners are half its height
    float height;

    /// \brief How far below the middle of its widget it sits.
    ///
    /// \note Both of them do, by about a pixel, and consistently between the
    /// lit and dark states of each -- so it is where they were drawn rather
    /// than where they were traced.
    float verticalOffset;

    float rimThickness;

    ////////////////////////////////////////////////////////////////////////
    ///
    /// \name The halo
    ///
    ///   Rounded capsules a pixel apart, laid down outermost first before the
    /// body covers their middles. Each is nearly as faint as the last, so what
    /// makes the light fall off is how many of them cover a given pixel: at
    /// \c d pixels out that is \c reach - d of them, which composites to
    /// `1 - (1 - alpha)^(reach - d)`. The artwork's, exactly -- and the reason
    /// a lit bypass carries fourteen pixels while nothing about it is brighter
    /// than four per cent.
    ///
    /// \note Two stacks, because the artwork had two: a broad faint one, and a
    /// hot pair against the rim. On a lit capsule the hot pair is white and the
    /// broad one blue, which is how the skin's blue gets a white core without
    /// anything having to interpolate.
    ///
    /// \note Lit and dark reach differently, and by a lot -- a lit bypass
    /// carries three times as far as a muted one. That is the whole of what a
    /// capsule five pixels wide has to say with.
    ///
    ////////////////////////////////////////////////////////////////////////////
    ///@{
    unsigned int litGlowReach; ///< px
    float litGlowAlpha;        ///< per ring, over most of that
    float litGlowHotAlpha;     ///< and over the two against the rim
    unsigned int darkGlowReach;
    float darkGlowAlpha;
    float darkGlowHotAlpha;

    /// \brief Whether a lit halo's hot pair is white rather than blue.
    ///
    /// \note The LED's is and the bypass's is not, which is the artwork's own
    /// answer: 42 laid four white rings over its blue stack and 4 laid none. A
    /// capsule eleven pixels wide needs the white to read as *on* rather than
    /// as merely blue; one half again as tall does not, and putting it there
    /// reads as a rim.
    bool litHotIsWhite;
    ///@}
}; // struct CapsuleStyle

/// \brief The LFO switch's, in a 25 x 14 widget. \see LEDTextButton, which puts
/// its caption beside one.
CapsuleStyle constexpr ledCapsule{
    /* size   */ 11.13f,   4.61f,
    /* offset */ 0.97f,
    /* rim    */ 0.85f,
    /* lit    */ 7u,       0.017f, 0.100f,
    /* dark   */ 6u,       0.018f, 0.038f,
    /* white core */ true,
};

/// \brief A module strip's bypass -- a bigger canvas for a smaller capsule,
/// because its halo carries much further.
///@{
int constexpr bypassWidgetWidth{38};
int constexpr bypassWidgetHeight{33};

CapsuleStyle constexpr bypassCapsule{
    /* size   */ 10.51f,    5.05f,
    /* offset */ 0.80f,
    /* rim    */ 0.89f,
    /* lit    */ 10u,       0.043f, 0.110f,
    /* dark   */ 4u,        0.045f, 0.160f,
    /* white core */ false,
};
///@}

////////////////////////////////////////////////////////////////////////////////
///
/// \class CapsulePainter
///
////////////////////////////////////////////////////////////////////////////////

class CapsulePainter
{
  public:
    /// \brief Draws a capsule centred in \p bounds.
    static void paint(juce::Graphics &, juce::Rectangle<float> bounds, CapsuleStyle const &,
                      bool lit);

  public:
    CapsulePainter() = delete; // a drawing, not an object
}; // class CapsulePainter

} // namespace LE::SW::GUI

#endif // capsulePainter_hpp
