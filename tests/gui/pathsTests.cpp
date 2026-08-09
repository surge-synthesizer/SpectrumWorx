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
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/gui.hpp"

#include <sst/plugininfra/paths.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace LE::SW;

/// \note "Documents" in Japanese -- the reporter's locale in issue #28 -- written
/// as the bytes rather than as the characters. The rest of this tree is ASCII
/// but for an em dash and an acute accent, the case is about the bytes anyway,
/// and this way it does not depend on the encoding of the file it is in. Six
/// code points, three bytes each: `U+30C9 U+30AD U+30E5 U+30E1 U+30F3 U+30C8`.
constexpr char const japaneseDocuments[]{"\xE3\x83\x89\xE3\x82\xAD\xE3\x83\xA5"
                                         "\xE3\x83\xA1\xE3\x83\xB3\xE3\x83\x88"};

constexpr int japaneseDocumentsCharacters{6};

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("The user preset paths answer without an initialisation step", "[paths]")
{
    /// \note The first call in the process, and it has to work. Anything that
    /// has to run before it is the bug this file is about.
    auto const &root(GUI::rootPath());

    INFO("root " << root.getFullPathName());
    CHECK(root != juce::File());
    CHECK(root.isAbsolutePath(root.getFullPathName()));
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
    ///                                       (05.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const rootName(root.getFileName());
#ifdef __linux__
    CHECK((rootName == "SpectrumWorx" || rootName == ".SpectrumWorx"));
#else
    CHECK(rootName == "SpectrumWorx");
#endif // __linux__

    auto const &presets(GUI::presetsFolder());

    INFO("presets " << presets.getFullPathName());
    CHECK(presets != juce::File());
    CHECK(presets.isAChildOf(root));

    /// \note Idempotent, and the same object each time -- the browser holds the
    /// reference across its own lifetime and writes the most-recently-used
    /// folder back into it.
    CHECK(&GUI::rootPath() == &root);
    CHECK(&GUI::presetsFolder() == &presets);

    /// \note Named rather than probed on disk, and that is the point: this test
    /// says where the presets go without asking the filesystem anything, so it
    /// neither creates a directory in the Documents folder of whoever runs it
    /// nor passes or fails depending on whether one is already there.
    CHECK(presets.getFileName() == "Presets");
    CHECK(presets.getParentDirectory() == root);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note Issue #28, and the reason this file gained a second case. On a
/// `ja_JP.UTF-8` desktop the plugin created `~/<mojibake>/SpectrumWorx` instead
/// of finding the localised Documents folder XDG had correctly pointed it at.
/// Nothing was wrong with the XDG lookup -- `paths_linux.cpp` parses
/// `user-dirs.dirs` a byte at a time and hands the bytes back untouched. What
/// was wrong was one conversion at the end of `rootPath()`: `juce::String`'s
/// `char const *` constructor reads through `CharPointer_ASCII`, which widens
/// each *byte* into its own code point and re-encodes the result as UTF-8. The
/// long note in gui.cpp has why the `char8_t` overload that would have saved it
/// is not there (`-fno-char8_t`, from sst-plugininfra's `filesystem` target --
/// and this translation unit is built with it too, so what is checked here is
/// what ships).
///
/// \note The character count is the assertion that actually bites. Byte
/// equality catches it as well, but "six characters, not eighteen" is the
/// difference between the two constructors stated directly: `CharPointer_ASCII`
/// cannot produce six out of eighteen bytes, whatever else it does.
///
/// \note Still no side effect, in keeping with the case above -- these paths are
/// built and parsed, never created, and none of them needs to exist. The last
/// section is the one that would have failed on the reporter's machine and
/// passes everywhere else, which is precisely why the first two are here: they
/// fail on any machine, in any locale, the moment the conversion goes back.
///                                       (09.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A non-ASCII documents folder survives the trip into juce::File", "[paths]")
{
    SECTION("UTF-8 bytes decode as characters rather than as Latin-1")
    {
        std::string const documents(japaneseDocuments);
        auto const decoded(juce::String::fromUTF8(documents.c_str()));

        INFO("decoded " << decoded);
        CHECK(decoded.length() == japaneseDocumentsCharacters);
        CHECK(decoded.toStdString() == documents);

        /// \note The length parameter is a count of *bytes* -- which is what the
        /// preset browser passes it, having measured a file name -- and not a
        /// count of characters. Getting that wrong would truncate mid-sequence.
        auto const counted(
            juce::String::fromUTF8(documents.c_str(), static_cast<int>(documents.size())));
        CHECK(counted == decoded);
    }

#ifndef _WIN32
    SECTION("The whole of rootPath()'s conversion, on a path that has to be built")
    {
        /// \note The expression below is `rootPath()`'s, with a hand-made path
        /// standing in for the one XDG answers with: a machine that runs this
        /// almost certainly has an ASCII home directory, and ASCII is a fixed
        /// point of the widening that was wrong. The bug only shows on bytes
        /// above 0x7F, so the test has to bring its own.
        std::string const expected(std::string("/home/tester/") + japaneseDocuments +
                                   "/SpectrumWorx");

        juce::File const file(juce::String::fromUTF8(fs::path{expected}.u8string().c_str()));

        INFO("file " << file.getFullPathName());
        CHECK(file.getFullPathName().toStdString() == expected);
        CHECK(file.getFileName() == "SpectrumWorx");
        CHECK(file.getParentDirectory().getFileName().length() == japaneseDocumentsCharacters);
    }
#endif // _WIN32

#ifndef _WIN32
    SECTION("rootPath() is byte for byte what sst-plugininfra answered")
    {
        /// \note The end-to-end invariant, and the one the reporter's machine
        /// broke: whatever the waterfall picks, `rootPath()` must hand back those
        /// bytes and no others. It says nothing on an ASCII machine, which is the
        /// whole reason the two sections above exist -- but it is the only check
        /// here that would have caught the bug in situ.
        auto const answered(sst::plugininfra::paths::bestDocumentsVendorFolderPathFor(
                                "Surge Synth Team", "SpectrumWorx")
                                .u8string());

        INFO("answered " << answered);
        CHECK(GUI::rootPath().getFullPathName().toStdString() == answered);
    }
#endif // _WIN32
}
