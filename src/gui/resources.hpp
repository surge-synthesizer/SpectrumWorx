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

/// \brief One piece of skin artwork: a vector where the skin has been redrawn,
/// a bitmap where it has not.
///
///   A file that exists as `NN.svg` is kept as a juce::Drawable and painted
/// into whatever juce::Graphics it is given, so it comes out at that context's
/// resolution -- crisp on a 2x display, and crisp again if the editor ever
/// scales. A file that is still `NN.png` is a juce::Image and draws exactly as
/// it always did. Both report getWidth()/getHeight() in skin coordinates, so
/// nothing about layout depends on which one a number turned out to be.
///
/// \note image() rasterises a vector on demand rather than refusing, so a call
/// site that has not been moved off juce::Image keeps working the moment its
/// artwork is converted. That is what makes the two migrations -- redrawing a
/// file, and teaching its widget to hold an Artwork -- independent of each
/// other. It is also the thing to grep for when a converted button still looks
/// soft: it means the widget is asking for a bitmap.
class Artwork
{
  public:
    Artwork();
    explicit Artwork(juce::Image);
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
              juce::Colour overlay = juce::Colour()) const;

    /// \brief Draws the \p source region of the artwork into \p target.
    ///
    ///   Two callers, both of which need a part of the artwork rather than all
    /// of it: a knob film strip takes one frame's band out of a tall sheet, and
    /// a slider thumb is drawn at 5/3 while dragged. A vector gets a clip and a
    /// transform, so it still resolves at the target's size rather than the
    /// source's -- which is the point for the enlarged thumb.
    void drawScaled(juce::Graphics &, juce::Rectangle<int> target, juce::Rectangle<int> source,
                    float opacity = 1.0f) const;

    /// The bitmap; a vector is rasterised at its own size on first ask.
    juce::Image const &image() const;

    /// \brief A copy of the drawable, or null for a bitmap.
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
///                                       (28.07.2026.) (SW port)
///
/// \brief x( Name, fileNumber )
// clang-format off
#define LE_SW_RESOURCE_BITMAP_LIST(x)                       \
    /* Editor */                                            \
    x(EditorBackground,        1)                           \
    x(AddModule,               6)                           \
    x(PresetOff,               8)                           \
    x(PresetOn,                9)                           \
    x(SettingsOff,            10)                           \
    x(SettingsOn,             11)                           \
                                                            \
    /* Module panel */                                      \
    x(ModuleOn,                4)                           \
    x(ModuleMuted,             5)                           \
    x(TriggerBtnOff,          13)                           \
    x(TriggerBtnOn,           14)                           \
    x(Eject,                  16)                           \
    x(ModuleBg,               55)                           \
    x(ModuleBgSelected,       56)                           \
    x(ModuleKnobSelected,     58)                           \
    x(ModuleCombo,            59)                           \
    x(ModuleComboOn,          60)                           \
                                                            \
    /* Settings panel */                                    \
    x(SettingsEngineBg,       17)                           \
    x(SettingsIntrfcBg,       17) /* ...same as engine... */\
    x(SettingsAboutBg,        20)                           \
    x(SettingsEngineOff,      21)                           \
    x(SettingsEngineOn,       22)                           \
    x(SettingsGUIOff,         23)                           \
    x(SettingsGUIOn,          24)                           \
    x(SettingsAboutOff,       27)                           \
    x(SettingsAboutOn,        28)                           \
    x(SettingsCombo,          61)                           \
    x(SettingsComboOn,        62)                           \
    x(UsersGuideUp,           66)                           \
    x(UsersGuideDown,         67)                           \
                                                            \
    /* Preset browser */                                    \
    x(PresetBackground,        7)                           \
    x(PresetSaveUp,           30)                           \
    x(PresetSaveDown,         31)                           \
    x(PresetDeleteUp,         32)                           \
    x(PresetDeleteDown,       33)                           \
    x(PresetSaveAsUp,         34)                           \
    x(PresetSaveAsDown,       35)                           \
                                                            \
    /* LFO */                                               \
    x(LFOSliderThumb,         40)                           \
    x(LEDOff,                 41)                           \
    x(LEDOn,                  42)                           \
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
    x(LFOdIRAC,               53)                           \
    x(ChangeWaveform,         57)
