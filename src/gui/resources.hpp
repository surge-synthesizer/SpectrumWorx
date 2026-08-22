////////////////////////////////////////////////////////////////////////////////
///
/// \file resources.hpp
/// -------------------
///
///   The editor's bitmaps and fonts, read out of the binary.
///
///   In 2016 these lived on disk. The plugin found them by mmapping a
/// `SpectrumWorx.paths` file that the installer wrote next to the binary
/// (gui.cpp's initializePaths / mapPathsFile / rootPath / resourcesPath), so a
/// plugin that had merely been copied rather than installed came up with no
/// skin at all. They are compiled in now, and that whole path machinery goes
/// with them.
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef resources_hpp__D4A81C36_7E92_4B05_9F17_2C8E6A4D530B
#define resources_hpp__D4A81C36_7E92_4B05_9F17_2C8E6A4D530B
//------------------------------------------------------------------------------
#include "colourMap.hpp"

#include <juce_graphics/juce_graphics.h>

#include <memory>

namespace juce
{
class Drawable;
}

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Artwork
///
////////////////////////////////////////////////////////////////////////////////

/// \brief One piece of skin artwork.
///
///   `NN.svg` is kept as a juce::Drawable and painted into whatever
/// juce::Graphics it is given, so it comes out at that context's resolution --
/// crisp on a 2x display, and crisp again if the editor ever scales. It reports
/// getWidth()/getHeight() in skin coordinates, which is what every widget lays
/// itself out from.
///
/// \note The skin was half bitmap and half vector while it was being redrawn,
/// and this class is what let those two migrations -- converting a file, and
/// teaching its widget to hold an Artwork -- happen independently. Both are
/// finished. \see the note on loadArtwork() in resources.cpp.
///
/// \note image() rasterises on demand, at the artwork's own size and therefore
/// at 1x. Two callers want it and both have a reason: a tint fills the
/// artwork's alpha with a colour, which needs pixels, and the show-ui skin page
/// is looking at the sheet rather than drawing it. It is also the thing to grep
/// for when something in the editor looks soft.
class Artwork
{
  public:
    Artwork();
    Artwork(std::unique_ptr<juce::Drawable>, int width, int height);
    Artwork(Artwork &&) noexcept;
    Artwork &operator=(Artwork &&) noexcept;
    ~Artwork();

    Artwork(Artwork const &) = delete;
    Artwork &operator=(Artwork const &) = delete;

    bool isValid() const;
    bool isVector() const;

    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    /// \brief Paints at (x, y) at the artwork's own size.
    ///
    /// \param overlay a colour to tint the artwork's alpha with, as
    /// juce::LookAndFeel_V2::drawImageButton does for a moused-over
    /// ImageButton. Transparent -- which is every case but three -- keeps a
    /// vector on the vector path; a real tint needs the pixels, so it
    /// rasterises, which is no worse than the bitmap it replaced.
    void draw(juce::Graphics &, int x, int y, float opacity = 1.0f,
              juce::Colour overlay = ColourMap::getColour(ColourMap::Transparent)) const;

    /// \brief Draws the \p source region of the artwork into \p target.
    ///
    ///   Two callers, both of which need a part of the artwork rather than all
    /// of it: a knob film strip takes one frame's band out of a tall sheet, and
    /// a slider thumb is drawn at 5/3 while dragged. A vector gets a clip and a
    /// transform, so it still resolves at the target's size rather than the
    /// source's -- which is the point for the enlarged thumb.
    void drawScaled(juce::Graphics &, juce::Rectangle<int> target, juce::Rectangle<int> source,
                    float opacity = 1.0f) const;

    /// \brief Draws the artwork to fit \p area, keeping its proportions.
    ///
    /// \note For the logo, which is a 64 x 64 drawing put in a rectangle that
    /// is neither 64 x 64 nor square. Everything else in the skin is drawn at
    /// the size it was made for.
    void drawWithin(juce::Graphics &, juce::Rectangle<float> area) const;

    /// The artwork rasterised at its own size, on first ask.
    juce::Image const &image() const;

