////////////////////////////////////////////////////////////////////////////////
///
/// presetFileTests.cpp
/// -------------------
///
///   Two holes, both on the *file* side of loading a preset.
///
///   **Loading one preset on top of another.** `presetCorpusTests.cpp` uses a
/// fresh engine per preset, deliberately and correctly -- a snapshot in which
/// row 200 changes when row 199 does is not a snapshot of row 200. The
/// consequence is that nothing in the suite ever did what a user does, which is
/// click the next preset in the browser. That is not the same code path:
/// `loadModuleChain()` *merges*, moving a module already holding the right
/// effect across rather than building a new one, so the second load is the
/// interesting one and it had never run headlessly against the corpus.
///
///   **A file that is not a preset.** Four `[clap][state]` cases drive bad
/// *streams* -- empty, truncated, wrong root, a format from the future -- and
/// the file side had none of it. `readPresetFile` has four ways to answer "no"
/// and nothing had asked it any of them. The corpus proves 303 happy paths and
/// asserts `unknownEffect == 0` and `missing` against a committed number, so
/// both counters were only ever checked in the direction that says nothing:
/// neither had ever been driven above zero, and a reporter wired to a counter
/// that is never incremented reads exactly like one that works.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "presets/presetHarness.hpp"
#include "utility/localeHarness.hpp"

#include "core/modules/factory.hpp"

/// \note Not sorted with the block above, for the reason presetCorpusTests.cpp
/// gives: loadPreset() downcasts a chain node to SW::Module and this is the
/// header with the complete type.
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/modules/finalImplementations.hpp"

#include "le/spectrumworx/presetStorage.hpp"
#include "le/spectrumworx/presets.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace LE;
using namespace LE::SW;

using SWTest::Loaded;
using SWTest::PresetConsumer;
using SWTest::ScopedProblemCounter;

//------------------------------------------------------------------------------
// The corpus, and reading it
//------------------------------------------------------------------------------

std::vector<std::pair<std::string, std::filesystem::path>> corpus()
{
    std::filesystem::path const root(SW_PRESET_DATA_DIR);
    std::vector<std::pair<std::string, std::filesystem::path>> found;

    std::error_code error;
    for (auto const &entry : std::filesystem::recursive_directory_iterator(root, error))
    {
        if (!entry.is_regular_file() || (entry.path().extension() != ".swp"))
            continue;
        found.emplace_back(std::filesystem::relative(entry.path(), root, error).generic_string(),
                           entry.path());
    }

    std::ranges::sort(found, {}, &std::pair<std::string, std::filesystem::path>::first);
    return found;
}

/// The bytes of a preset file, in the writable NUL-terminated buffer the
/// destructive parse wants. Empty if the file cannot be read.
std::vector<char> presetBytes(std::filesystem::path const &file)
{
    auto const data(readPresetFile(file));
    if (!data)
        return {};
    std::string_view const text(data.get());
    std::vector<char> buffer(text.begin(), text.end());
    buffer.push_back('\0');
    return buffer;
}

/// A configured engine with nothing loaded into it.
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

    /// \brief Loads \p data into this engine, whatever it already holds.
    /// \return whether the load succeeded.
    bool load(std::vector<char> data)
    {
        if (data.empty())
            return false;
        SWTest::clearPresetProblems();
        ScopedProblemCounter const counting;
        return LE::SW::loadPreset(data.data(), true /*ignore external samples*/, nullptr,
                                  PresetConsumer{engine_});
    }

    Loaded dump() { return SWTest::dump(engine_); }

  private:
    SWTest::Engine engine_;
}; // class Fixture

/// What \p file loads to in an engine that has never held anything else, which
/// is the answer presetCorpus.txt committed.
Loaded intoAFreshEngine(std::filesystem::path const &file)
{
    Fixture fixture;
    REQUIRE(fixture.load(presetBytes(file)));
    return fixture.dump();
}

//------------------------------------------------------------------------------
// Writing files that are not presets
//------------------------------------------------------------------------------

