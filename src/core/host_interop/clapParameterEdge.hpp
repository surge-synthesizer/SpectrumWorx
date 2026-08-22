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

#include "le/plugins/clap/tag.hpp"

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

inline bool isNormalised(ParameterID const parameterID) { return isNormalised(parameterID.type()); }

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
} // namespace CLAPEdge

} // namespace LE::SW

#endif // clapParameterEdge_hpp
