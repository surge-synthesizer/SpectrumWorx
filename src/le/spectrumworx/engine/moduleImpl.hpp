////////////////////////////////////////////////////////////////////////////////
///
/// \file moduleImpl.hpp
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef moduleImpl_hpp__0876E0CC_6291_428B_AC32_93B28EDD1251
#define moduleImpl_hpp__0876E0CC_6291_428B_AC32_93B28EDD1251
//------------------------------------------------------------------------------
#include "configuration.hpp"
#include "module.hpp"

#include "le/parameters/lfo.hpp"
#include "le/plugins/plugin.hpp" //...ugh...mrmlj...for Plugins::*AutomatedParameter usage in printer.hpp...clean this up...
#include "le/parameters/parametersUtilities.hpp"
#include "le/parameters/parser.hpp"  // parsers for module parameters
#include "le/parameters/printer.hpp" // printers for module parameters
#include "le/parameters/uiElements.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/parameters/trigger/tag.hpp"
#include "le/spectrumworx/effects/effects.hpp"
#include "le/spectrumworx/engine/channelData.hpp"
#include "le/spectrumworx/engine/moduleParameters.hpp"
#include "le/utility/buffers.hpp"
#include "le/utility/platformSpecifics.hpp"
#include "le/utility/span.hpp"

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility> // std::(make_)index_sequence
//------------------------------------------------------------------------------
#ifdef _MSC_VER // msvc12 does not have std::make_index_sequence
__if_not_exists(std::make_index_sequence)
{
    namespace std
    {
    template <std::uint16_t...> struct index_sequence
    {
        using type = index_sequence;
    };

    template <class S1, class S2> struct concat_impl;
    template <std::uint16_t... I1, std::uint16_t... I2>
    struct concat_impl<index_sequence<I1...>, index_sequence<I2...>>
        : index_sequence<I1..., (sizeof...(I1) + I2)...>
    {
    };

    template <class S1, class S2> using concatenate = typename concat_impl<S1, S2>::type;

    template <std::uint16_t N> struct make_index_sequence_impl;
    template <std::uint16_t N>
    struct make_index_sequence_impl
        : concatenate<typename make_index_sequence_impl<N / 2>::type,
                      typename make_index_sequence_impl<N - N / 2>::type>
    {
    };

    template <> struct make_index_sequence_impl<0> : index_sequence<>
    {
    };
    template <> struct make_index_sequence_impl<1> : index_sequence<0>
    {
    };

    template <std::uint16_t N>
    using make_index_sequence = typename make_index_sequence_impl<N>::type;
    } // namespace std
} // __if_not_exists( std::make_index_sequence )
#endif
namespace LE
{

namespace Parameters
{
template <class Parameter> struct DiscreteValues;
template <class Parameter> struct DisplayValueTransformer;
template <class Parameter> struct Name;

struct PowerOfTwoParameterTag;
} // namespace Parameters

namespace SW::Engine
{

struct StorageFactors;

namespace Detail ///< \internal
{
using offset_t = std::uint8_t;
using index_t = std::size_t;
using ParameterInfo = Parameters::RuntimeInformation;

//...mrmlj...ugh...cleanup and document this...
//...mrmlj...C++11 prototypes/experiments at creating non-trivial
//...variadic-length arrays w/o macros and limited maximum lengths...
template <typename Parameters, typename Data, typename Indices> struct array_aux;
#if 1 // the shorter version below does not yet work even with Clang 3.5
template <typename Parameters, index_t... Indices>
struct array_aux<Parameters, ParameterInfo, std::index_sequence<Indices...>>
{
    static std::uint16_t const N = sizeof...(Indices);
    using DataArray = std::array<ParameterInfo const, N>;
    static DataArray const data;
}; // struct array_aux

template <typename Parameters, index_t... Indices>
struct array_aux<Parameters, offset_t, std::index_sequence<Indices...>>
{
    static std::uint16_t const N = sizeof...(Indices);
    using DataArray = std::array<offset_t const, N>;
    static DataArray const data;
}; // struct array_aux

// terminating partial specialisations
template <typename Parameters> struct array_aux<Parameters, ParameterInfo, std::index_sequence<>>
{
    static constexpr ParameterInfo const *const data = nullptr;
}; // struct array_aux
template <typename Parameters> struct array_aux<Parameters, offset_t, std::index_sequence<>>
{
    static constexpr offset_t const *const data = nullptr;
}; // struct array_aux
#else // disabled/does not work
template <typename Parameters, typename Data, index_t... Indices>
struct array_aux<Parameters, Data, std::index_sequence<Indices...>>
{
    static std::uint16_t const N = sizeof...(Indices);
    using DataArray = std::array<Data const, N>;

