////////////////////////////////////////////////////////////////////////////////
///
/// \file lfo.hpp
/// -------------
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef lfoImpl_hpp__506C13FB_1220_44C6_BFC0_C6C9844F02B0
#define lfoImpl_hpp__506C13FB_1220_44C6_BFC0_C6C9844F02B0
//------------------------------------------------------------------------------
#include "lfo.hpp"

#include "le/math/conversion.hpp"
#include "le/math/math.hpp"
#include "le/parameters/boolean/parameter.hpp"
#include "le/parameters/dynamic/tag.hpp"
#include "le/parameters/factoryMacro.hpp"
#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/symmetric/parameter.hpp"
#include "le/parameters/uiElements.hpp" // the UIElements below

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace LE
{
namespace Plugins //...mrmlj...
{
using AutomatedParameterValue = float;
}

namespace Parameters
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class LFO
///
////////////////////////////////////////////////////////////////////////////////

class LFOImpl : public LFO
{
  public:
    static unsigned int const minimumValue = 0;
    static unsigned int const maximumValue = 1;

    static unsigned int const maximumPeriodInNumberOfBars = 16;
    static unsigned int const minimumPeriodAsMaximumBeatDenominator = 8;

  private:
    struct PeriodScaleParameterTraits;

  public:
    using value_type = float;
    using SnappedPeriod = std::pair<float, SyncType>;
    using PeriodScale = Parameters::Parameter<PeriodScaleParameterTraits>;

  public: // Parameters
    // Implementation note:
    //   To enable LFO automation, settings are stored as
    // LE::Parameters::Parameters<>.
    //                                        (18.02.2011.) (Domagoj Saric)

    ////////////////////////////////////////////////////////////////////////////
    /// \class SyncTypes
    /// \brief Which beat divisions the period may snap to; `Free` is none.
    ///
    /// \note A bit mask over Quarter|Triplet|Dotted, not an ordinal -- see
    /// `LFO::SyncType`. `All` is the three of them offered together.
    ///
    /// \note **The default is `Quarter`, full stop**, and must not be a function
    /// of the transport: the two Programs build their modules at two different
    /// moments, so a default that read whether a tempo was known yet would have
    /// them write different `sync` attributes for one LFO.
    ////////////////////////////////////////////////////////////////////////////

    using SyncTypes = Parameters::Parameter<Parameters::LinearUnsignedInteger::Modify<
        Parameters::Traits::Minimum<Free>, Parameters::Traits::Maximum<All>,
        Parameters::Traits::Default<Quarter>>::type>;

    // Implementation note:
    //   Unlike with other enumerate/"discrete values parameters", we do not use the
    // LE_ENUMERATED_PARAMETER macro to define the window function parameter
    // because its possible values are already defined with a plain enum in the
    // SW SDK which we simply reuse here.
    //                                            (01.04.2011.) (Domagoj Saric)
    // Implementation note:
    //   As a first choice, based on the comment at the bottom of this page
    // http://www.katjaas.nl/FFTwindow/FFTwindow&filtering.html, the Hann
    // window was chosen as the default/"overall best" window.
    //                                            (20.01.2010.) (Domagoj Saric)
    /// \todo Further investigate the Hann <-> Hanning debate/confusion. In this
    /// http://www.hydrogenaudio.org/forums/lofiversion/index.php/t29439.html
    /// discussion it is claimed that both Hann and Hanning windows exist.
    ///                                           (25.01.2010.) (Domagoj Saric)

    using Waveform = Parameters::EnumeratedParameter<LFO::Waveform::NumberOfWaveforms>;

    ////////////////////////////////////////////////////////////////////////////
    /// \class PeriodScale
    /// \brief Implements the Parameter<> concept for the PeriodScale parameter.
    ////////////////////////////////////////////////////////////////////////////
    //...mrmlj...a 'dynamic' (bounds) parameter...
  private:
    struct PeriodScaleParameterTraits
        : Parameters::Detail::LinearFloatParameterTraits<
              Parameters::TraitPack //...mrmlj...
              <Parameters::Traits::Minimum<1>, Parameters::Traits::Maximum<1>,
               Parameters::Traits::Default<1>, Parameters::Traits::ValuesDenominator<1>,
               Parameters::Traits::Unit<"">>>
    {
        using Tag = Parameters::DynamicRangeParameterTag;

        static value_type minimum();
        static value_type maximum();

        static bool isValidValue(param_type value);
    }; // struct PeriodScaleParameterTraits

  public:
    /// \note The traits are qualified here rather than imported: this is
    /// namespace LE::Parameters itself, and only the namespaces that declare a
    /// lot of parameters -- LE::SW::Effects -- import them.
    LE_DEFINE_PARAMETER(Enabled, Boolean);
    LE_DEFINE_PARAMETER(Phase, SymmetricFloat, Traits::MaximumOffset<50>,
                        Traits::ValuesDenominator<100>);
    LE_DEFINE_PARAMETER(LowerBound, LinearFloat, Traits::Minimum<minimumValue>,
                        Traits::Maximum<maximumValue>, Traits::Default<minimumValue>);
    LE_DEFINE_PARAMETER(UpperBound, LowerBound, Traits::Default<maximumValue>,
                        //...mrmlj...
                        Traits::ValuesDenominator<1>, Traits::Unit<"">);
    LE_DEFINE_PARAMETERS(Enabled, PeriodScale, Phase, LowerBound, UpperBound, SyncTypes, Waveform);

    Parameters &parameters() { return parameters_; }
    Parameters const &parameters() const { return const_cast<LFOImpl &>(*this).parameters(); }

    value_type periodScale() const;

    void setLowerBound(value_type newLowerBound);
    void setUpperBound(value_type newUpperBound);

    void setPeriodScale(value_type periodScale);

  public:
    class Timer
    {
      public:
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable : 4512) // Assignment operator could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.
#endif                  // _MSC_VER
        struct TimingInformationChange
        {
            bool timingInfoChanged() const
            {
                return barDurationChanged() || measureNumeratorChanged();
            }

            bool barDurationChanged() const;
            bool measureNumeratorChanged() const { return measureNumeratorChanged_; }

            value_type const barDurationChangeRatio_;
            bool const measureNumeratorChanged_;
        }; // struct TimingInformationChange
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

      public:
        Timer();

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \brief One bar at the tempo and meter the engine assumes when a host
        /// reports none: 120 BPM in four four, so two seconds.
        ///
        /// \note **The unit a free-running LFO's period is measured in.** A
        /// synced LFO's period is a fraction of the *host's* bar, which is the
        /// whole of what syncing means and is why that one follows the tempo. A
        /// free one is a duration in seconds, and expressing it against a bar
        /// that never changes length is how it stays one without the parameter
        /// having to be rewritten every time the tempo moves.
        ///
        ////////////////////////////////////////////////////////////////////////
        static constexpr value_type referenceBarDuration{60.0f / 120 * 4};
        static constexpr std::uint8_t referenceMeasureNumerator{4};

        value_type const &currentTimeInBars() const { return currentTimeInBars_; }

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \brief The same instant measured in reference bars rather than in the
        /// host's, which is the clock a free-running LFO runs off.
        ///
        /// \note Derived rather than tracked, and the identity is what makes this
        /// a rewrite of the old behaviour rather than a change to it. A free LFO
        /// used to be kept honest by rescaling its *period* on every bar-duration
        /// change, so that `periodScale * barDuration` stayed constant; its phase
        /// was therefore
        ///
        ///     frac( (offset + timeInBars) / periodScale )
        ///   = frac( (offset + timeInBars) * barDuration / periodInSeconds )
        ///
        /// -- which is exactly this clock divided by a period expressed in these
        /// units. Same phase, same locate behaviour, same everything an LFO does;
        /// what stops moving is the number the host and the file hold.
        ///
        ////////////////////////////////////////////////////////////////////////
        value_type currentTimeInReferenceBars() const
        {
            return currentTimeInBars_ * basePeriod() / referenceBarDuration;
        }

        TimingInformationChange updatePositionAndTimingInformation(float positionInBars,
                                                                   float barDuration,
                                                                   std::uint8_t measureNumerator);
        TimingInformationChange
        updatePositionAndTimingInformation(unsigned int deltaNumberOfSamples, float sampleRate);

        void setPosition(unsigned int numberOfSamples, float sampleRate);
        void setPosition(float numberOfSeconds);

        void reset();

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \brief The host's tempo, as every LFO in the process sees it.
        ///
        /// \note Still process-wide, and still wrong for two instances in two
        /// tracks at two tempi -- but no longer a data race. Each instance's
        /// audio thread writes these once a block and the message thread reads
        /// them to lay out the LFO panel, which as plain scalars is undefined
        /// behaviour rather than merely a stale answer. Relaxed atomics, because
        /// nothing else is published through them: what a reader needs is a
        /// tempo, not a happens-before edge.
        ///
        ///   Making them per-instance is the real fix and is not this redesign's:
        /// they are read by `snapPeriodScale()`, `clampFreePeriod()` and the two
        /// period-scale bounds, all *static* and all called from the parameter
        /// layer and the editor, so a per-instance timer means threading one
        /// through the LFO parameter interface. Recorded in issue #11.
        ///
        /// \note There is deliberately no "has a host told us a tempo" flag: one
        /// that has not is 120 BPM 4/4, which every LFO already runs against, and
        /// a sticky process-global answer is not something a parameter default
        /// may depend on.
        ///
        ////////////////////////////////////////////////////////////////////////
        static value_type basePeriod() { return barDuration_.load(std::memory_order_relaxed); }
        static std::uint8_t measureNumerator()
        {
            return measureNumerator_.load(std::memory_order_relaxed);
        }
        static value_type measureNumeratorFloat();

      private:
        /// \brief What to report for incoming timing, given what was already
        /// known. See the definition: the first call after construction or
        /// reset() establishes the timing rather than changing it.
        TimingInformationChange establishedChange(value_type barDuration,
                                                  std::uint8_t measureNumerator);

      private:
        value_type currentTimeInBars_;

        /// \note No longer readable from outside, and the pair of accessors that
        /// exposed it went with the last reader: `getValue()` used to ask how far
        /// the clock had moved since its own previous tick and call the answer
        /// "a period began", which is issue #151. What a waveform needs is a fact
        /// about itself, so it keeps one. This stays because the asserts below
        /// still use it -- a clock that ran backwards would be worth knowing
        /// about -- and not because anything reads it for an answer.
        value_type previousTimeInBars_;

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \brief Whether this timer has ever been told the timing, as opposed to
        /// still holding the assumed 120 BPM 4/4.
        ///
        /// \note Per instance, and the reason no static can answer it: reset()
        /// puts barDuration_ back to the assumption, so a process-wide "we have
        /// been told" would claim knowledge of a value that is a placeholder
        /// again. The question is also per engine rather than per process.
        ///
        ///   What it is for: the first block after construction or reset()
        /// *establishes* the timing rather than changing it, and the difference
        /// matters because a Free LFO's period is rescaled by every bar-duration
        /// change (see updateForNewTimingInformation). Comparing a host's real
        /// tempo against an assumption we invented and calling the difference a
        /// change silently moved every LFO period in the plugin on the first
        /// block of any session not at 120 BPM.
        ///
        ////////////////////////////////////////////////////////////////////////
        bool timingInformationEstablished_{false};

        static std::atomic<value_type> barDuration_;
        static std::atomic<std::uint8_t> measureNumerator_;
    }; // class Timer

  public:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief What a waveform function carries between calls: two scratch values
    /// and, for the three random waveforms, the generator they draw from.
    ///
    /// \note The generator is per LFO and deliberately **not** per channel -- an
    /// LFO modulates a parameter, and a parameter has no channel. That also
    /// means a random waveform still moves with the number of `process()` calls,
    /// because it is evaluated once per call from `ModuleDSP::preProcess()`;
    /// making that motion per chunk is issue #78 and is a separate question from
    /// issue #86, which is what put this generator here.
    ///
    ////////////////////////////////////////////////////////////////////////////
    struct WaveformState
    {
        value_type values[2];
        Math::Rng rng;

        /// \brief Which period the last evaluation fell in, and whether there
        /// has been one. Together they are `newPeriodBegun`. \see getValue(),
        /// which has the reason this is per LFO rather than read off the clock.
        int periodIndex{0};
        bool neverEvaluated{true};
    }; // struct WaveformState

    LFOImpl();

    value_type getValue(Timer const &) const;

    /// \brief Gives this LFO its own random stream. \see ModuleDSP::seedRandomState().
    void seed(std::uint64_t seed);

    /// \todo These synchronization type altering functions do not automatically
    /// cause period scale resnapping. Reconsider this.
    ///                                       (23.02.2011.) (Domagoj Saric)
    //void addSyncType   ( SyncType syncType );
    //void removeSyncType( SyncType syncType );

    void updateForNewTimingInformation(Timer::TimingInformationChange const &);

    static value_type currentPeriodScaleMinimum();
    static value_type currentPeriodScaleMaximum();

    static SnappedPeriod snapPeriodScale(value_type periodScale, std::uint8_t syncTypes);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief What a period *reads* as, which is not the number it is stored as.
    ///
    ///   A synced LFO's period is a note value -- `1/4 bars`, `1/8T bars`,
    ///   `2/1 bars` -- and a free one's is a length of time in milliseconds. The
    /// stored value is neither: it is a multiple of a bar, from about 0.0208 to
    /// 24, and a host asking `value_to_text` used to be handed exactly that.
    /// \see issue #158.
    ///
    /// \note Here rather than in the panel that has always drawn it, because
    /// what a host is told and what the panel says have to be the same thing and
    /// this layer is the one both can reach -- the panel is JUCE and the
    /// parameter edge may not link it.
    ///
    /// \returns the number of characters written, not counting the terminator.
    ///
    ////////////////////////////////////////////////////////////////////////////

    static std::size_t printPeriodScale(value_type periodScale, std::uint8_t syncTypes,
                                        std::span<char> buffer);

    /// \brief The note value alone, without the milliseconds arm: what the panel
    /// draws on its own line. \see printPeriodScale(), whose synced arm this is.
    static std::size_t printSyncedPeriodScale(value_type periodScale, std::uint8_t syncTypes,
                                              std::span<char> buffer);

    /// \brief printPeriodScale() run backwards, or nothing for text that is not
    /// a period this LFO could hold.
    static std::optional<value_type> parsePeriodScale(char const *text, std::uint8_t syncTypes);

    //...mrmlj...cleanup with a new 'logarithmic' parameter/control...
    static void snapPeriodScaleFromAutomation(PeriodScale &);

  public: // Preset saving/loading section
    template <typename T> typename T::value_type adjustValueForPreset(T const &value) const
    {
        return value;
    }

    template <typename T>
    typename T::value_type adjustValueFromPreset(typename T::value_type const value) const
    {
        return value;
    }

  public: // Parameters
          // Implementation note:
          //   To enable LFO automation, settings are stored as
          // LE::Parameters::Parameters<>.
          //                                        (18.02.2011.) (Domagoj Saric)

  public:
    static Plugins::AutomatedParameterValue
    internal2AutomatedValue(std::uint8_t parameterIndex, float internalValue, bool normalised);
    static float automated2InternalValue(std::uint8_t parameterIndex,
                                         Plugins::AutomatedParameterValue automatedValue,
                                         bool normalised);

    static Plugins::AutomatedParameterValue
    unlinearisePeriodScale(Plugins::AutomatedParameterValue linearisedNormalisedPeriodScale);
    static Plugins::AutomatedParameterValue
    linearisePeriodScale(Plugins::AutomatedParameterValue nonlinearNormalisedPeriodScale);

  private:
    friend class LFO;
    static value_type clampFreePeriod(value_type absolutePeriod);
    static SnappedPeriod snapSyncedPeriod(value_type periodScale, std::uint8_t syncTypes);

    value_type getWaveformAmplitudeForPosition(value_type position, bool newPeriodBegun) const;

    bool isValueInBounds(value_type) const;
    static bool isValueInRange(value_type);

  private:
    friend class LFO;
    Parameters parameters_;
    mutable WaveformState state_;
}; // class LFOImpl