// clang-format on

enum ResourceBitmaps
{
#define LE_SW_AUX_RESOURCE_BITMAP(name, number) name = number,
    LE_SW_RESOURCE_BITMAP_LIST(LE_SW_AUX_RESOURCE_BITMAP)
#undef LE_SW_AUX_RESOURCE_BITMAP
}; // enum ResourceBitmaps

/// The highest number `assets/skin` holds, and the size of the cache.
unsigned int constexpr numberOfResourceBitmaps = 67;

/// \brief Decodes on first use and caches; the reference stays valid until
/// releaseCachedResources().
///
/// \note The 2016 cache was a function-local static inside a template, so there
/// was one juce::Image per instantiation and no way to release any of them --
/// they outlived the JUCE they were allocated under. This is one array, and it
/// can be emptied.
///
/// \note The numbering has holes: 2, 3, 12, 15, 18, 54, 63, 64, 65 and 68 do not
/// exist, and 19, 25, 26, 29 and 36 to 39 exist but no widget names them. A hole
/// yields an invalid image rather than an assertion, so that iterating the range
/// is legal; the check that every *named* bitmap resolves is a test
/// (skinTests.cpp), which is a better place for it than every call.
///
/// \note Six of those holes are the knob film strips and what went with them:
/// the module knob's four (3, 12, 63, 64) plus its focus ring (65) and LFO disc
/// (68) on 14.08.2026, and the editor knob's (2) on the 15th. Every one was 127
/// frames of one drawing at a fixed size; the drawings are ModuleKnobStyle and
/// EditorKnobStyle now. 58 stays -- TriggerButton still draws its focus ring.
juce::Image const &resourceBitmap(unsigned int number);

/// \brief The same artwork, in the form that can still be a vector.
///
/// \note Prefer this to resourceBitmap() in new code and when touching old:
/// it is what lets a converted file reach the screen as one. The reference
/// stays valid until releaseCachedResources(), same as resourceBitmap()'s.
Artwork const &resourceArtwork(unsigned int number);

template <unsigned int bitmapID> Artwork const &resourceArtwork()
{
    return resourceArtwork(bitmapID);
}

bool hasResourceBitmap(unsigned int number);

/// \brief Whether this number is served by a vector file rather than by its
/// bitmap.
///
/// \note The skin is being redrawn as SVG a few files at a time and both forms
/// are embedded, so which one a number resolves to is otherwise invisible from
/// out here -- a converted button and its bitmap are the same size and, at 1x,
/// very nearly the same pixels. Without this, a glob that quietly stopped
/// picking up assets/skin/*.svg would leave every test passing against the
/// artwork it was supposed to have replaced.
bool resourceIsVector(unsigned int number);

template <unsigned int bitmapID> juce::Image const &resourceBitmap()
{
    return resourceBitmap(bitmapID);
}

/// Bitstream Vera, the skin's font, loaded straight from the embedded bytes.
///
/// \note 2016 registered the two .ttf files *with the operating system*
/// (AddFontResourceEx / CTFontManagerRegisterFontsForURL) and then referred to
/// them by family name, which needed a file on disk, leaked the registration if
/// the plugin was unloaded abruptly, and let a system font of the same name win.
/// JUCE 8 can make a Typeface directly out of memory.
///                                       (28.07.2026.) (SW port)
juce::Typeface::Ptr regularTypeface();
juce::Typeface::Ptr boldTypeface();

/// Drops every decoded image and both typefaces. Called when the last editor
/// goes away, so that nothing outlives the JUCE instance it was created under.
void releaseCachedResources();

} // namespace LE::SW::GUI

#endif // resources_hpp
