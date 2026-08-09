////////////////////////////////////////////////////////////////////////////////
///
/// \file blender.hpp
/// -----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef blender_hpp__F2596834_8722_40A2_B5AB_B2E91D94182A
#define blender_hpp__F2596834_8722_40A2_B5AB_B2E91D94182A
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
/// \class Blender
///
/// \ingroup Effects
///
/// \brief Linear blend of two signals.
///
/// Input and side channel frequencies are combined.
///
////////////////////////////////////////////////////////////////////////////////

struct Blender
{
    LE_DEFINE_PARAMETER(Amount, LinearUnsignedInteger, Minimum<0>, Maximum<100>, Default<30>,
                        Unit<" %">);
    LE_DEFINE_PARAMETERS(Amount);

    /// \typedef Amount
    /// \brief Controls how much of each channel is sent to the output.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Blender UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Blender::Amount, "Amount")

} // namespace LE::SW::Effects

#endif // blender_hpp
