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

////////////////////////////////////////////////////////////////////////////////
//
// Constants private to this module.
// ---------------------------------
//
////////////////////////////////////////////////////////////////////////////////

namespace Constants::Layout
{
unsigned int const textBoxHorizontalOffset = 114;
unsigned int const textBoxHeight = 33;
unsigned int const textBoxWidth = 170;

/// \brief What the main area's strings keep clear of their boxes' edges.
///
/// \note The boxes are painted by the skin and the text was laid out across
/// their full width, so a long effect title -- "Pitch Follower (PV)" -- ran into
/// the rounded ends. Only the fitting rectangle shrinks; the text is centred, so
/// nothing moves that was not already touching. \see issue #76.
///                                           (16.08.2026.)
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

      /// \todo Reimplement these Alex's widgets.
      ///                                       (22.09.2009.) (Domagoj Saric)
      //cSamplerDisplay ( CRect( 0, 0, 235, 129 ).offset( 222, 17 ), this, kBankSelect, 0, &resourceBitmap<kbLoad>(), &resourceBitmap<kbDel>(), &resourceBitmap<kbLock>() ),
      //cSpectrumDisplay( CRect( 0, 0, 235, 129 ).offset( 222, 17 ), this, SpectrumDisplay, 0, capture ),

      // buttons...
      /// \note These two lines never changed for issue #73 and never needed to:
      /// what was wrong was which file each name pointed at. \see resources.hpp.
      /// \note The size was the artwork's -- 57 x 24, of which 50 x 17 was the
      /// pill and the rest the room its halo needed. It is written down here
      /// now, and the two are placed three pixels apart as they were.
      ///                                       (18.08.2026.)
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

    /// \note The focus grab that was here moved to parentHierarchyChanged().
    /// A component can only take focus once it is on screen, and in 2016 the
    /// editor was constructed by a plugin that had already parented it. The
    /// CLAP shim builds it first and parents it after, so grabbing here asserts
    /// in JUCE and does nothing.
    ///                                       (29.07.2026.) (SW port)
    setDefaultFocusHandling();

    // Resizable VST GUI discussions:
    // http://www.kvraudio.com/forum/viewtopic.php?t=141313
    // http://lists.steinberg.net:8100/Lists/vst-plugins/Message/17785.html
    // http://www.u-he.com/vstsource
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

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The column, taken here rather than through requestEditorSize(),
    /// because at this point there is no window to resize: the shim builds this
    /// editor inside `guiCreate()` and then answers `guiGetSize()` out of the
    /// holder, which it sizes from *this* component. So an alwaysVisible editor
    /// has to already be the width it wants, and the host is told once, the way
    /// it is told 845 x 564 otherwise.
    ///                                       (06.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    if (panelPlacement_ == PanelPlacement::alwaysVisible)
    {
        panelHasOwnColumn_ = true;
        setSize(expandedWidth, estimatedHeight);
        openRestingPanel();
    }

    setOpaque(true);
    setVisible();

    /// \note Whatever the mailbox has been accumulating with no editor open is
    /// not this editor's news: it starts from what the widgets were built with.
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

    /// \note And nothing may be *pointing* at one either. A menu is asynchronous
    /// -- JUCE 8 defaults JUCE_MODAL_LOOPS_PERMITTED to 0 -- so a host closing
    /// the window with one down leaves a callback holding a `SafePointer` to a
    /// component that is going. The SafePointers are what make that survivable;
    /// this is what makes it not happen.
    ///                                       (02.08.2026.) (SW port)
    juce::PopupMenu::dismissAllActiveMenus();

    editorHost_.deregisterSampleLoadedListener(*this);

    /// \note Two spin-waits stood here, on plain `bool`s read through a
    /// `volatile` cast: "wait until whatever non-GUI thread is inside the LFO
    /// display or the shared controls has left". Both producers carried the
    /// comment "This gets called from a non GUI thread", and by the time this
    /// port began neither did -- the editor's `updateForNewTimingInfo()` had no
    /// live caller at all, and `updateLFO()` is reached from
    /// `parameterChangedElsewhere()`, which is the main thread draining a
    /// message. Stage 5 removed the last route by which the audio thread could
    /// have reached either. `updateForNewTimingInfo()` has a caller again as of
    /// 09.08.2026 and it is the same kind: a `ToUI` message, drained here.
    ///
    ///   A `volatile` read is not a synchronisation primitive in any case, so
    /// this was never the guarantee it looked like; what made it work was that
    /// the loops always exited immediately.
    ///                                       (02.08.2026.) (SW port)

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

/// \note `attachToHostWindow` had three overloads here -- a Win32 SetParent, a
/// Cocoa NSView one and a 32 bit Carbon HIView one -- and no callers on any
/// platform: the CLAP shim parents the editor. Deleted with the rest of the
/// owned-window machinery in stage 6.4, and they took `-framework Carbon` with
/// them.
///                                           (01.08.2026.) (SW port)

/// \note Walks up to the nearest enclosing editor rather than to the top-level
/// component. Those were the same thing in 2016: VST 2.4 and AU parented the
/// editor straight into the host's window, so "the top" *was* the editor.
/// clap-wrapper's JUCE shim nests it two deep instead --
/// implDesktop -> implHolder -> editor (clap_juce_shim_impl.cpp) -- so the old
/// walk landed on implDesktop, and the downcast asserted in a checked build and
/// silently produced a bad pointer in a release one. Every widget that asks its
/// editor for the engine setup went through here, so this failed as soon as a
/// module's widgets were built.
///                                           (29.07.2026.) (SW port)
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

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxEditor::showPanel()
// -------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The whole of what stage 6.4 replaced ~500 lines of OwnedWindowBase
/// with. The panel is an ordinary child; the only thing that needed saying is
/// that it goes on top -- gradient_ raises itself to always-on-top for a module
/// drag, and a stale one would otherwise paint through this. *Where* it goes is
/// layOutPanels()' answer and not this function's, because the placement can
/// change under a panel that is already up.
///                                           (01.08.2026, amended 06.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

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

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxEditor::layOutPanels()
// ----------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   Where the three placements are the three placements, and the only code that
/// reads panelPlacement_. Everything that opens or shuts a panel ends here, so
/// there is one answer to "how wide is the editor and where is the panel" rather
/// than one per entry point.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::layOutPanels()
{
    auto *const pPanel(currentPanel());

    /// \note alwaysVisible keeps the column whether or not anything is in it: it
    /// is what the resting panel is opened *into*, and holding it means the
    /// editor does not flicker a width between one panel closing and the next
    /// opening.
    bool const wantColumn((panelPlacement_ == PanelPlacement::alwaysVisible) ||
                          ((panelPlacement_ == PanelPlacement::expandContract) && pPanel));

    auto const ownColumn(setPanelColumnVisible(wantColumn));

    /// \note The skin moves and the panel does not follow it: the column is at
    /// the left edge either way, and an overlay is a position *in* the skin --
    /// which is the editor's own coordinates only while there is no column.
    mainArea_.setTopLeftPosition(ownColumn ? mainAreaX : 0, 0);
    if (pPanel)
        pPanel->setTopLeftPosition(ownColumn ? panelColumnX : overlayX, overlayY);
}

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxEditor::setPanelColumnVisible()
// -------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The host is asked first and the editor follows its answer, because the
/// window is the host's and this is a request. One that refuses to *grow* leaves
/// everything as it was and the panel goes over the module strips, which is
/// exactly overlay placement -- so a host with no `clap.gui` at all gets the
/// stage 6.4 editor and nothing worse. One that refuses to *shrink* has already
/// been handed the space back and the worst of that is a margin, so the editor
/// contracts either way.
///                                           (06.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

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

    /// \note In skin pixels, like every other caller: the scaling to window
    /// units happens on the far side, out of the same preference this has just
    /// written.
    ///
    /// \note And an announcement rather than a request -- the editor has already
    /// changed and there is no fallback to negotiate for. \see
    /// EditorHost::editorSizeChanged(), which has what treating it as a request
    /// looked like.
    editorHost_.editorSizeChanged(getWidth(), getHeight());
}

