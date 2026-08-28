////////////////////////////////////////////////////////////////////////////////
///
/// etherealImpl.cpp
/// ----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "etherealImpl.hpp"

#include "le/spectrumworx/engine/channelDataAmPh.hpp"
#include "le/math/conversion.hpp"

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
//
// Ethereal static member definitions.
//
////////////////////////////////////////////////////////////////////////////////

char const Ethereal::title[] = "Ethereal";
char const Ethereal::description[] = "Compare and replace.";

////////////////////////////////////////////////////////////////////////////////
//
// EtherealImpl::setup()
// ---------------------
//
////////////////////////////////////////////////////////////////////////////////

void EtherealImpl::setup(IndexRange const &, Engine::Setup const &)
{
    threshold_ = Math::dB2NormalisedLinear(parameters().get<Threshold>());

    mode_.unpack(parameters().get<Mode>());
}

////////////////////////////////////////////////////////////////////////////////
//
// EtherealImpl::process()
// -----------------------
//
////////////////////////////////////////////////////////////////////////////////

void EtherealImpl::process(Engine::MainSideChannelData_AmPh data, Engine::Setup const &) const
{
    bool const replaceWhenWeaker(parameters().get<Condition>() == Condition::DiffLower);

    float const threshold(threshold_);

    /// \note Which side to read, rather than a range to read it from: a range
    /// picked before the loop does not advance with it. \see issue #21.
    bool const takeSideAmps(mode_.magnitudes());
    bool const takeSidePhases(mode_.phases());

    while (data)
    {
        bool const sideIsWeaker(data.side().amps().front() <
                                (data.main().amps().front() * threshold));
        bool const shouldReplace(sideIsWeaker == replaceWhenWeaker);

        if (shouldReplace)
        {
            if (takeSideAmps)
                data.main().amps().front() = data.side().amps().front();
            if (takeSidePhases)
                data.main().phases().front() = data.side().phases().front();
        }

        ++data;
    }
}

} // namespace LE::SW::Effects
