////////////////////////////////////////////////////////////////////////////////
///
/// \file knobsPage.cpp
/// -------------------
///
///   Both knobs, swept across their range.
///
///   They are drawn rather than blitted as of 14/15.08.2026, and what that
/// replaced was five film strips of 127 frames each -- so the artwork was its
/// own contact sheet and anyone could see the whole travel at once. This is that
/// contact sheet for the paint code: the editor knob, then the module knob in
/// both polarities and both sizes, and the two states that are not a value at
/// all (focused, and LFO-driven, where the knob's own value says nothing and the
/// wedge goes away).
///
///   The editor pages cannot show this. A knob there sits wherever its
/// parameter's default put it -- which for the symmetric ones is dead centre,
/// the one frame where the wedge is a hairline and says least -- and nothing
/// headless can drag it somewhere else.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "../page.hpp"

#include "gui/modules/moduleUI.hpp"
#include "gui/theme.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <iterator>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

namespace GUI = LE::SW::GUI;

class KnobsPage final : public juce::Component
{
  public:
    KnobsPage()
    {
        if (!GUI::Theme::haveSingleton())
            GUI::Theme::createSingleton();
        setSize(margin * 2 + steps * cell, margin * 2 + rows * cell + headerHeight);
    }

    void paint(juce::Graphics &graphics) override
    {
        // The module panel's own background, so the rims and the focus halo are
        // read against what they will actually sit on.
        graphics.fillAll(juce::Colour(0xFF232323));

        label(graphics, "The editor's knobs, painted", margin, margin, 15.0f, juce::Colours::white);

        int y(margin + headerHeight);
        for (auto const &row : rows_)
        {
            label(graphics, row.caption, margin, y - 14, 11.0f, juce::Colours::grey);
            for (unsigned int step(0); step < steps; ++step)
            {
                auto const value(static_cast<float>(step) / (steps - 1));
                juce::Rectangle<float> const face(
                    static_cast<float>(margin + int(step) * cell + (cell - int(row.diameter)) / 2),
                    static_cast<float>(y + (cell - int(row.diameter)) / 2),
                    static_cast<float>(row.diameter), static_cast<float>(row.diameter));
                if (row.editor)
                    GUI::paintEditorKnob(graphics, face, value);
                else
                    GUI::paintModuleKnob(graphics, face, value, row.bipolar, row.drawWedge,
                                         row.selected);
            }
            y += cell;
        }
    }

  private:
    static constexpr unsigned int steps{9}; ///< values across, 0 to 1 inclusive
    static constexpr int cell{68};
    static constexpr int margin{20};
    static constexpr int headerHeight{40};

    struct Row
    {
        char const *caption;
        unsigned int diameter;
        bool editor;  ///< an EditorKnob rather than a ModuleKnob
        bool bipolar; ///< module knobs only, as are the two below
        bool drawWedge;
        bool selected;
    }; // struct Row

    static constexpr Row rows_[]{
        {"EditorKnob, 55 px (in, out and mix)", GUI::EditorKnob::diameter, true, false, false,
         false},
        {"ModuleKnob unipolar, 51 px", GUI::ModuleKnob::diameter, false, false, true, false},
        {"ModuleKnob bipolar, 51 px", GUI::ModuleKnob::diameter, false, true, true, false},
        {"ModuleKnob unipolar, 51 px, focused", GUI::ModuleKnob::diameter, false, false, true,
         true},
        {"LFO driven -- no wedge, whatever the value", GUI::ModuleKnob::diameter, false, false,
         false, false},
        {"ModuleKnob unipolar, 23 px (gain and wet)", GUI::ModuleKnob::smallDiameter, false, false,
         true, false},
        {"ModuleKnob bipolar, 23 px, focused", GUI::ModuleKnob::smallDiameter, false, true, true,
         true},
    };
    static constexpr int rows{int(std::size(rows_))};

    static void label(juce::Graphics &graphics, juce::String const &text, int const x, int const y,
                      float const height, juce::Colour const colour)
    {
        graphics.setColour(colour);
        graphics.setFont(juce::FontOptions(GUI::regularTypeface()).withHeight(height));
        graphics.drawText(text, x, y, 600, 16, juce::Justification::centredLeft);
    }
}; // class KnobsPage

std::unique_ptr<juce::Component> construct() { return std::make_unique<KnobsPage>(); }

SWShowUI::PageRegistration const registration{
    "knobs", "both painted knobs across their whole travel", &construct};

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------
