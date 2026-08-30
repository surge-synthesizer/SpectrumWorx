////////////////////////////////////////////////////////////////////////////////
///
/// \file authorTests.cpp
/// ---------------------
///
///   The byline: what a patch may be signed with, and what a signature does to
/// the document. \see issue #56.
///
///   Two claims, and the second is the one that needed a rule at all. **A name
/// is sanitised before it reaches a file** -- so a name with a quote in it
/// cannot end a stanza that was never meant to end, and cannot come back as
/// `&quot;` either, which is what would happen if the escaping were the whole of
/// the defence. **And an unsigned patch is byte-for-byte the patch it always
/// was**, which is what keeps every committed fixture and every digest in
/// `presetFixtures.txt` still.
///
///   The reason the rule is not merely belt and braces: TinyXML *does* escape an
/// attribute value it prints, so a hostile name would produce a well-formed
/// document either way. What it would not produce is a *byline* -- the name in
/// the file and the name the user typed would be different strings, and the
/// difference would only show up wherever it was read back.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "presets/presetHarness.hpp"

#include "core/modules/factory.hpp"

/// \note Not sorted with the block above, for the reason presetCorpusTests.cpp
/// gives: loadPreset() downcasts a chain node to SW::Module and this is the
/// header with the complete type.
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/modules/finalImplementations.hpp"

#include "le/spectrumworx/authorName.hpp"
#include "le/spectrumworx/presets.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace LE;
using namespace LE::SW;

/// \note A class rather than a function returning one, for the reason
/// presetRoundTripTests.cpp gives: SWTest::Engine points SpectrumWorxCore at a
/// Program it holds by value, so it can be neither copied nor moved.
class Fixture
{
  public:
    Fixture()
    {
        engine_.setNumberOfChannels(2, 2);
        engine_.setSampleRate(48000);
        engine_.setBlockSize(512);
        REQUIRE(engine_.initialise());
    }

    SWTest::Engine &engine() { return engine_; }

  private:
    SWTest::Engine engine_;
}; // class Fixture

/// \brief What savePreset() writes for \p author, parsed back.
///
/// \note Through a real parse rather than a substring search: what the case is
/// about is what a *reader* sees, and a document whose attribute is escaped and
/// one whose attribute was sanitised look different in the text and the same to
/// `strstr`.
std::string authorOf(std::string const &document)
{
    std::vector<char> buffer(document.begin(), document.end());
    buffer.push_back('\0'); // the parse is destructive and wants a terminator

    Preset preset;
    REQUIRE(preset.loadFrom(buffer.data()));
    return std::string(preset.author());
}

