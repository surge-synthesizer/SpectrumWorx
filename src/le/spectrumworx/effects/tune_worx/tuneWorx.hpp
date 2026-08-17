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
#include "le/spectrumworx/effects/parameters.hpp"
#include "le/parameters/boolean/parameter.hpp"
#include "le/parameters/enumerated/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

namespace LE::SW::Effects
{

namespace Detail
{
/// \note Thirteen parameters, not the thirty four the LE framework's Tune Worx
/// declared. The 2016 plugin built this effect with LE_SIMPLE_TUNEWORX -- "SW
/// plugin uses the old/'simple' version of TuneWorx", said the note beside the
/// define in the build file -- and the rest of the framework's parameters went
/// with a product this repository does not contain. The implementation here is
/// the matching one: it reads Key and the twelve semitones and nothing else, so
/// declaring Direction, Bypass 1..12, the vibrato group, the pitch range,
/// Retune time and Pitch shift exports twenty one parameters that no DSP reads,
/// pushes the semitones out of the ten a host can address and shows a second
/// family of controls named "Bypass" beside the module's own.
///
///   That is not hypothetical: the define lived in a cmake file the clap-first
/// build stopped including, the arm it selected was then deleted as dead, and
/// the extra parameters reached a release. Do not restore them without the
/// implementation to match.
///                                           (11.08.2026.) (SW port)
struct TuneWorxBase ///<
{
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
    LE_DEFINE_PARAMETERS(Key, Semi01, Semi02, Semi03, Semi04, Semi05, Semi06, Semi07, Semi08,
                         Semi09, Semi10, Semi11, Semi12);

    /// \typedef Key
    /// \brief chromatic scale root tone (default "A").
    /// \details
    ///   - A  : set root tone to A.
    ///   - Ais: set root tone to A#/Bb.
    ///   - B  : set root tone to B.
    ///   - C  : set root tone to C.
    ///   - Cis: set root tone to C#/Db.
    ///   - D  : set root tone to D.
    ///   - Dis: set root tone to D#/Eb.
    ///   - E  : set root tone to E.
    ///   - F  : set root tone to F.
    ///   - Fis: set root tone to F#/Gb.
    ///   - G  : set root tone to G.
    ///   - Gis: set root tone to G#/Ab.
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

////////////////////////////////////////////////////////////////////////////////
///
/// \note Both spellings of each black key, sharp first. The list is chromatic
/// rather than diatonic -- a root is picked from all twelve -- so there is no key
/// signature here to settle whether one of them is A sharp or B flat, and naming
/// only the sharp made the plugin unreadable to anybody working in flats.
/// Neither spelling is the right one to pick, so both are given. \see issue #89.
///
/// \note **The order here is the parameter's, and the order the menu shows is
/// not.** A value is an index: it is what a `.swp` stores, what a host automates
/// and what the DSP adds to a note offset off a 27.5 Hz A
/// (musicalScales.cpp:109), so A stays zero. What starts at C is the combo box,
/// which lists the same twelve values in musical order --
/// `fillComboBoxForParameter< Key >` in gui/modules/moduleWidgets.cpp.
///                                           (17.08.2026.)
///
////////////////////////////////////////////////////////////////////////////////

EFFECT_ENUMERATED_PARAMETER_STRINGS(Detail::TuneWorxBase, Key,
    {A, "A"},
    {Ais, "A#/Bb"},
    {B, "B"},
    {C, "C"},
    {Cis, "C#/Db"},
    {D, "D"},
    {Dis, "D#/Eb"},
    {E, "E"},
    {F, "F"},
    {Fis, "F#/Gb"},
    {G, "G"},
    {Gis, "G#/Ab"})

} // namespace LE::SW::Effects

#endif // tuneWorx_hpp
