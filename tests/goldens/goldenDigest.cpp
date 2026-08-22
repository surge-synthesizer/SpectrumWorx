////////////////////////////////////////////////////////////////////////////////
///
/// goldenDigest.cpp
/// ----------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "goldenDigest.hpp"

// For LE_ACC_FFT / LE_PFFFT: which FFT backend is under the render is part of
// what makes a fixture file's bit-exact hashes meaningful or not.
#include "le/math/dft/fft.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
// \note std::numbers::pi rather than M_PI, which is a POSIX extension MSVC only
// defines under _USE_MATH_DEFINES.
#include <numbers>
#include <sstream>
#include <string>

namespace SWTest
{

namespace
{
constexpr float silenceFloor{-200.0f}; ///< dB reported for an empty band

std::uint64_t fnv1a(std::span<float const> const samples)
{
    // Over the raw bits, so a change of one ulp anywhere is a different hash.
    std::uint64_t hash{0xCBF29CE484222325ull};
    for (auto const sample : samples)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &sample, sizeof(bits));
        // -0.0 and +0.0 compare equal but hash differently; normalise, because
        // which one a denormal-flushing branch produces is not interesting.
        if (bits == 0x80000000u)
            bits = 0;
        for (unsigned int byte(0); byte < sizeof(bits); ++byte)
        {
            hash ^= (bits >> (byte * 8)) & 0xFFu;
            hash *= 0x100000001B3ull;
        }
    }
    return hash;
}

float dB(double const amplitude)
{
    return (amplitude > 0) ? static_cast<float>(20 * std::log10(amplitude)) : silenceFloor;
}
} // anonymous namespace

Digest Digest::of(std::span<float const> const interleaved, std::uint8_t const channels,
                  float const sampleRate)
{
    Digest digest{};
    digest.hash = fnv1a(interleaved);

    double sumOfSquares{0};
    double sum{0};
    float peak{0};
    std::uint32_t nonFinite{0};
    for (auto const sample : interleaved)
    {
        if (!std::isfinite(sample))
        {
            ++nonFinite;
            continue;
        }
        peak = std::max(peak, std::abs(sample));
        sum += sample;
        sumOfSquares += static_cast<double>(sample) * sample;
    }
    auto const count(interleaved.size() ? interleaved.size() : 1);
    digest.peak = peak;
    digest.rms = static_cast<float>(std::sqrt(sumOfSquares / count));
    digest.dcOffset = static_cast<float>(sum / count);
    digest.nonFiniteSamples = nonFinite;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// Band energies, from a plain DFT of the first channel over a fixed 1024
    /// point window. Fixed size and fixed grid, so the summary does not move
    /// when the render length does.
    ///
    /// \note **The windows overlap by half, and that is not a refinement.** They
    /// used to be laid end to end, and a Hann window is zero at both ends -- so
    /// the analysis was blind at every 1024-sample boundary and anything short
    /// landing there contributed nothing to any band.
    ///
    ///   It bit when #83 moved the engine's delay to `fftSize`: at 2048/8 the
    /// golden impulse's output landed on 7168, which is 7 x 1024 exactly, and the
    /// band columns of every impulse row at that configuration collapsed to the
    /// silence floor. What they then reported was numerical residue rather than
    /// signal -- and residue is exactly the thing that does not agree between
    /// Accelerate and pffft, so `Octaver/impulse/2048/8` drifted 16 dB in band 2
    /// against an 8 dB bound and CI went red on Linux and Windows while macOS,
    /// which minted the file, stayed green.
    ///
    ///   At a hop of half a window Hann satisfies COLA: every sample is covered
    /// with a total weight of 1, and a transient cannot hide between windows.
    ///
    ////////////////////////////////////////////////////////////////////////////
    constexpr std::size_t window{1024};
    constexpr std::size_t hop{window / 2};
    auto const frames(interleaved.size() / std::max<std::uint8_t>(channels, 1));
    std::vector<double> magnitude(window / 2 + 1, 0.0);
    if (frames >= window)
    {
        // Average the magnitude spectrum over as many windows as fit, which is
        // far more stable across platforms than a single one.
        std::size_t windows{0};
        for (std::size_t start(0); start + window <= frames; start += hop)
        {
            for (std::size_t bin(0); bin < magnitude.size(); ++bin)
            {
                double real{0}, imaginary{0};
                for (std::size_t n(0); n < window; ++n)
                {
                    auto const sample(interleaved[(start + n) * channels]);
                    if (!std::isfinite(sample))
                        continue;
                    // Hann, to keep leakage from smearing the band edges.
                    auto const w(0.5 * (1 - std::cos(2 * std::numbers::pi * n / (window - 1))));
                    auto const angle(-2 * std::numbers::pi * static_cast<double>(bin) * n / window);
                    real += sample * w * std::cos(angle);
                    imaginary += sample * w * std::sin(angle);
                }
                magnitude[bin] += std::sqrt(real * real + imaginary * imaginary);
            }
            ++windows;
        }
        if (windows)
            for (auto &value : magnitude)
                value /= windows;
    }

    // Eight log-spaced bands from 20 Hz to Nyquist.
    auto const binWidth(sampleRate / window);
    auto const lowest(20.0);
    auto const highest(static_cast<double>(sampleRate) / 2);
    auto const ratio(std::pow(highest / lowest, 1.0 / numberOfBands));
    auto edge(lowest);
    for (unsigned int band(0); band < numberOfBands; ++band)
    {
        auto const next(edge * ratio);
        auto const firstBin(static_cast<std::size_t>(edge / binWidth));
        auto const lastBin(
            std::min(magnitude.size() - 1, static_cast<std::size_t>(next / binWidth)));
        double energy{0};
        std::size_t bins{0};
        for (auto bin(firstBin); bin <= lastBin; ++bin, ++bins)
            energy += magnitude[bin] * magnitude[bin];
        digest.bands[band] = dB(bins ? std::sqrt(energy / bins) : 0.0);
        edge = next;
    }
    return digest;
}

