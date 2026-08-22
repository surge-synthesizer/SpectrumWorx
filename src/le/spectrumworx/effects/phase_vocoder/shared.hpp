////////////////////////////////////////////////////////////////////////////////
///
/// \file shared.hpp
/// ----------------
///
///  Contains shared functionality related to the "phase vocoder domain". The
/// intent was to allow for easier reuse of parts of the original PhaseVocoder
/// component (previously every component wanting to use a part of the phase
/// vocoder functionality had to hold and manage the whole PhaseVocoder object).
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef shared_hpp__7896AB45_1E91_4B7E_97CB_C49BA8FE8D54
#define shared_hpp__7896AB45_1E91_4B7E_97CB_C49BA8FE8D54
//------------------------------------------------------------------------------
#include "le/spectrumworx/effects/channelStateDynamic.hpp"
#include "le/spectrumworx/effects/effects.hpp"
#include "le/spectrumworx/effects/indexRange.hpp"
#include "le/spectrumworx/engine/buffers.hpp"
#include "le/spectrumworx/engine/channelDataAmPh.hpp"
#include "le/utility/platformSpecifics.hpp"

#include "le/utility/assert.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace LE::SW::Effects::PhaseVocoderShared
{

////////////////////////////////////////////////////////////////////////////////
///
/// \struct AnalysisChannelState
///
////////////////////////////////////////////////////////////////////////////////

namespace Detail
{
/// \note One member left: the struct is what remains of an Array-of-Structures
/// layout that carried the transient/steady-state per-bin data alongside it.
struct AnalysisBinStateData
{
    Engine::real_t lastPhase;
};
struct AnalysisChannelStateBase : DynamicChannelState_<AnalysisChannelStateBase>
{
    Engine::HalfFFTBuffer<AnalysisBinStateData> binData;
    auto members() { return std::tie(binData); }
};
} // namespace Detail

struct AnalysisChannelState : Detail::AnalysisChannelStateBase
{
    void reset()
    {
        AnalysisChannelStateBase::reset();
        previousScaleFactor = 1;
        reinitializePhases = true;
    }
    float previousScaleFactor;
    bool reinitializePhases;
}; // struct AnalysisChannelState

////////////////////////////////////////////////////////////////////////////////
///
/// \struct SynthesisChannelState
///
////////////////////////////////////////////////////////////////////////////////

struct SynthesisChannelState : Engine::HalfFFTBuffer<Engine::real_t>
{
    DataRange const &phaseSum() { return *this; }
    void reset()
    {
        this->clear();
        binToReduce = 0;
    }
    std::uint16_t binToReduce;
}; // struct SynthesisChannelState

////////////////////////////////////////////////////////////////////////////////
///
/// \class BaseParameters
///
////////////////////////////////////////////////////////////////////////////////
/// \todo Add sanity checks that setup was called before any of the getters for
/// this and similar 'parameters' helper classes.
///                                           (12.01.2011.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

class BaseParameters
{
  public:
    float const &freqPerBin() const { return freqPerBin_; }
    float const &expctRate() const { return expctRate_; }
    float deviationFactor() const { return deviationFactor_; }
    float invDeviationFactor() const { return 1 / deviationFactor_; }

    void setup(Engine::Setup const &);

  private:
    float freqPerBin_;
    float expctRate_; ///< The expected rate of phase change per frame.
    double deviationFactor_;
}; // class BaseParameters

////////////////////////////////////////////////////////////////////////////////
///
/// \class PitchShiftParameters
///
////////////////////////////////////////////////////////////////////////////////

class PitchShiftParameters
{
  public: // Query interface.
    float const &scale() const { return scale_; }

    bool skipProcessing() const;

  public: // Setup interface.
    void setScalingFactor(float newScale, std::uint16_t numberOfBins);

  public: // Utility interface.
    static float scaleFromSemiTonesAndCents(float const &semiTones, std::int8_t cents);
    static float scaleFromSemiTones(float semiTones);

  private:
    float scale_;
}; // class PitchShiftParameters

////////////////////////////////////////////////////////////////////////////////
///
/// \class StandaloneEffect
///
///   A utility class for wrapping a PVD effect and transforming it into a
/// non-PVD, i.e. "standalone", effect.
///
////////////////////////////////////////////////////////////////////////////////

namespace Detail
{
class StandaloneEffectBase : private BaseParameters
{
  public:
    using ChannelState = CompoundChannelState<AnalysisChannelState, SynthesisChannelState>;

  public: // LE::Effect interface.
    using BaseParameters::setup;

  protected:
    BaseParameters &baseParameters() { return *this; }
    BaseParameters const &baseParameters() const { return *this; }
};
} // namespace Detail

template <class PVDEffect, class SDKBaseClass>
class StandaloneEffect : private PVDEffect,
                         private Detail::StandaloneEffectBase,
                         public SDKBaseClass
{
  public:
    using Parameters = typename PVDEffect::Parameters;

    using SDKBaseClass::description;
    using SDKBaseClass::title;

    using PVDEffect::parameters;

    using ChannelState = CompoundChannelState<typename PVDEffect ::ChannelState,
                                              Detail::StandaloneEffectBase::ChannelState>;

    void setup(IndexRange const &workingRange, Engine::Setup const &engineSetup)
    {
        Detail::StandaloneEffectBase::setup(engineSetup);
        PVDEffect ::setup(workingRange, engineSetup);
    }

    void process(ChannelState &channelState, Engine::ChannelData_AmPh data,
                 Engine::Setup const &engineSetup) const
    {
        analysis(channelState, data.full(), baseParameters());
        PVDEffect::process(channelState, data, engineSetup);
        synthesis(channelState, data.full().phases(), baseParameters());
    }
}; // class StandaloneEffect

////////////////////////////////////////////////////////////////////////////////
///
/// \class PVPitchShifter
///
/// \brief A base or utility class for all effects that require full pitch
/// shifting capabilities (to reduce the repetitive inclusion of the analysis,
/// shifting and synthesis functionality) and operate in the PV domain.
///
////////////////////////////////////////////////////////////////////////////////
/// \todo Consider replacing the usage of this class (and the related
/// PitchShifter class) with the StandaloneEffect class (this might incur a
/// performance hit, for example doing the analysis and synthesis phase even
/// when PitchShiftParameters::skipProcessing() returns true but this might turn
/// out to be irrelevant/negligible in real use cases and may be solved by
/// requiring that all PVDEffect classes implement a skipProcessing() member
/// function.
///                                           (12.01.2011.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

namespace Detail
{
/// \note Was BOOST_MPL_HAS_XXX_TRAIT_DEF( ChannelState ), which generates the
/// same detection idiom out of SFINAE and a nested-type probe. A requires-clause
/// says it in one line and does not need the macro to have been included by
/// somebody else.
template <class T> struct has_ChannelState : std::bool_constant < requires
{
    typename T::ChannelState;
}>{};

struct DummyChannelStateHolder
{
    struct ChannelState : StaticChannelState
    {
        void reset() {}
    };
};
} // namespace Detail

class PVPitchShifter
{
  public:
    bool skipProcessing() const { return pitchShiftParameters().skipProcessing(); }

  public: // LE::Effect interface.
    void setup(Engine::Setup const &) {}

    void process(float pitchScale, Engine::ChannelData_AmPh &&, Engine::Setup const &) const;
    void process(Engine::ChannelData_AmPh &&) const;

    void LE_FORCEINLINE process(Engine::ChannelData_AmPh &&data, Engine::Setup const &) const
    {
        process(std::forward<Engine::ChannelData_AmPh>(data));
    }

  public:
    void setPitchScaleFromSemitones(float semitones, std::uint16_t numberOfBins);

  protected:
    void setDynamicScalingFactor(float newScale, std::uint16_t numberOfBins) const;

    PitchShiftParameters const &pitchShiftParameters() const { return pitchShiftParameters_; }
    PitchShiftParameters &pitchShiftParameters() { return pitchShiftParameters_; }

  private:
    PitchShiftParameters pitchShiftParameters_;
}; // class PVPitchShifter

////////////////////////////////////////////////////////////////////////////////
///
/// \class PitchShifter
///
/// \brief A base or utility class for all effects that require full pitch
/// shifting capabilities (to reduce the repetitive inclusion of the analysis,
/// shifting and synthesis functionality).
///
////////////////////////////////////////////////////////////////////////////////

class PitchShifter : public PVPitchShifter, public Detail::StandaloneEffectBase
{
  public: // LE::Effect interface.
    using Detail::StandaloneEffectBase::setup;

    void process(float pitchScale, ChannelState &, Engine::ChannelData_AmPh &&,
                 Engine::Setup const &) const;
    void process(ChannelState &, Engine::ChannelData_AmPh &&) const;

    //void LE_FORCEINLINE process( ChannelState & channelState, Engine::ChannelData_AmPh && data, Engine::Setup const & ) const { process( channelState, std::forward<Engine::ChannelData_AmPh>( data ) ); }
    void LE_FORCEINLINE process(ChannelState &channelState, Engine::ChannelData_AmPh data,
                                Engine::Setup const &) const
    {
        process(channelState, std::forward<Engine::ChannelData_AmPh>(data));
    }
}; // class PitchShifter

////////////////////////////////////////////////////////////////////////////////
/// \internal
/// \class CombinedChannelState
/// \brief Helper for creating combined ChannelState structures.
////////////////////////////////////////////////////////////////////////////////

template <class ChannelStateHolder1, bool hasChannelState1, class ChannelStateHolder2,
          bool hasChannelState2>
class CombineAndOverrideChannelState : public ChannelStateHolder1, public ChannelStateHolder2
{
  private:
    using ChannelState1 =
        typename std::conditional_t<hasChannelState1, ChannelStateHolder1,
                                    Detail::DummyChannelStateHolder>::ChannelState;

    using ChannelState2 =
        typename std::conditional_t<hasChannelState2, ChannelStateHolder2,
                                    Detail::DummyChannelStateHolder>::ChannelState;

  public:
    using ChannelState = CompoundChannelState<ChannelState1, ChannelState2>;
}; // class CombineAndOverrideChannelState

template <class ChannelStateHolder1, class ChannelStateHolder2>
class CombineAndOverrideChannelState<ChannelStateHolder1, false, ChannelStateHolder2, false>
    : public ChannelStateHolder1, public ChannelStateHolder2
{
    // no channel state
};

////////////////////////////////////////////////////////////////////////////////
/// \internal
/// \class PitchShifterBasedEffect
/// \brief Helper for easier creation of PVD and non-PVD versions of the same
/// effect.
///
/// \tparam EffectBase         - class implementing the functionality shared by
///                              the PVD and non-PVD versions
/// \tparam PitchShifterHelper - class implementing the pitch-shift/
///                              phase-vocoder functionality (e.g. PitchShifter
///                              or PVPitchShifter).
////////////////////////////////////////////////////////////////////////////////

template <class EffectBase, class PitchShifterHelper>
class PitchShifterBasedEffect
    : public CombineAndOverrideChannelState<EffectBase, Detail::has_ChannelState<EffectBase>::value,
                                            PitchShifterHelper,
                                            Detail::has_ChannelState<PitchShifterHelper>::value>
{
  public: // LE::Effect interface.
    void setup(IndexRange const &workingRange, Engine::Setup const &engineSetup)
    {
        PitchShifterHelper::setup(engineSetup);
        EffectBase ::setup(workingRange, engineSetup);
    }
}; // class PitchShifterBasedEffect

void analysis(AnalysisChannelState &, Engine::FullChannelData_AmPh &, BaseParameters const &);
void synthesis(SynthesisChannelState &, DataRange const &anaFreqInSynthPhaseOut,
               BaseParameters const &);
void pitchShiftAndScale(Engine::ChannelData_AmPh &, PitchShiftParameters const &);

} // namespace LE::SW::Effects::PhaseVocoderShared

#endif // shared_hpp
