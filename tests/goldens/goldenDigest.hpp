////////////////////////////////////////////////////////////////////////////////
///
/// \file goldenDigest.hpp
/// ----------------------
///
///   What a golden fixture actually stores.
///
///   A raw float dump of the whole matrix -- 57 effects x 2 FFT sizes x 2
/// overlap factors x 4 signals -- is ~30 MB before compression, which is a lot
/// of binary to put in a repository nobody can review. A digest is committed
/// instead: a bit-exact hash for same-platform regression, plus enough
/// numerical summary to compare across platforms within a tolerance, which is
/// what the plan asks the goldens to be able to do.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef goldenDigest_hpp__A7E4B93C_51D8_4A67_9F02_63C5D8E1B7A9
#define goldenDigest_hpp__A7E4B93C_51D8_4A67_9F02_63C5D8E1B7A9
//------------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace SWTest
{

/// Log-spaced, so the bands are musically rather than linearly meaningful and
/// a shift of one bin at the top does not swamp everything below it.
constexpr unsigned int numberOfBands{8};

struct Digest
{
    std::uint64_t hash;                     ///< FNV-1a over the raw bits: same-platform exactness
    float peak;                             ///< max |x|
    float rms;                              ///< whole-render RMS
    float dcOffset;                         ///< mean; catches sign and windowing errors
    std::array<float, numberOfBands> bands; ///< RMS per log-spaced band, in dB
    std::uint32_t nonFiniteSamples;         ///< must be zero

    static Digest of(std::span<float const> interleaved, std::uint8_t channels, float sampleRate);
}; // struct Digest

/// One row of the fixture file.
struct Fixture
{
    std::string key; ///< effect/signal/fft/overlap, and the row's identity
    Digest digest;

    std::string serialise() const;
    static Fixture parse(std::string const &line);
};

////////////////////////////////////////////////////////////////////////////////
///
/// \brief A whole fixture file: the rows, plus the one line in it that is not a
/// row.
///
/// \note Here rather than in goldenTests.cpp since 05.08.2026, when the
/// side-chain fixtures became a second file of the same shape. What the
/// provenance marker decides -- whether the bit-exact hash column is a contract
/// at all -- is a property of the format, so it belongs with the format.
///
////////////////////////////////////////////////////////////////////////////////

struct FixtureFile
{
    std::string provenance; ///< empty if the file predates the marker
    std::map<std::string, Digest> fixtures;

    /// Whether this build is the one that minted the file, and so whether the
    /// hashes are being checked.
    bool mintedByThisBuild() const;
}; // struct FixtureFile

FixtureFile readFixtures(std::string const &path);

/// \param preamble the explanatory "#" lines, written above the provenance
/// marker; each is emitted as given, prefixed with "# ".
void writeFixtures(std::string const &path, std::span<Fixture const> fixtures,
                   std::span<char const *const> preamble);

/// Whether SW_GOLDEN_UPDATE asked for the fixture files to be rewritten.
bool goldenUpdateRequested();

/// \brief What produced a fixture file, to the granularity that decides whether
/// its bit-exact hashes mean anything here.
///
///   OS, architecture and FFT backend. A compiler or libm change on one machine
/// can also move the last bit, so a matching provenance is a necessary and not a
/// sufficient condition for the hashes to agree -- but keying on the compiler
/// version too would mean the hash was effectively never checked, and its whole
/// value is catching a same-machine regression.
std::string provenance();

/// The measured distance between two digests, before any verdict is passed on
/// it. Separated out from compare() so a cross-platform run can rank and report
/// the drift rather than only announce the first field that exceeded a limit.
struct Deltas
{
    float peak{0};             ///< relative
    float rms{0};              ///< relative
    float dcOffset{0};         ///< absolute, against the render's own RMS
    float band{0};             ///< worst dB difference over the bands compared
    unsigned int worstBand{0}; ///< which band that was
    /// How many of the eight were below the audibility floor and so not compared
    /// at all. Reported rather than silent: a test should say what it ignored.
    unsigned int bandsSkipped{0};
    bool nonFiniteDiffers{false};

    /// For ranking: the amplitude fields only, since a dB difference between two
    /// quiet bands is not comparable to a relative error on a peak.
    float worst() const;
}; // struct Deltas

Deltas deltas(Digest const &golden, Digest const &actual);

/// \brief What counts as the same render on a machine that did not produce the
/// fixture file.
///
///   Two sets, because two populations. Stage 4.3 measured 464 fixtures of
/// macOS/Accelerate against Linux/pffft and found the median difference to be one
/// float ulp — but a handful of effects *make a decision*, and there one ulp
/// flips a comparison, the chosen bin moves, and the output moves by percent.
/// No single bound describes both: tight enough for the fifty is a guaranteed
/// failure on the nine, and loose enough for the nine is not a test.
///
/// \note `peak` is deliberately looser than `rms`. It is a single-sample
/// statistic — one sample landing the other side of a rounding step moves it —
/// whereas RMS is an average over the whole render and is the robust one. Nine
/// fixtures sat at 1.1e-4 on peak with their RMS at 4e-7.
struct Tolerances
{
    float peak;
    float rms;
    float dcOffset; ///< against the render's own RMS
    float band;     ///< dB

    /// The contract for an effect whose output is a continuous function of its
    /// input. 49 of the 58 chains, 392 fixtures, all inside this.
    static Tolerances strict();

    /// The contract for the nine that are not — see chaoticEffects() in
    /// goldenTests.cpp. Loose enough to absorb a moved bin, still tight enough
    /// that silence, a gross gain change or a different spectrum fails.
    static Tolerances amplified();
}; // struct Tolerances

bool withinTolerance(Deltas const &, Tolerances const &);

/// Bands quieter than this, on the digest's own dB scale, are not compared.
/// `Digest::of` reports a band that is empty as -200 dB, and comparing two
/// different flavours of inaudible is how the same-platform contract produced a
/// "9.6 dB drift" between -189 dB and -180 dB. Everything audible is far above
/// this; the loudest bands in the fixture set sit near +28 dB.
float bandAudibilityFloor();

/// \brief Cross-platform comparison.
///
/// The hash is checked only when \p exact -- it is the same-platform contract,
/// and any reassociation by a different compiler breaks it legitimately. The
/// numeric fields are always compared, in dB for the bands and relatively for
/// the rest.
struct Comparison
{
    bool matches;
    std::string explanation;
};

Comparison compare(Digest const &golden, Digest const &actual, bool exact, Tolerances const &);

} // namespace SWTest

#endif // goldenDigest_hpp
