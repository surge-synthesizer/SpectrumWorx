////////////////////////////////////////////////////////////////////////////////
///
/// tuneWorxImpl.cpp
/// ----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "tuneWorxImpl.hpp"

#include "le/spectrumworx/engine/channelDataAmPh.hpp"
#include "le/spectrumworx/engine/setup.hpp"
#include "le/math/conversion.hpp"
#include "le/math/math.hpp"

namespace LE::SW::Effects
{

// Papers/lectures:

// Discussions:

// 3rd party SDKs:
// - free:
// - commercial:

// 3rd party end products:
// - free:
// http://www.gvst.co.uk/gsnap.htm
// - commercial:
// http://www.antarestech.com/products/auto-tune_live.shtml

////////////////////////////////////////////////////////////////////////////////
//
// TuneWorx static member definitions.
//
////////////////////////////////////////////////////////////////////////////////

char const TuneWorx ::title[] = "TuneWorx";
char const TuneWorxPVD::title[] = "TuneWorx (PV)";

char const Detail::TuneWorxBase::description[] = "Chromatic scale pitch discretization.";
namespace Detail
{

void TuneWorxBaseImpl::setup(IndexRange const &, [[maybe_unused]] Engine::Setup const &engineSetup)
{
    // Tones:
    {
        Music::Scale::ToneOffsets &userTones(userScale_.toneOffsets());

        std::int8_t pos(0);

        if (parameters().get<Semi01>())
        {
            userTones[pos++] = 0;
        }
        if (parameters().get<Semi02>())
        {
            userTones[pos++] = 1;
        }
        if (parameters().get<Semi03>())
        {
            userTones[pos++] = 2;
        }
        if (parameters().get<Semi04>())
        {
            userTones[pos++] = 3;
        }
        if (parameters().get<Semi05>())
        {
            userTones[pos++] = 4;
        }
        if (parameters().get<Semi06>())
        {
            userTones[pos++] = 5;
        }
        if (parameters().get<Semi07>())
        {
            userTones[pos++] = 6;
        }
        if (parameters().get<Semi08>())
        {
            userTones[pos++] = 7;
        }
        if (parameters().get<Semi09>())
        {
            userTones[pos++] = 8;
        }
        if (parameters().get<Semi10>())
        {
            userTones[pos++] = 9;
        }
        if (parameters().get<Semi11>())
        {
            userTones[pos++] = 10;
        }
        if (parameters().get<Semi12>())
        {
            userTones[pos++] = 11;
        }
        std::int8_t const userNumTones(pos);
        std::int8_t const userBypassedTones(0);

        userScale_.tonesUpdated(userNumTones, userBypassedTones);
    }
}

float LE_NOINLINE TuneWorxBaseImpl::findNewPitchScale(Engine::ChannelData_AmPh const &data,
                                                      Engine::Setup const &engineSetup,
                                                      ChannelState &cs) const
{
    unsigned int const vibratoPitch(1);
    unsigned int const pitchShift_(1);

    if (userScale_.numberOfTones() == 0)
        return pitchShift_ * vibratoPitch;

    // Current pitch:
    float const pitch(PitchDetector::findPitch(data.full().amps(), cs, 70,
                                               70 * 2 * 2 * 2 * 2 * 2, // 5 octaves
                                               engineSetup));

    if (pitch == 0)
    {
        return pitchShift_ * vibratoPitch;
    }

    // Target tone - detected pitch snapped to user scale:
    float const freqScaletone(userScale_.snap2Scale(pitch, parameters().get<Key>()));

    float const pitchScale(freqScaletone / pitch);
    return pitchScale;
}

} // namespace Detail

////////////////////////////////////////////////////////////////////////////////
//
// TuneWorxImpl::process()
// -----------------------
//
////////////////////////////////////////////////////////////////////////////////

void TuneWorxImpl::process(ChannelState &cs, Engine::ChannelData_AmPh data,
                           Engine::Setup const &setup) const
{
    float const newPitch(TuneWorxBaseImpl::findNewPitchScale(data, setup, cs));
    PitchShifter::process(newPitch, cs, std::forward<Engine::ChannelData_AmPh>(data), setup);
}

////////////////////////////////////////////////////////////////////////////////
//
// TuneWorxPVDImpl::process()
// --------------------------
//
////////////////////////////////////////////////////////////////////////////////

void TuneWorxPVDImpl::process(ChannelState &cs, Engine::ChannelData_AmPh data,
                              Engine::Setup const &setup) const
{
    float const newPitch(TuneWorxBaseImpl::findNewPitchScale(data, setup, cs));
    PVPitchShifter::process(newPitch, std::forward<Engine::ChannelData_AmPh>(data), setup);
}

} // namespace LE::SW::Effects
