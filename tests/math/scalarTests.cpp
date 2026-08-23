////////////////////////////////////////////////////////////////////////////////
///
/// scalarTests.cpp
/// ---------------
///
///   le/math's scalar routines. Weighted towards the ones stage 3 had to
/// disentangle: ln/log2/log10/exp/exp2 were defined twice or not at all once
/// NT2 became optional, and clamp()'s portable branch had never been compiled.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "le/math/constants.hpp"
#include "le/math/conversion.hpp"
#include "le/math/math.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
//------------------------------------------------------------------------------

using Catch::Approx;
namespace Math = LE::Math;

TEST_CASE("The transcendentals have exactly one definition and it is the right one",
          "[math][scalar]")
{
    CHECK(Math::ln(1.0f) == Approx(0.0f));
    CHECK(Math::ln(2.0f) == Approx(std::log(2.0f)));
    CHECK(Math::log2(8.0f) == Approx(3.0f));
    CHECK(Math::log2(0.5f) == Approx(-1.0f));
    CHECK(Math::log10(1000.0f) == Approx(3.0f));
    CHECK(Math::exp(0.0f) == Approx(1.0f));
    CHECK(Math::exp(1.0f) == Approx(std::exp(1.0f)));
    CHECK(Math::exp2(10.0f) == Approx(1024.0f));

    // exp2 is the one Xcode 6/7 miscompiled through NT2, which is why Apple
    // has always taken the CRT here.
    for (float exponent(-8); exponent <= 8; ++exponent)
        CHECK(Math::exp2(exponent) == Approx(std::exp2(exponent)));

    // \note And the same sweep for log2, which is the one that was actually
    // wrong: on MSVC it divided an already-base-two result by ln2 and so
    // returned 1/ln2 times the answer. The two point checks above caught it,
    // but only because 8 and 0.5 are far enough from 1 -- at a value near 1 the
    // error is a fraction of the tolerance, and a scale factor deserves to be
    // checked across a range rather than at a lucky point.
    for (float exponent(-8); exponent <= 8; ++exponent)
        CHECK(Math::log2(std::exp2(exponent)) == Approx(exponent).margin(1e-5f));
}

TEST_CASE("Integer log2 counts bits", "[math][scalar]")
{
    CHECK(Math::log2(1u) == 0);
    CHECK(Math::log2(2u) == 1);
    CHECK(Math::log2(1024u) == 10);
    CHECK(Math::log2(8192u) == 13);
}

TEST_CASE("clamp's portable branch works", "[math][scalar]")
{
    // Every previous build took an SSE or NEON branch; the #else called
    // unqualified min/max, which inside namespace LE::Math finds only the
    // float const * range overloads.
    CHECK(Math::clamp(0.5f, 0.0f, 1.0f) == Approx(0.5f));
    CHECK(Math::clamp(-1.0f, 0.0f, 1.0f) == Approx(0.0f));
    CHECK(Math::clamp(2.0f, 0.0f, 1.0f) == Approx(1.0f));
    CHECK(Math::clamp(-3.0f, -2.0f, 2.0f) == Approx(-2.0f));
}

TEST_CASE("Rounding matches the CRT", "[math][scalar]")
{
    CHECK(Math::floor(1.9f) == 1);
    CHECK(Math::floor(-1.1f) == -2);
    CHECK(Math::ceil(1.1f) == 2);
    CHECK(Math::ceil(-1.9f) == -1);
    CHECK(Math::truncate(1.9f) == 1);
    CHECK(Math::truncate(-1.9f) == -1);
    CHECK(Math::round(1.4f) == 1);
    CHECK(Math::round(1.6f) == 2);
}

TEST_CASE("modulo matches std::fmod for positive operands", "[math][scalar]")
{
    CHECK(Math::modulo(7.5f, 2.0f) == Approx(std::fmod(7.5f, 2.0f)));
    CHECK(Math::modulo(1.0f, 4.0f) == Approx(1.0f));
    CHECK(Math::modulo(7, 3) == 1);
    CHECK(Math::modulo(7u, 3u) == 1u);
}

TEST_CASE("Power-of-two helpers", "[math][scalar]")
{
    CHECK(Math::isPowerOfTwo(1024u));
    CHECK_FALSE(Math::isPowerOfTwo(1000u));
    // \note PowerOfTwo::floor and ::round return the *exponent*, not the power
    // of two -- floor is firstSetBit -- while ::ceil returns the value. The
    // asymmetry is 2016's; the assertions record it rather than endorse it.
    CHECK(Math::PowerOfTwo::floor(1000u) == 9u);
    CHECK(Math::PowerOfTwo::ceil(1000.0f) == 1024u);
    CHECK(Math::PowerOfTwo::log2(4096u) == 12);
    static_assert(Math::IsPowerOfTwo<256>::value);
    static_assert(!Math::IsPowerOfTwo<255>::value);
}

