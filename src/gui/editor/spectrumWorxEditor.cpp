////////////////////////////////////////////////////////////////////////////////
///
/// spectrumWorxEditor.cpp
/// ----------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "spectrumWorxEditor.hpp"

#include "external_audio/sample.hpp"

#include "core/automatedModuleChain.hpp"
#include "core/host_interop/plugin2Host.hpp"
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/spectrumWorxCore.hpp"
#include "core/threading/publish.hpp"
#include "gui/editor/editorHost.hpp"
#include "gui/editor/presetLoading.hpp"
#include "gui/editor/zoomedEditor.hpp"
#include "gui/preferences.hpp"
#include "io/jucePath.hpp"

#include "le/parameters/lfo.hpp"
#include "le/parameters/printer.hpp"
#include "le/parameters/uiElements.hpp"
#include "le/spectrumworx/presetStorage.hpp"
#include "le/spectrumworx/presets.hpp"
#include "le/utility/countof.hpp"
#include "le/utility/parentFromMember.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include "le/utility/assert.hpp"
#include "le/utility/polymorphicDowncast.hpp"
#include "le/utility/intrusivePtr.hpp"
#include "le/utility/ignoreUnused.hpp"

#include <array>
#include <cstdio>
#include <span>
#include <optional>
#include <string_view>
#include "le/utility/span.hpp"

namespace LE::SW::GUI
{

//...mrmlj...the specialized/optimized fillComboBoxForParameter<>() helpers no
//...longer go through the generic Parameters::print<>() function so we also
//...have to provide a specialization of the fillComboBoxForParameter()
//...function template to get the overlap factor in percentages in the settings
//...window...
//...clean this up...
template <> void fillComboBoxForParameter<Engine::OverlapFactor>(ComboBox &comboBox)
{
    //...mrmlj...
#if defined(__clang__) && defined(_DEBUG)
    Engine::Setup const *pEngineSetup(nullptr);
    ++pEngineSetup; //...mrmlj...workaround for clang's -fcatch-undefined-behavior...
#else
    static Engine::Setup const *const pEngineSetup(nullptr);
#endif // _DEBUG

    using Parameter = Engine::OverlapFactor;
    std::array<char, 20> buffer;
    Parameter::value_type value(Parameter::minimum());
    while (value <= Parameter::maximum())
    {
        using LE::Parameters::DisplayValueTransformer;
        using LE::Parameters::print;
        print<Parameter>(value, const_cast<Engine::Setup const &>(*pEngineSetup),
                         LE::Utility::makeSpan(&buffer[0], buffer.size()));
        std::strcat(&buffer[0], DisplayValueTransformer<Engine::OverlapFactor>::Suffix::c_str());
        comboBox.addItem(value, &buffer[0]);
        value *= 2;
    }
}

namespace Constants::Layout
{
unsigned int const textBoxHorizontalOffset = 114;
unsigned int const textBoxHeight = 33;
unsigned int const textBoxWidth = 170;

/// \brief What the main area's strings keep clear of their boxes' edges.
///
/// \note The boxes are painted by the skin, and a long effect title -- "Pitch
/// Follower (PV)" -- runs into their rounded ends at full width. Only the
/// fitting rectangle shrinks; the text is centred. \see issue #76.
unsigned int const textBoxMargin = 6;

unsigned int const moduleNameVerticalOffset = 20;
unsigned int const controlNameVerticalOffset = 66;
unsigned int const controlValueVerticalOffset = 79;
unsigned int const sampleNameVerticalOffset = 458;
} // namespace Constants::Layout

#pragma warning(push)
#pragma warning(disable : 4355) // 'this' used in base member initializer list.

SpectrumWorxEditor::SpectrumWorxEditor(EditorHost &editorHost, PanelPlacement const placement)
    : editorHost_(editorHost), panelPlacement_(placement), nextAvailableModuleSlot_(0),

      /// \note The chassis draws a well under each of these and a caption over
      /// it, from constants of its own -- a painter two layers below the editor
      /// cannot see one. The assertions at the top of the body are what keep
      /// the two from drifting apart.
      in_(*this, BackgroundStyle::knobWells[0].x, BackgroundStyle::knobWells[0].y),
      out_(*this, BackgroundStyle::knobWells[1].x, BackgroundStyle::knobWells[1].y),
      mix_(*this, BackgroundStyle::knobWells[2].x, BackgroundStyle::knobWells[2].y),

      moduleMenuButton_(*this), dropIndicator_(mainArea_),

      /// \todo A sampler display and a spectrum display were planned here.

