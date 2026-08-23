////////////////////////////////////////////////////////////////////////////////
///
/// moduleFactory.cpp
/// -----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "factory.hpp"

#include "configuration/constants.hpp"
#include "configuration/versionConfiguration.hpp"

#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/modules/finalImplementations.hpp"

#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/presets.hpp" // reportPresetProblem()
#include "le/spectrumworx/effects/configuration/indexToEffectImplMapping.hpp"
#include "le/spectrumworx/effects/configuration/includedEffects.hpp"
#include "le/spectrumworx/engine/moduleParameters.hpp"
#include "le/utility/rvalueReferences.hpp"
#include "le/utility/typeList.hpp"

// Implementation note:
//   Required for the (PVD)PitchMagnet::Target-unnecessarily-quantized
// workaround. Included here to avoid slowing down the compilation of
// modules that include gui.hpp.
//                                        (13.12.2011.) (Domagoj Saric)
#include "le/spectrumworx/effects/pitch_magnet/pitchMagnet.hpp"

#include "le/utility/assert.hpp"
#include "le/utility/intrusivePtr.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace LE::SW
{

namespace
{

/// \brief The widest effect in the build, counted in parameters of its own.
template <std::size_t... indices>
consteval std::uint8_t largestEffectParameterCount(std::index_sequence<indices...>)
{
    return std::max(
        {std::uint8_t(Effects::ImplForIndex<indices>::type::Parameters::static_size)...});
}

////////////////////////////////////////////////////////////////////////////
/// \note A module's parameters are the base block plus the effect's own, and
/// `maxNumberOfParametersPerModule` is the whole of what a host can address --
/// so an effect over the ceiling keeps every parameter and loses the automation
/// on the ones past it. Nothing said so: TuneWorx spent from 2011 to issue #156
/// with eight semitones the engine ran and no DAW could reach, and the way that
/// was found was a user reporting it.
///
///   Raise the ceiling to admit a wider effect rather than deleting this, and
/// read `constants.hpp` on what each raise costs first.
////////////////////////////////////////////////////////////////////////////
static_assert(
    largestEffectParameterCount(std::make_index_sequence<Effects::Constants::numberOfEffects>()) +
            Engine::ModuleParameters::numberOfBaseParameters <=
        Constants::maxNumberOfParametersPerModule,
    "an effect declares more parameters than a host can address");

////////////////////////////////////////////////////////////////////////////
/// \internal
/// \struct ModuleSizeGetter
////////////////////////////////////////////////////////////////////////////

template <class ModuleInterface> struct ModuleSizeGetter
{
    using result_type = std::uint16_t;

    template <class EffectIndex> result_type operator()(EffectIndex) const
    {
        using EffectImplementation = typename Effects::ImplForIndex<EffectIndex::value>::type;
        using ModuleImplementation = typename ModuleInterface::template Impl<EffectImplementation>;
        return sizeof(ModuleImplementation);
    }
}; // struct ModuleSizeGetter

////////////////////////////////////////////////////////////////////////////
/// \internal
/// \struct ModuleConstructor
////////////////////////////////////////////////////////////////////////////

/// \note No data members: create() mallocs, casts the block to a
/// `ModuleConstructor &` and calls through it, so the storage both operator()s
/// placement-new into is spelled `this`.
template <class ModuleInterface> struct ModuleConstructor
{
    /// \note No LE_RESTRICT: this is the two operator()s' return type, where the
    /// qualifier is ignored and warned about once per effect.
    using result_type = ModuleInterface *;

    template <class EffectIndex> result_type operator()(EffectIndex)
    {
        using EffectImplementation = typename Effects::ImplForIndex<EffectIndex::value>::type;
        using ModuleImplementation = typename ModuleInterface::template Impl<EffectImplementation>;
        result_type const result(new (this) ModuleImplementation(EffectIndex()));
        LE_ASSUME(result);
        return result;
    }

    // Implementation note:
    //   A "quick-patch" to handle the special case of the Armonizer effect
    // where we want the Wet parameter to default to 50%.
    //                                        (12.12.2011.) (Domagoj Saric)
    using ArmonizerIndex = std::integral_constant<unsigned int, 52>;
    result_type operator()(ArmonizerIndex)
    {
        typedef typename Effects::ImplForIndex<ArmonizerIndex::value>::type EffectImplementation;
        typedef typename ModuleInterface::template Impl<EffectImplementation> ModuleImplementation;
        static_assert(std::is_same<EffectImplementation, Effects::ArmonizerImpl>::value,
                      "Internal inconsistency");
        auto const pModule(new (this) ModuleImplementation(ArmonizerIndex()));
        LE_ASSUME(pModule);
        /// \note Through setBaseParameter() rather than into the parameter
        /// directly, so that the unmodulated value moves with it. Writing the
        /// storage behind the setter's back would leave this module claiming a
        /// Wet of 100 to the host and to a preset while sounding like 50.
        /// \note Qualified: ModuleInterface may override this privately to push
        /// the value into a widget, and there is no widget at factory time.
        pModule->Engine::ModuleParameters::setBaseParameter(
            LE::Parameters::IndexOf<Effects::BaseParameters::Parameters,
                                    Effects::BaseParameters::Wet>::value,
            50.0f);
        return pModule;
    }
}; // struct ModuleConstructor

////////////////////////////////////////////////////////////////////////////
/// \internal
/// \struct ModuleDestroyer
////////////////////////////////////////////////////////////////////////////

template <class ModuleInterface> struct ModuleDestroyer
{
    using result_type = void;

    template <class EffectIndex> result_type operator()(EffectIndex) const
    {
        using EffectImplementation = typename Effects::ImplForIndex<EffectIndex::value>::type;
        using ModuleImplementation = typename ModuleInterface::template Impl<EffectImplementation>;
        auto *const pModule(static_cast<ModuleImplementation *>(pInterface));
        pModule->~ModuleImplementation();
        std::free(pModule);
    }

    ModuleInterface *pInterface;
}; // struct ModuleDestroyer

} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
//
// ModuleFactory::create()
// -----------------------
//
////////////////////////////////////////////////////////////////////////////////

template <class ModuleInterface>
LE::Utility::IntrusivePtr<ModuleInterface> ModuleFactory::create(std::int8_t const effectIndex)
{
    std::int8_t const noModule(-1);
    if (effectIndex == noModule)
        return nullptr;
    LE_ASSUME(effectIndex >= 0);

    bool const moduleEnabled(Effects::includedEffects[effectIndex]);
    if (!moduleEnabled)
    {
        /// \note Was `GUI::warningMessageBox`, from a path a host's parameter
        /// event reaches -- a live branch raising a juce::AlertWindow from a path
        /// the audio thread can reach, held off only by `includedEffects` being a
        /// constexpr table of all `true`. It reports through the same counted
        /// reporter a preset uses, which is in this layer and raises nothing.
        /// (`LE_SW_FULL` stood above it, asserting the table's contents, and
        /// went with the demo SKU it gated on 04.08.2026.)
        reportPresetProblem(PresetProblem::EffectNotAvailable, Effects::effectName(effectIndex));
        return nullptr;
    }

    using SizeGetter = ModuleSizeGetter<ModuleInterface>;
    auto const storageSize(Utility::switchOn<Effects::ValidIndices>(effectIndex, SizeGetter()));
    void *const pStorage(std::malloc(storageSize));

    if (!pStorage) [[unlikely]]
        return nullptr;

    using Constructor = ModuleConstructor<ModuleInterface>;
    Constructor &moduleConstructor(*static_cast<Constructor *>(pStorage));
    return Utility::switchOn<Effects::ValidIndices>(effectIndex,
                                                    std::forward<Constructor>(moduleConstructor));
}

////////////////////////////////////////////////////////////////////////////////
//
// ModuleFactory::destroy()
// ------------------------
//
////////////////////////////////////////////////////////////////////////////////

template <class ModuleInterface> void ModuleFactory::destroy(ModuleInterface const &module)
{
    auto const effectIndex(module.effectTypeIndex());
    using Destroyer = ModuleDestroyer<ModuleInterface>;
    Utility::switchOn<Effects::ValidIndices>(effectIndex,
                                             Destroyer{const_cast<ModuleInterface *>(&module)});
}

template LE::Utility::IntrusivePtr<SW::Module> ModuleFactory::create(std::int8_t effectIndex);
template void ModuleFactory::destroy(SW::Module const &);

/// \note The definition lived in moduleDSP.hpp, under a
/// "//...mrmlj...for TalkBox4Unity" comment and with no `inline`. Every
/// translation unit that included the header emitted the symbol, which suited
/// a single-TU Unity build; making it inline instead just moved the problem,
/// because a release build inlines it everywhere and emits no out-of-line copy
/// for module.cpp and moduleChainImpl.cpp to call. It is an ordinary function
/// in one translation unit now, which is what moduleDSPAndGUI.cpp does for the
/// GUI build.

} // namespace LE::SW
