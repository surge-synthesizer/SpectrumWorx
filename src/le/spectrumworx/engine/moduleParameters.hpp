////////////////////////////////////////////////////////////////////////////////
///
/// \file moduleParameters.hpp
/// --------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef moduleParameters_hpp__F444F629_3EBC_4840_BFA4_817E1218B10D
#define moduleParameters_hpp__F444F629_3EBC_4840_BFA4_817E1218B10D
//------------------------------------------------------------------------------
#include "moduleNode.hpp"

#include "le/parameters/lfoImpl.hpp"
#include "le/parameters/parser_fwd.hpp"  //...mrmlj...ParseParameterValue
#include "le/parameters/printer_fwd.hpp" //...mrmlj...GetParameterValueString
#include "le/parameters/runtimeInformation.hpp"
#include "le/spectrumworx/effects/baseParameters.hpp"
#include "le/utility/platformSpecifics.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include "le/utility/span.hpp"

namespace LE
{

namespace Parameters
{
struct RuntimeInformation;
}

namespace SW
{
using ParameterInfo = Parameters::RuntimeInformation;

class ParametersLoader;
class ParametersSaver;

namespace Engine
{

using parameter_value_t = Parameters::LFOImpl::value_type;

class ModuleParameters : public ModuleNode
{
  public:
    using BaseParameters = Effects::BaseParameters::Parameters;

    static std::uint8_t const numberOfBaseParameters =
        BaseParameters::static_size; // ModuleDSP::Parameters::static_size;
    static std::uint8_t const numberOfNonLFOBaseParameters = 1 /* Bypass */;
    static std::uint8_t const numberOfLFOBaseParameters =
        numberOfBaseParameters - numberOfNonLFOBaseParameters;

  public:
    std::uint8_t numberOfParameters() const
    {
        return numberOfEffectSpecificParameters() + numberOfBaseParameters;
    }
    std::uint8_t numberOfEffectSpecificParameters() const
    {
        return metaData_.numberOfExtraParameters;
    }
    std::uint8_t numberOfLFOControledParameters() const
    {
        return numberOfEffectSpecificParameters() + numberOfLFOBaseParameters;
    }

    static std::uint8_t effectSpecificParameterIndex(std::uint8_t parameterIndex);

    ParameterInfo const &parameterInfo(std::uint8_t parameterIndex) const;
    ParameterInfo const &effectSpecificParameterInfo(std::uint8_t parameterIndex) const;

    std::uint8_t effectTypeIndex() const { return metaData_.typeIndex_; }

  public:
    bool bypass() const;

    BaseParameters &baseParameters() { return baseParameters_; }
    BaseParameters const &baseParameters() const { return baseParameters_; }

    // Implementation note:
    //   If chunks are not used/supported and/or the host generated UI is used,
    // the host application "manually" iterates over all parameters to
    // save/restore them so we must explicitly handle the case(s) of parameters
    // that do not actually exist (i.e. requested parameter index is greater
    // than the number of parameters provided by the effect).
    //                                        (26.06.2009.) (Domagoj Saric)
    /// \note `LE_AUX_VIRTUAL_SET` expanded to `virtual` and `SW::Module` overrode
    /// both setters to push the value into a widget. Nothing overrides them now,
    /// so they are ordinary functions -- which is the thing `dsp.cmake` says the
    /// deleted `LE_SW_GUI` flag used to decide, "which in a release build decided
    /// the layout of every module object, so the engine had two ABIs and only the
    /// debug ones matched".
    float getBaseParameter(std::uint8_t baseParameterIndex) const;
    float setBaseParameter(std::uint8_t baseParameterIndex, float value);

