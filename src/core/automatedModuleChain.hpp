////////////////////////////////////////////////////////////////////////////////
///
/// \file automatedModuleChain.hpp
/// ------------------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef automatedModuleChain_hpp__46188EA4_1820_4042_A387_537f2C9BEB85
#define automatedModuleChain_hpp__46188EA4_1820_4042_A387_537f2C9BEB85
//------------------------------------------------------------------------------
#include "modules/factory.hpp"

#include "configuration/constants.hpp"
#include "core/host_interop/parameters.hpp" //...mrmlj...for Program

#include "le/parameters/linear/parameter.hpp"
#include "le/parameters/parameter.hpp"
#include "le/spectrumworx/effects/configuration/constants.hpp"
#include "le/spectrumworx/engine/moduleChainImpl.hpp"
#include "le/spectrumworx/engine/moduleParameters.hpp"
#include "le/utility/cstdint.hpp"

#include "le/utility/intrusivePtr.hpp"

#include <array>

namespace LE::SW
{

//...mrmlj...defined outside of the AutomatedModuleChain class to enable forward declarations to speed up compilation...clean this up...
std::int8_t constexpr noModule = -1;

LE_DEFINE_PARAMETER(ModuleChainParameter, Parameters::LinearSignedInteger,
                    Parameters::Traits::Minimum<noModule>,
                    Parameters::Traits::Maximum<Effects::Constants::numberOfEffects - 1>,
                    Parameters::Traits::Default<noModule>);

////////////////////////////////////////////////////////////////////////////////
///
/// \class AutomatedModuleChain
///
////////////////////////////////////////////////////////////////////////////////

/// \note final because the two `delete`s of one -- publish.cpp and the CLAP's
/// retire handler -- go through this exact static type, and ModuleNode grows a
/// virtual in a checked build purely so dynamic_cast works. Saying so is what
/// makes those deletes provably right rather than merely true.
class AutomatedModuleChain final : public Engine::ModuleChainImpl
{
  public:
    /// \note A typed constant rather than the unnamed enum it was. The enum
    /// made `slotIsFull ? module.effectTypeIndex() : noModule` -- the shape
    /// both of the conditionals below have -- mix an enumerated and a
    /// non-enumerated type, which is what -Wextra reports and what an effect
    /// index compared against this constant should never have been. It is the
    /// module chain parameter's own value type, which is what the two
    /// conditionals produce and what every caller of these already holds.
    static std::int8_t constexpr noModule = SW::noModule;

    //...mrmlj...GUI only chains don't hold ModuleDSPs...
    //using ModulePtr  = Engine::ModuleChainImpl::      pointer;
    //using ModuleCPtr = Engine::ModuleChainImpl::const_pointer;
    using Module = Engine::ModuleParameters;
    using ModulePtr = LE::Utility::IntrusivePtr<Module>;
    using ModuleCPtr = LE::Utility::IntrusivePtr<Module const>;

    static std::uint8_t const maximumSize = Constants::maxNumberOfModules;

  public:
#if defined(_MSC_VER) && _MSC_VER < 1900
    AutomatedModuleChain() {}
    AutomatedModuleChain(AutomatedModuleChain &&other)
        : Engine::ModuleChainImpl(std::forward<Engine::ModuleChainImpl>(other))
    {
    }
#else
    using Engine::ModuleChainImpl::ModuleChainImpl;
#endif // _MSC_VER
    using ModuleChainImpl::operator=;

    ModuleChainParameter getParameterForIndex(std::uint8_t moduleIndex) const;

    ModulePtr module(std::uint8_t index);
    ModuleCPtr module(std::uint8_t index) const;

    template <class ActualModule>
    LE::Utility::IntrusivePtr<ActualModule> moduleAs(std::uint8_t const index)
    {
        auto const pModule(ModuleChainImpl::module(index));
        return (!isEnd(pModule)) ? &Engine::actualModule<ActualModule>(*pModule) : nullptr;
    }