std::filesystem::path outputDirectory()
{
    std::filesystem::path const directory(std::filesystem::path(SW_TEST_OUTPUT_DIR) / "presets");
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory;
}

/// \brief Puts \p bytes on disk under \p name and hands back the path.
std::filesystem::path fileHolding(std::string const &name, std::string_view const bytes)
{
    auto const path(outputDirectory() / name);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size())));
    stream.close();
    REQUIRE(std::filesystem::exists(path));
    return path;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
// One preset on top of another
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A preset loaded on top of another is the preset, not a blend", "[preset-file]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The whole corpus through **one** engine, in order, which is a
    /// browser being clicked down a bank. After each load the engine is compared
    /// against the same preset loaded into a fresh one, so every preset is
    /// checked against a *different* predecessor -- 302 distinct "what was there
    /// before" states rather than one chosen by hand.
    ///
    ///   What it is written against is the merge: `loadModuleChain()` moves a
    /// module already holding the right effect across rather than building a new
    /// one, so anything the previous preset left in that module and the new one
    /// does not mention is carried forward. A parameter the writer omits because
    /// it is at its default is exactly such a thing, and 104 of these 303 files
    /// omit at least one.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const files(corpus());
    REQUIRE(files.size() >= 288);

    Fixture reused;
    unsigned int loaded{0};
    std::string previous("(nothing)");

    for (auto const &[key, path] : files)
    {
        INFO("preset " << key << " loaded on top of " << previous);

        REQUIRE(reused.load(presetBytes(path)));
        auto const onTop(reused.dump());
        auto const fresh(intoAFreshEngine(path));

        // The effect chain first, in the clear: the common failure is a module
        // the previous preset had and this one did not ask for.
        CHECK(onTop.modules == fresh.modules);
        CHECK(onTop.effects == fresh.effects);
        // ...and then every parameter of every one of them.
        CHECK(onTop.text == fresh.text);

        previous = key;
        ++loaded;
    }

    INFO("presets loaded in sequence: " << loaded);
    CHECK(loaded == files.size());
}

TEST_CASE("Loading the same preset twice changes nothing the second time", "[preset-file]")
{
    /// \note The degenerate case of the merge, and the one where it does the
    /// most work: every module in the chain already holds the effect the preset
    /// asks for, so every one of them is moved across rather than built. A
    /// merge that mutated what it moved would show up here on the second load
    /// and nowhere else.
    auto const files(corpus());
    REQUIRE(files.size() >= 288);

    /// The first preset holding three or more modules, so the merge has a real
    /// chain to reuse rather than one slot.
    for (auto const &[key, path] : files)
    {
        Fixture fixture;
        REQUIRE(fixture.load(presetBytes(path)));
        auto const once(fixture.dump());
        if (once.modules < 3)
            continue;

        INFO("preset " << key << ", " << once.modules << " modules");
        REQUIRE(fixture.load(presetBytes(path)));
        CHECK(fixture.dump().text == once.text);
        return;
    }
    FAIL("no preset in the corpus holds three modules");
}

TEST_CASE("An empty preset empties the chain a full one filled", "[preset-file]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The direction the corpus sweep above cannot reach, because every
    /// file in it has at least one module: loading a preset with **no** modules
    /// on top of one with five has to leave five empty slots, not five modules
    /// nobody asked for.
    ///
    ///   Written by hand rather than found in the banks for that reason. It is
    /// legal state -- `stateTests.cpp` builds the same shape -- and it is what
    /// "New" in the browser produces.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const files(corpus());
    REQUIRE_FALSE(files.empty());

    Fixture fixture;
    REQUIRE(fixture.load(presetBytes(files.front().second)));
    REQUIRE(fixture.dump().modules > 0);

    std::string const empty(
        "<SpectrumWorxPreset Format=\"3\" Version=\"3.0\" LastModified=\"\" Comment=\"\">"
        "<Global><p n=\"In\" v=\"0.5\" /></Global>"
        "<Modules /></SpectrumWorxPreset>");
    REQUIRE(fixture.load(std::vector<char>(empty.begin(), empty.end() + 1)));

    auto const cleared(fixture.dump());
    CHECK(cleared.modules == 0);
    CHECK(cleared.effects == "-");
}

