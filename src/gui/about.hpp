////////////////////////////////////////////////////////////////////////////////
///
/// \file about.hpp
/// ---------------
///
///   The settings panel's third tab: what this build is, who ported it and who
/// wrote it in the first place.
///
///   It was a single baked bitmap (skin file 20) with a version string drawn
/// over it, which meant the credits could not be corrected, the year could not
/// move and the version could not say anything the 2016 layout had not left room
/// for. It is text and three links now, and every word of it is in one block at
/// the top of about.cpp.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef about_hpp__9F3C21A7_54D8_4B0E_A6C1_7E2B93D45F08
#define about_hpp__9F3C21A7_54D8_4B0E_A6C1_7E2B93D45F08
//------------------------------------------------------------------------------
#include "gui/gui.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>
//------------------------------------------------------------------------------

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class AboutPage
///
////////////////////////////////////////////////////////////////////////////////

/// \brief The About tab, painted rather than blitted.
///
/// \note Still a BackgroundImage, but of the *frame* the other two settings
/// pages use (skin file 17) rather than of an about-specific one. What that
/// buys: the three tabs are the same panel with different contents, which is
/// what they always looked like and now is.
class AboutPage final : public PanelBackground
{
  public:
    AboutPage();
    ~AboutPage() override;

    /// \brief Everything the About page knows, as one block of text.
    ///
    ///   What the "copy info" link puts on the clipboard, and the thing to paste
    /// into a bug report. Public and static because it is a fact about the
    /// binary rather than about the panel: a caller that wants it does not need
    /// an editor open.
    static juce::String information();

    /// \brief The credit lines, exactly as the page draws them.
    ///
    /// \note Public for one test, and that test earns it: these are the only
    /// non-ASCII strings in the tree that reach a screen, and the ways they go
    /// wrong -- a compiler that re-encodes the literals, a juce::String built
    /// through the ASCII constructor -- both produce a page that renders, so
    /// nothing but an assertion about the code points can see them.
    /// \see tests/gui/aboutPageTests.cpp.
    static std::vector<juce::String> originalAuthors();

  private: // juce::Component overrides
    void paint(juce::Graphics &) override;

  private:
    /// The three links under the version block. \see Content::links in the .cpp.
    enum LinkIndex
    {
        manualLink,
        sourceLink,
        copyInfoLink,
        numberOfLinks
    };

    class Link;

    void addLink(LinkIndex, char const *text, char const *flashText, std::function<void()> onClick);

    void copyInformation();

  private:
    /// \note Pointers because Link is defined in the .cpp -- the whole point of
    /// this file is that the content and its layout are editable in one place,
    /// and a widget declared here would drag half of it back into a header.
    std::array<std::unique_ptr<Link>, numberOfLinks> links_;
}; // class AboutPage

} // namespace LE::SW::GUI

//------------------------------------------------------------------------------
#endif // about_hpp
