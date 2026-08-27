////////////////////////////////////////////////////////////////////////////////
///
/// vectorTests.cpp
/// ---------------
///
///   le/math/vector.cpp's primitives. Five of these had no implementation at
/// all outside NT2 and Accelerate before stage 3 -- mix(), addPolar(),
/// polar2rectangular()'s scalar arm, rectangular2polar() and amplitudes() --
/// and the last two returned without writing their outputs. They are the
/// reason this file exists.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "le/math/constants.hpp"
#include "le/math/vector.hpp"
#include "le/utility/buffers.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>
//------------------------------------------------------------------------------

using Catch::Approx;
namespace Math = LE::Math;

namespace
{
/// \note The primitives take LE_RESTRICT pointers and several of them assume
/// SIMD alignment, so the fixtures allocate through the engine's own aligned
/// buffer rather than std::vector.
class AlignedFloats
{
  public:
    explicit AlignedFloats(unsigned int const size) { buffer_.resize(size); }

    float *begin() const { return buffer_.begin(); }
    float *end() const { return buffer_.end(); }
    float *data() const { return buffer_.data(); }
    unsigned int size() const { return buffer_.size(); }
    float &operator[](unsigned int const index) const { return buffer_[index]; }

    operator Math::OutputRange() const { return Math::OutputRange(begin(), end()); }
    operator Math::InputRange() const { return Math::InputRange(begin(), end()); }

    void fill(float const value) const { std::fill(begin(), end(), value); }
    void iota(float const first = 0) const { std::iota(begin(), end(), first); }

  private:
    mutable LE::Utility::AlignedHeapBuffer<float> buffer_;
};

constexpr unsigned int size{64};
} // anonymous namespace

TEST_CASE("copy, clear and fill", "[math][vector]")
{
    AlignedFloats const source(size), destination(size);
    source.iota(1);
    destination.fill(-1);

    Math::copy(source, destination);
    for (unsigned int index(0); index < size; ++index)
        CHECK(destination[index] == Approx(source[index]));

    Math::fill(destination, 7.0f);
    CHECK(std::all_of(destination.begin(), destination.end(),
                      [](float const value) { return value == 7; }));

    Math::clear(destination);
    CHECK(std::all_of(destination.begin(), destination.end(),
                      [](float const value) { return value == 0; }));
}

TEST_CASE("negate, add and multiply", "[math][vector]")
{
    AlignedFloats const data(size), other(size);
    data.iota(1);
    other.fill(2);

    Math::negate(Math::InputOutputRange(data));
    for (unsigned int index(0); index < size; ++index)
        CHECK(data[index] == Approx(-static_cast<float>(index + 1)));

    Math::negate(Math::InputOutputRange(data));
    Math::add(Math::InputRange(other), Math::InputOutputRange(data));
    for (unsigned int index(0); index < size; ++index)
        CHECK(data[index] == Approx(static_cast<float>(index + 1) + 2));

    Math::multiply(Math::InputOutputRange(data), 0.5f);
    for (unsigned int index(0); index < size; ++index)
        CHECK(data[index] == Approx((static_cast<float>(index + 1) + 2) * 0.5f));
}

TEST_CASE("a strided negate stays inside the range it was given", "[math][vector]")
{
    /// \note Phlip is the only caller that passes a stride, and what it hands over
    /// is the main channel's phases -- so anything written past the end lands in
    /// the next buffer of the engine's shared storage. \see issue #10.
    AlignedFloats const data(size);
    data.fill(1);

    constexpr unsigned int half{size / 2};
    Math::negate(Math::InputOutputRange(data.begin(), data.begin() + half), 2);

    for (unsigned int index(0); index < half; ++index)
        CHECK(data[index] == ((index % 2) ? 1.0f : -1.0f));
    for (unsigned int index(half); index < size; ++index)
        CHECK(data[index] == 1.0f);
}

TEST_CASE("addProduct is a fused multiply-add over the buffer", "[math][vector]")
{
    AlignedFloats const a(size), b(size), accumulator(size);
    a.fill(3);
    b.fill(4);
    accumulator.fill(1);

    Math::addProduct(a.data(), b.data(), accumulator.data(), size);
    CHECK(std::all_of(accumulator.begin(), accumulator.end(),
                      [](float const value) { return value == Approx(13.0f); }));
}

TEST_CASE("min, max and rms", "[math][vector]")
{
    AlignedFloats const data(size);
    data.iota(0);
    data[7] = -5;
    data[13] = 1000;

    CHECK(Math::min(Math::InputRange(data)) == Approx(-5.0f));
    CHECK(Math::max(Math::InputRange(data)) == Approx(1000.0f));

    AlignedFloats const constant(size);
    constant.fill(3);
    CHECK(Math::rms(Math::InputRange(constant)) == Approx(3.0f));
}

