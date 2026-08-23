////////////////////////////////////////////////////////////////////////////////
///
/// \file about.cpp
/// ---------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "about.hpp"

#include "resources.hpp"
#include "theme.hpp"

#include "configuration/buildStamp.hpp"

#include <sst/plugininfra/version_information.h>

#include <algorithm>
#include <iterator>
//------------------------------------------------------------------------------

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
//
// The content
// -----------
//
//   Everything the About page says is in this namespace, and paint() below says
// nothing that is not. To correct a credit, add a link or move the year, edit
// here and nowhere else.
//
// \note The source file is UTF-8 and several of these names need it. The MSVC
// `/utf-8` switch in the top-level CMakeLists is what makes that true of the
// *execution* charset as well -- without it these come out as mojibake on
// Windows and only on Windows.
//
////////////////////////////////////////////////////////////////////////////////

namespace Content
{
char const *const title{"SpectrumWorx"};

char const *const originalAuthorsHeading{"Original Authors"};

/// Verbatim from the "Credits" panel of the bitmap this page replaced, which is
/// the only place they were ever written down.
char const *const originalAuthors[]{
    "Concept: Alexey Menshikov",    //
    "Programming: Domagoj Šarić",   //
    "DSP: Ivan Dokmanić",           //
    "Graphics: Matija Bošnjaković", //
    "Documentation: Scot Solida",   //
    "Direction: Danijel Domazet",   //
};

/// The manual is in the repository rather than on a documentation site; this is
/// the folder it lives in. Point it somewhere better the moment there is one.
char const *const manualURL{
    "https://github.com/surge-synthesizer/SpectrumWorx/tree/main/doc/manual"};

char const *const manualLinkText{"Manual"};
char const *const copyInfoLinkText{"Copy Info"};
char const *const copiedLinkText{"Copied!"};

/// \note Order matches the sprite in aboutIconsArtwork() (\see resources.hpp)
/// left to right, then wraps into the second row -- \see AboutPage::IconIndex,
/// which the two arrays below are indexed by.
char const *const iconLabels[]{
    "Source Code", "Our Discord", "GPL v3", "JUCE", "CLAP", "Audio Units", "VST3", "ASIO",
};

char const *const iconURLs[]{
    "https://github.com/surge-synthesizer/SpectrumWorx",
    "https://discord.gg/aFQDdMV",
    "https://www.gnu.org/licenses/gpl-3.0-standalone.html",
    "https://juce.com",
    "https://cleveraudio.org",
    "https://developer.apple.com/documentation/audiounit",
    "https://www.steinberg.net/technology/",
    "https://www.steinberg.net/technology/",
};
} // namespace Content

////////////////////////////////////////////////////////////////////////////////
//
// The layout
// ----------
//
//   Every position is expressed relative to the one above it, so inserting or
// removing a line means changing the count beside it and nothing else. The page
// itself is 191 x 347 -- the settings frame, skin file 17.
//
////////////////////////////////////////////////////////////////////////////////

namespace Layout
{
int constexpr margin{32};

/// One line of body text, ascent to next ascent.
int constexpr lineHeight{21};

int constexpr titleY{41};
int constexpr titleWidth{165};
int constexpr titleHeight{30};

int constexpr versionY{titleY + titleHeight + 3};
/// \see versionLines() below, which returns exactly this many.
int constexpr versionLineCount{3};

int constexpr linksY{versionY + versionLineCount * lineHeight + 12};
int constexpr linksHeight{21};
int constexpr linkGap{88};

int constexpr authorsHeadingY{linksY + 177};
int constexpr authorsY{authorsHeadingY + lineHeight + 6};

/// The icon row sits in the gap between the links row and the Original Authors
/// heading -- \see AboutPage::IconIndex. It's two rows of four, each icon 36px
/// square (\see resources.hpp / the SVG behind aboutIconsArtwork()), with a
/// caption strip below each row that's blank except while a mouse is over one
/// of that row's icons.
int constexpr iconSize{36};
int constexpr iconGap{iconSize + 24};
int constexpr iconsPerRow{4};
int constexpr iconLabelHeight{18};

/// Wider than iconSize on purpose -- captions like "Audio Units" don't fit in
/// 36px. \see AboutPage::paint(), which is where this actually gets used.
int constexpr iconCaptionWidth{72};

/// Centred top and bottom: (gap - 2 icon rows - 2 caption strips) split evenly.
int constexpr iconsTopMargin{
    (authorsHeadingY - (linksY + linksHeight) - 2 * iconSize - 2 * iconLabelHeight) / 2};
int constexpr iconRow1Y{linksY + linksHeight + iconsTopMargin + 4};
int constexpr iconRow2Y{iconRow1Y + iconSize + iconLabelHeight + 6};
} // namespace Layout

