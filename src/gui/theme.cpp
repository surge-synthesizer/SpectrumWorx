////////////////////////////////////////////////////////////////////////////////
///
/// \file theme.cpp
/// ---------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "theme.hpp"

#include "resources.hpp"

#include "le/utility/assert.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace LE::SW::GUI
{

namespace
{
std::optional<Theme> singleton_;

/// \note The film-strip thumb is drawn at 5/3 while dragged, which is what the
/// enlarge flag means. Lifted unchanged from gui.cpp.
void paintSliderThumb(juce::Graphics &graphics, Artwork const &image, float const position,
                      int const sliderVerticalPosition, int const sliderHeight, bool const enlarge)
{
    auto const thumbWidth(image.getWidth());
    auto const thumbHeight(image.getHeight());

    auto const scaledThumbWidth(enlarge ? thumbWidth * 5 / 3 : thumbWidth);
    auto const scaledThumbHeight(enlarge ? thumbHeight * 5 / 3 : thumbHeight);

    auto const horizontalPosition(juce::roundToInt(position) - (scaledThumbWidth / 2));
    auto const verticalPosition(sliderVerticalPosition + (sliderHeight / 2) -
                                (scaledThumbHeight / 2));

    image.drawScaled(graphics,
                     {horizontalPosition, verticalPosition, scaledThumbWidth, scaledThumbHeight},
                     {0, 0, thumbWidth, thumbHeight});
}
} // anonymous namespace

/// \note 2016 named the font family -- "Bitstream Vera Sans" -- after
/// registering the .ttf files with the operating system. The typefaces come
/// straight out of the binary now, so they are handed to JUCE by pointer and
/// no system font of the same name can win. See resources.hpp.
///                                       (28.07.2026.) (SW port)
///
/// \note **Regular, not bold, since issue #76.** Both of these were the bold
/// face, which is why every label in the skin read as emphasised and why the
/// longer effect titles had to be squeezed horizontally to fit their boxes at
/// all. Bitstream Vera Sans ships Roman and Bold and nothing between, so "one
/// weight down" is the regular face; `boldTypeface()` is still what the About
/// page's title asks for.
///                                       (16.08.2026.)
Theme::Theme()
    : blueFont_(juce::FontOptions(regularTypeface()).withHeight(14.0f)),
      whiteFont_(juce::FontOptions(regularTypeface()).withHeight(12.0f))
{
    setDefaultSansSerifTypeface(regularTypeface());

    setColour(juce::PopupMenu::textColourId, juce::Colours::lightgrey);
    setColour(juce::PopupMenu::headerTextColourId, juce::Colours::white);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colours::transparentWhite);
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colours::black.withAlpha(0.8f));

    setColour(juce::TextButton::buttonColourId, juce::Colours::black);
    setColour(juce::TextButton::buttonOnColourId, blueColour());
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    setColour(juce::AlertWindow::backgroundColourId, juce::Colours::darkgrey);
    setColour(juce::AlertWindow::textColourId, juce::Colours::white);

    setColour(juce::ComboBox::backgroundColourId, juce::Colours::black);
    setColour(juce::ComboBox::buttonColourId, juce::Colours::lightgrey);
    setColour(juce::ComboBox::textColourId, juce::Colours::white);

    setColour(juce::Label::backgroundColourId, juce::Colours::transparentWhite);
    setColour(juce::Label::outlineColourId, juce::Colours::transparentWhite);
    setColour(juce::Label::textColourId, juce::Colours::white);

    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0x88000000));
    setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::highlightColourId, blueColour());
    setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    setColour(juce::TextEditor::textColourId, juce::Colours::white);
    setColour(juce::CaretComponent::caretColourId, blueColour());

    setColour(juce::ListBox::backgroundColourId, juce::Colour(0x11000000));
    setColour(juce::ListBox::outlineColourId, juce::Colour(0xAA000000));
    setColour(juce::ListBox::textColourId, juce::Colours::white);

    setColour(juce::DirectoryContentsDisplayComponent::highlightColourId, juce::Colour(0x88ffffff));
    setColour(juce::DirectoryContentsDisplayComponent::textColourId, juce::Colours::white);

    setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TabbedButtonBar::tabTextColourId, juce::Colours::transparentBlack);
    setColour(juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TabbedButtonBar::frontTextColourId, juce::Colours::transparentBlack);

    setColour(juce::TabbedComponent::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);

    /// \note `ScrollBar::trackColourId, lightgrey` stood here and was the whole
    /// of issue #90's contrast complaint: it made the groove the brightest
    /// rectangle in the preset browser and left the white thumb sitting on it
    /// almost invisible. drawScrollbar() paints no groove now, so there is no
    /// track colour to name; the thumb is the only mark a scroll bar makes.
    setColour(juce::ScrollBar::thumbColourId, juce::Colours::white);
}