TEST_CASE("ln, exp, square and squareRoot", "[math][vector]")
{
    AlignedFloats const data(size);
    data.iota(1);

    AlignedFloats const roundTrip(size);
    Math::copy(data, roundTrip);
    Math::ln(Math::InputOutputRange(roundTrip));
    Math::exp(Math::InputOutputRange(roundTrip));
    for (unsigned int index(0); index < size; ++index)
        CHECK(roundTrip[index] == Approx(data[index]).epsilon(1e-4));

    Math::copy(data, roundTrip);
    Math::square(Math::InputOutputRange(roundTrip));
    Math::squareRoot(Math::InputOutputRange(roundTrip));
    for (unsigned int index(0); index < size; ++index)
        CHECK(roundTrip[index] == Approx(data[index]).epsilon(1e-4));
}

TEST_CASE("reverse and swap", "[math][vector]")
{
    // Both chose their NT2 fast path on __GNUC__ rather than on NT2 being
    // present, so this exercises code that used to be unreachable.
    AlignedFloats const data(size);
    data.iota(0);
    Math::reverse(data.begin(), data.end());
    for (unsigned int index(0); index < size; ++index)
        CHECK(data[index] == Approx(static_cast<float>(size - 1 - index)));

    AlignedFloats const first(size), second(size);
    first.fill(1);
    second.fill(2);
    Math::swap(first.begin(), first.end(), second.begin());
    CHECK(std::all_of(first.begin(), first.end(), [](float const v) { return v == 2; }));
    CHECK(std::all_of(second.begin(), second.end(), [](float const v) { return v == 1; }));
}

TEST_CASE("rectangular2polar and polar2rectangular round-trip", "[math][vector]")
{
    // rectangular2polar and amplitudes had an NT2 arm and an Accelerate arm
    // and nothing else: on any other build they returned without writing.
    AlignedFloats const reals(size), imags(size), amps(size), phases(size);
    for (unsigned int index(0); index < size; ++index)
    {
        auto const angle(static_cast<float>(index) * 0.1f);
        reals[index] = std::cos(angle) * (1 + index);
        imags[index] = std::sin(angle) * (1 + index);
    }

    Math::rectangular2polar(reals.data(), imags.data(), amps.data(), phases.data(),
                            static_cast<std::uint16_t>(size));
    for (unsigned int index(0); index < size; ++index)
    {
        CHECK(amps[index] == Approx(static_cast<float>(1 + index)).epsilon(1e-4));
        CHECK(amps[index] >= 0);
    }

    AlignedFloats const realsBack(size), imagsBack(size);
    Math::polar2rectangular(amps.data(), phases.data(), realsBack.data(), imagsBack.data(),
                            static_cast<std::uint16_t>(size));
    for (unsigned int index(0); index < size; ++index)
    {
        CHECK(realsBack[index] == Approx(reals[index]).margin(1e-3));
        CHECK(imagsBack[index] == Approx(imags[index]).margin(1e-3));
    }
}

TEST_CASE("amplitudes is the magnitude of the rectangular pair", "[math][vector]")
{
    AlignedFloats const reals(size), imags(size), amps(size);
    reals.fill(3);
    imags.fill(4);
    amps.fill(-1);

    Math::amplitudes(reals.data(), imags.data(), amps.data(), amps.end());
    CHECK(std::all_of(amps.begin(), amps.end(),
                      [](float const value) { return value == Approx(5.0f); }));
}

TEST_CASE("mix blends a polar and a rectangular spectrum", "[math][vector]")
{
    // No non-NT2 implementation existed. amPhGain weights the (amp, phase)
    // input, reImGain the (real, imag) accumulator already in place.
    AlignedFloats const amps(size), phases(size), reals(size), imags(size);
    amps.fill(2);
    phases.fill(0); // cos = 1, sin = 0
    reals.fill(10);
    imags.fill(20);

    Math::mix(Math::InputRange(amps), Math::InputRange(phases), Math::InputOutputRange(reals),
              Math::InputOutputRange(imags), 0.5f, 0.25f);

    for (unsigned int index(0); index < size; ++index)
    {
        CHECK(reals[index] == Approx(2 * 0.5f * 1.0f + 10 * 0.25f));
        CHECK(imags[index] == Approx(2 * 0.5f * 0.0f + 20 * 0.25f).margin(1e-5));
    }
}

TEST_CASE("mix's five argument overload splits the weight", "[math][vector]")
{
    AlignedFloats const amps(size), phases(size), reals(size), imags(size);
    amps.fill(1);
    phases.fill(0);
    reals.fill(4);
    imags.fill(0);

    Math::mix(Math::InputRange(amps), Math::InputRange(phases), Math::InputOutputRange(reals),
              Math::InputOutputRange(imags), 0.25f);

    for (unsigned int index(0); index < size; ++index)
        CHECK(reals[index] == Approx(1 * 0.25f + 4 * 0.75f));
}

