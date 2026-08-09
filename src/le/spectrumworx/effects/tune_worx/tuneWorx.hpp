////////////////////////////////////////////////////////////////////////////////
///
/// \file tuneWorx.hpp
/// ------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef tuneWorx_hpp__08D752F7_70CC_436C_8C8A_BE59A2A4900D
#define tuneWorx_hpp__08D752F7_70CC_436C_8C8A_BE59A2A4900D
#if defined(_MSC_VER) && !defined(DOXYGEN_ONLY)
#endif // MSVC && !Doxygen
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/commonParameters.hpp"
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/boolean/parameter.hpp"
#include "le/parameters/enumerated/parameter.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/symmetric/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

namespace Detail
{
struct TuneWorxBase ///<
{
    /// \name Parameters
    /// @{
    typedef CommonParameters::SpringType SpringType;
    /// @}

    LE_ENUMERATED_PARAMETER(Key, A, Ais, B, C, Cis, D, Dis, E, F, Fis, G, Gis);

    LE_DEFINE_PARAMETER(Semi01, Boolean);
    LE_DEFINE_PARAMETER(Semi02, Boolean);
    LE_DEFINE_PARAMETER(Semi03, Boolean);
    LE_DEFINE_PARAMETER(Semi04, Boolean);
    LE_DEFINE_PARAMETER(Semi05, Boolean);
    LE_DEFINE_PARAMETER(Semi06, Boolean);
    LE_DEFINE_PARAMETER(Semi07, Boolean);
    LE_DEFINE_PARAMETER(Semi08, Boolean);
    LE_DEFINE_PARAMETER(Semi09, Boolean);
    LE_DEFINE_PARAMETER(Semi10, Boolean);
    LE_DEFINE_PARAMETER(Semi11, Boolean);
    LE_DEFINE_PARAMETER(Semi12, Boolean);
    LE_DEFINE_PARAMETER(BypassSemi01, Boolean);
    LE_DEFINE_PARAMETER(BypassSemi02, Boolean);
    LE_DEFINE_PARAMETER(BypassSemi03, Boolean);
    LE_DEFINE_PARAMETER(BypassSemi04, Boolean);
    LE_DEFINE_PARAMETER(BypassSemi05, Boolean);
    LE_DEFINE_PARAMETER(BypassSemi06, Boolean);
    LE_DEFINE_PARAMETER(BypassSemi07, Boolean);
    LE_DEFINE_PARAMETER(BypassSemi08, Boolean);
    LE_DEFINE_PARAMETER(BypassSemi09, Boolean);
    LE_DEFINE_PARAMETER(BypassSemi10, Boolean);
    LE_DEFINE_PARAMETER(BypassSemi11, Boolean);
    LE_DEFINE_PARAMETER(BypassSemi12, Boolean);
    LE_DEFINE_PARAMETER(Vibrato, Boolean);
    LE_DEFINE_PARAMETER(PitchMinFreq, LinearFloat, Minimum<50>, Maximum<2000>, Default<70>,
                        Unit<"Hz">);
    LE_DEFINE_PARAMETER(PitchMaxFreq, LinearFloat, Minimum<100>, Maximum<10000>, Default<2000>,
                        Unit<"Hz">);
    LE_DEFINE_PARAMETER(TuneTolerance, LinearFloat, Minimum<0>, Maximum<50>, Default<0>,
                        Unit<"Hz">);
    LE_DEFINE_PARAMETER(RetuneTime, LinearUnsignedInteger, Minimum<0>, Maximum<500>, Default<50>,
                        Unit<"ms">);
    LE_DEFINE_PARAMETER(VibratoDelay, LinearUnsignedInteger, Minimum<0>, Maximum<1000>,
                        Default<100>, Unit<"ms">);
    LE_DEFINE_PARAMETER(VibratoPeriod, LinearUnsignedInteger, Minimum<10>, Maximum<250>,
                        Default<100>, Unit<"ms">);
    LE_DEFINE_PARAMETER(VibratoDepth, LinearUnsignedInteger, Minimum<0>, Maximum<100>, Default<50>,
                        Unit<"\"">);
    LE_DEFINE_PARAMETER(PitchShift, SymmetricInteger, MaximumOffset<1200>, Default<0>, Unit<"\"">);
    LE_DEFINE_PARAMETERS(Key, SpringType, Semi01, Semi02, Semi03, Semi04, Semi05, Semi06, Semi07,
                         Semi08, Semi09, Semi10, Semi11, Semi12, BypassSemi01, BypassSemi02,
                         BypassSemi03, BypassSemi04, BypassSemi05, BypassSemi06, BypassSemi07,
                         BypassSemi08, BypassSemi09, BypassSemi10, BypassSemi11, BypassSemi12,
                         Vibrato, PitchMinFreq, PitchMaxFreq, TuneTolerance, RetuneTime,
                         VibratoDelay, VibratoPeriod, VibratoDepth, PitchShift);

