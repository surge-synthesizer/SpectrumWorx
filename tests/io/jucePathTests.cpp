////////////////////////////////////////////////////////////////////////////////
///
/// \file jucePathTests.cpp
/// -----------------------
///
///   The five conversions in io/jucePath.hpp, over bytes that are not ASCII.
///
///   This is issue #28's regression test, moved. The bug -- a `ja_JP.UTF-8`
/// desktop where the plugin created `~/<mojibake>/SpectrumWorx` instead of
/// finding the localised Documents folder -- was one conversion at the end of
/// `rootPath()`, and pathsTests.cpp pinned it there. `rootPath()` answers with an
/// `fs::path` now and performs no conversion at all, so the assertion that used
/// to bite ("byte for byte what sst-plugininfra answered") is true by
/// construction and says nothing.
///
///   The *risk* did not go away, it moved: into these five functions, which the
/// preset browser's listing and location strip, both file choosers and the User's
/// Guide link all now go through. So the byte sequence and the character count
/// come with it.
///
/// \note **No platform guard, deliberately.** Every interesting section of
/// pathsTests.cpp was `#ifndef _WIN32`, which left the Windows arm of the
/// conversion -- the `wchar_t` route, the one that cannot be checked by reading
/// it -- with no coverage at all on the platform we only see through a log.
/// Nothing here needs a POSIX path or a POSIX separator: the cases build their
/// paths out of `fs::path` components and an absolute base the build hands them.
///
/// \note No side effect, in keeping with pathsTests.cpp: these paths are built
/// and converted, never created, and none of them needs to exist.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "io/jucePath.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace LE::IO;

/// \note "Documents" in Japanese -- the reporter's locale in issue #28 -- written
/// as the bytes rather than as the characters. The rest of this tree is ASCII but
/// for an em dash and an acute accent, the case is about the bytes anyway, and
/// this way it does not depend on the encoding of the file it is in. Six code
/// points, three bytes each: `U+30C9 U+30AD U+30E5 U+30E1 U+30F3 U+30C8`.
constexpr char const japaneseDocuments[]{"\xE3\x83\x89\xE3\x82\xAD\xE3\x83\xA5"
                                         "\xE3\x83\xA1\xE3\x83\xB3\xE3\x83\x88"};

constexpr int japaneseDocumentsCharacters{6};
constexpr int japaneseDocumentsBytes{18};

