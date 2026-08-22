////////////////////////////////////////////////////////////////////////////////
///
/// \file printer.hpp
/// -----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef printer_hpp__272E159A_8E28_48C6_9775_6C7199CEA3CE__mrmlj
#define printer_hpp__272E159A_8E28_48C6_9775_6C7199CEA3CE__mrmlj
//------------------------------------------------------------------------------
#include "printer_fwd.hpp"

#include "boolean/printer.hpp"
#include "dynamic/printer.hpp"
#include "enumerated/printer.hpp"
#include "linear/printer.hpp"
#include "powerOfTwo/printer.hpp"
#include "symmetric/printer.hpp"
#include "trigger/printer.hpp"

#include "le/plugins/plugin.hpp" //...ugh...mrmlj...for Plugins::*AutomatedParameter...clean this up...

#include "le/utility/platformSpecifics.hpp"
#include "le/utility/tchar.hpp"

#include <optional>

namespace LE::Parameters
{

////////////////////////////////////////////////////////////////////////////////
//
// print()
// -------
//
////////////////////////////////////////////////////////////////////////////////

template <class Parameter, typename Source>
char const *print(Source const parameterValue, SW::Engine::Setup const &engineSetup,
                  PrintBuffer const &buffer)
{
    char const *const valueString(
        Detail::print<Parameter>(parameterValue, engineSetup, buffer, typename Parameter::Tag()));
    LE_ASSERT((valueString != buffer.begin()) ||
              (std::strlen(valueString) < unsigned(buffer.size())));
    return valueString;
}

#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.

struct PrinterBase
{
    typedef char const *result_type;
    template <class Parameter, typename Source>
    result_type operator()(Source const parameterValue) const
    {
        return Parameters::print<Parameter>(parameterValue, engineSetup, buffer);
    }
    Parameters::PrintBuffer const buffer;
    SW::Engine::Setup const &engineSetup;
}; // struct PrinterBase

struct ParameterPrinter
{
    using result_type = char const *;
    template <class Parameter> result_type operator()() const
    {
        return printer.operator()<Parameter>(parameterValue);
    }
    float const parameterValue;
    PrinterBase const printer;
}; // struct ParameterPrinter

struct Printer
{
    using result_type = ParameterPrinter::result_type;

    template <class Parameter> result_type operator()(Parameter const &parameter) const
    {
        char const *const pString(pValue_ ? printer.operator()<Parameter>(*pValue_)
                                          : printer.operator()<Parameter>(parameter.getValue()));
        return pString;
    }

    float const *LE_RESTRICT const pValue_;

    PrinterBase printer;
}; // struct Printer

////////////////////////////////////////////////////////////////////////////////
///
/// \struct AutomatedParameterPrinter
///
/// \brief Renders either a parameter's own value or a value somebody is asking
/// a what-if question about.
///
/// \note `forValue` engaged is the what-if: a host's automation lane asking
/// "what would 0.25 read as" rather than "what does this read as now". Nothing
/// but an optional distinguishes the two -- an `Internal` enumerator used to,
/// and it meant exactly "the value member is not a value", which is what
/// `std::nullopt` says without a second thing to keep in step.
///
/// \note Disengaged is not a value of `valueSource`, so a caller that has no
/// value to give still has to name the edge it *would* be on. That costs
/// nothing and keeps the two questions from sharing a spelling.
///
////////////////////////////////////////////////////////////////////////////////

struct AutomatedParameterPrinter
{
    using result_type = ParameterPrinter::result_type;

    /// \brief Which edge \p forValue is expressed on.
    enum ValueSource : std::uint8_t
    {
        NormalisedLinear,
        Linear,
        Unchanged
    };

    template <class Parameter> result_type operator()(Parameter const &parameter) const
    {
        if (!forValue)
            return printer.operator()<Parameter>(parameter.getValue());
        return this->operator()<Parameter>();
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The arm for a caller that has no parameter object to offer.
    ///
    ///   Every module and effect parameter goes through here: they are reached
    /// by index through a static invoker (EffectParameterPrinter), which knows
    /// the *type* and never has an instance. So the value has to have been
    /// supplied.
    ///
    /// \note The converted value is printed directly rather than assigned to a
    /// `Parameter` first. That temporary was the reason `paramsValueToText`
    /// could not honour the value it was given: default-constructing one runs
    /// `isValidValue` on a detached object, and a dynamic range finds its limits
    /// by walking from its own address to the owner that has none -- so a
    /// perfectly ordinary what-if question aborted a checked build. Nothing
    /// needed the object; `print()` takes a value.
    ///
    ////////////////////////////////////////////////////////////////////////////

    template <class Parameter> result_type operator()() const
    {
        LE_ASSERT_MSG(forValue, "Nothing to print: no parameter object and no value.");
        auto const automationValue(*forValue);
        typename Parameter::value_type parameterValue{};
        /// \note An ad hoc double-dispatch implementation to account for both
        /// the different automation/parameter marshaling ABIs as well as for
        /// different parameter types (with different printing logic).
        /// To be cleaned up, decoupled, .....
        ///                                   (09.12.2014.) (Domagoj Saric)
        switch (valueSource)
        {
        case NormalisedLinear:
            parameterValue =
                Plugins::NormalisedAutomatedParameter::convertAutomationToParameterValue<Parameter>(
                    automationValue);
            break;
        case Linear:
            parameterValue =
                Plugins::FullRangeAutomatedParameter ::convertAutomationToParameterValue<Parameter>(
                    automationValue);
            break;
        case Unchanged:
            parameterValue = Math::convert<typename Parameter::value_type>(automationValue);
            break;
            LE_DEFAULT_CASE_UNREACHABLE();
        }
        return printer.operator()<Parameter>(parameterValue);
    }

    /// \brief The value to answer about, or nothing to answer about the
    /// parameter's own.
    mutable std::optional<Plugins::AutomatedParameterValue> forValue;
    mutable ValueSource valueSource;
    PrinterBase printer;
}; // struct AutomatedParameterPrinter

#pragma warning(pop)

} // namespace LE::Parameters

#endif // printer_hpp
