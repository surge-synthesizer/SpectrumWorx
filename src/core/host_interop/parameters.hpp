////////////////////////////////////////////////////////////////////////////////
///
/// \file parameters.hpp
/// --------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef parameters_hpp__4BD9593B_D1BA_479C_B068_64F56F997500
#define parameters_hpp__4BD9593B_D1BA_479C_B068_64F56F997500
//------------------------------------------------------------------------------
#include "configuration/constants.hpp"

#include "le/spectrumworx/engine/parameters.hpp"
#include "le/utility/cstdint.hpp"

namespace LE
{
namespace SW
//------------------------------------------------------------------------------
{
namespace ParameterCounts
{
////////////////////////////////////////////////////////////////////////////////
///
/// \brief How many of an LFO's seven sub-parameters a host can address.
///
/// \note All of them, since issue #159. It was five from 2011 until 22.08.2026 --
/// "the SyncTypes and Waveform parameters are not yet exported for automation"
/// -- so the two that decide what an LFO *is* were the two a DAW could not
/// touch, and the editor reached them by a side channel of its own.
///
/// \note They are the last two of `LFOImpl::Parameters`, which is what makes
/// this a change to a count rather than to an identifier: every ParameterID a
/// host has ever been given still means what it meant, and the two new ones take
/// indices nothing had used. What *does* move is the position of everything
/// after them in a list sorted by id, which is why the AUv2 ordering extension
/// comes with this. \see CLAPEdge::parameterVersion().
///
////////////////////////////////////////////////////////////////////////////////

static std::uint8_t const lfoExportedParameters = 7;

static std::uint8_t const lfoParametersPerModule =
    lfoExportedParameters * (Constants::maxNumberOfParametersPerModule - 1);
static std::uint16_t const lfoParameters = lfoParametersPerModule * Constants::maxNumberOfModules;

static std::uint16_t const maxNumberOfParameters =
    GlobalParameters::Parameters::static_size + Constants::maxNumberOfModules +
    Constants::maxNumberOfModuleParameters + lfoParameters;

} //namespace ParameterCounts

//------------------------------------------------------------------------------
} // namespace SW
//------------------------------------------------------------------------------

//...mrmlj...required to be in the header only for getParameterProperties() and EditorKnob::paint()...
namespace SW::Engine
{
class Setup;
} // namespace SW::Engine
namespace Parameters
{
template <class Parameter> struct DisplayValueTransformer;

template <> struct DisplayValueTransformer<SW::GlobalParameters::InputGain>
{
    template <typename Source>
    static Source transform(Source const &value, SW::Engine::Setup const &)
    {
        return Math::normalisedLinear2dB(value);
    }
    static float inverse(float const dB, SW::Engine::Setup const &)
    {
        return Math::dB2NormalisedLinear(dB);
    }
    /// \note The leading space is significant, and these three were the only
    /// units in the tree without one -- every effect parameter declares
    /// `Unit<" dB">`, so the main knobs read "0dB" beside a module's "0.0 dB".
    /// The suffix is concatenated straight onto the value by both the editor
    /// (`EditorKnob::parameterValueText`) and `clap_plugin_params::value_to_text`,
    /// so it is the string itself that carries the separator. \see issue #94.
    using Suffix = UnitString<" dB">;
};

template <>
struct DisplayValueTransformer<SW::GlobalParameters::OutputGain>
    : DisplayValueTransformer<SW::GlobalParameters::InputGain>
{
};

template <> struct DisplayValueTransformer<SW::GlobalParameters::MixPercentage>
{
    template <typename Source>
    static Source transform(Source const &value, SW::Engine::Setup const &)
    {
        return Math::normalisedLinear2Percentage(value);
    }
    static float inverse(float const percentage, SW::Engine::Setup const &)
    {
        return Math::percentage2NormalisedLinear(percentage);
    }
    /// \note With the space, as above -- an effect's Wet is `Unit<" %">`.
    using Suffix = UnitString<" %">;
};

template <> struct DisplayValueTransformer<SW::GlobalParameters::OverlapFactor>
{
    static float transform(unsigned int const parameterValue, SW::Engine::Setup const &)
    {
        float const value(Math::convert<float>(parameterValue));
        float const percentage((value - 1) * 100 / value);
        return percentage;
    }

    /// \note The overlap *factor* that overlaps by this much: transform() is
    /// (f - 1) / f, so this is 1 / (1 - percentage/100). Not a power of two in
    /// general -- 93.8%, which is what a factor of 16 prints as at one decimal
    /// place, comes back as 16.13 -- and it does not have to be: the power-of-two
    /// parser snaps whatever this answers to the nearest representable factor.
    /// A hundred percent or more is not reachable by any factor, so it is the
    /// largest one.
    static float inverse(float const percentage, SW::Engine::Setup const &)
    {
        if (percentage >= 100)
            return static_cast<float>(SW::Engine::OverlapFactor::maximum());
        return 100 / (100 - percentage);
    }
    using Suffix = UnitString<"%">;
};
} // namespace Parameters

} // namespace LE

#endif // parameters_hpp