    template <class ActualModule>
    LE::Utility::IntrusivePtr<ActualModule const> moduleAs(std::uint8_t const index) const
    {
        auto const pModule(ModuleChainImpl::module(index));
        return (!isEnd(pModule))
                   ? &Engine::actualModule<
                         typename std::remove_const<ActualModule>::type /*const*/>(*pModule)
                   : nullptr;
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Puts the effect \p newValue names into slot \p moduleIndex.
    ///
    /// \param ppDisplaced receives whatever this call took out of circulation --
    ///        the module that was in the slot, or, when the new one could not be
    ///        built or initialised, the new one -- carrying **one reference,
    ///        transferred**. Set to null when there was nothing.
    ///
    /// \note That parameter is not optional, and it is the whole point of this
    /// function's shape. Unlinking a node drops the chain's reference to it, and
    /// when nothing else holds one -- a slot the *host* changed, with the window
    /// shut and no strip on screen -- that was the last, so the unlink ran the
    /// deleter: a `delete` and a `HeapSharedStorage` free, **inside `process()`**,
    /// going around the retire protocol that exists so that nothing is ever
    /// destroyed on the audio thread (threading_model.md §5).
    ///
    ///   `installModuleInSlot()` has taken the reference first and handed it back
    /// for as long as that protocol has existed; this is the same three lines at
    /// the one route into the chain that never got them. A caller that owns the
    /// engine outright -- either copy on the main thread -- may simply release
    /// what it is given.
    ///
    ////////////////////////////////////////////////////////////////////////////
    template <class ModuleInitialiser>
    std::pair<LE::Utility::IntrusivePtr<typename ModuleInitialiser::Module>, std::int8_t>
    setParameter(std::uint8_t const moduleIndex, std::int8_t const newValue,
                 ModuleInitialiser const &initialise,
                 typename ModuleInitialiser::Module **const ppDisplaced)
    {
        using Module = typename ModuleInitialiser::Module;
        using Engine::actualModule;

        *ppDisplaced = nullptr;

        auto const effectIndex(newValue);

        auto const pCurrentModuleNode(ModuleChainImpl::module(moduleIndex));
        auto const pCurrentModule(
            !isEnd(pCurrentModuleNode) ? &actualModule<Module>(*pCurrentModuleNode) : nullptr);
        std::int8_t const currentEffect(
            !isEnd(pCurrentModuleNode) ? static_cast<std::int8_t>(pCurrentModule->effectTypeIndex())
                                       : noModule);
        if (currentEffect == newValue)
        {
            return std::make_pair(pCurrentModule, currentEffect);
        }
        else if (effectIndex == noModule)
        {
            takeOutOfCirculation(pCurrentModule, ppDisplaced);
            this->remove(*pCurrentModuleNode);
            return std::make_pair(nullptr, noModule);
        }

        auto const pNewModule(ModuleFactory::create<Module>(effectIndex));

        if (pNewModule && initialise(*pNewModule, moduleIndex))
        {
            takeOutOfCirculation(pCurrentModule, ppDisplaced);
            insertAtAndReplace(pCurrentModuleNode, Engine::node(*pNewModule));
            return std::make_pair(pNewModule, effectIndex);
        }

        /// \note And a module that was built and could not be initialised leaves
        /// by the same door. The chain never saw it, so the local pointer above
        /// holds the only reference and letting it expire is the same free() on
        /// the same thread.
        takeOutOfCirculation(pNewModule.get(), ppDisplaced);

        return std::make_pair(pCurrentModule, currentEffect);
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The same, destroying the displaced module on the spot.
    ///
    /// \note **Never from `process()`.** For a caller that owns the chain
    /// outright and is on a thread where a `free()` is allowed: either `Program`
    /// on the main thread, and the test harnesses driving an engine of their own.
    /// The engine's chain, while the plugin is activated, is not such a caller --
    /// see the overload above, which is the one it uses.
    ///
    ////////////////////////////////////////////////////////////////////////////
    template <class ModuleInitialiser>
    std::pair<LE::Utility::IntrusivePtr<typename ModuleInitialiser::Module>, std::int8_t>
    setParameter(std::uint8_t const moduleIndex, std::int8_t const newValue,
                 ModuleInitialiser const &initialise)
    {
        typename ModuleInitialiser::Module *pDisplaced(nullptr);
        auto result(setParameter(moduleIndex, newValue, initialise, &pDisplaced));
        if (pDisplaced)
            intrusive_ptr_release(&Engine::node(*pDisplaced));
        return result;
    }

  private:
    /// \brief Adds a reference to \p pModule and hands it to \p ppDisplaced, so
    /// that whatever happens to the chain next cannot be the thing that frees it.
    template <class Module>
    static void takeOutOfCirculation(Module *const pModule, Module **const ppDisplaced)
    {
        if (!pModule)
            return;
        intrusive_ptr_add_ref(&Engine::node(*pModule));
        *ppDisplaced = pModule;
    }
}; // class AutomatedModuleChain

class Program
{
  public:
    using Parameters = GlobalParameters::Parameters;
    using ModuleChain = AutomatedModuleChain;
    using Name = std::array<char, 24>; //...mrmlj...kVstMaxProgNameLen

    Parameters &parameters() { return parameters_; }
    Parameters const &parameters() const { return const_cast<Program &>(*this).parameters(); }

    ModuleChain &moduleChain() { return moduleChain_; }
    ModuleChain const &moduleChain() const { return const_cast<Program &>(*this).moduleChain(); }

    Name &name() { return name_; }
    Name const &name() const { return const_cast<Program &>(*this).name(); }

  private:
    ModuleChain moduleChain_;
    Parameters parameters_;

    /// \note Value-initialised, and it matters. Program has no constructor, so a
    /// plain `Name name_;` default-initialises -- which for a std::array<char>
    /// leaves all 24 bytes indeterminate. A preset load is the only thing that
    /// ever writes it (copyPresetName, from presetLoading.cpp and
    /// presetFile.hpp), so on an instance that has not loaded one
    /// `SpectrumWorxEditor::currentProgramName()` returned 24 indeterminate
    /// bytes with no guarantee of a terminator anywhere in them -- and the
    /// preset browser hands that straight to juce::String for the Save-As field.
    ///
    ///   It reads empty far more often than not, which is what made it a ghost:
    /// a first instance in a fresh process gets zeroed pages from the OS, and
    /// one created after the process has churned memory does not.
    Name name_{};
}; // class Program

} // namespace LE::SW

#endif // automatedModuleChain_hpp