    /// \typedef Key
    /// \brief chromatic scale root tone (default "A").
    /// \details
    ///   - A  : set root tone to A.
    ///   - Ais: set root tone to Ais.
    ///   - B  : set root tone to B.
    ///   - C  : set root tone to C.
    ///   - Cis: set root tone to Cis.
    ///   - D  : set root tone to D.
    ///   - Dis: set root tone to Dis.
    ///   - E  : set root tone to E.
    ///   - F  : set root tone to F.
    ///   - Fis: set root tone to Fis.
    ///   - G  : set root tone to G.
    ///   - Gis: set root tone to Gis.
    /// \typedef SpringType
    /// \brief Type of vibrato.
    /// \typedef Semi01
    /// \brief Snap to semitone  1 on the chromatic scale.
    /// \typedef Semi02
    /// \brief Snap to semitone  2 on the chromatic scale.
    /// \typedef Semi03
    /// \brief Snap to semitone  3 on the chromatic scale.
    /// \typedef Semi04
    /// \brief Snap to semitone  4 on the chromatic scale.
    /// \typedef Semi05
    /// \brief Snap to semitone  5 on the chromatic scale.
    /// \typedef Semi06
    /// \brief Snap to semitone  6 on the chromatic scale.
    /// \typedef Semi07
    /// \brief Snap to semitone  7 on the chromatic scale.
    /// \typedef Semi08
    /// \brief Snap to semitone  8 on the chromatic scale.
    /// \typedef Semi09
    /// \brief Snap to semitone  9 on the chromatic scale.
    /// \typedef Semi10
    /// \brief Snap to semitone 10 on the chromatic scale.
    /// \typedef Semi11
    /// \brief Snap to semitone 11 on the chromatic scale.
    /// \typedef Semi12
    /// \brief Snap to semitone 12 on the chromatic scale.
    /// \typedef BypassSemi01
    /// \brief Do not autotune if detected pitch is closest to semitone  1 on the chromatic scale.
    /// \typedef BypassSemi02
    /// \brief Do not autotune if detected pitch is closest to semitone  2 on the chromatic scale.
    /// \typedef BypassSemi03
    /// \brief Do not autotune if detected pitch is closest to semitone  3 on the chromatic scale.
    /// \typedef BypassSemi04
    /// \brief Do not autotune if detected pitch is closest to semitone  4 on the chromatic scale.
    /// \typedef BypassSemi05
    /// \brief Do not autotune if detected pitch is closest to semitone  5 on the chromatic scale.
    /// \typedef BypassSemi06
    /// \brief Do not autotune if detected pitch is closest to semitone  6 on the chromatic scale.
    /// \typedef BypassSemi07
    /// \brief Do not autotune if detected pitch is closest to semitone  7 on the chromatic scale.
    /// \typedef BypassSemi08
    /// \brief Do not autotune if detected pitch is closest to semitone  8 on the chromatic scale.
    /// \typedef BypassSemi09
    /// \brief Do not autotune if detected pitch is closest to semitone  9 on the chromatic scale.
    /// \typedef BypassSemi10
    /// \brief Do not autotune if detected pitch is closest to semitone 10 on the chromatic scale.
    /// \typedef BypassSemi11
    /// \brief Do not autotune if detected pitch is closest to semitone 11 on the chromatic scale.
    /// \typedef BypassSemi12
    /// \brief Do not autotune if detected pitch is closest to semitone 12 on the chromatic scale.
    /// \typedef Vibrato
    /// \brief Add vibrato to output signal.
    /// \typedef VibratoDelay
    /// \brief Waits for signal to be stable at a certain tone before
    /// vibrato is applied.
    /// \typedef VibratoPeriod
    /// \brief Period of added vibrato in ms.
    /// \typedef VibratoDepth
    /// \brief Maximum vibrato pitch variation.
    /// \typedef PitchMinFreq
    /// \brief Searches for pitch only at frequencies higher than
    /// PitchMinFreq.
    /// \typedef PitchMaxFreq
    /// \brief Searches for pitch only at frequencies lower than
    /// PitchMaxFreq.
    /// \typedef TuneTolerance
    /// \brief Do not autotune if the detected pitch is closer to the
    /// targeted note than desired tolerance in Hz.
    /// \typedef RetuneTime
    /// \brief Set time for autotune in ms (0 = instant change, "Cher
    /// effect").
    /// \typedef PitchShift
    /// \brief Pitch shifts output by the specified number of cents.
    static char const description[];
};
} // namespace Detail