    float getEffectParameter(std::uint8_t effectParameterIndex) const;
    float setEffectParameter(std::uint8_t effectParameterIndex, float value);

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The value a parameter has when its LFO is off, remembered while it
    /// is on.
    ///
    ///   Written by a user edit, by host automation and by a preset load; never
    /// by the LFO, which writes the live parameter and nothing else. This is what
    /// `clap_plugin_params` reports and what a preset stores, so an LFO sweeping
    /// no longer moves either.
    ///
    /// \note Called *unmodulated* rather than *base* in the code, because "base
    /// parameters" already means the five shared ones -- Bypass, Gain, Wet and
    /// the two frequencies. doc/tech/threading_model.md §4 calls the same
    /// thing the base value, in the CLAP sense.
    ///
    /// \note There was no such value at all: `setEffectParameterFromLFOAux` wrote
    /// the LFO's output into the parameter itself, so an LFO *overwrote* rather
    /// than modulated. That is why `paramsValue` polled the sweep, why saving a
    /// preset with an LFO running froze that LFO's instantaneous output into the
    /// file, and why the interface disables a knob whose LFO is on -- there was
    /// nothing to drag to.
    ///
    /// \note Indexed as `lfo()` is: `numberOfLFOBaseParameters` entries for the
    /// LFO-able shared parameters (Bypass excluded, hence the -1), then one per
    /// effect parameter.
    ///
    ////////////////////////////////////////////////////////////////////////////

    float unmodulatedBaseParameter(std::uint8_t baseParameterIndex) const;
    float unmodulatedEffectParameter(std::uint8_t effectParameterIndex) const;

  protected:
    /// \brief Copies every live value into its unmodulated slot.
    ///
    /// \note Called once, from the most derived constructor, which is the first
    /// point at which the effect's own parameters hold their defaults. Before
    /// that there is nothing to copy.
    void captureUnmodulatedValues();

    /// \brief The live parameter only, leaving the unmodulated value alone.
    /// What the LFO writes through; nothing else should.
    float setBaseParameterLive(std::uint8_t baseParameterIndex, float value);
    float setEffectParameterLive(std::uint8_t effectParameterIndex, float value);

  public: // LFO section
    /// \note Until FMOD timing info functionality settles in, all LFO stuff
    /// will be crammed here.
    ///                                       (14.03.2014.) (Domagoj Saric)
    using LFO = LE::Parameters::LFOImpl;
    using LFOs = LE::Utility::Span<LFO>;

    void updateLFOs(LFO::Timer::TimingInformationChange);

    LFO &lfo(std::uint8_t lfoableParameterIndex);
    LFO const &lfo(std::uint8_t lfoableParameterIndex) const;

    LFO &baseLFO(std::uint8_t index)
    {
        LE_ASSUME(index < numberOfLFOBaseParameters);
        return lfos()[index];
    }
    LFO const &baseLFO(std::uint8_t index) const
    {
        return const_cast<ModuleParameters &>(*this).baseLFO(index);
    }

    LFO &effectLFO(std::uint8_t index) { return lfos()[numberOfLFOBaseParameters + index]; }
    LFO const &effectLFO(std::uint8_t index) const
    {
        return const_cast<ModuleParameters &>(*this).effectLFO(index);
    }

    static LFO::value_type normalisedToParameterValue(parameter_value_t normalisedValue,
                                                      ParameterInfo const &);
    static parameter_value_t parameterToNormalisedValue(LFO::value_type parameterValue,
                                                        ParameterInfo const &);

  protected:
    using LFOPlaceholder = std::aligned_storage<sizeof(LFO), __alignof(LFO)>::type;
    LFOs lfos() const;

  public: //...mrmlj...
    using ParameterInfos = std::array<ParameterInfo const, BaseParameters::static_size>;
    static ParameterInfos const &parameterInfos();

  public: //...mrmlj...AutomatedModuleImpl & getParameterValueString...
    using ParameterPrinter = LE::Parameters::AutomatedParameterPrinter;
    using ParameterParser = LE::Parameters::ParameterValueParser;
    struct EffectMetaData;
    EffectMetaData const &metaData() const { return metaData_; }

