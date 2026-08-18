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

/// The line under the version block. The year is the port's, not the original's.
char const *const portedBy[]{"Ported by Surge Synth Team, 2026"};

char const *const originalAuthorsHeading{"Original Authors:"};

/// Verbatim from the "Credits" panel of the bitmap this page replaced, which is
/// the only place they were ever written down.
char const *const originalAuthors[]{
    "Alexey Menshikov (original idea)",    //
    "Domagoj Šarić (programming)",         //
    "Ivan Dokmanić (DSP expertise)",       //
    "Matija Bošnjaković (graphic design)", //
    "Scot Solida (documentation)",         //
    "Danijel Domazet (ćevapi i luk)",      //
};

/// \note The first line is kept from the bitmap deliberately: the port is
/// GPL-3.0-or-later over Little Endian's 2016 release, and a notice that was on
/// screen for ten years is not something to drop as a side effect of retyping
/// the panel. The second is the port's own, and says the same thing the source
/// headers do -- who holds it is a question the log answers, not this page.
char const *const copyright[]{
    "© 2006 - 2014 Little Endian",    //
    "© 2024 - 20xx Surge Synth Team", //
};

/// The manual is in the repository rather than on a documentation site; this is
/// the folder it lives in. Point it somewhere better the moment there is one.
char const *const manualURL{
    "https://github.com/surge-synthesizer/SpectrumWorx/tree/main/doc/manual"};
char const *const sourceURL{"https://github.com/surge-synthesizer/SpectrumWorx"};

char const *const manualLinkText{"Manual"};
char const *const sourceLinkText{"Source"};
char const *const copyInfoLinkText{"Copy Info"};
/// What the copy link says for a moment after it has been clicked.
char const *const copiedLinkText{"Copied!"};
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
int constexpr margin{14};
int constexpr indent{10}; // the credits, under their heading

/// One line of body text, ascent to next ascent.
int constexpr lineHeight{14};

int constexpr titleY{18};
int constexpr titleHeight{20};

int constexpr versionY{titleY + titleHeight + 2};
/// \see versionLines() below, which returns exactly this many.
int constexpr versionLineCount{3};

int constexpr linksY{versionY + versionLineCount * lineHeight + 8};
int constexpr linksHeight{14};
int constexpr linkGap{12};

int constexpr portedByY{linksY + linksHeight + 28};
int constexpr portedByLineCount{static_cast<int>(std::size(Content::portedBy))};

int constexpr copyrightY{portedByY + portedByLineCount * lineHeight + 8};
int constexpr copyrightLineCount{static_cast<int>(std::size(Content::copyright))};

int constexpr authorsHeadingY{copyrightY + copyrightLineCount * lineHeight + 28};
int constexpr authorsY{authorsHeadingY + lineHeight + 10};
} // namespace Layout

////////////////////////////////////////////////////////////////////////////////

namespace
{
juce::Font titleFont() { return juce::Font(juce::FontOptions(boldTypeface()).withHeight(17.0f)); }
juce::Font headingFont() { return Theme::singleton().headingFont(); }
juce::Font bodyFont() { return DrawableText::defaultFont(); }

/// \brief One line of Content, as a juce::String.
///
/// \note And this is the only way any of it may become one. `juce::String(char
/// const *)` is *ASCII*: it asserts on any byte over 127 and takes the rest as
/// Latin-1, so four of the six credits and the copyright sign came out mangled
/// and noisy. The literals above stay readable UTF-8 -- with `/utf-8` on MSVC
/// keeping them that way through the compiler -- and this says so to JUCE.
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
/// tree rather than leaving the field empty, so an emptiness test is not enough
/// and the About page announced `Version 3.0.0 (-no-tag-)` until this existed.
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

    juce::String release("Version ");
    release << VersionInformation::project_version;
    if (auto const tag(releaseTag()); tag.isNotEmpty())
        release << " (" << tag << ")";

    juce::String source(VersionInformation::git_branch);
    source << " @ " << VersionInformation::git_commit_hash;

    juce::String built("Built ");
    built << BuildStamp::date << " " << BuildStamp::time;

    return {release, source, built};
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

    graphics.setColour(ColourMap::getColour(ColourMap::Blue).withAlpha(alpha));
    graphics.setFont(font());
    graphics.drawText(asText(flashing_ ? flashText_ : text_), getLocalBounds(),
                      juce::Justification::centredLeft);

    if (isMouseOverButton && !flashing_)
        graphics.fillRect(0, getHeight() - 1, getWidth(), 1);
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
    addLink(sourceLink, Content::sourceLinkText, nullptr,
            [] { juce::URL(Content::sourceURL).launchInDefaultBrowser(); });
    addLink(copyInfoLink, Content::copyInfoLinkText, Content::copiedLinkText,
            [this] { copyInformation(); });
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
    graphics.setFont(titleFont());
    graphics.drawFittedText(asText(Content::title), Layout::margin, Layout::titleY,
                            width - 2 * Layout::margin, Layout::titleHeight,
                            juce::Justification::centredLeft, 1);

    {
        auto y(Layout::versionY);
        for (auto const &line : versionLines())
        {
            drawLine(graphics, line, y, bodyFont(), ColourMap::getColour(ColourMap::TextDimmed),
                     width);
            y += Layout::lineHeight;
        }
    }

    {
        auto y(Layout::portedByY);
        for (auto const *const line : Content::portedBy)
        {
            drawLine(graphics, asText(line), y, bodyFont(), ColourMap::getColour(ColourMap::Text),
                     width);
            y += Layout::lineHeight;
        }
    }

    {
        auto y(Layout::copyrightY);
        for (auto const *const line : Content::copyright)
        {
            drawLine(graphics, asText(line), y, bodyFont(),
                     ColourMap::getColour(ColourMap::TextFaint), width);
            y += Layout::lineHeight;
        }
    }

    drawLine(graphics, asText(Content::originalAuthorsHeading), Layout::authorsHeadingY,
             headingFont(), ColourMap::getColour(ColourMap::Blue), width);

    {
        auto y(Layout::authorsY);
        for (auto const &author : originalAuthors())
        {
            drawLine(graphics, author, y, bodyFont(), ColourMap::getColour(ColourMap::Text), width,
                     Layout::indent);
            y += Layout::lineHeight;
        }
    }
}

} // namespace LE::SW::GUI
