////////////////////////////////////////////////////////////////////////////////
///
/// exaggeratorImpl.cpp
/// -------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "exaggeratorImpl.hpp"

#include "le/math/math.hpp"
#include "le/math/vector.hpp"
#include "le/spectrumworx/engine/channelDataAmPh.hpp"
#include "le/spectrumworx/engine/setup.hpp"

#include <cmath>

namespace LE::SW::Effects
{

////////////////////////////////////////////////////////////////////////////////
//
// Exaggerator static member definitions.
//
////////////////////////////////////////////////////////////////////////////////

char const Exaggerator::title[] = "Exaggerator";
char const Exaggerator::description[] = "Emphasize or flatten spectral peaks.";

////////////////////////////////////////////////////////////////////////////////
//
// ExaggeratorImpl::setup()
// ------------------------
//
////////////////////////////////////////////////////////////////////////////////

void ExaggeratorImpl::setup(IndexRange const &, Engine::Setup const &)
{
    //exaggerate_  = -1 + 5 * (parameters().get<Exaggerate>() / 128.f + 0.5f);

    // Map [-100, 0, 100] to [-1, +1, 4]. Got this range from original
    // plugin made by Dobson.
    float const &exaggerate(parameters().get<Exaggerate>());
    float const scaleFactor(Math::isNegative(exaggerate) ? 50.0f : 33.333f);
    exaggerate_ = exaggerate / scaleFactor + 1;
}

////////////////////////////////////////////////////////////////////////////////
//
// ExageratorImpl::process()
// -------------------------
//
////////////////////////////////////////////////////////////////////////////////

void ExaggeratorImpl::process(Engine::ChannelData_AmPh data, Engine::Setup const &) const
{
    auto const maxamp(Math::max(data.amps()));
    if (!maxamp)
        return;

    float const exaggerate(exaggerate_);
    float normaliser(1 / maxamp);
    float postTotalAmp(0);
    float preTotalAmp(0);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note An empty bin stays empty, because `std::pow` says otherwise twice
    /// over and both answers are wrong here. `exaggerate_` spans [-1, 4]:
    ///
    ///   - `pow( 0, negative )` is `+inf`. One infinity in `postTotalAmp` makes
    ///     the normaliser zero, and `inf * 0` is NaN -- so a single empty bin
    ///     turns the whole spectrum into NaN, for the rest of the session.
    ///   - `pow( 0, 0 )` is `1`, which is an empty bin coming out at full scale.
    ///
    ///   An exact zero is not exotic: a preceding Bandpass, Sharper or Denoiser
    /// leaves whole regions at zero, which is exactly the company this effect
    /// keeps. A handful of shipped presets rendered NaN -- LE and ESS "Glass
    /// Rain", "Chime Dancing", "Glass Landscape" -- and every one of them has an
    /// Exaggerator in it. Found by rendering the whole bank, which nothing did:
    /// the corpus test loads presets without playing them and the goldens play
    /// one effect at its defaults, where the exponent is 1 and `pow` is identity.
    ///
    ///   Nothing else moves. For a positive exponent `pow( 0, e )` is already 0,
    /// so this changes the output only where the output was NaN or full scale.
    ///
    ////////////////////////////////////////////////////////////////////////////
    for (auto &amplitude : data.amps())
    {
        preTotalAmp += amplitude;
        if (amplitude != 0)
            amplitude = std::pow(amplitude * normaliser, exaggerate);
        postTotalAmp += amplitude;
    }

    /// \note `> 0` alone let an infinity through, which is how the NaN above
    /// reached the output rather than stopping here.
    LE_ASSERT(postTotalAmp > 0);
    LE_ASSERT(std::isfinite(postTotalAmp));
    normaliser = (preTotalAmp / postTotalAmp) / 2;

    Math::multiply(data.amps(), normaliser);
}

} // namespace LE::SW::Effects
