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
#ifndef lfo_hpp__43E00321_1DA0_4093_9688_EC409F3DE554
#define lfo_hpp__43E00321_1DA0_4093_9688_EC409F3DE554
//------------------------------------------------------------------------------
#include "le/parameters/enumerated/parameter.hpp"
#include "le/utility/abi.hpp"

#include <cstdint>

namespace LE::Parameters
{

/// \addtogroup Parameters
/// Parameter abstractions and utilities used across LE SDKs
/// @{

////////////////////////////////////////////////////////////////////////////////
///
/// \class LFO
///
/// \brief <a href="http://en.wikipedia.org/wiki/Low-frequency_oscillation">
/// Low frequency oscillator</a> for automatic modulation of parameters
///
/// \details A first time encounter with LFOs might be overwhelming (especially
/// for those without a musical and/or mathematical background). In such cases,
/// climbing the learning curve might be easier by first playing with the SW
/// plugin (and consulting the topic on LFOs in its manual) as the hands-on,
/// realtime, GUI experience should provide an intuitive understanding.
///
////////////////////////////////////////////////////////////////////////////////

class LFO
{
  public:
#ifdef DOXYGEN_ONLY
    /// \brief Types of possible LFO synchronizations to tempo
    /// \details Most often the best or desired effect of modulating a parameter
    /// with an LFO is achieved by synchronizing the LFO's frequency or period
    /// with the rhythm (i.e. the tempo and the <a
    /// href="http://en.wikipedia.org/wiki/Meter_(music)">meter</a>) of a song
    /// or sound.<BR>
    /// The ("synchronized") relationship between a rhythm's beat and the LFO's
    /// period need not be a straightforward 1-to-1 mapping (i.e. setting the
    /// period to the length of one quarter note), all sorts of interesting,
    /// syncopated rhythms can be achieved and this is what this parameter aims
    /// to ease: set the allowed 'relationships' between the LFO's period and
    /// the master tempo and then tweak the period parameter until the desired
    /// effect is achieved (the LFO object will 'snap' the period parameter to
    /// the closest value allowed by the set SyncTypes).
    enum SyncType
    {
        Free,    ///< Allow free/unconstrained period adjustment
        Quarter, ///< Allow syncing to (parts or multiples) of <a href="http://en.wikipedia.org/wiki/Quarter_note">quarter notes</a> (beats in most often used meters)
        Triplet, ///< Allow syncing to (parts or multiples) of <a href="http://en.wikipedia.org/wiki/Tuplet#Triplets">triplet notes</a>
        Dotted, ///< Allow syncing to (parts or multiples) of <a href="http://en.wikipedia.org/wiki/Dotted_note">dotted notes</a>
        All     ///< Allow all supported synchronization types
    }; // enum SyncType
#else  // !DOXYGEN_ONLY
    enum SyncType : std::uint8_t
    {
        Free = 0,
        Quarter = 1 << 0,
        Triplet = 1 << 1,
        Dotted = 1 << 2,

        All = (Quarter | Triplet | Dotted)
    }; // enum SyncType
#endif // DOXYGEN_ONLY

    /// Supported <a href="http://en.wikipedia.org/wiki/Waveform">waveforms</a>
    enum Waveform
#ifndef DOXYGEN_ONLY
        : std::uint8_t
#endif // DOXYGEN_ONLY
    {
        Sine,     ///< <a href="http://en.wikipedia.org/wiki/Sine_wave">sine wave</a>
        Triangle, ///< <a href="http://en.wikipedia.org/wiki/Triangle_wave">triangle waveform</a>
        Sawtooth, ///< <a href="http://en.wikipedia.org/wiki/Sawtooth_wave">sawtooth waveform</a>
        ReverseSawtooth, ///< <a href="http://en.wikipedia.org/wiki/Sawtooth_wave">reverse sawtooth waveform</a>
        Square,      ///< <a href="http://en.wikipedia.org/wiki/Square_wave">square wave</a>
        Exponent,    ///< exponential rise and fall oscillations
        RandomHold,  ///< generate a random number and hold it until the next period
        RandomSlide, ///< generate a random number every period and linearly slide to it from the previous the previous value
        Whacko, ///< generate a new random number on every query (i.e. this "waveform" is independent of the set period)
        Dirac,  ///< a simple <a href="http://en.wikipedia.org/wiki/Dirac_delta_function">pulse</a>
        dIRAC,  ///< the ("graphical vertical") inverse of the Dirac waveform

        NumberOfWaveforms
    }; // enum Waveform