std::string saveSignedBy(std::string_view const author)
{
    Fixture fixture;
    auto &engine(fixture.engine());
    return savePreset({}, engine.sideChainSource(), "signed", author, engine.program());
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("An author name keeps everything an attribute value may carry", "[preset-author][author]")
{
    CHECK(sanitisedAuthorName("Paul Walker") == "Paul Walker");

    // the ordinary punctuation of a name, none of which is a problem
    CHECK(sanitisedAuthorName("Jean-Luc O Brien (SST), 2026 & co.") ==
          "Jean-Luc O Brien (SST), 2026 & co.");

    /// \note `&` above is deliberate. It is the one character TinyXML has to
    /// escape that this does *not* drop: an ampersand cannot end an attribute
    /// value, it reads back as itself, and a rule that took it out would be
    /// refusing to spell a good half of the duos in the corpus.
    CHECK(authorOf(saveSignedBy("Simon & Garfunkel")) == "Simon & Garfunkel");

    // and a name in somebody else's script goes through whole
    CHECK(sanitisedAuthorName("Ана Ковачевић") == "Ана Ковачевић");
}

TEST_CASE("An author name drops what would close an attribute early",
          "[preset-author][author][hostile]")
{
    // the shape from the issue: a name that tries to finish the tag it is in
    CHECK(sanitisedAuthorName(R"(paul"></xml>)") == "paul/xml");

    CHECK(sanitisedAuthorName("a\"b") == "ab");
    CHECK(sanitisedAuthorName("a'b") == "ab");
    CHECK(sanitisedAuthorName("a<b") == "ab");
    CHECK(sanitisedAuthorName("a>b") == "ab");

    /// \note And the control codes, which an attribute value may legally carry
    /// but which the reader normalises: a name holding a newline would not come
    /// back as the name that was typed.
    CHECK(sanitisedAuthorName("two\nlines\tover") == "twolinesover");

    // surrounding blanks are not a name either, and a tab-only entry is nobody
    CHECK(sanitisedAuthorName("   Paul   ") == "Paul");
    CHECK(sanitisedAuthorName(" \t\r\n ").empty());
}

TEST_CASE("An author name is cut to length without splitting a character",
          "[preset-author][author]")
{
    std::string const tooLong(maxAuthorLength * 2, 'x');
    CHECK(sanitisedAuthorName(tooLong).size() == maxAuthorLength);

    /// \note Two bytes each, and 64 divides by two: the last character ends
    /// exactly on the limit, so a cut that backed out of it unconditionally
    /// would throw away a whole character that was never broken.
    std::string cyrillic;
    while (cyrillic.size() < maxAuthorLength * 2)
        cyrillic += "д";
    CHECK(sanitisedAuthorName(cyrillic).size() == maxAuthorLength);

    /// \note Three bytes each, and 64 does not divide by three: the cut lands
    /// one byte into the twenty-second character, and what comes back is
    /// twenty-one whole ones rather than a name ending in a third of a code
    /// point.
    std::string arrows;
    while (arrows.size() < maxAuthorLength * 3)
        arrows += "→";

    auto const kept(sanitisedAuthorName(arrows));
    CHECK(kept.size() == 63);
    CHECK((kept.size() % 3) == 0);
}

TEST_CASE("A saved patch carries the name it was signed with", "[preset-author][author]")
{
    auto const signed_(saveSignedBy("Martin Walker"));
    CHECK(authorOf(signed_) == "Martin Walker");

    // and where the reader finds it: an attribute on the root, beside Comment
    CHECK(signed_.find("Author=\"Martin Walker\"") != std::string::npos);
}

TEST_CASE("A hostile name reaches the file as a name and nothing else",
          "[preset-author][author][hostile]")
{
    auto const signed_(saveSignedBy(R"(paul"></SpectrumWorxPreset>)"));

    // it parses, it is the whole document, and the byline is what the rule left
    CHECK(authorOf(signed_) == "paul/SpectrumWorxPreset");

    /// \note Nothing escaped, either: had the name reached the writer intact,
    /// this document would be well-formed and its byline would read
    /// `paul&quot;&gt;&lt;/SpectrumWorxPreset&gt;` -- which is not what anybody
    /// typed and not a name.
    CHECK(signed_.find("&quot;") == std::string::npos);
}

TEST_CASE("An unsigned patch is the patch it always was", "[preset-author][author]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note What keeps `presetFixtures.txt` and the committed 3.0 fixture
    /// still: an author who has not named themselves writes no attribute rather
    /// than an empty one, so the default build's output did not move when this
    /// landed. An absent attribute is also the honest reading -- nobody claimed
    /// it.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const unsigned_(saveSignedBy({}));

    CHECK(unsigned_.find("Author=") == std::string::npos);
    CHECK(authorOf(unsigned_).empty());

    // a name that sanitises away is the same case as never having given one
    CHECK(saveSignedBy(R"("'<>)").find("Author=") == std::string::npos);
}

TEST_CASE("Editing a 2.x preset's comment does not sign it", "[preset-author][author]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The reason the byline is SavedPreset's rather than PresetHeader's.
    /// `saveDirtyComment()` reparses a file, moves its `Comment` and prints back
    /// the document it read, so that a 2.x preset stays 2.x -- and a byline
    /// written by `setHeader()` would be retrofitted onto a grammar that has no
    /// such attribute, in a file the plugin it came from cannot then read as it
    /// wrote it. Retrofitting the author to 2.x is deliberately not done yet.
    ///
    ////////////////////////////////////////////////////////////////////////////
    std::string legacy(R"(<SpectrumWorxPreset Version="2.6" LastModified="01.01.2011 00:00" )"
                       R"(Comment=""><Global In="1"/></SpectrumWorxPreset>)");
    std::vector<char> buffer(legacy.begin(), legacy.end());
    buffer.push_back('\0');

    Preset preset;
    REQUIRE(preset.loadFrom(buffer.data()));
    REQUIRE(preset.formatVersion() == 0); // no Format attribute: the legacy grammar

    PresetHeader const header{std::string_view("a new comment")};
    preset.setHeader(header);

    auto const rewritten(preset.saveTo());
    CHECK(rewritten.find("a new comment") != std::string::npos);
    CHECK(rewritten.find("Author=") == std::string::npos);
    CHECK(rewritten.find("Format=") == std::string::npos);
}