  public:
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.
#endif                  // _MSC_VER
    struct EffectMetaData
    {
        /// \note This carried an explicit `__fastcall` on the GNU side, added as
        /// a workaround for a 2013-era Clang crash. It is the same dead calling
        /// convention stage 0.6 deleted `LE_FASTCALL` for — meaningless on
        /// x86-64 SysV and on arm64 — but hand-written, so the sweep missed it.
        /// Clang accepts and ignores it on every target, which is why it
        /// survived a macOS build; GCC on aarch64 does not declare it at all and
        /// the typedef simply failed to parse. Gone, along with the
        /// `-Wignored-attribute` suppression that existed only to silence it.
        typedef char const *(GetParameterValueString)(std::uint8_t index,
                                                      ParameterPrinter const &)/*noexcept*/;

        /// \brief GetParameterValueString backwards, and here for the same
        /// reason: an effect's parameters are a compile time list this class
        /// deliberately does not know, so the one thing that does -- the effect's
        /// own instantiation -- leaves a function behind.
        typedef LE::Parameters::ParsedValue(ParseParameterValue)(
            std::uint8_t index, ParameterParser const &) /*noexcept*/;

        std::uint8_t const numberOfExtraParameters;
        std::uint8_t const typeIndex_;
        ParameterInfo const *LE_RESTRICT const pParameterInfos;
        GetParameterValueString &getParameterValueString;
        ParseParameterValue &parseParameterValue;

        /// \note `EffectMetaData( EffectMetaData const & ) = delete;` used to
        /// stand here to make it non-copyable. C++20 counts a user-*declared*
        /// constructor, deleted or not, against aggregate-ness, and
        /// MakeEffectMetaData initialises this as an aggregate. The const and
        /// reference members already make it non-assignable, and the only
        /// instances are the per-effect statics, handed out by reference.
    }; // struct EffectMetaData
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

  protected:
    ModuleParameters(
        //std::uint8_t moduleSlotIndex,
        EffectMetaData const &metadata, LFOPlaceholder *pLFOStorage,
        /// \note From the same place and sized the same way as the LFO storage:
        /// the most derived class holds an array whose length the effect decides.
        float *pUnmodulatedValues);

  public: // Presets
    void loadPresetParameters(ParametersLoader const &);
    void savePresetParameters(ParametersSaver const &) const;

  protected:
    void updateBaseParametersFromLFOs(LFO::Timer const &);
    void updateEffectParametersFromLFOs(LFO::Timer const &);

    //...mrmlj...make ModuleDSP::preProcess() call SW::Module::set*ParameterFromLFO()...
    //...mrmlj...and make ModuleChainImpl::preProcessAll() non template...
    ///
    /// \note These write the *live* parameter and leave the unmodulated value
    /// alone, which is the whole of the base/modulated split -- see
    /// unmodulatedBaseParameter() above.
    parameter_value_t setBaseParameterFromLFOAux(std::uint8_t parameterIndex, LFO::value_type);
    parameter_value_t setEffectParameterFromLFOAux(std::uint8_t parameterIndex, LFO::value_type);

  private:
    /// \note Nothing here is virtual for an interface's benefit: the engine tells
    /// no one anything, and the plugin publishes what the LFOs did into the
    /// ValueMailbox after the block. \see doc/tech/threading_model.md §1.
    LFO *constructLFOs(LFOPlaceholder *) const;

  private:
    /// \note We need to have local storage for caching shared parameters in
    /// order to support the current preset loading implementation (where a
    /// module is created and its parameters set before being inserted into the
    /// target chain - which in the separated DSP&GUI situation means that the
    /// parent/editor is not set for the ModuleUI instance in the moment
    /// setBaseParameter() is called during preset loading and so it cannot
    /// propagate the value being set upstream).
    ///                                       (20.10.2014.) (Domagoj Saric)
    //...mrmlj...svn r10423

    //std::uint8_t                             moduleSlotIndex_;
    EffectMetaData const &metaData_;
    BaseParameters baseParameters_;
    LFO *LE_RESTRICT const pLFOs_;
    /// One per LFO-able parameter, indexed as pLFOs_ is. See
    /// unmodulatedBaseParameter().
    float *LE_RESTRICT const pUnmodulatedValues_;
}; // class ModuleParameters

} // namespace Engine

} // namespace SW

} // namespace LE

#endif // automatedModule_hpp