  public:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief One bar at the tempo and meter the engine assumes when a host
    /// reports none: 120 BPM in four four, so two seconds.
    ///
    /// \note **The unit a free-running LFO's period is measured in.** A synced
    /// LFO's period is a fraction of the *host's* bar, which is the whole of what
    /// syncing means and is why that one follows the tempo. A free one is a
    /// duration in seconds, and expressing it against a bar that never changes
    /// length is how it stays one without the parameter having to be rewritten
    /// every time the tempo moves.
    ///
    ////////////////////////////////////////////////////////////////////////////
    static constexpr float referenceBarDuration{60.0f / 120 * 4};
    static constexpr std::uint8_t referenceMeasureNumerator{4};

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \class Timing
    /// \brief The host's bar as one *plugin instance* sees it: how long it is and
    /// how many beats it has.
    ///
    /// \note A snapshot taken by value, and that is the point of it: the two
    /// numbers live in the instance's `Timer` as separate atomics written by the
    /// audio thread, so a reader that wants both wants them from one instant.
    ///
    /// \note A default-constructed one is the reference bar above, which is the
    /// answer for anything with no host to ask. \see issue #11.
    ///
    ////////////////////////////////////////////////////////////////////////////

    struct Timing
    {
        float barDuration{referenceBarDuration};
        std::uint8_t measureNumerator{referenceMeasureNumerator};

        float measureNumeratorFloat() const { return static_cast<float>(measureNumerator); }
    }; // struct Timing

    void setEnabled(bool value); ///< \brief enable/disable the LFO
    void setPhase(
        float
            phase); ///< \brief set the (waveform's) phase offset (as a normalised percentage value [-0.5,+0.5])
    void setWaveform(Waveform); ///< \brief set the desired waveform
    /// \brief constrain the allowed range of values the LFO can set a parameter to
    /// \details a normalised [0, 1] value later automatically mapped to the range of the parameter the LFO is connected to
    /// \return true if the upper bound had to be adjusted in order to make sure that the upper-bound >= lower-bound condition always holds
    bool setLowerBound(float lowerBound);
    /// \brief constrain the allowed range of values the LFO can set a parameter to
    /// \details a normalised [0, 1] value later automatically mapped to the range of the parameter the LFO is mapped to
    /// \return true if the lower bound had to be adjusted in order to make sure that the upper-bound >= lower-bound condition always holds
    bool setUpperBound(float upperBound);

    /// \brief set the LFO period in milliseconds
    /// \return the actually applied value (autoadjusted/'clamped' to the values allowed by the currently configured sync types)
    /// \note The period is a fraction of a bar and the bar is the host's, so
    /// turning one into a length of time takes the instance's Timing rather than
    /// anything this object knows.
    std::uint16_t setPeriodInMilliseconds(std::uint16_t periodInMilliseconds, Timing const &);

    /// \brief set the LFO period in seconds
    /// \return the actually applied value (autoadjusted/'clamped' to the values allowed by the currently configured sync types)
    /// \overload
    float setPeriodInSeconds(float periodInSeconds, Timing const &);

    bool enabled() const;           ///< is LFO enabled?
    float period() const;           ///< retrieve the current period
    float phase() const;            ///< retrieve the "phase offset" parameter
    float lowerBound() const;       ///< retrieve the "lower bound" parameter
    float upperBound() const;       ///< retrieve the "upper bound" parameter
    Waveform waveForm() const;      ///< retrieve the "Waveform" parameter
    std::uint8_t syncTypes() const; ///< retrieve the syncTypes

    void addSyncType(SyncType);          ///< add/enable a specific SyncType
    void removeSyncType(SyncType);       ///< remove/disable a specific SyncType
    bool hasEnabledSync(SyncType) const; ///< is a specific SyncType enabled?

  protected:
    /// \note The `#if _MSC_VER < 1800` that used to pick user-provided special
    /// members here was vacuously true on GCC, where undefined `_MSC_VER`
    /// preprocesses to 0 — so Linux was getting `LFO() {}` / `~LFO() {}` and a
    /// merely *declared* copy constructor where macOS gets defaulted members and
    /// a deleted copy. That is not cosmetic: user-provided versus defaulted
    /// decides triviality, and a declared-not-defined copy turns a compile error
    /// into a link error. VS2013 is long out of scope, so the branch is gone.
    LFO() = default;
    LFO(LFO const &) = delete;
    ~LFO() = default;
}; // class LFO

/// @} // group Parameters

} // namespace LE::Parameters

#endif // lfo_hpp
