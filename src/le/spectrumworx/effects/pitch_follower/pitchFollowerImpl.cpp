////////////////////////////////////////////////////////////////////////////////
///
/// pitchFollowerImpl.cpp
/// ---------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "pitchFollowerImpl.hpp"

#include "le/spectrumworx/engine/channelDataAmPh.hpp"
#include "le/spectrumworx/engine/setup.hpp"
#include "le/math/conversion.hpp"

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
//
// PitchFollower static member definitions.
//
////////////////////////////////////////////////////////////////////////////////

char const PitchFollower ::title[] = "Pitch Follower";
char const PitchFollowerPVD::title[] = "Pitch Follower (PV)";

char const Detail::PitchFollowerBase::description[] = "Follow side channel's pitch.";

namespace Detail
{
void PitchFollowerBaseImpl::setup(IndexRange const &, Engine::Setup const &engineSetup)
{
    pitchChangeLimitSemitones_ = parameters().get<Speed>() / engineSetup.stepsPerSecond();
}

float PitchFollowerBaseImpl::findTargetPitch(ChannelState &cs,
                                             Engine::MainSideChannelData_AmPh const &data,
                                             Engine::Setup const &setup) const
{
    float const estimatedPitchMain(
        PitchDetector::findPitch(data.full().main().amps(), cs, 70, 7000, setup));
    float const estimatedPitchSide(
        PitchDetector::findPitch(data.full().side().amps(), cs, 70, 7000, setup));

    float pitchScale((estimatedPitchMain != 0.0f && estimatedPitchSide != 0.0f)
                         ? estimatedPitchSide / estimatedPitchMain
                         : 1);

    float pitchScaleSemitones(Math::interval12TET2Semitone(pitchScale));

    //if( pitchChangeLimitSemitones_!=0.0f )
    {
        if (pitchScaleSemitones > cs.prevPitchScaleSemitones)
        {
            if ((pitchScaleSemitones - cs.prevPitchScaleSemitones) > pitchChangeLimitSemitones_)
                pitchScaleSemitones = cs.prevPitchScaleSemitones + pitchChangeLimitSemitones_;
        }
        else
        {
            if ((cs.prevPitchScaleSemitones - pitchScaleSemitones) > pitchChangeLimitSemitones_)
                pitchScaleSemitones = cs.prevPitchScaleSemitones - pitchChangeLimitSemitones_;
        }

        cs.prevPitchScaleSemitones = pitchScaleSemitones;
        pitchScale = Math::semitone2Interval12TET(pitchScaleSemitones);
    }

    return pitchScale;
}

void PitchFollowerBaseImpl::ChannelState::reset()
{
    PitchDetector::ChannelState::reset();
    prevPitchScaleSemitones = 0;
}
} // namespace Detail

////////////////////////////////////////////////////////////////////////////////
//
// PitchFollowerImpl::process()
// ----------------------------
//
////////////////////////////////////////////////////////////////////////////////

void PitchFollowerImpl::process(ChannelState &cs, Engine::MainSideChannelData_AmPh data,
                                Engine::Setup const &setup) const
{
    float const scale(PitchFollowerBaseImpl::findTargetPitch(cs, data, setup));
    PitchShifter::process(scale, cs, std::forward<Engine::ChannelData_AmPh>(data.main()), setup);
}

void PitchFollowerPVDImpl::process(ChannelState &cs, Engine::MainSideChannelData_AmPh data,
                                   Engine::Setup const &setup) const
{
    float const scale(PitchFollowerBaseImpl::findTargetPitch(cs, data, setup));
    PVPitchShifter::process(scale, std::forward<Engine::ChannelData_AmPh>(data.main()), setup);
}

} // namespace LE::SW::Effects