    static DataArray const data;
}; // struct array_aux

// terminating partial specialisation
template <typename Parameters, typename Data>
struct array_aux<Parameters, Data, std::index_sequence<>>
{
    using DataArray = std::array<Data const, 0>;
    static DataArray const data;
}; // struct array_aux
template <typename Parameters, typename Data>
typename array_aux<Parameters, Data, std::index_sequence<>>::DataArray constexpr array_aux<
    Parameters, Data, std::index_sequence<>>::data;
#endif

/// \note Was `&static_cast<Parameters const *>(nullptr)->get<...>()`, with its
/// own "...this is actually UB and sooner or later Clang will start
/// miscompiling this...fix it ASAP..." beside it since 2016. GCC 15 is what
/// finally said it out loud -- -Wnonnull, once per parameter per effect, 142
/// times -- and the warning and the correctness fix are the same edit.
///
///   A real object and the difference between two of its addresses. It costs one
/// default-constructed parameter list per parameter, once, during static
/// initialisation of the offset table that calls this; nothing runs it again.
template <class Parameters, unsigned index> offset_t valueOffsetGetter()
{
    Parameters const parameters;
    auto const *const base(reinterpret_cast<char const *>(&parameters));
    auto const *const member(reinterpret_cast<char const *>(
        &parameters.template get<typename Parameters::template ParameterAt<index>::type>()));
    return static_cast<offset_t>(member - base);
}
template <typename Parameters, index_t... Indices>
typename array_aux<Parameters, offset_t, std::index_sequence<Indices...>>::DataArray const
    array_aux<Parameters, offset_t, std::index_sequence<Indices...>>::data = {
        {valueOffsetGetter<Parameters, Indices>()...}};

//...mrmlj...cleanup and document this...

template <typename ParameterTypeTag> struct ParameterType;

template <> struct ParameterType<Parameters::BooleanParameterTag>
{
    static ParameterInfo::Type constexpr type = ParameterInfo::Boolean;
};
template <> struct ParameterType<Parameters::TriggerParameterTag>
{
    static ParameterInfo::Type constexpr type = ParameterInfo::Trigger;
};
template <> struct ParameterType<Parameters::LinearIntegerParameterTag>
{
    static ParameterInfo::Type constexpr type = ParameterInfo::Integer;
};
//template <> struct ParameterType<Parameters::LinearSignedInteger         > { static ParameterInfo::Type constexpr type = ParameterInfo::Integer      ; };
//template <> struct ParameterType<Parameters::LinearUnsignedInteger       > { static ParameterInfo::Type constexpr type = ParameterInfo::Integer      ; };
template <> struct ParameterType<Parameters::SymmetricIntegerParameterTag>
{
    static ParameterInfo::Type constexpr type = ParameterInfo::Integer;
};
template <> struct ParameterType<Parameters::EnumeratedParameterTag>
{
    static ParameterInfo::Type constexpr type = ParameterInfo::Enumerated;
};
template <> struct ParameterType<Parameters::LinearFloatParameterTag>
{
    static ParameterInfo::Type constexpr type = ParameterInfo::FloatingPoint;
};
template <> struct ParameterType<Parameters::SymmetricFloatParameterTag>
{
    static ParameterInfo::Type constexpr type = ParameterInfo::FloatingPoint;
};

struct NonEnumeratedParameter
{
    static constexpr char const *LE_RESTRICT const *stringsBegin() { return nullptr; }
};
template <class Parameter, typename Tag> struct EnumeratedValueStrings
{
    using type = NonEnumeratedParameter;
};
template <class Parameter>
struct EnumeratedValueStrings<Parameter, Parameters::EnumeratedParameterTag>
{
    using type = LE::Parameters::DiscreteValues<Parameter>;
};

template <class Parameter> constexpr ParameterInfo info()
{
    static_assert(
        !std::is_same<typename Parameter::Tag, LE::Parameters::PowerOfTwoParameterTag>::value,
        "Meaningful only for linear parameters." //...mrmlj...
    );

    using Tag = typename Parameter::Tag;

    return {ParameterType<Tag>::type,

            static_cast<float>(Parameter::unscaledMinimum) / Parameter::rangeValuesDenominator,
            static_cast<float>(Parameter::unscaledMaximum) / Parameter::rangeValuesDenominator,
            static_cast<float>(Parameter::unscaledDefault) / Parameter::rangeValuesDenominator,

            /// \note The display name, and beside it the name presets are keyed
            /// on. Two fields, so that renaming a knob does not re-key every file
            /// that named it.
            Parameters::Name<Parameter>::string_, Parameters::streamingName<Parameter>(),

            Parameters::DisplayValueTransformer<Parameter>::Suffix::c_str(),
            EnumeratedValueStrings<Parameter, Tag>::type::stringsBegin()};
}

template <typename Parameters, index_t... Indices>
typename array_aux<Parameters, ParameterInfo, std::index_sequence<Indices...>>::DataArray const
    array_aux<Parameters, ParameterInfo, std::index_sequence<Indices...>>::data = {
        {info<typename LE::Parameters::ParameterAt<Parameters, Indices>>()...}};

template <class Parameters>
struct ParametersInformation
    : array_aux<Parameters, ParameterInfo, std::make_index_sequence<Parameters::static_size>>
{
};

/// \note Was BOOST_MPL_HAS_XXX_TRAIT_DEF( ChannelState ) -- the SFINAE
/// nested-type probe, generated by a macro that arrived here transitively and
/// from a library nothing else in this header used.
template <class T> struct has_ChannelState : std::bool_constant < requires
{
    typename T::ChannelState;
}>{};

using ChannelDataProxy = Engine::ModuleDSP::ChannelDataProxy;

struct MakeChannelStateHolder
{
    template <class Effect> struct ChannelStates
    {
        using ChannelState = typename Effect::ChannelState;
        using ChannelStateRange = LE::Utility::Span<ChannelState>;

        void LE_FORCEINLINE callProcess(Effect const &effect, std::uint8_t const channel,
                                        ChannelDataProxy const &data,
                                        Engine::Setup const &setup) const
        {
            effect.process(channelStates_[channel], data, setup);
        }

        LE_FORCEINLINE void callReset()
        {
#ifndef _MSC_VER
#endif // _MSC_VER
            for (auto &channelState : channelStates_)
                channelState.reset();
        }

        ////////////////////////////////////////////////////////////////////////
        ///
        /// \brief One seed per channel, for the effects that hold a generator.
        ///
        /// \note Detected rather than required: an effect declares
        /// `void seed( std::uint64_t )` on its ChannelState if it draws, and the
        /// fifty-odd that do not are not asked to grow an empty override.
        ///
        /// \note The source is drawn from either way, so that giving an existing
        /// effect a generator does not shift what a *later* module in the chain
        /// receives. What a module consumes depends on its shape -- how many LFOs,
        /// how many channels -- and not on whether it happens to want the numbers.
        ///
        ////////////////////////////////////////////////////////////////////////
        LE_FORCEINLINE void callSeed(Math::Rng &source)
        {
            for (auto &channelState : channelStates_)
            {
                auto const seed(source.next());
                if constexpr (requires { channelState.seed(seed); })
                    channelState.seed(seed);
            }
        }

        static std::uint16_t const sizeOfChannelState = sizeof(ChannelState);
        static std::uint32_t channelStateRequiredStorage(Engine::StorageFactors const &factors)
        {
            return ChannelState::requiredStorage(factors);
        }

        LE_FORCEINLINE void resize(Engine::Storage storage, Engine::StorageFactors const &factors)
        {
            LE_ASSUME(factors.numberOfChannels <= 16);
            char *const pChannelStatesBegin(storage.begin());
            char *const pChannelStatesEnd(
                storage.advance_begin(sizeof(ChannelState) * factors.numberOfChannels).begin());
            channelStates_ =
                ChannelStateRange(reinterpret_cast<ChannelState *>(pChannelStatesBegin),
                                  reinterpret_cast<ChannelState *>(pChannelStatesEnd));
            LE_ASSERT(unsigned(channelStates_.size()) == factors.numberOfChannels);
#ifndef _MSC_VER
#endif // _MSC_VER
            for (auto &channelState : channelStates_)
            {
                LE_ASSERT(reinterpret_cast<char *>(&channelState) < storage.end());
                ChannelState *LE_RESTRICT const pNewChannelState(new (&channelState) ChannelState);
                LE_ASSUME(pNewChannelState);
                pNewChannelState->resize(factors, storage);
            }
        }

        ChannelStateRange channelStates_;
    }; // struct ChannelStates
}; // struct MakeChannelStateHolder

struct MakeEmptyChannelStateHolder
{
    template <class Effect> struct ChannelStates
    {
        void LE_FORCEINLINE callProcess(Effect const &effect, std::uint8_t /*channel*/,
                                        ChannelDataProxy const &data,
                                        Engine::Setup const &setup) const
        {
            effect.process(data, setup);
            ((void)effect);
        }

        static void callReset() {}

        /// \note Nothing drawn, because there are no channel states to seed --
        /// see the note on the other holder about why the count matters.
        static void callSeed(Math::Rng &) {}

        static std::uint8_t const sizeOfChannelState = 0;
        static std::uint8_t channelStateRequiredStorage(Engine::StorageFactors const &)
        {
            return 0;
        }

        static void resize(Engine::Storage const &, Engine::StorageFactors const &) {}
    }; // struct ChannelStates
}; // struct MakeEmptyChannelStateHolder

/// \note The `__fastcall` this carried is gone for the reason given over
/// `EffectMetaData::GetParameterValueString`, which is the type this function
/// has to match: a dead calling convention that Clang accepts and ignores and
/// that GCC on aarch64 does not declare at all. The two spellings had to agree,
/// and now they agree on not having one.
template <class Parameters> struct EffectParameterPrinter
{
    static char const *print(std::uint8_t const parameterIndex,
                             LE::Parameters::AutomatedParameterPrinter const &printer)
    {
        LE_ASSUME(parameterIndex < Parameters::static_size);
        return LE::Parameters::invokeFunctorOnIndexedParameter<Parameters>(
            parameterIndex, std::forward<LE::Parameters::AutomatedParameterPrinter const>(printer));
    }
}; // class EffectParameterPrinter

/// \note The other direction, and the same shape. \see EffectMetaData.
template <class Parameters> struct EffectParameterParser
{
    static LE::Parameters::ParsedValue parse(std::uint8_t const parameterIndex,
                                             LE::Parameters::ParameterValueParser const &parser)
    {
        LE_ASSUME(parameterIndex < Parameters::static_size);
        return LE::Parameters::invokeFunctorOnIndexedParameter<Parameters>(parameterIndex, parser);
    }
}; // class EffectParameterParser

template <class Effect, typename TypeIndex> struct MakeEffectMetaData
{
    static ModuleParameters::EffectMetaData const data;
};

template <class Effect, typename TypeIndex>
ModuleParameters::EffectMetaData const MakeEffectMetaData<Effect, TypeIndex>::data = {
    Effect::Parameters::static_size, TypeIndex::value,
    &ParametersInformation<typename Effect::Parameters>::data[0],
    EffectParameterPrinter<typename Effect::Parameters>::print,
    EffectParameterParser<typename Effect::Parameters>::parse};

////////////////////////////////////////////////////////////////////////////
// EffectParameterOffsets<Effect>
////////////////////////////////////////////////////////////////////////////

template <class Effect> struct EffectParameterOffsets
{
    /// \note
    /// Up to revision 5812 a different approach was used where the
    /// ModuleDSP class template was also parameterized with an
    /// AutomatedParameter class and used that information to internally
    /// provide all conversion to and from automation values as well as
    /// preset loading and saving. This was replaced with the type-erased
    /// ParameterInfo approach in order to remove automation and preset
    /// related knowledge from the ModuleDSP class template so that it would
    /// be easier to provide SpectrumWorx versions without preset
    /// functionality.
    ///                                   (06.02.2012.) (Domagoj Saric)
    //typedef std::array<std::uint8_t const, Effect::Parameters::static_size> ParameterOffsets;
    using ParameterOffsets = std::uint8_t const *const;
    static ParameterOffsets const parameterOffsets;
}; // class EffectParameterOffsets

template <class Effect>
typename EffectParameterOffsets<Effect>::ParameterOffsets const
    EffectParameterOffsets<Effect>::parameterOffsets =
        &array_aux<typename Effect::Parameters, offset_t,
                   std::make_index_sequence<Effect::Parameters::static_size>>::data[0];
} // namespace Detail

////////////////////////////////////////////////////////////////////////////////
///
/// \class ModuleEffectImpl ...mrmlj...cleanup this naming mess...
///
/// \brief Implements the ModuleDSP interface(s) for a given effect.
///
/// Provides the following:
/// - implements the Module interface
/// - holds multiple channel state/data
/// - holds the effect specific UI components (parameter widgets).
///
////////////////////////////////////////////////////////////////////////////////

#pragma warning(push)
#pragma warning(disable : 4127) // Conditional expression is constant.
#pragma warning(                                                                                   \
    disable                                                                                        \
    : 4373) // Previous versions of the compiler did not override when parameters only differed by const/volatile qualifiers.

template <class EffectParam, class Base> class ModuleEffectImpl : public Base
{
  public:
    using Effect = EffectParam;

  private:
    using ChannelStatesHolder = typename std::conditional_t<
        Engine::Detail::has_ChannelState<Effect>::value, Engine::Detail::MakeChannelStateHolder,
        Engine::Detail::MakeEmptyChannelStateHolder>::template ChannelStates<Effect>;

  public:
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
#endif // __clang__
    template <typename EffectTypeIndex, typename... T>
    ModuleEffectImpl(EffectTypeIndex, T &&...args)
        : Base(std::forward<T>(args)...,
               Engine::Detail::MakeEffectMetaData<Effect, EffectTypeIndex>::data, &lfos_[0],
               &unmodulatedValues_[0],
               Engine::Detail::EffectParameterOffsets<Effect>::parameterOffsets,
               static_cast<std::uint16_t>(
                   reinterpret_cast<char const *>(&effect().parameters()) -
                   reinterpret_cast<char const *>(static_cast<Base const *>(this))))
#ifndef NDEBUG
          ,
          setupCalled_(false)
#endif // NDEBUG
    {
        /// \note Here and not in ModuleParameters' constructor: this is the first
        /// point at which `effect_` exists and its parameters hold their
        /// defaults, which is what an unmodulated value starts as.
        this->captureUnmodulatedValues();
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif // __clang__

  public:
    Effect &effect() { return effect_; }
    Effect const &effect() const { return effect_; }

    ChannelStatesHolder const &channelStatesHolder() const { return channelStatesHolder_; }

  protected: // Module process interface implementation.
    LE_FORCEINLINE void doPreProcess(Setup const &engineSetup) override
    {
        effect().setup(ModuleDSP::workingRange(), engineSetup);
#ifndef NDEBUG
        setupCalled_ = true;
#endif
    }

    LE_FORCEINLINE void doProcess(std::uint8_t const channel,
                                  Engine::ModuleDSP::ChannelDataProxy const data,
                                  Setup const &setup) const override
    {
        LE_ASSERT(setupCalled_);
        channelStatesHolder_.callProcess(effect(), channel, data, setup);
    }

  public: //...mrmlj...
    LE_NOINLINE void reset() override final { channelStatesHolder_.callReset(); }

  private:
    LE_NOINLINE void doSeedChannelStates(Math::Rng &source) override final
    {
        channelStatesHolder_.callSeed(source);
    }

  private:
    bool resize(StorageFactors const &factors) override final
    {
        //...mrmlj...au uninitialise...LE_ASSERT( factors.complete() );

        if (ChannelStatesHolder::sizeOfChannelState == 0)
        {
            LE_ASSERT(channelStatesHolder_.channelStateRequiredStorage(factors) == 0);
            return true;
        }

        if (this->allocateStorage(factors, channelStatesHolder_.sizeOfChannelState,
                                  channelStatesHolder_.channelStateRequiredStorage(factors)))
            [[likely]]
        {
            channelStatesHolder_.resize(this->storage(), factors);
            ModuleEffectImpl<Effect, Base>::reset();
            return true;
        }

        return false;
    }

  private:
    Effect effect_;
    ChannelStatesHolder channelStatesHolder_;

    /// \note Only allocate the storage for all (base + effect) LFOs in the
    /// implementation class and let the base class construct all the LFOs. The
    /// other approach is to have the base class allocate and construct the
    /// base parameter LFOs and the implementation class the LFOs for the effect
    /// specific parameters. The first approach is better as all the LFOs are in
    /// a single, contiguous location (and can thus be accessed through a single
    /// pointer) and the LFO array construction code is generated only in a
    /// single location (smaller code and faster compiles).
    ///                                       (23.04.2015.) (Domagoj Saric)
    using LFOStorage =
        std::array<ModuleParameters::LFOPlaceholder,
                   ModuleParameters::numberOfLFOBaseParameters + Effect::Parameters::static_size>;
    LFOStorage lfos_;

    /// \note One per LFO-able parameter, sized and indexed exactly as lfos_ is:
    /// the value each of those parameters has when its LFO is off. See
    /// ModuleParameters::unmodulatedBaseParameter().
    std::array<float, ModuleParameters::numberOfLFOBaseParameters + Effect::Parameters::static_size>
        unmodulatedValues_;

#ifndef NDEBUG
    bool setupCalled_;
#endif
}; // class ModuleEffectImpl

#pragma warning(pop)

template <class Effect> class ModuleDSP::Impl final : public ModuleEffectImpl<Effect, ModuleDSP>
{
  public:
    /// \note The `#if (_MSC_VER < 1900) && !defined(__clang__)` that used to
    /// select a hand-written forwarding constructor here stood for "VS2013,
    /// which has no inherited constructors". GCC satisfies it vacuously —
    /// undefined `_MSC_VER` preprocesses to 0 — and then failed on the branch
    /// body, which named the base as a bare `ModuleEffectImpl`. VS2013 predates
    /// everything this project now requires, so the branch is gone rather than
    /// re-gated.
    using ModuleEffectImpl<Effect, ModuleDSP>::ModuleEffectImpl;
}; // class ModuleDSP::Impl

/// \note parameterInfos() is defined in moduleParameters.cpp, and must be:
/// callers outside this header see the declaration alone and need a real symbol,
/// where an inline definition can only ever satisfy its own translation unit.

} // namespace SW::Engine

} // namespace LE

#endif // moduleImpl_hpp
