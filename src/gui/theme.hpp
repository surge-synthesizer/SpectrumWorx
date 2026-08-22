////////////////////////////////////////////////////////////////////////////////
///
/// \file theme.hpp
/// ---------------
///
///   SpectrumWorx's LookAndFeel.
///
/// \note A `Theme::Settings` used to travel with it -- the three answers the
/// settings panel's Interface page asks for. They were never the LookAndFeel's
/// and they are GUI::Preferences now. \see preferences.hpp.
///
///   Split out of gui.hpp so that it can be built and looked at before the
/// widget set compiles: everything else in src/gui reaches Theme for a font or
/// a colour, so it has to come first, and it has almost no dependencies of its
/// own.
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef theme_hpp__A1E7C204_53B8_4D96_BF31_0C7A5E2D8946
#define theme_hpp__A1E7C204_53B8_4D96_BF31_0C7A5E2D8946
//------------------------------------------------------------------------------
#include "colourMap.hpp"
#include "painters/sliderThumbPainter.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class SliderWithSelectedThumb
///
/// \brief A slider that highlights a thumb the mouse is merely over, not only
/// the one being dragged.
///
/// \note juce::Slider::getThumbBeingDragged() answers the narrower question and
/// its state is private, so the wider notion is declared here rather than forced
/// into JUCE's, and Theme asks for it where a slider offers one.
////////////////////////////////////////////////////////////////////////////////

class SliderWithSelectedThumb
{
  public:
    /// 0 for a single thumb, 1 minimum, 2 maximum, -1 for none -- the same
    /// encoding juce::Slider::getThumbBeingDragged() uses.
    virtual int selectedThumb() const = 0;

  protected:
    ~SliderWithSelectedThumb() = default;
}; // class SliderWithSelectedThumb

/// The selected thumb if the slider tracks one, else the dragged thumb.
int selectedOrDraggedThumb(juce::Slider const &);

/// \note LookAndFeel_V2, not LookAndFeel_V4, and not LookAndFeel.
///
///   LookAndFeel is abstract in JUCE 8, inheriting some twenty-six
/// LookAndFeelMethods interfaces whose members are pure.
///
///   V4 is the wrong repair: LookAndFeel_V4::drawLinearSlider paints the whole
/// slider itself, so drawLinearSliderThumb below would never be called and the
/// LFO slider would quietly lose its skinned thumb. V2::drawLinearSlider
/// forwards to background and thumb, which is what this skin is written against.
class Theme final : public juce::LookAndFeel_V2
{
  public:
    Theme(Theme const &) = delete; // makes non-copyable
    Theme &operator=(Theme const &) = delete;

  public:
    static void createSingleton();
    static void destroySingleton();

    static Theme &singleton();
    static bool haveSingleton();

    Theme();
    ~Theme() override;

  public:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Takes the colours again, if the palette has moved since they were
    /// last taken. `[main-thread]`
    ///
    ///   Every other colour in the editor is asked for while painting, so a
    /// repaint is the whole of what a palette change needs. These are not:
    /// the constructor copies four dozen of them into JUCE colour IDs and the
    /// folder icon has two baked into a Drawable, so they go stale in place.
    ///
    /// \note Guarded by the generation rather than by a flag the caller sets,
    /// because every open editor calls this on the tick it notices -- the first
    /// does the work and the rest cost a comparison.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void reloadColours();

  public:
    /// \brief The two sizes the skin writes in.
    ///
    /// \note These were blueFont() and whiteFont() until the palette moved to
    /// ColourMap, and the names were already not true: whiteFont() is what a
    /// TextButton draws its caption in, and it draws it *blue*. A font has a
    /// face and a size and nothing else, so these are named for what asks for
    /// them -- the name of a thing, and the text under or inside it.
    juce::Font const &headingFont() const { return headingFont_; }
    juce::Font const &labelFont() const { return labelFont_; }

  private:
    /// \brief Everything the constructor and reloadColours() have in common,
    /// which is all of it.
    void takeColours();

  public: // juce::LookAndFeel_V2 overrides
    void drawLinearSliderBackground(juce::Graphics &, int x, int y, int width, int height,
                                    float sliderPos, float minSliderPos, float maxSliderPos,
                                    juce::Slider::SliderStyle, juce::Slider &) override;
    void drawLinearSliderThumb(juce::Graphics &, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               juce::Slider::SliderStyle, juce::Slider &) override;
    void drawPopupMenuBackground(juce::Graphics &, int width, int height) override;
    void drawResizableFrame(juce::Graphics &, int width, int height,
                            juce::BorderSize<int> const &) override;
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar &, juce::Graphics &, int w,
                                      int h) override;
    /// \note Reached through an explicitly qualified call from the preset browser
    /// rather than through the vtable, so `override` is what keeps the signature
    /// pinned to juce_FileBrowserComponent.h's.
    juce::Drawable const *getDefaultFolderImage() override;
    int getMenuWindowFlags() override;
    juce::Font getPopupMenuFont() override;
    int getSliderThumbRadius(juce::Slider &) override;
    int getTabButtonSpaceAroundImage() override { return 0; }
    int getTabButtonOverlap(int /*tabDepth*/) override { return 0; }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \name The scroll bars
    ///
    ///   A thumb and nothing else: no groove behind it and no step buttons at
    /// its ends, so what a scrolling view gains is a bar floating over its own
    /// background rather than a strip of furniture beside it. \see issue #90.
    ///
    /// \note V2 draws a light rounded slot the full length of the bar, a
    /// gradient-shaded thumb on top of it and a triangle button at each end, all
    /// eighteen pixels wide. Against this skin the slot was the brightest thing
    /// in the preset browser and the thumb was barely a shade off it.
    ///
    /// \note The width is the *LookAndFeel's* answer rather than each viewport's,
    /// which is what makes "narrower everywhere" one number. `juce::Viewport`
    /// asks for it whenever it has not been told otherwise
    /// (juce_Viewport.cpp:162), and a `juce::TextEditor`'s viewport never is --
    /// which is why the preset browser's comment box carried a bar half again as
    /// wide as the list's, with correspondingly larger arrows on it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    ///@{
    bool areScrollbarButtonsVisible() override { return false; }
    int getDefaultScrollbarWidth() override { return scrollBarThickness; }
    int getMinimumScrollbarThumbSize(juce::ScrollBar &) override;
    void drawScrollbar(juce::Graphics &, juce::ScrollBar &, int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override;
    ///@}

    /// \brief What a scroll bar takes out of the view it scrolls, in pixels.
    ///
    /// \note Nine, of which the thumb is six: the inset either side is what keeps
    /// it off the edge of the view. JUCE's own answer is eighteen.
    ///
    /// \note Six until 19.08.2026, which was this number drawn through the
    /// skin's 1.5 transform. Drawing the editor at 1:1 left it at six *screen*
    /// pixels -- a two pixel thumb, which is a bar too thin to aim at. \see
    /// issue #134.
    static int constexpr scrollBarThickness{9};

  private:
    juce::Font const headingFont_;
    juce::Font const labelFont_;

    std::unique_ptr<juce::Drawable> folderIcon_;

    /// Which ColourMap::generation() the two above were taken from.
    std::uint32_t palette_;
}; // class Theme

} // namespace LE::SW::GUI

#endif // theme_hpp
