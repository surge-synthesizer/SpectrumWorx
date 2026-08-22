////////////////////////////////////////////////////////////////////////////////
///
/// fftTests.cpp
/// ------------
///
///   The real FFT the golden fixtures in 3.6 sit on top of: Accelerate/vDSP on
/// Apple, pffft everywhere else. Everything here is a property of the transform
/// rather than a captured output, so it holds for both backends -- and the
/// [backend] cases at the bottom are what make that claim checkable rather than
/// hopeful.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "le/math/constants.hpp"
#include "le/math/dft/fft.hpp"
#include "le/spectrumworx/engine/buffers.hpp"
#include "le/utility/buffers.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
// \note std::numbers::pi, not M_PI and not LE::Math::pi_d. M_PI is a POSIX
// extension MSVC only defines under _USE_MATH_DEFINES, and the engine's own
// constant is what the reference below is checking the engine against.
#include <numbers>
#include <vector>
//------------------------------------------------------------------------------

using Catch::Approx;
namespace Engine = LE::SW::Engine;
namespace Math = LE::Math;

namespace
{
/// Owns the shared storage the FFT positions itself in.
class FFTFixture
{
  public:
    explicit FFTFixture(std::uint16_t const fftSize)
    {
        Engine::StorageFactors const factors{fftSize, 2, 1, 44100};
        auto const required(Math::FFT_float_real_1D::requiredStorage(factors));
        REQUIRE(storage_.resize(required + LE::Utility::Constants::vectorAlignment));
        Engine::Storage span(storage_.begin(), storage_.end());
        fft_.resize(factors, span);
        REQUIRE(fft_.size() == fftSize);
    }

    Math::FFT_float_real_1D const &fft() const { return fft_; }

  private:
    LE::Utility::AlignedHeapBuffer<char> storage_;
    Math::FFT_float_real_1D fft_;
};

constexpr std::uint16_t fftSize{256};

/// The transform the two backends both have to implement, in double precision:
/// the textbook DFT divided by sqrt(N).
///
/// \note This is the reference the whole stage 4 backend swap is graded against.
/// Accelerate and pffft each get there by a different route -- vDSP's real
/// forward transform carries a factor of two that pffft's does not, and the
/// packed layouts they produce are not the same -- so an identity test alone
/// cannot tell a correct unpacking from a self-consistently wrong one.
void referenceSpectrum(float const *const time, std::uint16_t const size,
                       std::vector<double> &reals, std::vector<double> &imaginaries)
{
    auto const bins(size / 2 + 1);
    reals.assign(bins, 0.0);
    imaginaries.assign(bins, 0.0);
    auto const normalisation(1 / std::sqrt(static_cast<double>(size)));
    for (std::uint16_t bin(0); bin < bins; ++bin)
    {
        double real{0}, imaginary{0};
        for (std::uint16_t n(0); n < size; ++n)
        {
            auto const angle(-2 * std::numbers::pi * bin * n / size);
            real += time[n] * std::cos(angle);
            imaginary += time[n] * std::sin(angle);
        }
        reals[bin] = real * normalisation;
        imaginaries[bin] = imaginary * normalisation;
    }
}

/// Something with content in every bin, so no comparison is against a zero.
void fillTestSignal(float *const time, std::uint16_t const size)
{
    for (std::uint16_t n(0); n < size; ++n)
    {
        auto const t(static_cast<double>(n) / size);
        time[n] = static_cast<float>(std::sin(2 * std::numbers::pi * 3 * t) +
                                     0.5 * std::cos(2 * std::numbers::pi * 29 * t) +
                                     0.25 * std::sin(2 * std::numbers::pi * 71 * t + 0.4) +
                                     0.125 * std::cos(2 * std::numbers::pi * (size / 2) * t) + 0.1);
    }
}
} // anonymous namespace

TEST_CASE("A forward then inverse transform is the identity", "[math][fft]")
{
    FFTFixture const fixture(fftSize);

    LE::Utility::AlignedHeapBuffer<float> time;
    REQUIRE(time.resize(fftSize));
    LE::Utility::AlignedHeapBuffer<float> imaginary;
    REQUIRE(imaginary.resize(fftSize / 2 + 1));

    LE::Utility::AlignedHeapBuffer<float> original;
    REQUIRE(original.resize(fftSize));
    for (std::uint16_t index(0); index < fftSize; ++index)
    {
        auto const t(static_cast<float>(index) / fftSize);
        original[index] = std::sin(2 * Math::Constants::pi * 5 * t) +
                          0.25f * std::cos(2 * Math::Constants::pi * 17 * t);
        time[index] = original[index];
    }

    fixture.fft().transform(time.begin(), imaginary.begin(), fftSize);
    fixture.fft().inverseTransform(time.begin(), imaginary.begin(), fftSize);

    for (std::uint16_t index(0); index < fftSize; ++index)
        CHECK(time[index] == Approx(original[index]).margin(1e-3));
}

