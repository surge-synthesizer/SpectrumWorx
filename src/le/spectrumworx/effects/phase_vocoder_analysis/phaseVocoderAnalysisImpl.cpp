////////////////////////////////////////////////////////////////////////////////
///
/// phaseVocoderAnalysisImpl.cpp
/// ----------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "phaseVocoderAnalysisImpl.hpp"

#include "le/spectrumworx/engine/channelDataAmPh.hpp"
#include "le/spectrumworx/engine/setup.hpp"

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
//
// PhaseVocoderAnalysis static member definitions.
//
////////////////////////////////////////////////////////////////////////////////

char const PhaseVocoderAnalysis::title[] = "To PV";
char const PhaseVocoderAnalysis::description[] = "Transform signal into phase vocoder domain.";

////////////////////////////////////////////////////////////////////////////////
//
// PhaseVocoderAnalysisImpl::setup()
// ---------------------------------
//
////////////////////////////////////////////////////////////////////////////////

void PhaseVocoderAnalysisImpl::setup(IndexRange const &, Engine::Setup const &engineSetup)
{
    pvParameters_.setup(engineSetup);
}

////////////////////////////////////////////////////////////////////////////////
//
// PhaseVocoderAnalysisImpl::process()
// -----------------------------------
//
////////////////////////////////////////////////////////////////////////////////

void PhaseVocoderAnalysisImpl::process(ChannelState &state, Engine::ChannelData_AmPh data,
                                       Engine::Setup const &) const
{
    PhaseVocoderShared::analysis(state, data.full(), pvParameters_);
}

} // namespace LE::SW::Effects