std::string Fixture::serialise() const
{
    char buffer[512];
    int written(std::snprintf(buffer, sizeof(buffer), "%-44s %016llx %.9g %.9g %.9g %u",
                              key.c_str(), static_cast<unsigned long long>(digest.hash),
                              static_cast<double>(digest.peak), static_cast<double>(digest.rms),
                              static_cast<double>(digest.dcOffset), digest.nonFiniteSamples));
    for (auto const band : digest.bands)
        written += std::snprintf(buffer + written, sizeof(buffer) - written, " %.4f",
                                 static_cast<double>(band));
    return buffer;
}

Fixture Fixture::parse(std::string const &line)
{
    Fixture fixture{};
    std::istringstream stream(line);
    std::string hash;
    stream >> fixture.key >> hash >> fixture.digest.peak >> fixture.digest.rms >>
        fixture.digest.dcOffset >> fixture.digest.nonFiniteSamples;
    fixture.digest.hash = std::stoull(hash, nullptr, 16);
    for (auto &band : fixture.digest.bands)
        stream >> band;
    return fixture;
}

////////////////////////////////////////////////////////////////////////////////
// The fixture file
////////////////////////////////////////////////////////////////////////////////

namespace
{
constexpr char provenanceMarker[]{"# provenance "};
constexpr std::size_t provenanceMarkerLength{sizeof(provenanceMarker) - 1};
} // anonymous namespace

bool FixtureFile::mintedByThisBuild() const { return provenance == SWTest::provenance(); }

FixtureFile readFixtures(std::string const &path)
{
    FixtureFile file;
    std::ifstream stream(path);
    std::string line;
    while (std::getline(stream, line))
    {
        if (line.compare(0, provenanceMarkerLength, provenanceMarker) == 0)
        {
            file.provenance = line.substr(provenanceMarkerLength);
            continue;
        }
        if (line.empty() || (line.front() == '#'))
            continue;
        auto const fixture(Fixture::parse(line));
        file.fixtures.emplace(fixture.key, fixture.digest);
    }
    return file;
}

void writeFixtures(std::string const &path, std::span<Fixture const> const fixtures,
                   std::span<char const *const> const preamble)
{
    std::ofstream file(path, std::ios::trunc);
    for (auto const *const line : preamble)
        file << "# " << line << '\n';
    file << provenanceMarker << provenance() << '\n';
    for (auto const &fixture : fixtures)
        file << fixture.serialise() << '\n';
}

bool goldenUpdateRequested()
{
    auto const *const value(std::getenv("SW_GOLDEN_UPDATE"));
    return value && (std::string(value) != "0");
}

std::string provenance()
{
#if defined(__APPLE__)
    char const *const os{"macos"};
#elif defined(_WIN32)
    char const *const os{"windows"};
#elif defined(__linux__)
    char const *const os{"linux"};
#else
    char const *const os{"unknown-os"};
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    char const *const architecture{"arm64"};
#elif defined(__x86_64__) || defined(_M_X64)
    char const *const architecture{"x86_64"};
#else
    char const *const architecture{"unknown-arch"};
#endif

#if defined(LE_ACC_FFT)
    char const *const fft{"accelerate"};
#elif defined(LE_PFFFT)
    char const *const fft{"pffft"};
#else
    char const *const fft{"unknown-fft"};
#endif

    return std::string(os) + "-" + architecture + "/" + fft;
}

namespace
{
/// \note The numbers are stage 4.4's answer, and they are measurements rather
/// than preferences: every one of them is the smallest round value that the 464
/// fixtures of macOS/Accelerate against Linux/pffft actually fit inside, with the
/// nine amplifying effects held separately. See the table under
/// "The cross-platform contract, measured" in the plan.
constexpr float audibilityFloor{-60.0f}; // dB, on Digest::of's own scale

float relative(float const expected, float const got)
{
    auto const scale(std::max({std::abs(expected), std::abs(got), 1e-6f}));
    return std::abs(expected - got) / scale;
}
} // anonymous namespace

float Deltas::worst() const { return std::max(peak, rms); }