TEST_CASE("A pure tone at a bin centre lands in that bin", "[math][fft]")
{
    FFTFixture const fixture(fftSize);
    constexpr std::uint16_t bin{20};

    LE::Utility::AlignedHeapBuffer<float> reals;
    REQUIRE(reals.resize(fftSize));
    LE::Utility::AlignedHeapBuffer<float> imags;
    REQUIRE(imags.resize(fftSize / 2 + 1));

    for (std::uint16_t index(0); index < fftSize; ++index)
        reals[index] = std::cos(2 * Math::Constants::pi * bin * index / fftSize);

    fixture.fft().transform(reals.begin(), imags.begin(), fftSize);

    LE::Utility::AlignedHeapBuffer<float> amplitudes;
    REQUIRE(amplitudes.resize(fftSize / 2 + 1));
    Math::amplitudes(reals.begin(), imags.begin(), amplitudes.begin(), amplitudes.end());

    auto const peak(std::max_element(amplitudes.begin(), amplitudes.end()));
    CHECK((peak - amplitudes.begin()) == bin);

    // and nothing else is anywhere near it
    auto const peakValue(*peak);
    for (std::uint16_t index(0); index < amplitudes.size(); ++index)
        if ((index < bin - 1) || (index > bin + 1))
            CHECK(amplitudes[index] < peakValue * 0.01f);
}

TEST_CASE("A constant signal is entirely DC", "[math][fft]")
{
    FFTFixture const fixture(fftSize);

    LE::Utility::AlignedHeapBuffer<float> reals;
    REQUIRE(reals.resize(fftSize));
    LE::Utility::AlignedHeapBuffer<float> imags;
    REQUIRE(imags.resize(fftSize / 2 + 1));
    std::fill(reals.begin(), reals.end(), 1.0f);

    fixture.fft().transform(reals.begin(), imags.begin(), fftSize);

    LE::Utility::AlignedHeapBuffer<float> amplitudes;
    REQUIRE(amplitudes.resize(fftSize / 2 + 1));
    Math::amplitudes(reals.begin(), imags.begin(), amplitudes.begin(), amplitudes.end());

    CHECK(amplitudes[0] > 0);
    for (std::uint16_t index(1); index < amplitudes.size(); ++index)
        CHECK(amplitudes[index] == Approx(0.0f).margin(amplitudes[0] * 1e-4f));
}

TEST_CASE("The transform is linear", "[math][fft]")
{
    FFTFixture const fixture(fftSize);
    auto const transform([&](float const scale, LE::Utility::AlignedHeapBuffer<float> &reals,
                             LE::Utility::AlignedHeapBuffer<float> &imags) {
        REQUIRE(reals.resize(fftSize));
        REQUIRE(imags.resize(fftSize / 2 + 1));
        for (std::uint16_t index(0); index < fftSize; ++index)
            reals[index] = scale * std::sin(2 * Math::Constants::pi * 9 * index / fftSize);
        fixture.fft().transform(reals.begin(), imags.begin(), fftSize);
    });

    LE::Utility::AlignedHeapBuffer<float> singleReals, singleImags, doubleReals, doubleImags;
    transform(1.0f, singleReals, singleImags);
    transform(2.0f, doubleReals, doubleImags);

    for (std::uint16_t index(0); index < fftSize / 2 + 1; ++index)
    {
        CHECK(doubleReals[index] == Approx(2 * singleReals[index]).margin(1e-3));
        CHECK(doubleImags[index] == Approx(2 * singleImags[index]).margin(1e-3));
    }
}