////////////////////////////////////////////////////////////////////////////////
// A file that is not a preset
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A preset file that cannot be read answers with nothing", "[preset-file]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `readPresetFile` has four ways to fail and nothing had driven any
    /// of them. Each is a real thing a user can arrange -- a preset deleted
    /// while the browser still lists it, a directory named `*.swp`, a file
    /// truncated by a full disk -- and the contract for all of them is the same
    /// empty pointer rather than a partly filled buffer.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const directory(outputDirectory());

    CHECK_FALSE(readPresetFile(directory / "no such preset.swp"));

    /// \note A directory, which `file_size` answers about without erroring on
    /// some platforms -- so this is the one that could plausibly get through and
    /// hand back a buffer of whatever the directory entry's size happens to be.
    auto const asDirectory(directory / "a directory.swp");
    std::error_code error;
    std::filesystem::create_directories(asDirectory, error);
    CHECK_FALSE(readPresetFile(asDirectory));

    /// An empty file reads successfully and yields a buffer holding just the
    /// terminator -- which is a *parse* failure rather than a read one, and the
    /// case below is where that is asserted.
    auto const emptyFile(fileHolding("empty.swp", ""));
    auto const empty(readPresetFile(emptyFile));
    REQUIRE(empty);
    CHECK(empty[0] == '\0');
}

TEST_CASE("A file that is not a preset is refused rather than half applied", "[preset-file]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The file-side twin of stateTests.cpp's "A state that cannot be read
    /// is refused rather than half applied". Same shapes, driven through
    /// `readPresetFile` and `loadPreset` instead of through a `clap_istream`,
    /// because those are different code and a user reaches them by a different
    /// route -- the browser, not the host.
    ///
    ///   As there, the `false` is the smaller half of it. What matters is that
    /// the engine still holds the preset it held before, rather than a chain
    /// half rebuilt from a document that stopped making sense in the middle.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const files(corpus());
    REQUIRE_FALSE(files.empty());
    auto const good(presetBytes(files.front().second));
    REQUIRE_FALSE(good.empty());

    auto const refuses([&](std::string const &name, std::string_view const bytes) {
        INFO(name);
        Fixture fixture;
        REQUIRE(fixture.load(good));
        auto const before(fixture.dump());

        auto const path(fileHolding(name, bytes));
        CHECK_FALSE(fixture.load(presetBytes(path)));
        CHECK(fixture.dump().text == before.text);
    });

    refuses("empty.swp", "");
    refuses("prose.swp", "this is not a preset, it is a sentence");
    refuses("half a document.swp", "<SpectrumWorxPreset Format=\"3\"><Global>");
    refuses("wrong root.swp", "<SomethingElse Format=\"3\"></SomethingElse>");
    refuses("from the future.swp",
            "<SpectrumWorxPreset Format=\"99\"><Global /><Modules /></SpectrumWorxPreset>");

    /// \note A real preset cut in half, which is the truncation a full disk
    /// actually produces: valid bytes as far as they go, and a tag that never
    /// closes. Cut at 60 % so the cut lands inside the module list rather than
    /// in the header, where the parse would fail on the first element.
    std::string_view const whole(good.data(), good.size() - 1);
    refuses("truncated.swp", whole.substr(0, whole.size() * 3 / 5));
}

