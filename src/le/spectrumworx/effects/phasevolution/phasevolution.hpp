////////////////////////////////////////////////////////////////////////////////
///
/// \file phasevolution.hpp
/// -----------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef phasevolution_hpp__F60D23C3_A21E_4D23_B742_3E9E59D1D3F4
#define phasevolution_hpp__F60D23C3_A21E_4D23_B742_3E9E59D1D3F4
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
/// \class Phasevolution
///
/// \ingroup Effects
///
/// \brief Accelerated phase change.
///
/// Creates an accelerated change in phase over time, which results in a
/// pulsating pitch effect. Demands a short frame size for proper operation.
/// Works better on signals with lots of clear harmonics.
///
////////////////////////////////////////////////////////////////////////////////

struct Phasevolution
{
    LE_DEFINE_PARAMETER(PhasePeriod, LinearFloat, Minimum<1>, Maximum<5000>, Default<500>,
                        ValuesDenominator<1000>, Unit<" s">);
    LE_DEFINE_PARAMETERS(PhasePeriod);

    /// \typedef PhasePeriod
    /// \brief Controls the rate at which the phase comes full circle.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Phasevolution UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Phasevolution::PhasePeriod, "Period")

} // namespace LE::SW::Effects

#endif // phasevolution_hpp
