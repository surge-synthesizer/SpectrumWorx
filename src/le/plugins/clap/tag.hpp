////////////////////////////////////////////////////////////////////////////////
///
/// \file tag.hpp
/// -------------
///
/// CLAP protocol tag and traits.
///
///   Mirrors le/plugins/au/tag.hpp and the trait block at the end of
/// le/plugins/au/plugin.hpp. The AU shape is the one to copy rather than the
/// VST 2.4 one, because both AU and CLAP identify a parameter by a stable ID
/// rather than by its position in a list -- which is the whole reason this port
/// targets CLAP: the parameter list changes when a module's effect changes.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef tag_hpp__6E2C1A54_9B07_43D8_A1F6_7C4E90B2D385
#define tag_hpp__6E2C1A54_9B07_43D8_A1F6_7C4E90B2D385
//------------------------------------------------------------------------------
#include "core/parameterID.hpp"

#include "le/parameters/conversion.hpp"
#include "le/parameters/printer.hpp"
#include "le/parameters/runtimeInformation.hpp"
#include "le/plugins/plugin.hpp"
#include "le/utility/staticLog2.hpp"

#include <array>
#include <cstring>
#include <type_traits>

namespace LE::Plugins
{

namespace Protocol
{
struct CLAP
{
};
} // namespace Protocol

////////////////////////////////////////////////////////////////////////////////
///
/// \struct ErrorCode<Protocol::CLAP>
///
///   CLAP's entry points answer bool, so the framework's richer codes collapse
/// on the way out. They are kept distinct here because Host2PluginInteropImpl
/// returns them and the distinctions are worth reading at the call site.
///
////////////////////////////////////////////////////////////////////////////////

/// \note Deliberately not bool, even though every CLAP entry point answers one.
/// Host2PluginInteropImpl declares makeErrorCode(ErrorCode) alongside
/// makeErrorCode(bool), and if the two are the same type those are the same
/// function. The plugin narrows to bool at the boundary, where `!= Success`
/// still says what it means.
template <> struct ErrorCode<Protocol::CLAP>
{
    using value_type = int;

    static value_type const Success = 0;
    static value_type const OutOfMemory = 1;
    static value_type const OutOfRange = 2;
    static value_type const CannotDoInCurrentContext = 3;
}; // struct ErrorCode<Protocol::CLAP>

/// \note Full range rather than normalised, as for AU: clap_param_info carries
/// min_value, max_value and default_value, so a parameter can be exported in
/// its own units and the host's generic panel shows real numbers.
template <> struct AutomatedParameterFor<Protocol::CLAP>
{
    using type = FullRangeAutomatedParameter;
};

////////////////////////////////////////////////////////////////////////////////
///
/// \class ParameterInformation<Protocol::CLAP>
///
/// \brief What ParameterInfoGetter fills in, in terms clap_param_info wants.
///
///   Deliberately not a clap_param_info itself. That struct carries an id, a
/// cookie and a module path that only the plugin knows, and it is memset by its
/// caller; this holds the part that comes from the parameter, and
/// SpectrumWorxCLAP copies it across.
///
////////////////////////////////////////////////////////////////////////////////

template <> class ParameterInformation<Protocol::CLAP>
{
  public:
    using result_type = void;
    using value_type = double;

    /// Long enough for the longest "M12.SomeParameterName" the name getter
    /// produces, and shorter than CLAP_NAME_SIZE so the copy out cannot clip.
    using NameBuf = std::array<char, 160>;

    template <class Parameter> result_type operator()()
    {
        using Tag = typename Parameter::Tag;
        using ParameterValue = typename Parameter::value_type;

        setValues<Parameter>(std::is_same<Tag, Parameters::PowerOfTwoParameterTag>());

        stepped_ = std::is_same<ParameterValue, bool>::value ||
                   std::is_same<Tag, Parameters::PowerOfTwoParameterTag>::value ||
                   std::is_same<Tag, Parameters::EnumeratedParameterTag>::value ||
                   std::is_integral<ParameterValue>::value;

        /// \note An empty range is how the framework spells "this parameter is
        /// not there in the current program" -- a module slot's parameter when
        /// the slot holds a narrower effect, say. CLAP has no such concept, so
        /// it becomes read-only and the host greys it out.
        automatable_ = (Parameter::unscaledMinimum != Parameter::unscaledMaximum) ||
                       (Parameter::unscaledMinimum != 0 /*LFO::PeriodScale*/);

        using Label = typename Parameters::DisplayValueTransformer<Parameter>::Suffix;
        setUnit(Label::c_str(), static_cast<unsigned int>(Label::size()));
    }

    void clear()
    {
        minimum_ = maximum_ = defaultValue_ = 0;
        stepped_ = false;
        automatable_ = false;
        meta_ = false;
        name_[0] = '\0';
        unit_[0] = '\0';
    }

    void set(AutomatedParameter::Info const &info)
    {
        minimum_ = info.minimum;
        maximum_ = info.maximum;
        defaultValue_ = info.default_;

        using Info = AutomatedParameter::Info;
        stepped_ = (info.type == Info::Boolean) || (info.type == Info::Trigger) ||
                   (info.type == Info::Enumerated) || (info.type == Info::Integer);
        automatable_ = true;

        setUnit(info.unit, static_cast<unsigned int>(std::strlen(info.unit)));
    }

    NameBuf *nameBuffer() { return &name_; }
    void nameSet() {}

    /// \note The framework's "meta" flag marks a parameter whose value changes
    /// what other parameters mean -- a module slot's effect, an LFO bound. CLAP
    /// has no direct equivalent; what it wants to know is that reading the list
    /// again may give different answers, which is what a rescan is for. Recorded
    /// so the plugin can decide.
    void markAsMeta() { meta_ = true; }
    bool isMeta() const { return meta_; }

    value_type minimum() const { return minimum_; }
    value_type maximum() const { return maximum_; }
    value_type default_() const { return defaultValue_; }
    bool isStepped() const { return stepped_; }
    bool isAutomatable() const { return automatable_; }

    char const *name() const { return name_.data(); }
    char const *unit() const { return unit_; }

  public:
    ParameterInformation() { clear(); }
    ParameterInformation(ParameterInformation const &) = delete;

  private:
    template <class Parameter> void setValues(std::true_type /*power of two*/)
    {
        //...mrmlj...as for AU: there is no way to say "only powers of two", so
        //...mrmlj...the exponent is exported and the printer puts it back.
        auto const minimumExponent(LE::Utility::staticLog2(Parameter::unscaledMinimum));
        auto const maximumExponent(LE::Utility::staticLog2(Parameter::unscaledMaximum));
        auto const defaultExponent(LE::Utility::staticLog2(Parameter::unscaledDefault));
        minimum_ = 0;
        maximum_ = static_cast<value_type>(maximumExponent - minimumExponent);
        defaultValue_ = static_cast<value_type>(defaultExponent - minimumExponent);
    }

    template <class Parameter> void setValues(std::false_type /*linear*/)
    {
        minimum_ = static_cast<value_type>(Parameter::minimum());
        maximum_ = static_cast<value_type>(Parameter::maximum());
        defaultValue_ = static_cast<value_type>(Parameter::default_());
    }

    void setUnit(char const *const unit, unsigned int const length)
    {
        auto const copied(std::min<unsigned int>(length, sizeof(unit_) - 1));
        std::memcpy(unit_, unit, copied);
        unit_[copied] = '\0';
    }

  private:
    value_type minimum_;
    value_type maximum_;
    value_type defaultValue_;

    bool stepped_;
    bool automatable_;
    bool meta_;

    NameBuf name_;
    char unit_[16];
}; // class ParameterInformation<Protocol::CLAP>

} // namespace LE::Plugins

#endif // tag_hpp