    /// \brief A copy of the drawable, or null if this artwork is invalid.
    ///
    /// \note For juce::PopupMenu, whose icon overloads take either a
    /// juce::Image or ownership of a juce::Drawable. Handing it the latter is
    /// what keeps a menu icon sharp -- and JUCE wants its own, so this cannot
    /// be a reference to ours.
    std::unique_ptr<juce::Drawable> drawableCopy() const;

  private:
    std::unique_ptr<juce::Drawable> drawable_;
    mutable juce::Image image_;
    int width_ = 0;
    int height_ = 0;
}; // class Artwork

/// \brief Widget name to skin file number, the single source of truth.
///
/// \note The 2016 values were multi-character literals -- `EditorBackground =
/// '01'` -- which resourceBitmap<>() then took apart with boost::mpl::string to
/// recover the two digits of the file name. Multi-character literals have an
/// implementation-defined value, and the only thing ever wanted from them was
/// the number, so these are now the number. The names, and therefore every call
/// site, are unchanged.
///
/// \note A list rather than a bare enum so that a test can walk it and assert
/// every named bitmap is actually in the binary -- the failure it replaces (a
/// mistyped number) is otherwise a blank widget nobody notices until someone
/// looks at a screenshot. Same idiom as LE_SW_EFFECT_LIST.
///
/// \brief x( Name, fileNumber )
// clang-format off
#define LE_SW_RESOURCE_BITMAP_LIST(x)                       \
    /* LFO */                                               \
    x(LFOSine,                43)                           \
    x(LFOTriangle,            44)                           \
    x(LFOSawtooth,            45)                           \
    x(LFOReverseSaw,          46)                           \
    x(LFOSquare,              47)                           \
    x(LFOExponent,            48)                           \
    x(LFORandomHold,          49)                           \
    x(LFORandomSlide,         50)                           \
    x(LFORandomWhacko,        51)                           \
    x(LFODirac,               52)                           \
    x(LFOdIRAC,               53)
// clang-format on

enum ResourceBitmaps
{
#define LE_SW_AUX_RESOURCE_BITMAP(name, number) name = number,
    LE_SW_RESOURCE_BITMAP_LIST(LE_SW_AUX_RESOURCE_BITMAP)
#undef LE_SW_AUX_RESOURCE_BITMAP
}; // enum ResourceBitmaps

/// The highest number `assets/skin` holds, and the size of the cache.
unsigned int constexpr numberOfResourceBitmaps = 62;

