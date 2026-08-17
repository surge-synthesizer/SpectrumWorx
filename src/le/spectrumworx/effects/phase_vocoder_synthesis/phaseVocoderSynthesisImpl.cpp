////////////////////////////////////////////////////////////////////////////////
///
/// phaseVocoderSynthesis.cpp
/// -------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "phaseVocoderSynthesisImpl.hpp"

#include "le/spectrumworx/engine/channelDataAmPh.hpp"
#include "le/spectrumworx/engine/setup.hpp"

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
//
// PhaseVocoderSynthesis static member definitions.
//
////////////////////////////////////////////////////////////////////////////////

char const PhaseVocoderSynthesis::title[] = "From PV";
char const PhaseVocoderSynthesis::description[] =
    "Transform signal back from the phase vocoder domain.";

////////////////////////////////////////////////////////////////////////////////
//
// PhaseVocoderSynthesisImpl::process()
// ------------------------------------
//
////////////////////////////////////////////////////////////////////////////////

void PhaseVocoderSynthesisImpl::process(ChannelState &state, Engine::ChannelData_AmPh data,
                                        Engine::Setup const &) const
{
    PhaseVocoderShared::synthesis(state, data.full().phases(), pvParameters_);
}

void PhaseVocoderSynthesisImpl::setup(IndexRange const &, Engine::Setup const &engineSetup)
{
    pvParameters_.setup(engineSetup);
}

} // namespace LE::SW::Effects
