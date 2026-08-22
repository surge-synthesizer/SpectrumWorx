////////////////////////////////////////////////////////////////////////////////
///
/// bandpassImpl.cpp
/// ----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "bandpassImpl.hpp"

#include "le/spectrumworx/effects/indexRange.hpp"
#include "le/spectrumworx/engine/channelDataAmPh.hpp"
#include "le/spectrumworx/engine/setup.hpp"
#include "le/math/conversion.hpp"
#include "le/math/math.hpp"
#include "le/math/vector.hpp"
#include "le/math/windows.hpp"

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
//
// Bandpass static member definitions.
//
////////////////////////////////////////////////////////////////////////////////

char const Bandpass::title[] = "Bandpass";
char const Bandpass::description[] = "Band-pass filter.";

////////////////////////////////////////////////////////////////////////////////
//
// Bandstop static member definitions.
//
////////////////////////////////////////////////////////////////////////////////

char const Bandstop::title[] = "Bandstop";
char const Bandstop::description[] = "Band-stop filter.";

////////////////////////////////////////////////////////////////////////////////
//
// Detail::BandGainImpl::setup()
// -----------------------------
//
////////////////////////////////////////////////////////////////////////////////

/// \note A flat attenuation, with no windowed roll-off either side of the band.
/// A windowed variant was tried and abandoned; if it comes back it must not come
/// back behind a macro that only one compiler's debug build defines,
/// which is how Bandpass and Bandstop came to produce different audio there than
/// everywhere else with the goldens unable to see it.
void Detail::BandGainImpl::setup(IndexRange const &, Engine::Setup const &)
{
    attenuation_ = Math::dB2NormalisedLinear(-parameters().get<Attenuation>());
}

////////////////////////////////////////////////////////////////////////////////
//
// BandpassImpl::process()
// -----------------------
//
////////////////////////////////////////////////////////////////////////////////

void BandpassImpl::process(Engine::ChannelData_AmPh data, Engine::Setup const &) const
{
    using namespace Math;

    multiply(attenuation_, data.full().amps().begin(), data.amps().begin());
    multiply(attenuation_, data.amps().end(), data.full().amps().end());
}

////////////////////////////////////////////////////////////////////////////////
//
// BandstopImpl::process()
// -----------------------
//
////////////////////////////////////////////////////////////////////////////////

void BandstopImpl::process(Engine::ChannelData_AmPh data, Engine::Setup const &) const
{
    Math::multiply(data.amps(), attenuation_);
}

} // namespace LE::SW::Effects
