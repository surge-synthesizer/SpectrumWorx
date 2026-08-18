////////////////////////////////////////////////////////////////////////////////
///
/// \file uiElements.hpp
/// --------------------
///
///   Defines "placeholders" for data and functional user interface elements for
/// a particular Parameter class.
///
///  "Placeholders", that is declarations without default implementations, are
/// used to ensure that the user/programmer provides a proper implementation for
/// his/hers parameter class (so the error is caught by the compiler instead of
/// at runtime through wrong behaviour caused by the usage of a default
/// implementation).
///
/// \note "By the compiler" read "at link time" until 08.2026, and that is the
/// difference this file turns on: a placeholder specialised in a .cpp is a
/// definition the users of the parameter never see, which is ill-formed with no
/// diagnostic required and was 283 of the 285 warnings the tree emitted. Every
/// specialisation of one of these now belongs in the header that declares the
/// parameter. See UI_NAME.
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef uiElements_hpp__E78E35E8_D163_442F_84C0_19427B8844BA
#define uiElements_hpp__E78E35E8_D163_442F_84C0_19427B8844BA
//------------------------------------------------------------------------------
#include "linear/parameter.hpp"

#include "le/utility/platformSpecifics.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace LE::SW::Engine
{
class Setup;
} // namespace LE::SW::Engine

namespace LE::Parameters
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Name
///
/// \brief Placeholder for the name of the parameter (non-optional, must be
/// defined for each parameter).
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   Outside code accesses a particular parameter name through the name() free
// function template while a parameter's name is defined by specializing the
// string_ static data member of the Name class template. This approach
// minimizes verbosity of both name-fetching and name-defining code.
//                                            (22.02.2011.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////
///
/// \note The primary template is deliberately a declaration with no definition:
/// a parameter nobody has named does not compile where it is used. UI_NAME is
/// how one is named and where.

template <class Parameter> struct Name
{
    static char const string_[];
};

template <class Parameter> constexpr std::string_view name() { return Name<Parameter>::string_; }

////////////////////////////////////////////////////////////////////////////////
///
/// \class StreamingName
///
/// \brief The name a parameter is written to a file under, which is not the
/// same thing as the name it is shown under.
///
/// \note Until 08.2026 these were one string. `Name` fed both the editor and
/// `RuntimeInformation::name`, and `RuntimeInformation::name` is what
/// `ModuleParameters::{save,load}PresetParameters` key on -- so renaming a knob
/// silently re-keyed every preset that had ever named it, which then loaded a
/// default instead. The 2016 author knew: see the comment on the `Name` line in
/// `info()` (moduleImpl.hpp).
///
///   The default is the display name, because that *is* what every preset ever
/// saved contains: seeded here, this template is the pre-port name table, at the
/// point of definition rather than in a file beside it. Pin one with
/// STREAMING_NAME the moment a display name has to change, and the file keeps
/// saying what it always said.
///
/// \note Not a placeholder like `Name` -- deliberately. A parameter with no
/// streaming name of its own is the overwhelmingly common case and its answer is
/// correct; making it a link error would buy nothing and cost 147 restatements
/// of a string that is already there. What makes the default safe is
/// tests/parameters/streamingNameTests.cpp, which pins every one of them.
///                                           (01.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

/// \note Null, and not `Name<Parameter>::string_`, because `Name`'s definition
/// lives in another translation unit: seeding the default with it would ask
/// every parameter to instantiate a variable template it cannot see, which clang
/// says out loud (-Wundefined-var-template) six times over. The fallback is in
/// streamingName() instead, where it costs the same and is asked only of the
/// parameters that are actually streamed.
template <class Parameter> struct StreamingName
{
    static constexpr char const *string_{nullptr}; ///< null: the display name serves
};

