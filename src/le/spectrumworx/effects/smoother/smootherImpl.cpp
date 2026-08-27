////////////////////////////////////////////////////////////////////////////////
///
/// smootherImpl.cpp
/// ----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "smootherImpl.hpp"

#include "le/math/math.hpp"
#include "le/math/vector.hpp"
#include "le/spectrumworx/engine/channelDataAmPh.hpp"
#include "le/spectrumworx/engine/setup.hpp"
#include "le/utility/buffers.hpp"

#include "le/utility/stackBuffer.hpp"

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
//
// Smoother static member definitions.
//
////////////////////////////////////////////////////////////////////////////////

char const Smoother::title[] = "Smoother";
char const Smoother::description[] = "Smooth the spectrum.";

///////////////////////////////////////////////////////////////////////////////
//
// SmootherImpl::setup()
// ---------------------
//
////////////////////////////////////////////////////////////////////////////////

void SmootherImpl::setup(IndexRange const &, Engine::Setup const &engineSetup)
{
    /// \todo Reinvestigate and fix or document why do we send half the filter
    ///       length to the smoothing routine.
    ///                                       (19.10.2011.) (Domagoj Saric)
    filterLenHalf_ = engineSetup.frequencyInHzToBin(parameters().get<AveragingWidth>()) / 2;
}

////////////////////////////////////////////////////////////////////////////////
//
// SmootherImpl::process()
// -----------------------
//
////////////////////////////////////////////////////////////////////////////////

void SmootherImpl::process(Engine::ChannelData_AmPh data, Engine::Setup const &) const
{
    unsigned int filterLenHalf(filterLenHalf_);
    if (filterLenHalf == 0)
        return;
    LE_ALIGNED_STACK_BUFFER(workBuffer, Engine::real_t, data.size());
    //...mrmlj...
    unsigned int const dataSize(static_cast<unsigned int>(workBuffer.size()));
    if (dataSize < 4)
        return;
    if (filterLenHalf > dataSize / 2)
        filterLenHalf = dataSize / 2 - 1;
    Math::symmetricMovingAverage(data.amps(), workBuffer, filterLenHalf, true /*amplitudes*/);
    Math::copy(workBuffer, data.amps());
}

} // namespace LE::SW::Effects