TEST_CASE("A preset naming an effect this build does not have loads the rest", "[preset-file]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `unknownEffect` driven above zero for the first time. The corpus
    /// asserts it is *zero* over 303 files, which is the right assertion there
    /// -- an effect going missing is a build that dropped one -- but on its own
    /// it is equally consistent with a counter nothing ever increments and a
    /// reporter nothing ever calls.
    ///
    ///   The behaviour it pins is degrading rather than refusing, which is the
    /// preset format's long-standing contract: a project written by a build with
    /// an effect this one does not have still opens, with everything it can
    /// understand.
    ///
    ////////////////////////////////////////////////////////////////////////////
    std::string const preset(
        "<SpectrumWorxPreset Format=\"3\" Version=\"3.0\" LastModified=\"\" Comment=\"\">"
        "<Global><p n=\"In\" v=\"0.25\" /></Global>"
        "<Modules>"
        "<Module effect=\"No Such Effect\"><p n=\"Bypass\" v=\"0\" /></Module>"
        "<Module effect=\"Ah-ah\"><p n=\"Bypass\" v=\"0\" /></Module>"
        "</Modules></SpectrumWorxPreset>");

    Fixture fixture;
    REQUIRE(fixture.load(presetBytes(fileHolding("unknown effect.swp", preset))));

    auto const loaded(fixture.dump());
    CHECK(loaded.modules == 1);
    CHECK(loaded.effects == "Ah-ah");

    // ...and it said so, once, rather than dropping the module silently.
    CHECK(SWTest::presetProblems().unknownEffect == 1);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The module count a file supplies had no bound in a release build --
/// only `LE_ASSERT_MSG(moduleIndex <= maxNumberOfModules)`, which is not a check.
/// Past the fifth slot there is nothing for a module to be: `ParameterID` packs
/// its indices into bytes and the parameter list was built once for five slots,
/// so the sixth module's parameters have no id a host can name; the rack has five
/// strips. And `ModuleChainBase::size()` is a `std::uint8_t`, so a chain of 256
/// reports itself **empty** -- the truncation this finding is named for, and the
/// reason a bound is worth more than an assert.
///
/// \note Seven, not 256. What is being pinned is the rule, and a preset carrying
/// two modules more than there are slots exercises it exactly as a preset
/// carrying 251 more would -- while staying a document a person can read.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A preset with more modules than there are slots loads the ones that fit",
          "[preset-file][hostile]")
{
    std::string preset(
        "<SpectrumWorxPreset Format=\"3\" Version=\"3.0\" LastModified=\"\" Comment=\"\">"
        "<Global><p n=\"In\" v=\"0.25\" /></Global>"
        "<Modules>");
    constexpr unsigned int modulesInTheFile{7};
    static_assert(modulesInTheFile > LE::SW::Constants::maxNumberOfModules);
    for (unsigned int module{0}; module < modulesInTheFile; ++module)
        preset += "<Module effect=\"Ah-ah\"><p n=\"Bypass\" v=\"0\" /></Module>";
    preset += "</Modules></SpectrumWorxPreset>";

    Fixture fixture;
    REQUIRE(fixture.load(presetBytes(fileHolding("too many modules.swp", preset))));

    auto const loaded(fixture.dump());
    CHECK(loaded.modules == LE::SW::Constants::maxNumberOfModules);

    // ...and the chain says so too, rather than counting past its own type.
    CHECK(fixture.engine().program().moduleChain().size() == LE::SW::Constants::maxNumberOfModules);

    // Refused rather than dropped: one report per module that did not fit.
    CHECK(SWTest::presetProblems().other ==
          (modulesInTheFile - LE::SW::Constants::maxNumberOfModules));
}

