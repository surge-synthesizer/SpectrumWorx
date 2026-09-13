////////////////////////////////////////////////////////////////////////////////
///
/// peakDetector.cpp
/// ----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "peakDetector.hpp"

#include "le/math/conversion.hpp"
#include "le/math/math.hpp"
#include "le/math/vector.hpp"
#include "le/utility/clear.hpp"

#include "le/utility/stackBuffer.hpp"

#include "le/utility/assert.hpp"

#include <algorithm>
#include <cmath>

namespace LE
{
// https://ccrma.stanford.edu/~jos/parshl/Peak_Detection_Steps_3.html
// http://hci.rwth-aachen.de/materials/publications/lee2006a.pdf (under
// "Multiresolution Peak-Picking Algorithm")
// http://hci.rwth-aachen.de/materials/publications/karrer2006a.pdf
//------------------------------------------------------------------------------

using namespace LE::SW; //...mrmlj...

bool operator<(Peak const &left, Peak const &right)
{
    LE_ASSERT(left.strength >= 0);
    LE_ASSERT(right.strength >= 0);
    /// \note We want a descending sort.
    ///                                       (05.04.2016.) (Domagoj Saric)
    return left.strength > right.strength;
}

namespace
{
std::int16_t findThdStart(float const *const amp, float const thd, std::uint16_t const num)
{
    // Try to find stronger amplitude
    auto const pValue(std::find_if(amp, amp + num, [=](float const value) { return value > thd; }));
    return static_cast<std::int16_t>(pValue != (amp + num) ? (pValue - amp) : -1);
}

std::int16_t findUpMax(float const *const amp, std::uint16_t const num)
{
    // Set starting position as maximum:
    std::uint16_t maxIndex(0);

    // Try to find stronger amplitude
    for (std::uint16_t index(1); index < num; ++index)
    {
        if (amp[index] >= amp[index - 1])
            maxIndex = index; // if stronger, make it target
        else
            return maxIndex; // if weaker, exit
    }

    return -1;
}

std::int16_t findDownMin(float const *const amp, std::uint16_t const num)
{
    // Set starting position as minimum:
    std::uint16_t minIndex(0);

    // Try to find weaker amplitude
    for (std::uint16_t index(1); index < num; ++index)
    {
        if (amp[index] < amp[index - 1])
            minIndex = index; // if weaker, make it target
        else
            return minIndex /* - 1 */; // if stronger or same, exit
    }

    return -1;
}
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
//
// PeakDetector::
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////

PeakDetector::PeakDetector()
    : localThreshold_(PD_DEFAULT_LOCAL_THRESHOLD), globalThreshold_(PD_DEFAULT_GLOBAL_THRESHOLD),
      strengthThreshold_(PD_DEFAULT_STRENGTH), maxGlobal_(std::numeric_limits<float>::epsilon()),
      numberOfPeaks_(0)
{
}

////////////////////////////////////////////////////////////////////////////////
//
// PeakDetector::
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////

void PeakDetector::setLocalThreshold(float const threshold)
{
    LE_ASSERT(((threshold <= PD_MIN_LOCAL_THRESHOLD) && (threshold >= PD_MAX_LOCAL_THRESHOLD)));
    localThreshold_ = threshold;
}

////////////////////////////////////////////////////////////////////////////////
//
// PeakDetector::
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////

void PeakDetector::setGlobalThreshold(float const threshold)
{
    LE_ASSERT(((threshold <= PD_MIN_GLOBAL_THRESHOLD) && (threshold >= PD_MAX_GLOBAL_THRESHOLD)));
    globalThreshold_ = threshold;
}

////////////////////////////////////////////////////////////////////////////////
//
// PeakDetector::
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////

void PeakDetector::setStrengthThreshold(float const strength)
{
    LE_ASSERT(!((strength <= PD_MIN_STRENGTH) && (strength >= PD_MAX_STRENGTH)));
    strengthThreshold_ = strength;
}

////////////////////////////////////////////////////////////////////////////////
//
// PeakDetector::
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////

void PeakDetector::setZeroDecibelValue(float const zeroDecibel) { maxGlobal_ = zeroDecibel; }

////////////////////////////////////////////////////////////////////////////////
//
// PeakDetector::
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////

void PeakDetector::restart()
{
    Utility::clear(isPeak_);
    numberOfPeaks_ = 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// PeakDetector::findPeaks*()
// --------------------------
//
////////////////////////////////////////////////////////////////////////////////

void PeakDetector::findPeaks(float const *const amplitudes, std::uint16_t const numberOfBins)
{
    findPeaksImpl(amplitudes, numberOfBins, 0);
    LE_ASSERT(numberOfPeaks_ <= MAX_NUM_PEAKS);
}

void PeakDetector::findPeaksAndStrengthSort(float const *const amplitudes,
                                            std::uint16_t const numberOfBins)
{
    findPeaks(amplitudes, numberOfBins);
    // data() + n rather than &peaks_[n]: the end of a full array is one past the
    // last, which a hardened operator[] refuses to index \see issue #190
    std::sort(peaks_.data(), peaks_.data() + numberOfPeaks_);
}

void PeakDetector::findPeaksAndEstimateFrequency(float const *const amplitudes,
                                                 std::uint16_t const numberOfBins,
                                                 std::uint32_t const fs)
{
    findPeaksImpl(amplitudes, numberOfBins, fs);
}

////////////////////////////////////////////////////////////////////////////////
//
// PeakDetector::findPeaksImpl()
// -----------------------------
//
////////////////////////////////////////////////////////////////////////////////

namespace
{
void calculateTrueFrequency(Peak &peak, float const bins, std::uint32_t const fs,
                            SW::Engine::ReadOnlyDataRange const &mags)
{
    using Math::convert;
    // Parabola fit:
    float const a(mags[peak.maxPos - 1]);
    float const b(mags[peak.maxPos]);
    float const c(mags[peak.maxPos + 1]);

    float const p(0.5f * (a - c) / (a - 2.0f * b + c));
    float const bin(convert<float>(peak.maxPos) + p);

    peak.freq = convert<float>(fs) * bin / bins / 2;
    peak.amplitude = b - 0.25f + (a - c) * p;

    LE_ASSERT(peak.freq);
    LE_ASSERT(peak.amplitude);
}
} // anonymous namespace

void PeakDetector::findPeaksImpl(float const *LE_RESTRICT const amplitudes,
                                 std::uint16_t const numBins, std::uint32_t const fs)
{
    restart();

    using namespace Math;

    // Find max element
    float maxLocal(Math::max(amplitudes, numBins));
    // ... and global maximum
    float const maxGlobal(std ::max(maxGlobal_, maxLocal));
    // ... in decibel relative to global maximum
    maxLocal = normalisedLinear2dB(maxLocal / maxGlobal);
    maxGlobal_ = maxGlobal;

    // Convert to dB, with reference level being the maximum in this frame.
    LE_ALIGNED_STACK_BUFFER(ampsdB, SW::Engine::real_t, numBins);
    //...mrmlj...ugly msvc12 codegen...std::transform( amplitudes, amplitudes + numBins, ampsdB.begin(), [=]( float const & amp ) { return normalisedLinear2dB( amp / maxGlobal ); } );
    for (std::uint16_t i(0); i < numBins; ++i)
    {
        ampsdB[i] = normalisedLinear2dB(amplitudes[i] / maxGlobal);
    }

    float const numberOfBins(convert<float>(numBins));

    // Place for found peaks
    Peak *LE_RESTRICT pPeak(&peaks_[0]);

    // Start search from spectrum beginning
    std::uint16_t pos(0);
    do
    {
        // Find starting bin that is above some threshold, to avoid small amplitudes
        {
            float const lowestAmplitudeThreshold(-55);
            auto const thdStart(
                findThdStart(&ampsdB[pos], lowestAmplitudeThreshold, numBins - pos));
            if (thdStart == -1)
                break; // if not found exit
            pPeak->startPos = static_cast<std::uint16_t>(thdStart + pos);
        }

        // Find peak
        {
            auto const upMax(findUpMax(&ampsdB[pPeak->startPos], numBins - pPeak->startPos));
            if (upMax == -1)
                break; // if not found exit
            pPeak->maxPos = static_cast<std::uint16_t>(pPeak->startPos + upMax);
        }

        // Find where peak ends, falloff
        {
            auto const downMin(findDownMin(&ampsdB[pPeak->maxPos], numBins - pPeak->maxPos));
            if (downMin == -1)
                break; // if not found exit
            pPeak->stopPos = static_cast<std::uint16_t>(pPeak->maxPos + downMin);
        }

        // Calculate peak strength as difference between peak and averaged
        // magnitudes that are below the peak at peak borders.
        pPeak->strength =
            ampsdB[pPeak->maxPos] - (ampsdB[pPeak->startPos] + ampsdB[pPeak->stopPos]) / 2;

        // update position
        pos = pPeak->stopPos;

        // Decide if peak is OK!
        if (pPeak->startPos != pPeak->maxPos)
        {
            float const mag(ampsdB[pPeak->maxPos]);
            if ((((maxLocal - mag) <
                  localThreshold_) ||           // is peak closer to local max than threshold?
                 ((0 - mag) < globalThreshold_) // is peak closer to global max than threshold?
                 ) &&                           // AND
                (pPeak->strength > strengthThreshold_) // is peak strong enough?
            )
            {
                std::fill(isPeak_.data() + pPeak->startPos + 1, isPeak_.data() + pPeak->stopPos,
                          true);

                if (fs)
                    calculateTrueFrequency(*pPeak, numberOfBins, fs, ampsdB);

                pPeak->valid = true;
                ++numberOfPeaks_;
                if (numberOfPeaks_ >= peaks_.size())
                    break;

                // next peak
                ++pPeak;
            }
        }
    } while (pos < numBins - 1);
}

////////////////////////////////////////////////////////////////////////////////
//
// PeakDetector::getPeak()
// -----------------------
//
////////////////////////////////////////////////////////////////////////////////

Peak const *PeakDetector::getPeak(std::uint8_t const pos) const
{
    LE_ASSERT(pos < numberOfPeaks_ || pos == 0);
    return &peaks_[pos];
}

////////////////////////////////////////////////////////////////////////////////
//
// PeakDetector::
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////

Peak const *PeakDetector::getPeakAboveThreshold(float const threshold) const
{
    for (std::uint8_t pos(0); pos < numberOfPeaks_; pos++)
    {
        if (peaks_[pos].amplitude > threshold)
            return &peaks_[pos];
    }

    return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// PeakDetector::attenuatePeaks()
// ------------------------------
//
////////////////////////////////////////////////////////////////////////////////

namespace
{
void attenuateBins(float *LE_RESTRICT const pAmplitudes, bool *LE_RESTRICT const pPeaks,
                   std::uint16_t const startBin, std::uint16_t const stopInclusive,
                   float const factor, bool const ifPeak)
{
    float const gain((factor > 150) ? 0 : Math::dB2NormalisedLinear(-factor));

    for (auto pos(startBin); pos <= stopInclusive; ++pos)
    {
        if (pPeaks[pos] == ifPeak)
            pAmplitudes[pos] *= gain;
    }
}
} // anonymous namespace

void PeakDetector::attenuatePeaks(float *const amplitudes, std::uint16_t const startBin,
                                  std::uint16_t const stopInclusive, float const factor)
{
    attenuateBins(amplitudes, &isPeak_[0], startBin, stopInclusive, factor, true);
}

////////////////////////////////////////////////////////////////////////////////
//
// PeakDetector::attenuateNonPeaks()
// ---------------------------------
//
////////////////////////////////////////////////////////////////////////////////

void PeakDetector::attenuateNonPeaks(float *const amplitudes, std::uint16_t const startBin,
                                     std::uint16_t const stopInclusive, float const factor)
{
    attenuateBins(amplitudes, &isPeak_[0], startBin, stopInclusive, factor, false);
}

} // namespace LE
