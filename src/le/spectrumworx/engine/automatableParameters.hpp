////////////////////////////////////////////////////////////////////////////////
///
/// \file automatableParameters.hpp
/// -------------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef automatableParameters_hpp__FF0C3B97_B4C7_4326_B5B9_659183A9AA22
#define automatableParameters_hpp__FF0C3B97_B4C7_4326_B5B9_659183A9AA22
//------------------------------------------------------------------------------
#include "le/spectrumworx/engine/configuration.hpp"
#include "le/parameters/enumerated/parameter.hpp"
#include "le/parameters/powerOfTwo/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE
{

namespace SW::Engine
{

// Implementation note:
//   Unlike with other enumerate/"discrete values parameters", we do not use the
// LE_ENUMERATED_PARAMETER macro to define the window function parameter
// because its possible values are already defined with a plain enum in the
// SW SDK which we simply reuse here.
//                                            (01.04.2011.) (Domagoj Saric)
// Implementation note:
//   As a first choice, based on the comment at the bottom of this page
// http://www.katjaas.nl/FFTwindow/FFTwindow&filtering.html, the Hann
// window was chosen as the default/"overall best" window.
//                                            (20.01.2010.) (Domagoj Saric)
/// \todo Further investigate the Hann <-> Hanning debate/confusion. In this
/// http://www.hydrogenaudio.org/forums/lofiversion/index.php/t29439.html
/// discussion it is claimed that both Hann and Hanning windows exist.
///                                           (25.01.2010.) (Domagoj Saric)

using WindowFunction = Parameters::EnumeratedParameter<Constants::NumberOfWindows>;

using FFTSize =
    Parameters::PowerOfTwoParameter<Parameters::Traits::Minimum<Constants::minimumFFTSize>,
                                    Parameters::Traits::Maximum<Constants::maximumFFTSize>,
                                    Parameters::Traits::Default<Constants::defaultFFTSize>>;

using OverlapFactor =
    Parameters::PowerOfTwoParameter<Parameters::Traits::Minimum<Constants::minimumOverlapFactor>,
                                    Parameters::Traits::Maximum<Constants::maximumOverlapFactor>,
                                    Parameters::Traits::Default<Constants::defaultOverlapFactor>>;

} // namespace SW::Engine

namespace Parameters
{

////////////////////////////////////////////////////////////////////////////////
//
// Engine UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

UI_NAME(SW::Engine::FFTSize, "FFT Size")
UI_NAME(SW::Engine::OverlapFactor, "Overlap Factor")
UI_NAME(SW::Engine::WindowFunction, "Window Type")

//...mrmlj...this does not work yet because the Window enum is not a member
//...of the WindowFunction parameter class...fix this...
//ENUMERATED_PARAMETER_STRINGS
//(
//    SW::Engine, WindowFunction,
//    (( Hamming       , "Hamming"         ))
//    (( Hann          , "Hann"            ))
//    (( Rectangle     , "Rectangle"       ))
//    (( Triangle      , "Triangle"        ))
//    (( Blackman      , "Blackman"        ))
//    (( BlackmanHarris, "Blackman Harris" ))
//    (( Welch         , "Welch"           ))
//    (( FlatTop       , "Flat top"        ))
//    (( Gaussian      , "Gaussian"        ))
//)

/// \note Written out rather than through ENUMERATED_PARAMETER_STRINGS for the
/// reason above: that macro checks each string against the enumerator it names,
/// and this parameter has no enumerators to name.
template <>
constexpr DiscreteValues<SW::Engine::WindowFunction>::Strings
    DiscreteValues<SW::Engine::WindowFunction>::strings{
        "Hann",     "Hamming", "Blackman", "Blackman-Harris", "Gaussian",
        "Flat Top", "Welch",   "Triangle", "Rectangle"};

} // namespace Parameters

} // namespace LE

#endif // automatableParameters_hpp