TEST_CASE("A preset that omits a parameter reports it and uses the default", "[preset-file]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `missingParameter` driven deliberately, which is the other counter
    /// the corpus only ever reads. It reports a committed *number* per preset --
    /// normal for a 2009-2011 file against a 2016 effect, and a number that must
    /// not grow -- so the count is pinned but the mechanism behind it is not:
    /// a reporter that stopped firing would move 104 rows at once and read as
    /// one large diff rather than as a broken reporter.
    ///
    ///   Here the omission is deliberate and the expected count is exact.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const withOnly([](std::string_view const parameters) {
        return std::string("<SpectrumWorxPreset Format=\"3\" Version=\"3.0\" LastModified=\"\" "
                           "Comment=\"\"><Global><p n=\"In\" v=\"0.5\" /></Global><Modules>"
                           "<Module effect=\"Gain\">") +
               std::string(parameters) + "</Module></Modules></SpectrumWorxPreset>";
    });

    /// Everything Gain has: five base parameters and its own one, so a complete
    /// preset raises nothing and the difference below is the omission itself.
    auto const complete(withOnly("<p n=\"Bypass\" v=\"0\" /><p n=\"Gain\" v=\"0\" />"
                                 "<p n=\"Wet\" v=\"100\" /><p n=\"StartFreq\" v=\"0\" />"
                                 "<p n=\"StopFreq\" v=\"22050\" /><p n=\"Gain\" v=\"3\" />"));

    Fixture fixture;
    REQUIRE(fixture.load(presetBytes(fileHolding("complete.swp", complete))));
    auto const whenComplete(SWTest::presetProblems().missingParameter);

    /// The same module with only its Bypass, which is what a file written by a
    /// build whose Gain had fewer knobs looks like.
    REQUIRE(fixture.load(
        presetBytes(fileHolding("sparse.swp", withOnly("<p n=\"Bypass\" v=\"0\" />")))));
    auto const whenSparse(SWTest::presetProblems().missingParameter);

    UNSCOPED_INFO("complete: " << whenComplete << " missing, sparse: " << whenSparse);
    CHECK(whenSparse > whenComplete);

    // It loaded anyway, and what it did not mention is at its default.
    auto const loaded(fixture.dump());
    CHECK(loaded.modules == 1);
    CHECK(loaded.effects == "Gain");
}

////////////////////////////////////////////////////////////////////////////////
//
// A preset whose LFO values are not values
// ----------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The one family of preset attributes that was read straight into a
/// parameter without asking whether it was in range. Every ordinary parameter
/// goes through `ParametersLoader::operator()`, which checks `isValidValue()`
/// before `setValue()`; the seven LFO sub-parameters went through
/// `LFODataLoader::doLoad()`, which checked nothing, and `Parameter::setValue`'s
/// own range check is an assertion -- absent in a release build.
///
///   `wfrm` is the sharp one. It is an EnumeratedParameter<11> and it indexes
/// `lfoFunctions[]`, an eleven-entry table of function pointers, every block for
/// as long as the LFO is enabled -- `lfoFunctions[waveForm()](…)` with no bound
/// (lfoImpl.cpp:357). A preset carrying `wfrm="200"` is an indirect call through
/// whatever sits 189 entries past that table, on the audio thread, on the first
/// block after the load. `ph`, `lbnd` and `ubnd` are the same shape one step
/// further on: out of [0,1] they make `LE_ASSUME(position >= 0 && <= 1)` a false
/// assumption, which is undefined behaviour by construction.
///
///   Reachable from a double-clicked `.swp` and from session state, which take
/// the same reader.
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
/// An "Ah-ah" whose LFO'd parameter carries \p lfoAttributes.
std::string presetWithLFO(std::string_view const lfoAttributes)
{
    return std::string("<SpectrumWorxPreset Format=\"3\" Version=\"3.0\" LastModified=\"\" "
                       "Comment=\"\">"
                       "<Global>"
                       "<p n=\"In\" v=\"1\" /><p n=\"Out\" v=\"1\" /><p n=\"Mix\" v=\"1\" />"
                       "<p n=\"FFT size\" v=\"2048\" /><p n=\"Overlap factor\" v=\"4\" />"
                       "<p n=\"Window type\" v=\"1\" />"
                       "</Global><Modules><Module effect=\"Ah-ah\">"
                       "<p n=\"Bypass\" v=\"0\" />"
                       "<p n=\"Gain\" v=\"0\" sync=\"0\" />"
                       "<p n=\"Wet\" v=\"100\" sync=\"0\" />"
                       "<p n=\"Start frequency\" v=\"0\" sync=\"0\" />"
                       "<p n=\"Stop frequency\" v=\"1\" sync=\"0\" />"
                       "<p n=\"Center (LFO me!)\" v=\"3000\" ") +
           std::string(lfoAttributes) +
           " />"
           "<p n=\"Width\" v=\"750\" sync=\"0\" />"
           "<p n=\"Strength\" v=\"9999\" sync=\"0\" />"
           "</Module></Modules></SpectrumWorxPreset>";
}

