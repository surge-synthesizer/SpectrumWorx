////////////////////////////////////////////////////////////////////////////////
///
/// \file themePage.cpp
/// -------------------
///
///   The LookAndFeel, on the widgets it dresses.
///
///   The sliders are the point. Theme derives from LookAndFeel_V2 rather than
/// V4 because V4 paints a linear slider whole and would never call
/// Theme::drawLinearSliderThumb -- so if the two sliders below show a round
/// skinned thumb, V2 is doing what it should, and if they show JUCE's default
/// blob, the base class is wrong. That failure is silent in every other
/// setting, which is exactly why it is worth a page.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "../page.hpp"

#include "gui/resources.hpp"
#include "gui/theme.hpp"

#include <juce_gui_basics/juce_gui_basics.h>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

namespace GUI = LE::SW::GUI;

class ThemePage final : public juce::Component
{
  public:
    ThemePage()
    {
        if (!GUI::Theme::haveSingleton())
            GUI::Theme::createSingleton();
        setLookAndFeel(&GUI::Theme::singleton()); // children inherit it

        single_.setSliderStyle(juce::Slider::LinearHorizontal);
        single_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        single_.setRange(0.0, 1.0);
        single_.setValue(0.35, juce::dontSendNotification);
        addAndMakeVisible(single_);

        range_.setSliderStyle(juce::Slider::TwoValueHorizontal);
        range_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        range_.setRange(0.0, 1.0);
        range_.setMinAndMaxValues(0.25, 0.75, juce::dontSendNotification);
        addAndMakeVisible(range_);

        button_.setButtonText("Text button");
        addAndMakeVisible(button_);

        toggled_.setButtonText("Toggled");
        toggled_.setToggleState(true, juce::dontSendNotification);
        addAndMakeVisible(toggled_);

        editor_.setText("A juce::TextEditor");
        addAndMakeVisible(editor_);

        label_.setText("A juce::Label", juce::dontSendNotification);
        addAndMakeVisible(label_);

        setSize(820, 470);
    }

    ~ThemePage() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics &graphics) override
    {
        graphics.fillAll(juce::Colour(0xFF101010));

        auto const &theme(GUI::Theme::singleton());

        graphics.setColour(GUI::ColourMap::getColour(GUI::ColourMap::Text));
        graphics.setFont(theme.headingFont());
        graphics.drawText("Theme -- LookAndFeel_V2", 20, 16, 400, 22,
                          juce::Justification::centredLeft);

        graphics.setColour(GUI::ColourMap::getColour(GUI::ColourMap::Accent));
        graphics.setFont(theme.headingFont());
        graphics.drawText("headingFont, ColourMap::Accent", 20, 46, 400, 20,
                          juce::Justification::centredLeft);

        graphics.setColour(GUI::ColourMap::getColour(GUI::ColourMap::Text));
        graphics.setFont(theme.labelFont());
        graphics.drawText("labelFont -- SpectrumWorx 0123456789", 20, 68, 400, 18,
                          juce::Justification::centredLeft);

        graphics.setFont(GUI::Theme::singleton().getPopupMenuFont());
        graphics.drawText("getPopupMenuFont -- the regular face", 20, 88, 400, 18,
                          juce::Justification::centredLeft);

        // The popup menu background, drawn where it can be seen rather than
        // under a menu.
        {
            juce::Graphics::ScopedSaveState const state(graphics);
            graphics.setOrigin(430, 40);
            graphics.setColour(juce::Colours::black);
            GUI::Theme::singleton().drawPopupMenuBackground(graphics, 340, 70);
            graphics.setColour(juce::Colours::lightgrey);
            graphics.setFont(GUI::Theme::singleton().getPopupMenuFont());
            graphics.drawText("drawPopupMenuBackground", 12, 24, 300, 20,
                              juce::Justification::centredLeft);
        }

        graphics.setColour(juce::Colours::grey);
        graphics.setFont(juce::FontOptions(11.0f));
        graphics.drawText("LinearHorizontal -- thumb is SliderThumbPainter", 20, 148, 460, 16,
                          juce::Justification::centredLeft);
        graphics.drawText("TwoValueHorizontal -- two thumbs", 20, 208, 460, 16,
                          juce::Justification::centredLeft);

        drawThumbReference(graphics);
    }

    void resized() override
    {
        single_.setBounds(20, 166, 460, 28);
        range_.setBounds(20, 226, 460, 28);
        button_.setBounds(20, 290, 140, 26);
        toggled_.setBounds(176, 290, 140, 26);
        label_.setBounds(20, 330, 200, 22);
        editor_.setBounds(20, 360, 300, 26);
    }

  private:
    /// The thumb on its own beside the sliders, at rest and at the 5/3 a drag
    /// enlarges it to, so that a mismatch is obvious rather than a matter of
    /// memory.
    static void drawThumbReference(juce::Graphics &graphics)
    {
        graphics.setColour(juce::Colours::grey);
        graphics.setFont(juce::FontOptions(11.0f));
        graphics.drawText("thumb, and dragged:", 480, 172, 120, 16,
                          juce::Justification::centredLeft);

        auto const at([&](float const x, float const scale) {
            auto const w(GUI::SliderThumbStyle::width * scale);
            auto const h(GUI::SliderThumbStyle::height * scale);
            GUI::SliderThumbPainter::paint(graphics, {x, 180 - h / 2, w, h});
        });
        at(606, 1.0f);
        at(620, 5.0f / 3);
    }

    juce::Slider single_, range_;
    juce::TextButton button_, toggled_;
    juce::TextEditor editor_;
    juce::Label label_;
}; // class ThemePage

std::unique_ptr<juce::Component> construct() { return std::make_unique<ThemePage>(); }

SWShowUI::PageRegistration const registration{"theme", "the LookAndFeel, on the widgets it dresses",
                                              &construct};

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------
