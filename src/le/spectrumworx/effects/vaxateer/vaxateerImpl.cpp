////////////////////////////////////////////////////////////////////////////////
///
/// vaxateerImpl.cpp
/// ----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "vaxateerImpl.hpp"

#include "le/spectrumworx/engine/channelDataAmPh.hpp"
#include "le/spectrumworx/engine/setup.hpp"
#include "le/math/conversion.hpp"
#include "le/math/math.hpp"
#include "le/math/vector.hpp"

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
//
// Vaxateer static member definitions.
//
////////////////////////////////////////////////////////////////////////////////

char const Vaxateer::title[] = "Vaxateer";
char const Vaxateer::description[] = "Combination based on RMS.";

////////////////////////////////////////////////////////////////////////////////
//
// VaxateerImpl::setup()
// ---------------------
//
////////////////////////////////////////////////////////////////////////////////

void VaxateerImpl::setup(IndexRange const &, Engine::Setup const &)
{
    rmsGain_ = Math::dB2NormalisedLinear(parameters().get<RMSGain>());
}

////////////////////////////////////////////////////////////////////////////////
//
// VaxateerImpl::process()
// -----------------------
//
////////////////////////////////////////////////////////////////////////////////

void VaxateerImpl::process(Engine::MainSideChannelData_AmPh data, Engine::Setup const &) const
{
    // Calculate RMS of the input signal:
    Engine::ChannelData_AmPh const *pRMSSource;
    switch (parameters().get<RMSTarget>().getValue())
    {
    case RMSTarget::MainRMS:
        pRMSSource = &data.main();
        break;
    case RMSTarget::SideRMS:
        pRMSSource = &data.side();
        break;
        LE_DEFAULT_CASE_UNREACHABLE();
    }

    float const thr(rmsGain_ * Math::rms(pRMSSource->amps()));

    // Setup threshold comparison sources:
    /// \note Cursors of our own, advanced alongside `data`: a range taken from
    /// the spectra does not follow the loop, so pointing at one pinned every
    /// comparison to bin zero. \see issue #21.
    ReadOnlyDataRange mainAmps(data.main().amps());
    ReadOnlyDataRange sideAmps(data.side().amps());
    ReadOnlyDataRange const threshold(&thr, &thr + 1);

    ReadOnlyDataRange const *pThresholdComparisonHigherSource;
    ReadOnlyDataRange const *pThresholdComparisonLowerSource;
    ReadOnlyDataRange const *pAmpSideComparisonHigherSource;
    ReadOnlyDataRange const *pAmpSideComparisonLowerSource;

    switch (parameters().get<Mode>().getValue())
    {
    case Mode::M1:
        pThresholdComparisonHigherSource = &mainAmps;
        pThresholdComparisonLowerSource = &threshold;
        pAmpSideComparisonHigherSource = &mainAmps;
        pAmpSideComparisonLowerSource = &sideAmps;
        break;

    case Mode::M2:
        pThresholdComparisonHigherSource = &mainAmps;
        pThresholdComparisonLowerSource = &threshold;
        pAmpSideComparisonHigherSource = &sideAmps;
        pAmpSideComparisonLowerSource = &mainAmps;
        break;

    case Mode::M3:
        pThresholdComparisonHigherSource = &threshold;
        pThresholdComparisonLowerSource = &mainAmps;
        pAmpSideComparisonHigherSource = &mainAmps;
        pAmpSideComparisonLowerSource = &sideAmps;
        break;

    case Mode::M4:
        pThresholdComparisonHigherSource = &threshold;
        pThresholdComparisonLowerSource = &mainAmps;
        pAmpSideComparisonHigherSource = &sideAmps;
        pAmpSideComparisonLowerSource = &mainAmps;
        break;

    case Mode::M5:
        pThresholdComparisonHigherSource = &sideAmps;
        pThresholdComparisonLowerSource = &threshold;
        pAmpSideComparisonHigherSource = &mainAmps;
        pAmpSideComparisonLowerSource = &sideAmps;
        break;

    case Mode::M6:
        pThresholdComparisonHigherSource = &sideAmps;
        pThresholdComparisonLowerSource = &threshold;
        pAmpSideComparisonHigherSource = &sideAmps;
        pAmpSideComparisonLowerSource = &mainAmps;
        break;

    case Mode::M7:
        pThresholdComparisonHigherSource = &threshold;
        pThresholdComparisonLowerSource = &sideAmps;
        pAmpSideComparisonHigherSource = &mainAmps;
        pAmpSideComparisonLowerSource = &sideAmps;
        break;

    case Mode::M8:
        pThresholdComparisonHigherSource = &threshold;
        pThresholdComparisonLowerSource = &sideAmps;
        pAmpSideComparisonHigherSource = &sideAmps;
        pAmpSideComparisonLowerSource = &mainAmps;
        break;

        LE_DEFAULT_CASE_UNREACHABLE();
    }

    while (data)
    {
        if ((*pThresholdComparisonHigherSource->begin() >
             *pThresholdComparisonLowerSource->begin()) &&
            (*pAmpSideComparisonHigherSource->begin() > *pAmpSideComparisonLowerSource->begin()))
        {
            data.main().amps().front() = data.side().amps().front();
        }

        ++data;
        mainAmps.advance_begin(1);
        sideAmps.advance_begin(1);
    }
}

} // namespace LE::SW::Effects