////////////////////////////////////////////////////////////////////////////////
/// The backend contract
///
///   Everything above is a property that any sane FFT has. The three cases below
/// are the ones that pin the *backend*: what the transform is normalised to, how
/// its output is laid out, and how much error it carries. They are what makes a
/// golden difference attributable -- if these hold, a difference downstream is
/// the effect amplifying an ulp, not the FFT being wrong.
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("The forward transform is the DFT over sqrt(N), bin for bin", "[math][fft][backend]")
{
    // Every size the engine can ask for, at both ends and in between: a pffft
    // setup is per size, and vDSP's is per log2(size).
    for (std::uint16_t const size : {128, 256, 512, 2048, 8192})
    {
        FFTFixture const fixture(size);

        LE::Utility::AlignedHeapBuffer<float> time;
        REQUIRE(time.resize(size));
        LE::Utility::AlignedHeapBuffer<float> imaginary;
        REQUIRE(imaginary.resize(size / 2 + 1));
        fillTestSignal(time.begin(), size);

        std::vector<double> referenceReals, referenceImaginaries;
        referenceSpectrum(time.begin(), size, referenceReals, referenceImaginaries);

        fixture.fft().transform(time.begin(), imaginary.begin(), size);

        // Relative to the magnitude of the whole spectrum rather than to each
        // bin: a bin whose reference value is near zero cannot carry a
        // meaningful relative error, and the absolute error there is what
        // matters anyway.
        double scale{0};
        for (std::uint16_t bin(0); bin <= size / 2; ++bin)
            scale = std::max(scale, std::hypot(referenceReals[bin], referenceImaginaries[bin]));
        REQUIRE(scale > 0);

        double worst{0};
        for (std::uint16_t bin(0); bin <= size / 2; ++bin)
        {
            worst = std::max(worst, std::abs(time[bin] - referenceReals[bin]) / scale);
            worst = std::max(worst, std::abs(imaginary[bin] - referenceImaginaries[bin]) / scale);
        }
        INFO("fft size " << size << ", worst error " << worst << " of full scale");
        // ~24 bits of mantissa, spread over log2(size) butterfly stages. 1e-6 is
        // roughly an order of magnitude of headroom over what either backend
        // actually delivers; it is here to catch a wrong normalisation or a
        // mis-unpacked layout, not to grade the last ulp.
        CHECK(worst < 1e-6);
    }
}

TEST_CASE("The spectrum layout is the one the engine reads", "[math][fft][backend]")
{
    // DC and Nyquist are real by construction and the engine reads both of those
    // imaginary slots, so they have to be an exact zero rather than a small one.
    // Nyquist's real part lives at the end of the reals, past the last complex
    // bin -- which is where the packed layouts of both backends have to be
    // unpacked to.
    for (std::uint16_t const size : {128, 512, 2048})
    {
        FFTFixture const fixture(size);

        LE::Utility::AlignedHeapBuffer<float> time;
        REQUIRE(time.resize(size));
        LE::Utility::AlignedHeapBuffer<float> imaginary;
        REQUIRE(imaginary.resize(size / 2 + 1));

        // A Nyquist-frequency cosine: all the energy belongs in the last real.
        for (std::uint16_t n(0); n < size; ++n)
            time[n] = (n % 2) ? -1.0f : 1.0f;

        fixture.fft().transform(time.begin(), imaginary.begin(), size);

        INFO("fft size " << size);
        CHECK(imaginary[0] == 0.0f);
        CHECK(imaginary[size / 2] == 0.0f);
        // sum of N unit-magnitude terms, over sqrt(N)
        CHECK(time[size / 2] == Approx(std::sqrt(static_cast<double>(size))).epsilon(1e-5));
        CHECK(time[0] == Approx(0.0f).margin(std::sqrt(static_cast<double>(size)) * 1e-6));
    }
}

TEST_CASE("A round trip is the identity to float precision", "[math][fft][backend]")
{
    // The existing identity case above allows 1e-3 absolute, which would not
    // notice a backend an order of magnitude worse than it should be.
    for (std::uint16_t const size : {128, 512, 2048, 8192})
    {
        FFTFixture const fixture(size);

        LE::Utility::AlignedHeapBuffer<float> time, original;
        REQUIRE(time.resize(size));
        REQUIRE(original.resize(size));
        LE::Utility::AlignedHeapBuffer<float> imaginary;
        REQUIRE(imaginary.resize(size / 2 + 1));
        fillTestSignal(time.begin(), size);
        std::copy(time.begin(), time.end(), original.begin());

        fixture.fft().transform(time.begin(), imaginary.begin(), size);
        fixture.fft().inverseTransform(time.begin(), imaginary.begin(), size);

        float peak{0};
        for (std::uint16_t n(0); n < size; ++n)
            peak = std::max(peak, std::abs(original[n]));

        double worst{0};
        for (std::uint16_t n(0); n < size; ++n)
            worst = std::max(worst, std::abs(static_cast<double>(time[n]) - original[n]) / peak);
        INFO("fft size " << size << ", worst round-trip error " << worst << " of peak");
        CHECK(worst < 1e-6);
    }
}
