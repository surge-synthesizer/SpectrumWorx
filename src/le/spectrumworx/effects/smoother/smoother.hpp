////////////////////////////////////////////////////////////////////////////////
///
/// \file smoother.hpp
/// ------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef smoother_hpp__17DDFA09_71A5_4CDD_9A50_B60BB89DD53B
#define smoother_hpp__17DDFA09_71A5_4CDD_9A50_B60BB89DD53B
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Smoother
///
/// \ingroup Effects
///
/// \brief Smooth spectrum with a lowpass filter.
///
/// "Smoothes" the frequency spectrum of the incoming signal, causing it to
/// lose sharpness and detail for a blurred image.
///
////////////////////////////////////////////////////////////////////////////////

struct Smoother
{
    LE_DEFINE_PARAMETER(AveragingWidth, LinearUnsignedInteger, Minimum<0>, Maximum<2000>,
                        Default<500>, Unit<" Hz">);
    LE_DEFINE_PARAMETERS(AveragingWidth);

    /// \typedef AveragingWidth
    /// \brief Width of the region to be smoothed.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Smoother UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Smoother::AveragingWidth, "Smoothness")

} // namespace LE::SW::Effects

#endif // smoother_hpp
