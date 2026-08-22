////////////////////////////////////////////////////////////////////////////////
///
/// \file whispererImpl.hpp
/// -----------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef whispererImpl_hpp__7E88348A_B8DC_4D2C_B805_7E845F3B5ACF
#define whispererImpl_hpp__7E88348A_B8DC_4D2C_B805_7E845F3B5ACF
//------------------------------------------------------------------------------
#include "whisperer.hpp"

#include "le/math/math.hpp"
#include "le/spectrumworx/effects/channelStateStatic.hpp"
#include "le/spectrumworx/effects/effects.hpp"

namespace LE::SW::Effects
{

class WhispererImpl : public NoParametersEffectImpl<Whisperer>
{
  public: // LE::Effect required interface.
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief One phase-randomising stream per channel, and the only reason this
    /// effect has a ChannelState at all.
    ///
    ///   The draws used to come from a process-global generator, which made the
    /// output depend on how many times `process()` was called: the engine runs
    /// every hop of channel 0 before channel 1 starts, so cutting a host block
    /// into hop-sized pieces left the draw *count* untouched and changed which
    /// channel received which number. \see issue #86.
    ///
    ////////////////////////////////////////////////////////////////////////////
    struct ChannelState : StaticChannelState
    {
        Math::Rng rng;

        /// \note Not a reseed -- see Math::Rng. The stream simply carries on.
        static void reset() {}

        void seed(std::uint64_t const seed) { rng.seed(seed); }
    }; // struct ChannelState

    ////////////////////////////////////////////////////////////////////////////
    // setup() and process()
    ////////////////////////////////////////////////////////////////////////////

    static void setup(IndexRange const &, Engine::Setup const &) {}
    void process(ChannelState &, Engine::ChannelData_AmPh, Engine::Setup const &) const;
};

} // namespace LE::SW::Effects

#endif // whispererImpl_hpp