float bandAudibilityFloor() { return audibilityFloor; }

Tolerances Tolerances::strict()
{
    // peak looser than rms deliberately -- see the note on the struct.
    return Tolerances{1e-3f, 1e-4f, 1e-4f, 0.1f};
}

Tolerances Tolerances::amplified()
{
    /// Measured worst over the nine, at the floor above: peak 0.208 (Pitch
    /// Spring on the sweep), rms 0.107 (Octaver on the impulse), dc 9.9e-4,
    /// band 5.8 dB. Roughly 1.5x headroom on each, which still fails on the
    /// things that would matter -- silence and a doubled gain both read as a
    /// relative difference near 1, and a genuinely different spectrum moves a
    /// band by far more than 8 dB.
    return Tolerances{0.35f, 0.20f, 5e-3f, 8.0f};
}

Tolerances Tolerances::sameBuildOnly()
{
    // Infinities rather than a large number, so that nothing here reads as a
    // bound somebody measured. compare() still enforces the non-finite count
    // and, on the minting machine, the hash.
    auto const unbounded(std::numeric_limits<float>::infinity());
    return Tolerances{unbounded, unbounded, unbounded, unbounded};
}

bool withinTolerance(Deltas const &measured, Tolerances const &tolerances)
{
    return !measured.nonFiniteDiffers && (measured.peak <= tolerances.peak) &&
           (measured.rms <= tolerances.rms) && (measured.dcOffset <= tolerances.dcOffset) &&
           (measured.band <= tolerances.band);
}

Deltas deltas(Digest const &golden, Digest const &actual)
{
    Deltas measured;
    measured.nonFiniteDiffers = (golden.nonFiniteSamples != actual.nonFiniteSamples);
    measured.peak = relative(golden.peak, actual.peak);
    measured.rms = relative(golden.rms, actual.rms);
    // dcOffset is an absolute quantity near zero, so a relative test is
    // meaningless for it; compare against the render's own scale.
    measured.dcOffset = std::abs(golden.dcOffset - actual.dcOffset) / std::max(golden.rms, 1e-6f);

    for (unsigned int band(0); band < numberOfBands; ++band)
    {
        /// \note A band inaudible on *both* sides is not compared. This used to
        /// read `<= silenceFloor + 1`, i.e. -199 dB, which meant two bands 120 dB
        /// under the signal were still held to 0.01 dB of each other -- and that
        /// is measuring rounding noise, not audio. It accounted for 58 of the 89
        /// cross-platform failures on its own, including the largest "drift" in
        /// the whole matrix: -189 dB against -180 dB.
        ///
        ///   Raising the floor to something audible is a scoping fix rather than
        /// a loosening; nothing that can be heard changes hands. Both sides have
        /// to be below it, so an effect that goes quiet where the golden is loud
        /// still fails.
        if ((golden.bands[band] < audibilityFloor) && (actual.bands[band] < audibilityFloor))
        {
            ++measured.bandsSkipped;
            continue;
        }
        auto const difference(std::abs(golden.bands[band] - actual.bands[band]));
        if (difference > measured.band)
        {
            measured.band = difference;
            measured.worstBand = band;
        }
    }
    return measured;
}

Comparison compare(Digest const &golden, Digest const &actual, bool const exact,
                   Tolerances const &tolerances)
{
    auto const fail([](std::string reason) { return Comparison{false, std::move(reason)}; });
    auto const number([](float const value) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.4g", static_cast<double>(value));
        return std::string(buffer);
    });

    auto const measured(deltas(golden, actual));

    if (measured.nonFiniteDiffers)
        return fail("non-finite sample count " + std::to_string(actual.nonFiniteSamples) +
                    ", expected " + std::to_string(golden.nonFiniteSamples));

    if (exact && (actual.hash != golden.hash))
        return fail("bit-exact hash mismatch");

    // Each says the measured distance and the bound it broke, so a failure can be
    // read without also running the drift report.
    if (measured.peak > tolerances.peak)
        return fail("peak differs by " + number(measured.peak) + " relative, over " +
                    number(tolerances.peak) + " (" + number(actual.peak) + " against " +
                    number(golden.peak) + ")");
    if (measured.rms > tolerances.rms)
        return fail("rms differs by " + number(measured.rms) + " relative, over " +
                    number(tolerances.rms) + " (" + number(actual.rms) + " against " +
                    number(golden.rms) + ")");
    if (measured.dcOffset > tolerances.dcOffset)
        return fail("dc offset differs by " + number(measured.dcOffset) + " of rms, over " +
                    number(tolerances.dcOffset) + " (" + number(actual.dcOffset) + " against " +
                    number(golden.dcOffset) + ")");
    if (measured.band > tolerances.band)
        return fail("band " + std::to_string(measured.worstBand) + " differs by " +
                    number(measured.band) + " dB, over " + number(tolerances.band) + " (" +
                    number(actual.bands[measured.worstBand]) + " dB against " +
                    number(golden.bands[measured.worstBand]) + " dB)");

    return Comparison{true, {}};
}

} // namespace SWTest
