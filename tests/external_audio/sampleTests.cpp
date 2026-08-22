////////////////////////////////////////////////////////////////////////////////
///
/// sampleTests.cpp
/// ---------------
///
///   The external audio file loader. Two things are worth pinning and neither
/// was before: that the factory samples are actually in the binary and actually
/// decode -- they are MP3, and which decoder answers for that is a per-platform
/// question (§5.0) -- and that a load produces two equal channels at the rate it
/// was asked for, because that is the contract the side channel is fed under.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "external_audio/sample.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>
//------------------------------------------------------------------------------

namespace
{
using LE::Sample;

float peak(Sample::ChannelData const &channel)
{
    float loudest{0};
    for (auto const value : channel)
        loudest = std::max(loudest, std::abs(value));
    return loudest;
}
} // anonymous namespace

TEST_CASE("The factory samples are embedded", "[external-audio]")
{
    auto const samples(Sample::factorySamples());

    REQUIRE(!samples.empty());

    for (auto const &sample : samples)
    {
        UNSCOPED_INFO(sample);
        /// \note A bare name and no directory: see the note on factorySamples().
        /// As an `fs::path` that is one statement -- the whole path *is* its own
        /// last component -- where it used to take two accessors to say.
        CHECK(sample.filename() == sample);
        CHECK(!sample.is_absolute());
        CHECK(!sample.stem().empty());
        CHECK(Sample::isFactorySample(sample));
    }

    /// \note Plain, because `fs::path` is totally ordered and every entry is a
    /// single component -- so ordering by the whole path is ordering by name.
    CHECK(std::ranges::is_sorted(samples));
}

TEST_CASE("A file that is neither on disk nor embedded fails to load", "[external-audio]")
{
    Sample sample;

    CHECK(sample.load("no such sample.mp3", 48000) != nullptr);
    CHECK(!sample);
    CHECK(sample.sampleRate() == 0);
}

TEST_CASE("Every factory sample decodes to two equal channels", "[external-audio]")
{
    constexpr unsigned int rate{48000};

    for (auto const &file : Sample::factorySamples())
    {
        UNSCOPED_INFO(file);

        Sample sample;
        REQUIRE(sample.load(file, rate) == nullptr);
        REQUIRE(static_cast<bool>(sample));

        CHECK(sample.sampleFile() == file);
        CHECK(sample.sampleRate() == rate);

        // A floor, not a duration -- and a quarter second rather than the half
        // it used to be, because half a second is more audio than the shortest
        // of these files holds and the check only ever passed on padding.
        //
        //   MW-Metallica1.mp3 is that file. `afinfo` reads it as "21454 valid
        // frames + 576 priming + 1010 remainder = 23040" at 44.1 kHz, and at
        // 48 kHz it decoded to 25077 frames on one macOS and 23351 on a GitHub
        // runner. Those are precisely the padded and the unpadded lengths
        // resampled: the two decoders disagree about whether LAME's encoder
        // delay is theirs to strip. `> rate / 2` sat between the two answers,
        // so what it tested was a padding policy.
        CHECK(sample.channel1().size() > (rate / 4));
        CHECK(sample.channel1().size() == sample.channel2().size());
        CHECK(sample.channel(0).begin() == sample.channel1().begin());
        CHECK(sample.channel(1).begin() == sample.channel2().begin());

        // Not silence, and not out of range: this is what says the bytes went
        // through a decoder rather than straight into the buffer.
        auto const loudest(peak(sample.channel1()));
        CHECK(loudest > 0.001f);
        CHECK(loudest < 1.5f);
    }
}

TEST_CASE("The requested sample rate decides the length", "[external-audio]")
{
    auto const samples(Sample::factorySamples());
    REQUIRE(!samples.empty());
    auto const &file(samples.front());

    Sample at44k;
    Sample at88k;
    REQUIRE(at44k.load(file, 44100) == nullptr);
    REQUIRE(at88k.load(file, 88200) == nullptr);

    // Twice the rate, twice the frames, to within the frame the ratio rounds
    // off -- the same audio either way.
    auto const ratio(static_cast<double>(at88k.channel1().size()) /
                     static_cast<double>(at44k.channel1().size()));
    CHECK(ratio == Catch::Approx(2.0).epsilon(0.001));
}

TEST_CASE("A sample loaded with no rate keeps the file's own", "[external-audio]")
{
    auto const samples(Sample::factorySamples());
    REQUIRE(!samples.empty());
    auto const &file(samples.front());

    /// \note What a session restored before activate() does; the plugin then
    /// re-reads it once the host says what rate it wants, and sampleRate() being
    /// zero is how it knows to. See SpectrumWorxCLAP::activate().
    Sample sample;
    REQUIRE(sample.load(file, 0) == nullptr);
    CHECK(sample.sampleRate() == 0);
    CHECK(!sample.channel1().empty());
}

