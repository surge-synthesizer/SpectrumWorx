////////////////////////////////////////////////////////////////////////////////
///
/// \file burrito.hpp
/// -----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef burrito_hpp__C27A00BA_023C_4D1F_9742_F431F45C6018
#define burrito_hpp__C27A00BA_023C_4D1F_9742_F431F45C6018
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/enumerated/parameter.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/symmetric/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Burrito
///
/// \ingroup Effects
///
/// \brief Random spectrum combination.
///
/// Copies side-channel data to the input at random locations.
///
////////////////////////////////////////////////////////////////////////////////

struct Burrito
{
    LE_ENUMERATED_PARAMETER(Mode, Replace, Sum);

    LE_DEFINE_PARAMETER(Range, LinearUnsignedInteger, Minimum<0>, Maximum<100>, Default<50>,
                        Unit<"%">);
    LE_DEFINE_PARAMETER(Period, LinearUnsignedInteger, Minimum<10>, Maximum<2000>, Default<250>,
                        Unit<" ms">);
    LE_DEFINE_PARAMETER(SideGain, SymmetricInteger, MaximumOffset<24>, Unit<" dB">);
    LE_DEFINE_PARAMETERS(Mode, Range, Period, SideGain);

    /// \typedef Mode
    /// \brief Combination mode.
    /// \details
    ///   - Replace: input is replaced by the side-channel at random locations.
    ///   - Sum: input is summed with side-channel at random locations.
    /// \typedef Range
    /// \brief Maximum amount of spectrum to randomize.
    /// \details (in percentage of bandwidth i.e. total
    /// frequency range)
    /// \typedef Period
    /// \brief Period in which the random locations are kept constant.
    /// \typedef SideGain
    /// \brief Gain applied to side-channel.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Burrito UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Burrito::Mode, "Target Creation")
EFFECT_PARAMETER_NAME(Burrito::Range, "Target Range")
EFFECT_PARAMETER_NAME(Burrito::Period, "Period")
EFFECT_PARAMETER_NAME(Burrito::SideGain, "SC Gain")

EFFECT_PARAMETER_STREAMING_NAME(Burrito::Mode, "Target creation")
EFFECT_PARAMETER_STREAMING_NAME(Burrito::Range, "Target range")
EFFECT_PARAMETER_STREAMING_NAME(Burrito::Period, "Range period")
EFFECT_PARAMETER_STREAMING_NAME(Burrito::SideGain, "Side gain")

EFFECT_ENUMERATED_PARAMETER_STRINGS(Burrito, Mode,
    {Replace, "Replace"},
    {Sum, "Sum"})

} // namespace LE::SW::Effects

#endif // burrito_hpp
