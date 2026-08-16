////////////////////////////////////////////////////////////////////////////////
///
/// \file freezeImpl.hpp
/// --------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef freezeImpl_hpp__1C855F51_D0CA_48B9_8F40_4B259941C1F9
#define freezeImpl_hpp__1C855F51_D0CA_48B9_8F40_4B259941C1F9
//------------------------------------------------------------------------------
#include "freeze.hpp"

#include "le/spectrumworx/effects/channelStateDynamic.hpp"
#include "le/spectrumworx/effects/effects.hpp"
#include "le/spectrumworx/effects/phase_vocoder/shared.hpp"
#include "le/utility/buffers.hpp"

namespace LE::SW::Effects
{

class FreezeImpl : public EffectImpl<Freeze>
{
  public: // LE::Effect required interface.
    ////////////////////////////////////////////////////////////////////////////
    // ChannelState
    ////////////////////////////////////////////////////////////////////////////

    struct DynamicChannelState : DynamicChannelState_<DynamicChannelState>
    {
        PhaseVocoderShared::PitchShifter::ChannelState pvState;
        Engine::HalfFFTBuffer<float> frozenMagNew;
        Engine::HalfFFTBuffer<float> frozenFreqNew;
        Engine::HalfFFTBuffer<float> frozenMagOld;
        Engine::HalfFFTBuffer<float> frozenFreqOld;
        auto members()
        {
            return std::tie(pvState, frozenMagNew, frozenFreqNew, frozenMagOld, frozenFreqOld);
        }
    };

    struct ChannelState : DynamicChannelState
    {
        float frameCounter;

        bool freezeDone;
        bool meltDone;

        bool frozen;
        bool normal;

        //...mrmlj...quick-workaround for a non-deterministic relationship
        //...mrmlj...between setup() and process() calls...
        bool previousFreezeFlag;
        bool previousMeltFlag;

        void reset();
    }; // struct ChannelState

    ////////////////////////////////////////////////////////////////////////////
    // setup() and process()
    ////////////////////////////////////////////////////////////////////////////

    void setup(IndexRange const &, Engine::Setup const &);
    void process(ChannelState &, Engine::ChannelData_AmPh, Engine::Setup const &) const;

  private:
    float inverseTransitionTime_;
    /// Whether TransitionTime rounds to no steps at all. \see setup().
    bool noTransition_;
    bool freeze_;
    bool melt_;

    PhaseVocoderShared::BaseParameters pvParameters_;
}; // class FreezeImpl

} // namespace LE::SW::Effects

#endif // freezeImpl_hpp
