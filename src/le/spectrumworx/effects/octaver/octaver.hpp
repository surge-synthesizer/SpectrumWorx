////////////////////////////////////////////////////////////////////////////////
///
/// \file octaver.hpp
/// -----------------
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef octaver_hpp__9579DBDB_C9A8_486D_985B_DB6955708192
#define octaver_hpp__9579DBDB_C9A8_486D_985B_DB6955708192
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/symmetric/parameter.hpp"
#include "le/parameters/enumerated/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Octaver
///
/// \ingroup Effects
///
/// \brief Adds two octaves.
///
/// This module adds two (or one) octaves to the input signal. Range is up or
/// down two octaves. Each octave has Gain control and cut-off frequency is
/// shared between both octaves.
///
////////////////////////////////////////////////////////////////////////////////

struct Octaver
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Two octaves that are not the same octave, which is what an Octaver
    /// dropped into a slot ought to be doing: both rested on `Down2` because
    /// zero was the only default an enumerated parameter could have, so the
    /// second one added nothing the first had not. The nearer octave first and
    /// the further one under it. \see issue #163 and
    /// LE_ENUMERATED_PARAMETER_DEFAULTING_TO, which is what it asked for.
    ///
    /// \note The same argument as the cutoff below, and the same blast radius: a
    /// 2.x preset writes every parameter its effect has, so no committed file
    /// moves with this. What moves is a freshly inserted Octaver -- and the
    /// golden fixtures, which render every effect at its defaults.
    ///
    ////////////////////////////////////////////////////////////////////////////

    LE_ENUMERATED_PARAMETER_DEFAULTING_TO(Octave1, Down1, Down2, Down1, Off, Up1, Up2);
    LE_ENUMERATED_PARAMETER(Octave2, Down2, Down1, Off, Up1, Up2);

    LE_DEFINE_PARAMETER(GainOctave1, LinearFloat, Minimum<-48>, Maximum<+24>, Default<0>,
                        Unit<" dB">);
    LE_DEFINE_PARAMETER(GainOctave2, LinearFloat, Minimum<-48>, Maximum<+24>, Default<0>,
                        Unit<" dB">);
    LE_DEFINE_PARAMETER(CutoffFrequency, LinearUnsignedInteger, Minimum<0>, Maximum<16000>,
                        Default<16000>, Unit<" Hz">);
    LE_DEFINE_PARAMETERS(Octave1, GainOctave1, Octave2, GainOctave2, CutoffFrequency);

    /// \typedef Octave1
    /// \brief Controls the first octave to be added.
    /// \details
    ///   - Down 2: adds an octave which is 2 octaves down.
    ///   - Down 1: adds an octave which is 1 octaves down.
    ///   - Off   : no octave added.
    ///   - Up   1: adds an octave which is 1 octaves up.
    ///   - Up   2: adds an octave which is 2 octaves up.
    /// \typedef GainOctave1
    /// \brief Controls the first octave's gain.
    /// \typedef Octave2
    /// \brief Controls the second octave to be added.
    /// \details
    ///   - Down 2: adds an octave which is 2 octaves down.
    ///   - Down 1: adds an octave which is 1 octaves down.
    ///   - Off   : no octave added.
    ///   - Up   1: adds an octave which is 1 octaves up.
    ///   - Up   2: adds an octave which is 2 octaves up.
    /// \typedef GainOctave2
    /// \brief Controls the second octave's gain.
    /// \typedef CutoffFrequency
    /// \brief Low passes the module's *output* -- the added octaves and the
    ///        signal they were mixed into alike, rather than the octaves alone.
    /// \note Defaulted to 350 Hz until 19.08.2026, which meant an Octaver
    ///       dropped into a slot removed most of what it had just added: the
    ///       up-octave of anything above F3 was cut. It now rests at its maximum.
    ///       No 2.x preset moves with it -- that grammar writes every parameter
    ///       an effect has, so a file naming an Octaver names this too. Issue #15.

    static char const title[];
    static char const description[];
};

////////////////////////////////////////////////////////////////////////////////
//
// Octaver UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

EFFECT_PARAMETER_NAME(Octaver::Octave1, "Octave 1");
EFFECT_PARAMETER_NAME(Octaver::GainOctave1, "Gain 1");
EFFECT_PARAMETER_NAME(Octaver::Octave2, "Octave 2");
EFFECT_PARAMETER_NAME(Octaver::GainOctave2, "Gain 2");
EFFECT_PARAMETER_NAME(Octaver::CutoffFrequency, "Lowpass");

EFFECT_PARAMETER_STREAMING_NAME(Octaver::CutoffFrequency, "Low pass");

EFFECT_ENUMERATED_PARAMETER_STRINGS(Octaver, Octave1,
    {Down2, "-2"},
    {Down1, "-1"},
    {Off, "0"},
    {Up1, "+1"},
    {Up2, "+2"})

EFFECT_ENUMERATED_PARAMETER_STRINGS(Octaver, Octave2,
    {Down2, "-2"},
    {Down1, "-1"},
    {Off, "0"},
    {Up1, "+1"},
    {Up2, "+2"})

} // namespace LE::SW::Effects

#endif // octaver_hpp