TEST_CASE("Clearing a sample forgets the file", "[external-audio]")
{
    auto const samples(Sample::factorySamples());
    REQUIRE(!samples.empty());

    Sample sample;
    REQUIRE(sample.load(samples.front(), 48000) == nullptr);
    REQUIRE(static_cast<bool>(sample));

    sample.clear();

    CHECK(!sample);
    CHECK(sample.sampleFile().empty());
    CHECK(sample.sampleRate() == 0);
}

TEST_CASE("The supported formats wildcard names what can be loaded", "[external-audio]")
{
    auto const wildcards(Sample::supportedFormats());

    UNSCOPED_INFO(wildcards);
    // The factory samples are MP3, so a build whose format manager cannot say
    // so cannot open its own content.
    CHECK(wildcards.containsIgnoreCase("*.mp3"));
    CHECK(wildcards.containsIgnoreCase("*.wav"));
}

////////////////////////////////////////////////////////////////////////////////
//
// Headers that describe audio nobody has
// --------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note Every number `doLoad()` works from comes out of a file header, and the
/// only bound any of them had was "not zero" plus a hundred-million-frame guard
/// on the length *before* resampling.
///
///   What is worth knowing about this surface, because it is not what the review
/// that prompted it assumed:
///
///   - **The channel count is the one that mattered.** `juce::AudioBuffer` was
///     built with `reader->numChannels` and only ever read through channels 0
///     and 1. A plain WAV cannot make that enormous -- its data chunk length is
///     32 bits, so channels x frames x 2 is capped at 4 GiB -- but RF64 carries
///     a 64-bit length, and a hundred million frames against a thousand declared
///     channels is a 400 GB allocation.
///
///   - **The AIFF denormal rate does not exist.** The review suspected AIFF's
///     80-bit extended rate could arrive as 1e-300 and drive the resampling
///     ratio to zero. JUCE's reader refuses any exponent byte other than 0x40
///     (`juce_AiffAudioFormat.cpp:452-455`), so the smallest rate it will report
///     is about 2 Hz, and it truncates the result to an `int`. The rate bound is
///     kept as a guard on a number from a file, not as a fix for a live hole.
///
///   - **The reachable one is legitimate.** The length guard looked at the file
///     before resampling, so an 8 kHz file against a 768 kHz engine -- ninety-six
///     times as many frames -- got past it and asked the allocator for the
///     result.
///
/// \note So what these cases pin is behaviour, not allocation sizes: a wide file
/// still yields its first two channels, and a resample that would run past the
/// bound is refused rather than attempted. The allocation the channel bound
/// saves is real and is not observable from here, which is said plainly rather
/// than dressed up in a case that would pass either way.
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
void appendLittleEndian(std::vector<char> &bytes, std::uint32_t const value, unsigned const width)
{
    for (unsigned byte(0); byte < width; ++byte)
        bytes.push_back(static_cast<char>((value >> (8 * byte)) & 0xFF));
}

void appendTag(std::vector<char> &bytes, char const *const tag)
{
    for (unsigned index(0); index < 4; ++index)
        bytes.push_back(tag[index]);
}

/// \brief A 16-bit PCM WAV of \p frames frames, whose header says whatever it is
/// told to. \p rampChannels writes a different constant into each channel, so
/// that which channel ended up where is readable afterwards.
std::vector<char> wavFile(unsigned const channels, std::uint32_t const sampleRate,
                          unsigned const frames)
{
    constexpr std::uint16_t bitsPerSample{16};
    auto const blockAlign(static_cast<std::uint16_t>(channels * bitsPerSample / 8));
    auto const dataBytes(frames * blockAlign);

    std::vector<char> bytes;
    appendTag(bytes, "RIFF");
    appendLittleEndian(bytes, 36 + dataBytes, 4);
    appendTag(bytes, "WAVE");

    appendTag(bytes, "fmt ");
    appendLittleEndian(bytes, 16, 4); // chunk size
    appendLittleEndian(bytes, 1, 4);  // PCM, and the channel count beside it
    bytes.pop_back();
    bytes.pop_back();
    appendLittleEndian(bytes, channels, 2);
    appendLittleEndian(bytes, sampleRate, 4);
    appendLittleEndian(bytes, sampleRate * blockAlign, 4); // byte rate
    appendLittleEndian(bytes, blockAlign, 2);
    appendLittleEndian(bytes, bitsPerSample, 2);

    appendTag(bytes, "data");
    appendLittleEndian(bytes, dataBytes, 4);

    /// A different level per channel: channel n sits at (n + 1) / 16 of full
    /// scale, which stays well inside the range and is exactly recoverable.
    for (unsigned frame(0); frame < frames; ++frame)
        for (unsigned channel(0); channel < channels; ++channel)
            appendLittleEndian(
                bytes, static_cast<std::uint32_t>((channel + 1) * (32768 / 16)) & 0xFFFF, 2);

    return bytes;
}