void SpectrumWorxEditor::panelPlacement(PanelPlacement const placement)
{
    if (placement == panelPlacement_)
        return;

    panelPlacement_ = placement;

    /// \note Leaving alwaysVisible drops the resting panel, because nothing the
    /// user asked for is in it -- a panel they *did* ask for stays up and moves.
    if ((placement != PanelPlacement::alwaysVisible) && settings_.has_value() &&
        !settingsButton_.getToggleState())
        settings_ = std::nullopt;

    if ((placement == PanelPlacement::alwaysVisible) && !currentPanel())
    {
        openRestingPanel();
        return;
    }
    layOutPanels();
}

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxEditor::openRestingPanel()
// --------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   The preset browser, and its button lit like any other way of opening it.
///
/// \note In `alwaysVisible` the column cannot be empty, so the two buttons stop
/// being independent toggles and become a two-way selector: pressing the lit one
/// lands back here. Which is why this goes through showPresetBrowser() rather
/// than building the panel itself -- there is one way to put the browser up, and
/// the button state and the layout follow from it either way.
///
/// \note The About page is the fallback and not the resting state. It was the
/// resting state briefly, on the reasoning that it is the one panel with nothing
/// in it to get wrong; the browser is what a user opens the plugin for.
///                                           (06.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::openRestingPanel()
{
    LE_ASSERT(panelPlacement_ == PanelPlacement::alwaysVisible);
    LE_ASSERT(!currentPanel());

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
/// \note Only *builds* a browser when there is none, as showSettings() does. It
/// used to rebuild unconditionally, which was invisible while the only way here
/// was a button that had just been off -- and is not, now that `alwaysVisible`
/// rests on an open browser: pressing PRESETS would have thrown away whichever
/// bank the user was in and dropped them back at the root.
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

/// \note Where the constructor's focus grab went. Fires when the host's window
/// takes the editor, which is the first moment it can hold focus.
void SpectrumWorxEditor::parentHierarchyChanged()
{
    if (isShowing() || isOnDesktop())
        grabKeyboardFocus();
}

////////////////////////////////////////////////////////////////////////////////
//
// Reordering the rack
// -------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   A strip can be dropped in two ways and they do different things. On another
/// strip it changes places with it, and nothing else moves; between two of them,
/// or at either end, it is inserted there and everything from there on shifts
/// along. Which of the two a given point means is moduleDropAt()'s answer, drawn
/// by DropIndicator and carried out by applyModuleDrop() -- one function each, so
/// that what the user is shown while dragging and what happens when they let go
/// cannot disagree.
///
/// \note The middle half of a strip is a swap and its outer quarters are inserts,
/// which is what makes both reachable without aiming: an insert is a gap, and a
/// gap with no width to it is a target nobody can hit. The zone also runs half a
/// strip past each end of the rack, so dropping at the front or the back does not
/// need the strip to be over the rack at all.
///
/// \note And the point all of that is measured at is the **dragged strip's own
/// middle**, not the pointer. \see draggedStripCentre().
///                                           (14.08.2026.) (SW port)
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
//
// SpectrumWorxEditor::draggedStripCentre()
// ----------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   A drag is aimed with the strip, not with the pointer. Somebody who picked a
/// strip up by its left edge is carrying the whole thing, and what they are
/// lining up against the rack is the middle of what they can see -- which is the
/// column the eject `X` is drawn in, that marker being centred on the strip. It
/// is also where JUCE's drag image is: `startDragging()` snapshots the strip and
/// offsets the ghost by the grab, so the ghost sits where the strip would be.
///
///   Deciding the drop from the pointer instead put the answer out by however far
/// from the middle it was picked up -- up to half a strip, which is exactly the
/// width of a swap zone. Carrying a strip until it covered another one and being
/// told "insert to the left of it" is what that looked like, and which way it was
/// wrong depended on which edge had been grabbed.
///                                           (14.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

juce::Point<int> SpectrumWorxEditor::draggedStripCentre(juce::Point<int> const pointer,
                                                        juce::Point<int> const grabbedAt)
{
    /// \note The strip itself has not moved -- it stays in its slot for the whole
    /// drag -- so this is where it *would* be: the pointer, less where in the
    /// strip it was picked up, plus half a strip.
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

    /// \note The two margins first: past either end of the rack there is no strip
    /// to be over, so the only thing a drop there can mean is the gap it is past.
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

    /// \note And the drops that would change nothing, which are three: swapping a
    /// strip with itself, and the two gaps either side of it -- it is already in
    /// both. Answering "nothing" for those is what stops the indicator offering a
    /// move the user would not see happen.
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
    /// \note Asked again rather than remembered from the last moduleDrag(): the
    /// two answers are the same function of the same point, and a click that never
    /// became a drag has no last answer to remember. Such a click leaves the strip
    /// exactly where it was, which moduleDropAt() calls "nothing".
    auto const drop(moduleDropAt(moduleUI.slot(), draggedStripCentre(moduleUI, event)));

    dropIndicator_.hide();

    applyModuleDrop(moduleUI.slot(), drop);
}

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxEditor::applyModuleDrop()
// -------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The rack is told first and the engine second, which is the order every
/// edit takes now: the strips move here so the drag ends where the user let go,
/// and resyncModuleRack() puts them where the chain says once the engine has
/// caught up. They agree either way -- this is the same permutation, applied to
/// the same two orderings. The host is told last, because what it is told is read
/// back out of the rack.
///
////////////////////////////////////////////////////////////////////////////////

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
        /// \note A gap index is one more than the slot to its left, and taking the
        /// source strip out closes every gap after it -- so a gap beyond the
        /// source names a slot one lower once the strip has left.
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

////////////////////////////////////////////////////////////////////////////////
///
/// \note Two moves, because a move is the only reordering primitive the chain
/// has and a swap of two strips that are not neighbours is not one. Taking the
/// first to where the second is drags the second back one place -- everything
/// between them shifted left to close the gap -- so bringing it from there to
/// where the first was puts the block between them back where it started. For
/// neighbours the first move is already the swap and the second is a no-op, which
/// is why it is skipped rather than special-cased.
///
////////////////////////////////////////////////////////////////////////////////

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

    /// \note Only the two, unlike a move: nothing between them changed slots.
    host().gestureBegin("Swap modules");
    host().moduleChangedByUser(first, effectInRackSlot(first));
    host().moduleChangedByUser(second, effectInRackSlot(second));
    host().gestureEnd();
}

////////////////////////////////////////////////////////////////////////////////
//
// The right-click effect menu
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
///   The same list of effects the add-module button opens, asked in three places
/// and meaning something different in each: on a strip it replaces that effect,
/// between two it goes in there and shifts the rest along, and past the last one
/// it is simply added. Which of the three is the heading on the menu, so that the
/// answer is read before anything is chosen rather than discovered afterwards.
///
/// \note The zones are the drag's in shape -- a band at each end of a strip means
/// "between these two" and the middle means "this one" -- but narrower. A drag
/// draws where it would land and can be walked back before the button is let go;
/// a right-click is committed to the moment it goes down, and the heading is the
/// first the user hears of which of the two they asked for. So the seam is
/// something to be aimed at here and something to be fallen into there.
///                                           (14.08.2026.) (SW port)
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
    /// \note The whole rack, not the filled part of it: what is past the last
    /// strip is where a module is added, and that is the emptiest and most
    /// obvious place to ask for one.
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

    /// \note And a full rack has no gaps to insert into, so its strips are
    /// replaceable edge to edge. Offering an insert that would have to drop
    /// somebody's last module to make room is the one answer nobody wants.
    if (filled < SW::Constants::maxNumberOfModules)
    {
        auto const acrossTheStrip(alongTheRack % slotWidth);
        if (acrossTheStrip < menuInsertZone)
            return {EffectMenuTarget::insert, slot};
        if (acrossTheStrip >= (slotWidth - menuInsertZone))
        {
            /// \note Except off the right of the last strip, which is the end of
            /// the rack rather than a gap in it -- and inserting there is adding.
            /// Said as an add so that the heading does not change across a
            /// boundary the user cannot see and the action does not.
            auto const gap(static_cast<std::uint8_t>(slot + 1));
            return (gap < filled) ? EffectMenuTarget{EffectMenuTarget::insert, gap}
                                  : EffectMenuTarget{EffectMenuTarget::append, filled};
        }
    }

    return {EffectMenuTarget::replace, slot};
}

/// \note A replacement names what it would replace -- "Replace Tonal Effect" --
/// because that is the one of the three where the user has pointed at something
/// in particular, and a heading that read "Replace with" left them checking which
/// strip the menu had opened over. The other two point at a gap, which has no
/// name.
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
    /// \note `none` never gets this far -- showEffectMenuAt() answers it by not
    /// opening a menu -- so this is the switch being total, not a fourth heading.
    return "Add Effect";
}

