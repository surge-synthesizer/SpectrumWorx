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
///                                       (15.08.2026.) (SW port)
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
/// \note juce::Slider::getThumbBeingDragged() answers the narrower question,
/// and its state is private. The 2016 code wrote to that private member -- it
/// was protected then -- to widen the meaning. This declares the wider notion
/// instead, and Theme asks for it where a slider offers one.
///                                           (29.07.2026.) (SW port)
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
///   LookAndFeel is abstract in JUCE 8 -- it inherits some twenty-six
/// LookAndFeelMethods interfaces whose members are pure -- so the 2016
/// derivation no longer compiles.
///
///   V4 is the wrong repair. LookAndFeel_V4::drawLinearSlider paints the whole
/// slider itself, so drawLinearSliderThumb below would never be called and the
/// LFO slider would quietly lose its bitmap thumb. V2::drawLinearSlider
/// forwards to background and thumb, which is what this skin was written
/// against. Every other override here has the same signature on V2 that it had
/// in 2016.
///                                       (28.07.2026.) (SW port)
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
    static juce::Colour blueColour() { return juce::Colour(19, 181, 234); }

    juce::Font const &blueFont() const { return blueFont_; }
    juce::Font const &whiteFont() const { return whiteFont_; }

  public: // juce::LookAndFeel_V2 overrides
    void drawLinearSliderBackground(juce::Graphics &, int x, int y, int width, int height,
                                    float sliderPos, float minSliderPos, float maxSliderPos,
                                    juce::Slider::SliderStyle, juce::Slider &) override;
    void drawLinearSliderThumb(juce::Graphics &, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               juce::Slider::SliderStyle, juce::Slider &) override;
    void drawPopupMenuBackground(juce::Graphics &, int width, int height) override;
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar &, juce::Graphics &, int w,
                                      int h) override;
    /// \note Returned juce::Image in 2016 and returns const Drawable * now
    /// (juce_FileBrowserComponent.h). It is reached through an explicitly
    /// qualified call from the preset browser rather than through the vtable,
    /// which is why the 2016 declaration had its `override` commented out --
    /// the signature had already drifted and nobody noticed.
    ///                                       (28.07.2026.) (SW port)
    juce::Drawable const *getDefaultFolderImage() override;
    int getMenuWindowFlags() override;
    juce::Font getPopupMenuFont() override;
    int getSliderThumbRadius(juce::Slider &) override;
    int getTabButtonSpaceAroundImage() override { return 0; }
    int getTabButtonOverlap(int /*tabDepth*/) override { return 0; }

  private:
    juce::Font const blueFont_;
    juce::Font const whiteFont_;

    std::unique_ptr<juce::Drawable> folderIcon_;
}; // class Theme

} // namespace LE::SW::GUI

#endif // theme_hpp