/// \brief An absolute path with a non-ASCII component, built rather than spelled.
///
/// \note Absolute because `pathToJuceFile()` may only be called on one --
/// `juce::File` asserts otherwise -- and built off the build tree's own output
/// directory because that is the one absolute path every platform's test run
/// agrees on. It is never created.
///
/// \note **`utf8ToPath()` for the Japanese component, not `operator/` on the
/// bytes**, and this file's first Windows run is what said so. It read
/// `fs::path(SW_TEST_OUTPUT_DIR) / japaneseDocuments / "SpectrumWorx"`, and
/// `fs::path`'s narrow constructor decodes with the *active code page* on
/// Windows -- so the fixture was already mojibake before a single conversion had
/// been tested, and three sections failed against production code that was
/// right. On POSIX the narrow constructor is byte-transparent, which is why it
/// passed everywhere it was written.
///
///   That is the very trap io/jucePath.hpp exists to close, sprung inside the
/// test for it. The base stays a plain construction because it is ASCII, and
/// ASCII is a fixed point of every ANSI code page.
fs::path awkwardPath()
{
    return fs::path(SW_TEST_OUTPUT_DIR) / utf8ToPath(japaneseDocuments) / "SpectrumWorx";
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("UTF-8 bytes become characters rather than Latin-1", "[paths]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The character count is the assertion that actually bites. Byte
    /// equality catches it too, but "six characters, not eighteen" states the
    /// difference between the two `juce::String` constructors directly:
    /// `CharPointer_ASCII` cannot produce six characters out of eighteen bytes,
    /// whatever else it does.
    ///
    ////////////////////////////////////////////////////////////////////////////

    std::string const documents(japaneseDocuments);
    REQUIRE(documents.size() == japaneseDocumentsBytes);

    auto const component(utf8ToPath(documents));

    INFO("component " << pathToUTF8(component));
    CHECK(pathToUTF8(component) == documents);
    CHECK(pathToJuceString(component).length() == japaneseDocumentsCharacters);
    CHECK(pathToJuceString(component).toStdString() == documents);
}

TEST_CASE("A path with a non-ASCII component survives the trip through JUCE", "[paths]")
{
    auto const path(awkwardPath());

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The fixture, asserted before anything is asked of it. When this file
    /// first ran on Windows it failed three times over -- and every one of those
    /// failures was in production code that was correct, because `awkwardPath()`
    /// had built a mojibake path to begin with. A `REQUIRE` here says "the
    /// fixture is wrong" in one line instead.
    ///
    ///   Note which assertions did *not* catch it: all three round trips
    /// passed, because a round trip is self-consistent whatever the bytes mean.
    /// Only the checks against the known sequence bit. \see the note in
    /// pathsTests.cpp on the same distinction.
    ///
    ////////////////////////////////////////////////////////////////////////////

    REQUIRE(pathToUTF8(path.parent_path().filename()) == std::string(japaneseDocuments));

    SECTION("through juce::String")
    {
        auto const string(pathToJuceString(path));

        INFO("string " << string);
        CHECK(juceStringToPath(string) == path);

        /// \note The component, not the whole path: the base is the build tree's
        /// and is ASCII, so counting characters over all of it would say nothing.
        CHECK(pathToJuceString(path.parent_path().filename()).length() ==
              japaneseDocumentsCharacters);
    }

    SECTION("through juce::File -- the one round trip the file choosers make")
    {
        /// \note This is what a `juce::FileChooser` does to a path: it is handed
        /// one to start from and hands a `juce::File` back. Both edges --
        /// presetBrowser.cpp's folder chooser and spectrumWorxEditor.cpp's audio
        /// file chooser -- are exactly this composition.
        auto const file(pathToJuceFile(path));

        INFO("file " << file.getFullPathName());
        CHECK(juceFileToPath(file) == path);
        CHECK(juceFileToPath(file).filename() == "SpectrumWorx");
        CHECK(pathToUTF8(juceFileToPath(file).parent_path().filename()) ==
              std::string(japaneseDocuments));
    }

    SECTION("through the preset format's UTF-8 bytes")
    {
        /// \note What a preset carries: `pathToUTF8` on the way out, `utf8ToPath`
        /// on the way back in. `fs::path( std::string )` in place of the latter
        /// is the active code page on Windows, which is the bug this pins.
        auto const bytes(pathToUTF8(path));

        INFO("bytes " << bytes);
        CHECK(utf8ToPath(bytes) == path);
        CHECK(bytes.find(japaneseDocuments) != std::string::npos);
    }
}

TEST_CASE("The conversions are total over the empty and the relative", "[paths]")
{
    /// \note An empty path is how "no sample is loaded" is spelled everywhere
    /// above this header, so it goes through the conversions as often as a real
    /// one does and must not become a "." or a stray separator.
    CHECK(juceStringToPath(juce::String()).empty());
    CHECK(juceFileToPath(juce::File()).empty());
    CHECK(pathToJuceString(fs::path()).isEmpty());
    CHECK(pathToUTF8(fs::path()).empty());
    CHECK(utf8ToPath({}).empty());

    /// \note A bare factory sample name is a resource key rather than a path --
    /// see Sample::factorySamples() -- and must stay one component, unrooted.
    /// `pathToJuceFile()` is the one conversion it may not be given.
    auto const factory(utf8ToPath("Carrier.mp3"));
    CHECK(factory.filename() == factory);
    CHECK(!factory.is_absolute());
    CHECK(pathToJuceString(factory) == "Carrier.mp3");
}
