////////////////////////////////////////////////////////////////////////////////
///
/// lfoDisplayTests.cpp
/// -------------------
///
///   The LFO panel's own edits, and whether they reach the audio thread.
///
///   The panel had no test of any kind, and the shape of what it hid is worth
/// stating: **every widget on it wrote the LFO the strip holds, and that stopped
/// being the engine's LFO when the editor was bound to `programMain_`.** All seven
/// sub-parameters go through `EditorHost::editParameter` now, which moves both
/// copies; Waveform and SyncTypes had no `ParameterID` until issue #159 and took
/// a `ToEngine::SetUnexportedLFOParameter` of their own, and the N/T/D buttons
/// were not converted with the waveform popup: they called `LFO::addSyncType()`
/// straight onto the main thread's module, so a sync-mode change moved the
/// display and the saved state and nothing anybody could hear.
///
///   So what these cases assert is not "the parameter changed" -- it did, that
/// was never the bug -- but **that a message was queued for the engine**. The
/// harness's `toEngine()` is a real ring, which makes that an ordinary read.
///
/// \note No window and no peer, unlike moduleControlFocusTests.cpp: nothing here
/// needs focus. `moduleControlActivated()` is public and is what builds the
/// panel, and a button is driven by `setToggleState( …, sendNotificationSync )`
/// rather than by a synthesised click -- `setClickingTogglesState( true )` means
/// a real click does exactly that and then notifies.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/editorHarness.hpp"

/// \note Before anything that names SW::Module, as elsewhere: the module chain
/// downcasts a node to it and this is the header with the complete type.
#include "core/modules/moduleDSPAndGUI.hpp"

#include "core/threading/messages.hpp"
#include "gui/modules/moduleControl.hpp"
#include "gui/modules/moduleUI.hpp"
#include "gui/painters/backgroundPainter.hpp"

#include "le/parameters/lfoImpl.hpp"
#include "le/parameters/parametersUtilities.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <set>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using LE::Parameters::LFO;
using LE::Parameters::LFOImpl;
using LE::SW::GUI::ModuleControlBase;
using LE::SW::Threading::ToEngine;

constexpr std::uint8_t
    syncTypesIndex(LE::Parameters::IndexOf<LFOImpl::Parameters, LFOImpl::SyncTypes>::value);

/// \brief The first module control in \p component's subtree, which is a knob on
/// the module strip and therefore something an LFO can be attached to.
ModuleControlBase *firstControl(juce::Component &component)
{
    for (auto *const pChild : component.getChildren())
    {
        if (auto *const pControl = dynamic_cast<ModuleControlBase *>(pChild))
            return pControl;
        if (auto *const pFound = firstControl(*pChild))
            return pFound;
    }
    return nullptr;
}

/// \brief Every module control in \p component's subtree.
void allControls(juce::Component &component, std::vector<ModuleControlBase *> &into)
{
    for (auto *const pChild : component.getChildren())
    {
        if (auto *const pControl = dynamic_cast<ModuleControlBase *>(pChild))
            into.push_back(pControl);
        allControls(*pChild, into);
    }
}

/// \brief The button named \p name anywhere under \p component.
///
/// \note By name because that is what `GUI::TextButton` sets from its label, and
/// because the panel's widgets are private members of a private nested class --
/// reaching them through the component tree is what a click does anyway.
juce::Button *buttonNamed(juce::Component &component, juce::StringRef const name)
{
    for (auto *const pChild : component.getChildren())
    {
        if (auto *const pButton = dynamic_cast<juce::Button *>(pChild);
            pButton && (pButton->getName() == name))
            return pButton;
        if (auto *const pFound = buttonNamed(*pChild, name))
            return pFound;
    }
    return nullptr;
}