      // buttons...
      preset_(mainArea_, "PRESETS", 86, 36), settingsButton_(mainArea_, "SETTINGS", 86, 36),
      ignoreExternalSample_(mainArea_, GlyphButton::Glyph::Lock, true /*toggles*/)
{
    using LE::Parameters::IndexOf;
    using namespace GlobalParameters;
    using GlobalParameters::Parameters;
    in_.setupForParameter(IndexOf<Parameters, InputGain>::value, InputGain ::minimum(),
                          InputGain ::maximum(), InputGain ::default_());
    out_.setupForParameter(IndexOf<Parameters, OutputGain>::value, OutputGain ::minimum(),
                           OutputGain ::maximum(), OutputGain ::default_());
    mix_.setupForParameter(IndexOf<Parameters, MixPercentage>::value, MixPercentage::minimum(),
                           MixPercentage::maximum(), MixPercentage::default_());

    updateMainKnobs();
    LE_ASSERT(!settings_);

    // Implementation note:
    //   A sample may have already been loaded, either at startup using the last
    // session "preset" or through the GUI that was then destroyed and is now
    // being recreated.
    //                                        (10.06.2010.) (Domagoj Saric)
    updateSampleNameAsync();

    // the focus grab is in parentHierarchyChanged(): the shim builds this
    // editor before parenting it, and a component off screen cannot take focus
    setDefaultFocusHandling();

    LE_ASSERT_MSG(mainArea_.getWidth() == estimatedWidth && mainArea_.getHeight() == artworkHeight,
                  "the skin and the editor's constants disagree");
    setSize(mainArea_.getWidth(), mainArea_.getHeight());

    moduleMenuButton_.moveToSlot(0);

    sampleArea_.setBounds(113, 461, 173, 30);

    preset_.setTopLeftPosition(111, 507);
    settingsButton_.setTopLeftPosition(201, 507);

    /// \note Placed from the label it belongs to rather than from a constant of
    /// its own -- the pair is centred over the sidechain source box, so where
    /// the lock goes depends on how wide the words beside it come out.
    ignoreExternalSample_.setCentrePosition(
        BackgroundPainter::sideChainLockBounds().getCentre().roundToInt());
    ignoreExternalSample_.setName("Ignore external audio");

    preset_.addListener(this);
    settingsButton_.addListener(this);

    resyncModuleRack();

    // the column is taken here rather than through requestEditorSize(), there
    // being no window to resize yet: the shim answers guiGetSize() out of the
    // holder it sizes from this component, so an alwaysVisible editor has to
    // already be the width it wants
    if (panelPlacement_ == PanelPlacement::alwaysVisible)
    {
        panelHasOwnColumn_ = true;
        setSize(expandedWidth, estimatedHeight);
        openRememberedPanel();
    }

    setOpaque(true);
    setVisible();

    // what the mailbox accumulated with no editor open is not this editor's
    // news: it starts from what the widgets were built with
    editorHost_.modulatedValues().discardChanges();

    startTimerHz(modulationRefreshHz);

    // Last: nothing may reach a half-built editor.
    editorHost_.editorOpened(*this);
}

#pragma warning(pop)

SpectrumWorxEditor::~SpectrumWorxEditor()
{
    LE_ASSERT(GUI::isThisTheGUIThread());

    // First: nothing may reach a dying editor.
    stopTimer();
    editorHost_.editorClosed(*this);

    // nor may anything be pointing at one: a menu is asynchronous, so a host
    // closing the window with one down leaves a callback holding a SafePointer
    // to a component that is going
    juce::PopupMenu::dismissAllActiveMenus();

    editorHost_.deregisterSampleLoadedListener(*this);

    /// \note
    ///   Take the focus beforehand to workaround JUCE's problematic focus
    /// handling (while taking the focus it will refocus the last focused
    /// component if it was a child of the component that is taking focus, IOW
    /// it will refocus the ModuleUI being destroyed, just what we are trying
    /// to avoid).
    ///                                       (13.01.2012.) (Domagoj Saric)
    LE_ASSERT(getWantsKeyboardFocus());
    LE_ASSERT(getMouseClickGrabsKeyboardFocus());
    // Only meaningful while on screen, and JUCE asserts otherwise.
    if (isShowing() || isOnDesktop())
        grabKeyboardFocus();
    destroyChainGUIs();

    /// \note
    ///   Required now that std::optional does not mark itself as
    /// uninitialised in its destructor so the PresetBrowser would think that
    /// the Settings window still exists (and vice verse) in its destructor.
    ///                                       (12.01.2012.) (Domagoj Saric)
    //...mrmlj...think of a cleaner solution...
    settings_ = std::nullopt;
    presetBrowser_ = std::nullopt;
}

/// \note Walks up to the nearest enclosing editor rather than to the top-level
/// component: clap-wrapper's JUCE shim nests it two deep --
/// implDesktop -> implHolder -> editor -- so the top is not the editor. Every
/// widget that asks its editor for the engine setup comes through here.
SpectrumWorxEditor &SpectrumWorxEditor::fromChild(juce::Component const &widget)
{
    LE_ASSERT(widget.getParentComponent());
    for (auto *pParent(widget.getParentComponent()); pParent;
         pParent = pParent->getParentComponent())
    {
        if (auto *const pEditor = dynamic_cast<SpectrumWorxEditor *>(pParent))
            return *pEditor;
    }
    LE_UNREACHABLE_CODE();
}

SpectrumWorxEditor &SpectrumWorxEditor::fromPresetBrowser(PresetBrowser &presetBrowser)
{
    return Utility::ParentFromOptionalMember<SpectrumWorxEditor, PresetBrowser,
                                             &SpectrumWorxEditor::presetBrowser_, false>()(
        presetBrowser);
}

Engine::Setup const &SpectrumWorxEditor::engineSetup() const
{
    return effect().uncheckedEngineSetup();
}

/// \note The main thread's chain, not the engine's. The editor walks this
/// constantly and the engine splices its own copy inside `process()`; see the
/// note on `EditorHost::programMain()`.
AutomatedModuleChain &SpectrumWorxEditor::moduleChain() { return program().moduleChain(); }
AutomatedModuleChain const &SpectrumWorxEditor::moduleChain() const
{
    return program().moduleChain();
}

SpectrumWorxCore &SpectrumWorxEditor::effect() { return editorHost_.core(); }
SpectrumWorxCore const &SpectrumWorxEditor::effect() const
{
    return const_cast<SpectrumWorxEditor &>(*this).effect();
}

SpectrumWorxEditor::Host &SpectrumWorxEditor::host() { return editorHost_.automation(); }
SpectrumWorxEditor::Host const &SpectrumWorxEditor::host() const
{
    return const_cast<SpectrumWorxEditor &>(*this).host();
}

Program &SpectrumWorxEditor::program() { return editorHost_.programMain(); }
Program const &SpectrumWorxEditor::program() const
{
    return const_cast<SpectrumWorxEditor &>(*this).program();
}

void SpectrumWorxEditor::togglePresetBrowser(juce::Button const &button)
{
    auto &editor(SpectrumWorxEditor::fromChild(button));
    LE_ASSERT(editor.getPeer());
    editor.showPresetBrowser(button.getToggleState());
}

/// \note The panel is an ordinary child, and the only thing that needs saying is
/// that it goes on top: `gradient_` raises itself to always-on-top for a module
/// drag, and a stale one would paint through this. *Where* it goes is
/// layOutPanels()' answer, the placement being able to change under a panel that
/// is already up.

void SpectrumWorxEditor::showPanel(juce::Component &panel)
{
    addAndMakeVisible(panel);
    panel.toFront(false);
    layOutPanels();
}

juce::Component *SpectrumWorxEditor::currentPanel()
{
    if (settings_.has_value())
        return &*settings_;
    if (presetBrowser_.has_value())
        return &*presetBrowser_;
    return nullptr;
}

/// \note The only code that reads `panelPlacement_`. Everything that opens or
/// shuts a panel ends here, so "how wide is the editor and where is the panel"
/// has one answer rather than one per entry point.

void SpectrumWorxEditor::layOutPanels()
{
    auto *const pPanel(currentPanel());

    // alwaysVisible keeps the column whether or not anything is in it, so the
    // editor does not flicker a width between one panel closing and the next
    bool const wantColumn((panelPlacement_ == PanelPlacement::alwaysVisible) ||
                          ((panelPlacement_ == PanelPlacement::expandContract) && pPanel));

    auto const ownColumn(setPanelColumnVisible(wantColumn));

    // the skin moves and the panel does not follow: the column is at the left
    // edge either way, and an overlay is a position in the skin, which is the
    // editor's own coordinates only while there is no column
    mainArea_.setTopLeftPosition(ownColumn ? mainAreaX : 0, 0);
    if (pPanel)
        pPanel->setTopLeftPosition(ownColumn ? panelColumnX : overlayX, overlayY);
}

/// \note The host is asked first and the editor follows its answer, the window
/// being the host's. One that refuses to *grow* leaves the panel over the module
/// strips, which is exactly overlay placement; one that refuses to *shrink* has
/// already been handed the space back, and the worst of that is a margin.

bool SpectrumWorxEditor::setPanelColumnVisible(bool const wanted)
{
    if (wanted == panelHasOwnColumn_)
        return panelHasOwnColumn_;

    unsigned short const width(wanted ? expandedWidth : estimatedWidth);
    bool const granted(editorHost_.requestEditorSize(width, estimatedHeight));
    if (wanted && !granted)
        return false;

    panelHasOwnColumn_ = wanted;
    setSize(width, estimatedHeight);
    return wanted;
}

void SpectrumWorxEditor::setZoom(unsigned int const zoomPercent)
{
    if (!Preferences::isOfferedZoom(zoomPercent))
        return;

    preferences().setZoomPercent(zoomPercent);

    if (auto *const pWrapper = findParentComponentOfClass<ZoomedEditor>())
        pWrapper->setZoomPercent(zoomPercent);

    // in skin pixels, like every other caller: the scaling to window units
    // happens on the far side, out of the preference this has just written
    //
    // an announcement rather than a request -- the editor has already changed
    // and there is no fallback to negotiate for
    editorHost_.editorSizeChanged(getWidth(), getHeight());
}

void SpectrumWorxEditor::panelPlacement(PanelPlacement const placement)
{
    if (placement == panelPlacement_)
        return;

    panelPlacement_ = placement;

    // leaving alwaysVisible drops the resting panel, nothing the user asked for
    // being in it; one they did ask for stays up and moves
    if ((placement != PanelPlacement::alwaysVisible) && settings_.has_value() &&
        !settingsButton_.getToggleState())
        settings_ = std::nullopt;

    if ((placement == PanelPlacement::alwaysVisible) && !currentPanel())
    {
        openRememberedPanel();
        return;
    }
    layOutPanels();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The preset browser, with its button lit as by any other way of
/// opening it.
///
/// \note In `alwaysVisible` the column cannot be empty, so the two buttons stop
/// being independent toggles and become a two-way selector: pressing the lit one
/// lands here. Hence showPresetBrowser() rather than building the panel outright
/// -- one way to put the browser up, with the button state and the layout
/// following from it.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::openRestingPanel()
{
    LE_ASSERT(panelPlacement_ == PanelPlacement::alwaysVisible);
    LE_ASSERT(!currentPanel());

    showPresetBrowser(true);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Fills a column that has nothing in it with whichever panel this
/// session was last left on.
///
/// \note Which is *not* what pressing the lit button does, and the difference is
/// why this is a second function rather than an argument to the one above: a
/// "resting" state that read the remembered panel would land back on the panel
/// just pressed, making the button a no-op.
///
/// \note This is for a column filled from nothing -- at construction, or when the
/// placement changes under an editor with no panel up -- where the last place the
/// user was is the right answer. \see issue #129.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::openRememberedPanel()
{
    LE_ASSERT(panelPlacement_ == PanelPlacement::alwaysVisible);
    LE_ASSERT(!currentPanel());

    if (editorHost_.panelState().panel == PanelState::Panel::settings)
        showSettings();
    else
        showPresetBrowser(true);
}

void SpectrumWorxEditor::hidePanels()
{
    settings_ = std::nullopt;
    settingsButton_.setToggleState(false, juce::dontSendNotification);
    presetBrowser_ = std::nullopt;
    preset_.setToggleState(false, juce::dontSendNotification);

    if (panelPlacement_ == PanelPlacement::alwaysVisible)
        openRestingPanel();
    else
        layOutPanels();
}

/// \note The two panels share one rectangle, so opening either shuts the other
/// and un-toggles its button.
///
/// \note Only *builds* a browser when there is none, as showSettings() does:
/// `alwaysVisible` rests on an open browser, and rebuilding would throw away
/// whichever bank the user was in and drop them back at the root.
void SpectrumWorxEditor::showPresetBrowser(bool const show)
{
    if (!show)
    {
        hidePanels();
        return;
    }

    if (!presetBrowser_.has_value())
    {
        settings_ = std::nullopt;
        settingsButton_.setToggleState(false, juce::dontSendNotification);
        presetBrowser_.emplace();
        showPanel(*presetBrowser_);
    }
    preset_.setToggleState(true, juce::dontSendNotification);

    // \see showSettings(), which records the other half of the same answer
    editorHost_.panelState().panel = PanelState::Panel::presets;
}

void SpectrumWorxEditor::showFactoryBank(juce::String const &bank)
{
    showPresetBrowser(true);
    presetBrowser_->setFactoryBank(bank);
}

void SpectrumWorxEditor::setDefaultFocusHandling()
{
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
}

/// \note Fires when the host's window takes the editor, which is the first
/// moment it can hold focus.
void SpectrumWorxEditor::parentHierarchyChanged()
{
    if (isShowing() || isOnDesktop())
        grabKeyboardFocus();
}

////////////////////////////////////////////////////////////////////////////////
///
///   A strip can be dropped two ways. On another strip it changes places with it
/// and nothing else moves; between two of them, or at either end, it is inserted
/// and everything from there shifts along. Which one a point means is
/// moduleDropAt()'s answer, drawn by DropIndicator and carried out by
/// applyModuleDrop() -- one function each, so what the user is shown while
/// dragging and what happens when they let go cannot disagree.
///
/// \note The middle half of a strip is a swap and its outer quarters are
/// inserts, which makes both reachable without aiming: a gap with no width to it
/// is a target nobody can hit. The zone runs half a strip past each end of the
/// rack, so a drop at the front or the back needs no strip over the rack at all.
///
/// \note The point all of that is measured at is the **dragged strip's own
/// middle**, not the pointer. \see draggedStripCentre().
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
constexpr int slotWidth{ModuleUI::width + ModuleUI::distance};

/// How far outside the rack a drop still counts: far enough to reach the gaps at
/// either end, not so far that a drag let go across the editor lands one.
constexpr int dropMargin{ModuleUI::width / 2};

/// How much of each end of a strip means "between this one and its neighbour".
constexpr int insertZone{slotWidth / 4};
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
///
///   A drag is aimed with the strip, not with the pointer. Somebody who picked a
/// strip up by its left edge is carrying the whole thing, and what they line up
/// against the rack is the middle of what they see -- which is also where JUCE's
/// drag image is, `startDragging()` offsetting the ghost by the grab.
///
///   Measuring from the pointer would put the answer out by however far from the
/// middle the strip was picked up: up to half a strip, which is exactly the width
/// of a swap zone.
///
////////////////////////////////////////////////////////////////////////////////

juce::Point<int> SpectrumWorxEditor::draggedStripCentre(juce::Point<int> const pointer,
                                                        juce::Point<int> const grabbedAt)
{
    // the strip stays in its slot for the whole drag, so this is where it
    // *would* be: the pointer, less where it was picked up, plus half a strip
    return pointer - grabbedAt + juce::Point<int>(ModuleUI::width / 2, ModuleUI::height / 2);
}

/// \brief The above, with the two coordinate conversions a mouse event needs.
///
/// \note `grabbedAt` is put through the strip rather than taken off the event,
/// because `MouseEvent::getMouseDownPosition()` is relative to whichever
/// component the event was delivered to.
juce::Point<int> SpectrumWorxEditor::draggedStripCentre(ModuleUI const &moduleUI,
                                                        juce::MouseEvent const &event) const
{
    return draggedStripCentre(
        mainArea_.getLocalPoint(nullptr, event.getScreenPosition()),
        moduleUI.getLocalPoint(event.eventComponent, event.getMouseDownPosition()));
}

SpectrumWorxEditor::ModuleDrop
SpectrumWorxEditor::moduleDropAt(std::uint8_t const sourceSlot,
                                 juce::Point<int> const position) const
{
    ModuleDrop drop;

    auto const strips(nextAvailableModuleSlot_);
    if ((strips < 2) || (sourceSlot >= strips))
        return drop; // Nothing to reorder, or a strip that is on its way out.

    juce::Rectangle<int> const rack(ModuleUI::horizontalOffset, ModuleUI::verticalOffset,
                                    strips * slotWidth, ModuleUI::height);
    if (!rack.expanded(dropMargin).getIntersection(mainArea_.getLocalBounds()).contains(position))
        return drop;

    auto const alongTheRack(position.getX() - ModuleUI::horizontalOffset);

    // the two margins first: past either end of the rack there is no strip to
    // be over, so a drop there can only mean the gap it is past
    if (alongTheRack < 0)
        drop = {ModuleDrop::insert, 0};
    else if (alongTheRack >= (strips * slotWidth))
        drop = {ModuleDrop::insert, strips};
    else
    {
        auto const slot(static_cast<std::uint8_t>(alongTheRack / slotWidth));
        auto const acrossTheStrip(alongTheRack % slotWidth);

        if (acrossTheStrip < insertZone)
            drop = {ModuleDrop::insert, slot};
        else if (acrossTheStrip >= (slotWidth - insertZone))
            drop = {ModuleDrop::insert, static_cast<std::uint8_t>(slot + 1)};
        else
            drop = {ModuleDrop::swap, slot};
    }

    // the three drops that would change nothing: swapping a strip with itself,
    // and the two gaps either side of it, which it is already in. Answering
    // "nothing" stops the indicator offering a move nobody would see happen
    bool const pointless(
        (drop.action == ModuleDrop::swap)
            ? (drop.slot == sourceSlot)
            : ((drop.slot == sourceSlot) || (drop.slot == std::uint8_t(sourceSlot + 1))));
    if (pointless)
        return {};

    return drop;
}

void SpectrumWorxEditor::moduleDrag(ModuleUI &moduleUI, juce::MouseEvent const &event)
{
    if (!isDragAndDropActive())
    {
        startDragging(juce::var(), &moduleUI);
        return;
    }

    showModuleDrop(moduleDropAt(moduleUI.slot(), draggedStripCentre(moduleUI, event)));
}

void SpectrumWorxEditor::showModuleDrop(ModuleDrop const drop)
{
    switch (drop.action)
    {
    case ModuleDrop::swap:
        dropIndicator_.showSwap(drop.slot);
        break;
    case ModuleDrop::insert:
        dropIndicator_.showInsert(drop.slot);
        break;
    case ModuleDrop::nothing:
        dropIndicator_.hide();
        break;
    }
}

void SpectrumWorxEditor::moduleDragEnd(ModuleUI &moduleUI, juce::MouseEvent const &event)
{
    // asked again rather than remembered from the last moduleDrag(): a click
    // that never became a drag has no last answer, and leaves the strip where it
    // was -- which moduleDropAt() calls "nothing"
    auto const drop(moduleDropAt(moduleUI.slot(), draggedStripCentre(moduleUI, event)));

    dropIndicator_.hide();

    applyModuleDrop(moduleUI.slot(), drop);
}

/// \note The rack is told first and the engine second, which is the order every
/// edit takes: the strips move here so the drag ends where the user let go, and
/// resyncModuleRack() puts them where the chain says once the engine has caught
/// up. The host is told last, because what it is told is read back out of the
/// rack.

void SpectrumWorxEditor::applyModuleDrop(std::uint8_t const sourceSlot, ModuleDrop const drop)
{
    LE_ASSERT(isThisTheGUIThread());

    if (drop.action == ModuleDrop::nothing)
        return;

    /// \note We have to block automation here because of FMOD's MVC
    /// implementation in which it responds to
    /// EDITOR_TO_HOST_SET_PARAMETER_VALUE calls (part of the below
    /// host().modulesChanged() calls) by immediately calling
    /// HOST_TO_EDITOR_UPDATE_PARAMETER_VALUE which in turn, coupled with the
    /// "dependent parameter caching hack-mechanism", breaks the module chain
    /// contents while it is being traversed.
    ///                                       (20.10.2014.) (Domagoj Saric)
    Host2PluginInteropControler::AutomationBlocker const automationBlocker(
        /*host*/ moduleChainOwner /*mrmlj*/ ());

    if (drop.action == ModuleDrop::swap)
        swapModuleSlots(sourceSlot, drop.slot);
    else
    {
        // a gap index is one more than the slot to its left, and taking the
        // source strip out closes every gap after it
        moveModuleSlot(sourceSlot, static_cast<std::uint8_t>(
                                       (drop.slot > sourceSlot) ? (drop.slot - 1) : drop.slot));
    }

    refreshModuleRackAsync();
}

void SpectrumWorxEditor::moveModuleSlot(std::uint8_t const from, std::uint8_t const to)
{
    LE_ASSERT(from != to);

    auto const first(static_cast<std::int8_t>(from));
    auto const last(static_cast<std::int8_t>(to));
    std::int8_t const step(first < last ? -1 : +1);
    for (auto &pRegion : moduleRegions_)
    {
        if (!pRegion)
            continue;
        auto const slot(static_cast<std::int8_t>(pRegion->slot()));
        if (slot == first)
            pRegion->moveToSlot(to);
        else if ((slot >= std::min(first, last)) && (slot <= std::max(first, last)))
            pRegion->moveToSlot(static_cast<std::uint8_t>(slot + step));
    }

    editorHost().editModuleMove(from, to);

    host().gestureBegin("Drag module");
    for (std::uint8_t slot(std::min(from, to)); slot <= std::max(from, to); ++slot)
        host().moduleChangedByUser(slot, effectInRackSlot(slot));
    host().gestureEnd();
}

/// \note Two moves, a move being the only reordering primitive the chain has.
/// Taking the first to where the second is drags the second back one place, so
/// bringing it from there to where the first was puts the block between them back
/// where it started. For neighbours the first move is already the swap and the
/// second a no-op, which is why it is skipped rather than special-cased.

void SpectrumWorxEditor::swapModuleSlots(std::uint8_t const a, std::uint8_t const b)
{
    LE_ASSERT(a != b);

    auto const first(std::min(a, b));
    auto const second(std::max(a, b));

    auto *const pFirst(regionInRackSlot(first));
    auto *const pSecond(regionInRackSlot(second));
    LE_ASSERT_MSG(pFirst && pSecond, "A swap of a slot with no strip in it.");
    if (!pFirst || !pSecond)
        return;

    pFirst->moveToSlot(second);
    pSecond->moveToSlot(first);

    editorHost().editModuleMove(first, second);
    if (auto const displaced(static_cast<std::uint8_t>(second - 1)); displaced != first)
        editorHost().editModuleMove(displaced, first);

    // only the two, unlike a move: nothing between them changed slots
    host().gestureBegin("Swap modules");
    host().moduleChangedByUser(first, effectInRackSlot(first));
    host().moduleChangedByUser(second, effectInRackSlot(second));
    host().gestureEnd();
}

////////////////////////////////////////////////////////////////////////////////
///
///   The same list of effects the add-module button opens, meaning something
/// different in three places: on a strip it replaces that effect, between two it
/// goes in there and shifts the rest along, past the last one it is added. Which
/// of the three is the menu's heading, so the answer is read before anything is
/// chosen rather than discovered afterwards.
///
/// \note The zones are the drag's in shape but narrower. A drag draws where it
/// would land and can be walked back; a right-click is committed the moment it
/// goes down, so the seam is something to aim at here and to fall into there.
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
/// \see the note above.
constexpr int menuInsertZone{slotWidth / 6};
static_assert(menuInsertZone < insertZone, "The menu's seam is the narrower of the two.");
} // anonymous namespace

SpectrumWorxEditor::EffectMenuTarget
SpectrumWorxEditor::effectMenuTargetAt(juce::Point<int> const position) const
{
    // the whole rack, not the filled part: past the last strip is where a
    // module is added, and the most obvious place to ask for one
    juce::Rectangle<int> const rack(ModuleUI::horizontalOffset, ModuleUI::verticalOffset,
                                    SW::Constants::maxNumberOfModules * slotWidth,
                                    ModuleUI::height);
    if (!rack.contains(position))
        return {};

    auto const filled(nextAvailableModuleSlot_);
    auto const alongTheRack(position.getX() - ModuleUI::horizontalOffset);
    auto const slot(static_cast<std::uint8_t>(alongTheRack / slotWidth));

    if (slot >= filled)
        return {EffectMenuTarget::append, filled};

    // a full rack has no gaps to insert into, so its strips are replaceable edge
    // to edge: an insert would have to drop somebody's last module to make room
    if (filled < SW::Constants::maxNumberOfModules)
    {
        auto const acrossTheStrip(alongTheRack % slotWidth);
        if (acrossTheStrip < menuInsertZone)
            return {EffectMenuTarget::insert, slot};
        if (acrossTheStrip >= (slotWidth - menuInsertZone))
        {
            // except off the right of the last strip, which is the end of the
            // rack rather than a gap in it, and inserting there is adding
            auto const gap(static_cast<std::uint8_t>(slot + 1));
            return (gap < filled) ? EffectMenuTarget{EffectMenuTarget::insert, gap}
                                  : EffectMenuTarget{EffectMenuTarget::append, filled};
        }
    }

    return {EffectMenuTarget::replace, slot};
}

/// \note A replacement is the one of the three where the user has pointed at
/// something in particular; the other two point at a gap, which has no name.
juce::String SpectrumWorxEditor::effectMenuHeader(EffectMenuTarget const target) const
{
    switch (target.action)
    {
    case EffectMenuTarget::replace:
        return "Replace Effect";
    case EffectMenuTarget::insert:
        return "Insert Effect";
    case EffectMenuTarget::append:
        return "Add Effect";
    case EffectMenuTarget::none:
        break;
    }
    // `none` never gets this far -- showEffectMenuAt() answers it by opening no
    // menu -- so this is the switch being total, not a fourth heading
    return "Add Effect";
}

PopupMenu::OnChosen SpectrumWorxEditor::effectMenuCallback(EffectMenuTarget const target)
{
    // the menu does not block, so the editor can be torn down while it is open
    juce::Component::SafePointer<SpectrumWorxEditor> pEditor(this);
    return [pEditor, target](PopupMenu::OptionalID const chosenMenuEntryID) {
        if (!pEditor || !chosenMenuEntryID.has_value())
            return;
        auto &editor(*pEditor);
        LE_ASSERT(editor.moduleMenu_.isOwnerOfEntry(*chosenMenuEntryID));
        editor.applyEffectMenuChoice(target,
                                     editor.moduleMenu_.effectIndexForEntry(*chosenMenuEntryID));
    };
}

void SpectrumWorxEditor::showEffectMenuAt(juce::Point<int> const screenPosition)
{
    auto const target(effectMenuTargetAt(mainArea_.getLocalPoint(nullptr, screenPosition)));
    if (target.action == EffectMenuTarget::none)
        return;

    auto const header(effectMenuHeader(target));
    // the main area rather than the strip that was clicked: a strip can be
    // replaced under an open menu, which JUCE would take as its cue to dismiss
    moduleMenu_.menuWithHeader(header.toRawUTF8())
        .showAtScreenPosition(mainArea_, screenPosition, effectMenuCallback(target));
}

void SpectrumWorxEditor::applyEffectMenuChoice(EffectMenuTarget const target,
                                               std::uint8_t const effectIndex)
{
    LE_ASSERT(isThisTheGUIThread());

    switch (target.action)
    {
    case EffectMenuTarget::replace:
        replaceModuleInSlot(target.slot, effectIndex);
        return;
    case EffectMenuTarget::insert:
        insertModuleAtGap(target.slot, effectIndex);
        return;
    case EffectMenuTarget::append:
        addUserAddedModule(effectIndex);
        return;
    case EffectMenuTarget::none:
        return;
    }
}

void SpectrumWorxEditor::replaceModuleInSlot(std::uint8_t const slot,
                                             std::uint8_t const effectIndex)
{
    // not an assertion: the menu is asynchronous, so the strip it was opened on
    // can leave the rack -- a host changing a slot, a preset arriving
    if (slot >= nextAvailableModuleSlot_)
        return;
    if (effectInRackSlot(slot) == static_cast<std::int8_t>(effectIndex))
        return; // already that effect; nothing to tell anybody

    // \see applyModuleDrop() for why the chain is not walked while the host is
    // being told about it
    Host2PluginInteropControler::AutomationBlocker const automationBlocker(
        /*host*/ moduleChainOwner /*mrmlj*/ ());

    if (!setModuleInSlot(slot, static_cast<std::int8_t>(effectIndex)))
        return; // not in this build; nothing was asked of the engine

    // as the add-module menu does: what the user just asked for is what they
    // are about to reach for
    slotAwaitingFocus_ = slot;

    host().gestureBegin("Replace module");
    host().moduleChangedByUser(slot, static_cast<std::int8_t>(effectIndex));
    host().gestureEnd();

    refreshModuleRackAsync();
}

/// \note Added on the end and then moved, an insert not being something the
/// chain can be asked for. Both edits reach the engine through the same ring and
/// it drains the whole ring per block, so the two are one change as far as
/// anything processing is concerned.
///
/// \note No strip is moved here, unlike a drag: the module the user asked for has
/// no strip until resyncModuleRack() next runs. Which is what makes
/// effectInRackSlot() below the right thing to ask -- it still describes the rack
/// the menu was opened over.

void SpectrumWorxEditor::insertModuleAtGap(std::uint8_t const gap, std::uint8_t const effectIndex)
{
    auto const filled(nextAvailableModuleSlot_);
    if ((gap >= filled) || (filled >= SW::Constants::maxNumberOfModules))
        return; // the rack changed under the open menu

    Host2PluginInteropControler::AutomationBlocker const automationBlocker(
        /*host*/ moduleChainOwner /*mrmlj*/ ());

    if (!setModuleInSlot(filled, static_cast<std::int8_t>(effectIndex)))
        return; // not in this build; nothing was asked of the engine
    editorHost().editModuleMove(filled, gap);

    slotAwaitingFocus_ = gap;
    moduleAdded();

    // the new module, and every one it pushed along
    host().gestureBegin("Insert module");
    host().moduleChangedByUser(gap, static_cast<std::int8_t>(effectIndex));
    for (std::uint8_t moved(gap + 1); moved <= filled; ++moved)
        host().moduleChangedByUser(moved, effectInRackSlot(moved - 1));
    host().gestureEnd();

    refreshModuleRackAsync();
}

void SpectrumWorxEditor::setLastModulePosition(std::uint_fast8_t const slotIndex)
{
    LE_ASSERT(slotIndex <= SW::Constants::maxNumberOfModules);
    nextAvailableModuleSlot_ = slotIndex;
    moduleMenuButton_.moveToSlot(slotIndex);
}

namespace
{
#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.

struct EditorMainAreaText
{
    juce::String const *pText;
    juce::Font const *pFont;
    ColourMap::Name const colour;
    unsigned int const verticalOffset;
    juce::Justification const justification;
    unsigned int const textLinesToUse;
}; // struct EditorMainAreaText

#pragma warning(pop)

// Implementation note:
//   To prevent the "static initialisation order fiasco" (occurring with
// Clang 2.8 on OS X) we do not use the JUCE static Colours::white object
// but construct our own white juce::Colour here.
//                                        (25.01.2011.) (Domagoj Saric)
//
/// \note A `ColourMap::Name` rather than a `juce::Colour`: a colour taken at
/// static-initialisation time is taken before a palette has been chosen.
EditorMainAreaText mainAreaTexts[] = {
    {0, 0, ColourMap::Accent, Constants::Layout::moduleNameVerticalOffset,
     juce::Justification::centred, 1}, // active module name
    {0, 0, ColourMap::Text, Constants::Layout::controlNameVerticalOffset,
     juce::Justification::top | juce::Justification::horizontallyCentred, 2}, // control name
    {0, 0, ColourMap::TextDimmed, Constants::Layout::controlValueVerticalOffset,
     juce::Justification::centred, 1}, // control value
    {0, 0, ColourMap::Accent, Constants::Layout::sampleNameVerticalOffset,
     juce::Justification::centred, 1}, // sample name
};

void drawMainAreaText(juce::Graphics &graphics, EditorMainAreaText const &text)
{
    using namespace Constants::Layout;

    graphics.setColour(ColourMap::getColour(text.colour));
    graphics.setFont(*text.pFont);
    graphics.drawFittedText(*text.pText, textBoxHorizontalOffset + textBoxMargin,
                            text.verticalOffset, textBoxWidth - 2 * textBoxMargin,
                            textBoxHeight * text.textLinesToUse, text.justification,
                            text.textLinesToUse);
}
} //anonymous namespace

SpectrumWorxEditor::MainArea::MainArea()
{
    setSize(BackgroundStyle::width, BackgroundStyle::height);
    setOpaque(true);

    // neither wanted nor grabbed: this is a background, and a click on it
    // should focus the editor, which JUCE walks up to
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    addToParentAndShow(editor(), *this);
}

SpectrumWorxEditor &SpectrumWorxEditor::MainArea::editor()
{
    return Utility::ParentFromMember<SpectrumWorxEditor, MainArea,
                                     &SpectrumWorxEditor::mainArea_>()(*this);
}

SpectrumWorxEditor const &SpectrumWorxEditor::MainArea::editor() const
{
    return const_cast<MainArea &>(*this).editor();
}

void SpectrumWorxEditor::MainArea::paint(juce::Graphics &graphics)
{
    auto &editor(this->editor());

    BackgroundPainter::paint(graphics, getLocalBounds().toFloat());

    juce::Font const &moduleNameFont(Theme::singleton().headingFont());
    juce::Font const &sampleNameFont(DrawableText::defaultFont());
    juce::Font const &controlTextFont(Theme::singleton().labelFont());

    mainAreaTexts[0].pText = &editor.string(activeModuleName);
    mainAreaTexts[0].pFont = &moduleNameFont;
    mainAreaTexts[1].pText = &editor.string(activeControlName);
    mainAreaTexts[1].pFont = &controlTextFont;
    mainAreaTexts[2].pText = &editor.string(activeControlValue);
    mainAreaTexts[2].pFont = &controlTextFont;
    mainAreaTexts[3].pText = &editor.string(currentSampleName);
    mainAreaTexts[3].pFont = &sampleNameFont;

    for (auto text : mainAreaTexts)
    {
        drawMainAreaText(graphics, text);
    }
}

/// \note The logo, and this component's rather than the editor's because the
/// rectangle is a position in the skin -- which is what these coordinates are and
/// the editor's are not once the panel column is up.
///
/// \note And the right button, which is the rack's empty slots asking for an
/// effect. The strips have their own handler; this is the skin underneath them.
void SpectrumWorxEditor::MainArea::mouseDown(juce::MouseEvent const &event)
{
    if (event.mods.isPopupMenu())
    {
        editor().showEffectMenuAt(event.getScreenPosition());
        return;
    }

    if (logoArea().contains(event.x, event.y))
    {
        // JUCE clamps an out of range tab index to -1, which raises the panel
        // with no page selected at all
        editor().showSettings(aboutPageIndex);
    }
}

/// \note What is left of the editor once the skin is a child of it: the gutter
/// the panel column sits in. The colour comes from the skin's own outer surround
/// rather than a constant here, so it stays right for a skin that changes it.
void SpectrumWorxEditor::paint(juce::Graphics &graphics)
{
    // only what is outside the skin; the skin itself is MainArea's
    if (auto const column(mainArea_.getX()); column > 0)
    {
        graphics.setColour(BackgroundPainter::gutterColour());
        graphics.fillRect(0, 0, column, int{BackgroundStyle::height});
    }
}

void SpectrumWorxEditor::buttonClicked(juce::Button *const pButton)
{
    if (pButton == &settingsButton_)
    {
        if (settingsButton_.getToggleState())
        {
            showSettings();
        }
        else
        {
            LE_ASSERT(settings_);
            hidePanels();
        }
    }
    else
    {
        LE_ASSERT(pButton == &preset_);
        togglePresetBrowser(*pButton);
    }
}

void LE_NOINLINE SpectrumWorxEditor::updateString(String const stringID,
                                                  unsigned int const stringVerticalOffset,
                                                  unsigned int const stringHeight,
                                                  juce::String const &updatedString)
{
    string(stringID) = updatedString;

    using namespace Constants::Layout;
    // the skin's coordinates, which are the main area's
    mainArea_.repaint(textBoxHorizontalOffset, stringVerticalOffset, textBoxWidth, stringHeight);
}

void SpectrumWorxEditor::setActiveModuleName(juce::String const &newName)
{
    using namespace Constants::Layout;
    updateString(activeModuleName, moduleNameVerticalOffset, textBoxHeight, newName);
}

void SpectrumWorxEditor::setActiveControlName(juce::String const &newName)
{
    using namespace Constants::Layout;
    updateString(activeControlName, controlNameVerticalOffset, textBoxHeight * 4, newName);
}

void SpectrumWorxEditor::setActiveControlValue(juce::String const &newValue)
{
    using namespace Constants::Layout;
    updateString(activeControlValue, controlValueVerticalOffset, textBoxHeight, newValue);
}

/// \note A check rather than an assertion: there is no display between one
/// control being deactivated and the next being selected, and the timer this
/// runs from does not know that.
void SpectrumWorxEditor::updateActiveControlValue()
{
    if (!lfoDisplay_)
        return;

    LFODisplay const &lfoDisplay(*lfoDisplay_);
    if (lfoDisplay.lfo().enabled())
        setActiveControlValue("Controlled by LFO");
    else
        setActiveControlValue(lfoDisplay.control().getValueText());
}

void SpectrumWorxEditor::updateSampleName(juce::String const &newSampleName)
{
    using namespace Constants::Layout;
    updateString(currentSampleName, sampleNameVerticalOffset, textBoxHeight, newSampleName);
}

/// \note The box shows the **source**, which is a file's name only when a file is
/// what was selected, and it is never empty -- "nothing" is not one of the three
/// answers. \see doc/tech/sidechain-approach.md.

void SpectrumWorxEditor::updateSampleName()
{
    switch (editorHost_.sideChainSource())
    {
    case SideChainSource::File:
        return updateSampleName(LE::IO::pathToJuceString(editorHost_.currentSampleFile().stem()));
    case SideChainSource::Main:
        return updateSampleName("Main Input (1+2)");
    case SideChainSource::Host:
        return updateSampleName("Sidechain Input (3+4)");
    }
}

/// \note The loading branch is unreachable while setNewSample() decodes on this
/// thread, so the host is never mid-load when it is asked. Kept whole rather than
/// collapsed, as the shape a threaded loader would come back in.
void SpectrumWorxEditor::updateSampleNameAsync()
{
    if (editorHost_.isSampleLoadInProgress())
    {
        editorHost_.registerSampleLoadedListener(*this);
        setSampleLoadingStatus();
    }
    else
    {
        sampleArea_.setVisible();
        updateSampleName();
    }
}

void SpectrumWorxEditor::setSampleLoadingStatus()
{
    sampleArea_.setInvisible();
    updateSampleName("Loading...");
}

/// \note Picking one of these **discards** a loaded file: the box has three
/// answers and should not hide a fourth piece of state behind one of them.
/// \see `SpectrumWorxCLAP::setSideChainSource()`.
void SpectrumWorxEditor::sideChainSourceSelected(SideChainSource const source)
{
    editorHost_.setSideChainSource(source);
    updateSampleName();
}

/// \note The only call site that raises a dialog: a user picked this file out of
/// a browser a moment ago and is owed an answer. Every other caller of
/// `setNewSample` is a preset or a session, where there is nobody to answer a
/// modal box and possibly no window to put one in.
void SpectrumWorxEditor::newSampleFileSelected(fs::path const &file)
{
    auto const *const pErrorMessage(editorHost_.setNewSample(file));
    if (pErrorMessage)
        GUI::warningMessageBox("Error loading selected sample file!", pErrorMessage, false);
    updateSampleNameAsync();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief What the rack says is in \p slotIndex, which is not the same question
/// as what the chain says.
///
/// \note Deliberately the rack. A slot change is a request, so between the click
/// and the engine applying it the two disagree -- and every caller here is
/// describing what the *user* did, which is what they were looking at.
///
////////////////////////////////////////////////////////////////////////////////

std::int8_t SpectrumWorxEditor::effectInRackSlot(std::uint8_t const slotIndex) const
{
    for (auto const &pRegion : moduleRegions_)
        if (pRegion && (pRegion->slot() == slotIndex))
            return pRegion->module().effectTypeIndex();
    return AutomatedModuleChain::noModule;
}

void SpectrumWorxEditor::removeModule(ModuleUI &moduleUI)
{
    LE_ASSERT(isThisTheGUIThread());

    /// \note See the note for the equivalent statement in the moduleDragEnd()
    /// member function.
    ///                                       (20.10.2014.) (Domagoj Saric)
    Host2PluginInteropControler::AutomationBlocker const automationBlocker(
        /*host*/ moduleChainOwner /*mrmlj*/ ());

    // a strip whose module has already left the chain is still on screen and
    // still clickable until the resync runs. Not an assertion: clicking one is
    // something a user does, not a fault
    auto const slot(moduleUI.slot());
    if (slot >= nextAvailableModuleSlot_)
    {
        refreshModuleRackAsync();
        return;
    }

    setModuleInSlot(slot, AutomatedModuleChain::noModule);

    // the host is told what was *asked for* rather than what the chain holds,
    // which may still be the old chain for another block. Removing a module
    // shifts every later one down, so every selector to the end moves
    host().gestureBegin("Remove module");
    for (std::uint8_t moved(slot); moved < nextAvailableModuleSlot_; ++moved)
        host().moduleChangedByUser(moved, effectInRackSlot(moved + 1));
    host().gestureEnd();

    moduleRemoved();
    refreshModuleRackAsync();
}

bool SpectrumWorxEditor::setModuleInSlot(std::uint8_t const slotIndex,
                                         std::int8_t const effectIndex)
{
    LE_ASSERT(isThisTheGUIThread());
    // both copies, and the module building that goes with each, are the host's
    return editorHost().editSlot(slotIndex, effectIndex);
}

void SpectrumWorxEditor::addUserAddedModule(std::uint8_t const effectIndex)
{
    // filling a slot is a request the engine answers when it next runs, so the
    // focus is asked for by slot and taken by resyncModuleRack() once the strip
    // exists
    LE_ASSERT(isThisTheGUIThread());
    // Implementation note:
    //   We want any user-added module (using the add module menu) to
    // automatically gain focus.
    //                                        (09.02.2010.) (Domagoj Saric)
#ifdef _WIN32
    LE_ASSERT(getWantsKeyboardFocus());
    LE_ASSERT(getMouseClickGrabsKeyboardFocus());
    this->grabKeyboardFocus();
#endif // _WIN32

    std::uint8_t const changedSlot(nextAvailableModuleSlot_);
    if (!setModuleInSlot(changedSlot, static_cast<std::int8_t>(effectIndex)))
        return; // not in this build; nothing was asked of the engine

    slotAwaitingFocus_ = changedSlot;
    moduleAdded();

    host().gestureBegin("Add module");
    host().moduleChangedByUser(changedSlot, static_cast<std::int8_t>(effectIndex));
    host().gestureEnd();

    refreshModuleRackAsync();
}

bool SpectrumWorxEditor::ignoreExternalSample() const
{
    return ignoreExternalSample_.getToggleState();
}

bool SpectrumWorxEditor::loadPreset(fs::path const &presetFile, bool const ignoreExternalSample,
                                    juce::String &comment, juce::String const &presetName)
{
    auto const pPresetName(presetName.getCharPointer().getAddress());
    return GUI::loadPreset(editorHost_, this, presetFile, ignoreExternalSample, &comment,
                           pPresetName);
}

bool SpectrumWorxEditor::loadPreset(char *const inMemoryPreset, bool const ignoreExternalSample,
                                    juce::String &comment, juce::String const &presetName)
{
    auto const pPresetName(presetName.getCharPointer().getAddress());
    return GUI::loadPreset(editorHost_, this, inMemoryPreset, ignoreExternalSample, &comment,
                           pPresetName);
}

void SpectrumWorxEditor::savePreset(fs::path const &presetFile, bool const ignoreExternalSample,
                                    juce::String const &comment) const
{
    fs::path const externalSample(ignoreExternalSample ? fs::path()
                                                       : editorHost_.currentSampleFile());
    // a preset saved with "Ignore external audio" on names no file, so it
    // cannot honestly say its side channel comes from one. The other two
    // sources are unaffected: that toggle withholds audio, not routing
    auto source(editorHost_.sideChainSource());
    if (externalSample.empty() && (source == SideChainSource::File))
        source = SideChainSource::Main;

    // where the interface's juce::String becomes the format's bytes
    SW::savePreset(presetFile, externalSample, source,
                   std::string_view(comment.toRawUTF8(), comment.getNumBytesAsUTF8()), program());
}

char const *SpectrumWorxEditor::currentProgramName() const { return program().name().data(); }

/// \note Not preset machinery despite the name: the flag lives on the engine
/// side and guards automation while a whole program is being swapped in.
bool SpectrumWorxEditor::presetLoadingInProgress() const
{
    return static_cast<Host2PluginInteropControler const &>(
               moduleChainOwner()) /*...mrmlj...*/.presetLoadingInProgress();
}

void SpectrumWorxEditor::moduleActivated()
{
    LE_ASSERT(isThisTheGUIThread());
    LE_ASSERT(selectedModule());
    ModuleUI const &module(*selectedModule());
    setActiveModuleName(module.getName());
    if (!activeControl())
    {
        setActiveControlName(module.description());
        setActiveControlValue(juce::String());
    }

    /// \note
    ///   See the note in the moduleDeactivated() member function for an
    /// explanation as to why we expect the SharedModuleControls instance to
    /// possibly be already created.
    ///                                       (17.01.2012.) (Domagoj Saric)
    if (!sharedModuleControls_)
        sharedModuleControls_.emplace();
    else
        sharedModuleControls_->setEnabled(true);
    sharedModuleControls_->updateForActiveModule();
}

void SpectrumWorxEditor::moduleDeactivated()
{
    LE_ASSERT(selectedModule());

    // Implementation note:
    //   We need to prevent JUCE from transferring focus to other module UIs
    // when it destroys the currently active ModuleUI and/or the
    // SharedModuleControls instance as that would cause a call to
    // moduleActivated() while we are still in the SharedModuleControls
    // destructor which in turn would cause another (reentrant) call to the
    // SharedModuleControls destructor. This is accomplished by first
    // transferring focus to the editor window if the module being deactivated
    // (and possibly destroyed) is currently focused.
    //                                        (03.01.2012.) (Domagoj Saric)
    LE_ASSERT(this->getWantsKeyboardFocus());
    if (selectedModule()->juce::Component::isParentOf(getCurrentlyFocusedComponent()))
    {
        this->grabKeyboardFocus();
        LE_ASSERT(hasDirectFocus());
    }

    LE_ASSERT_MSG(!lfoDisplay_ || !lfoDisplay_->isEnabled(), "Module controls not deactivated.");

    setActiveModuleName(juce::String());
    setActiveControlName(juce::String());
    setActiveControlValue(juce::String());

    if (sharedModuleControls_)
    {
        /// \note We defer the destruction of the SharedModuleControls instance
        /// so that we can avoid the destruction+recreation in case the user is
        /// actually only activating a different module.
        ///                                   (17.01.2012.) (Domagoj Saric)
        sharedModuleControls_->setEnabled(false);
        GUI::postMessageToComponent(*this, [](GUI::SpectrumWorxEditor &editor) {
            auto &sharedModuleControls(editor.sharedModuleControls_);
            if (sharedModuleControls && !sharedModuleControls->isEnabled())
                sharedModuleControls = std::nullopt;
            return true;
        });
    }
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The ID a host knows \p control's parameter by.
///
/// \note `+ 1 /*Bypass*/`, because `ModuleControlBase::moduleParameterIndex()` is
/// the **LFO-able** index -- Bypass is not LFO-able and is not counted -- while a
/// `ParameterID::Module` carries the module parameter index, which counts it.
/// Every other reader of that getter adds the one too.
///
////////////////////////////////////////////////////////////////////////////////

ParameterID SpectrumWorxEditor::moduleControlID(ModuleControlBase const &control) const
{
    return moduleParameterID(control.module(),
                             static_cast<std::uint8_t>(control.moduleParameterIndex() + 1));
}

/// \note The chain's index rather than the slot a strip is *drawn* in: the two
/// differ while a slot change is in flight, and an ID is about the module.
ParameterID SpectrumWorxEditor::moduleParameterID(Module const &module,
                                                  std::uint8_t const moduleParameterIndex) const
{
    ParameterID parameterID;
    parameterID.value.type = ParameterID::ModuleParameter;
    parameterID.value._.module.moduleIndex = moduleChain().getIndexForModule(module);
    parameterID.value._.module.moduleParameterIndex = moduleParameterIndex;
    return parameterID;
}

/// \note The same number the strip's caption and the host's own parameter names
/// carry -- the chain index counted from one. \see ParameterNameGetter.
juce::String SpectrumWorxEditor::moduleParameterMenuName(Module const &module,
                                                         char const *const parameterName) const
{
    return "Module " + juce::String(moduleChain().getIndexForModule(module) + 1) + " - " +
           parameterName;
}

void SpectrumWorxEditor::moduleControlActivated(ModuleControlBase &control, double const minimum,
                                                double const maximum, double const interval)
{
    /// \note
    ///   In addition to the reason given for the SharedModuleControls instance
    /// in the moduleActivated()/moduleDeactivated() member functions, the
    /// LFODisplay instance can also be expected to be already created here
    /// because of the SharedModuleControls::FrequencyRange control (because
    /// one of its thumbs can be activated w/o first deactivating the other).
    ///                                       (17.01.2012.) (Domagoj Saric)
    if (!lfoDisplay_)
        lfoDisplay_.emplace();
    else
        lfoDisplay_->setEnabled(true);

    lfoDisplay_->setupForControl(control, minimum, maximum, interval);

    setActiveModuleName(control.moduleUI().getName());
    setActiveControlName(control.widget().getName());
    updateActiveControlValue();
}

void SpectrumWorxEditor::moduleControlDectivated(ModuleControlBase const &control)
{
    LE_ASSERT(lfoDisplay_);
    LE_ASSERT_MSG((&static_cast<LFODisplay const &>(*lfoDisplay_).control() == &control),
                  "Deactivating active module control through a wrong control.");
    LE::Utility::ignoreUnused(control);

    setActiveControlName(selectedModule() ? selectedModule()->description() : juce::String());
    setActiveControlValue(juce::String());

    retireLFODisplay();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The gesture a module control's drag is held in, opened on the press
/// and closed on the release.
///
/// \note Which is all a gesture is for. Selecting a control used to open one --
/// `moduleControlActivated()` did, and `focusGained` reaches that -- so merely
/// clicking a knob and then reaching for another told the host two parameters
/// were being edited, in the order `end( the first )`, `begin( the second )`. A
/// host with MIDI learn armed takes the first parameter it hears about, so it
/// learned the knob the user had walked away from. \see issue #188.
///
/// \note An edit that is not a drag -- a wheel notch, a menu row, a typed value
/// -- brackets itself instead, through `asDiscreteGesture`. \see
/// ModuleControlBase::publishValue().
///
/// \note The ID rather than the control, so that a begin and its end cannot name
/// different parameters. The frequency range is one widget standing for two, and
/// `FrequencyRange::valueChanged()` can move it from one thumb to the other while
/// the mouse is still down. \see ModuleControlBase::beginGesture().
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::moduleControlGestureBegin(ParameterID const parameterID) const
{
    host().automatedParameterBeginEdit(parameterID);
}

void SpectrumWorxEditor::moduleControlGestureEnd(ParameterID const parameterID) const
{
    host().automatedParameterEndEdit(parameterID);
}

void SpectrumWorxEditor::retireLFODisplay()
{
    if (!lfoDisplay_)
        return;

    /// \note See the note in the moduleDeactivated() member function.
    ///                                       (17.01.2012.) (Domagoj Saric)
    setDefaultFocusHandling();
    lfoDisplay_->setEnabled(false);
    /// \note We defer LFODisplay destruction so that we can avoid the
    /// destruction+recreation in case the user is actually only switching
    /// between controls.
    ///                                       (02.09.2013.) (Domagoj Saric)
    postMessageToComponent(*this, [](GUI::SpectrumWorxEditor &editor) {
        auto &lfoDisplay(editor.lfoDisplay_);
        if (lfoDisplay && !lfoDisplay->isEnabled())
        {
            lfoDisplay = std::nullopt;
            LE_ASSERT(editor.getWantsKeyboardFocus());
            LE_ASSERT(editor.getMouseClickGrabsKeyboardFocus());
        }
        return true;
    });
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Lets go of \p region's controls before it is destroyed.
///
///   Destroying a strip makes JUCE move the keyboard focus, which reaches
/// `ModuleControlImpl::focusLost` -> `reportInactiveControl()` on whichever
/// control had it -- and that calls `moduleControlDectivated()`, which asserts
/// that the LFO display for that control is still there.
///
/// \note Deliberately **not** `moduleControlDectivated()`: it asserts that the
/// LFO display is still there, and clearing `pActiveControl_` first is what makes
/// the focus loss a no-op. Deactivation used to end the host's automation
/// gesture too, which a dropped strip could not name a parameter for; a gesture
/// now lasts only as long as the mouse is down. \see issue #188.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::detachFrom(ModuleUI &region)
{
    LE_ASSERT(isThisTheGUIThread());

    // each of the three asks whether *it* points into the strip. Guarding on
    // the editor's record of what is current instead would miss a control
    // deactivated before the strip was dropped: deactivation is deferred, so the
    // widget is still alive, still parented to the editor, still holding a raw
    // ModuleUI * into the strip
    bool const activeControlIsRegions(pActiveControl_ && pActiveControl_->pointsInto(region));
    bool const lfoDisplayIsRegions(lfoDisplay_ && lfoDisplay_->pointsInto(region));
    bool const sharedControlsAreRegions(sharedModuleControls_ &&
                                        sharedModuleControls_->pointsInto(region));

    // the pointer first, before anything is destroyed: JUCE delivers a focus
    // loss synchronously, which comes back here through reportInactiveControl(),
    // and a cleared pActiveControl_ makes that re-entry a no-op
    if (activeControlIsRegions)
        pActiveControl_ = nullptr;

    // both destroyed *now*, where retireLFODisplay() and moduleDeactivated()
    // only disable them and post a message to do it later. That deferral suits a
    // user moving between controls, where the strip survives; here it is about
    // to be freed, and both are children of the editor holding a raw ModuleUI *
    if (lfoDisplayIsRegions)
    {
        retireLFODisplay(); // for the focus handling it does on the way out
        lfoDisplay_ = std::nullopt;
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And the keyboard out of the shared controls before they are
    /// destroyed, rather than leaving JUCE to move it for us.
    ///
    ///   JUCE answers a focused component going away by walking the focus to the
    /// next thing that will take it, synchronously, from inside `~Component`.
    /// These are parented to `mainArea_`, which does not want the keyboard, so
    /// the walk goes up to it and back down into **another strip** --
    /// `ModuleUI::focusGained` -> `activate()` -> `moduleActivated()`, which
    /// reaches for the shared controls while `std::optional::reset()` is midway
    /// through destroying them. Taking the keyboard to the editor, which wants
    /// it, stops the walk there.
    ///
    /// \note And the selection cleared here rather than left to the focus walk.
    /// Moving the keyboard out used to deselect the module for us -- that is what
    /// `SharedModuleControls::focusLost()` did -- so `pSelectedModule_` came back
    /// null on its own. It no longer does, and a strip left recorded as selected
    /// while its shared controls are freed reaches `~ModuleUI`, which takes its
    /// `if ( selected() )` branch into `moduleDeactivated()` and dereferences the
    /// empty optional. \see issue #139.
    ///
    ////////////////////////////////////////////////////////////////////////////

    if (sharedControlsAreRegions)
    {
        if (sharedModuleControls_->hasFocus())
            grabKeyboardFocus();
        sharedModuleControls_ = std::nullopt;
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And last, the keyboard out of the strip itself and the editor's
    /// record of it cleared -- the two things the focus walk used to do on the
    /// way past.
    ///
    ///   `~ModuleUI` asserts that a strip it is destroying *unselected* is not
    /// still holding the keyboard, which was true for free while a focus loss
    /// deselected: whichever of the two happened first, the other followed. Now
    /// neither happens on its own, and a strip whose knob had the focus reaches
    /// the destructor both unselected and focused. \see issue #139.
    ///
    ////////////////////////////////////////////////////////////////////////////

    if (region.hasFocus())
        grabKeyboardFocus();

    if (pSelectedModule_ == &region)
        pSelectedModule_ = nullptr;
}

void SpectrumWorxEditor::mainKnobDragStarted(std::uint8_t const index) const
{
    ParameterID parameterID;
    parameterID.value.type = ParameterID::GlobalParameter;
    parameterID.value._.global.index = index;
    host().automatedParameterBeginEdit(parameterID);
}

void SpectrumWorxEditor::mainKnobDragStopped(std::uint8_t const index) const
{
    ParameterID parameterID;
    parameterID.value.type = ParameterID::GlobalParameter;
    parameterID.value._.global.index = index;
    host().automatedParameterEndEdit(parameterID);
}

/// \note Here rather than in the widget layer: it instantiates
/// globalParameterChanged<>, which reaches host() and so needs the complete
/// plugin type.
void EditorKnob::valueChanged() noexcept
{
    using LE::Parameters::IndexOf;
    using namespace GlobalParameters;
    typedef GlobalParameters::Parameters GlobalParams;
    auto &editor(this->editor());
    auto const &value(this->getValue());
    switch (parameterIndex_)
    {
    case IndexOf<GlobalParams, InputGain>::value:
        LE_VERIFY(editor.globalParameterChanged<InputGain>(value, false));
        break;
    case IndexOf<GlobalParams, OutputGain>::value:
        LE_VERIFY(editor.globalParameterChanged<OutputGain>(value, false));
        break;
    case IndexOf<GlobalParams, MixPercentage>::value:
        LE_VERIFY(editor.globalParameterChanged<MixPercentage>(value, false));
        break;
        LE_DEFAULT_CASE_UNREACHABLE();
    }
}

void SpectrumWorxEditor::destroyChainGUIs()
{
    // everything, whatever the chain holds: the strips are the editor's, and
    // dropping one drops its reference to its module
    for (auto &pRegion : moduleRegions_)
    {
        if (!pRegion)
            continue;
        detachFrom(*pRegion);
        pRegion.reset();
    }
    setLastModulePosition(0);
}

/// \note `settings_` may already be up without the user having opened it -- it is
/// what alwaysVisible rests on -- so this only builds a panel when there is none,
/// and lights the button either way.
void SpectrumWorxEditor::showSettings(unsigned int const pageIndexToActivate)
{
    if (!settings_.has_value())
    {
        presetBrowser_ = std::nullopt;
        preset_.setToggleState(false, juce::dontSendNotification);
        settings_.emplace();
        showPanel(*settings_);
    }
    settings_->setCurrentTabIndex(pageIndexToActivate, false);
    settingsButton_.setToggleState(true, juce::dontSendNotification);

    // which panel the user is in is as much a place as which tab of it they
    // are on, and both are the session's. \see issue #129
    editorHost_.panelState().panel = PanelState::Panel::settings;
}

/// \note Checked against the tabs this build has rather than trusted: the number
/// comes out of a host's state blob, written by some earlier version, and JUCE
/// clamps an out-of-range tab index to -1 and opens a panel with no page in it.
///
/// \note The argument is read before the call, which is what makes the delegation
/// correct rather than merely short: building a Settings adds its first tab and
/// so selects page 0, and that write goes through `currentTabChanged`.

void SpectrumWorxEditor::showSettings()
{
    auto const page(editorHost_.panelState().settingsPage);
    showSettings((page < numberOfSettingsPages) ? page : unsigned{enginePageIndex});
}

void SpectrumWorxEditor::updateSettings()
{
    if (settings_.has_value())
        settings_->updateEnginePage();
}

void SpectrumWorxEditor::updateMainKnobs()
{
    auto const &parameters(program().parameters());
    using namespace GlobalParameters;
    in_.setValue(parameters.get<InputGain>());
    out_.setValue(parameters.get<OutputGain>());
    mix_.setValue(parameters.get<MixPercentage>());
}

void SpectrumWorxEditor::updateForGlobalParameterChange()
{
    updateMainKnobs();
    updateSettings();
}

using namespace GlobalParameters;
template <> void SpectrumWorxEditor::updateGlobalParameterWidget<FFTSize>()
{
    updateSettings();
    updateForEngineSetupChanges();
}
template <> void SpectrumWorxEditor::updateGlobalParameterWidget<OverlapFactor>()
{
    updateSettings();
    updateForEngineSetupChanges();
}
template <> void SpectrumWorxEditor::updateGlobalParameterWidget<WindowFunction>()
{
    updateSettings();
    updateForEngineSetupChanges();
}
template <> void SpectrumWorxEditor::updateGlobalParameterWidget<InputGain>() { updateMainKnobs(); }
template <> void SpectrumWorxEditor::updateGlobalParameterWidget<OutputGain>()
{
    updateMainKnobs();
}
template <> void SpectrumWorxEditor::updateGlobalParameterWidget<MixPercentage>()
{
    updateMainKnobs();
}

void SpectrumWorxEditor::updateForEngineSetupChanges()
{
    Engine::Setup const &engineSetup(this->engineSetup());
    if (sharedModuleControlsActive())
        sharedModuleControls().updateForEngineSetupChanges(engineSetup);
    moduleChain().forEach<Module>([&](Module &module) {
        //...mrmlj...when switching programs...
        auto *const pRegion(regionFor(module));
        LE_ASSERT(pRegion);
        if (pRegion)
            pRegion->updateForEngineSetupChanges(engineSetup);
    });
}

/// \note Where a tempo change lands. It arrives on a flag the engine raises, the
/// change being noticed on the audio thread where a widget may not be reached.
///
/// \note Only a *synced* LFO moves: its period is a fraction of the host's bar,
/// so the same parameter is a different number of seconds at a new tempo and
/// snaps to a different grid. A free one is measured against a bar that never
/// changes length. \see doc/tech/how-lfo-rates-and-eval-work.md.
void SpectrumWorxEditor::updateForNewTimingInfo()
{
    LE_ASSERT(isThisTheGUIThread());
    if (lfoDisplay_ && lfoDisplay_->isEnabled())
        lfoDisplay_->updateForNewTimingInfo();
}

void SpectrumWorxEditor::updateLFO(ModuleUI &moduleUI, std::uint8_t const parameterIndex,
                                   std::uint8_t const lfoParameterIndex,
                                   Plugins::AutomatedParameterValue const value)
{
    LE_ASSERT(isThisTheGUIThread());

    // an LFO switched off elsewhere -- a host writing the switch, or a preset
    // arriving -- leaves the widget wherever the sweep stopped
    using LFOImpl = LE::Parameters::LFOImpl;
    if ((lfoParameterIndex ==
         LE::Parameters::IndexOf<LFOImpl::Parameters, LFOImpl::Enabled>::value) &&
        (value == 0))
        showUnmodulatedValue(moduleUI, parameterIndex);

    if (lfoDisplay_ && lfoDisplay_->isEnabled())
        lfoDisplay_->updateForChangedParameters(moduleUI, parameterIndex, lfoParameterIndex, value);
}

/// \see the declaration.
void SpectrumWorxEditor::showUnmodulatedValue(ModuleUI &moduleUI,
                                              std::uint8_t const lfoableParameterIndex)
{
    LE_ASSERT(isThisTheGUIThread());

    // AutomationOrPreset rather than LFOValue: this is the parameter speaking
    // for itself, so the strip's value readout follows it as it would any other
    // change made somewhere else
    moduleUI.setParameter(static_cast<std::uint8_t>(lfoableParameterIndex + 1 /*Bypass*/),
                          moduleUI.module().unmodulatedParameter(lfoableParameterIndex),
                          ModuleUI::AutomationOrPreset);
}

////////////////////////////////////////////////////////////////////////////////
///
///   The module strips, and nothing waits for the engine's answer. An edit
/// queues; snapping is a pure function of the parameter's *static* description
/// and the widget already carries that -- a knob's range and interval come from
/// the same `ParameterInfo`, a combo box's value is an index, a button's a bool
/// -- so the value arriving here is already the snapped one.
///
/// \see doc/tech/threading_model.md §3.
///
////////////////////////////////////////////////////////////////////////////////

ModuleUI *SpectrumWorxEditor::regionFor(Module const &module)
{
    for (auto &pRegion : moduleRegions_)
        if (pRegion && (&pRegion->module() == &module))
            return pRegion.get();
    return nullptr;
}

ModuleUI *SpectrumWorxEditor::regionInSlot(std::uint8_t const slotIndex)
{
    auto const pModule(moduleChain().moduleAs<Module>(slotIndex));
    return pModule ? regionFor(*pModule) : nullptr;
}

/// \note Which strip is *drawn* in that slot, where regionInSlot() asks the
/// chain. The two differ for as long as a slot change is in flight; see
/// effectInRackSlot().
ModuleUI *SpectrumWorxEditor::regionInRackSlot(std::uint8_t const slotIndex)
{
    for (auto &pRegion : moduleRegions_)
        if (pRegion && (pRegion->slot() == slotIndex))
            return pRegion.get();
    return nullptr;
}

void SpectrumWorxEditor::createModuleRegion(LE::Utility::IntrusivePtr<Module> const &pModule,
                                            std::uint8_t const slotIndex)
{
    LE_ASSERT(isThisTheGUIThread());
    LE_ASSERT(pModule);

    // a module that already has a strip keeps it and moves; only a newly built
    // one needs one. A slot can be refilled with the same effect, and strips
    // shift left when one ahead of them is removed
    if (auto *const pExisting = regionFor(*pModule))
    {
        pExisting->moveToSlot(slotIndex);
        return;
    }

    for (auto &pRegion : moduleRegions_)
    {
        if (pRegion)
            continue;

        try
        {
            pRegion = std::make_unique<ModuleUI>(*this, pModule, slotIndex);
        }
        catch (...)
        {
            // swallowed: this can run while a preset is being applied, and a
            // strip that failed to build is not worth taking the host down for
            LE_ASSERT_MSG(false, "Module region construction threw; the slot has no strip.");
            return;
        }
        addToParentAndShow(mainArea_, *pRegion);
        return;
    }

    LE_ASSERT_MSG(false, "No free module region; the rack and the chain disagree.");
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Makes the rack a function of the chain, and nothing else.
///
/// \note The one place the two are reconciled, and a recomputation rather than an
/// update: a slot change is a request, so between the click and the engine
/// applying it the rack is what the user asked for and the chain is what is
/// playing. This closes the gap when the answer arrives. `ToUI::ChainChanged` is
/// one caller; the editor's own edits, which move a strip before the engine has
/// caught up, are the others.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::resyncModuleRack()
{
    LE_ASSERT(isThisTheGUIThread());

    // before any strip is destroyed: a menu opened from one -- its effect combo
    // box, its LFO type -- is asynchronous, and this is where the strip under it
    // can go. The dismissal is queued, so the menu stops taking input here and
    // answers "dismissed" a message-loop turn later
    juce::PopupMenu::dismissAllActiveMenus();

    auto &chain(moduleChain());

    for (auto &pRegion : moduleRegions_)
    {
        if (!pRegion)
            continue;

        bool stillChained(false);
        chain.forEach<Module>(
            [&](Module const &module) { stillChained |= (&module == &pRegion->module()); });
        if (stillChained)
            continue;

        // before the reset, never after: destroying a strip moves the keyboard
        // focus, and JUCE delivers that to whichever control had it
        detachFrom(*pRegion);
        pRegion.reset();
    }

    std::uint8_t slot(0);
    chain.forEach<Module>([&](Module &module) {
        // builds one when the module has none, which is how a chain the engine
        // installed comes to have strips at all
        createModuleRegion(LE::Utility::IntrusivePtr<Module>(&module), slot);
        ++slot;
    });
    setLastModulePosition(slot);

    // whatever the mailbox holds is about the rack that was here a moment ago.
    // It coalesces rather than queues, so there is nothing to deliver late
    editorHost().modulatedValues().discardChanges();

    if (slotAwaitingFocus_ != noSlotAwaitingFocus)
    {
        if (auto *const pRegion = regionInRackSlot(slotAwaitingFocus_))
        {
            slotAwaitingFocus_ = noSlotAwaitingFocus;
            // a component on no screen cannot take the keyboard, and
            // grabKeyboardFocus() says so with a jassert rather than by
            // declining -- which an offscreen render would hit. The slot is
            // cleared either way, so focus cannot jump to a stale choice later
            if (pRegion->isShowing() || pRegion->isOnDesktop())
                pRegion->grabKeyboardFocus();
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The same, on the next turn of the message loop.
///
/// \note Asynchronous, and it has to be: the path that removes a module is
/// `ModuleUI::buttonClicked` -> `removeModule`, so dropping the strip
/// synchronously would destroy the component whose button callback is on the
/// stack.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::refreshModuleRackAsync()
{
    ++rackResyncRequests_; // \see rackResyncRequests()

    postMessageToComponent(*this, [](SpectrumWorxEditor &editor) {
        editor.resyncModuleRack();
        return true;
    });
}

void SpectrumWorxEditor::parameterChangedElsewhere(ParameterID const parameterID, float const value)
{
    LE_ASSERT(isThisTheGUIThread());

    switch (parameterID.type())
    {
    case ParameterID::ModuleParameter:
        if (auto *const pRegion = regionInSlot(parameterID.value._.module.moduleIndex))
            pRegion->setParameter(parameterID.value._.module.moduleParameterIndex, value,
                                  ModuleUI::AutomationOrPreset);
        return;

    case ParameterID::LFOParameter:
        if (auto *const pRegion = regionInSlot(parameterID.value._.lfo.moduleIndex))
            updateLFO(*pRegion, parameterID.value._.lfo.moduleParameterIndex,
                      parameterID.value._.lfo.lfoParameterIndex, value);
        return;

    case ParameterID::GlobalParameter:
        // the six global knobs, and the only route by which host automation of
        // one moves the editor
        updateForGlobalParameterChange();
        return;

    case ParameterID::ModuleChainParameter:
        // a slot's effect changed under us; nothing sends this yet
        return;

        LE_DEFAULT_CASE_UNREACHABLE();
    }
}

void SpectrumWorxEditor::timerCallback()
{
    applyPaletteIfChanged();

    updateEngineInformationIfChanged();

    updateSaveButtonsIfShowing();

    pumpModulatedValues();
}

/// \note Polled for the reason the engine information is: the flag the two Save
/// buttons read is set by any parameter write, host automation included, and
/// that arrives on a thread with no business touching a widget.
void SpectrumWorxEditor::updateSaveButtonsIfShowing()
{
    LE_ASSERT(isThisTheGUIThread());

    if (presetBrowser_)
        presetBrowser_->updateSaveButtons();
}

/// \note Four `lexical_cast`s and four string compares per tick, and only while
/// the settings panel happens to be up.
bool SpectrumWorxEditor::updateEngineInformationIfChanged()
{
    LE_ASSERT(isThisTheGUIThread());

    return settings_.has_value() && settings_->updateEngineInformation();
}

/// \note Polled rather than pushed, because the change may not be this editor's:
/// the palette is process-wide, so a second instance has just had its colours
/// swapped by a settings page it never heard of. Every editor watching one
/// counter makes "change it once, change it everywhere" true without a registry
/// of live editors to keep correct.

void SpectrumWorxEditor::applyPaletteIfChanged()
{
    LE_ASSERT(isThisTheGUIThread());

    auto const generation(ColourMap::generation());
    if (generation == palette_)
        return;
    palette_ = generation;

    // before the repaint, and idempotent: whichever editor gets here first
    // takes the colours and the rest find them taken
    Theme::singleton().reloadColours();

    aboutIconsArtwork(false, true);

    // the wrapper rather than this, where there is one: ZoomedEditor paints
    // ColourMap::Ground behind the transform and is this editor's parent, so a
    // repaint from here would not reach it
    //
    // sendLookAndFeelChange() rather than repaint(): it repaints every
    // descendant and tells each one its colours moved, which a widget holding a
    // LookAndFeel colour of its own needs to hear
    auto *const pWrapper(findParentComponentOfClass<ZoomedEditor>());
    juce::Component &root(pWrapper ? static_cast<juce::Component &>(*pWrapper) : *this);
    root.sendLookAndFeelChange();
}

void SpectrumWorxEditor::setPalette(ColourMap::Palette const palette)
{
    preferences().setPalette(palette);
    ColourMap::setPalette(palette);

    // not applied here: this editor picks it up on its next tick through the
    // same path a second instance does, so a palette reaches the screen one way
    // rather than two that have to agree
}

void SpectrumWorxEditor::pumpModulatedValues()
{
    LE_ASSERT(isThisTheGUIThread());

    editorHost().modulatedValues().forEachChanged([&](std::size_t const index, float const value) {
        ParameterID const parameterID{Plugins::ParameterIndex{static_cast<std::uint16_t>(index)}};
        if (parameterID.type() != ParameterID::ModuleParameter)
            return;

        auto *const pRegion(regionInSlot(parameterID.value._.module.moduleIndex));
        if (!pRegion)
            return;

        // the slot's effect may have changed since the value was written, and
        // then this is an index the *previous* effect had: the mailbox is a
        // fixed array over the maximal layout and keeps its dirty bit until
        // swept, so a preset leaves values for slots whose effects it replaced
        if (parameterID.value._.module.moduleParameterIndex >=
            pRegion->module().numberOfParameters())
            return;

        pRegion->setParameter(parameterID.value._.module.moduleParameterIndex, value,
                              ModuleUI::LFOValue);
    });
}

/// \note `editGlobalParameter` rather than `editParameter`: the value here is in
/// the parameter's own units, and `editParameter` takes the automation edge's,
/// which for a power-of-two parameter is the exponent.
void SpectrumWorxEditor::queueGlobalParameter(std::uint8_t const index, float const value) const
{
    editorHost().editGlobalParameter(index, value);
}

void SpectrumWorxEditor::updateModuleParameterAndNotifyHost(ModuleUI &moduleUI,
                                                            std::uint8_t const moduleParameterIndex,
                                                            float const parameterValue,
                                                            bool const asDiscreteGesture) const
{
    auto &module(moduleUI.module());
    std::uint8_t const moduleIndex(moduleChain().getIndexForModule(module));

    ParameterID parameterID;
    parameterID.value.type = ParameterID::ModuleParameter;
    parameterID.value._.module = {ParameterID::Zero, moduleParameterIndex, moduleIndex};

    editorHost().editParameter(parameterID, parameterValue);

    // straight to the host rather than waiting for the engine to confirm:
    // automatedParameterChanged queues into the ring the host collects on its
    // next process() or flush(), and the requestParameterFlush() inside it is
    // what gets the command drained with the transport parked
    host().automatedParameterChanged(module, moduleIndex, moduleParameterIndex, parameterValue,
                                     asDiscreteGesture);
}

SpectrumWorxEditor::ModuleMenuButton::ModuleMenuButton(SpectrumWorxEditor &parent)
    : ArrowButton(parent.mainArea(), ArrowStyle::addModuleWidth, ArrowStyle::addModuleHeight,
                  true /*fades in from its base*/, ColourMap::Accent)
{
}

void SpectrumWorxEditor::ModuleMenuButton::moveToSlot(std::uint8_t const slotIndex)
{
    //...mrmlj..."magic number" adjustments...
    setTopLeftPosition(6 + ModuleUI::horizontalOffset +
                           ((ModuleUI::width + ModuleUI::distance) * slotIndex),
                       (ModuleUI::verticalOffset - 6) + (ModuleUI::height / 2) - (getHeight() / 2));
    setIsVisible(slotIndex < SW::Constants::maxNumberOfModules);
}

void SpectrumWorxEditor::ModuleMenuButton::clicked()
{
    // fromChild() rather than a downcast of the parent, which is the main area
    SpectrumWorxEditor &editor(SpectrumWorxEditor::fromChild(*this));
    LE_ASSERT(editor.nextAvailableModuleSlot_ < SW::Constants::maxNumberOfModules);

    EffectMenuTarget const target{EffectMenuTarget::append, editor.nextAvailableModuleSlot_};
    auto const header(editor.effectMenuHeader(target));
    editor.moduleMenu_.menuWithHeader(header.toRawUTF8())
        .showCenteredAtRight(*this, editor.effectMenuCallback(target));
}

SpectrumWorxEditor::DropIndicator::DropIndicator(juce::Component &parent)
{
    // neither focusable nor clickable: it is drawn over the rack during a drag,
    // and a drag is delivered to the strip it started on
    setWantsKeyboardFocus(false);
    setInterceptsMouseClicks(false, false);

    // on top for good rather than raised per drag: it has to be above the module
    // strips or a swap's fill is painted over by the strip it marks, and it is
    // built before any strip exists. A panel is a sibling of the main area, so
    // there is nothing here for it to wrongly rise above
    setAlwaysOnTop(true);

    addToParentAndShow(parent, *this);
    setInvisible();
}

void SpectrumWorxEditor::DropIndicator::showSwap(std::uint8_t const slotIndex)
{
    insert_ = false;
    setBounds(ModuleUI::horizontalOffset + (slotIndex * slotWidth), ModuleUI::verticalOffset,
              ModuleUI::width, ModuleUI::height);
    setVisible();
}

/// \note Taller than a strip, by a few pixels at each end. A strip is already
/// outlined in the skin's blue, so a line exactly as tall as one reads as a
/// thicker border on whichever strip the eye assigns it to.
void SpectrumWorxEditor::DropIndicator::showInsert(std::uint8_t const gapIndex)
{
    insert_ = true;
    setBounds(ModuleUI::horizontalOffset + (gapIndex * slotWidth) - (lineWidth / 2),
              ModuleUI::verticalOffset - lineOverrun, lineWidth,
              ModuleUI::height + (2 * lineOverrun));
    setVisible();
}

/// \note The skin's own blue for both, brightened for the line so that four
/// pixels of it read as an edge against the rack rather than as part of it, and
/// laid over the target strip for a swap so that what is underneath still shows
/// through -- which is what says *which* strip is being pointed at.
void SpectrumWorxEditor::DropIndicator::paint(juce::Graphics &graphics)
{
    auto const blue(ColourMap::getColour(ColourMap::Accent));

    if (insert_)
    {
        graphics.fillAll(blue.brighter(0.85f));
        return;
    }

    graphics.fillAll(blue.withAlpha(0.35f));
    graphics.setColour(blue);
    graphics.drawRect(getLocalBounds(), 2);
}

namespace
{
using LFO = LE::Parameters::LFOImpl;

LE_NOINLINE LFO::value_type rangeSliderValueToLFOValue(juce::Slider const &slider,
                                                       double const value)
{
    return Math::convertLinearRange<LFO::value_type, LFO::minimumValue,
                                    LFO::maximumValue - LFO::minimumValue, 1, double>(
        value, slider.getMinimum(), slider.getMaximum());
}

LE_NOINLINE double lfoValueToRangeSliderValue(juce::Slider const &slider,
                                              LFO::value_type const &value)
{
    return Math::convertLinearRange<double, LFO::value_type, LFO::minimumValue,
                                    LFO::maximumValue - LFO::minimumValue, 1>(
        value, slider.getMinimum(), slider.getMaximum());
}

void fillLFOWaveformsMenu(PopupMenu &menu)
{
    Artwork const *LE_RESTRICT const icons[] = {
        &resourceArtwork<LFOSine>(),         &resourceArtwork<LFOTriangle>(),
        &resourceArtwork<LFOSawtooth>(),     &resourceArtwork<LFOReverseSaw>(),
        &resourceArtwork<LFOSquare>(),       &resourceArtwork<LFOExponent>(),
        &resourceArtwork<LFORandomHold>(),   &resourceArtwork<LFORandomSlide>(),
        &resourceArtwork<LFORandomWhacko>(), &resourceArtwork<LFODirac>(),
        &resourceArtwork<LFOdIRAC>()};

    //LE_ASSERT( menu.getNumItems() == 0 );...mrmlj...add size information to the new ComboBox class...
    unsigned int itemId(0);
    Artwork const *LE_RESTRICT const *ppIcon = icons;
    for (auto const waveFormName : LE::Parameters::DiscreteValues<LFO::Waveform>::strings)
        menu.addItem(itemId++, waveFormName, *ppIcon++);
}

/// \brief Where the LFO strip sits in the chassis, which is the frame the
/// artwork's own rectangles are measured in. \see BackgroundStyle.
juce::Point<int> const lfoStripOrigin{107, 234};

/// \name The waveform target, in the strip's own coordinates
///@{
juce::Rectangle<int> lfoWaveformWellBounds()
{
    using namespace BackgroundStyle;
    return juce::Rectangle<float>{lfoWaveformWell.x, lfoWaveformWell.y,
                                  lfoWaveformWell.right - lfoWaveformWell.x,
                                  lfoWaveformWell.bottom - lfoWaveformWell.y}
        .toNearestInt()
        .translated(-lfoStripOrigin.x, -lfoStripOrigin.y);
}

juce::Point<int> const lfoWaveformMarkPosition{119, 144};
juce::Rectangle<int> const lfoWaveformArrowBounds{164, 149, ArrowStyle::stepWidth,
                                                  ArrowStyle::stepHeight};

/// The two of them and the well between them, which is what a press lands on.
juce::Rectangle<int> lfoWaveformTargetBounds()
{
    return lfoWaveformWellBounds().getUnion(lfoWaveformArrowBounds);
}
///@}
} // namespace

#define LE_COMP_PTR(member) reinterpret_cast<ComponentPtr>(&SpectrumWorxEditor::LFODisplay::member)
SpectrumWorxEditor::LFODisplay::ComponentPtr const
    SpectrumWorxEditor::LFODisplay::componentsToDisableKeyboardGrabingFor[] = {
        LE_COMP_PTR(switch_),  LE_COMP_PTR(phase_),   LE_COMP_PTR(range_), LE_COMP_PTR(period_),
        LE_COMP_PTR(quarter_), LE_COMP_PTR(triplet_), LE_COMP_PTR(dotted_)
        /*, this*/
};
#undef LE_COMP_PTR

#pragma warning(push)
#pragma warning(disable : 4355) // 'this' used in base member initializer list.

SpectrumWorxEditor::LFODisplay::LFODisplay()
    : switch_(*this), quarter_(*this, 93, " N "), triplet_(*this, 93 + 27 * 1, " T "),
      dotted_(*this, 93 + 27 * 2 - 3, " D "), waveform_(*this), period_(*this),
      phase_(*this, LE::Parameters::IndexOf<LFO::Parameters, LFO::Phase>::value), range_(*this),
      pModuleControl_(nullptr)
{
    for (auto const pComponent : componentsToDisableKeyboardGrabingFor)
    {
        juce::Component &component(this->*pComponent);
        component.setWantsKeyboardFocus(false);
        component.setMouseClickGrabsKeyboardFocus(false);
    }
    this->setWantsKeyboardFocus(false);
    this->setMouseClickGrabsKeyboardFocus(false);

    fillLFOWaveformsMenu(type_);

    switch_.setTopLeftPosition(44, 4);

    period_.setBounds(11, 48, 162, 27);
    period_.setSliderStyle(juce::Slider::LinearHorizontal);
    period_.setTextBoxStyle(juce::Slider::NoTextBox, true, 15, 18);
    //period_.setVelocityBasedMode( true );
    addToParentAndShow(*this, period_);

    phase_.setBounds(59, 177, 60, 18);
    phase_.setSliderStyle(juce::Slider::LinearHorizontal);
    phase_.setTextBoxStyle(juce::Slider::NoTextBox, true, 15, 18);
    phase_.setRange(-0.5, +0.5);
    phase_.setDoubleClickReturnValue(true, 0);
    addToParentAndShow(*this, phase_);

    waveform_.addListener(this);

    range_.setBounds(11, 110, width - 11, 15);
    range_.setSliderStyle(juce::Slider::TwoValueHorizontal);
    range_.setTextBoxStyle(juce::Slider::NoTextBox, true, 135, 30);
    addToParentAndShow(*this, range_);

    this->setBounds(lfoStripOrigin.x, lfoStripOrigin.y, width, 192);

    switch_.addListener(this);
    quarter_.addListener(this);
    triplet_.addListener(this);
    dotted_.addListener(this);
    range_.addListener(this);
    period_.addListener(this);
    phase_.addListener(this);
}

#pragma warning(pop)

SpectrumWorxEditor::LFODisplay::~LFODisplay() { editor().setDefaultFocusHandling(); }

void SpectrumWorxEditor::LFODisplay::setupForControl(ModuleControlBase &control,
                                                     double const minimum, double const maximum,
                                                     double const interval)
{
    pModuleControl_ = &control;

    range_.setRange(minimum, maximum, interval);

    // Implementation note:
    //   A two-valued juce::Slider does not allow to set a max periodScale that
    // is lower than the current min periodScale and vice verse. As a workaround
    // we first set both values to their respective extremes so that the
    // juce::Slider::set(Max/Min)Value() setter function would not alter our
    // values.
    //                                        (24.03.2010.) (Domagoj Saric)
    range_.setMaxValue(range_.getMaximum(), juce::dontSendNotification, false);
    range_.setMinValue(range_.getMinimum(), juce::dontSendNotification, false);

    //range_.setSkewFactor( control.getSkewFactor() );

    updateAllControls();

    addToParentAndShow(editor().mainArea(), *this);

    repaint();
}

namespace
{
/// \note Only the LFO's own sync setting decides between a note ratio and
/// milliseconds. There is always a tempo -- the host's, or an assumed 120 BPM in
/// four four -- so a ratio always means something.
bool skipPeriodRatio(SpectrumWorxEditor::LFODisplay::Period const &period)
{
    return period.lastSyncType() == LFO::Free;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The formatter a host's `value_to_text` uses, so the panel and a DAW's
/// automation lane cannot come to say different things about one period.
/// \see LFOImpl::printSyncedPeriodScale().
///
/// \note By the grid the last snap chose rather than by the whole mask: a file
/// may carry more than one (`sync="5"`), and a reading is labelled with the grid
/// the value is actually on.
///
////////////////////////////////////////////////////////////////////////////////

juce::String periodRatioString(SpectrumWorxEditor::LFODisplay const &parent,
                               double const &periodScale)
{
    std::array<char, 32> buffer;
    auto const written(LFO::printSyncedPeriodScale(static_cast<float>(periodScale),
                                                   parent.period().lastSyncType(), buffer));
    return juce::String(&buffer[0], written);
}

juce::String periodMillisecondsString(SpectrumWorxEditor::LFODisplay const &parent,
                                      double const &periodScale)
{
    LE_ASSERT(parent.period().milliseconds() == periodScale);

    bool const skipRatio(skipPeriodRatio(parent.period()));
    unsigned char const precision(!skipRatio * 2);

    // the number is written into the buffer *less the suffix*, so the strcpy at
    // the returned offset cannot reach the end however wide the number is
    constexpr char suffix[]{" ms"};
    std::array<char, 32> buffer;
    auto const charactersWritten(Utility::lexical_cast(
        periodScale, precision, std::span(buffer).first(buffer.size() - sizeof(suffix))));
    std::strcpy(&buffer[charactersWritten], suffix);

    return juce::String(&buffer[0]);
}

/// \note Through the transformer the host prints with rather than beside it:
/// these were two separate pieces of arithmetic, and said different things.
juce::String phaseString(SpectrumWorxEditor::LFODisplay const &parent, double const &phase)
{
    using Display = LE::Parameters::DisplayValueTransformer<LFO::Phase>;
    auto const &engineSetup(parent.control().editor().engineSetup());
    return juce::String(Display::transform(static_cast<float>(phase), engineSetup), 1) +
           Display::Suffix::c_str();
}

juce::String rangeValueString(SpectrumWorxEditor::LFODisplay const &parent,
                              double const &periodScale)
{
    LE_ASSERT(parent.control().isActive());
    return parent.control().getTextFromValue(static_cast<float>(periodScale));
}

#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.

struct LFOTextData
{
    typedef juce::String(StringGetter)(SpectrumWorxEditor::LFODisplay const &, double const &);

    double value;
    StringGetter *const getString;
    unsigned int const x;
    unsigned int const y;
    unsigned int const width;
    unsigned int const height;
    juce::Justification const justification;
};

#pragma warning(pop)

std::size_t const lfoWidth = 174;

LFOTextData sliderTexts[] = {
    {0, &periodRatioString, 14, 36, 158, 18, juce::Justification::right},        // period ratio
    {0, &periodMillisecondsString, 14, 71, 158, 18, juce::Justification::right}, // period ms
    {0, &rangeValueString, 14, 93, lfoWidth - 15 - 3, 18, juce::Justification::right}, // range max
    {0, &rangeValueString, 15, 126, lfoWidth - 15, 18, juce::Justification::left},     // range min
    {0, &phaseString, 14, 177, 158, 18, juce::Justification::right},                   // phase %
};

#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.

struct FixedText
{
    char const *const string;
    unsigned int const verticalPosition;
};

#pragma warning(pop)

static FixedText const fixedText[] = {
    {"Period", 36 + 14},
    {"Range", 93 + 14},
    {"Waveform", 149 + 14},
    {"Phase", 177 + 14},
};
} // anonymous namespace

void SpectrumWorxEditor::LFODisplay::paint(juce::Graphics &graphics)
{
    static_assert(lfoWidth == width, ""); //...mrmlj...clean this up...

    //...mrmlj...ugh...2.6.x quick-fix workarounds...reinvestigate and clean this up...
    if (!this->isEnabled())
        return;
    LE_ASSERT(editor().activeControl() != nullptr);
    LE_ASSERT(editor().activeControl() == &control());
    LE_ASSERT(control().isActive());
    LE_ASSERT(getParentComponent() == &editor().mainArea());

    {
        graphics.setFont(DrawableText::defaultFont());
        graphics.setColour(ColourMap::getColour(ColourMap::Text));

        for (auto const &text : fixedText)
            graphics.drawSingleLineText(text.string, 14, text.verticalPosition);
    }

    sliderTexts[0].value = period_.getValue();
    sliderTexts[1].value = period_.milliseconds();
    sliderTexts[2].value = range_.getMaxValue();
    sliderTexts[3].value = range_.getMinValue();
    sliderTexts[4].value = phase_.getValue();

    for (auto const &text : LE::Utility::makeSpan(sliderTexts).subspan(skipPeriodRatio(period_)))
    {
        graphics.setColour(ColourMap::getColour(ColourMap::TextDimmed));
        graphics.drawText(text.getString(*this, text.value), text.x, text.y, text.width,
                          text.height, text.justification, false);
    }
}

/// \note The parameter goes out through `editParameter()` and the host is told
/// separately, as `LFODisplay::updateParameterAndNotifyHost<>` does for the five
/// LFO parameters that have a ParameterID -- spelled out here because this is
/// reachable with no LFO strip on screen at all.
///
/// \note `lfoStateChanged()` is not cosmetic: it re-keys the knob's scroll wheel
/// and its double-click default on the answer that just changed.

void SpectrumWorxEditor::setLFOEnabled(ModuleControlBase &control, bool const enable)
{
    auto const moduleParameterIndex(control.moduleParameterIndex());
    if (moduleParameterIndex >= (SW::Constants::maxNumberOfParametersPerModule - 1))
        return; //...mrmlj...a parameter no LFO can drive...

    ParameterID::LFO const lfoParameterID{
        LE::Parameters::IndexOf<LFO::Parameters, LFO::Enabled>::value, moduleParameterIndex,
        moduleChain().getIndexForModule(control.module())};

    ParameterID parameterID;
    parameterID.value.type = ParameterID::LFOParameter;
    parameterID.value._.lfo = lfoParameterID;

    float const value(enable ? 1.0f : 0.0f);
    editorHost().editParameter(parameterID, value);
    host().automatedParameterChanged(lfoParameterID, value);

    control.lfoStateChanged();
    // ...and the knob goes back to the value under the LFO rather than staying
    // wherever the sweep stopped \see issue #204
    if (!enable)
        showUnmodulatedValue(control.moduleUI(), moduleParameterIndex);
    if (lfoDisplay_ && lfoDisplay_->isFor(control))
        lfoDisplay_->resyncEnabledSwitch();
    updateActiveControlValue();
    control.widget().repaint();
}

void SpectrumWorxEditor::LFODisplay::resyncEnabledSwitch()
{
    switch_.setToggleState(lfo().enabled(), juce::dontSendNotification);
    repaint();
}

void SpectrumWorxEditor::LFODisplay::buttonClicked(juce::Button *const pButton)
{
    if (pButton == &switch_)
    {
        bool const enable(switch_.getToggleState());
        LE_ASSERT(enable != lfo().enabled());
        // through the editor, where the knob's own menu goes too. The button
        // has already toggled itself, so the resync coming back is a no-op
        editor().setLFOEnabled(control(), enable);
    }
    else if (pButton == &waveform_)
    {
        //...mrmlj...
        if (!type_.menuActive())
        {
            juce::Component::SafePointer<LFODisplay> pThis(this);
            type_.showCenteredAtRight(waveform_, [pThis](bool const selectionChanged) {
                if (!pThis || !selectionChanged)
                    return;
                pThis->setWaveform(pThis->type_.getSelectedID());
            });
        }
    }
    else
    {
        LFO::SyncType syncType;
        if (pButton == &quarter_)
        {
            syncType = LFO::Quarter;
        }
        else if (pButton == &triplet_)
        {
            syncType = LFO::Triplet;
        }
        else
        {
            LE_ASSERT(pButton == &dotted_);
            syncType = LFO::Dotted;
        }

        // through updateParameterAndNotifyHost<> rather than LFO::addSyncType():
        // those write the strip's own module, which is the main thread's copy,
        // and never reach the audio thread. Every LFO sub-parameter takes the one
        // route since #159; SyncTypes had a channel of its own before that, and
        // only this branch had been left out of it
        //
        // the mask goes before the period, the ring being ordered and the engine
        // resnapping what it is given against whatever mask it holds
        //
        // the three buttons are one choice, not three toggles: the mask can hold
        // any combination and snapSyncedPeriod() picks whichever enabled grid
        // lands nearest, so all three lit reads N almost everywhere. The
        // parameter is still a mask -- two shipped presets carry sync="5" and
        // sync="7" and load exactly as they did
        bool const selectSyncType(pButton->getToggleState());
        LE_ASSERT(selectSyncType != lfo().hasEnabledSync(syncType));

        setSyncTypes(selectSyncType ? syncType : LFO::Free);
    }
}

/// \see the declaration. The popup has already recorded its own selection when
/// it calls this; the menu's value rows have not, hence the write either way.
void SpectrumWorxEditor::LFODisplay::setWaveform(unsigned int const waveformID)
{
    type_.setSelectedID(waveformID);
    updateParameterAndNotifyHost<LFO::Waveform>(waveformID);
    repaint();
}

/// \see the declaration. Also what a sync button's menu resets to.
void SpectrumWorxEditor::LFODisplay::setSyncTypes(LFO::SyncType const syncTypes)
{
    updateParameterAndNotifyHost<LFO::SyncTypes>(syncTypes);

    // ...which is what un-lights whichever of the three was lit before.
    updateSnapControls();

    updatePeriodControl();
    updateLFOAndHostFromPeriodControl();
    this->repaint();
}

void SpectrumWorxEditor::LFODisplay::sliderValueChanged(juce::Slider *const pSlider) noexcept
{
    repaint();

    if (pSlider == &range_)
    {
        auto const newLowerBound(rangeSliderValueToLFOValue(range_, range_.getMinValue()));
        auto const newUpperBound(rangeSliderValueToLFOValue(range_, range_.getMaxValue()));

        updateParameterAndNotifyHost<LFO::LowerBound>(newLowerBound);
        updateParameterAndNotifyHost<LFO::UpperBound>(newUpperBound);
    }
    else if (pSlider == &period_)
    {
        updateLFOAndHostFromPeriodControl();
    }
    else
    {
        LE_ASSERT(pSlider == &phase_);
        updateParameterAndNotifyHost<LFO::Phase>(phase_.getValue());
    }
}

void SpectrumWorxEditor::LFODisplay::updateForNewTimingInfo()
{
    updatePeriodControl();
    updateSnapControls();
    verifyGUIAndLFOConsistency();
    repaint();
}

void SpectrumWorxEditor::LFODisplay::updateForChangedParameters(
    ModuleUI const &moduleUI, std::uint8_t const parameterIndex,
    std::uint8_t const lfoParameterIndex, Plugins::AutomatedParameterValue /*const value*/)
{
    if ((&moduleUI == &control().moduleUI()) &&
        (parameterIndex == control().moduleParameterIndex()))
    {
        if (lfoParameterIndex == LE::Parameters::IndexOf<LFO::Parameters, LFO::Enabled>::value)
            control().lfoStateChanged();
        updateAutomatableControls();
        repaint();
    }
    verifyGUIAndLFOConsistency();
}

void SpectrumWorxEditor::LFODisplay::updateAllControls()
{
    updateAutomatableControls();
    verifyGUIAndLFOConsistency();
}

/// \note All seven of them, which is what "automatable" has meant since issue
/// #159: the sync mask and the waveform were left out of this and moved only
/// when a widget on the panel was pressed, so a host automating either of them
/// wrote the parameter and drew nothing. \see issue #209.
void SpectrumWorxEditor::LFODisplay::updateAutomatableControls()
{
    updatePeriodControl();
    updateRangeControl();
    updateSnapControls();
    auto &lfo(this->lfo());
    switch_.setToggleState(lfo.enabled(), juce::dontSendNotification);
    phase_.setValue(lfo.phase(), juce::dontSendNotification);
    type_.setSelectedID(lfo.waveForm());
}

void SpectrumWorxEditor::LFODisplay::updatePeriodControl()
{
    auto &lfo(this->lfo());

    float const rangeMinimum(LFO::PeriodScale::minimum());
    float const rangeMaximum(LFO::PeriodScale::maximum());
    double const rangeBeginning(LFO::snapPeriodScale(rangeMinimum, lfo.syncTypes()).first);
    double const rangeEnd(LFO::snapPeriodScale(rangeMaximum, lfo.syncTypes()).first);
    // one millisecond of the *reference* bar, so the slider's step does not
    // change under the user when the host changes tempo
    double const step(
        (lfo.syncTypes() != LFO::Free) ? 0 : 1 / 1000.0 / LFO::Timer::referenceBarDuration // 1 ms
    );

    period_.setRange(rangeBeginning, rangeEnd, step);
    period_.setSkewFactorFromMidPoint(1);

    // the override ignores the DragMode; notDragging spells "not a drag"
    double const resnappedValue(
        period_.Period::snapValue(lfo.periodScale(), juce::Slider::notDragging));
    lfo.setPeriodScale(static_cast<LFO::value_type>(
        resnappedValue)); //...mrmlj...rethink whether this should be done by the LFO class...
    period_.setValue(resnappedValue, juce::dontSendNotification);

    verifyGUIAndLFOConsistency();
}

void SpectrumWorxEditor::LFODisplay::updateRangeControl()
{
    auto &lfo(this->lfo());
    range_.setMaxValue(lfoValueToRangeSliderValue(range_, lfo.upperBound()),
                       juce::dontSendNotification, false);
    range_.setMinValue(lfoValueToRangeSliderValue(range_, lfo.lowerBound()),
                       juce::dontSendNotification, false);
}

/// \note Never disabled on "the host reported no tempo". There is always one --
/// the host's, or an assumed 120 BPM in four four that every LFO free-runs
/// against -- so a quarter note always means half a second of something.

void SpectrumWorxEditor::LFODisplay::updateSnapControls()
{
    auto &lfo(this->lfo());
    quarter_.setToggleState(lfo.hasEnabledSync(LFO::Quarter), juce::dontSendNotification);
    triplet_.setToggleState(lfo.hasEnabledSync(LFO::Triplet), juce::dontSendNotification);
    dotted_.setToggleState(lfo.hasEnabledSync(LFO::Dotted), juce::dontSendNotification);
}

void SpectrumWorxEditor::LFODisplay::updateLFOAndHostFromPeriodControl()
{
    updateParameterAndNotifyHost<LFO::PeriodScale>(period_.getValue());
}

void SpectrumWorxEditor::LFODisplay::automatedParameterChanged(std::uint8_t const lfoParameterIndex,
                                                               float const parameterValue) const
{
    auto const moduleParameterIndex(control().moduleParameterIndex());

    if (moduleParameterIndex >= (SW::Constants::maxNumberOfParametersPerModule - 1))
        return;

    ParameterID::LFO const lfoParameterID = {lfoParameterIndex, moduleParameterIndex,
                                             moduleIndex()};
    editor().host().automatedParameterChanged(lfoParameterID, parameterValue);
}

void SpectrumWorxEditor::LFODisplay::queueLFOParameter(std::uint8_t const lfoParameterIndex,
                                                       float const value) const
{
    auto const moduleParameterIndex(control().moduleParameterIndex());
    if (moduleParameterIndex >= (SW::Constants::maxNumberOfParametersPerModule - 1))
        return;

    ParameterID parameterID;
    parameterID.value.type = ParameterID::LFOParameter;
    parameterID.value._.lfo = {lfoParameterIndex, moduleParameterIndex, moduleIndex()};

    editor().editorHost().editParameter(parameterID, value);
}

void SpectrumWorxEditor::LFODisplay::verifyGUIAndLFOConsistency() const
{
#ifndef NDEBUG
    //...mrmlj...
    //...mrmlj...the rounding error difference is too great even for the nearEqual() function...
    //LE_ASSERT( Math::nearEqual( lfo().periodScale(), static_cast<LFO::value_type>( period_.getValue() ) ) );
    //LE_ASSERT( Math::abs( lfo().periodScale() - period_.getValue() ) < 0.001 );
    double const guiPeriod(lfo().periodScale());
    double const lfoPeriod(period_.getValue());
    LE_ASSERT(Math::abs(guiPeriod - lfoPeriod) < 0.001);
#endif // NDEBUG
}

std::uint8_t SpectrumWorxEditor::LFODisplay::moduleIndex() const
{
    auto const moduleIndex(editor().program().moduleChain().getIndexForModule(control().module()));
    return moduleIndex;
}

SpectrumWorxEditor &SpectrumWorxEditor::LFODisplay::editor()
{
    SpectrumWorxEditor &editor(
        Utility::ParentFromOptionalMember<SpectrumWorxEditor, LFODisplay,
                                          &SpectrumWorxEditor::lfoDisplay_, false>()(*this));
    LE_ASSERT((&editor.mainArea() == this->getParentComponent()) || !this->getParentComponent());
    return editor;
}

SpectrumWorxEditor const &SpectrumWorxEditor::LFODisplay::editor() const
{
    return const_cast<SpectrumWorxEditor::LFODisplay &>(*this).editor();
}

namespace
{
struct LFONameGetter
{
    using result_type = char const *;
    template <class Parameter> result_type operator()() const
    {
        return LE::Parameters::Name<Parameter>::string_;
    }
};

/// The LFO parameter's own name: "Period", "Phase", "Range Min", "Range Max".
char const *lfoParameterName(std::uint8_t const index)
{
    return LE::Parameters::invokeFunctorOnIndexedParameter<LFO::Parameters>(index, LFONameGetter());
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The four things the sync mask can be said to be, as a menu reads them.
///
/// \note Words rather than the strip's N, T and D, and in the order the buttons
/// sit in. `Free` is the empty mask and is the one a menu can offer that the
/// three buttons cannot: on the strip it is whichever of them was lit, pressed
/// again.
///
////////////////////////////////////////////////////////////////////////////////

struct SyncTypeName
{
    LFO::SyncType type;
    char const *name;
};

SyncTypeName const syncTypeNames[]{{LFO::Free, "Free"},
                                   {LFO::Quarter, "Note"},
                                   {LFO::Triplet, "Triplet"},
                                   {LFO::Dotted, "Dotted"}};

/// \brief The mask as words. Every grid it holds, or Free for none.
juce::String syncTypesString(std::uint8_t const mask)
{
    if (mask == LFO::Free)
        return syncTypeNames[0].name;

    juce::String reading;
    for (auto const &sync : syncTypeNames)
        if (sync.type && (mask & sync.type))
            reading += (reading.isEmpty() ? "" : " ") + juce::String(sync.name);
    return reading;
}

/// \brief What the menu heads itself with: the modulated parameter -- module and
/// all, \see issue #203 -- then which of the LFO's own this widget carries.
juce::String lfoParameterMenuName(SpectrumWorxEditor::LFODisplay const &parent,
                                  std::uint8_t const index)
{
    return parent.control().parameterMenuName() + " - LFO " + lfoParameterName(index);
}

/// \note The host's own string, not the two lines the panel draws beside the
/// slider: this is what parsePeriodScale() reads back, so what the field offers
/// is what may be typed into it.
juce::String periodString(SpectrumWorxEditor::LFODisplay const &parent)
{
    std::array<char, 64> buffer;
    auto const written(LFO::printPeriodScale(static_cast<float>(parent.period().getValue()),
                                             parent.lfo().syncTypes(), buffer));
    return juce::String(&buffer[0], written);
}

/// \brief What phaseString() prints, read back.
std::optional<double> parsePhase(SpectrumWorxEditor::LFODisplay const &parent,
                                 juce::String const &text)
{
    using Display = LE::Parameters::DisplayValueTransformer<LFO::Phase>;
    auto const number(text.upToFirstOccurrenceOf(Display::Suffix::c_str(), false, false).trim());
    if (number.isEmpty() || !number.containsOnly("0123456789.,+-eE"))
        return {};
    return Display::inverse(number.getFloatValue(), parent.control().editor().engineSetup());
}
} // anonymous namespace

SpectrumWorxEditor::LFODisplay::WaveformButton::WaveformButton(LFODisplay &parent)
    : ArrowButton(parent, lfoWaveformTargetBounds().getWidth(),
                  lfoWaveformTargetBounds().getHeight(), false, ColourMap::MouseOverGlow),
      ParameterButtonMenu(parent, LE::Parameters::IndexOf<LFO::Parameters, LFO::Waveform>::value),
      parent_(parent)
{
    setTopLeftPosition(lfoWaveformTargetBounds().getPosition());
}

void SpectrumWorxEditor::LFODisplay::WaveformButton::mouseDown(juce::MouseEvent const &event)
{
    if (event.mods.isPopupMenu())
        return showParameterMenu(event);
    ArrowButton::mouseDown(event);
}

/// \note The list the left button drops, as menu rows: what a right press used
/// to give was that list and nothing else, so the host had no way in.
void SpectrumWorxEditor::LFODisplay::WaveformButton::addParameterValueEntries(juce::PopupMenu &menu)
{
    auto const selected(parent_.type_.getSelectedIndex());
    for (unsigned int row(0); row < parent_.type_.numberOfItems(); ++row)
        menu.addItem(parent_.type_.getItemText(row), /*isEnabled*/ true,
                     /*isTicked*/ row == selected,
                     [pThis = juce::Component::SafePointer<WaveformButton>(this), row] {
                         if (pThis)
                             pThis->parent_.setWaveform(row);
                     });
}

/// \note The well and the mark are drawn here rather than with the chassis: this
/// strip is on screen only while a control is selected, and the chassis would
/// leave a pill alone in an empty LFO box. \see issue #134.
void SpectrumWorxEditor::LFODisplay::WaveformButton::paintButton(juce::Graphics &graphics,
                                                                 bool const isMouseOver,
                                                                 bool const /*isButtonDown*/)
{
    // in the chassis' own frame, which is where the artwork measured the well
    BackgroundPainter::paintLFOWaveformWell(graphics, lfoStripOrigin + getPosition());

    // Null for an item with no icon; every LFO waveform has one.
    if (auto const *const icon(parent_.type_.getSelectedItemIcon()); icon != nullptr)
    {
        auto const mark(lfoWaveformMarkPosition - getPosition());
        paintImage(graphics, *icon, mark.x, mark.y);
    }

    paintArrow(graphics, (lfoWaveformArrowBounds - getPosition()).toFloat(), isMouseOver);
}

SpectrumWorxEditor::LFODisplay::ParameterSlider::ParameterSlider(
    LFODisplay &parent, std::uint8_t const lfoParameterIndex)
    : parent_(parent), lfoParameterIndex_(lfoParameterIndex)
{
}

void SpectrumWorxEditor::LFODisplay::ParameterSlider::mouseDown(juce::MouseEvent const &event)
{
    notePressAt(event.position.x);
    if (event.mods.isPopupMenu())
        return showParameterMenu(event);
    HorizontalSlider::mouseDown(event);
}

void SpectrumWorxEditor::LFODisplay::ParameterSlider::mouseDrag(juce::MouseEvent const &event)
{
    if (event.mods.isPopupMenu())
        return;
    HorizontalSlider::mouseDrag(event);
}

juce::String SpectrumWorxEditor::LFODisplay::ParameterSlider::parameterName() const
{
    return lfoParameterMenuName(parent(), lfoParameterIndex());
}

juce::String SpectrumWorxEditor::LFODisplay::ParameterSlider::parameterValueText() const
{
    using LE::Parameters::IndexOf;
    switch (lfoParameterIndex())
    {
    case IndexOf<LFO::Parameters, LFO::PeriodScale>::value:
        return periodString(parent());
    case IndexOf<LFO::Parameters, LFO::Phase>::value:
        return phaseString(parent(), getValue());
    // the module control's own formatter rather than rangeValueString(), which
    // asserts the control is the active one -- true while it paints, and not a
    // question the menu has any business asking
    case IndexOf<LFO::Parameters, LFO::LowerBound>::value:
        return parent().control().getTextFromValue(static_cast<float>(getMinValue()));
    case IndexOf<LFO::Parameters, LFO::UpperBound>::value:
        return parent().control().getTextFromValue(static_cast<float>(getMaxValue()));
        LE_DEFAULT_CASE_UNREACHABLE();
    }
}

ParameterID SpectrumWorxEditor::LFODisplay::ParameterSlider::parameterID() const
{
    ParameterID parameterID;
    parameterID.value.type = ParameterID::LFOParameter;
    parameterID.value._.lfo = {lfoParameterIndex(), parent().control().moduleParameterIndex(),
                               parent().moduleIndex()};
    return parameterID;
}

/// \note Through the widget rather than around it: sendNotificationSync puts the
/// typed value through the path a drag takes, so the engine, the host and the
/// panel hear about it once each.
///
/// \note The two bounds read in the units of the parameter they modulate, which
/// is what the panel prints them in.
bool SpectrumWorxEditor::LFODisplay::ParameterSlider::setParameterFromText(juce::String const &text)
{
    using LE::Parameters::IndexOf;
    switch (lfoParameterIndex())
    {
    case IndexOf<LFO::Parameters, LFO::PeriodScale>::value:
    {
        auto const periodScale(LFO::parsePeriodScale(text.toRawUTF8(), parent().lfo().syncTypes()));
        if (!periodScale)
            return false;
        setValue(*periodScale, juce::sendNotificationSync);
        return true;
    }

    case IndexOf<LFO::Parameters, LFO::Phase>::value:
    {
        auto const phase(parsePhase(parent(), text));
        if (!phase)
            return false;
        setValue(*phase, juce::sendNotificationSync);
        return true;
    }

    case IndexOf<LFO::Parameters, LFO::LowerBound>::value:
    case IndexOf<LFO::Parameters, LFO::UpperBound>::value:
    {
        auto const value(parent().control().parseValueString(text));
        if (!value)
            return false;
        if (lfoParameterIndex() == IndexOf<LFO::Parameters, LFO::LowerBound>::value)
            setMinValue(*value, juce::sendNotificationSync, true);
        else
            setMaxValue(*value, juce::sendNotificationSync, true);
        return true;
    }
        LE_DEFAULT_CASE_UNREACHABLE();
    }
}

void SpectrumWorxEditor::LFODisplay::ParameterSlider::setParameterToDefault()
{
    using LE::Parameters::IndexOf;
    switch (lfoParameterIndex())
    {
    case IndexOf<LFO::Parameters, LFO::LowerBound>::value:
        setMinValue(getMinimum(), juce::sendNotificationSync, true);
        return;
    case IndexOf<LFO::Parameters, LFO::UpperBound>::value:
        setMaxValue(getMaximum(), juce::sendNotificationSync, true);
        return;
    default:
        /// \note What the double click already returns to, so that the two ways
        /// back to a default cannot disagree.
        setValue(getDoubleClickReturnValue(), juce::sendNotificationSync);
    }
}

SpectrumWorxEditor::LFODisplay::RangeSlider::RangeSlider(LFODisplay &parent)
    : ParameterSlider(parent, LE::Parameters::IndexOf<LFO::Parameters, LFO::LowerBound>::value)
{
}

/// \note Whichever thumb the press was nearest, which answers all three cases
/// the issue names: outside the band, the nearest thumb is the one on that side.
std::uint8_t SpectrumWorxEditor::LFODisplay::RangeSlider::lfoParameterIndex() const
{
    using LE::Parameters::IndexOf;
    auto const toLower(Math::abs(getPositionOfValue(getMinValue()) - pressPosition()));
    auto const toUpper(Math::abs(getPositionOfValue(getMaxValue()) - pressPosition()));
    return (toLower <= toUpper) ? IndexOf<LFO::Parameters, LFO::LowerBound>::value
                                : IndexOf<LFO::Parameters, LFO::UpperBound>::value;
}

SpectrumWorxEditor::LFODisplay::Period::Period(LFODisplay &parent)
    : ParameterSlider(parent, LE::Parameters::IndexOf<LFO::Parameters, LFO::PeriodScale>::value),
      lastSyncType_(LFO::Free)
{
}

double SpectrumWorxEditor::LFODisplay::Period::snapValue(double const attemptedValue,
                                                         DragMode /*dragMode*/)
{
    LFO::SnappedPeriod const result(
        LFO::snapPeriodScale(static_cast<float>(attemptedValue), parent().lfo().syncTypes()));
    lastSyncType_ = result.second;
    return result.first;
}

double SpectrumWorxEditor::LFODisplay::Period::milliseconds() const
{
    // whichever bar the period is a fraction of: the reference one for a free
    // LFO, the host's for a synced one. The panel has to convert the way
    // LFOImpl::getValue does, or it prints a rate the LFO is not running at
    float const bar((lastSyncType() == LFO::Free)
                        ? LFO::Timer::referenceBarDuration
                        : parent().editor().effect().lfoTimer().basePeriod());
    return this->getValue() * bar * 1000;
}

////////////////////////////////////////////////////////////////////////////////
//
// The LFO strip's own buttons, and what their menus answer
// --------------------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////

SpectrumWorxEditor::LFODisplay::EnableSwitch::EnableSwitch(LFODisplay &parent)
    : CapsuleButton(parent, ledCapsule, LEDTextButton::ledWidth, LEDTextButton::ledHeight),
      ParameterButtonMenu(parent, LE::Parameters::IndexOf<LFO::Parameters, LFO::Enabled>::value)
{
}

void SpectrumWorxEditor::LFODisplay::EnableSwitch::mouseDown(juce::MouseEvent const &event)
{
    if (event.mods.isPopupMenu())
        return showParameterMenu(event);
    CapsuleButton::mouseDown(event);
}

SpectrumWorxEditor::LFODisplay::SyncButton::SyncButton(LFODisplay &parent, unsigned int const x,
                                                       char const *const text)
    : TextButton(parent, x, 8, text),
      ParameterButtonMenu(parent, LE::Parameters::IndexOf<LFO::Parameters, LFO::SyncTypes>::value)
{
}

void SpectrumWorxEditor::LFODisplay::SyncButton::mouseDown(juce::MouseEvent const &event)
{
    if (event.mods.isPopupMenu())
        return showParameterMenu(event);
    TextButton::mouseDown(event);
}

juce::String SpectrumWorxEditor::LFODisplay::ParameterButtonMenu::parameterName() const
{
    return lfoParameterMenuName(parent_, lfoParameterIndex_);
}

/// \note Never asked for -- `parameterAcceptsText()` is false and the type-in
/// field is its only reader -- but a reading is what the question is for.
juce::String SpectrumWorxEditor::LFODisplay::ParameterButtonMenu::parameterValueText() const
{
    using LE::Parameters::IndexOf;
    if (lfoParameterIndex_ == IndexOf<LFO::Parameters, LFO::Enabled>::value)
        return parent_.lfo().enabled() ? "On" : "Off";

    if (lfoParameterIndex_ == IndexOf<LFO::Parameters, LFO::Waveform>::value)
        return parent_.type_.getSelectedItemText();

    LE_ASSERT((lfoParameterIndex_ == IndexOf<LFO::Parameters, LFO::SyncTypes>::value));
    return syncTypesString(parent_.lfo().syncTypes());
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note "Enabled" rather than the parameter's own name -- which is "Enable",
/// and which the header above the row already carries: a row beside a checkmark
/// reads as a state rather than as a command.
///
/// \note The sync mask gets its four values, which is one more than the strip
/// offers: `Free` is on a button only as the lit one pressed again. Ticked per
/// grid the mask holds, as the three buttons are lit, and choosing one *replaces*
/// the mask rather than adding to it -- which is what pressing a button does.
///
/// \note The waveform answers this for itself. \see WaveformButton.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::LFODisplay::ParameterButtonMenu::addParameterValueEntries(
    juce::PopupMenu &menu)
{
    using LE::Parameters::IndexOf;
    juce::Component::SafePointer<LFODisplay> const pStrip(&parent_);

    if (lfoParameterIndex_ == IndexOf<LFO::Parameters, LFO::Enabled>::value)
    {
        menu.addItem("Enabled", /*isEnabled*/ true, /*isTicked*/ parent_.lfo().enabled(), [pStrip] {
            if (pStrip)
                pStrip->editor().setLFOEnabled(pStrip->control(), !pStrip->lfo().enabled());
        });
        return;
    }

    if (lfoParameterIndex_ != IndexOf<LFO::Parameters, LFO::SyncTypes>::value)
        return;

    auto const mask(parent_.lfo().syncTypes());
    for (auto const &sync : syncTypeNames)
    {
        bool const ticked(sync.type ? ((mask & sync.type) != 0) : (mask == LFO::Free));
        menu.addItem(sync.name, /*isEnabled*/ true, ticked, [pStrip, type = sync.type] {
            if (pStrip)
                pStrip->setSyncTypes(type);
        });
    }
}

ParameterID SpectrumWorxEditor::LFODisplay::ParameterButtonMenu::parameterID() const
{
    ParameterID parameterID;
    parameterID.value.type = ParameterID::LFOParameter;
    parameterID.value._.lfo = {lfoParameterIndex_, parent_.control().moduleParameterIndex(),
                               parent_.moduleIndex()};
    return parameterID;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Through the paths a press takes rather than through the parameter:
/// switching an LFO off is more than a write -- \see setLFOEnabled() -- and the
/// three sync buttons are one choice, which `setSyncTypes()` is.
///
/// \note Both defaults are read off a default-constructed `LFO::Parameters`,
/// which is what `getDefaultAutomatedLFOParameter()` answers a host from, rather
/// than named here twice.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::LFODisplay::ParameterButtonMenu::setParameterToDefault()
{
    using LE::Parameters::IndexOf;
    LFO::Parameters const defaults;

    if (lfoParameterIndex_ == IndexOf<LFO::Parameters, LFO::Enabled>::value)
    {
        bool const enable(defaults.get<LFO::Enabled>());
        if (enable != parent_.lfo().enabled())
            parent_.editor().setLFOEnabled(parent_.control(), enable);
        return;
    }

    if (lfoParameterIndex_ == IndexOf<LFO::Parameters, LFO::Waveform>::value)
    {
        parent_.setWaveform(static_cast<unsigned int>(defaults.get<LFO::Waveform>()));
        return;
    }

    LE_ASSERT((lfoParameterIndex_ == IndexOf<LFO::Parameters, LFO::SyncTypes>::value));
    parent_.setSyncTypes(
        static_cast<LFO::SyncType>(static_cast<std::uint8_t>(defaults.get<LFO::SyncTypes>())));
}

SpectrumWorxEditor::SampleArea::SampleArea()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addToParentAndShow(editor().mainArea(), *this);
}

/// \note A menu rather than a file dialog outright, because the factory samples
/// are in the binary and a file dialog cannot show them. The dialog is one entry
/// away.

void SpectrumWorxEditor::SampleArea::mouseUp(juce::MouseEvent const &event)
{
    SpectrumWorxEditor &editor(this->editor());
    juce::ModifierKeys const mouseButtons(event.mods);
    // the right button selects the main input, which is what clearing the file
    // always meant
    if (mouseButtons.isRightButtonDown())
    {
        editor.sideChainSourceSelected(SideChainSource::Main);
        return;
    }
    if (!mouseButtons.isLeftButtonDown() || menu_.menuActive())
        return;

    auto const factorySamples(Sample::factorySamples());

    enum : PopupMenu::ItemID
    {
        browse = 0,
        mainAsSideChain,
        hostSideChain,
        firstFactorySample
    };

    // the three sources first, being what this control is for, and each named
    // by the source it selects rather than by the absence of another.
    // \see issue #113
    //
    // neither is disabled when already selected: they are a choice rather than a
    // command, and greying out the current one hides what it says
    menu_.clear();
    menu_.addItem(mainAsSideChain, "Main Input (1+2)");
    menu_.addItem(hostSideChain, "Sidechain Input (3+4)");
    menu_.addSectionHeader("Audio File");
    menu_.addItem(browse, "Load file...");
    for (std::size_t sample(0); sample < factorySamples.size(); ++sample)
        menu_.addItem(static_cast<PopupMenu::ItemID>(firstFactorySample + sample),
                      LE::IO::pathToUTF8(factorySamples[sample].stem()).c_str());

    juce::Component::SafePointer<SpectrumWorxEditor> pEditor(&editor);
    menu_.showCenteredBelow(*this, [this, pEditor, factorySamples](PopupMenu::OptionalID chosen) {
        if (!pEditor || !chosen)
            return;
        switch (*chosen)
        {
        case browse:
            return browseForFile();
        case mainAsSideChain:
            return pEditor->sideChainSourceSelected(SideChainSource::Main);
        case hostSideChain:
            return pEditor->sideChainSourceSelected(SideChainSource::Host);
        default:
            return pEditor->newSampleFileSelected(factorySamples[*chosen - firstFactorySample]);
        }
    });
}

void SpectrumWorxEditor::SampleArea::browseForFile()
{
    SpectrumWorxEditor &editor(this->editor());

    // one of the two places in src/ that may name juce::File, the other being
    // the preset browser's folder chooser; tests/checkNoJuceFile.cmake
    // allowlists both. The conversion happens here and the type goes no further
    //
    // only a real file is a place to start from: a factory sample is a bare name
    // with no directory, which JUCE would resolve against the working directory
    auto const currentFile(editor.editorHost().currentSampleFile());
    std::error_code error;
    auto const startingFile(fs::is_regular_file(currentFile, error)
                                ? LE::IO::pathToJuceFile(currentFile)
                                : juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    // held rather than stack-allocated: launchAsync() returns immediately and
    // the chooser must outlive the dialog
    fileChooser_ = std::make_unique<juce::FileChooser>("Choose audio file", startingFile,
                                                       Sample::supportedFormats(), true);
    juce::Component::SafePointer<SpectrumWorxEditor> pEditor(&editor);
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [pEditor](juce::FileChooser const &chooser) {
                                  if (!pEditor || chooser.getResults().isEmpty())
                                      return;
                                  LE_ASSERT(chooser.getResults().size() == 1);
                                  pEditor->newSampleFileSelected(
                                      LE::IO::juceFileToPath(chooser.getResults().getReference(0)));
                              });
}

SpectrumWorxEditor &SpectrumWorxEditor::SampleArea::editor()
{
    return Utility::ParentFromMember<SpectrumWorxEditor, SampleArea,
                                     &SpectrumWorxEditor::sampleArea_>()(*this);
}

#pragma warning(push)
#pragma warning(disable : 4355) // 'this' used in base member initializer list.

SpectrumWorxEditor::Settings::Settings() /// \throws std::bad_alloc Out of memory
    : juce::TabbedComponent(juce::TabbedButtonBar::TabsAtTop),

      fftSize_(enginePage_, xMargin, yMargin + yStep * 0 + yOffset, (Engine ::FFTSize *)(0)),
      overlapFactor_(enginePage_, xMargin, yMargin + yStep * 1 + yOffset,
                     (Engine ::OverlapFactor *)(0)),
      windowFunction_(enginePage_, xMargin, yMargin + yStep * 2 + yOffset,
                      (Engine ::WindowFunction *)(0))
{
    // the sum of what it draws -- a 16 px tab bar over a 347 px page bitmap --
    // rather than the editor's height, so an overlay leaves no empty band
    this->setSize(PanelPainter::width, ButtonStyle::tabHeight + PanelPainter::settingsPageHeight);

    updateEnginePage();

    setOutline(0);
    setIndent(0);
    setTabBarDepth(ButtonStyle::tabHeight);

    // real names, even though the captions are baked into the artwork: the name
    // JUCE holds is what accessibility and the tests read
    addTab("Engine", ColourMap::getColour(ColourMap::Transparent), &enginePage_, false);
    addTab("GUI", ColourMap::getColour(ColourMap::Transparent), &interfacePage_, false);
    addTab("About", ColourMap::getColour(ColourMap::Transparent), &aboutPage_, false);

    LE_ASSERT(getNumTabs() == numberOfSettingsPages);
}

SpectrumWorxEditor::Settings::~Settings()
{
    //...mrmlj..."desktop window" fade-out does not work with the current JUCE
    //getCurrentContentComponent()->fadeOutComponent( 200, 0, 0, 0.2f );
    //this->fadeOutComponent( 200, 0, 0, 0.2f );
    clearTabs();
}

#pragma warning(push)
#pragma warning(disable : 4702) // Unreachable code.

void SpectrumWorxEditor::Settings::comboBoxValueChanged(ComboBox const &comboBox)
{
    auto &settings(*LE::Utility::polymorphicDowncast<Settings *>(
        comboBox.getParentComponent()->getParentComponent()));
    auto &editor(settings.editor());

    unsigned int const value(comboBox.getValue());

    using namespace GlobalParameters;

    if (&comboBox == &settings.fftSize_)
    {
        LE_VERIFY(
            editor.globalParameterChanged<FFTSize>(static_cast<FFTSize ::value_type>(value), true));
    }
    else if (&comboBox == &settings.overlapFactor_)
    {
        LE_VERIFY(editor.globalParameterChanged<OverlapFactor>(
            static_cast<OverlapFactor ::value_type>(value), true));
    }
    else if (&comboBox == &settings.windowFunction_)
    {
        LE_VERIFY(editor.globalParameterChanged<WindowFunction>(
            static_cast<WindowFunction ::value_type>(value), true));
    }
    else if (&comboBox == &settings.interfacePage_.zoomComboBox())
    {
        editor.setZoom(value);
    }
    else if (&comboBox == &settings.interfacePage_.paletteComboBox())
    {
        editor.setPalette(static_cast<ColourMap::Palette>(value));
    }
    else if (&comboBox == &settings.interfacePage_.mouseOverComboBox())
    {
        preferences().setModuleUIMouseOverReaction(
            static_cast<Preferences::ModuleUIMouseOverReaction>(value));
    }
    else
    {
        LE_UNREACHABLE_CODE();
    }

    // the engine has been asked, not necessarily answered: a spectral parameter
    // is applied on whichever thread owns the engine, so the setup read here is
    // still the old one. updateEngineInformation() polls for the new one
    settings.updateEngineInformation();
}

#pragma warning(pop)

void SpectrumWorxEditor::Settings::updateEnginePage()
{
    auto const &editor(this->editor());
    // Implementation note:
    //   In rare circumstances this function gets called very often (if engine
    // setup parameters change rapidly, e.g. someone automates them using the
    // Ableton Live's 'dual control') and it gets called asynchronously to the
    // actual Engine::Setup instance updating. This can cause the Engine::Setup
    // instance to get 'out-of-date' which in turn would cause an assertion
    // failure if the SpectrumWorx::engineSetup() getter was used. Because
    // of this the SpectrumWorx::uncheckedEngineSetup() getter is used to
    // avoid the assertion failures.
    //   This is safe to do as a non-up-to-date engine setup is harmless here,
    // there will surely be a next message/asynchronous call when it will be up
    // to date).
    //                                        (15.06.2010.) (Domagoj Saric)
    auto const &engineSetup(editor.effect().uncheckedEngineSetup());
    auto const &parameters(editor.program().parameters());

    fftSize_->setValue(parameters.get<Engine::FFTSize>());
    overlapFactor_->setValue(parameters.get<Engine::OverlapFactor>());
    windowFunction_->setValue(parameters.get<Engine::WindowFunction>());

    if (enginePage_.setEngineInformation(engineSetup))
        enginePage_.repaint();
}

bool SpectrumWorxEditor::Settings::updateEngineInformation()
{
    /// \note The unchecked getter, for the reason updateEnginePage() gives
    /// above: this is called precisely while the setup and the parameters
    /// disagree, and reading it then is the whole point.
    if (!enginePage_.setEngineInformation(editor().effect().uncheckedEngineSetup()))
        return false;
    enginePage_.repaint();
    return true;
}

SpectrumWorxEditor::Settings::EnginePage::EnginePage() : PanelBackground(SettingsPage) {}

/// \note All four lines, and it answers whether any of them moved. They are held
/// as strings and compared rather than rebuilt inside paint(), which is what lets
/// the editor poll this at the modulation rate without repainting thirty times a
/// second.
///
/// \note It takes the setup rather than reading it, so paint() never calls the
/// *checked* `engineSetup()` getter -- which asserts that the setup agrees with
/// the spectral parameters, and which is false for as long as one is in flight.

bool SpectrumWorxEditor::Settings::EnginePage::setEngineInformation(Engine::Setup const &setup)
{
    auto const previousQuality(engineQuality_);
    auto const previousResolution(frequencyResolution_);
    auto const previousStep(timeResolution_);
    auto const previousLatency(latency_);

    float const qualityFactor(setup.wolaRippleFactor());
    // Implementation note:
    //   In this document http://eprints.kfupm.edu.sa/21525/1/21525.pdf (at the
    // end of page 32) it is argued that a variation of 0.03% or less is
    // negligible.
    //                                        (25.01.2010.) (Domagoj Saric)
    char const *description;
    if (qualityFactor < 0.0003f)
        description = "% (excellent)";
    else if (qualityFactor < 0.01f)
        description = "% (average)";
    else
        description = "% (poor)";
    // the buffer goes over whole, so staying inside it is the callee's promise
    char buffer[32];
    Utility::lexical_cast(qualityFactor * 100.0f, 2, buffer);
    // assigned rather than emptied in place: an empty juce::String owns no
    // buffer, pointing instead at a constexpr shared singleton in .rodata
    engineQuality_ = "Ripple Amount: ";
    engineQuality_ += buffer;
    engineQuality_ += description;

    auto const diagnostic([](char const *const title, float const value, char const *const suffix) {
        char valueStr[32];
        Utility::lexical_cast(value, 1, valueStr);
        juce::String line(title);
        line += ": ";
        line += valueStr;
        line += ' ';
        line += suffix;
        return line;
    });

    frequencyResolution_ =
        diagnostic("Frequency Resolution", setup.frequencyRangePerBin<float>(), "Hz");
    timeResolution_ = diagnostic("Time Resolution", setup.stepTime() * 1000, "ms");
    latency_ = diagnostic("Latency", setup.latencyInMilliseconds(), "ms");

    return (engineQuality_ != previousQuality) || (frequencyResolution_ != previousResolution) ||
           (timeResolution_ != previousStep) || (latency_ != previousLatency);
}

void SpectrumWorxEditor::Settings::EnginePage::paint(juce::Graphics &g)
{
    PanelBackground::paint(g);
    g.setColour(ColourMap::getColour(ColourMap::Text));
    g.setFont(DrawableText::defaultFont());

    auto const line([&g](juce::String const &text, unsigned int const verticalOffset) {
        g.drawFittedText(text, xMargin + 2, static_cast<int>(verticalOffset), 213, 18,
                         juce::Justification::centredLeft, 1);
    });

    const auto infoTextY = yMargin + yStep * 5 + 67;
    const auto lineHeight = 21;

    line(engineQuality_, infoTextY + (lineHeight * 0));
    line(frequencyResolution_, infoTextY + (lineHeight * 1));
    line(timeResolution_, infoTextY + (lineHeight * 2));
    line(latency_, infoTextY + (lineHeight * 3));
}

SpectrumWorxEditor::Settings::InterfacePage::InterfacePage()
    : PanelBackground(SettingsPage), zoom_(*this, xMargin, yMargin + 0 * yStep + yOffset, "Zoom"),
      palette_(*this, xMargin, yMargin + 1 * yStep + yOffset, "Color Scheme"),
      moduleUIMouseOverReaction_(*this, xMargin, yMargin + 2 * yStep + yOffset,
                                 "Mouse Over Reaction"),
      hideCursorOnKnobDrag_(*this, xMargin - 4, yMargin + 3 * yStep + yOffset,
                            "Hide cursor on knob drag")
{
    Settings &parent(
        Utility::ParentFromMember<Settings, InterfacePage, &Settings::interfacePage_>()(*this));

    // the percentage is the item's ID, so getValue() answers the zoom itself
    // rather than a position in this list something would have to translate
    for (auto const percent : Preferences::zoomPercentages)
    {
        std::array<char, 8> text;
        std::snprintf(text.data(), text.size(), "%u%%", percent);
        zoom_.addItem(percent, text.data());
    }
    zoom_.setSelectedID(preferences().zoomPercent());

    // spelled here rather than taken from ColourMap::nameOf(): what goes in the
    // preferences file has to be greppable and what goes in this list has to be
    // readable, and "DarkAmber" cannot be both
    palette_.addItem(ColourMap::ClassicBlue, "Classic Blue");
    palette_.addItem(ColourMap::ClassicRed, "Classic Red");
    palette_.addItem(ColourMap::ClassicGreen, "Classic Green");
    palette_.addItem(ColourMap::ClassicYellow, "Classic Yellow");
    palette_.addItem(ColourMap::ClassicAmber, "Classic Amber");
    palette_.addItem(ColourMap::ClassicPurple, "Classic Purple");
    palette_.addItem(ColourMap::ClassicGray, "Classic Gray");
    palette_.addItem(ColourMap::DarkBlue, "Dark Blue");
    palette_.addItem(ColourMap::DarkRed, "Dark Red");
    palette_.addItem(ColourMap::DarkGreen, "Dark Green");
    palette_.addItem(ColourMap::DarkYellow, "Dark Yellow");
    palette_.addItem(ColourMap::DarkAmber, "Dark Amber");
    palette_.addItem(ColourMap::DarkPurple, "Dark Purple");
    palette_.addItem(ColourMap::DarkGray, "Dark Gray");
    palette_.setSelectedIndex(preferences().palette());

    moduleUIMouseOverReaction_.addItem(Preferences::Never, "Never");
    moduleUIMouseOverReaction_.addItem(Preferences::WhenParentModuleSelected, "Module selected");
    moduleUIMouseOverReaction_.addItem(Preferences::WhenParentOrNothingSelected, "Always");
    moduleUIMouseOverReaction_.setSelectedIndex(preferences().moduleUIMouseOverReaction());

    hideCursorOnKnobDrag_.setToggleState(preferences().hideCursorOnKnobDrag(),
                                         juce::dontSendNotification);
    hideCursorOnKnobDrag_.addListener(&parent);
}

void SpectrumWorxEditor::Settings::InterfacePage::paint(juce::Graphics &graphics)
{
    graphics.setColour(ColourMap::getColour(ColourMap::Text));
    PanelBackground::paint(graphics);
}

#pragma warning(pop)

/// \note Drawn from the tab's name and a ButtonPainter rather than from a
/// bitmap per tab, which is what lets the widths below follow the words. Where
/// the bar itself goes is Settings::resized()'s question.
class SettingsTab : public juce::TabBarButton
{
  public:
    SettingsTab(juce::String const &tabName, juce::TabbedButtonBar &ownerBar)
        : TabBarButton(tabName, ownerBar)
    {
    }

  private:
    int getBestTabLength(int /*depth*/) override
    {
        return ButtonPainter::widthFor(getButtonText(), ButtonPainter::Tab);
    }

    bool hitTest(int /*mx*/, int /*my*/) override { return true; }

    void paint(juce::Graphics &graphics) override
    {
        ButtonPainter::paint(graphics, getLocalBounds().toFloat(), ButtonPainter::Tab,
                             getToggleState(), getButtonText());
    }
};

juce::TabBarButton *SpectrumWorxEditor::Settings::createTabButton(juce::String const &tabName,
                                                                  int const /*tabIndex*/)
{
    return new SettingsTab(tabName, getTabbedButtonBar());
}

/// \note Two of the indices arriving here are not a user's choice, and the range
/// check is what keeps either out of the session state: addTab() selects the
/// first tab as it adds it, and clearTabs() deselects with -1 on the way out.
/// JUCE reaches this whether or not `setCurrentTabIndex()` was asked to send the
/// change message, so opening the About page by the logo is remembered too.
void SpectrumWorxEditor::Settings::currentTabChanged(int const newCurrentTabIndex,
                                                     juce::String const & /*newTabName*/)
{
    if ((newCurrentTabIndex >= 0) && (newCurrentTabIndex < int{numberOfSettingsPages}))
        editor().editorHost().panelState().settingsPage =
            static_cast<unsigned int>(newCurrentTabIndex);
}

SpectrumWorxEditor &SpectrumWorxEditor::Settings::editor()
{
    return Utility::ParentFromOptionalMember<SpectrumWorxEditor, Settings,
                                             &SpectrumWorxEditor::settings_, false>()(*this);
}

void SpectrumWorxEditor::Settings::buttonClicked(juce::Button *const pButton)
{
    LE_ASSERT(pButton == &interfacePage_.hideCursorOnKnobDrag_);
    (void)pButton;

    preferences().setHideCursorOnKnobDrag(interfacePage_.hideCursorOnKnobDrag_.getToggleState());
}

} // namespace LE::SW::GUI
