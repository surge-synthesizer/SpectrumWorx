////////////////////////////////////////////////////////////////////////////////
///
/// \file preferences.hpp
/// ---------------------
///
///   The three answers the settings panel's Interface page asks for, and the
/// file they survive in.
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
///                                       (15.08.2026.) (SW port)
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
/// `fs`, for the folder the file lives in.
#include "filesystem/import.h"

#include <memory>

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
/// every call: `moduleUIMouseOverReaction()` is asked on mouse movement over a
/// module control, and the provider's answer is a map lookup and a string
/// comparison. Each setter writes its own key through, so the file is one
/// rewrite per user click rather than three.
///
/// \note The enumerations are streamed by *name*, not by ordinal. The struct
/// this replaces carried a "layout is on disk; do not reorder it" note -- about
/// a binary blob that no longer exists -- and a name has no such constraint:
/// inserting a value in the middle cannot silently change what an existing file
/// means, and the file stays legible to whoever opens it. The names are the enum
/// identifiers, so a value in the file can be grepped for in the source. An
/// unrecognised one reads as the default.
///
////////////////////////////////////////////////////////////////////////////////

class Preferences
{
  public:
    enum ModuleUIMouseOverReaction
    {
        Never,
        WhenParentModuleSelected,
        WhenParentOrNothingSelected
    };

    enum LFOUpdateBehaviour
    {
        NoUpdate,
        WhenControlSelected,
        WhenControlActive,
        Always
    };

  public:
    /// \note Reads the file. A missing one is not an error -- it is what a first
    /// run looks like -- and leaves every value at its default.
    explicit Preferences(fs::path const &folder);
    ~Preferences();

    Preferences(Preferences const &) = delete; // makes non-copyable
    Preferences &operator=(Preferences const &) = delete;

    ModuleUIMouseOverReaction moduleUIMouseOverReaction() const
    {
        return moduleUIMouseOverReaction_;
    }
    LFOUpdateBehaviour lfoUpdateBehaviour() const { return lfoUpdateBehaviour_; }
    bool hideCursorOnKnobDrag() const { return hideCursorOnKnobDrag_; }

    /// Each of these writes the file. `[main-thread]`
    void setModuleUIMouseOverReaction(ModuleUIMouseOverReaction);
    void setLFOUpdateBehaviour(LFOUpdateBehaviour);
    void setHideCursorOnKnobDrag(bool);

    /// Where this instance reads and writes.
    fs::path const &file() const;

  private:
    /// \note Out of line so that `sst/plugininfra/userdefaults.h` -- and with it
    /// tinyxml, `<fstream>` and `<iostream>` -- is preferences.cpp's business
    /// rather than every editor translation unit's.
    class Storage;
    std::unique_ptr<Storage> storage_;

    ModuleUIMouseOverReaction moduleUIMouseOverReaction_{Never};
    LFOUpdateBehaviour lfoUpdateBehaviour_{Always};
    bool hideCursorOnKnobDrag_{true};
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
