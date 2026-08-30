////////////////////////////////////////////////////////////////////////////////
///
/// \file preferences.hpp
/// ---------------------
///
///   The answers the settings panel's Interface page asks for, and the file
/// they survive in.
///
/// \note These were `Theme::Settings`, three members of a process-wide static
/// that nothing ever read off disk or wrote back to it: every session started at
/// the defaults, whatever the user had chosen last time. \see issue #61.
///
///   They are not the Theme's. A LookAndFeel answers "what does this widget look
/// like"; these answer "how does this user like the editor to behave", which is
/// the question `sst::plugininfra::defaults::Provider` exists for and which
/// outlives any one plugin instance. The move is what lets the answer live
/// beside the user's presets -- `rootPath()` is in the widget layer and Theme is
/// below it, so a Theme that persisted itself would have had to work out where
/// the user's folder is a second time.
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef preferences_hpp__6F0B2C41_9D77_4A5E_8C3B_1E4A0D96B27F
#define preferences_hpp__6F0B2C41_9D77_4A5E_8C3B_1E4A0D96B27F
//------------------------------------------------------------------------------
/// `AnimationStyle`, which is one of the answers.
#include "animation.hpp"

/// `ColourMap::Palette`, which is one of the answers.
#include "colourMap.hpp"

/// `fs`, for the folder the file lives in.
#include "filesystem/import.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Preferences
///
/// \brief What the user chose on the Interface page, kept in
/// `<folder>/SpectrumWorxUserDefaults.xml`.
///
/// \note Values are cached in members rather than read through the provider on
/// every call: `showLFOAnimation()` is asked once per knob per repaint, and the
/// provider's answer is a map lookup and a string comparison. Each setter writes
/// its own key through, so the file is one rewrite per user click rather than
/// five.
///
/// \note The enumerations in here -- the palette and the animation style -- are
/// streamed by *name*, not by ordinal, so inserting a value in the middle cannot silently change what an
/// existing file means and the file stays legible to whoever opens it. The names
/// are the enum identifiers, so a value in the file can be grepped for in the
/// source, and an unrecognised one reads as the default.
///
////////////////////////////////////////////////////////////////////////////////

class Preferences
{
  public:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The zooms the Interface page offers, ascending, as percentages.
    ///
    /// \note **100 is the size the plugin has always opened at, and is now
    /// also a scale factor of one.** It was not: the editor was laid out in a
    /// 563 x 376 bitmap from 2010 and drawn at 1.5x, so "normal size" and
    /// "no transform" were different things and 100 meant the former. The skin
    /// was rescaled to 845 x 564 on 19.08.2026 and the two became the same
    /// thing -- the window did not change size, the constants did. \see
    /// ZoomedEditor::scaleForZoom() and issue #55.
    ///
    /// \note This list is the whole of what a zoom may be: it is what the combo
    /// box offers and what a value read out of the preferences file is checked
    /// against. A typed-in or dragged custom zoom is a separate question.
    ///
    ////////////////////////////////////////////////////////////////////////////
    static constexpr std::array<unsigned int, 7> zoomPercentages{50, 75, 90, 100, 125, 150, 200};

    static constexpr unsigned int defaultZoomPercent{100};

    /// Whether \p percent is one of the above.
    static bool isOfferedZoom(unsigned int percent);

  public:
    /// \note Reads the file. A missing one is not an error -- it is what a first
    /// run looks like -- and leaves every value at its default.
    explicit Preferences(fs::path const &folder);
    ~Preferences();

    Preferences(Preferences const &) = delete; // makes non-copyable
    Preferences &operator=(Preferences const &) = delete;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Whether a knob an LFO is driving follows the sweep.
    ///
    ///   On, the value moves as the LFO moves it, which is what the plugin has
    /// always done. Off, the knob draws the LFO's *bounds* instead and draws no
    /// value at all -- the same information, and honestly, without eight strips'
    /// worth of movement in the corner of the eye. \see paintModuleKnob(),
    /// and issue #210.
    ///
    ////////////////////////////////////////////////////////////////////////////
    bool showLFOAnimation() const { return showLFOAnimation_; }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Whether the LFO strip follows the pointer as well as the click.
    ///
    ///   A control is selected by clicking it, whatever this says. With it on,
    /// merely hovering one shows its LFO for as long as the pointer is there and
    /// puts it back afterwards; the selection -- and the ring that marks it --
    /// does not move. \see SpectrumWorxEditor::displayedControl() and issue #210.
    ///
    ////////////////////////////////////////////////////////////////////////////
    bool previewLFOOnHover() const { return previewLFOOnHover_; }

