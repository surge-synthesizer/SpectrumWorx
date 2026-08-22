////////////////////////////////////////////////////////////////////////////////
///
/// \file plugin2Host.hpp
/// ---------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef plugin2Host_hpp__8B5D322C_926B_4B2E_8504_8F1124B6598F
#define plugin2Host_hpp__8B5D322C_926B_4B2E_8504_8F1124B6598F
//------------------------------------------------------------------------------
#include "parameters.hpp"

#include "core/parameterID.hpp"

#include "le/parameters/printer.hpp"
#include "le/parameters/parametersUtilities.hpp" // IndexOf, Clang
#include "le/plugins/plugin.hpp"

#include "le/utility/cstdint.hpp"

#include "le/utility/intrusivePtr.hpp"
#include "le/utility/span.hpp"

namespace LE
{

namespace Parameters
{
//...mrmlj...required to be in the header only for getParameterProperties() and EditorKnob::paint()...

/// \note The overlap factor prints as a percentage rather than as a factor, and
/// that comes from DisplayValueTransformer like every other display transform.
} // namespace Parameters

namespace SW
{

namespace Engine
{
class ModuleParameters;
}
class AutomatedModuleChain;
class Program;

namespace GUI
{
bool isThisTheGUIThread();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief What a parameter no effect currently owns is called, and displays as.
///
/// \note One spelling, because the two have to agree: a host that is told a
/// parameter is named this and shows a value of something else has been told two
/// different things about the same absent parameter. SpectrumWorxCLAP also reads
/// it back -- see paramsTextToValue.
///
////////////////////////////////////////////////////////////////////////////////

inline constexpr char notAvailable[]{"N/A"};

/// \brief What a slot with no effect in it displays as, and reads back from.
inline constexpr char emptySlot[]{"<empty>"};

////////////////////////////////////////////////////////////////////////////////
///
/// \class Plugin2HostInteropControler
///
////////////////////////////////////////////////////////////////////////////////

class Plugin2HostInteropControler
{
  public:
    using Parameters = GlobalParameters::Parameters;
    using Module = Engine::ModuleParameters;

    template <class Parameter>
    void globalParameterChanged(Parameter const parameter, bool const asDiscreteGesture)
    {
        //...mrmlj...In and Out gain parameters only appear to be linear 'by chance' (because of the chosen minimum and maximum values)...
        globalParameterChanged(
            LE::Parameters::IndexOf<GlobalParameters::Parameters, Parameter>::value,
            Plugins::FullRangeAutomatedParameter ::convertParameterToAutomationValue(parameter),
            Plugins::NormalisedAutomatedParameter::convertParameterToAutomationValue(parameter),
            asDiscreteGesture);
    }

    void automatedParameterChanged(ParameterID::LFO, float value) const;
    void automatedParameterChanged(Module const &, std::uint8_t moduleIndex,
                                   std::uint8_t moduleParameterIndex, float parameterValue) const;
    void modulesChanged(AutomatedModuleChain const &, std::uint8_t firstModuleIndex,
                        std::uint8_t lastModuleIndex) const;
    void moduleChangedByUser(std::uint8_t chainParameterIndex, Module const *) const;
    /// \see the definition for why the effect index alone is the useful form.
    void moduleChangedByUser(std::uint8_t chainParameterIndex, std::int8_t effectIndex) const;

    static bool canParameterBeAutomated(Plugins::ParameterIndex, void const * /*pContext*/)
    {
        return true;
    }
    static bool canParameterBeAutomated(Plugins::ParameterID, void const * /*pContext*/)
    {
        return true;
    }

    struct ParameterValueForAutomation
    {
        using value_type = Plugins::AutomatedParameter::value_type;

        value_type fullRange;
        value_type normalised;
    }; // ParameterValueForAutomation

    template <typename TargetParameterSelector> static TargetParameterSelector make(ParameterID);

  public: // Protocol specific functionality to be implemented by derived classes.
    // notifications
    virtual void automatedParameterBeginEdit(ParameterID) const = 0;
    virtual void automatedParameterEndEdit(ParameterID) const = 0;
    virtual void gestureBegin(char const *description) const = 0;
    virtual void gestureEnd() const = 0;
    virtual void automatedParameterChanged(ParameterID, ParameterValueForAutomation) const = 0;
    virtual void moduleChanged(std::uint8_t moduleIndex, Module const *) const = 0;
    virtual bool parameterListChanged() const = 0;
    virtual void presetChangeBegin() const = 0;
    virtual void presetChangeEnd() const = 0;
    virtual bool latencyChanged() = 0;