/// \note `<fstream>` and `create_directories`, where this was
/// `juce::File::createDirectory()` and `replaceWithData()`. The same shape lives
/// in tests/presets/presetFileTests.cpp as `outputDirectory()`/`fileHolding()`;
/// the two are in different test binaries -- sw-plugin-tests and sw-dsp-tests --
/// so the idiom is shared rather than the symbol.
fs::path fileHolding(std::string const &name, std::vector<char> const &bytes)
{
    fs::path const directory(SW_TEST_OUTPUT_DIR);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    REQUIRE(std::filesystem::is_directory(directory, error));

    auto const file(directory / name);
    {
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        REQUIRE(stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size())));
    }
    return file;
}
} // anonymous namespace

TEST_CASE("A hand-built WAV loads the way a factory sample does", "[external-audio][hostile]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note First, because without it the cases below would all pass against a
    /// reader that simply refused everything the helper writes.
    ///
    ////////////////////////////////////////////////////////////////////////////
    constexpr unsigned frames{1024};
    auto const file(fileHolding("ordinary.wav", wavFile(2, 44100, frames)));

    Sample sample;
    REQUIRE(sample.load(file, 44100) == nullptr);
    CHECK(sample.channel1().size() == frames);
    CHECK(sample.channel2().size() == frames);

    // The levels the helper wrote, which is what says the channels are the right
    // way round rather than merely present.
    CHECK(sample.channel1()[0] == Catch::Approx(1.0 / 16).epsilon(0.001));
    CHECK(sample.channel2()[0] == Catch::Approx(2.0 / 16).epsilon(0.001));

    Sample doubled;
    REQUIRE(doubled.load(file, 88200) == nullptr);
    CHECK(static_cast<double>(doubled.channel1().size()) ==
          Catch::Approx(2.0 * frames).epsilon(0.01));
}

TEST_CASE("A file wider than two channels yields its first two", "[external-audio][hostile]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The contract the channel bound must not have broken. Only the first
    /// two channels are ever read, so only the first two are decoded now -- and
    /// this is what says the two that come out are still 0 and 1 rather than
    /// whatever a narrower buffer happened to catch.
    ///
    ////////////////////////////////////////////////////////////////////////////
    constexpr unsigned frames{512};
    auto const file(fileHolding("ten channels.wav", wavFile(10, 44100, frames)));

    Sample sample;
    REQUIRE(sample.load(file, 44100) == nullptr);

    CHECK(sample.channel1().size() == frames);
    CHECK(sample.channel1().size() == sample.channel2().size());
    CHECK(sample.channel1()[0] == Catch::Approx(1.0 / 16).epsilon(0.001));
    CHECK(sample.channel2()[0] == Catch::Approx(2.0 / 16).epsilon(0.001));
}

TEST_CASE("A mono file is still duplicated into both channels", "[external-audio][hostile]")
{
    // The other side of the same clamp: one channel in, two equal ones out.
    constexpr unsigned frames{512};
    auto const file(fileHolding("mono.wav", wavFile(1, 44100, frames)));

    Sample sample;
    REQUIRE(sample.load(file, 44100) == nullptr);

    REQUIRE(sample.channel1().size() == frames);
    REQUIRE(sample.channel2().size() == frames);
    CHECK(sample.channel1()[0] == Catch::Approx(1.0 / 16).epsilon(0.001));
    CHECK(sample.channel2()[0] == sample.channel1()[0]);
}

TEST_CASE("A resample that would run past the length guard is refused", "[external-audio][hostile]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The guard used to be on the file's own length, so this got past it:
    /// one hertz against forty-eight kilohertz is forty-eight thousand times as
    /// many frames, and the refusal came from the allocator rather than from a
    /// bound. Two channels of 32-bit float at that length is 38 TB.
    ///
    ///   A legitimate file reaches the same place more modestly -- an 8 kHz
    /// recording against a 768 kHz engine is ninety-six times its own length.
    ///
    ////////////////////////////////////////////////////////////////////////////
    auto const file(fileHolding("one hertz.wav", wavFile(2, 1, 8192)));

    Sample sample;
    auto const *const error(sample.load(file, 48000));

    REQUIRE(error != nullptr);
    CHECK(!sample);
}