////////////////////////////////////////////////////////////////////////////////
///
/// \class TuneWorx
///
/// \ingroup Effects
///
/// \brief Auto-tuner.
///
/// This is the classic Autotune effect. The main channel's pitch is detected
/// and shifted to the nearest selected semitone.
///
////////////////////////////////////////////////////////////////////////////////

struct TuneWorx : Detail::TuneWorxBase
{
    static char const title[];
};

////////////////////////////////////////////////////////////////////////////////
///
/// \class TuneWorxPVD
///
/// \ingroup Effects
///
/// \brief Auto-tuner.
///
////////////////////////////////////////////////////////////////////////////////

struct TuneWorxPVD : Detail::TuneWorxBase
{
    static char const title[];
};

EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::Key, "Key")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::Semi01, "1")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::Semi02, "2")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::Semi03, "3")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::Semi04, "4")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::Semi05, "5")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::Semi06, "6")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::Semi07, "7")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::Semi08, "8")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::Semi09, "9")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::Semi10, "10")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::Semi11, "11")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::Semi12, "12")
/// \note These were behind #ifdef LE_SW_SDK_BUILD, so until then no plugin build
/// had a name for them, and they shared the placeholder "N/A" -- which
/// ParameterInfo carries into the preset, because presets serialise by name.
/// Saving a TuneWorx preset therefore wrote twenty one elements called `<N/A>`,
/// which is not even a legal XML name: reading one back failed the parse
/// outright. The names below are the ones that were commented out beside each.
///
///   Safe by stage 0.7's rule, which forbids renaming anything a preset file
/// references: no preset file can reference "N/A", because the 2011 banks
/// predate every one of these parameters. The round-trip test is what noticed.
///                                           (31.07.2026.) (SW port)
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::BypassSemi01, "Bypass 1")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::BypassSemi02, "Bypass 2")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::BypassSemi03, "Bypass 3")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::BypassSemi04, "Bypass 4")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::BypassSemi05, "Bypass 5")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::BypassSemi06, "Bypass 6")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::BypassSemi07, "Bypass 7")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::BypassSemi08, "Bypass 8")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::BypassSemi09, "Bypass 9")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::BypassSemi10, "Bypass 10")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::BypassSemi11, "Bypass 11")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::BypassSemi12, "Bypass 12")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::PitchMinFreq, "Pitch range lower bound")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::PitchMaxFreq, "Pitch range upper bound")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::TuneTolerance, "Out of tune tolerance")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::RetuneTime, "Retune time")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::PitchShift, "Pitch shift")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::Vibrato, "Vibrato")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::VibratoDepth, "Vibrato extent")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::VibratoPeriod, "Vibrato rate")
EFFECT_PARAMETER_NAME(Detail::TuneWorxBase::VibratoDelay, "Vibrato delay")

EFFECT_ENUMERATED_PARAMETER_STRINGS(Detail::TuneWorxBase, Key,
    {A, "A"},
    {Ais, "A#"},
    {B, "B"},
    {C, "C"},
    {Cis, "C#"},
    {D, "D"},
    {Dis, "D#"},
    {E, "E"},
    {F, "F"},
    {Fis, "F#"},
    {G, "G"},
    {Gis, "G#"})

} // namespace LE::SW::Effects

#endif // tuneWorx_hpp