template <>
LFOImpl::PeriodScale::value_type LFOImpl::adjustValueForPreset(PeriodScale const &) const;

template <>
LFOImpl::PeriodScale::value_type
    LFOImpl::adjustValueFromPreset<LFOImpl::PeriodScale>(LFOImpl::PeriodScale::value_type) const;

////////////////////////////////////////////////////////////////////////////////
//
// LFOImpl UIElements definitions.
//
////////////////////////////////////////////////////////////////////////////////

UI_NAME(LFOImpl::Enabled, "Enable")
UI_NAME(LFOImpl::PeriodScale, "Period")
UI_NAME(LFOImpl::Phase, "Phase")
UI_NAME(LFOImpl::LowerBound, "Range Min")
UI_NAME(LFOImpl::UpperBound, "Range Max")
UI_NAME(LFOImpl::SyncTypes, "Sync")
UI_NAME(LFOImpl::Waveform, "Wave")

STREAMING_NAME(LFOImpl::Enabled, "on")
STREAMING_NAME(LFOImpl::PeriodScale, "T")
STREAMING_NAME(LFOImpl::Phase, "ph")
STREAMING_NAME(LFOImpl::LowerBound, "lbnd")
STREAMING_NAME(LFOImpl::UpperBound, "ubnd")
STREAMING_NAME(LFOImpl::SyncTypes, "sync")
STREAMING_NAME(LFOImpl::Waveform, "wfrm")

