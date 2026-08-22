////////////////////////////////////////////////////////////////////////////////
///
/// \file waveformPainter.hpp
/// -------------------------
///
///   The eleven LFO waveform marks, drawn rather than parsed.
///
///   Skin files 43 to 53 until 19.08.2026, and the last SVGs in the tree: with
/// these gone assets/skin holds two typefaces and nothing else, and
/// resources.cpp no longer parses vector at startup for anything but the mark.
///
/// \note The numbers below are the files' own, on the 24 x 17 canvas they were
/// drawn on, so that each can still be read against the artwork it came from.
/// They are *not* skin pixels: the painter maps that grid onto whatever
/// rectangle it is given, so an icon has no size of its own and nothing here
/// moved when the skin was rescaled. \see WaveformStyle::iconWidth for the size
/// the editor actually asks for.
///
/// \note Transcribed rather than generated. A sine from `std::sin` would be
/// tidier than nine traced control points -- and the traced one is visibly
/// asymmetric, starting at 9.4 and ending at 7.98 where a sine would not -- but
/// the point of this change was to keep the picture and drop the parser, so the
/// asymmetry is kept too. Generating them is a separate question with a
/// separate answer.
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef waveformPainter_hpp__B7E24A19_5C83_4F06_A2D1_9E3B60C7F485
#define waveformPainter_hpp__B7E24A19_5C83_4F06_A2D1_9E3B60C7F485
//------------------------------------------------------------------------------
#include <juce_graphics/juce_graphics.h>

#include <cstdint>
#include <memory>

/// \note juce::Drawable lives in gui_basics rather than in graphics, and this
/// header is included by painters that want nothing from gui_basics. \see the
/// same note in resources.cpp.
namespace juce
{
class Drawable;
}

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \namespace WaveformStyle
///
////////////////////////////////////////////////////////////////////////////////

namespace WaveformStyle
{
/// \brief The grid the marks are drawn on, which is the artwork's viewBox.
///@{
float constexpr gridWidth{24.0f};
float constexpr gridHeight{17.0f};
///@}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The size the editor draws one at, in skin pixels.
///
/// \note 24 x 17 until 19.08.2026, which is the grid above -- the files were
/// drawn at the size they were used at. The skin is 1.5x what it was and these
/// have to be too, or the icon that used to fill the LFO's well would cover two
/// thirds of it.
///
////////////////////////////////////////////////////////////////////////////////
///@{
int constexpr iconWidth{36};
int constexpr iconHeight{26};
///@}

/// \note The artwork's 1.75 at the grid's scale, so it thickens with the icon
/// rather than staying a hairline as it grows. \see RuleStyle::thickness, which
/// this deliberately is not: a rule bounds a box and wants whole pixels, a
/// waveform is a drawing.
float constexpr strokeOnGrid{1.75f};

/// The discs RandomWhacko is made of, on the same grid.
float constexpr whackoRadius{1.39f};
} // namespace WaveformStyle

////////////////////////////////////////////////////////////////////////////////
///
/// \class WaveformPainter
///
////////////////////////////////////////////////////////////////////////////////

class WaveformPainter
{
  public:
    /// \note The order is LE::Parameters::DiscreteValues<LFO::Waveform>'s, which
    /// is what the waveform menu is built from and what the editor indexes with.
    /// \see fillLFOWaveformsMenu().
    enum class Waveform : std::uint8_t
    {
        sine,
        triangle,
        sawtooth,
        reverseSaw,
        square,
        exponent,
        randomHold,
        randomSlide,
        randomWhacko,
        dirac,
        dIRAC,

        count
    }; // enum class Waveform

    /// \brief Draws \p waveform to fill \p bounds, in \p colour.
    static void paint(juce::Graphics &, Waveform, juce::Rectangle<float> bounds, juce::Colour);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief \p waveform as a Drawable at WaveformStyle::iconWidth.
    ///
    /// \note What resources.cpp hands to an Artwork, so that the waveform menu
    /// and the LFO's own readout keep taking an `Artwork const &` and neither
    /// had to learn what a waveform is. \see loadWaveform().
    ///
    ////////////////////////////////////////////////////////////////////////////
    static std::unique_ptr<juce::Drawable> drawable(Waveform, juce::Colour);

  public:
    WaveformPainter() = delete; // a drawing, not an object
}; // class WaveformPainter

} // namespace LE::SW::GUI

//------------------------------------------------------------------------------
#endif // waveformPainter_hpp