TEST_CASE("movingAverage slides a window", "[math][vector]")
{
    // The idiom Span exists for: advance_begin/advance_end walking a window.
    AlignedFloats const data(16);
    data.fill(1);
    Math::movingAverage(Math::InputOutputRange(data), 4, false /*forcePositive*/);
    // A constant signal averages to itself over the full-window section.
    for (unsigned int index(0); index + 4 <= 16; ++index)
        CHECK(data[index] == Approx(1.0f));
}

TEST_CASE("symmetricMovingAverage survives a window wider than the data", "[math][vector]")
{
    // a working range is as narrow as Start and Stop make it -- one bin, at the
    // smallest FFT -- and the window is a parameter in bins that knows neither,
    // so this was a precondition no caller could establish \see issue #190
    for (unsigned int size(1); size <= 8; ++size)
    {
        AlignedFloats const input(size);
        AlignedFloats const output(size);
        input.fill(1);
        output.fill(0);

        for (unsigned int window(1); window <= 16; ++window)
        {
            INFO("size " << size << ", window " << window);
            Math::symmetricMovingAverage(input, output, window, false /*forcePositive*/);
            // A constant signal averages to itself, whatever the window does.
            for (unsigned int index(0); index < size; ++index)
                CHECK(output[index] == Approx(1.0f));
        }
    }
}

TEST_CASE("symmetricMovingAverage keeps a magnitude non-negative", "[math][vector]")
{
    // a bin below the ulp of the partial sum vanishes into the running sum and
    // is then subtracted back out, leaving a residue no later term removes
    AlignedFloats const input(12), output(12);
    input.fill(0);
    input[2] = 74;
    input[3] = 1e-4f;

    Math::symmetricMovingAverage(input, output, 4, true /*forcePositive*/);

    for (unsigned int index(0); index < input.size(); ++index)
    {
        INFO("bin " << index << " = " << output[index]);
        CHECK(output[index] >= 0);
    }
}

TEST_CASE("symmetricMovingAverage carries no negative residue", "[math][vector]")
{
    // the residue is sticky: it survives to poison a later window that has
    // nothing to do with the pair of bins that produced it
    AlignedFloats const input(20), output(20);
    input.fill(0);
    input[2] = 74;
    input[3] = 1e-4f;
    input[14] = 1e-6f;

    Math::symmetricMovingAverage(input, output, 4, true /*forcePositive*/);

    // one non-zero bin inside a five wide window
    CHECK(output[14] == Approx(1e-6f / 5));
}

TEST_CASE("symmetricMovingAverage leaves a signed input signed", "[math][vector]")
{
    // the floor is opt-in: a cepstrum or a phase axis has a sign to keep
    AlignedFloats const input(16), output(16);
    input.fill(-1);

    Math::symmetricMovingAverage(input, output, 4, false /*forcePositive*/);
    for (unsigned int index(0); index < input.size(); ++index)
        CHECK(output[index] == Approx(-1.0f));
}

TEST_CASE("symmetricMovingAverage's floor costs a well conditioned signal nothing",
          "[math][vector]")
{
    // it may only fire where the running sum has already lost the value
    AlignedFloats const input(64), floored(64), unfloored(64);
    input.iota(1);

    for (unsigned int window(1); window <= 16; ++window)
    {
        INFO("window " << window);
        Math::symmetricMovingAverage(input, floored, window, true /*forcePositive*/);
        Math::symmetricMovingAverage(input, unfloored, window, false /*forcePositive*/);
        for (unsigned int index(0); index < input.size(); ++index)
            CHECK(floored[index] == unfloored[index]);
    }
}

TEST_CASE("interleave and deinterleave are inverses", "[math][vector]")
{
    constexpr std::uint16_t frames{32};
    AlignedFloats const left(frames), right(frames), interleaved(frames * 2);
    left.iota(0);
    right.iota(100);

    float const *inputs[]{left.data(), right.data()};
    Math::interleave(inputs, interleaved.data(), frames, 2);
    for (std::uint16_t frame(0); frame < frames; ++frame)
    {
        CHECK(interleaved[frame * 2 + 0] == Approx(left[frame]));
        CHECK(interleaved[frame * 2 + 1] == Approx(right[frame]));
    }

    AlignedFloats const leftBack(frames), rightBack(frames);
    float *outputs[]{leftBack.data(), rightBack.data()};
    Math::deinterleave(interleaved.data(), outputs, frames, 2);
    for (std::uint16_t frame(0); frame < frames; ++frame)
    {
        CHECK(leftBack[frame] == Approx(left[frame]));
        CHECK(rightBack[frame] == Approx(right[frame]));
    }
}

TEST_CASE("alignIndex rounds up to the vector width", "[math][vector]")
{
    CHECK(Math::alignIndex(0) == 0);
    CHECK(Math::alignIndex(1) == 4);
    CHECK(Math::alignIndex(4) == 4);
    CHECK(Math::alignIndex(5) == 8);
}