/// Everything the interface has queued for the engine, drained.
std::vector<ToEngine> drain(LE::SW::Threading::ToEngineQueue &queue)
{
    std::vector<ToEngine> messages;
    ToEngine message;
    while (queue.pop(message))
        messages.push_back(message);
    return messages;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The last edit of LFO sub-parameter \p lfoParameterIndex of module 0's
/// parameter \p moduleParameterIndex.
///
/// \note A `SetBaseParameter` addressed by ParameterID. It was a
/// `SetUnexportedLFOParameter` addressed by index until issue #159: SyncTypes
/// and Waveform had no identifier to be addressed by, so they travelled down a
/// channel of their own. They are ordinary exported parameters now and take the
/// route every other edit takes.
///
////////////////////////////////////////////////////////////////////////////////

std::optional<ToEngine> lastLFOEdit(std::vector<ToEngine> const &messages,
                                    std::uint8_t const moduleParameterIndex,
                                    std::uint8_t const lfoParameterIndex)
{
    LE::SW::ParameterID parameterID;
    parameterID.value.type = LE::SW::ParameterID::LFOParameter;
    parameterID.value._.lfo = {lfoParameterIndex, moduleParameterIndex, /*moduleIndex*/ 0};

    std::optional<ToEngine> found;
    for (auto const &message : messages)
        if ((message.kind == ToEngine::Kind::SetBaseParameter) &&
            (message.setBaseParameter.parameterID == parameterID.binaryValue))
            found = message;
    return found;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief An editor with a module in slot 0 and its first control's LFO panel up.
///
////////////////////////////////////////////////////////////////////////////////

class PanelUnderTest
{
  public:
    explicit PanelUnderTest(SWTest::Instance &instance) : instance_(instance)
    {
        instance.openEditor();
        auto &editor(instance.editor());
        editor.addUserAddedModule(0);
        editor.resyncModuleRack();

        auto *const pModuleUI(editor.regionInSlot(0));
        REQUIRE(pModuleUI != nullptr);
        pControl_ = firstControl(*pModuleUI);
        REQUIRE(pControl_ != nullptr);

        /// \note The range the panel's two-value bound slider is laid out over.
        /// An LFO's bounds are normalised, so the control's own units do not come
        /// into it and neither does the effect in the slot.
        editor.moduleControlActivated(*pControl_, 0.0, 1.0, 0.0);

        // Whatever building the strip and the panel queued is not this case's.
        drain(instance.toEngine());
    }

    ModuleControlBase &control() const { return *pControl_; }
    LFO &lfo() const { return pControl_->lfo(); }

    /// \brief Clicks the panel's \p name button, the way a mouse would.
    juce::Button &click(juce::StringRef const name) const
    {
        auto &button(this->button(name));
        button.setToggleState(!button.getToggleState(), juce::sendNotificationSync);
        return button;
    }

    juce::Button &button(juce::StringRef const name) const
    {
        auto *const pButton(buttonNamed(instance_.editor(), name));
        REQUIRE(pButton != nullptr);
        return *pButton;
    }

    bool lit(juce::StringRef const name) const { return button(name).getToggleState(); }

  private:
    SWTest::Instance &instance_;
    ModuleControlBase *pControl_{nullptr};
}; // class PanelUnderTest

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
///
/// \note What a knob under an LFO is allowed to quantise, and what it is not.
///
///   A slow LFO on some knobs paints a staircase rather than a sweep, and the
/// question this settles is whether that is the interface losing resolution or
/// the parameter not having it. It is the latter, and the proof is one switch:
/// `ModuleDSP::setEffectParameter` stores into a slot typed by the parameter --
/// `bool`, `std::uint8_t`, `std::int16_t`, `float` -- and **returns the stored
/// value**, so `publishModulatedValues()` puts the already-rounded number in the
/// mailbox. A knob showing three positions for a three-way Direction is showing
/// what the DSP is doing.
///
///   So the invariant worth holding is the narrow one: a **FloatingPoint**
/// parameter must keep most of its sweep. If one of those ever starts snapping
/// to whole units -- a `setRange` given an integer interval, a `Math::convert`
/// to an integral type slipped into the path -- the knob would be inventing a
/// staircase, and that is the regression this catches.
///
/// \note A *small* discrete parameter is checked against its own arithmetic
/// rather than against a fixed list: N legal values must show exactly N. That
/// fails if a rounding rule changes from round-to-nearest to truncation, which
/// drops one end. Wider ones only have to stay a sweep -- an Integer parameter
/// measured in milliseconds is re-ranged to the engine's step time on top of its
/// own unit, so its exact count is the FFT setup's answer and not arithmetic
/// this test should be repeating.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("An LFO sweep keeps every value the parameter actually has", "[gui][lfo][modulation]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    instance.openEditor();
    auto &editor(instance.editor());

    struct Wrong
    {
        juce::String effect, parameter;
        std::size_t distinct, expected;
    };
    std::vector<Wrong> wrong;
    unsigned int continuous{0}, discrete{0};

    constexpr int samples{100};

    /// How many legal values still counts as a switch rather than a range. The
    /// widest genuine enumeration in the effect set is the twelve-key Chromatic.
    constexpr std::size_t smallEnumeration{16};

    for (std::uint8_t effect{0}; effect < LE::SW::Effects::Constants::numberOfEffects; ++effect)
    {
        editor.addUserAddedModule(effect);
        editor.resyncModuleRack();

        auto *const pModuleUI(editor.regionInSlot(0));
        REQUIRE(pModuleUI != nullptr);

        std::vector<ModuleControlBase *> controls;
        allControls(*pModuleUI, controls);

        for (auto *const pControl : controls)
        {
            auto const &info(pControl->info());
            if (!(info.maximum > info.minimum))
                continue;

            std::set<float> distinct;
            for (int step(0); step < samples; ++step)
            {
                auto const t(static_cast<double>(step) / (samples - 1));
                pControl->setValue(
                    static_cast<float>(info.minimum + t * (info.maximum - info.minimum)));
                distinct.insert(pControl->getValue());
            }

            ////////////////////////////////////////////////////////////////////
            ///
            /// \note A discrete parameter is held to its own arithmetic: N legal
            /// values must show as exactly N.
            ///
            /// \note A continuous one is held to a floor rather than to the full
            /// hundred, because some of them *are* quantised and correctly so --
            /// a knob measured in Hz or ms is re-ranged to the engine's bin width
            /// and step time by `quantizeRangeForEngineSetup()`, since the DSP
            /// cannot resolve between two values inside one bin. Those come back
            /// in the eighties. What the floor separates them from is a
            /// continuous parameter snapped to whole units, which is what an
            /// integer interval would do and which lands an order of magnitude
            /// lower -- the coarsest genuinely discrete control here shows 12.
            ///
            ////////////////////////////////////////////////////////////////////
            auto const isContinuous(info.type == LE::Parameters::RuntimeInformation::FloatingPoint);
            (isContinuous ? continuous : discrete)++;

            auto const legalValues(static_cast<std::size_t>(info.maximum - info.minimum) + 1);

            //   A switch or a small enumeration: the count is exact and there is
            // no engine resolution coarse enough to reach it.
            if (!isContinuous && (legalValues <= smallEnumeration))
            {
                if (distinct.size() != legalValues)
                    wrong.push_back(
                        {pModuleUI->description(), info.name, distinct.size(), legalValues});
                continue;
            }

            //   Everything else only has to stay a sweep -- and never has to beat
            // its own resolution: Threshold and SC Gain are whole decibels over
            // plus or minus 24, so forty-nine is all there is of them.
            auto const leastOfASweep(std::min<std::size_t>(samples / 2, legalValues));
            if (distinct.size() < leastOfASweep)
                wrong.push_back(
                    {pModuleUI->description(), info.name, distinct.size(), leastOfASweep});
        }

        editor.removeModule(*pModuleUI);
        editor.resyncModuleRack();
    }

    //   Both kinds are actually present, so neither branch is vacuous.
    CHECK(continuous > 0);
    CHECK(discrete > 0);

    for (auto const &entry : wrong)
        UNSCOPED_INFO(entry.effect << " / " << entry.parameter << ": " << entry.distinct
                                   << " distinct, expected " << entry.expected);
    CHECK(wrong.empty());
}

TEST_CASE("A sync mode set in the interface is queued for the engine", "[gui][lfo]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The regression, and the reason it was invisible: nothing in the
    /// suite had ever read the *engine* side of an LFO after a UI edit. The
    /// display, `paramsValue`, `stateSave` and the preset writer all answer from
    /// the main thread's copy, so every existing case agreed with a change the
    /// audio thread never received.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    PanelUnderTest const panel(instance);

    // Quarter is the default, so turning it off is the smallest real edit.
    REQUIRE(panel.lfo().hasEnabledSync(LFO::Quarter));
    panel.click(" N ");

    CHECK_FALSE(panel.lfo().hasEnabledSync(LFO::Quarter)); // ...the half that worked
    CHECK(panel.lfo().syncTypes() == LFO::Free);

    auto const queued(lastLFOEdit(drain(instance.toEngine()),
                                  panel.control().moduleParameterIndex(), syncTypesIndex));
    REQUIRE(queued.has_value()); // ...and the half that did not
    /// \note The mask travels as a float, which is what a parameter edit carries;
    /// these are small exact integers in one.
    CHECK(queued->setBaseParameter.value == static_cast<float>(LFO::Free));
}

TEST_CASE("N, T and D are one choice rather than three toggles", "[gui][lfo]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Issue #111. `SyncType` is a bit mask -- Quarter|Triplet|Dotted,
    /// `Free` being none of them -- and `snapSyncedPeriod()` picks whichever of
    /// the enabled grids lands nearest to the current period. With more than one
    /// enabled that is the quarter grid nearly everywhere, so lighting T or D on
    /// top of N read as "the button does nothing". The panel now selects one
    /// grid at a time.
    ///
    ///   What crosses to the engine is still the whole mask and not a bit: the
    /// engine applies what it is given rather than merging it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    PanelUnderTest const panel(instance);

    REQUIRE(panel.lit(" N ")); // Quarter is the default.

    panel.click(" T ");
    {
        auto const queued(lastLFOEdit(drain(instance.toEngine()),
                                      panel.control().moduleParameterIndex(), syncTypesIndex));
        REQUIRE(queued.has_value());
        CHECK(queued->setBaseParameter.value == static_cast<float>(LFO::Triplet));
        CHECK(panel.lfo().syncTypes() == LFO::Triplet);
        CHECK_FALSE(panel.lit(" N ")); // ...and the one that was lit went out
        CHECK(panel.lit(" T "));
        CHECK_FALSE(panel.lit(" D "));
    }

    panel.click(" D ");
    {
        auto const queued(lastLFOEdit(drain(instance.toEngine()),
                                      panel.control().moduleParameterIndex(), syncTypesIndex));
        REQUIRE(queued.has_value());
        CHECK(queued->setBaseParameter.value == static_cast<float>(LFO::Dotted));
        CHECK(panel.lfo().syncTypes() == LFO::Dotted);
        CHECK_FALSE(panel.lit(" T "));
        CHECK(panel.lit(" D "));
    }

    // Clicking the lit one is how Free is reached, as it always was.
    panel.click(" D ");
    {
        auto const queued(lastLFOEdit(drain(instance.toEngine()),
                                      panel.control().moduleParameterIndex(), syncTypesIndex));
        REQUIRE(queued.has_value());
        CHECK(queued->setBaseParameter.value == static_cast<float>(LFO::Free));
        CHECK(panel.lfo().syncTypes() == LFO::Free);
        CHECK_FALSE(panel.lit(" N "));
        CHECK_FALSE(panel.lit(" T "));
        CHECK_FALSE(panel.lit(" D "));
    }
}

/// \note The waveform, which takes the same route, is deliberately **not**
/// covered here. Its only entry point is the popup -- `type_.showCenteredAtRight`
/// with a callback -- and a menu is one of the things a headless editor cannot
/// drive. What guards it is that both go through the one
/// `updateParameterAndNotifyHost<>`, which the two cases above exercise; a
/// waveform case would need a real menu and would be measuring JUCE.

////////////////////////////////////////////////////////////////////////////////
///
/// \note What issue #174 was, and the one thing about the mark worth a case.
///
///   `fillLFOWaveformsMenu()` held its eleven icons in a function-local static,
/// and those are pointers into the artwork cache -- which `SkinLifetime` empties
/// the moment the last editor closes. A second editor never re-ran the
/// initialiser, so all eleven stayed invalid, `Artwork::draw()` returned without
/// drawing, and the well was blank for the rest of the process. Picking another
/// waveform did not help because every one of them was blank.
///
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
/// \note What issue #202 asked for: the well, the mark in it and the arrow
/// beside it are one press. The arrow was the only thing that answered a click
/// and it is 11x17 of a target that reads as 63x30, so the obvious place to
/// aim -- the mark naming the waveform -- did nothing.
///
///   Asked of the component tree rather than of a click, for the reason the note
/// above gives: the press ends in a popup menu, and a menu is what a headless
/// editor cannot drive. Where the press *lands* is the half that changed.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The whole waveform well answers a press, not just the arrow", "[gui][lfo]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;
    PanelUnderTest const panel(instance);

    auto *const pDisplay(instance.editor().lfoDisplay());
    REQUIRE(pDisplay != nullptr);

    // the pill the artwork draws, in the strip's own coordinates
    using namespace LE::SW::GUI::BackgroundStyle;
    auto const well(juce::Rectangle<float>{lfoWaveformWell.x, lfoWaveformWell.y,
                                           lfoWaveformWell.right - lfoWaveformWell.x,
                                           lfoWaveformWell.bottom - lfoWaveformWell.y}
                        .toNearestInt()
                        .translated(-pDisplay->getX(), -pDisplay->getY()));

    auto *const pPressed(dynamic_cast<juce::Button *>(pDisplay->getComponentAt(well.getCentre())));
    REQUIRE(pPressed != nullptr);

    // the whole pill, and the arrow that sits outside it to the right
    CHECK(pPressed->getBounds().contains(well));
    CHECK(pPressed->getRight() > well.getRight());
}

TEST_CASE("A second editor's LFO well has a mark in it", "[gui][lfo][skin]")
{
    SWTest::HostSideJuce const juce;
    SWTest::Instance instance;

    // an editor that raised an LFO panel and went away, which is what empties
    // the cache the marks live in
    {
        PanelUnderTest const first(instance);
    }
    instance.closeEditor();

    PanelUnderTest const panel(instance);
    auto *const pDisplay(instance.editor().lfoDisplay());
    REQUIRE(pDisplay != nullptr);

    auto const *const pMark(pDisplay->waveformMenu().getSelectedItemIcon());
    REQUIRE(pMark != nullptr);
    CHECK(pMark->isValid());
}
