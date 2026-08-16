////////////////////////////////////////////////////////////////////////////////
///
/// pitchShifterImpl.cpp
/// --------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "pitchShifterImpl.hpp"

#include "le/spectrumworx/engine/channelDataAmPh.hpp"
#include "le/spectrumworx/engine/setup.hpp"

#include <cstdint>

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
//
// PitchShifter static member definitions.
//
////////////////////////////////////////////////////////////////////////////////

char const PitchShifter ::title[] = "Pitch Shifter";
char const PVPitchShifter::title[] = "Pitch Shifter (PV)";

char const Detail::PitchShifterBase::description[] = "Pitch shifter.";

////////////////////////////////////////////////////////////////////////////////
//
// PitchShifterImpl::setup()
// -------------------------
//
////////////////////////////////////////////////////////////////////////////////

namespace
{
void setPitchScale(PhaseVocoderShared::PitchShiftParameters &pitchShiftParameters,
                   Detail::PitchShifterBase::Parameters const &parameters,
                   std::uint16_t const numberOfBins)
{
    using namespace Detail;
    float const pitchScale(PhaseVocoderShared::PitchShiftParameters::scaleFromSemiTonesAndCents(
        parameters.get<PitchShifterBase::SemiTones>(), parameters.get<PitchShifterBase::Cents>()));
    pitchShiftParameters.setScalingFactor(pitchScale, numberOfBins);
}
} // anonymous namespace

void PitchShifterImpl::setup(IndexRange const &, Engine::Setup const &engineSetup)
{
    PhaseVocoderShared::PitchShifter::setup(engineSetup);
    setPitchScale(pitchShiftParameters(), parameters(), engineSetup.numberOfBins());
}

void PVPitchShifterImpl::setup(IndexRange const &, Engine::Setup const &engineSetup)
{
    setPitchScale(pitchShiftParameters(), parameters(), engineSetup.numberOfBins());
}

void PVPitchShifterImpl::process(Engine::ChannelData_AmPh data, Engine::Setup const &) const
{
    PhaseVocoderShared::PVPitchShifter::process(std::forward<Engine::ChannelData_AmPh>(data));
}

} // namespace LE::SW::Effects
