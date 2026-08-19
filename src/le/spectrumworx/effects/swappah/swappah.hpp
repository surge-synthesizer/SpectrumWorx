////////////////////////////////////////////////////////////////////////////////
///
/// \file swappah.hpp
/// -----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef swappah_hpp__8B69F4EC_B747_4300_B33F_701BABBE80F9
#define swappah_hpp__8B69F4EC_B747_4300_B33F_701BABBE80F9
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/spectrumworx/effects/commonParameters.hpp"
#include "le/parameters/enumerated/parameter.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Swappah
///
/// \ingroup Effects
///
/// \brief Swaps three spectral bands, low, mid and high.
///
/// Swaps the three frequency bands determined by the band borders. Swap order
/// is determined by the Swap order parameter.
///
////////////////////////////////////////////////////////////////////////////////

struct Swappah
{
    /// \name Parameters
    /// @{
    typedef CommonParameters::Mode Mode;
    /// @}

    LE_ENUMERATED_PARAMETER(BandOrder, LowHighMid, MidLowHigh, MidHighLow, HighLowMid, HighMidLow);

    LE_DEFINE_PARAMETER(BandLowMid, LinearUnsignedInteger, Minimum<0>, Maximum<100>, Default<33>,
                        Unit<"%">);
    LE_DEFINE_PARAMETER(BandMidHigh, LinearUnsignedInteger, Minimum<0>, Maximum<100>, Default<66>,
                        Unit<"%">);
    LE_DEFINE_PARAMETERS(Mode, BandOrder, BandLowMid, BandMidHigh);

    /// \typedef Mode
    /// \brief Specifies what is to be swapped.
    /// \details
    ///   - Magnitudes: swap only magnitudes.
    ///   - Phases: swap only phases.
    ///   - Both: swap both magnitudes and phases.
    /// \typedef BandOrder
    /// \brief Determines the swapping order.
    /// \details
    ///    - LowHighMid: low-high-mid is output.
    ///    - MidLowHigh: mid-low-high is output.
    ///    - MidHighLow: mid-high-low is output.
    ///    - HighLowMid: high-low-mid is output.
    ///    - HighMidLow: high-mid-low is output.
    /// \typedef BandLowMid
    /// \brief Determines the border between low and mid band.
    /// \typedef BandMidHigh
    /// \brief Determines the border between mid and high band.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Swappah UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Swappah::BandLowMid, "Low <> Mid")
EFFECT_PARAMETER_NAME(Swappah::BandMidHigh, "Mid <> High")
EFFECT_PARAMETER_NAME(Swappah::BandOrder, "Swap Order")

EFFECT_PARAMETER_STREAMING_NAME(Swappah::BandLowMid, "Low-Mid border")
EFFECT_PARAMETER_STREAMING_NAME(Swappah::BandMidHigh, "Mid-High border")
EFFECT_PARAMETER_STREAMING_NAME(Swappah::BandOrder, "Swap order")

EFFECT_ENUMERATED_PARAMETER_STRINGS(Swappah, BandOrder,
    {LowHighMid, "Low - High - Mid"},
    {MidLowHigh, "Mid - Low - High"},
    {MidHighLow, "Mid - High - Low"},
    {HighLowMid, "High - Low - Mid"},
    {HighMidLow, "High - Mid - Low"})

EFFECT_ENUMERATED_PARAMETER_SHORT_STRINGS(Swappah, BandOrder,
    {LowHighMid, "L - H - M"},
    {MidLowHigh, "M - L - H"},
    {MidHighLow, "M - H - L"},
    {HighLowMid, "H - L - M"},
    {HighMidLow, "H - M - L"})

} // namespace LE::SW::Effects

#endif // swappah_hpp