TEST_CASE("dB conversion round-trips", "[math][scalar][conversion]")
{
    CHECK(Math::dB2NormalisedLinear(0.0f) == Approx(1.0f));
    CHECK(Math::dB2NormalisedLinear(-20.0f) == Approx(0.1f));
    CHECK(Math::normalisedLinear2dB(1.0f) == Approx(0.0f));
    CHECK(Math::normalisedLinear2dB(0.1f) == Approx(-20.0f));

    for (float dB(-60); dB <= 0; dB += 6)
        CHECK(Math::normalisedLinear2dB(Math::dB2NormalisedLinear(dB)) == Approx(dB).margin(1e-3));
}

TEST_CASE("addPolar sums two phasors", "[math][scalar]")
{
    // No non-NT2 implementation existed before stage 3.
    SECTION("in phase, amplitudes add")
    {
        float amplitude(2), phase(0.0f);
        Math::addPolar(3.0f, 0.0f, amplitude, phase);
        CHECK(amplitude == Approx(5.0f));
        CHECK(phase == Approx(0.0f).margin(1e-6));
    }
    SECTION("antiphase, amplitudes cancel")
    {
        float amplitude(3), phase(0.0f);
        Math::addPolar(3.0f, Math::Constants::pi, amplitude, phase);
        CHECK(amplitude == Approx(0.0f).margin(1e-5));
    }
    SECTION("quadrature")
    {
        float amplitude(1), phase(Math::Constants::pi / 2);
        Math::addPolar(1.0f, 0.0f, amplitude, phase);
        CHECK(amplitude == Approx(std::sqrt(2.0f)));
        CHECK(phase == Approx(Math::Constants::pi / 4));
    }
}

TEST_CASE("The magnitude bound catches what the finiteness guards cannot", "[math][scalar]")
{
    ////////////////////////////////////////////////////////////////////////////
    // Uninitialised memory read as float is overwhelmingly huge and *finite*,
    // so every guard on the engine's input path passed it and it only became a
    // NaN three layers later, inside the amplitude conversion -- which is why
    // the assertion that fired named the wrong buffer. 2.9e33 is the value
    // measured coming out of an unconnected AUv2 bus.
    ////////////////////////////////////////////////////////////////////////////
    using namespace LE::Math;

    float const garbage[]{0.1f, -0.2f, 2.9e33f, 0.3f};
    CHECK(has<InvalidOrSlow>(garbage, std::size(garbage)) == 0); // the whole problem
    CHECK(has<Above100dB>(garbage, std::size(garbage)) == Above100dB);
    CHECK((has<ImplausibleAudio>(garbage, std::size(garbage)) & Above100dB) != 0);

    // Ordinary audio, including a good deal louder than full scale, is not it.
    float const audio[]{0.0f, 1.0f, -1.0f, 31.6f /* +30 dB */, -0.5f};
    CHECK(has<ImplausibleAudio>(audio, std::size(audio)) == 0);

    // The line itself, and it is a magnitude: the sign does not save a value.
    float const atTheLine[]{hundredDecibels, -hundredDecibels};
    CHECK(has<Above100dB>(atTheLine, std::size(atTheLine)) == 0);
    float const overTheLine[]{-hundredDecibels * 1.001f};
    CHECK(has<Above100dB>(overTheLine, std::size(overTheLine)) == Above100dB);

    ////////////////////////////////////////////////////////////////////////////
    // A NaN and an infinity are not this bit's business -- they have their own,
    // and reporting them twice would say "the magnitude is wrong" about a value
    // that has no magnitude.
    ////////////////////////////////////////////////////////////////////////////
    float const notFinite[]{std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity()};
    CHECK(has<Above100dB>(notFinite, std::size(notFinite)) == 0);
    CHECK(has<ImplausibleAudio>(notFinite, std::size(notFinite)) == (QuietNaN | Infinity));

    // 100 dB over unity, stated as the conversion rather than as a literal.
    CHECK(normalisedLinear2dB(hundredDecibels) == Approx(100.0f));
}

TEST_CASE("An empty random range is answered rather than divided by", "[math][scalar]")
{
    // an empty interval has one answer; it was a plain modulo, silent on arm64
    // and SIGFPE on x86, and Burrito's Target Range is Minimum<0> \see issue #190
    LE::Math::Rng rng;
    rng.seed(1u);

    CHECK(rng.ranged(std::uint32_t(0)) == 0u);
    CHECK(rng.ranged(std::uint16_t(0)) == 0u);

    // and the non-empty case still lands inside the interval
    for (unsigned int draw(0); draw < 64; ++draw)
    {
        CHECK(rng.ranged(std::uint32_t(7)) < 7u);
        CHECK(rng.ranged(std::uint16_t(3)) < 3u);
    }
}
