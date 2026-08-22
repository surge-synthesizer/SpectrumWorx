////////////////////////////////////////////////////////////////////////////////
///
/// \file about.hpp
/// ---------------
///
///   The settings panel's third tab: what this build is, who ported it and who
/// wrote it in the first place.
///
///   Text and three links rather than a baked bitmap, so the credits can be
/// corrected and the version can say whatever it needs to. Every word of it is in
/// one block at the top of about.cpp.
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

class AboutPage final : public PanelBackground
{
  public:
    AboutPage();
    ~AboutPage() override;

    static juce::String information();
    static std::vector<juce::String> originalAuthors();

  private: // juce::Component overrides
    void paint(juce::Graphics &) override;

  private:
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