////////////////////////////////////////////////////////////////////////////////
///
/// \note The phase is stored as a fraction of a period -- plus or minus a half
/// -- and read as degrees, which is what the LFO panel draws. \see issues #158
/// and #168.
///
/// \note It read as a percentage until #181: the panel had moved to degrees and
/// the transformer a host prints through had not, so the two said different
/// things about one parameter. Both go through this one now -- `phaseString()`
/// prints through it rather than beside it.
///
/// \note `ValuesDenominator<100>` on the declaration says something else again
/// and says it to nobody: nothing in the printer reads that trait. The transform
/// is where a display unit lives, and the inverse beside it is what lets a host
/// read the number back.
///
////////////////////////////////////////////////////////////////////////////////

template <> struct DisplayValueTransformer<LFOImpl::Phase>
{
    template <typename Source>
    static Source transform(Source const &value, SW::Engine::Setup const &)
    {
        return value * 360;
    }
    static float inverse(float const degrees, SW::Engine::Setup const &) { return degrees / 360; }
    /// \note No leading space, unlike every other unit here: a degree sign is
    /// written against its number.
    using Suffix = UnitString<"°">;
};

//...mrmlj...this does not work yet because the Window enum is not a member
//...of the WindowFunction parameter class...fix this...
//ENUMERATED_PARAMETER_STRINGS
//(
//    LFOImpl, Waveform,
//    (( Sine           , "Sine"        ))
//    (( Triangle       , "Triangle"    ))
//    (( Sawtooth       , "Ramp"        ))
//    (( ReverseSawtooth, "Sawtooth"    ))
//    (( Square         , "Square"      ))
//    (( Exponent       , "Exponent"    ))
//    (( RandomHold     , "Sample&Hold" ))
//    (( RandomSlide    , "Sample&Glide"))
//    (( Whacko         , "Wacko"       ))
//    (( Dirac          , "Dirac Up"    ))
//    (( dIRAC          , "Dirac Down"  ))
//)

/// \note Written out rather than through ENUMERATED_PARAMETER_STRINGS for the
/// reason above: that macro checks each string against the enumerator it names,
/// and this parameter has no enumerators to name.
template <>
constexpr DiscreteValues<LFOImpl::Waveform>::Strings DiscreteValues<LFOImpl::Waveform>::strings{
    "Sine",        "Triangle",     "Ramp",  "Sawtooth", "Square",    "Exponent",
    "Sample&Hold", "Sample&Glide", "Wacko", "Dirac Up", "Dirac Down"};

} // namespace Parameters

} // namespace LE

#endif // lfoImpl_hpp
