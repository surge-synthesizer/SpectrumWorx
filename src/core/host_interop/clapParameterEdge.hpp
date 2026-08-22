////////////////////////////////////////////////////////////////////////////////
///
/// \file clapParameterEdge.hpp
/// ---------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef clapParameterEdge_hpp__6F2A1D74_3E85_4C0B_9A17_2D4E8B1C05F3
#define clapParameterEdge_hpp__6F2A1D74_3E85_4C0B_9A17_2D4E8B1C05F3
//------------------------------------------------------------------------------
#include "core/parameterID.hpp"

#include "le/parameters/lfoImpl.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/plugins/clap/tag.hpp"
#include "le/spectrumworx/engine/parameters.hpp"

namespace LE::SW
{

////////////////////////////////////////////////////////////////////////////////
///
/// \namespace CLAPEdge
///
/// \brief What the host sees, as opposed to what the engine stores.
///
///   A module or LFO parameter's natural range belongs to whichever effect the
/// slot currently holds: Gain runs 0..2, a filter's cutoff runs 20..20000, and
/// swapping the effect moves both ends. CLAP will not have that. ext/params.h
/// puts min_value, max_value and the is_stepped flag together in the
/// CLAP_PARAM_RESCAN_ALL list -- the one that "can only be used while the plugin
/// is deactivated", requiring clap_host->restart() first. A slot's effect change
/// arrives as an ordinary parameter event mid-block, so it cannot legally move
/// any of the three.
///
///   So those parameters present a fixed 0..1 edge instead, for the plugin's
/// lifetime, and the natural value is normalised across it. What is left to
/// change -- the name, the module path, the hidden flag, the displayed text --
/// is exactly the set CLAP_PARAM_RESCAN_INFO and _TEXT cover, both legal while
/// active.
///
/// \note Global and module-chain (slot selector) parameters are not normalised.
/// Their ranges are properties of the plugin rather than of an effect, so they
/// never move, and they keep their real values and their step counts -- which is
/// what makes a slot selector usable as a discrete choice in a host's own UI.
///
/// \note Modelled on surge-xt2's clap_edge:: (src/clap/param_edge.h), for the
/// same reason it is a namespace of free functions rather than plugin members:
/// the policy the host sees can then be tested without a host.
///
////////////////////////////////////////////////////////////////////////////////

namespace CLAPEdge
{
using Info = Plugins::ParameterInformation<Plugins::Protocol::CLAP>;
using Value = Plugins::AutomatedParameterValue;

/// \brief Does this parameter hide a moving natural range behind a fixed 0..1?
inline bool isNormalised(ParameterID::Type const type)
{
    return (type == ParameterID::ModuleParameter) || (type == ParameterID::LFOParameter);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief How many choices this parameter offers a host, or zero when it is not
/// one at all.
///
///   The two LFO sub-parameters that are *choices* rather than quantities. They
/// are the only module or LFO parameters whose range is the plugin's rather than
/// the slot's effect's -- an LFO is an LFO whatever it modulates, so four sync
/// choices and nine waveforms are four and nine for the life of the binary.
///
///   That is what lets them carry a real stepped range where every other one has
/// to hide behind 0..1: `CLAP_PARAM_IS_STEPPED` needs `min_value` and
/// `max_value` to be the integers it steps between, and all three of those sit in
/// the RESCAN_ALL list a plugin may not use while active. A count that cannot
/// move may be stated once and left.
///
/// \note Sync is the one that needed it. It is a bit *mask* internally, so a
/// host handed the natural value could write 3, 5 or 6 -- combinations the panel
/// stopped making in issue #111 -- and a lane read `4` rather than `Dotted`.
/// \see LFOImpl::syncChoiceOf() and issue #159.
///                                           (22.08.2026.)
///
////////////////////////////////////////////////////////////////////////////////

inline unsigned choiceCount(ParameterID const parameterID)
{
    if (parameterID.type() != ParameterID::LFOParameter)
        return 0;

    using LFO = LE::Parameters::LFOImpl;
    using LE::Parameters::IndexOf;

    switch (parameterID.value._.lfo.lfoParameterIndex)
    {
    case IndexOf<LFO::Parameters, LFO::SyncTypes>::value:
        return LFO::syncChoices;
    case IndexOf<LFO::Parameters, LFO::Waveform>::value:
        return LE::Parameters::LFO::Waveform::NumberOfWaveforms;
    default:
        return 0;
    }
}

/// \brief A choice's natural stored value -> its ordinal, which is what a host
/// is given. \see choiceCount().
inline double choiceToHost(ParameterID const parameterID, Value const natural)
{
    using LFO = LE::Parameters::LFOImpl;
    using LE::Parameters::IndexOf;

    if (parameterID.value._.lfo.lfoParameterIndex ==
        IndexOf<LFO::Parameters, LFO::SyncTypes>::value)
        return LFO::syncChoiceOf(static_cast<std::uint8_t>(natural));

    return natural; // a waveform is already its ordinal
}

/// \brief The ordinal a host wrote -> the natural stored value, rounded and
/// clamped because a host may write anything.
inline Value choiceFromHost(ParameterID const parameterID, unsigned const choices,
                            double const host)
{
    auto const rounded(static_cast<long>(host + 0.5));
    auto const clamped(static_cast<unsigned>((rounded < 0) ? 0
                                             : (rounded >= static_cast<long>(choices))
                                                 ? (choices - 1)
                                                 : rounded));

    using LFO = LE::Parameters::LFOImpl;
    using LE::Parameters::IndexOf;

    if (parameterID.value._.lfo.lfoParameterIndex ==
        IndexOf<LFO::Parameters, LFO::SyncTypes>::value)
        return LFO::syncTypeOfChoice(static_cast<std::uint8_t>(clamped));

    return static_cast<Value>(clamped);
}

/// \note A choice carries its own integer range, so it is not one of these.
inline bool isNormalised(ParameterID const parameterID)
{
    return isNormalised(parameterID.type()) && (choiceCount(parameterID) == 0);
}

/// \brief Is there an effect in this slot that owns this parameter at all?
///
/// \note An empty range is how the parameter model spells "not in the current
/// program" -- see ParameterInfoGetter's NotAvailableParameter. Nothing can be
/// normalised across it, and a host must not be handed it as a range to divide
/// by either.
inline bool isPresent(Info const &info) { return info.maximum() > info.minimum(); }

/// \brief Natural stored value -> the value the host reads.
inline double toHost(ParameterID const parameterID, Info const &info, Value const natural)
{
    if (choiceCount(parameterID) != 0)
        return choiceToHost(parameterID, natural);
    if (!isNormalised(parameterID))
        return natural;
    if (!isPresent(info))
        return 0;
    auto const normalised((natural - info.minimum()) / (info.maximum() - info.minimum()));
    return (normalised < 0) ? 0 : (normalised > 1) ? 1 : normalised;
}

/// \brief The value the host writes -> natural stored value.
///
/// \note Clamped on both branches, because a host may write anything.
/// clap-validator's `param-range-robustness` deliberately writes outside
/// min_value..max_value, and `Parameter::setValue` answers an out-of-range value
/// with an assert -- in a release build, by storing it. The normalised branch has
/// always clamped, across its fixed 0..1 edge; the other one passed the host's
/// double straight through to the engine. A global gain ranged 0.001..2 was the
/// one that aborted.
inline Value fromHost(ParameterID const parameterID, Info const &info, double const host)
{
    if (auto const choices = choiceCount(parameterID); choices != 0)
        return choiceFromHost(parameterID, choices, host);
    if (!isNormalised(parameterID))
    {
        /// \note An absent parameter has nothing to clamp against -- see
        /// isPresent() -- and globals are never absent, so this is the
        /// belt-and-braces arm rather than a case that happens.
        if (!isPresent(info))
            return static_cast<Value>(host);
        auto const minimum(static_cast<double>(info.minimum()));
        auto const maximum(static_cast<double>(info.maximum()));
        return static_cast<Value>((host < minimum) ? minimum : (host > maximum) ? maximum : host);
    }
    if (!isPresent(info))
        return static_cast<Value>(info.minimum());
    auto const clamped((host < 0) ? 0 : (host > 1) ? 1 : host);
    return static_cast<Value>(info.minimum() + clamped * (info.maximum() - info.minimum()));
}

/// \brief The default, on the same edge as toHost() reports values.
inline double defaultToHost(ParameterID const parameterID, Info const &info)
{
    return toHost(parameterID, info, static_cast<Value>(info.default_()));
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief May a host put automation or modulation on this parameter?
///
/// \note No for the three that rebuild the spectral setup. Each defers through
/// `deferOrApplySpectralSetup()` and is applied only with the engine stopped, so
/// a change made while active ends in `clap_host::request_restart` -- a
/// deactivate and reactivate mid-playback, on the host's own schedule. The FFT
/// size is the reported latency as well (\see doc/tech/latency.md), and
/// clap/ext/latency.h says outright that the latency may change only during
/// `activate`. A promise the plugin cannot keep is worse than a missing lane.
///
/// \note The parameter is not unreachable: it is still in the list, still holds
/// and reports its value, still round-trips through state, and a host's generic
/// panel can still set it. CLAP_PARAM_IS_AUTOMATABLE governs automation and
/// modulation, not parameter events.
///
/// \note Deliberately not `ParameterInformation::isAutomatable()`, which exists
/// and means something else -- "has a non-empty range", i.e. some effect owns
/// this slot's parameter. Conflating the two would mark every parameter of an
/// empty slot non-automatable, which is what the note over `paramsInfo` rejects
/// for CLAP_PARAM_IS_HIDDEN. \see issue #171.
///
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Which release of the plugin first exported this parameter.
///
///   AUv2 hosts key automation on a parameter's *position* in the list rather
/// than on its id, and the wrapper's default order is the id order -- so a
/// parameter whose id lands in the middle of the existing ones pushes every
/// parameter after it along, and every automation lane a user has drawn in Logic
/// moves with it. Ordering by release first and by id within a release keeps
/// what shipped where it was and appends what is new.
///
/// \note Zero is everything this plugin has ever shipped. One is the two LFO
/// sub-parameters issue #159 exported: their ids are 5 and 6 of each LFO's
/// seven, which is the middle of every LFO block in the id order.
///
/// \note A number rather than a bool, because the next addition is a two and
/// wants the same treatment. \see CLAP_PLUGIN_AUV2_PARAM_ORDERING, whose header
/// carries the six-sines implementation this follows.
///                                           (22.08.2026.)
///
////////////////////////////////////////////////////////////////////////////////

inline unsigned parameterVersion(ParameterID const parameterID)
{
    if (parameterID.type() != ParameterID::LFOParameter)
        return 0;

    using LE::Parameters::IndexOf;
    constexpr auto firstAddedByIssue159(
        IndexOf<LE::Parameters::LFOImpl::Parameters, LE::Parameters::LFOImpl::SyncTypes>::value);

    return (parameterID.value._.lfo.lfoParameterIndex >= firstAddedByIssue159) ? 1 : 0;
}

inline bool isAutomatable(ParameterID const parameterID)
{
    if (parameterID.type() != ParameterID::GlobalParameter)
        return true;

    using LE::Parameters::IndexOf;
    using Globals = GlobalParameters::Parameters;
    switch (parameterID.value._.global.index)
    {
    case IndexOf<Globals, GlobalParameters::FFTSize>::value:
    case IndexOf<Globals, GlobalParameters::OverlapFactor>::value:
    case IndexOf<Globals, GlobalParameters::WindowFunction>::value:
        return false;
    default:
        return true;
    }
}
} // namespace CLAPEdge

} // namespace LE::SW

#endif // clapParameterEdge_hpp