template <class Parameter> constexpr char const *streamingName()
{
    return StreamingName<Parameter>::string_ ? StreamingName<Parameter>::string_
                                             : Name<Parameter>::string_;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \struct DiscreteValues
///
/// \brief Placeholder for individual value strings for discrete-valued
/// parameters.
///
////////////////////////////////////////////////////////////////////////////////

template <class Parameter> struct DiscreteValues
{
    /// \note The element is not itself const -- valueStrings() below fills one
    /// of these during constant evaluation, and a const element cannot be
    /// assigned even there. The array object is const, which is what matters.
    using Strings = std::array<char const *, Parameter::numberOfDiscreteValues>;
    static Strings const strings;

    /// \note What ParameterInfo carries, beside the nullptr a parameter with no
    /// value strings gives it. constexpr to match NonEnumeratedParameter's, now
    /// that ENUMERATED_PARAMETER_STRINGS produces a constant.
    static constexpr char const *LE_RESTRICT const *stringsBegin() { return strings.data(); }
};

namespace Detail ///< \internal
{

////////////////////////////////////////////////////////////////////////////////
///
/// \struct ValueString
/// \internal
/// \brief One enumerated value and the string that names it, as
/// ENUMERATED_PARAMETER_STRINGS is given them.
///
////////////////////////////////////////////////////////////////////////////////

struct ValueString
{
    template <class Value>
    consteval ValueString(Value const value, char const *const string)
        : value_(static_cast<std::uint8_t>(value)), string_(string)
    {
    }

    std::uint8_t value_;
    char const *string_;
}; // struct ValueString

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The strings of \p pairs, having checked that each one is against the
/// value at its own position.
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   Value strings for enumerated parameters are defined in a .cpp file, away
// from the parameter, so nothing stops the two lists from drifting apart -- and
// a string against the wrong value is a wrong preset rather than a crash. The
// check used to be one static_assert per value, emitted by a Boost.PP walk over
// the pair sequence. It is the same check, said once, over a list the compiler
// can read: a consteval function that throws is a compile error naming the
// parameter, which is what the static_assert was for.
//                                            (31.07.2026.) (SW port)
////////////////////////////////////////////////////////////////////////////////

template <class Parameter, std::size_t count>
consteval typename DiscreteValues<Parameter>::Strings
valueStrings(ValueString const (&pairs)[count])
{
    static_assert(count == Parameter::numberOfDiscreteValues,
                  "Wrong number of enumerated parameter value strings");

    typename DiscreteValues<Parameter>::Strings strings{};
    for (std::size_t index{0}; index < count; ++index)
    {
        if (pairs[index].value_ != index)
            throw "Incorrect order of enumerated parameter value-string pairs";
        strings[index] = pairs[index].string_;
    }
    return strings;
}

} // namespace Detail

////////////////////////////////////////////////////////////////////////////////
///
/// \struct ShortValues
///
/// \brief What an enumerated parameter's values are *read* as, where the full
/// value strings do not fit.
///
/// \details A popup menu is as wide as it needs to be and a module strip's combo
/// box is sixty pixels; "Main: >Thr >Side" fits one of those. So a value carries
/// two strings -- the one the menu lists it under and the one the widget shows
/// once it is chosen -- and everything else (the host's readout, a preset, the
/// knob menu) keeps the full one. \see issue #120.
///
/// \note **Empty by default, and asked about with a requires-expression** rather
/// than carrying StreamingName's null-pointer sentinel. That shape is right for a
/// `char const *`, whose default can be a null *value*; the default here would
/// have to be a null *address*, and `pointer ? *pointer : fallback` over
/// `&shortStrings` is a condition GCC can see through -- "the address of ... will
/// never be NULL", `-Werror=address`, which is a Linux-only build failure that
/// clang does not raise. There is nothing to be null now: either the
/// specialisation has the array or it does not.
///                                           (19.08.2026.) (SW port)
///
/// \note Seeding a default with `DiscreteValues<Parameter>::strings` would ask
/// every enumerated parameter to instantiate a variable template it may not have
/// seen, which is why the fallback is in shortValueStrings() rather than here --
/// StreamingName's argument, and `if constexpr` is what keeps the unused arm
/// uninstantiated.
///
/// \note And, as with STREAMING_NAME, the specialisation belongs in the header
/// that declares the parameter -- a translation unit that does not see it gets
/// the full strings in the widget rather than a diagnostic.
///
////////////////////////////////////////////////////////////////////////////////

template <class Parameter> struct ShortValues
{
};

/// \brief \p Parameter's abbreviations if it has any, its full value strings if
/// it has not.
template <class Parameter>
constexpr typename DiscreteValues<Parameter>::Strings const &shortValueStrings()
{
    if constexpr (requires { ShortValues<Parameter>::strings; })
        return ShortValues<Parameter>::strings;
    else
        return DiscreteValues<Parameter>::strings;
}

////////////////////////////////////////////////////////////////////////////////
///
/// 'Transforms' the value of a parameter into a value that should be used for
/// displaying on a user interface.
///
////////////////////////////////////////////////////////////////////////////////

namespace Detail
{
template <class TraitTag, class TraitsPack, class... DefaultTraits> struct GetTraitDefaulted;
}
template <class Parameter> struct DisplayValueTransformer
{
    template <typename Source>
    static Source const &transform(Source const &value, SW::Engine::Setup const &)
    {
        return value;
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief transform() run backwards: what a user typed, in display units,
    /// back into the units the parameter is stored in.
    ///
    /// \note Not a template, unlike transform(): a display value has been through
    /// a decimal string by the time it gets here, so it is a float whatever the
    /// parameter's own type is, and le/parameters/parser.hpp puts it back into
    /// that type once -- for every parameter -- rather than here, five times.
    ///
    /// \note Every specialisation of this template owes an inverse as well as a
    /// transform, and the pair is what makes text_to_value honest. Until 08.2026
    /// there was no inverse at all and the CLAP entry point declined to parse
    /// anything; what it had done before that was read the display units as if
    /// they were storage units, which clap-validator caught turning an input gain
    /// of 0.001 into a NaN.
    ///                                       (07.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    static float inverse(float const displayValue, SW::Engine::Setup const &)
    {
        return displayValue;
    }

    using Suffix = typename Detail::GetTraitDefaulted<Traits::Tag::Unit, typename Parameter::Traits,
                                                      typename Parameter::Defaults>::type;
}; // struct DisplayValueTransformer

/// \defgroup UIElementMacros UIElement verbosity reducing macros.
/// \ingroup UIElementMacros
/// \{

////////////////////////////////////////////////////////////////////////////////
///
/// \def ENUMERATED_PARAMETER_STRINGS
///
/// \brief Writes the declaration part of a parameter discrete value's string_
/// definition.
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   Because value strings for enumerated parameters are defined separately (in
// a .cpp file) from the enumerated parameter we need a mechanism that would
// prevent one to unnoticeably change the order of values at the parameter
// definition site and forget to also change the order of value strings thus
// getting an incorrect mapping between values and their string representations.
//   A first solution (up to revision 4632) was to use a separate
// DiscreteValue<>::string instantiation for each value instead of a single
// std::array<char const *, <numberOfDiscreteValues>>. This required the
// developer to explicitly state for which parameter value a particular string
// is for thereby eliminating the above issue. However this required more
// verbose discrete value name definitions and it added compile time and runtime
// overhead (it required boost::switch_ to fetch strings for values at runtime).
// For this reason the current solution does use a plain array of strings but
// the macro for defining the array also adds static assertions that verify that
// the strings are defined in the proper order.
//                                            (15.07.2011.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

///   The pairs are written { Value, "string" }, and the value is named
/// unqualified: the lambda opens a scope the parameter's enumerators are visible
/// in, which is the job the macro used to do by pasting `parameter::` in front
/// of each of them.
///
/// \note constexpr, and therefore in a header beside the parameter -- see the
/// note on UI_NAME below, which this shares.
#define ENUMERATED_PARAMETER_STRINGS(parentNameSpaceOrClass, parameter, ...)                       \
    template <>                                                                                    \
    constexpr DiscreteValues<parentNameSpaceOrClass::parameter>::Strings                           \
        DiscreteValues<parentNameSpaceOrClass::parameter>::strings{[] {                            \
            using enum parentNameSpaceOrClass::parameter::value_type;                              \
            return Detail::valueStrings<parentNameSpaceOrClass::parameter>({__VA_ARGS__});         \
        }()};

/// \note One closing brace, not two: the effect headers say
/// `namespace LE::SW::Effects`, which is a single scope to leave and re-enter.
#define EFFECT_ENUMERATED_PARAMETER_STRINGS(parentClass, parameter, ...)                           \
    }                                                                                              \
    namespace LE::Parameters                                                                       \
    {                                                                                              \
    ENUMERATED_PARAMETER_STRINGS(SW::Effects::parentClass, parameter, __VA_ARGS__)                                                                   \
    }                                                                                              \
    namespace LE::SW::Effects                                                                      \
    {

////////////////////////////////////////////////////////////////////////////////
///
/// \def ENUMERATED_PARAMETER_SHORT_STRINGS
///
/// \brief The same list again, abbreviated to what a module strip's combo box
/// can show. \see ShortValues, and issue #120.
///
/// \note Optional and written in the same shape as the full list, value by
/// value, so that the pair check reads both: an abbreviation against the wrong
/// value is the one bug worth a compile error here, and the full list already
/// pays for the machinery.
///
////////////////////////////////////////////////////////////////////////////////

#define ENUMERATED_PARAMETER_SHORT_STRINGS(parentNameSpaceOrClass, parameter, ...)                 \
    template <> struct ShortValues<parentNameSpaceOrClass::parameter>                              \
    {                                                                                              \
        static constexpr DiscreteValues<parentNameSpaceOrClass::parameter>::Strings strings{[] {   \
            using enum parentNameSpaceOrClass::parameter::value_type;                              \
            return Detail::valueStrings<parentNameSpaceOrClass::parameter>({__VA_ARGS__});         \
        }()};                                                                                      \
    };

#define EFFECT_ENUMERATED_PARAMETER_SHORT_STRINGS(parentClass, parameter, ...)                     \
    }                                                                                              \
    namespace LE::Parameters                                                                       \
    {                                                                                              \
    ENUMERATED_PARAMETER_SHORT_STRINGS(SW::Effects::parentClass, parameter, __VA_ARGS__)                                                             \
    }                                                                                              \
    namespace LE::SW::Effects                                                                      \
    {

////////////////////////////////////////////////////////////////////////////////
///
/// \def UI_NAME
///
/// \brief Names a parameter.
///
/// \note **In a header, beside the parameter**, for the reason STREAMING_NAME
/// gives below: the name is an explicit specialisation, so every translation
/// unit that instantiates name<Parameter>() has to see it. Until 08.2026 these
/// were out-of-line definitions in a .cpp, which every such translation unit
/// used without seeing -- ill-formed, no diagnostic required, and clang says so
/// 235 times over (-Wundefined-var-template). The five that had been declared in
/// a header (baseParametersUIElements.hpp) still warned from plugin2Host.cpp,
/// which does not include it: a declaration only travels if it is somewhere the
/// parameter's own users already look, and the only such place is the header
/// that declares the parameter.
///
///   constexpr rather than extern, so that it is a definition every translation
/// unit may hold, and so that name() is a constant expression rather than a
/// strlen. What checks that a parameter is named at all is now the warning
/// itself, promoted to an error by the baseline (src/CMakeLists.txt): the
/// placeholder primary template below has no definition, so naming nothing
/// fails the build at the point of use rather than at link time.
///                                           (04.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

#define UI_NAME(parameter, name) template <> constexpr char const Name<parameter>::string_[]{name};

#define EFFECT_PARAMETER_NAME(parameter, name)                                                     \
    }                                                                                              \
    namespace LE::Parameters                                                                       \
    {                                                                                              \
    UI_NAME(SW::Effects::parameter, name)                                                          \
    }                                                                                              \
    namespace LE::SW::Effects                                                                      \
    {

////////////////////////////////////////////////////////////////////////////////
///
/// \def STREAMING_NAME
///
/// \brief Pins the name a parameter is written to a file under, holding it still
/// while its display name moves.
///
/// \note **In a header, beside the parameter -- never in a .cpp.** Every
/// translation unit that instantiates the parameter table has to see this
/// specialisation; one that does not gets the primary template and streams the
/// parameter under its display name, which is precisely the breakage being
/// pinned against. Silent, per-translation-unit, and ill-formed besides.
/// tests/parameters/streamingNameTests.cpp catches it -- it reads the same
/// runtime table the writer does -- but the place not to make the mistake is
/// here. This was the first of the UIElements to say so and is now the rule for
/// all of them; UI_NAME has the general argument.
///
/// \note The whole class template is specialised rather than its member: a
/// member specialisation would leave the in-class default declared as well, and
/// two answers to "what is this parameter called on disk" is exactly the bug
/// this exists to prevent.
///
////////////////////////////////////////////////////////////////////////////////

#define STREAMING_NAME(parameter, name)                                                            \
    template <> struct StreamingName<parameter>                                                    \
    {                                                                                              \
        static constexpr char const *string_{name};                                                \
    };

#define EFFECT_PARAMETER_STREAMING_NAME(parameter, name)                                           \
    }                                                                                              \
    namespace LE::Parameters                                                                       \
    {                                                                                              \
    STREAMING_NAME(SW::Effects::parameter, name)                                                   \
    }                                                                                              \
    namespace LE::SW::Effects                                                                      \
    {

/// \} // UIElementMacros

} // namespace LE::Parameters

namespace boost
{
template <std::size_t N>
char const *LE_RESTRICT (*addressof(char const *LE_RESTRICT (&strings)[N])) [N] { return &strings; }
} // namespace boost
#endif // uiElements_hpp