PopupMenu::OnChosen SpectrumWorxEditor::effectMenuCallback(EffectMenuTarget const target)
{
    /// \note The same SafePointer the add-module button has always taken: the
    /// menu no longer blocks, so the editor can be torn down while it is open.
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
    /// \note The main area rather than the strip that was clicked: the rack is
    /// rebuilt from the chain and a strip can be replaced under an open menu,
    /// which JUCE would take as its cue to dismiss it.
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
    /// \note Not an assertion, for removeModule()'s reason: the menu is
    /// asynchronous, so the strip it was opened on can leave the rack -- a host
    /// changing a slot, a preset arriving -- while it is down.
    if (slot >= nextAvailableModuleSlot_)
        return;
    if (effectInRackSlot(slot) == static_cast<std::int8_t>(effectIndex))
        return; // Already that effect; nothing to tell anybody.

    /// \see the note in applyModuleDrop() for why the chain is not walked while
    /// the host is being told about it.
    Host2PluginInteropControler::AutomationBlocker const automationBlocker(
        /*host*/ moduleChainOwner /*mrmlj*/ ());

    if (!setModuleInSlot(slot, static_cast<std::int8_t>(effectIndex)))
        return; // The effect is not in this build; nothing was asked of the engine.

    /// \note As the add-module menu does: what the user just asked for is what
    /// they are about to reach for.
    slotAwaitingFocus_ = slot;

    host().gestureBegin("Replace module");
    host().moduleChangedByUser(slot, static_cast<std::int8_t>(effectIndex));
    host().gestureEnd();

    refreshModuleRackAsync();
}

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxEditor::insertModuleAtGap()
// ---------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note Added on the end and then moved, because those are the two things the
/// chain can be asked for and an insert is not one of them. Both edits reach the
/// engine through the same ring and it drains the whole ring per block, so the
/// two are one change as far as anything processing is concerned -- the same
/// reasoning swapModuleSlots() runs on.
///
/// \note No strip is moved here, unlike a drag: the module the user asked for has
/// no strip yet -- it gets one when resyncModuleRack() next runs -- so there is
/// nothing to move it *around*, and the rack is left as it is until then. Which
/// is also what makes effectInRackSlot() below the right thing to ask: it still
/// describes the rack the menu was opened over.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::insertModuleAtGap(std::uint8_t const gap, std::uint8_t const effectIndex)
{
    auto const filled(nextAvailableModuleSlot_);
    if ((gap >= filled) || (filled >= SW::Constants::maxNumberOfModules))
        return; // The rack changed under the open menu. \see replaceModuleInSlot().

    Host2PluginInteropControler::AutomationBlocker const automationBlocker(
        /*host*/ moduleChainOwner /*mrmlj*/ ());

    if (!setModuleInSlot(filled, static_cast<std::int8_t>(effectIndex)))
        return; // The effect is not in this build; nothing was asked of the engine.
    editorHost().editModuleMove(filled, gap);

    slotAwaitingFocus_ = gap;
    moduleAdded();

    /// \note The new module, and every one it pushed along -- which is
    /// removeModule()'s loop with the shift the other way.
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
/// \note Which the name below settles for good: there is no colour here to be
/// initialised in any order at all. It was a juce::Colour until the palettes
/// arrived, and a colour taken at static-initialisation time is taken before a
/// palette has been chosen -- so these four strings stayed the skin's original
/// blue and white whatever the user had picked.
///                                       (18.08.2026.)
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

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxEditor::MainArea
// ----------------------------
//
////////////////////////////////////////////////////////////////////////////////

SpectrumWorxEditor::MainArea::MainArea()
{
    setSize(BackgroundStyle::width, BackgroundStyle::height);
    setOpaque(true);

    /// \note Neither wanted nor grabbed: this is a background, and the component
    /// a click on it should focus is the editor. JUCE walks up to the first
    /// parent that wants the keyboard, which is what this being transparent to
    /// both flags leaves in place.
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

/// \note The logo, and it is this component's rather than the editor's because
/// the rectangle is a position in the skin -- which is what this component's
/// coordinates are and what the editor's stopped being when the panel column
/// went to the left.
///
/// \note And the right button, which is the rack's empty slots asking for an
/// effect. The strips have their own handler for it; this is what is left of the
/// skin underneath them. \see showEffectMenuAt().
void SpectrumWorxEditor::MainArea::mouseDown(juce::MouseEvent const &event)
{
    if (event.mods.isPopupMenu())
    {
        editor().showEffectMenuAt(event.getScreenPosition());
        return;
    }

    if (logoArea().contains(event.x, event.y))
    {
        /// \note Was 3, and there are three tabs. JUCE clamps an out of range
        /// index to -1, so clicking the logo raised the panel with *no* page
        /// selected -- an empty transparent window over the desktop, which is
        /// why nobody saw it, and an empty panel over the editor now. The About
        /// page this means is index 2.
        ///                                   (01.08.2026.) (SW port)
        editor().showSettings(aboutPageIndex);
    }
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note What is left of the editor once the skin is a child of it: the column
/// the panel sits in, and the build-stamp bar.
///
///   The column is the bitmap's *first* pixel column stretched across it. That
/// column is the skin's outer surround -- flat, top to bottom, because the
/// rounded rectangle holding the knobs starts a few pixels inside it -- so
/// stretching it is what "the panel sits on the same chrome" looks like, and it
/// stays right for a skin that changes the colour. The alternative was a constant
/// here that a new skin would silently disagree with. This component is opaque,
/// so something has to cover it.
///                                           (06.08.2026, mirrored 14.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::paint(juce::Graphics &graphics)
{
    /// \note Only what is outside the skin: the gutter beside the panel column.
    /// The skin itself is MainArea's.
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
            showSettings(0);
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
    /// \note The skin's coordinates, which are the main area's. \see MainArea.
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

/// \note The check where an assertion was. There is no display between one
/// control being deactivated and the next being selected -- and this is called
/// from the timer, which does not know that -- so the assertion described a
/// state that does not hold and a shipped build dereferenced an empty optional
/// on the strength of it. The `catch (...)` around it was catching nothing: none
/// of the three calls throws, and an empty optional is not an exception in any
/// case.
///                                           (08.08.2026.) (SW port)
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

////////////////////////////////////////////////////////////////////////////////
///
/// \note The box shows the **source**, which is a file's name only when a file is
/// what was selected. It is never empty: "nothing" is not one of the three
/// answers, and an empty box was 2016's way of saying `Main` without a word for
/// it. \see doc/tech/sidechain-approach.md.
///
////////////////////////////////////////////////////////////////////////////////

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

/// \note "Async" is 2016's, and the branch it names is currently unreachable:
/// loading happens on this thread inside setNewSample(), so the host is never
/// mid-load when it is asked. Kept whole rather than collapsed to
/// updateSampleName(), because whether the loader gets a thread again is the
/// threading redesign's decision and this is the shape it would come back in.
///                                           (01.08.2026.) (SW port)
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

/// \note The dialog is raised here rather than by the loader, and this is the
/// only call site that raises one: a user picked this file out of a browser a
/// moment ago and is owed an answer about it. Every other caller of
/// `setNewSample` is a preset or a session being loaded, where there is nobody to
/// answer a modal box and possibly no window to put one in.
///                                           (08.08.2026.) (SW port)
////////////////////////////////////////////////////////////////////////////////
///
/// \note Picking one of these **discards** a loaded file. The box has three
/// answers and it should not be hiding a fourth piece of state behind one of
/// them; \see the note on `SpectrumWorxCLAP::setSideChainSource()`.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::sideChainSourceSelected(SideChainSource const source)
{
    editorHost_.setSideChainSource(source);
    updateSampleName();
}

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
/// \note And it is deliberately the rack. A slot change is a request now, so
/// between the click and the engine applying it the two disagree -- and every
/// caller here is describing what the *user* did, which is a statement about
/// what they were looking at.
///                                           (02.08.2026.) (SW port)
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

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note A strip whose module has already left the chain is still on screen
    /// and still clickable until the resync runs -- a window this editor did not
    /// used to have, because a module owned its own strip and destroying the
    /// module destroyed it.
    ///
    ///   Clicking one was the reported "Invalid downcast": the slot came from the
    /// stale strip's *position*, so ejecting the last one and then its ghost gave
    /// slot 2 against a `nextAvailableModuleSlot_` of 2, and the walk that
    /// followed ran 255 times off the end of the chain and onto its sentinel node.
    /// That walk is gone -- the rack is a function of the chain now, computed in
    /// one place -- and this is what is left of the guard.
    ///                                       (02.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    ///   Not an assertion: clicking a strip that is on its way out is something
    /// a user does, not a fault. It only used to be impossible.
    auto const slot(moduleUI.slot());
    if (slot >= nextAvailableModuleSlot_)
    {
        refreshModuleRackAsync();
        return;
    }

    setModuleInSlot(slot, AutomatedModuleChain::noModule);

    /// \note What the host is told, and it is told what was *asked for* rather
    /// than what the chain currently holds -- which may still be the old chain
    /// for another block. Removing a module shifts every later one down a slot,
    /// so every selector from here to the end moves.
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
    /// \note Both copies, and the module building that goes with each, belong to
    /// the host now -- it is the one that owns them. \see EditorHost::editSlot().
    return editorHost().editSlot(slotIndex, effectIndex);
}

void SpectrumWorxEditor::addUserAddedModule(std::uint8_t const effectIndex)
{
    // Implementation note:
    //   This is certainly executed from the GUI thread so this function expects
    // the module creation to be done synchronously, in order for the focus
    // grabbing to be safe.
    //                                        (06.07.2011.) (Domagoj Saric)
    /// \note It no longer is, and cannot be: filling a slot is a request the
    /// engine answers when it next runs. So the focus is asked for by slot and
    /// taken by resyncModuleRack() when the strip exists -- which is the same
    /// moment, when nothing is processing, and one block later when something
    /// is.
    ///                                       (02.08.2026.) (SW port)
    LE_ASSERT(isThisTheGUIThread());
    // Implementation note:
    //   We want any user-added module (using the add module menu) to
    // automatically gain focus.
    //                                        (09.02.2010.) (Domagoj Saric)
#ifdef _WIN32
    /// \note There was a runDispatchLoopUntil( 2 ) here, pumping the message
    /// queue before taking focus. It was a 2013 quick-fix for crashes in
    /// SoundForge 10, whose own diagnosis -- recorded at the time -- was that
    /// SetFocus() let Windows deliver a queued message that called
    /// loadProgramState() in the middle of adding a module. That is a re-entrancy
    /// hazard being treated by draining the queue *first*, which is the same
    /// hazard a moment earlier.
    ///
    ///   It cannot survive the port in any case: runDispatchLoopUntil() only
    /// exists when JUCE_MODAL_LOOPS_PERMITTED is 1, and this build sets it to 0 --
    /// the premise of the stage 6 rewrite that made the menus and dialogs
    /// asynchronous. A nested dispatch loop in the middle of a slot change is
    /// precisely what that setting exists to forbid.
    ///
    ///   If SoundForge 10 ever matters again, the fix is to make the add-module
    /// path re-entrancy-safe rather than to pick a quieter moment for it.
    ///                                       (30.07.2026.) (SW port)
    LE_ASSERT(getWantsKeyboardFocus());
    LE_ASSERT(getMouseClickGrabsKeyboardFocus());
    this->grabKeyboardFocus();
#endif // _WIN32

    std::uint8_t const changedSlot(nextAvailableModuleSlot_);
    if (!setModuleInSlot(changedSlot, static_cast<std::int8_t>(effectIndex)))
        return; // The effect is not in this build; nothing was asked of the engine.

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
    /// \note A preset saved with the browser's "Ignore external audio" on names
    /// no file, so it cannot honestly say its side channel comes from one. The
    /// other two sources are unaffected: what that toggle withholds is somebody
    /// else's audio, not the patch's routing.
    auto source(editorHost_.sideChainSource());
    if (externalSample.empty() && (source == SideChainSource::File))
        source = SideChainSource::Main;

    /// \note Where the interface's `juce::String` becomes the format's bytes, and
    /// the last thing presetFile.cpp used to do before it was deleted.
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
/// \note `+ 1 /*Bypass*/`, because `ModuleControlBase::moduleParameterIndex()`
/// is the **LFO-able** index -- Bypass is not LFO-able and is not counted -- and
/// a `ParameterID::Module` carries the module parameter index, which counts it.
/// The two differ by one and every other reader of that getter says so
/// (`moduleControl.cpp`, three times).
///
///   This did not, so the gesture that brackets a knob drag named the parameter
/// *before* the one being dragged: touching a module's Gain announced a gesture
/// on its Bypass, and the values that followed were Gain's. A host that binds its
/// automation gutter to the last touched parameter -- Bitwig does -- then bound
/// it to Bypass, and dragging that lane moved nothing on screen because the knob
/// it moved was not the one the user had touched. The *values* were always right:
/// they go through updateModuleParameterAndNotifyHost(), whose callers add the
/// one. Only the gesture was wrong, which is why this survived the automation
/// tests.
///                                           (07.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

ParameterID SpectrumWorxEditor::moduleControlID(ModuleControlBase const &control) const
{
    ParameterID parameterID;
    parameterID.value.type = ParameterID::ModuleParameter;
    parameterID.value._.module.moduleIndex = moduleChain().getIndexForModule(control.module());
    parameterID.value._.module.moduleParameterIndex =
        static_cast<std::uint8_t>(control.moduleParameterIndex() + 1 /*Bypass*/);
    return parameterID;
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

    host().automatedParameterBeginEdit(moduleControlID(control));
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

    host().automatedParameterEndEdit(moduleControlID(control));
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
/// \note This is the general form of a workaround the 2016 code carried in
/// `ModuleUI::buttonClicked`, deactivating the active control by hand before
/// removing the module, with an "...investigate why this doesn't work when
/// placed inside the ModuleUI destructor..." beside it. It worked because the
/// whole eject happened inside one message callback, so the *deferred* LFO
/// display destruction could not run in between. Dropping a strip is its own
/// posted message now, so it can and does.
///
/// \note Deliberately **not** `moduleControlDectivated()`: that ends the host's
/// automation gesture, and `moduleControlID()` walks the chain for the control's
/// module -- which by the time a strip is dropped has left it. The gesture is
/// ended on the path that removes the module, where the module is still there.
///                                           (02.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::detachFrom(ModuleUI &region)
{
    LE_ASSERT(isThisTheGUIThread());

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Each of the three asks whether **it** points into the strip, which
    /// is the question, and is not the question the first two used to ask.
    ///
    ///   `lfoDisplay_` was guarded on `pActiveControl_` and the shared controls
    /// on `pSelectedModule_` -- the editor's record of what is *current*, not of
    /// what these two are pointing at. Deactivation is deferred: it clears those
    /// records and leaves the widgets alive, still parented to the editor, still
    /// holding a raw `ModuleUI *` into the strip. So a control deactivated before
    /// the strip was dropped made both guards false and both widgets survived,
    /// pointing into freed memory -- and painting one reads
    /// `ModuleControlBase::isLFOEnabled()` -> `module()` -> `moduleUI().pModule_`
    /// straight through it.
    ///
    ///   `drainEngineEvents()` resynchronises the rack synchronously from
    /// `on_main_thread()`, so the window between the deactivation and the strip
    /// being freed is as long as the host's callback interval.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    bool const activeControlIsRegions(pActiveControl_ && pActiveControl_->pointsInto(region));
    bool const lfoDisplayIsRegions(lfoDisplay_ && lfoDisplay_->pointsInto(region));
    bool const sharedControlsAreRegions(sharedModuleControls_ &&
                                        sharedModuleControls_->pointsInto(region));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The pointer first, before anything is destroyed. Destroying a
    /// component moves the keyboard focus and JUCE delivers the loss
    /// *synchronously* to whichever control had it, which comes back here through
    /// `ModuleControlBase::reportInactiveControl()`. That guards on `isActive()`,
    /// so a cleared `pActiveControl_` makes the re-entry a no-op -- and a live one
    /// takes it into `moduleControlDectivated()`, which is what the note above
    /// says must not happen on this path.
    ///
    ////////////////////////////////////////////////////////////////////////////

    if (activeControlIsRegions)
        pActiveControl_ = nullptr;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And then both of these are destroyed **now**, where
    /// `retireLFODisplay()` and `moduleDeactivated()` only disable them and post
    /// a message to do it later. That deferral is an optimisation for the case
    /// they were written for -- a user moving between controls, or between
    /// modules, where the strip they point into survives -- and it is not
    /// available here, because here the strip is about to be freed.
    ///
    ///   Both are children of the *editor* rather than of the strip
    /// (`moduleControl.cpp:145-148` says so of the shared controls) and both hold
    /// a raw `ModuleUI *`. So a deferred cleanup leaves them in the component
    /// tree, still painted, pointing at freed memory -- and painting one reads
    /// `ModuleControlBase::isLFOEnabled()` -> `module()` -> `moduleUI().pModule_`,
    /// straight through the hole.
    ///
    ///   Found with ASan under Bitwig, deleting a module: heap-use-after-free
    /// reading a freed 1496-byte `ModuleUI`, freed by `resyncModuleRack()`'s
    /// `pRegion.reset()` a moment earlier, read by `ModuleKnob::paint`. Both on
    /// the main thread -- this is a lifetime bug and not a race. It needs a host
    /// that resyncs the rack from `clap_plugin::on_main_thread` while CoreAnimation
    /// can paint in between, which is why the standalone never showed it.
    ///                                       (07.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    if (lfoDisplayIsRegions)
    {
        retireLFODisplay(); // for the focus handling it does on the way out
        lfoDisplay_ = std::nullopt;
    }

    if (sharedControlsAreRegions)
        sharedModuleControls_ = std::nullopt;
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

/// \note Moved here from gui.cpp. It instantiates globalParameterChanged<>,
/// which reaches host() and so needs the complete SpectrumWorx; leaving it in
/// the widget layer meant every widget translation unit pulled in the 2016 VST2
/// plugin class and the deleted VST 2.4 SDK behind it.
///                                       (28.07.2026.) (SW port)
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
    /// \note Everything, whatever the chain holds. This walked the chain and
    /// asked each module to destroy the strip it owned; the strips are here now,
    /// and dropping one drops its reference to its module.
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

/// \note Where a tempo change lands. Its 2016 caller was
/// `SpectrumWorx::updatePosition()`, in a host class that is in no target, and
/// the CLAP's equivalent -- `updateLFOTiming()` -- runs on the audio thread,
/// where reaching a widget is what this whole redesign is about. So it arrives as
/// `ToUI::TimingChanged` and `drainEngineEvents()` calls this.
///
///   Only a *synced* LFO moves: its period is a fraction of the host's bar, so
/// the same parameter is a different number of seconds at a new tempo and snaps
/// to a different grid. A free one is measured against a bar that never changes
/// length and is unaffected -- see how-lfo-rates-and-eval-work.md.
///                                           (02.08.2026, wired up 09.08.2026.) (SW port)
void SpectrumWorxEditor::updateForNewTimingInfo()
{
    LE_ASSERT(isThisTheGUIThread());
    if (lfoDisplay_ && lfoDisplay_->isEnabled())
        lfoDisplay_->updateForNewTimingInfo();
}

void SpectrumWorxEditor::updateLFO(ModuleUI const &moduleUI, std::uint8_t const parameterIndex,
                                   std::uint8_t const lfoParameterIndex,
                                   Plugins::AutomatedParameterValue const value)
{
    LE_ASSERT(isThisTheGUIThread());
    if (lfoDisplay_ && lfoDisplay_->isEnabled())
        lfoDisplay_->updateForChangedParameters(moduleUI, parameterIndex, lfoParameterIndex, value);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note This called `Module::setParameterValueFromUI()` -- the engine, written
/// from the message thread while `process()` was reading it -- and used the
/// snapped value it returned. It queues now.
///
///   Nothing is lost by not waiting for the answer. Snapping is a pure function of
/// the parameter's *static* description, and the widget already carries that: a
/// knob's range and interval are set from the same `ParameterInfo`
/// (`ModuleKnob::setupForParameter`), a combo box's value is an index, a button's
/// is a bool. So the value arriving here is the snapped one, which is what the old
/// assertion in `Module::setEffectParameter` says too.
///
/// \see doc/tech/threading_model.md §3.
///
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// What the engine has been doing
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// The module strips
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

    /// \note A module that already has a strip keeps it and moves; only a newly
    /// created one needs building. Both cases matter: a slot can be refilled with
    /// the same effect, and strips shift left when one ahead of them is removed.
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
            /// \note Swallowed, because this can run while a preset is being
            /// applied and a strip that failed to build is not worth taking the
            /// host down for. A checked build stops here, because a slot with no
            /// strip is invisible in exactly the way that wastes an afternoon.
            ///                               (29.07.2026.) (SW port)
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
/// \note The one place the two are reconciled, and the reason there is only one.
/// The rack used to be maintained incrementally -- a strip built here, a strip
/// dropped there, every other strip shifted by a slot width at each call site --
/// against a running total in `nextAvailableModuleSlot_`. Any disagreement
/// between that total and the chain was then a walk off the end of it (§6.5a).
///
///   It also has to be a recomputation rather than an update, because a slot
/// change is a request now: between the click and the engine applying it the
/// rack is what the user asked for and the chain is what is playing, and this is
/// what closes the gap when the answer arrives. `ToUI::ChainChanged` is one
/// caller; the editor's own edits, which want the strip moved before the engine
/// has caught up, are the others.
///                                           (02.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::resyncModuleRack()
{
    LE_ASSERT(isThisTheGUIThread());

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Before any strip is destroyed, and for the same reason
    /// `~SpectrumWorxEditor` does it: a menu opened from a module strip -- its
    /// effect combo box, its LFO type -- is asynchronous, and this is where the
    /// strip under it can go. A module ejected, a preset with fewer modules than
    /// the rack has, a resync arriving because the engine caught up: all three
    /// come through here.
    ///
    /// \note It is queued rather than immediate. `dismissAllActiveMenus()` ends
    /// the modal state, and `ModalComponentManager` delivers the result through
    /// `triggerAsyncUpdate()` -- so the menu stops taking input here and answers
    /// "dismissed" a message-loop turn later, rather than answering with a choice
    /// made against a module that has since left the chain.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
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

        /// \note Before the reset, never after: destroying a strip moves the
        /// keyboard focus, and JUCE delivers that to whichever control had it.
        /// See detachFrom().
        detachFrom(*pRegion);
        pRegion.reset();
    }

    std::uint8_t slot(0);
    chain.forEach<Module>([&](Module &module) {
        /// \note Builds one when the module has none, which is how a chain the
        /// engine installed -- a preset, a session, a host-driven slot change --
        /// comes to have strips at all.
        createModuleRegion(LE::Utility::IntrusivePtr<Module>(&module), slot);
        ++slot;
    });
    setLastModulePosition(slot);

    /// \note And whatever the mailbox is holding is about the rack that was here
    /// a moment ago. It coalesces rather than queues, so there is nothing to
    /// deliver late -- an LFO that is still running writes again within the
    /// block, and one that is not has nothing to say.
    ///                                       (02.08.2026.) (SW port)
    editorHost().modulatedValues().discardChanges();

    if (slotAwaitingFocus_ != noSlotAwaitingFocus)
    {
        if (auto *const pRegion = regionInRackSlot(slotAwaitingFocus_))
        {
            slotAwaitingFocus_ = noSlotAwaitingFocus;
            /// \note JUCE's own precondition for the call, spelled out: a
            /// component that is on no screen cannot take the keyboard, and
            /// `grabKeyboardFocus()` says so with a jassert rather than by
            /// declining. An offscreen render reaches here with the editor on no
            /// desktop -- tools/show-ui builds a rack this way -- so the request
            /// is dropped here instead of being made and complained about. The
            /// slot is cleared either way: the grab was already a no-op in this
            /// state, and holding the request back for a later resync would let
            /// focus jump to a slot chosen some time ago.
            ///                                   (05.08.2026.) (SW port)
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
/// stack. The 2016 code met the same thing from the other side and left an
/// "...investigate why this doesn't work when placed inside the ModuleUI
/// destructor..." beside it.
///                                           (02.08.2026.) (SW port)
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
        /// \note The six global knobs. `updateForGlobalParameterChange()` and
        /// `updateGlobalParameterWidget<>` have had no callers since the 2016
        /// plugin class was deleted -- so host automation
        /// of a global never moved the editor -- and this is the route that gives
        /// them one back, without the audio-thread write the naive fix would have
        /// recreated.
        updateForGlobalParameterChange();
        return;

    case ParameterID::ModuleChainParameter:
        /// \note A slot's effect changed under us. Rebuilding a strip is stage 6's
        /// `ToUI::SlotChanged`; nothing sends this yet.
        return;

        LE_DEFAULT_CASE_UNREACHABLE();
    }
}

void SpectrumWorxEditor::timerCallback()
{
    applyPaletteIfChanged();

    pumpModulatedValues();
}

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxEditor::applyPaletteIfChanged()
// -------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note Polled rather than pushed because the change may not be this editor's.
/// The palette is process-wide, so a second instance in the same host has just
/// had its colours swapped by a settings page it has never heard of and nothing
/// has marked a pixel of it dirty. Every editor watching one counter is what
/// makes "change it once, change it everywhere" true without a registry of live
/// editors to keep correct.
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::applyPaletteIfChanged()
{
    LE_ASSERT(isThisTheGUIThread());

    auto const generation(ColourMap::generation());
    if (generation == palette_)
        return;
    palette_ = generation;

    /// \note Before the repaint, and idempotent: whichever editor gets here
    /// first takes the colours and the rest find them taken.
    Theme::singleton().reloadColours();

    ////////////////////////////////////////////////////////////////////////////
    /// \note The wrapper rather than this, where there is one: ZoomedEditor
    /// paints ColourMap::Ground behind the transform -- the half pixel rounding
    /// leaves down the right edge -- and it is this editor's *parent*, so a
    /// repaint from here would not reach it. \see setZoom(), which reaches for
    /// it the same way.
    ///
    /// \note And sendLookAndFeelChange() rather than repaint(): it repaints
    /// every descendant *and* tells each one its colours moved, which is what a
    /// widget holding a LookAndFeel colour of its own needs to hear.
    ////////////////////////////////////////////////////////////////////////////
    auto *const pWrapper(findParentComponentOfClass<ZoomedEditor>());
    juce::Component &root(pWrapper ? static_cast<juce::Component &>(*pWrapper) : *this);
    root.sendLookAndFeelChange();
}

void SpectrumWorxEditor::setPalette(ColourMap::Palette const palette)
{
    preferences().setPalette(palette);
    ColourMap::setPalette(palette);

    /// \note Not applied here. This editor picks the change up on its next tick
    /// through exactly the path a *second* instance does, so there is one way
    /// a palette reaches the screen rather than two that have to agree.
    /// \see applyPaletteIfChanged().
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

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \note The slot's effect may have changed since the value was written,
        /// and then this is a parameter index the *previous* effect had. The
        /// mailbox is a fixed array indexed by the maximal parameter layout and
        /// keeps a dirty bit until somebody sweeps it, so a preset load leaves
        /// values for slots whose effects it replaced -- and a knob's index that
        /// meant Whisperer's tenth parameter means nothing at all in a Gain.
        ///
        ///   Reported as "Parameter index out of range" from
        /// `effectSpecificParameterControl`, which is that index walked off the
        /// end of the strip's child list.
        ///                                   (02.08.2026.) (SW port)
        ///
        ////////////////////////////////////////////////////////////////////////
        if (parameterID.value._.module.moduleParameterIndex >=
            pRegion->module().numberOfParameters())
            return;

        pRegion->setParameter(parameterID.value._.module.moduleParameterIndex, value,
                              ModuleUI::LFOValue);
    });
}

/// \note `editGlobalParameter` rather than `editParameter`, which is what this
/// built the ParameterID for and called directly. The value here is in the
/// parameter's own units -- that is what every caller holds and what this
/// function's declaration has always said -- and `editParameter` takes the
/// automation edge's, which for a power-of-two parameter is the exponent. So the
/// three knobs worked, being linear, and the settings page's FFT size, overlap
/// factor and window function did not.
///                                           (08.08.2026.) (SW port)
void SpectrumWorxEditor::queueGlobalParameter(std::uint8_t const index, float const value) const
{
    editorHost().editGlobalParameter(index, value);
}

void SpectrumWorxEditor::updateModuleParameterAndNotifyHost(ModuleUI &moduleUI,
                                                            std::uint8_t const moduleParameterIndex,
                                                            float const parameterValue) const
{
    auto &module(moduleUI.module());
    std::uint8_t const moduleIndex(moduleChain().getIndexForModule(module));

    ParameterID parameterID;
    parameterID.value.type = ParameterID::ModuleParameter;
    parameterID.value._.module = {ParameterID::Zero, moduleParameterIndex, moduleIndex};

    editorHost().editParameter(parameterID, parameterValue);

    /// \note Straight to the host rather than waiting for the engine to confirm.
    /// `automatedParameterChanged` queues too -- into the ring the host collects
    /// on its next `process()` or `flush()` -- so both ends of the edit are on
    /// their way in one place, and it is `requestParameterFlush()` inside that
    /// call which gets the command drained when the transport is parked.
    host().automatedParameterChanged(module, moduleIndex, moduleParameterIndex, parameterValue);
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
    /// \note fromChild() rather than a downcast of the parent: the parent is the
    /// main area now, not the editor. \see MainArea.
    SpectrumWorxEditor &editor(SpectrumWorxEditor::fromChild(*this));
    LE_ASSERT(editor.nextAvailableModuleSlot_ < SW::Constants::maxNumberOfModules);

    EffectMenuTarget const target{EffectMenuTarget::append, editor.nextAvailableModuleSlot_};
    auto const header(editor.effectMenuHeader(target));
    editor.moduleMenu_.menuWithHeader(header.toRawUTF8())
        .showCenteredAtRight(*this, editor.effectMenuCallback(target));
}

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxEditor::DropIndicator
// ---------------------------------
//
////////////////////////////////////////////////////////////////////////////////

SpectrumWorxEditor::DropIndicator::DropIndicator(juce::Component &parent)
{
    /// \note Neither focusable nor clickable: it is drawn over the rack during a
    /// drag, and a drag is delivered to the strip it started on.
    setWantsKeyboardFocus(false);
    setInterceptsMouseClicks(false, false);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note And on top for good, rather than raised and lowered around each
    /// drag. It has to be above the module strips or a swap's fill is painted
    /// over by the strip it is marking -- and it is built before any strip
    /// exists, so ordinary z-order puts it underneath every one of them.
    ///
    ///   The raise-and-lower this replaces was there because the thing being
    /// raised was a child of the *editor*, alongside the panels, so a stale
    /// always-on-top left it painting over an open preset browser. It is a child
    /// of the main area now and a panel is a sibling of that, so there is nothing
    /// left for it to rise above.
    ///                                       (14.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
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
/// thicker border on whichever strip the eye assigns it to; overrunning both is
/// what makes it a divider between them instead.
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
    static Artwork const *LE_RESTRICT const icons[] = {
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
    : switch_(*this, ledCapsule, LEDTextButton::ledWidth, LEDTextButton::ledHeight),
      quarter_(*this, 93, 8, " N "), triplet_(*this, 93 + 27 * 1, 8, " T "),
      dotted_(*this, 93 + 27 * 2 - 3, 8, " D "),
      typeArrow_(*this, ArrowStyle::stepWidth, ArrowStyle::stepHeight, false,
                 ColourMap::MouseOverGlow),
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

    phase_.setBounds(59, 177, 63, 18);
    phase_.setSliderStyle(juce::Slider::LinearHorizontal);
    phase_.setTextBoxStyle(juce::Slider::NoTextBox, true, 15, 18);
    phase_.setRange(-0.5, +0.5);
    phase_.setDoubleClickReturnValue(true, 0);
    addToParentAndShow(*this, phase_);

    typeArrow_.setTopLeftPosition(164, 149);
    typeArrow_.addListener(this);

    range_.setBounds(11, 110, width - 11, 15);
    range_.setSliderStyle(juce::Slider::TwoValueHorizontal);
    range_.setTextBoxStyle(juce::Slider::NoTextBox, true, 135, 30);
    addToParentAndShow(*this, range_);

    this->setBounds(107, 234, width, 192);

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
/// \note `|| !LFO::Timer::hasTempoInformation()` stood here, so a host that
/// reported no transport printed every period in milliseconds. There is always a
/// tempo -- the host's, or an assumed 120 BPM in four four -- so a note ratio
/// always means something, and only the LFO's own sync setting decides which of
/// the two to show. See the note on updateSnapControls().
///                                           (02.08.2026.) (SW port)
bool skipPeriodRatio(SpectrumWorxEditor::LFODisplay::Period const &period)
{
    return period.lastSyncType() == LFO::Free;
}

juce::String periodRatioString(SpectrumWorxEditor::LFODisplay const &parent,
                               double const &periodScale)
{
    std::array<char, 16> buffer;

    double numerator;
    double denominator;
    char const *suffix;
    switch (parent.period().lastSyncType())
    {
    case LFO::Quarter:
        if (periodScale < 1)
        {
            numerator = 1;
            denominator = 1 / periodScale;
        }
        else
        {
            denominator = 1;
            numerator = periodScale;
        }
        suffix = "";
        break;

    case LFO::Triplet:
        if (periodScale < 1)
        {
            numerator = 1;
            denominator = 1 / (periodScale * 3 / 2);
        }
        else
        {
            denominator = 1;
            numerator = (periodScale * 3 / 2);
        }
        suffix = "T";
        break;

    case LFO::Dotted:
        if (periodScale < 1)
        {
            numerator = 1;
            denominator = 1 / (periodScale * 2 / 3);
        }
        else
        {
            denominator = 1;
            numerator = (periodScale * 2 / 3);
        }
        suffix = "D";
        break;

        LE_DEFAULT_CASE_UNREACHABLE();
    }

    unsigned int const charactersWritten(std::snprintf(
        &buffer[0], buffer.size(), "%u/%u%s bars", Math::convert<unsigned int>(numerator),
        Math::convert<unsigned int>(denominator), suffix));
    LE_ASSERT(charactersWritten < buffer.size());
    return juce::String(&buffer[0], charactersWritten);
}

juce::String periodMillisecondsString(SpectrumWorxEditor::LFODisplay const &parent,
                                      double const &periodScale)
{
    LE_ASSERT(parent.period().milliseconds() == periodScale);

    bool const skipRatio(skipPeriodRatio(parent.period()));
    unsigned char const precision(!skipRatio * 2);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The number is written into the buffer *less the suffix*, so that
    /// `strcpy` at the returned offset cannot reach the end however wide the
    /// number turns out to be.
    ///
    ///   Which is the shape of what went wrong here. `lexical_cast` was handed
    /// `&buffer[0]` and a constant it had no way to check, returned the length
    /// `snprintf` would have *wanted*, and this indexed a 32-byte stack array
    /// with it. Handing over the room there actually is makes both halves of that
    /// impossible rather than merely unlikely.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    constexpr char suffix[]{" ms"};
    std::array<char, 32> buffer;
    auto const charactersWritten(Utility::lexical_cast(
        periodScale, precision, std::span(buffer).first(buffer.size() - sizeof(suffix))));
    std::strcpy(&buffer[charactersWritten], suffix);

    return juce::String(&buffer[0]);
}

juce::String phaseString(SpectrumWorxEditor::LFODisplay const & /*parent*/,
                         double const &periodScale)
{
    constexpr char suffix[]{"%"};
    std::array<char, 32> buffer;
    auto const numberOfCharactersWritten(Utility::lexical_cast(
        periodScale * 100, 1, std::span(buffer).first(buffer.size() - sizeof(suffix))));
    std::strcpy(&buffer[numberOfCharactersWritten], suffix);
    return juce::String(&buffer[0]);
}

juce::String rangeValueString(SpectrumWorxEditor::LFODisplay const &parent,
                              double const &periodScale)
{
    /// \note `LE_ASSERT( activeControl() )` stood above this, over the file-scope
    /// pointer. It said "something is active"; the line below says "and it is this
    /// control", which subsumes it.
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

    /// \note The well before the mark that goes in it, and both here rather
    /// than in the chassis: this strip is on screen only while a control is
    /// selected, and a pill sitting alone in an empty LFO box was what the
    /// chassis drawing it looked like. \see issue #134.
    BackgroundPainter::paintLFOWaveformWell(graphics, getPosition());

    // Null for an item with no icon; every LFO waveform has one.
    if (auto const *const icon(type_.getSelectedItemIcon()); icon != nullptr)
        paintImage(graphics, *icon, 119, 144);
}

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxEditor::setLFOEnabled()
// -----------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The parameter goes out through `editParameter()` and the host is told
/// separately, which is what `LFODisplay::updateParameterAndNotifyHost<>` does
/// for the five LFO parameters that have a ParameterID -- spelled out here
/// because this is reachable with no LFO strip on screen at all.
///
/// \note `lfoStateChanged()` is not cosmetic: it is what re-keys the knob's
/// scroll wheel and its double-click default on the answer that just changed.
///
////////////////////////////////////////////////////////////////////////////////

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

/// \note No `auto &lfo( this->lfo() )` at the top any more: the sync branch below
/// stopped reading the mask when the three buttons became one choice, which left
/// the reference used by nothing but two `LE_ASSERT`s -- and those are gone in a
/// release build, where an unused variable is an error.
void SpectrumWorxEditor::LFODisplay::buttonClicked(juce::Button *const pButton)
{
    if (pButton == &switch_)
    {
        bool const enable(switch_.getToggleState());
        LE_ASSERT(enable != lfo().enabled());
        /// \note Through the editor, which is where the knob's own menu goes as
        /// well -- see setLFOEnabled(). The button has already toggled itself,
        /// so the resync that comes back is a no-op here.
        editor().setLFOEnabled(control(), enable);
    }
    else if (pButton == &typeArrow_)
    {
        //...mrmlj...
        if (!type_.menuActive())
        {
            juce::Component::SafePointer<LFODisplay> pThis(this);
            type_.showCenteredAtRight(typeArrow_, [pThis](bool const selectionChanged) {
                if (!pThis || !selectionChanged)
                    return;
                pThis->updateParameterAndNotifyHost<LFO::Waveform>(pThis->type_.getSelectedID());
                pThis->repaint();
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

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \note Through `updateParameterAndNotifyHost<>` rather than through
        /// `LFO::addSyncType()`/`removeSyncType()`, which write the parameter and
        /// nothing else. Those wrote the strip's own module, and since the editor
        /// was bound to `programMain_` that module is the *main thread's* -- so
        /// clicking N, T or D moved the display, moved what a session saved, and
        /// never reached the audio thread. `SyncTypes` is past
        /// `lfoExportedParameters` and has no ParameterID, so the route is the
        /// same `ToEngine::SetUnexportedLFOParameter` the waveform popup already
        /// took; only this branch had been left behind.
        ///
        ///   Before the mask reaches the queue and not after, because the ring is
        /// ordered and `updateLFOAndHostFromPeriodControl()` below queues the
        /// period the new mask snapped it to. The engine resnaps what it is given
        /// against whatever mask it holds, so the two have to arrive that way
        /// round.
        ///                                   (06.08.2026.) (SW port)
        ///
        /// \note **The three buttons are one choice, not three toggles.** The
        /// mask can hold any combination and `snapSyncedPeriod()` then picks
        /// whichever of the enabled grids lands nearest -- so with all three lit
        /// the period reads N almost everywhere and T and D look broken, which is
        /// issue #111. Selecting a grid therefore clears the other two, and
        /// clicking the lit one goes back to Free.
        ///
        ///   The *parameter* is still a mask and the file grammar is untouched:
        /// two shipped presets carry `sync="5"` and `sync="7"`, they load and
        /// snap exactly as they always did, and `updateSnapControls()` lights
        /// both of their grids. What has gone is the panel's ability to make such
        /// a value.
        ///                                   (18.08.2026.) (SW port)
        ///
        ////////////////////////////////////////////////////////////////////////
        bool const selectSyncType(pButton->getToggleState());
        LE_ASSERT(selectSyncType != lfo().hasEnabledSync(syncType));

        updateParameterAndNotifyHost<LFO::SyncTypes>(selectSyncType ? syncType : LFO::Free);

        // ...which is what un-lights whichever of the three was lit before.
        updateSnapControls();

        updatePeriodControl();
        updateLFOAndHostFromPeriodControl();
        this->repaint();
    }
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
    updateSnapControls();
    type_.setSelectedID(lfo().waveForm());
    verifyGUIAndLFOConsistency();
}

void SpectrumWorxEditor::LFODisplay::updateAutomatableControls()
{
    updatePeriodControl();
    updateRangeControl();
    auto &lfo(this->lfo());
    switch_.setToggleState(lfo.enabled(), juce::dontSendNotification);
    phase_.setValue(lfo.phase(), juce::dontSendNotification);
}

void SpectrumWorxEditor::LFODisplay::updatePeriodControl()
{
    auto &lfo(this->lfo());

    float const rangeMinimum(LFO::PeriodScale::minimum());
    float const rangeMaximum(LFO::PeriodScale::maximum());
    double const rangeBeginning(LFO::snapPeriodScale(rangeMinimum, lfo.syncTypes()).first);
    double const rangeEnd(LFO::snapPeriodScale(rangeMaximum, lfo.syncTypes()).first);
    /// \note One millisecond of the bar a free period is measured against, which
    /// is the reference one and therefore a constant -- the slider's step no
    /// longer changes under the user when the host changes tempo.
    double const step(
        (lfo.syncTypes() != LFO::Free) ? 0 : 1 / 1000.0 / LFO::Timer::referenceBarDuration // 1 ms
    );

    period_.setRange(rangeBeginning, rangeEnd, step);
    period_.setSkewFactorFromMidPoint(1);

    /// \note JUCE 8 passes a DragMode where 2016 passed a bool; the override
    /// ignores it, and notDragging is what "not a drag" spells now.
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

////////////////////////////////////////////////////////////////////////////////
///
/// \note The N, T and D buttons used to be disabled whenever
/// `LFO::Timer::hasTempoInformation()` was false -- which is every host that
/// reports no transport, the standalone among them. So an LFO could be *loaded*
/// tempo-synced there and run at the assumed 120 BPM, and the three buttons that
/// say so could not be touched.
///
///   There is always a tempo. A host that reports one gives it; one that does not
/// gets 120 BPM in four four, which every LFO already free-runs against -- see
/// pluginTests.cpp's "With no transport the LFO clock is 120 BPM in four four".
/// So a quarter note always means half a second of something, and the question
/// the flag answers -- "did the host tell us?" -- is not the question the panel
/// was asking.
///
/// \note The disabling was also one-way: nothing ever called `setEnabled( true )`
/// on these. A host that reported a tempo, stopped, and started again left them
/// dead for the rest of the session.
///                                           (02.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

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

void SpectrumWorxEditor::LFODisplay::queueUnexportedLFOParameter(
    std::uint8_t const lfoParameterIndex, float const value) const
{
    auto const moduleParameterIndex(control().moduleParameterIndex());
    if (moduleParameterIndex >= (SW::Constants::maxNumberOfParametersPerModule - 1))
        return;

    editor().editorHost().publishUnexportedLFOParameter(moduleIndex(), moduleParameterIndex,
                                                        lfoParameterIndex, value);
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
    /// \note Whichever bar the period is a fraction of: the reference one for a
    /// free LFO, the host's for a synced one. \see LFOImpl::getValue -- the panel
    /// has to convert the way the oscillator does or it prints a number the LFO
    /// is not running at.
    float const bar((lastSyncType() == LFO::Free)
                        ? LFO::Timer::referenceBarDuration
                        : parent().editor().effect().lfoTimer().basePeriod());
    return this->getValue() * bar * 1000;
}

SpectrumWorxEditor::LFODisplay const &SpectrumWorxEditor::LFODisplay::Period::parent() const
{
    return Utility::ParentFromMember<LFODisplay, Period, &LFODisplay::period_>()(*this);
}

SpectrumWorxEditor::SampleArea::SampleArea()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addToParentAndShow(editor().mainArea(), *this);
}

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxEditor::SampleArea::mouseUp()
// -----------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note A menu rather than 2016's straight-to-the-file-dialog, because the
/// factory samples are in the binary now and a file dialog cannot show them.
/// The dialog is still one entry away, and the right button still clears, as it
/// always did.
///                                           (01.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

void SpectrumWorxEditor::SampleArea::mouseUp(juce::MouseEvent const &event)
{
    SpectrumWorxEditor &editor(this->editor());
    juce::ModifierKeys const mouseButtons(event.mods);
    /// \note The right button used to clear the file, which meant "the main
    /// input" without saying so. It says so.
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

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The three sources first, because they are what this control is for,
    /// and "No external audio" is gone from among them: it named the *absence* of
    /// one source rather than the presence of another, which is why a user who
    /// cleared a file could not say what they wanted instead. \see issue #113.
    ///
    /// \note Neither of the two is disabled when it is already selected. They are
    /// a choice rather than a command, and greying out the current one is how the
    /// old `clear` entry came to be disabled in exactly the state a user most
    /// wanted to see what it said.
    ///
    ////////////////////////////////////////////////////////////////////////////

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

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note **One of the two places in `src/` that may name `juce::File`**, the
    /// other being the preset browser's folder chooser; tests/checkNoJuceFile.cmake
    /// allowlists both. `juce::FileChooser` is handed one to start from and
    /// answers with one, so the conversion happens here and the type goes no
    /// further -- `newSampleFileSelected()` takes an `fs::path`.
    ///
    /// \note Only a real file is a place to start from: a factory sample is a
    /// bare name and no directory, and JUCE would resolve it against whatever
    /// the host's working directory happens to be. `getSpecialLocation()` stays
    /// a `juce::File` because it feeds the constructor on the next line and
    /// never leaves the expression.
    ///
    ////////////////////////////////////////////////////////////////////////////

    auto const currentFile(editor.editorHost().currentSampleFile());
    std::error_code error;
    auto const startingFile(fs::is_regular_file(currentFile, error)
                                ? LE::IO::pathToJuceFile(currentFile)
                                : juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    /// \note Held rather than stack-allocated: launchAsync() returns
    /// immediately and the chooser must outlive the dialog.
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

////////////////////////////////////////////////////////////////////////////////
//
// SpectrumWorxEditor::Settings::Settings()
// ----------------------------------------
//
////////////////////////////////////////////////////////////////////////////////

#pragma warning(push)
#pragma warning(disable : 4355) // 'this' used in base member initializer list.

SpectrumWorxEditor::Settings::Settings() /// \throws std::bad_alloc Out of memory
    : juce::TabbedComponent(juce::TabbedButtonBar::TabsAtTop),

      fftSize_(enginePage_, xMargin, yMargin + yStep * 0, (Engine ::FFTSize *)(0)),
      overlapFactor_(enginePage_, xMargin, yMargin + yStep * 1, (Engine ::OverlapFactor *)(0)),
      windowFunction_(enginePage_, xMargin, yMargin + yStep * 2, (Engine ::WindowFunction *)(0))
/// \note pRegistrationData_(0) came last here; the member itself went with the
/// licence manager in stage 0 and the initialiser did not, so nothing had
/// compiled this constructor since.
///                                       (28.07.2026.) (SW port)
{
    /// \note The height was editor().getHeight() -- 564 -- while what this
    /// paints is a 16 px tab bar over a 347 px page bitmap: 363. The 13 px
    /// difference was empty and invisible while this was a transparent desktop
    /// window, and would not be as an overlay over the editor. So it is the sum
    /// of what it draws, which is also what the preset browser measures.
    ///                                       (01.08.2026.) (SW port)
    this->setSize(PanelPainter::width, ButtonStyle::tabHeight + PanelPainter::settingsPageHeight);

    updateEnginePage();

    setOutline(0);
    setIndent(0);
    setTabBarDepth(ButtonStyle::tabHeight);

    /// \note Real names, not the `dummyName( "a" )` that stood here: the three
    /// captions were baked into six bitmaps, so what a tab was called was
    /// nobody\'s business but the artwork\'s and the name JUCE held said
    /// nothing. \see SettingsTab.
    addTab("Engine", ColourMap::getColour(ColourMap::Transparent), &enginePage_, false);
    addTab("GUI", ColourMap::getColour(ColourMap::Transparent), &interfacePage_, false);
    addTab("About", ColourMap::getColour(ColourMap::Transparent), &aboutPage_, false);

    LE_ASSERT(getNumTabs() == numberOfSettingsPages);

    /// \note `OwnedWindow<Settings>::attach()` stood here; the editor parents
    /// and positions this now -- see SpectrumWorxEditor::openOverlay().
    ///                                       (01.08.2026.) (SW port)
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
    else if (&comboBox == &settings.interfacePage_.lfoUpdateComboBox())
    {
        preferences().setLFOUpdateBehaviour(static_cast<Preferences::LFOUpdateBehaviour>(value));
    }
    else
    {
        LE_UNREACHABLE_CODE();
    }

    settings.enginePage_.setNewQualityFactor(editor.engineSetup().wolaRippleFactor());
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
    /// \note And it was not used: the line above this note read
    /// `editor.engineSetup()`, the checked getter, which is precisely what the
    /// note says must not be called here. Whether that happened in the port or
    /// earlier, the comment has been describing a fix that was not present.
    ///                                       (31.07.2026.) (SW port)
    auto const &engineSetup(editor.effect().uncheckedEngineSetup());
    auto const &parameters(editor.program().parameters());

    fftSize_->setValue(parameters.get<Engine::FFTSize>());
    overlapFactor_->setValue(parameters.get<Engine::OverlapFactor>());
    windowFunction_->setValue(parameters.get<Engine::WindowFunction>());
    enginePage_.setNewQualityFactor(engineSetup.wolaRippleFactor());
}

SpectrumWorxEditor::Settings::EnginePage::EnginePage() : PanelBackground(SettingsPage) {}

void SpectrumWorxEditor::Settings::EnginePage::setNewQualityFactor(float const &qualityFactorParam)
{
    float const qualityFactor(qualityFactorParam);
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
    /// \note No `LE_VERIFY` on the length any more: the buffer goes over whole,
    /// so staying inside it is the callee's promise rather than the caller's
    /// hope. It was a check made one write too late in any case.
    char buffer[32];
    Utility::lexical_cast(qualityFactor * 100.0f, 2, buffer);
    /// \note Assigned rather than truncated in place. This read
    /// `*engineQuality_.getCharPointer().getAddress() = 0;` -- a poke of a nul
    /// into the string's own buffer, to empty it while keeping the allocation.
    /// An empty juce::String does not own a buffer: it points at the shared
    /// `emptyString` singleton, which JUCE 8 declares `constexpr` and the linker
    /// therefore puts in .rodata. So the first call -- and it is always the
    /// first call, the string is default-constructed and this is what fills it
    /// -- wrote to a read-only page. Every case that opened the settings panel
    /// segfaulted here.
    ///                                       (05.08.2026.) (SW port)
    engineQuality_ = "Ripple Amount: ";
    engineQuality_ += buffer;
    engineQuality_ += description;
}

namespace
{
void printEngineDiagnostics(juce::String &buffer, char const *const title, float const value,
                            char const *const suffix, unsigned int const verticalOffset,
                            juce::Graphics const &graphics)
{
    char valueStr[32];
    Utility::lexical_cast(value, 1, valueStr);
    buffer = title;
    buffer += ": ";
    buffer += valueStr;
    buffer += ' ';
    buffer += suffix;
    graphics.drawFittedText(buffer, SpectrumWorxEditor::Settings::xMargin + 6, verticalOffset, 213,
                            18, juce::Justification::centred, 1);
}
} // anonymous namespace

void SpectrumWorxEditor::Settings::EnginePage::paint(juce::Graphics &g)
{
    PanelBackground::paint(g);
    g.setColour(ColourMap::getColour(ColourMap::Text));
    g.setFont(DrawableText::defaultFont());
    g.drawFittedText(engineQuality_, xMargin + 6, yMargin + yStep * 5, 213, 18,
                     juce::Justification::centred, 1);

    Settings &settings(
        Utility::ParentFromMember<Settings, EnginePage, &Settings::enginePage_>()(*this));
    Engine::Setup const &engineSetup(settings.editor().engineSetup());
    juce::String tmp;
    tmp.preallocateBytes(sizeof(juce::String::CharPointerType::CharType) * 64);
    printEngineDiagnostics(tmp, "Frequency Resolution", engineSetup.frequencyRangePerBin<float>(),
                           "Hz", yMargin + yStep * 5 + 30, g);
    printEngineDiagnostics(tmp, "Time Resolution", engineSetup.stepTime() * 1000, "ms",
                           yMargin + yStep * 5 + 60, g);
    printEngineDiagnostics(tmp, "Latency", engineSetup.latencyInMilliseconds(), "ms",
                           yMargin + yStep * 5 + 90, g);

    //...mrmlj...for testing...
    //g.drawSingleLineText( engineQuality_, xMargin - 5, yMargin + yStep * 6 + 12 );
}

SpectrumWorxEditor::Settings::InterfacePage::InterfacePage()
    : PanelBackground(SettingsPage), zoom_(*this, xMargin, yMargin + 0 * yStep, "Zoom"),
      palette_(*this, xMargin, yMargin + 1 * yStep, "Color Scheme"),
      moduleUIMouseOverReaction_(*this, xMargin, yMargin + 2 * yStep, "Mouse Over Reaction"),
      lfoUpdateBehaviour_(*this, xMargin, yMargin + 3 * yStep, "LFO Update Behaviour"),
      hideCursorOnKnobDrag_(*this, xMargin - 4, yMargin + 4 * yStep, "Hide cursor on knob drag")
{
    Settings &parent(
        Utility::ParentFromMember<Settings, InterfacePage, &Settings::interfacePage_>()(*this));

    /// \note The percentage is the item's ID, so what comes back out of
    /// `getValue()` is the zoom itself rather than a position in this list that
    /// something else would have to translate.
    for (auto const percent : Preferences::zoomPercentages)
    {
        std::array<char, 8> text;
        std::snprintf(text.data(), text.size(), "%u%%", percent);
        zoom_.addItem(percent, text.data());
    }
    zoom_.setSelectedID(preferences().zoomPercent());

    ////////////////////////////////////////////////////////////////////////////
    /// \note Spelled here rather than taken from ColourMap::nameOf(), which
    /// answers with the enumerator: what goes in the preferences file has to be
    /// greppable in the source and what goes in this list has to be readable,
    /// and "SSTDark" cannot be both. Same split as every other box on this page.
    ////////////////////////////////////////////////////////////////////////////
    palette_.addItem(ColourMap::Classic, "Classic");
    palette_.addItem(ColourMap::Reds, "Reds");
    palette_.addItem(ColourMap::Greens, "Greens");
    palette_.addItem(ColourMap::Grays, "Grayscale");
    palette_.addItem(ColourMap::SSTDark, "Dark");
    palette_.setSelectedIndex(preferences().palette());

    moduleUIMouseOverReaction_.addItem(Preferences::Never, "Never");
    moduleUIMouseOverReaction_.addItem(Preferences::WhenParentModuleSelected, "Module selected");
    moduleUIMouseOverReaction_.addItem(Preferences::WhenParentOrNothingSelected, "Always");
    moduleUIMouseOverReaction_.setSelectedIndex(preferences().moduleUIMouseOverReaction());

    lfoUpdateBehaviour_.addItem(Preferences::NoUpdate, "Never");
    lfoUpdateBehaviour_.addItem(Preferences::WhenControlSelected, "Control selected");
    lfoUpdateBehaviour_.addItem(Preferences::WhenControlActive, "Control active");
    lfoUpdateBehaviour_.addItem(Preferences::Always, "Always");
    lfoUpdateBehaviour_.setSelectedIndex(preferences().lfoUpdateBehaviour());

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

/// \note SpectrumWorxEditor::Settings::AboutPage's constructor and paint() stood
/// here. \see gui/about.cpp.

/// \note Was a pair of bitmaps per tab -- six files, one caption baked into
/// each, so the three tabs could not be renamed without redrawing them. They
/// are the tab's name and a ButtonPainter now, which is also what makes the
/// widths below follow the words rather than the artwork.
///                                       (18.08.2026.)
class SettingsTab : public juce::TabBarButton
{
  public:
    /// \note `ownerBar.setTopLeftPosition( { 9, 9 } )` stood here and did
    /// nothing: `TabbedComponent::resized()` runs after every `addTab()` and
    /// puts the bar back in the corner. Where the bar goes is the panel's
    /// question rather than a tab's -- \see Settings::resized().
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

SpectrumWorxEditor &SpectrumWorxEditor::Settings::editor()
{
    return Utility::ParentFromOptionalMember<SpectrumWorxEditor, Settings,
                                             &SpectrumWorxEditor::settings_, false>()(*this);
}

/// \note One button left. The other branch here opened
/// `<user>/Documents/SpectrumWorx/Documents/User's Guide.PDF` -- a path the 2016
/// installer wrote and nothing has written since, so the About tab's one control
/// had been a button that could only fail. Its replacement is a link to the
/// manual in the repository. \see gui/about.cpp.
///                                           (15.08.2026.) (SW port)
void SpectrumWorxEditor::Settings::buttonClicked(juce::Button *const pButton)
{
    LE_ASSERT(pButton == &interfacePage_.hideCursorOnKnobDrag_);
    (void)pButton;

    preferences().setHideCursorOnKnobDrag(interfacePage_.hideCursorOnKnobDrag_.getToggleState());
}

} // namespace LE::SW::GUI

/*
    Alex's scrap:

	AudioEffectX *effect = (AudioEffectX*)getEffect ();
	VstFileType aiffType ("AIFF File", "AIFF", "aif", "aiff", "audio/aiff", "audio/x-aiff");
	VstFileType waveType ("Wave File", ".WAV", "wav", "wav",  "audio/wav", "audio/x-wav");
	VstFileType aifcType ("AIFC File", "AIFC", "aif", "aifc", "audio/x-aifc");
	VstFileType sdIIType ("SoundDesigner II File", "Sd2f", "sd2", "sd2");

	VstFileSelect vstFileSelect;
	memset (&vstFileSelect, 0, sizeof (VstFileType));

	vstFileSelect.command     = kVstFileLoad;
	vstFileSelect.type        = kVstFileType;
	strcpy (vstFileSelect.title, "Load sample..");
	vstFileSelect.nbFileTypes = 2;
	vstFileSelect.fileTypes   = &aiffType;
	vstFileSelect.returnPath  = new char[1024];
	sprintf(vstFileSelect.returnPath, "");
	//vstFileSelect.initialPath  = new char[1024];
	vstFileSelect.initialPath = 0;

	CFileSelector* cFile = new CFileSelector(effect);

	if (cFile->run (&vstFileSelect))
	{
		StandardAlert(0, "File", vstFileSelect.returnPath, 0,0);
		UpdateDisplay(vstFileSelect.returnPath);
	}

	delete cFile;

	delete [] vstFileSelect.returnPath;
	if (vstFileSelect.initialPath)	delete []vstFileSelect.initialPath;
*/
