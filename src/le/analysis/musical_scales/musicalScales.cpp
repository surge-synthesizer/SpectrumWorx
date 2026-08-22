////////////////////////////////////////////////////////////////////////////////
///
/// musicalScales.cpp
/// -----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "musicalScales.hpp"

#include "le/math/conversion.hpp"
#include "le/math/constants.hpp"
#include "le/math/math.hpp"
#include "le/math/vector.hpp"

#include "le/utility/assert.hpp"

#include <array>
#include <cstring>
#include <numeric>

namespace LE::Music
{

Scale::Scale()
{
    lastPitchScale_ = 1;
    centerTone_ = -1;
    targetPitchChangeDirection_ = 0;
}

void Scale::tonesUpdated(std::uint8_t const snappedTo, std::uint8_t const bypassed)
{
#ifndef NDEBUG
    auto const n(snappedTo + bypassed);
    LE_ASSERT(unsigned(n) <= toneOffsets_.size());
    LE_ASSERT((*std::max_element(toneOffsets_.begin(), toneOffsets_.begin() + n) < 12) || (n == 0));
#endif // NDEBUG

    numberOfTones_ = snappedTo;
    LE_ASSUME(bypassed == 0);

#if 0
    if ( snappedTo == 0 )
    {
        centerTone_                 = -1;
        targetPitchChangeDirection_ =  0;
    }
    else
#endif // 0
    {
        if (snappedTo)
        {
            float const newCenterTone(
                std::accumulate(toneOffsets_.begin(), toneOffsets_.begin() + snappedTo, 0) /
                Math::convert<float>(snappedTo));
            LE_ASSUME(newCenterTone >= 0);
            if (newCenterTone != centerTone_)
            {
                if (centerTone_ == -1)
                    targetPitchChangeDirection_ = 0;
                else if (newCenterTone > centerTone_)
                    targetPitchChangeDirection_ = +1;
                else if (newCenterTone < centerTone_)
                    targetPitchChangeDirection_ = -1;
                centerTone_ = newCenterTone;
            }
        }
        else
        {
            centerTone_ = -1;
            targetPitchChangeDirection_ = 0;
        }
    }
}

float Scale::snap2Scale(float const freq, std::uint8_t const keyIndex) const
{
    using namespace Math;

    struct PitchScaleRatio
    {
        float ratio;
        bool inverted;
        bool operator<(PitchScaleRatio const &other) const { return ratio < other.ratio; }
    };

    float const pitchScaleComparisonSource(lastPitchScale_);

    /// \note At least one tone is a precondition: with none the min_element()
    /// below runs over an empty range and reads pitchScaleDeltas[ 0 ], which
    /// nothing wrote. Its one caller returns before it when numberOfTones() is
    /// zero, from another translation unit, so only an optimising build sees the
    /// question -- and a checked build gets an assert if a second caller forgets.
    std::uint8_t const totalTones(numberOfTones() + numberOfBypassed());
    LE_ASSUME(totalTones > 0);
    LE_ASSUME(totalTones < 12);
    std::array<PitchScaleRatio, 12> pitchScaleDeltas;
    for (std::uint8_t n(0); n < totalTones; ++n)
    {
        ToneOffsets::value_type const noteOffset(toneOffset(n));
        //...mrmlj...quick-doc: transform A-based scale to (lowest) C-based scale
        // move index in range [-9,2] for calculation of base frequency of current note (frequency in zeroth octave)
        int const noteBaseIndex(keyIndex + noteOffset - ((keyIndex + noteOffset + 9) / 12) * 12);

        // note base frequency
        float const noteBaseFreq(27.5f * semitone2Interval12TET(static_cast<float>(noteBaseIndex)));

        // ratio between discovered pitch and note base frequency
        float const freqRatio(freq / noteBaseFreq);

        //...mrmlj...quick-doc: check two neighbouring octaves for the actual closest harmonic
        // closest note is note base frequency multiplied by power of 2 closest to frequency ratio
        std::uint8_t const lowerOctaveExponent(PositiveFloats::floor(Math::log2(freqRatio)));
        float const lowerOctaveRatio(convert<float>(1 << lowerOctaveExponent));
        LE_ASSUME(lowerOctaveRatio <= freqRatio);
        float const lowerOctavePitch(lowerOctaveRatio * noteBaseFreq);
        float const lowerPitchScale(lowerOctavePitch / freq);
        float const upperPitchScale(lowerPitchScale * 2);
        float const lowerRatio(pitchScaleComparisonSource / lowerPitchScale);
        float const upperRatio(upperPitchScale / pitchScaleComparisonSource);
        if ((targetPitchChangeDirection_ > 0) || (upperRatio < lowerRatio))
        {
            pitchScaleDeltas[n].ratio = upperRatio;
            pitchScaleDeltas[n].inverted = false;
        }
        else
        {
            pitchScaleDeltas[n].ratio = lowerRatio;
            pitchScaleDeltas[n].inverted = true;
        }
    }

    // find the note with minimum distance and return closest frequency
    auto const &minimumRatio(
        *std::min_element(pitchScaleDeltas.begin(), pitchScaleDeltas.begin() + totalTones));

    float const newPitchScale(minimumRatio.inverted
                                  ? pitchScaleComparisonSource / minimumRatio.ratio
                                  : pitchScaleComparisonSource * minimumRatio.ratio);

    lastPitchScale_ = newPitchScale;
    return newPitchScale * freq;
}

Scale::ToneOffsets::value_type Scale::toneOffset(std::uint8_t const i) const
{
    LE_ASSERT(i < numberOfTones() + numberOfBypassed());
    return toneOffsets_[i];
}

} // namespace LE::Music