    // queries

  private:
    void globalParameterChanged(
        std::uint8_t index, ParameterValueForAutomation::value_type fullRange,
        ParameterValueForAutomation::value_type normalised,
        bool
            asDiscreteGesture //....mrmlj...ugh cleanup....for distinction between knobs and comboboxes
    );
}; // class Plugin2HostInteropControler

////////////////////////////////////////////////////////////////////////////////
///
/// \class Plugin2HostPassiveInteropController
///
////////////////////////////////////////////////////////////////////////////////

class Plugin2HostPassiveInteropController
{
  public:
    ////////////////////////////////////////////////////////////////////////////
    // Parameters
    ////////////////////////////////////////////////////////////////////////////

  public: // Parameter info
    //static void getParameterDisplay( ParameterID, LE::Utility::Span<char> text , Engine::Setup const &, Plugins::AutomatedParameterValue const * pOptionalValue, Program const & );
    static void getParameterLabel(ParameterID, LE::Utility::Span<char> label, Program const *);
    static void getParameterName(ParameterID, LE::Utility::Span<char> name, Program const *);
    static void getParameterIDs(LE::Utility::Span<Plugins::ParameterID> ids, Program const *);

    static std::uint16_t numberOfParameters(Program const *);

  public: // Indexed parameter functors.
    /// \note
    ///   Up to revision 5763 a different approach for "printing" parameter
    /// values was used. Until then the parameter value to string conversion
    /// functionality was only needed for GUI code so each (parameter)
    /// widget stored a simple callback that directly converted a parameter
    /// value into its string representation. When extended support for
    /// parameter metadata/"generic UIs" (which includes exporting
    /// parameter values as strings through the plugin APIs) was added this
    /// was replaced with using the new "plugin parameter metadata"
    /// functionality.
    ///                                   (27.01.2012.) (Domagoj Saric)
    struct ParameterLabelGetter;
    struct ParameterValueStringGetter;
    struct ParameterNameGetter;
}; // class Plugin2HostPassiveInteropController

////////////////////////////////////////////////////////////////////////////////
///
/// \class Plugin2HostInteropControler::ParameterLabelGetter
///
////////////////////////////////////////////////////////////////////////////////

struct Plugin2HostPassiveInteropController::ParameterLabelGetter
{
    /// \note Was `char const *LE_RESTRICT const`, a return type carrying two
    /// top level qualifiers that a return type cannot carry -- both ignored,
    /// and warned about once per overload per translation unit. Spelled as the
    /// sibling getter below already spells it.
    using result_type = char const *;

    result_type operator()(ParameterID::Global, Program const *) const;
    result_type operator()(ParameterID::ModuleChain, Program const *) const { return nullptr; }
    result_type operator()(ParameterID::Module, Program const *) const;
    result_type operator()(ParameterID::LFO, Program const *) const;
}; // struct Plugin2HostPassiveInteropController::ParameterLabelGetter

////////////////////////////////////////////////////////////////////////////////
///
/// \struct Plugin2HostPassiveInteropController::ParameterValueStringGetter
///
////////////////////////////////////////////////////////////////////////////////

#pragma warning(push)
#pragma warning(disable : 4510) // Default constructor could not be generated.
#pragma warning(disable                                                                            \
                : 4610) // Class can never be instantiated - user-defined constructor required.
struct Plugin2HostPassiveInteropController::ParameterValueStringGetter
{
    using result_type = char const *;

    result_type operator()(ParameterID::Global, Program const *) const;
    result_type operator()(ParameterID::ModuleChain, Program const *) const;
    //result_type operator()( ParameterID::Module     , Program const * ) const;
    result_type operator()(ParameterID::LFO, Program const *) const;

    mutable Parameters::AutomatedParameterPrinter printer;
}; // struct ParameterValueStringGetter
#pragma warning(pop)

//...mrmlj...MSVC12u5: bad codegen if we move these functions into the .cpp file...
template <>
LE_FORCEINLINE Plugins::ParameterID
Plugin2HostInteropControler::make<Plugins::ParameterID>(SW::ParameterID const selector)
{
    return {selector.binaryValue};
}
template <>
LE_FORCEINLINE Plugins::ParameterIndex
Plugin2HostInteropControler::make<Plugins::ParameterIndex>(SW::ParameterID const selector)
{
    return parameterIndexFromBinaryID(selector.binaryValue);
}

} // namespace SW

} // namespace LE

#endif // plugin2Host_hpp