////////////////////////////////////////////////////////////////////////////////
//
// Theme::drawScrollbar()
// ----------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note \p x, \p y, \p width and \p height are the *thumb area*, which with the
/// step buttons gone is the whole bar. \p thumbSize is zero when there is not
/// room for a thumb worth dragging, which is JUCE's way of saying "draw nothing"
/// -- and nothing is exactly what this then draws, where V2 would still have laid
/// down its slot.
///
////////////////////////////////////////////////////////////////////////////////

void Theme::drawScrollbar(juce::Graphics &graphics, juce::ScrollBar &scrollBar, int const x,
                          int const y, int const width, int const height,
                          bool const isScrollbarVertical, int const thumbStartPosition,
                          int const thumbSize, bool const isMouseOver, bool const isMouseDown)
{
    if (thumbSize <= 0)
        return;

    /// What the thumb keeps clear of the edges of the strip it runs in.
    constexpr int inset{1};

    auto const bar(isScrollbarVertical
                       ? juce::Rectangle<int>(x, thumbStartPosition, width, thumbSize)
                       : juce::Rectangle<int>(thumbStartPosition, y, thumbSize, height));
    auto const thumb(bar.reduced(inset).toFloat());

    /// \note Translucent at rest and solid under the hand: a floating bar has no
    /// frame to separate it from the list it is over, so what keeps it from
    /// reading as content is that it is faint until it is being used.
    auto const opacity(isMouseDown ? 1.0f : (isMouseOver ? 0.8f : 0.45f));
    graphics.setColour(scrollBar.findColour(juce::ScrollBar::thumbColourId).withAlpha(opacity));
    graphics.fillRoundedRectangle(thumb, std::min(thumb.getWidth(), thumb.getHeight()) / 2);
}

/// \note Square rather than V2's `jmin( width, height ) * 2`, which for an eight
/// pixel bar asked for a sixteen pixel thumb -- a capsule where a round dot reads
/// better, and enough of the bar in a short list to make the thing look full.
int Theme::getMinimumScrollbarThumbSize(juce::ScrollBar &scrollBar)
{
    return std::min(scrollBar.getWidth(), scrollBar.getHeight());
}

/// \note Was registerFonts(false), unregistering the .ttf files from the OS.
/// Nothing to undo now.
Theme::~Theme() = default;

void Theme::drawPopupMenuBackground(juce::Graphics &graphics, int const width, int const height)
{
    bool cut = juce::Desktop::canUseSemiTransparentWindows();
    if (cut)
    {
        graphics.setColour(juce::Colour(0x10, 0x10, 0x12).withAlpha(0.9f));
        graphics.fillRoundedRectangle(0.f, 0.f, static_cast<float>(width),
                                      static_cast<float>(height), 10.f);
        graphics.setColour(juce::Colour(0x25, 0x25, 0x35));
        graphics.drawRoundedRectangle(0.f, 0.f, static_cast<float>(width),
                                      static_cast<float>(height), 10.f, 1.f);
    }
    else
    {
        graphics.setColour(juce::Colour(0x10, 0x10, 0x12));
        graphics.fillRect(0.f, 0.f, static_cast<float>(width), static_cast<float>(height));
        graphics.setColour(juce::Colour(0x25, 0x25, 0x35));
        graphics.drawRect(0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 1.f);
    }
}