/// \brief Every LFO in \p engine's chain, checked against its own parameters'
/// ranges.
///
/// \note The whole chain rather than the one that was tampered with: what has to
/// be true after *any* load is that nothing in the engine is out of range, and a
/// case that looked only where it put the bad value would not notice it landing
/// somewhere else.
void requireEveryLFOInRange(SWTest::Engine &engine)
{
    using LE::Parameters::LFO;

    auto &chain(engine.program().moduleChain());
    for (std::uint8_t slot(0); slot < chain.size(); ++slot)
    {
        auto const pModule(chain.moduleAs<LE::SW::Module>(slot));
        REQUIRE(pModule);

        for (std::uint8_t index(0); index < pModule->numberOfLFOControledParameters(); ++index)
        {
            auto const &lfo(pModule->lfo(index));
            INFO("slot " << unsigned(slot) << ", LFO " << unsigned(index));

            // The one that is an indirect call rather than a wrong number.
            CHECK(lfo.waveForm() < LFO::NumberOfWaveforms);

            CHECK(lfo.phase() >= -0.5f);
            CHECK(lfo.phase() <= 0.5f);
            CHECK(lfo.lowerBound() >= 0.0f);
            CHECK(lfo.lowerBound() <= 1.0f);
            CHECK(lfo.upperBound() >= 0.0f);
            CHECK(lfo.upperBound() <= 1.0f);
        }
    }
}
} // anonymous namespace

TEST_CASE("A preset carrying an impossible LFO waveform does not keep it", "[preset-file][hostile]")
{
    Fixture fixture;
    REQUIRE(fixture.load(presetBytes(fileHolding(
        "hostile lfo.swp",
        presetWithLFO("on=\"1\" T=\"500\" ph=\"0.25\" lbnd=\"0.1\" ubnd=\"0.9\" sync=\"0\" "
                      "wfrm=\"200\"")))));

    // The preset still loads -- one bad attribute is not a reason to refuse a
    // whole rack -- and the waveform is one the table has.
    auto const loaded(fixture.dump());
    CHECK(loaded.modules == 1);
    CHECK(loaded.effects == "Ah-ah");

    requireEveryLFOInRange(fixture.engine());
}

TEST_CASE("A preset carrying out-of-range LFO bounds does not keep them", "[preset-file][hostile]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `ph="1e30"` is also what reaches `lexical_cast(double, 1, char *)`
    /// through the editor's phase display, where a value that wide overruns the
    /// caller's buffer. That is its own fix; here it only has to not be stored.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Fixture fixture;
    REQUIRE(fixture.load(presetBytes(fileHolding(
        "hostile lfo bounds.swp",
        presetWithLFO("on=\"1\" T=\"500\" ph=\"1e30\" lbnd=\"-5\" ubnd=\"7\" sync=\"99\" "
                      "wfrm=\"0\"")))));

    auto const loaded(fixture.dump());
    CHECK(loaded.modules == 1);

    requireEveryLFOInRange(fixture.engine());
}

TEST_CASE("A preset whose LFO values are all legal keeps every one of them",
          "[preset-file][hostile]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The direction the two above cannot fail in. A gate that threw away
    /// every LFO attribute would satisfy both of them and quietly flatten every
    /// preset in the shipping banks, so what the values *are* is pinned here.
    ///
    ////////////////////////////////////////////////////////////////////////////
    Fixture fixture;
    REQUIRE(fixture.load(presetBytes(fileHolding(
        "legal lfo.swp",
        presetWithLFO("on=\"1\" T=\"500\" ph=\"0.25\" lbnd=\"0.1\" ubnd=\"0.9\" sync=\"0\" "
                      "wfrm=\"4\"")))));

    requireEveryLFOInRange(fixture.engine());

    auto &chain(fixture.engine().program().moduleChain());
    REQUIRE(chain.size() == 1);
    auto const pModule(chain.moduleAs<LE::SW::Module>(0));
    REQUIRE(pModule);

    /// The LFO on "Center (LFO me!)", which is the module's sixth parameter and
    /// so its fifth LFO -- the first parameter, Bypass, has none.
    auto const &lfo(pModule->lfo(4));
    CHECK(lfo.enabled());
    CHECK(lfo.waveForm() == LE::Parameters::LFO::Square);
    CHECK(lfo.phase() == 0.25f);
    CHECK(lfo.lowerBound() == 0.1f);
    CHECK(lfo.upperBound() == 0.9f);
}