////////////////////////////////////////////////////////////////////////////////

namespace
{
juce::Font titleFont() { return juce::Font(juce::FontOptions(boldTypeface()).withHeight(25.5f)); }
juce::Font headingFont() { return Theme::singleton().headingFont(); }
juce::Font bodyFont() { return DrawableText::defaultFont(); }
juce::Font iconLabelFont() { return bodyFont().withHeight(12.0f); }

/// \brief One line of Content, as a juce::String.
juce::String asText(char const *const utf8) { return juce::String(juce::CharPointer_UTF8(utf8)); }

/// \brief Draws one line at \p y, shrinking it to fit rather than clipping it.
///
/// \note drawFittedText rather than drawText: these are proper names and URLs
/// typed by hand into the block above, and a credit that silently loses its last
/// word is a worse failure than one drawn a point small.
void drawLine(juce::Graphics &graphics, juce::String const &text, int const y,
              juce::Font const &font, juce::Colour const colour, int const pageWidth,
              int const indent = 0)
{
    graphics.setColour(colour);
    graphics.setFont(font);
    graphics.drawFittedText(text, Layout::margin + indent, y,
                            pageWidth - 2 * Layout::margin - indent, Layout::lineHeight,
                            juce::Justification::centredLeft, 1);
}

/// \brief The git tag this was built from, or empty when there is none.
///
/// \note sst-plugininfra writes the literal string "-no-tag-" for an untagged
/// tree rather than leaving the field empty, so an emptiness test is not enough.
juce::String releaseTag()
{
    juce::String const tag(sst::plugininfra::VersionInformation::git_tag);
    return (tag == "-no-tag-") ? juce::String() : tag;
}

/// \brief What this binary is, in the three lines the layout leaves room for.
///
/// \note Both sources, and they answer different questions. sst-plugininfra's
/// VersionInformation is written when CMake configures -- it is the release this
/// tree *is* -- and BuildStamp is written by every build, so it is the binary
/// actually running. \see configuration/buildStamp.hpp.
std::array<juce::String, Layout::versionLineCount> versionLines()
{
    using sst::plugininfra::VersionInformation;

    juce::String sst("Restored by Surge Synth Team");

    juce::String release("Version ");
    release << VersionInformation::project_version << "." << VersionInformation::git_branch << "."
            << VersionInformation::git_commit_hash;

    // if (auto const tag(releaseTag()); tag.isNotEmpty())
    //     release << " (" << tag << ")";

    juce::String built("Built on ");
    built << BuildStamp::date << " @ " << BuildStamp::time;

    return {sst, release, built};
}
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
///
/// \class AboutPage::Link
///
////////////////////////////////////////////////////////////////////////////////

/// \brief A word that can be clicked: the skin's blue, underlined under the
/// mouse.
///
/// \note Not GUI::TextButton, which is a *toggle* -- it paints itself at 30%
/// alpha until it is switched on, which is right for the LFO's quarter/triplet
/// and wrong for something that only ever fires once. Not a BitmapButton either;
/// that is what this replaces, and it needed two skin files per word.
class AboutPage::Link final : public juce::Button
{
  public:
    Link(char const *text, char const *flashText);

    /// \brief Shows the flash text for a moment. \see AboutPage::copyInformation.
    void flash();

    /// The width the row should give it: enough for either of its two texts, so
    /// that flashing cannot make it clip or shove its neighbours.
    int naturalWidth() const;

    static juce::Font font() { return bodyFont(); }

  private: // juce::Button overrides
    void paintButton(juce::Graphics &, bool isMouseOverButton, bool isButtonDown) override;

  private:
    char const *const text_;
    char const *const flashText_;
    bool flashing_{false};
}; // class AboutPage::Link

AboutPage::Link::Link(char const *const text, char const *const flashText)
    : juce::Button(asText(text)), text_(text), flashText_(flashText)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    /// Nothing here takes typing, and a link that had grabbed focus would eat
    /// the arrow keys a knob wants.
    setWantsKeyboardFocus(false);
}

int AboutPage::Link::naturalWidth() const
{
    auto const width([](char const *const text) {
        return juce::GlyphArrangement::getStringWidthInt(font(), asText(text));
    });
    return std::max(width(text_), flashText_ ? width(flashText_) : 0);
}

void AboutPage::Link::flash()
{
    flashing_ = true;
    repaint();

    juce::Timer::callAfterDelay(1500, [self = juce::Component::SafePointer<Link>(this)] {
        if (!self)
            return;
        self->flashing_ = false;
        self->repaint();
    });
}