void Theme::drawTabAreaBehindFrontButton(juce::TabbedButtonBar &, juce::Graphics &, int /*w*/,
                                         int /*h*/)
{
    // Implementation note:
    //   This is necessary to prevent a line being drawn between the tab buttons
    // and the page(s) in our Settings windows which causes an ugly dot to
    // appear.
    //                                        (16.06.2010.) (Domagoj Saric)
}

juce::Drawable const *Theme::getDefaultFolderImage()
{
    if (!folderIcon_)
    {
        if (auto const *const base = juce::LookAndFeel_V2::getDefaultFolderImage())
        {
            folderIcon_ = base->createCopy();
            // Was Image::desaturate(); a Drawable has no equivalent, and a
            // colour overlay is the closest faithful thing.
            folderIcon_->replaceColour(juce::Colours::white, juce::Colours::lightgrey);
        }
    }
    return folderIcon_.get();
}

int Theme::getMenuWindowFlags()
{
    // Implementation note:
    //   The original enabled a drop shadow.
    //                                        (11.05.2010.) (Domagoj Saric)
    return 0;
}

juce::Font Theme::getPopupMenuFont()
{
    // whiteFont_ with the weight taken off: the regular face rather than
    // Font::setBold(false), which does nothing once a typeface is explicit.
    return juce::Font(juce::FontOptions(regularTypeface()).withHeight(12.0f));
}

int selectedOrDraggedThumb(juce::Slider const &slider)
{
    auto const *const pSelectable(dynamic_cast<SliderWithSelectedThumb const *>(&slider));
    return pSelectable ? pSelectable->selectedThumb() : slider.getThumbBeingDragged();
}

void Theme::drawLinearSliderBackground(juce::Graphics &graphics, int const x, int const y,
                                       int const width, int const height, float /*sliderPos*/,
                                       float /*minSliderPos*/, float /*maxSliderPos*/,
                                       juce::Slider::SliderStyle const style,
                                       juce::Slider & /*slider*/)
{
    LE_VERIFY((style == juce::Slider::LinearHorizontal) ||
              (style == juce::Slider::TwoValueHorizontal));
    graphics.setColour(juce::Colours::white);
    graphics.drawHorizontalLine(y + (height / 2), static_cast<float>(x),
                                static_cast<float>(x + width));
}

void Theme::drawLinearSliderThumb(juce::Graphics &graphics, int const /*x*/, int const y,
                                  int const /*width*/, int const height, float const sliderPos,
                                  float const minSliderPos, float const maxSliderPos,
                                  juce::Slider::SliderStyle const style, juce::Slider &slider)
{
    auto const &thumb(resourceArtwork<LFOSliderThumb>());

    auto const activeThumb(selectedOrDraggedThumb(slider));

    switch (style)
    {
    case juce::Slider::LinearHorizontal:
        paintSliderThumb(graphics, thumb, sliderPos, y, height, activeThumb == 0);
        break;

    case juce::Slider::TwoValueHorizontal:
        paintSliderThumb(graphics, thumb, minSliderPos, y, height, activeThumb == 1);
        paintSliderThumb(graphics, thumb, maxSliderPos, y, height, activeThumb == 2);
        break;

    default:
        LE_ASSERT_MSG(false, "Unsupported slider style.");
        break;
    }
}

int Theme::getSliderThumbRadius(juce::Slider &)
{
    return resourceArtwork<LFOSliderThumb>().getWidth() / 2;
}

void Theme::createSingleton() { singleton_.emplace(); }

void Theme::destroySingleton()
{
    singleton_.reset();
    // The Theme holds the two typefaces and the slider thumb; nothing skin
    // related may outlive the JUCE it was made under.
    releaseCachedResources();
}

Theme &Theme::singleton()
{
    LE_ASSERT_MSG(singleton_.has_value(), "Theme used before the GUI was initialised.");
    return *singleton_;
}

bool Theme::haveSingleton() { return singleton_.has_value(); }

} // namespace LE::SW::GUI
