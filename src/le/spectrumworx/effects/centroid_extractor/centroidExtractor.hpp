////////////////////////////////////////////////////////////////////////////////
///
/// \file centroidExtractor.hpp
/// ---------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef centroidExtractor_hpp__91D2BE08_ACCE_4C50_B172_CCB947A246DC
#define centroidExtractor_hpp__91D2BE08_ACCE_4C50_B172_CCB947A246DC
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/enumerated/parameter.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class CentroidExtractor
///
/// \ingroup Effects
///
/// \brief Adaptively finds the spectral centre and preserves the spectrum
/// around it.
///
/// Centroid is an adaptive, self-tuning band pass filter which follows the
/// centre frequency with a controllable symmetric band curve (slope).
/// The module determines the time variable centre frequency with different
/// processing modes. Frequencies outside the region around the centroid
/// frequency are removed, the rest inside the band is smoothed.
///
////////////////////////////////////////////////////////////////////////////////

struct CentroidExtractor
{
    LE_ENUMERATED_PARAMETER(Mode, Centroid, Peak, Dominant);

    LE_DEFINE_PARAMETER(Bandwidth, LinearUnsignedInteger, Minimum<0>, Maximum<6000>, Default<1000>,
                        Unit<" Hz">);
    LE_DEFINE_PARAMETER(Attenuation, LinearSignedInteger, Minimum<0>, Maximum<100>, Default<10>,
                        Unit<" dB">);
    LE_DEFINE_PARAMETERS(Mode, Bandwidth, Attenuation);

    /// \typedef Mode
    /// \brief
    ///   - Centroid: finds the weighted centre frequency.
    ///   - Peak: finds the highest peak in the spectrum.
    ///   - Dominant: estimates the dominant pitch.
    /// \typedef Bandwidth
    /// \brief Frequencies around the centroid frequency outside the Bandwidth
    /// region are removed.
    /// \typedef Attenuation
    /// \brief Intensity of attenuation.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// CentroidExtractor UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(CentroidExtractor::Mode, "Filter center")
EFFECT_PARAMETER_NAME(CentroidExtractor::Bandwidth, "Bandwidth")
EFFECT_PARAMETER_NAME(CentroidExtractor::Attenuation, "Border slope")

EFFECT_ENUMERATED_PARAMETER_STRINGS(CentroidExtractor, Mode,
    {Centroid, "Centroid"},
    {Peak, "Peak"},
    {Dominant, "Dominant"})

} // namespace LE::SW::Effects

#endif // centroidExtractor_hpp