void AboutPage::Link::paintButton(juce::Graphics &graphics, bool const isMouseOverButton,
                                  bool const isButtonDown)
{
    auto const alpha(isButtonDown ? 0.6f : (isMouseOverButton ? 1.0f : 0.8f));

    graphics.setColour(ColourMap::getColour(ColourMap::Accent).withAlpha(alpha));
    graphics.setFont(font());
    graphics.drawText(asText(flashing_ ? flashText_ : text_), getLocalBounds(),
                      juce::Justification::centred);

    if (isMouseOverButton && !flashing_)
        graphics.fillRect(0, getHeight() - 2, getWidth(), 2);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \class AboutPage::IconLink
///
////////////////////////////////////////////////////////////////////////////////

/// \brief One icon out of the aboutIconsArtwork() sprite: white, ColourMap::Accent
/// under the mouse, with a caption that only appears while it is hovered.
///
/// \note aboutIconsArtwork() is a single Drawable covering all
/// AboutPage::numberOfIconLinks icons in a row, 36px each, edge to edge with no
/// padding between them (\see resources.hpp). Rather than a Drawable per icon,
/// each IconLink clips a 36px window out of the shared sprite -- \see
/// spriteWindow(). That only lines up if the sprite really is exactly
/// numberOfIconLinks * Layout::iconSize wide; if the SVG is ever re-exported
/// with different spacing, this is the assumption to fix.
class AboutPage::IconLink final : public juce::Button
{
  public:
    IconLink(int index, char const *label);

    /// \brief Told when the mouse enters or leaves this icon, so AboutPage can
    /// track which one (if any) to show a caption under. \see
    /// AboutPage::addIconLink and the hoveredIcon_ member it sets.
    std::function<void(bool hovering)> onHover;

  private: // juce::Button overrides
    void paintButton(juce::Graphics &, bool isMouseOverButton, bool isButtonDown) override;

  private: // juce::Component overrides
    void mouseEnter(juce::MouseEvent const &) override;
    void mouseExit(juce::MouseEvent const &) override;

  private:
    juce::Rectangle<float> spriteWindow() const;
    int const index_;
}; // class AboutPage::IconLink

AboutPage::IconLink::IconLink(int const index, char const *const label)
    : juce::Button(asText(label)), index_(index)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(false);
}

juce::Rectangle<float> AboutPage::IconLink::spriteWindow() const
{
    auto constexpr spriteWidth(numberOfIconLinks * Layout::iconSize);
    return {static_cast<float>(-(index_ * Layout::iconSize)), 0.0f, static_cast<float>(spriteWidth),
            static_cast<float>(Layout::iconSize)};
}

void AboutPage::IconLink::mouseEnter(juce::MouseEvent const &event)
{
    juce::Button::mouseEnter(event);
    if (onHover)
        onHover(true);
}

void AboutPage::IconLink::mouseExit(juce::MouseEvent const &event)
{
    juce::Button::mouseExit(event);
    if (onHover)
        onHover(false);
}

void AboutPage::IconLink::paintButton(juce::Graphics &graphics, bool const isMouseOverButton,
                                      bool const)
{
    aboutIconsArtwork(isMouseOverButton, false).drawWithin(graphics, spriteWindow());
}

////////////////////////////////////////////////////////////////////////////////
///
/// \class AboutPage
///
////////////////////////////////////////////////////////////////////////////////

AboutPage::AboutPage() : PanelBackground(SettingsPage)
{
    setName("About");

    addLink(manualLink, Content::manualLinkText, nullptr,
            [] { juce::URL(Content::manualURL).launchInDefaultBrowser(); });
    addLink(copyInfoLink, Content::copyInfoLinkText, Content::copiedLinkText,
            [this] { copyInformation(); });

    /// Content::iconLabels / Content::iconURLs are in the same order as
    /// IconIndex, so the index doubles as the array index for both.
    for (int i(0); i < numberOfIconLinks; ++i)
        addIconLink(static_cast<IconIndex>(i), Content::iconURLs[i], Content::iconLabels[i]);
}

/// Out of line: Link is only complete here. \see the note on links_.
AboutPage::~AboutPage() = default;

void AboutPage::addLink(LinkIndex const index, char const *const text, char const *const flashText,
                        std::function<void()> onClick)
{
    auto &link(links_[index]);
    LE_ASSERT(!link);
    link = std::make_unique<Link>(text, flashText);
    link->onClick = std::move(onClick);

    /// Laid out left to right from whatever the one before it ended at, so that
    /// renaming a link -- or adding a fourth -- needs no new coordinates.
    auto const previous(index > 0 ? links_[index - 1].get() : nullptr);
    auto const x(previous ? previous->getRight() + Layout::linkGap : Layout::margin);

    link->setBounds(x, Layout::linksY, link->naturalWidth(), Layout::linksHeight);
    addAndMakeVisible(*link);
}

