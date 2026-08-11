////////////////////////////////////////////////////////////////////////////////
///
/// \file tuneWorxImpl.hpp
/// ----------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef tuneWorxImpl_hpp__08D752F7_70CC_436C_8C8A_BE59A2A4900D
#define tuneWorxImpl_hpp__08D752F7_70CC_436C_8C8A_BE59A2A4900D
//------------------------------------------------------------------------------
#include "tuneWorx.hpp"

#include "le/spectrumworx/effects/effects.hpp"
#include "le/spectrumworx/effects/phase_vocoder/shared.hpp"
#include "le/analysis/musical_scales/musicalScales.hpp"
#include "le/analysis/pitch_detector/pitchDetector.hpp"

namespace LE::SW::Effects
{

namespace Detail
{
class TuneWorxBaseImpl : public EffectImpl<TuneWorxBase>
{
  public:
    typedef PitchDetector::ChannelState ChannelState;

  protected:
    float findNewPitchScale(Engine::ChannelData_AmPh const &, Engine::Setup const &,
                            ChannelState &) const;

    void setup(IndexRange const &, Engine::Setup const &);

  private:
    Music::Scale userScale_;
}; // class TuneWorxBaseImpl
} // namespace Detail

class TuneWorxImpl
    : public TuneWorx,
      public PhaseVocoderShared::PitchShifterBasedEffect<Detail::TuneWorxBaseImpl,
                                                         PhaseVocoderShared::PitchShifter>
{
  public: // LE::Effect interface.
    void process(ChannelState &, Engine::ChannelData_AmPh, Engine::Setup const &) const;
};

class TuneWorxPVDImpl
    : public TuneWorxPVD,
      public PhaseVocoderShared::PitchShifterBasedEffect<Detail::TuneWorxBaseImpl,
                                                         PhaseVocoderShared::PVPitchShifter>
{
  public: // LE::Effect interface.
    void process(ChannelState &, Engine::ChannelData_AmPh, Engine::Setup const &) const;
};

} // namespace LE::SW::Effects

#endif // tuneWorxImpl_hpp