    ColourMap::Palette palette() const { return palette_; }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief How the module rack moves when it changes, or NoAnimation.
    ///
    ///   A strip grows when it is added, shrinks when it is removed, and slides
    /// when a slot ahead of it is filled or emptied. Under NoAnimation each of
    /// those is where it ends up straight away. \see GUI::styleFor(), which is
    /// what every caller asks rather than reading this directly -- it is also
    /// NoAnimation whenever the motion could not be seen. Issue #47.
    ///
    ////////////////////////////////////////////////////////////////////////////
    AnimationStyle animationStyle() const { return animationStyle_; }
    bool hideCursorOnKnobDrag() const { return hideCursorOnKnobDrag_; }
    /// Always one of zoomPercentages.
    unsigned int zoomPercent() const { return zoomPercent_; }

    /// \brief The name every 3.0 patch this user saves is signed with, empty
    /// until they give one. Always what SW::sanitisedAuthorName() answers.
    ///
    /// \note The odd one out here, these being otherwise about the editor, and
    /// in the same file for the same reason. \see issue #56.
    std::string const &author() const { return author_; }

    /// Each of these writes the file. `[main-thread]`
    void setShowLFOAnimation(bool);
    void setPreviewLFOOnHover(bool);
    /// \note Stores it. Painting in it is ColourMap::setPalette()'s half, and
    /// the two are done together -- \see SpectrumWorxEditor::setPalette(),
    /// which is the only place a user changes this.
    void setPalette(ColourMap::Palette);
    void setAnimationStyle(AnimationStyle);
    void setHideCursorOnKnobDrag(bool);
    /// \note A percentage this build does not offer is ignored, for the same
    /// reason an unrecognised enumeration name is: the file is the user's to
    /// edit, and every zoom has to be one the combo box can show.
    void setZoomPercent(unsigned int);

    /// \note Sanitised rather than refused: a name is not a command, and there
    /// is nothing to say that dropping the character does not say better.
    void setAuthor(std::string_view);

    /// Where this instance reads and writes.
    fs::path const &file() const;

  private:
    /// \note Out of line so that `sst/plugininfra/userdefaults.h` -- and with it
    /// tinyxml, `<fstream>` and `<iostream>` -- is preferences.cpp's business
    /// rather than every editor translation unit's.
    class Storage;
    std::unique_ptr<Storage> storage_;

    bool showLFOAnimation_{true};
    bool previewLFOOnHover_{true};
    ColourMap::Palette palette_{ColourMap::ClassicBlue};
    AnimationStyle animationStyle_{FastAnimation};
    bool hideCursorOnKnobDrag_{true};
    unsigned int zoomPercent_{defaultZoomPercent};
    std::string author_;
}; // class Preferences

/// \brief The process-wide preferences, under `rootPath()` -- beside the user's
/// presets, which is the folder a user already knows to look in. `[main-thread]`
///
/// \note Answered on demand rather than initialised, for the reason rootPath()
/// is: there is then no "too early" to get wrong. \see gui.cpp.
Preferences &preferences();

/// \brief Rebuilds the above against \p folder, for the tests.
///
///   A test that opens the Interface page reads these, and a test that clicks
/// its LED writes them; neither may touch the file the developer running it
/// actually uses. tests/gui/preferencesTests.cpp points the whole binary at its
/// build tree once, before any case runs.
void setPreferencesFolder(fs::path const &folder);

} // namespace LE::SW::GUI

//------------------------------------------------------------------------------
#endif // preferences_hpp