void AboutPage::addIconLink(IconIndex const index, char const *const url, char const *const label)
{
    auto &icon(iconLinks_[index]);
    LE_ASSERT(!icon);
    icon = std::make_unique<IconLink>(index, label);
    icon->onClick = [url] { juce::URL(url).launchInDefaultBrowser(); };
    icon->onHover = [this, index](bool const hovering) {
        hoveredIcon_ = hovering ? index : -1;
        repaint();
    };

    /// Four per row, spaced edge to edge across the page -- \see Layout::
    /// iconRow1Y / iconRow2Y for the (equally fixed) vertical placement. This
    /// page doesn't resize, so unlike the icon row's own spacing, there's no
    /// need to recompute this if the window changes -- it never does.
    using namespace Layout;
    auto const column(index % iconsPerRow);
    auto const row(index / iconsPerRow);
    auto const x(margin + juce::roundToInt(column * (iconGap)));
    auto const y(row == 0 ? iconRow1Y : iconRow2Y);

    icon->setBounds(x, y, iconSize, iconSize);
    addAndMakeVisible(*icon);
}

std::vector<juce::String> AboutPage::originalAuthors()
{
    std::vector<juce::String> authors;
    authors.reserve(std::size(Content::originalAuthors));
    for (auto const *const author : Content::originalAuthors)
        authors.push_back(asText(author));
    return authors;
}

void AboutPage::copyInformation()
{
    juce::SystemClipboard::copyTextToClipboard(information());
    links_[copyInfoLink]->flash();
}

juce::String AboutPage::information()
{
    using sst::plugininfra::VersionInformation;

    auto const tag(releaseTag());

    juce::String information;
    information << Content::title << " " << VersionInformation::project_version << "\n";
    information << "Release:  " << (tag.isNotEmpty() ? tag : juce::String("untagged, "))
                << VersionInformation::git_implied_display_version << "\n";
    information << "Commit:   " << VersionInformation::git_commit_hash << " ("
                << VersionInformation::git_branch << ")\n";
    information << "Built:    " << BuildStamp::date << " " << BuildStamp::time << " from "
                << BuildStamp::commit << "\n";
    information << "Packaged: " << VersionInformation::build_date << " "
                << VersionInformation::build_time << " UTC\n";
    information << "Compiler: " << VersionInformation::cmake_compiler << "\n";
    information << "System:   " << VersionInformation::cmake_system_name << "\n";
    return information;
}

////////////////////////////////////////////////////////////////////////////////
//
// AboutPage::paint()
// ------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   Top to bottom, one block per line of Layout. The links are child components
/// and paint themselves; everything else is here.
///
////////////////////////////////////////////////////////////////////////////////

void AboutPage::paint(juce::Graphics &graphics)
{
    PanelBackground::paint(graphics);

    auto const width(getWidth());

    graphics.setColour(ColourMap::getColour(ColourMap::Text));
    using namespace Layout;
    graphics.setFont(titleFont());

    logoFullArtwork().drawWithin(graphics, {margin, titleY, titleWidth, titleHeight});

    {
        auto y(Layout::versionY);
        for (auto const &line : versionLines())
        {
            drawLine(graphics, line, y, bodyFont(), ColourMap::getColour(ColourMap::TextDimmed),
                     width);
            y += Layout::lineHeight;
        }
    }

    if (hoveredIcon_ >= 0)
    {
        auto const icon(iconLinks_[hoveredIcon_]->getBounds());
        auto const xPos(icon.getX() + (Layout::iconSize - Layout::iconCaptionWidth) / 2);

        graphics.setColour(ColourMap::getColour(ColourMap::Accent));
        graphics.setFont(iconLabelFont());
        graphics.drawText(asText(Content::iconLabels[hoveredIcon_]), xPos, icon.getBottom(),
                          Layout::iconCaptionWidth, Layout::iconLabelHeight,
                          juce::Justification::centred);
    }

    drawLine(graphics, asText(Content::originalAuthorsHeading), Layout::authorsHeadingY,
             headingFont(), ColourMap::getColour(ColourMap::Accent), width);

    {
        auto y(Layout::authorsY);
        for (auto const &author : originalAuthors())
        {
            drawLine(graphics, author, y, bodyFont(), ColourMap::getColour(ColourMap::Text), width);
            y += Layout::lineHeight;
        }
    }
}

} // namespace LE::SW::GUI