/// \brief Decodes on first use and caches; the reference stays valid until
/// releaseCachedResources().
///
/// \note The 2016 cache was a function-local static inside a template, so there
/// was one juce::Image per instantiation and no way to release any of them --
/// they outlived the JUCE they were allocated under. This is one array, and it
/// can be emptied.
///
/// \note The numbering has holes and is now *entirely* holes: no number
/// resolves to a file any more. 43 to 53 still resolve, but to WaveformPainter
/// rather than to anything in assets/skin -- \see loadArtwork(), which is the
/// one place that knows. A hole yields an invalid image rather than an
/// assertion, so that iterating the range is legal; the check that every
/// *named* number resolves is a test (skinTests.cpp), which is a better place
/// for it than every call.
///
/// \note Fourteen went on 18.08.2026 and they were all the same drawing: the
/// Presets and Settings buttons (8 to 11), the settings tabs (21 to 24, 27, 28)
/// and the browser\'s Save, Save as and Delete (30 to 35). A rounded pill with a
/// ramp in it and a caption baked on, at four widths. \see buttonPainter.hpp.
///
/// \note And eight more the same day, every one of them an outline: a module
/// strip's frame (55, 56), the four combo-box backgrounds (59 to 62), and the
/// two overlay panels (7, 17). \see framePainter.hpp and panelPainter.hpp.
///
/// \note And 58 with them, the ring that says a round control has the focus,
/// and 13 and 14, which were a trigger button's two states -- one radial
/// gradient each, sharing every stop with the other from the cap outward, and
/// most of them with a module knob. \see painters/knobPainter.hpp.
///
/// \note And four capsules on the 18th as well -- an LFO's switch and a module
/// strip's bypass, lit and dark (41, 42, 4, 5). One drawing at two sizes.
/// \see painters/capsulePainter.hpp.
///
/// \note And the last three shapes with them: the tongue that ejects an effect
/// (16) and the two arrows (6, 57). \see painters/ejectPainter.hpp and
/// painters/arrowPainter.hpp.
///
/// \note And the bead an LFO slider is dragged by (40), which was the last
/// shape. \see painters/sliderThumbPainter.hpp.
///
/// \note And 1 with it, which was the editor's whole chassis and 45 % of what
/// was left: 52 paths, six gradients and six pieces of copy nothing could
/// change. \see painters/backgroundPainter.hpp.
///
/// \note And the eleven LFO waveform icons (43 to 53) on 19.08.2026, which
/// were the last SVGs in the tree. They are WaveformPainter now and keep their
/// numbers, so the waveform menu did not change. \see
/// painters/waveformPainter.hpp.
///
///   What is left in assets/skin is the two typefaces. Nothing there is a
/// picture any more.
///
/// \note Six of those holes are the knob film strips and what went with them:
/// the module knob's four (3, 12, 63, 64) plus its focus ring (65) and LFO disc
/// (68) on 14.08.2026, and the editor knob's (2) on the 15th. Every one was 127
/// frames of one drawing at a fixed size; the drawings are ModuleKnobStyle and
/// EditorKnobStyle now. 58 stays -- TriggerButton still draws its focus ring.
///
/// \note Three more went with the About page on 15.08.2026: the baked page
/// itself (20) and the two halves of the "User's guide" button (66, 67), which
/// opened a PDF no installer has written since 2016. \see gui/about.cpp.
juce::Image const &resourceBitmap(unsigned int number);

/// \brief The artwork itself, which is what reaches the screen as a vector.
///
/// \note Prefer this to resourceBitmap() everywhere: asking for the bitmap
/// rasterises at 1x and throws the resolution away. The reference stays valid
/// until releaseCachedResources(), same as resourceBitmap()'s.
Artwork const &resourceArtwork(unsigned int number);

template <unsigned int bitmapID> Artwork const &resourceArtwork()
{
    return resourceArtwork(bitmapID);
}

bool hasResourceBitmap(unsigned int number);

/// \brief Whether this number resolved to a drawable.
///
/// \note Which is now the same question as "is it there at all", and the case
/// in skinTests.cpp that walks the numbering with it is what would notice a
/// waveform that stopped being built -- at which point the LFO menu is eleven
/// blank rows and nothing else says so.
bool resourceIsVector(unsigned int number);

template <unsigned int bitmapID> juce::Image const &resourceBitmap()
{
    return resourceBitmap(bitmapID);
}

/// \brief The SpectrumWorx mark, which the editor draws down its left edge.
///
/// \note assets/LOGO.svg, embedded in place rather than copied into skin/ under
/// a number. It is also what scripts/make_icons.sh cuts the packaged icons from,
/// and a mark that is two files is a mark that is eventually two marks.
/// \see painters/backgroundPainter.hpp.
Artwork const &logoArtwork();

/// Bitstream Vera, the skin's font, loaded straight from the embedded bytes.
///
/// \note 2016 registered the two .ttf files *with the operating system*
/// (AddFontResourceEx / CTFontManagerRegisterFontsForURL) and then referred to
/// them by family name, which needed a file on disk, leaked the registration if
/// the plugin was unloaded abruptly, and let a system font of the same name win.
/// JUCE 8 can make a Typeface directly out of memory.
juce::Typeface::Ptr regularTypeface();
juce::Typeface::Ptr boldTypeface();

/// Drops every decoded image and both typefaces. Called when the last editor
/// goes away, so that nothing outlives the JUCE instance it was created under.
void releaseCachedResources();

} // namespace LE::SW::GUI

#endif // resources_hpp
