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

/// \note Drawn at 5/3 while dragged, which is what the enlarge flag means -- it
/// was a bitmap blown up by Artwork::drawScaled() and is a drawing asked for at
/// a bigger size now, so the enlarged one is no longer soft.
void paintSliderThumb(juce::Graphics &graphics, float const position,
                      int const sliderVerticalPosition, int const sliderHeight, bool const enlarge)
{
    auto const scale(enlarge ? 5.0f / 3 : 1.0f);
    auto const width(SliderThumbStyle::width * scale);
    auto const height(SliderThumbStyle::height * scale);

    SliderThumbPainter::paint(
        graphics, juce::Rectangle<float>(position - width / 2,
                                         sliderVerticalPosition + (sliderHeight - height) / 2,
                                         width, height));
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
    : headingFont_(juce::FontOptions(regularTypeface()).withHeight(14.0f)),
      labelFont_(juce::FontOptions(regularTypeface()).withHeight(12.0f)),
      palette_(ColourMap::generation())
{
    setDefaultSansSerifTypeface(regularTypeface());
    takeColours();
}

void Theme::reloadColours()
{
    if (palette_ == ColourMap::generation())
        return;

    palette_ = ColourMap::generation();
    takeColours();

    /// \note And the folder icon, which is not a colour but has two of them
    /// baked into it. Rebuilt on the next ask. \see getDefaultFolderImage().
    folderIcon_.reset();
}

void Theme::takeColours()
{
    /// \note Every colour here comes out of ColourMap, and the ones that are
    /// not there are transparent -- an absence rather than a choice. \see
    /// colourMap.hpp.
    auto const colour([](ColourMap::Name const name) { return ColourMap::getColour(name); });

    setColour(juce::PopupMenu::textColourId, colour(ColourMap::TextDimmed));
    setColour(juce::PopupMenu::headerTextColourId, colour(ColourMap::Text));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colour(ColourMap::Transparent));
    setColour(juce::PopupMenu::highlightedTextColourId, colour(ColourMap::Text));
    setColour(juce::PopupMenu::backgroundColourId,
              colour(ColourMap::MenuBackground).withAlpha(0.8f));

    setColour(juce::TextButton::buttonColourId, colour(ColourMap::Ground));
    setColour(juce::TextButton::buttonOnColourId, colour(ColourMap::Accent));
    setColour(juce::TextButton::textColourOnId, colour(ColourMap::Text));
    setColour(juce::TextButton::textColourOffId, colour(ColourMap::Text));

    setColour(juce::AlertWindow::backgroundColourId, colour(ColourMap::AlertBackground));
    setColour(juce::AlertWindow::textColourId, colour(ColourMap::Text));

    setColour(juce::ComboBox::backgroundColourId, colour(ColourMap::Ground));
    setColour(juce::ComboBox::buttonColourId, colour(ColourMap::TextDimmed));
    setColour(juce::ComboBox::textColourId, colour(ColourMap::Text));

    setColour(juce::Label::backgroundColourId, colour(ColourMap::Transparent));
    setColour(juce::Label::outlineColourId, colour(ColourMap::Transparent));
    setColour(juce::Label::textColourId, colour(ColourMap::Text));

    setColour(juce::TextEditor::backgroundColourId, colour(ColourMap::FieldBackground));
    setColour(juce::TextEditor::focusedOutlineColourId, colour(ColourMap::Transparent));
    setColour(juce::TextEditor::outlineColourId, colour(ColourMap::Transparent));
    setColour(juce::TextEditor::highlightColourId, colour(ColourMap::Accent));
    setColour(juce::TextEditor::highlightedTextColourId, colour(ColourMap::Text));
    setColour(juce::TextEditor::textColourId, colour(ColourMap::Text));
    setColour(juce::CaretComponent::caretColourId, colour(ColourMap::Accent));

    setColour(juce::ListBox::backgroundColourId, colour(ColourMap::ListBackground));
    setColour(juce::ListBox::outlineColourId, colour(ColourMap::ListOutline));
    setColour(juce::ListBox::textColourId, colour(ColourMap::Text));

    setColour(juce::DirectoryContentsDisplayComponent::highlightColourId,
              colour(ColourMap::ListHighlight));
    setColour(juce::DirectoryContentsDisplayComponent::textColourId, colour(ColourMap::Text));

    setColour(juce::TabbedButtonBar::tabOutlineColourId, colour(ColourMap::Transparent));
    setColour(juce::TabbedButtonBar::tabTextColourId, colour(ColourMap::Transparent));
    setColour(juce::TabbedButtonBar::frontOutlineColourId, colour(ColourMap::Transparent));
    setColour(juce::TabbedButtonBar::frontTextColourId, colour(ColourMap::Transparent));

    setColour(juce::TabbedComponent::backgroundColourId, colour(ColourMap::Transparent));
    setColour(juce::TabbedComponent::outlineColourId, colour(ColourMap::Transparent));

    /// \note `ScrollBar::trackColourId, lightgrey` stood here and was the whole
    /// of issue #90's contrast complaint: it made the groove the brightest
    /// rectangle in the preset browser and left the white thumb sitting on it
    /// almost invisible. drawScrollbar() paints no groove now, so there is no
    /// track colour to name; the thumb is the only mark a scroll bar makes.
    setColour(juce::ScrollBar::thumbColourId, colour(ColourMap::ScrollBarThumb));
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
        graphics.setColour(ColourMap::getColour(ColourMap::MenuBackground).withAlpha(0.9f));
        graphics.fillRoundedRectangle(0.f, 0.f, static_cast<float>(width),
                                      static_cast<float>(height), 10.f);
        graphics.setColour(ColourMap::getColour(ColourMap::MenuOutline));
        graphics.drawRoundedRectangle(0.f, 0.f, static_cast<float>(width),
                                      static_cast<float>(height), 10.f, 1.f);
    }
    else
    {
        graphics.setColour(ColourMap::getColour(ColourMap::MenuBackground));
        graphics.fillRect(0.f, 0.f, static_cast<float>(width), static_cast<float>(height));
        graphics.setColour(ColourMap::getColour(ColourMap::MenuOutline));
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
            folderIcon_->replaceColour(ColourMap::getColour(ColourMap::Text),
                                       ColourMap::getColour(ColourMap::TextDimmed));
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
    // labelFont_ with the weight taken off: the regular face rather than
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
    graphics.setColour(ColourMap::getColour(ColourMap::SliderTrack));
    graphics.drawHorizontalLine(y + (height / 2), static_cast<float>(x),
                                static_cast<float>(x + width));
}

void Theme::drawLinearSliderThumb(juce::Graphics &graphics, int const /*x*/, int const y,
                                  int const /*width*/, int const height, float const sliderPos,
                                  float const minSliderPos, float const maxSliderPos,
                                  juce::Slider::SliderStyle const style, juce::Slider &slider)
{
    auto const activeThumb(selectedOrDraggedThumb(slider));

    switch (style)
    {
    case juce::Slider::LinearHorizontal:
        paintSliderThumb(graphics, sliderPos, y, height, activeThumb == 0);
        break;

    case juce::Slider::TwoValueHorizontal:
        paintSliderThumb(graphics, minSliderPos, y, height, activeThumb == 1);
        paintSliderThumb(graphics, maxSliderPos, y, height, activeThumb == 2);
        break;

    default:
        LE_ASSERT_MSG(false, "Unsupported slider style.");
        break;
    }
}

int Theme::getSliderThumbRadius(juce::Slider &) { return SliderThumbStyle::width / 2; }

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
