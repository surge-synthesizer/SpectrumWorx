////////////////////////////////////////////////////////////////////////////////
///
/// \file pathsTests.cpp
/// --------------------
///
///   Where the user's presets live, answered without being initialised first.
///
///   This exists because of a bug it would have caught outright. `rootPath()`
/// and `presetsFolder()` were half of a two-phase initialisation whose other
/// half, `initializePaths()`, nothing called -- its only caller had been the
/// 2016 VST2/AU plugin class that the CLAP replaced. Both getters asserted
/// "Not initialized." and neither was reachable while `presetBrowser.cpp` was in
/// no target, so nothing noticed. Stage 8 put the browser in a target and the
/// presets button asserted on the first press.
///
/// \note Deliberately no side effect. Asking where the presets go must not
/// create a directory in someone's Documents folder, which is why creating it is
/// `createUserPresetsFolder()` and why this test does not call it.
///
/// \note **Issue #28's regression test is not here any more; it is
/// tests/io/jucePathTests.cpp.** It was here because `rootPath()` ended in a
/// `juce::File` conversion and that conversion was the bug. The getters answer
/// with an `fs::path` now and convert nothing, which makes "byte for byte what
/// sst-plugininfra answered" true by construction -- a tautology dressed as
/// coverage. What is left of that hazard is io/jucePath.hpp, and the byte
/// sequence and the character count went with it.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/gui.hpp"

#include <sst/plugininfra/paths.h>

#include <catch2/catch_test_macros.hpp>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace LE::SW;

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("The user preset paths answer without an initialisation step", "[paths]")
{
    /// \note The first call in the process, and it has to work. Anything that
    /// has to run before it is the bug this file is about.
    auto const &root(GUI::rootPath());

    INFO("root " << root);
    CHECK(!root.empty());
    CHECK(root.is_absolute());

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Either name, and only on Linux is there a second one. The folder
    /// comes from `sst::plugininfra::paths::bestDocumentsFolderPathFor()`, whose
    /// Linux waterfall is XDG_DOCUMENTS_DIR, then an existing `~/Documents`, and
    /// failing both a hidden `~/.SpectrumWorx` -- so the name depends on the home
    /// directory of whoever runs this and not on anything the build decides. A
    /// developer's machine has `~/Documents` and takes the first branch; a fresh
    /// CI runner has neither and takes the last, which is why this passed
    /// everywhere it was written and failed the first time CI ran it. The dot is
    /// the intended answer on that branch -- it is the assertion that was wrong,
    /// not the path.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const rootName(root.filename());
#ifdef __linux__
    CHECK((rootName == "SpectrumWorx" || rootName == ".SpectrumWorx"));
#else
    CHECK(rootName == "SpectrumWorx");
#endif // __linux__

    auto const &presets(GUI::presetsFolder());

    INFO("presets " << presets);
    CHECK(!presets.empty());

    /// \note Idempotent, and the same object each time -- both are function-local
    /// statics, and the browser compares against `presetsFolder()` on every
    /// `goToParent()`.
    CHECK(&GUI::rootPath() == &root);
    CHECK(&GUI::presetsFolder() == &presets);

    /// \note Named rather than probed on disk, and that is the point: this test
    /// says where the presets go without asking the filesystem anything, so it
    /// neither creates a directory in the Documents folder of whoever runs it
    /// nor passes or fails depending on whether one is already there.
    ///
    /// \note `parent_path()`, which is exact, rather than the `isAChildOf()` this
    /// read as a `juce::File`: `fs::path` composes and decomposes losslessly, so
    /// "the presets folder is `<root>/Presets`" can be stated rather than
    /// approximated by a containment check that a sibling named `PresetsOld`
    /// would also have satisfied.
    CHECK(presets.filename() == "Presets");
    CHECK(presets.parent_path() == root);
    CHECK(presets == root / "Presets");
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The end-to-end invariant, kept even though it can no longer fail the way
/// it once did: whatever the platform waterfall picks, `rootPath()` hands back
/// *those* bytes. It is one `==` now rather than a conversion round trip, and it
/// is here so that putting a conversion back into `rootPath()` -- which is what
/// issue #28 was -- fails something.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("rootPath() is what sst-plugininfra answered, untouched", "[paths]")
{
    auto const answered(sst::plugininfra::paths::bestDocumentsVendorFolderPathFor(
        "Surge Synth Team", "SpectrumWorx"));

    INFO("answered " << answered);
    CHECK(GUI::rootPath() == answered);
    CHECK(GUI::rootPath().u8string() == answered.u8string());
}