////////////////////////////////////////////////////////////////////////////////
//
// A file that is too big to be a preset
// -------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note `readPresetFile` narrowed `std::filesystem::file_size`'s `uintmax_t` to
/// an `unsigned int` before doing anything with it, so a file over 4 GiB was
/// read as `size mod 2^32` and parsed as a complete preset -- and at exactly
/// 4 GiB - 1 the `presetSize + 1` allocation wrapped to zero, giving a
/// zero-length buffer a 4 GiB read.
///
///   Neither needs a crafted file. Any large file renamed to `.swp`, or picked
/// in the browser, is one.
///
/// \note Sized just past the cap rather than past 4 GiB. `resize_file` makes a
/// sparse file in a millisecond here, but it is a real allocation on NTFS and
/// this has to run on a CI runner's disk. The cap is what makes the narrowing
/// unreachable, so the cap is what is worth pinning.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A file too large to be a preset is refused before it is read", "[preset-file][hostile]")
{
    auto const path(outputDirectory() / "much too large.swp");

    // Something that would parse, so that "refused" cannot be confused with
    // "did not parse".
    {
        std::string const preset("<SpectrumWorxPreset Format=\"3\" Version=\"3.0\" "
                                 "LastModified=\"\" Comment=\"\"><Global /><Modules />"
                                 "</SpectrumWorxPreset>");
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        REQUIRE(stream.write(preset.data(), static_cast<std::streamsize>(preset.size())));
    }
    REQUIRE(readPresetFile(path)); // it reads at this size

    std::error_code error;
    std::filesystem::resize_file(path, LE::SW::maximumPresetSize + 1, error);
    REQUIRE(!error);
    REQUIRE(std::filesystem::file_size(path) == LE::SW::maximumPresetSize + 1);

    // The same bytes at the front, and now it is not a preset.
    CHECK(!readPresetFile(path));

    std::filesystem::remove(path, error);
}

////////////////////////////////////////////////////////////////////////////////
//
// The host's locale
// -----------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The whole committed corpus, read twice: once in the C locale and once
/// under a host that has set a comma-decimal one. Every float in every one of
/// these files is written with a point, and `strtod` in such a locale stops at
/// it -- so `In="0.7000"` came back as 0.7 on one machine and **0** on another,
/// for the same bytes. Not a parse failure and not reported: a preset that had
/// been a filter sweep just quietly became silence.
///
///   Whole files rather than a value or two, because what the locale reaches is
/// every number in the format at once -- gains, mix, frequencies, LFO bounds --
/// and the interesting failures are the ones where the wrong value is still a
/// legal one. The dump compares them all.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A host's locale does not change what a preset file loads as", "[preset-file][locale]")
{
    auto const files(corpus());
    REQUIRE(files.size() >= 288);

    std::vector<Loaded> asWritten;
    asWritten.reserve(files.size());
    for (auto const &[key, path] : files)
        asWritten.push_back(intoAFreshEngine(path));

    SWTest::CommaDecimalHost const host;
    if (!host)
        SKIP("No comma-decimal locale is installed on this machine.");

    for (std::size_t index{0}; index < files.size(); ++index)
    {
        INFO("preset " << files[index].first);
        auto const underTheHostsLocale(intoAFreshEngine(files[index].second));
        CHECK(underTheHostsLocale.text == asWritten[index].text);
    }
}
