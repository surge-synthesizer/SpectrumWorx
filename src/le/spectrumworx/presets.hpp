////////////////////////////////////////////////////////////////////////////////
///
/// \file presets.hpp
/// -----------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef presets_hpp__6021812F_90A0_4BFC_A0EF_6413D7485312
#define presets_hpp__6021812F_90A0_4BFC_A0EF_6413D7485312
//------------------------------------------------------------------------------
#include "configuration/constants.hpp"

/// \note These were `#ifndef _MSC_VER`, the second of them commented "for eager
/// compilers" -- 2016 MSVC did not look names up in a template definition until
/// instantiation, so it did not need what the templates below name and the
/// includes were withheld from it. A conforming MSVC is eager too, and the
/// header did not compile on one: `GlobalParameters::Parameters` and
/// `Engine::actualModule` were undeclared at loadPreset()'s definition, which
/// MSVC reported as a cascade of syntax errors rather than as missing names.
///                                           (05.08.2026.) (SW port)
#include "configuration/versionConfiguration.hpp"

#include "le/math/conversion.hpp"
#include "le/parameters/parametersUtilities.hpp"
/// `isAnEvent`, which is what "an event always streams off" is spelt with.
#include "le/parameters/trigger/tag.hpp"
#include "le/spectrumworx/engine/parameters.hpp"
#include "le/utility/countof.hpp"
#include "le/utility/lexicalCast.hpp"
#include "le/utility/platformSpecifics.hpp"
#include "le/utility/tchar.hpp"

#include <tinyxml/tinyxml.h>

#include <functional>
#include <optional>

#include "le/parameters/parametersUtilities.hpp"
#include "le/utility/intrusivePtr.hpp"
#include "le/utility/ignoreUnused.hpp"
#include <string_view>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <type_traits>
#include <string_view>
#include <utility> // rvalues

namespace LE
{

namespace Parameters
{
class LFOImpl;
template <class Parameter> struct Name;
template <class Parameter> struct StreamingName;
template <class Parameter> constexpr char const *streamingName();
} // namespace Parameters

namespace SW
{

class SpectrumWorx;

////////////////////////////////////////////////////////////////////////////////
///
/// \enum PresetProblem
///
/// \brief Everything that can be wrong with a preset, and where it goes.
///
/// \note These were `GUI::warningMessageBox` calls made from inside the engine,
/// one dialog per problem. Two things wrong with that. It is a layering
/// inversion -- `sw-dsp` reaching up into the GUI -- and, more concretely, a
/// 2009-2011 preset does not mention parameters that the 2016 effects grew: the
/// 303 committed factory presets raise **722** MissingParameter reports between
/// them, which as dialogs is a wall of them in front of a user who opened a
/// bank.
///
///   So the preset layer says what happened and the caller decides. The default
/// reporter is still the message box, so a plugin behaves as it did; the corpus
/// test counts instead, which is how the number above is known.
///
////////////////////////////////////////////////////////////////////////////////

enum struct PresetProblem : std::uint8_t
{
    LoadFailed,         ///< not a preset, or the parse failed
    SaveFailed,         ///< the file could not be written
    FutureFormat,       ///< written by a newer SpectrumWorx than this one
    UnknownEffect,      ///< names an effect this build does not have
    EffectNotAvailable, ///< names an effect this edition excludes
    MissingParameter,   ///< the effect has a parameter the preset does not mention
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The preset carries a value this build cannot place.
    ///
    /// \note The opposite of MissingParameter, and the reason that one can be
    /// kept quiet. "The file does not mention a parameter" is the format working
    /// -- the effect grew it later and the default applies. "The file mentions
    /// something nothing asked for" is data being thrown away: a renamed
    /// parameter, a changed streaming name, a reader that stopped recognising an
    /// element. The preset will not sound the way it was saved, and that is worth
    /// interrupting somebody for.
    ///
    ///   It needs the loader to *notice*, which it did not: reading is pull, one
    /// lookup per parameter the effect has, so an element nobody asked for was
    /// never visited. See ParametersLoader::reportUnreadParameters().
    ///                                       (02.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    UnknownParameter,
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The preset names an audio file for the side channel that could not
    /// be loaded -- missing, moved, or in a format this build cannot read.
    ///
    /// \note The user's business, and nameable, which is what makes it worth
    /// reporting: the file has a name, and finding it again is something only
    /// they can do. The sample is cleared rather than left as whatever the
    /// previous preset loaded (presetLoading.cpp).
    ///
    ///   It raised a modal box of its own until 08.08.2026, from inside the
    /// load, which is how a host restoring a session came to be stopped by a
    /// dialog nobody had asked for.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    SampleNotLoaded,
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The preset holds more modules than this build has slots for, and
    /// the ones past the last slot were not loaded.
    ///
    /// \note Legitimately a *later* version's file rather than a damaged one, and
    /// reported for that reason: everything up to the last slot loads and plays,
    /// and what is missing is nameable. \see ParametersLoader::loadModuleChain.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    TooManyModules
    /// \note A `TempoSyncedLFOWithoutTempo` stood here, for a preset with
    /// tempo-synced LFOs loaded into a host that reports no transport. That is
    /// not a problem: such a host gets 120 BPM in four four, which is a defined
    /// answer, and the standalone is one of them. See the note in
    /// presetLoading.cpp's moduleChainFinished().
    ///                                       (02.08.2026.) (SW port)
};

/// \param detail the effect or parameter name where there is one, else empty.
using PresetProblemReporter = void (*)(PresetProblem, std::string_view detail);

/// \brief Installs a reporter, returning the previous one. `[main-thread]`, and
/// deliberately not a stack: a caller that wants nesting keeps what this
/// returned and puts it back.
PresetProblemReporter setPresetProblemReporter(PresetProblemReporter);

void reportPresetProblem(PresetProblem, std::string_view detail = {});

////////////////////////////////////////////////////////////////////////////////
///
/// \struct PresetLoadReport
///
/// \brief What one load ran into, counted.
///
/// \note The default reporter was a `GUI::warningMessageBox` per problem, which
/// was wrong three ways. It is a layering inversion -- the preset parser reaching
/// up into the GUI. It is a *storm*: a 2011 preset does not mention parameters its
/// effect grew later, and the 303 factory banks raise 722 `MissingParameter`
/// reports between them, so opening a bank meant that many dialogs. And once the
/// session state became the preset serialisation, it happened on a host restoring
/// a project -- without anyone asking, on whatever thread the host chose, possibly
/// before there is a window to put a dialog in front of.
///
///   So the default counts, and the caller decides. `GUI::loadPreset` raises one
/// summary when a *user* opened something; `stateLoad` stays silent, a session
/// restore being nobody's business. A test needs to install nothing to be quiet:
/// before this, one that forgot leaked 809 `juce::AsyncUpdater`s.
///
////////////////////////////////////////////////////////////////////////////////

struct PresetLoadReport
{
    unsigned int missingParameters{0};
    unsigned int unknownParameters{0};
    unsigned int unknownEffects{0};
    unsigned int unavailableEffects{0};
    unsigned int failures{0};         ///< LoadFailed, SaveFailed, FutureFormat
    unsigned int samplesNotLoaded{0}; ///< the audio file it names is not loadable
    unsigned int modulesDropped{0};   ///< more modules in it than there are slots

    /// The first `detail` seen, so a summary can name one thing rather than none.
    std::string firstDetail;

    unsigned int total() const
    {
        return missingParameters + unknownParameters + unknownEffects + unavailableEffects +
               failures + samplesNotLoaded + modulesDropped;
    }

    explicit operator bool() const { return total() != 0; }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Whether any of this is the user's business.
    ///
    /// \note Everything except `missingParameters`, and that omission is the
    /// point. A preset that does not mention a parameter is not a damaged
    /// preset: the effect grew that parameter after the file was written, the
    /// value defaults, and this is the format's forward compatibility working
    /// exactly as designed.
    ///
    ///   It is also not rare. **104 of the 303 shipped banks** raise one --
    /// every Freqverb preset predates `HF absorb`, every TuneWorx preset
    /// predates its twelve per-semitone bypasses and its whole vibrato section
    /// -- and `MissingParameter` is the *only* kind any of them raises. So
    /// mentioning it put a dialog in front of anyone arrowing through the
    /// factory content, saying something they could neither act on nor avoid.
    ///
    ///   Still counted, still traced: a parameter that goes missing because
    /// somebody renamed one is a real defect, and it looks exactly like this
    /// from here. What catches that is the pinned total in
    /// presetReportTests.cpp, not a dialog the user learns to dismiss.
    ///                                       (02.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////
    /// \note `samplesNotLoaded` is told: the file has a name, and going and
    /// finding it is something only the user can do. So is `modulesDropped`, for
    /// the plainer reason that the preset is not playing what it says it does.
    bool worthTellingTheUser() const
    {
        return (failures + unknownParameters + unknownEffects + unavailableEffects +
                samplesNotLoaded + modulesDropped) != 0;
    }
}; // struct PresetLoadReport

/// \brief Returns everything the default reporter has counted, and clears it.
///
/// \note Only meaningful while the default reporter is installed. A caller that
/// replaced it is counting for itself.
PresetLoadReport takePresetLoadReport();

////////////////////////////////////////////////////////////////////////////////
///
/// \struct PresetHeader
///
////////////////////////////////////////////////////////////////////////////////

struct PresetHeader
{
    static unsigned int const maxCommentLength = 256;

    explicit PresetHeader(std::string_view comment);

    char version[8];
    char timeStamp[64];
    char comment[maxCommentLength];

    void setCurrentTime();

    struct AttributeNames
    {
        static char const version[];
        static char const timeStamp[];
        static char const comment[];
    } attributeNames;
}; // struct PresetHeader

////////////////////////////////////////////////////////////////////////////////
///
/// \struct DawExtraState
///
/// \brief The block that makes one serialisation serve both a preset file and
/// the session state a host hands back.
///
///   A preset says what the plugin sounds like. A session says that, plus where
/// the user had got to -- which preset folder was open, what the editor was
/// showing, settings that are not parameters and never will be. Writing both
/// into every file would mean loading a preset silently resets the session;
/// writing neither means a session cannot remember anything that is not a
/// parameter, which is where this plugin has been.
///
///   So it is optional at both ends. `savePreset` writes the block only when
/// handed one of these, and `loadPreset` calls `from` only if the element is
/// actually *present* -- so a `.swp` loaded into a live session leaves that
/// session's own state alone. The shape is
/// `sst::plugininfra::patch_support::PatchBase::dawExtraState{To,From}`, which
/// is what the other Surge Synth Team plugins do.
///
/// \note The element is written even when `to` puts nothing in it. An empty
/// `<dawExtraState/>` is a claim a test can make -- that a session and a preset
/// really are different documents -- and a payload that is still empty is not a
/// reason to be unable to make it.
///
/// \note Passed to the two free functions rather than held on `Preset`, because
/// the caller does not own the `Preset`: `savePreset` builds a `SavedPreset` and
/// `loadPreset` builds a `Preset`, both internally, and neither is ever in a
/// caller's hands to hang a hook on.
///                                           (02.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

struct DawExtraState
{
    std::function<void(TiXmlElement &)> to{nullptr};
    std::function<void(TiXmlElement const &)> from{nullptr};
}; // struct DawExtraState

////////////////////////////////////////////////////////////////////////////////
///
/// \class Preset
///
////////////////////////////////////////////////////////////////////////////////

class Preset
{
  public:
    Preset(Preset const &) = delete; // makes non-copyable
    Preset &operator=(Preset const &) = delete;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief The grammar the file is written in, and the one this build writes.
    ///
    /// \note A separate attribute from `Version`, which is and always was the
    /// *product* version (`SW_VERSION_MAJOR.MINOR`). The corpus carries 2.6, 2.7,
    /// 2.8, 2.9 and 2.93 in that field, and it tracked the format only because
    /// the two moved together in 2011. They do not any more: this tree is
    /// 3.0.0, so it was already writing `Version="3.0"` onto 2.6-shaped files
    /// while the pre-2.7 check read that number as a format version.
    ///
    /// \note Absent means **0**, which is every file written before 08.2026 and
    /// is what selects the legacy reader. Greater than
    /// `currentFormatVersion` is refused by `loadPreset()` with
    /// `PresetProblem::FutureFormat`, so "saved by a newer SpectrumWorx" is not
    /// reported as a corrupt file.
    ///                                       (02.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    static constexpr unsigned int currentFormatVersion{3};
    static char const formatAttributeName[];

    unsigned int formatVersion() const;

  public:
    Preset() = default;
    explicit Preset(char const *const pBuffer) { loadFrom(pBuffer); }

    /// \brief Parses \p pBuffer, repairing the 2016 writer's illegal element
    /// names if it has to. See mangleName().
    ///
    /// \note Plain `bool`. It was `std::true_type` whenever exceptions were on
    /// -- which is always -- so `loadFrom(...) != true` at the one call site was
    /// a comparison that could not fail, and a malformed preset was reported by
    /// RapidXML *throwing* instead. TinyXML says so in its return value.
    ///                                       (31.07.2026.) (SW port)
    bool loadFrom(char const *pBuffer);

    /// \brief Prints the preset.
    ///
    /// \note A `std::string`, where this took a `std::span<char>` and answered 0
    /// when the preset did not fit. Every caller passed the same
    /// `std::array<char, 4096>`, and the 2016 sources already record that five
    /// TuneWorx modules overrun it -- so the size limit was real, reachable, and
    /// bought nothing: TiXmlPrinter builds the whole document in a string of its
    /// own before any of it is copied out. Session state, which has no size to
    /// be limited to, is what made keeping it indefensible.
    ///                                       (02.08.2026.) (SW port)
    std::string saveTo() const;

    void getHeader(PresetHeader &) const;
    void setHeader(PresetHeader const &);

    std::string_view getComment() const;

    TiXmlDocument &xml() { return document_; }
    TiXmlDocument const &xml() const { return document_; }

    TiXmlElement &root();
    TiXmlElement const &root() const;

    void reset() { document_.Clear(); }

    /// \note A preset arrives as a NUL-terminated buffer. Where it comes from is
    /// presetFile.hpp's business, not this header's.
    using InMemoryPreset = std::unique_ptr<char[]>;

    static void reportPresetLoadingError();

    static char const dawExtraStateNodeName[];

  private:
    TiXmlDocument document_;
}; // class Preset

////////////////////////////////////////////////////////////////////////////////
///
/// \class PresetHandler
///
////////////////////////////////////////////////////////////////////////////////

class PresetHandler
{
  protected:
    PresetHandler(Preset &preset) : preset_(preset) {}

  protected:
    ////////////////////////////////////////////////////////////////////////////
    // Parameter name <-> XML name.
    //
    // \note A parameter's or effect's name is used verbatim as an element or
    // attribute name, and three things they are called are not legal XML names:
    // a space ("Start frequency"), a leading digit -- TuneWorx's twelve
    // semitones are called "1" .. "12" -- and the punctuation in
    // "Center (LFO me!)" and the seven "... (pvd)" effects. The 2016 writer
    // replaced the space and nothing else, and got away with it because
    // RapidXML's fastest parse mode never checks a name. TinyXML does, and 25 of
    // the 303 factory presets do not parse until read through
    // repairLegacyElementNames().
    //
    // \note Not invertible, and it does not need to be: nothing unmangles. A
    // preset is matched to an effect by mangling the effect's name and comparing,
    // which is why a mangling that maps "(" and " " to the same character is
    // still unambiguous.
    //
    // \note The 2016 originals returned a view into RapidXML's arena, because
    // every string an attribute referred to had to outlive the document.
    // TinyXML copies what it is given, so these own what they return and the
    // arena, the `allocateString` that fed it and the lifetime rule that came
    // with them are all gone.
    //                                        (31.07.2026.) (SW port)
    ////////////////////////////////////////////////////////////////////////////

    static std::string mangleName(std::string_view parameterName);

    Preset &preset() { return preset_; }
    Preset const &preset() const { return preset_; }

    TiXmlDocument &xml() { return preset().xml(); }
    TiXmlDocument const &xml() const { return preset().xml(); }

    using LFO = Parameters::LFOImpl;

  protected:
    friend class LFODataSaver;

    static std::string makeString(std::string_view const source) { return std::string(source); }
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The buffer goes to `lexical_cast` whole, so the size it is written
    /// against is the size that was declared. It used to hand over
    /// `buffer.data()` and trust `RequiredStringStorage` to have been the bound
    /// the writer used -- and the length that came back could exceed the array,
    /// which this then built a `std::string` from. That read past the end of the
    /// buffer and into the saved preset.
    ///
    /// \note `RequiredStringStorage` still sizes the array, and is now big enough
    /// to mean it: a `double` gets 321 rather than 17, which is what `%.9f` of one
    /// actually needs. This is the one caller where that matters -- it writes the
    /// file, so a value falling back to `%g` would lose precision on disk rather
    /// than merely in a display.
    ///                                       (08.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    template <typename T> static std::string makeString(T const binarySource)
    {
        std::array<char, Utility::RequiredStringStorage<T>::value> buffer;
        auto const numberOfCharacters(Utility::lexical_cast(
            std::is_enum<T>::value ? static_cast<std::uint8_t>(binarySource) : binarySource,
            buffer));
        LE_ASSERT(numberOfCharacters < buffer.size());
        return std::string(buffer.data(), numberOfCharacters);
    }

  private:
    static std::string fixSpaces(std::string_view input, char searchFor, char replaceWith);

  private:
    Preset &preset_;
}; // class PresetHandler

template <> std::string PresetHandler::makeString<bool>(bool);

/// \note Nine significant figures, which is the shortest that round-trips every
/// `float`, where the generic path gives four *decimals* (lexicalCast.cpp:63).
/// Four decimals is plenty for 6000 Hz and coarse for a normalised 0..1
/// frequency, where it is about fourteen bits: "Start frequency" saved and
/// reloaded did not come back the same number. Reading is unaffected -- strtof
/// takes whatever it is given -- so no committed preset moves.
///                                           (02.08.2026.) (SW port)
template <> std::string PresetHandler::makeString<float>(float);

////////////////////////////////////////////////////////////////////////////////
///
/// \class ParametersLoader
///
////////////////////////////////////////////////////////////////////////////////

namespace Engine
{
class ModuleChainImpl;
class ModuleProcessorImpl;
} // namespace Engine
class AutomatedModuleChain;

class ParametersLoader : private PresetHandler
{
  public:
    ParametersLoader(Preset const &);

    typedef AutomatedModuleChain ModuleChain;

    /// \note Fills \p newChain rather than returning one, so that the call stays
    /// dependent on a template parameter and clang does not need the chain to be
    /// complete where the template is *defined*. Same reason as the note at the
    /// call site.
    void loadModuleChain(ModuleChain &newChain);

    std::string_view getSampleFileName();

    bool syncedLFOFound() const { return syncedLFOFound_; }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Reports every element of the current module that nothing read.
    ///
    /// \note Reading a preset is one lookup per parameter the *effect* has, so
    /// an element the effect does not have a parameter for is never visited --
    /// and a renamed parameter is exactly that, silently discarded. This is the
    /// other half of the ledger: what was in the file against what was taken out
    /// of it. \see PresetProblem::UnknownParameter.
    ///
    ////////////////////////////////////////////////////////////////////////////
    void reportUnreadParameters() const;

    template <typename T>
    LE_NOINLINE std::optional<T> getSimpleParameterValue(char const *const parameterName) const
    {
        return getParameterValue<T>(getParameterAttribute(parameterName), parameterName);
    }

    template <typename T>
    std::optional<T> getLFOParameterValue(char const *const parameterName, LFO &lfo) const
    {
        auto const pParameterNode(getParameterNode(parameterName));
        if (pParameterNode)
        {
            if (loadLFO(*pParameterNode, lfo))
                return std::nullopt;
            else
                return getParameterValue<T>(parameterValueText(*pParameterNode), parameterName);
        }
        else
        {
            // Implementation note:
            //   Fallback to handle old presets where not all module parameters
            // were LFO-able parameters.
            //                                (14.07.2011.) (Domagoj Saric)
            return getSimpleParameterValue<T>(parameterName);
        }
    }

  public: // For-each functor interface.
    using result_type = void;
    using const_qualified_lfo_t = LFO;

    template <class Parameter> void operator()(Parameter &parameter) const
    {
        if constexpr (LE::Parameters::isAnEvent<Parameter>)
            return; // \see ParametersSaver::valueToStream().

        using binary_type = typename Parameter::binary_type;
        std::optional<binary_type> const parameterValue(
            getSimpleParameterValue<binary_type>(LE::Parameters::streamingName<Parameter>()));
        if (parameterValue.has_value() && parameter.isValidValue(*parameterValue))
            parameter.setValue(*parameterValue);
    }

    template <class Parameter> void operator()(Parameter &parameter, LFO &lfo) const
    {
        using binary_type = typename Parameter::binary_type;

        /// \note The LFO's own settings are read either way -- they *are* state,
        /// and an event under an LFO is a rate and a shape like any other. Only
        /// the parameter's own value is dropped.
        std::optional<binary_type> const parameterValueWithoutLFO(getLFOParameterValue<binary_type>(
            Parameters::streamingName<Parameter>(), lfo, &parameter));

        if constexpr (LE::Parameters::isAnEvent<Parameter>)
            return;
        else if (parameterValueWithoutLFO.has_value() &&
                 parameter.isValidValue(*parameterValueWithoutLFO))
            parameter.setValue(*parameterValueWithoutLFO);
    }

  private:
    /// \note One overload over a plain C string, where there were two over a
    /// RapidXML base class: TinyXML's attribute value and element text arrive as
    /// `char const *` already, and `TiXmlElement::Value()` is the element's
    /// *name*, so a shared base would have read the wrong thing for one of them.
    template <typename T>
    std::optional<T> getParameterValue(char const *const text,
                                       char const *const parameterName) const
    {
        if (text)
            return Utility::lexical_cast<T>(text);
        warnAboutMissingParameter(parameterName);
        return std::nullopt;
    }

    ////////////////////////////////////////////////////////////////////////////
    // The two grammars.
    //
    // \note Four private members are everything that differs between a 2.x file
    // and a 3.0 one: where a parameter's value lives, where its LFO attributes
    // live, how the module elements are walked, and how one of them names its
    // effect. So this branches on a flag rather than growing a second class --
    // and the legacy arm is the code that was here, moved and not rewritten,
    // because the 303 committed presets are the only sample of that grammar
    // anyone will ever have.
    ////////////////////////////////////////////////////////////////////////////

    enum struct Grammar : std::uint8_t
    {
        /// Element name *is* the mangled parameter name; the value is an
        /// attribute on the parent for a plain parameter and the element's own
        /// text for an LFO-able one; a module element is named after the mangled
        /// effect name.
        Legacy,
        /// `<p n="…" v="…">` throughout, with the LFO attributes on the same
        /// element, and `<Module effect="…">`.
        V3
    };

    /// \brief The value text for \p parameterName, or null if it is absent.
    char const *getParameterAttribute(char const *parameterName) const;

    /// \brief The element carrying \p parameterName's value and LFO settings, or
    /// null. In 3.0 this and getParameterAttribute() answer about the same
    /// element; in 2.x they are two different shapes and either may be the one
    /// present, which is what getLFOParameterValue()'s fallback is for.
    TiXmlElement const *getParameterNode(char const *parameterName) const;

    /// \brief Where the value sits on a node getParameterNode() returned.
    char const *parameterValueText(TiXmlElement const &parameterNode) const;

    bool loadLFO(TiXmlElement const &parameterNode, LFO &lfo) const;

    static void warnAboutMissingParameter(char const *parameterName);

    static std::int8_t effectIndexFromMangledName(std::string_view mangledName);

    /// \brief The effect the module element now under the cursor names, and the
    /// spelling to report if this build does not have it.
    std::pair<std::int8_t, char const *> currentEffect() const;

    TiXmlElement const &parameters() const
    {
        LE_ASSUME(pParameters_);
        return *pParameters_;
    }

    bool switchedToModuleParameters() const;

    /// \brief Records that \p pNode's value was taken, for reportUnreadParameters().
    ///
    /// \note Pointers into the parsed document, which outlives every read of it;
    /// a vector rather than a set because a module has at most a dozen
    /// parameters and this is a linear scan either way.
    TiXmlElement const *noteAsRead(TiXmlElement const *pNode) const;

  private:
    TiXmlElement const *LE_RESTRICT pParameters_;

    Grammar grammar_;

    mutable bool syncedLFOFound_;

    mutable std::vector<TiXmlElement const *> readParameters_;

    template <class PresetConsumer>
    friend bool loadPreset(char *inMemoryPreset, bool ignoreExternalSample, std::string *pComment,
                           PresetConsumer);
}; // class ParametersLoader

////////////////////////////////////////////////////////////////////////////////
///
/// \class SavedPreset
///
/// \brief A preset being built: the three fixed nodes every one of them has.
///
/// \note Was `PresetWithPreallocatedFixedNodes`, which held the header node,
/// the two section nodes and *five module nodes* as members -- because RapidXML
/// allocates from an arena and does not own what you append, so pre-allocating
/// them was how you avoided the allocation. TinyXML owns its children, so a node
/// held by value cannot be linked into a document at all; the five module nodes
/// and the `moduleNodesEnd()` walk over them go with the class.
///                                           (31.07.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

class SavedPreset : public Preset
{
  public:
    SavedPreset();

    void setHeader(PresetHeader const &);

    TiXmlElement &globalParametersNode() { return *pGlobalParametersNode_; }
    TiXmlElement &moduleParametersNode() { return *pModuleParametersNode_; }

  private:
    /// \note Owned by the document, which deletes them.
    TiXmlElement *pGlobalParametersNode_;
    TiXmlElement *pModuleParametersNode_;
}; // class SavedPreset

////////////////////////////////////////////////////////////////////////////////
///
/// \class ParametersSaver
///
////////////////////////////////////////////////////////////////////////////////

class ParametersSaver : private PresetHandler
{
  public:
    ParametersSaver(SavedPreset &);

    void saveEffectModuleChain(AutomatedModuleChain const &);

    //...mrmlj...temporarily reverting to old code for the 2.1 release...
    //void setSampleFileName( juce::String const & sampleFileName );
    void setSampleFileName(std::string_view const &sampleFileName);

    std::string saveTo() const;

  public: // For-each functor interface.
    using result_type = void;
    using const_qualified_lfo_t = LFO const;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief What \p parameter is written as. **An event always streams off.**
    ///
    ///   A trigger has no value worth keeping: it is armed, the engine consumes
    /// it, and it is spent. Writing the armed state means a preset that fires
    /// itself on load, once, every time -- a Freeze that freezes a session the
    /// moment it is opened. The parameter is still written, because the format
    /// is a full list and a reader may warn about a missing name; it is written
    /// at rest. The loader drops it for the same reason.
    ///
    /// \note It is not only the window between arming and consuming. Nothing
    /// disarms the *main thread's* copy at all -- `consumeValue()` runs on the
    /// engine's -- so from the first press onwards that copy reads armed for the
    /// life of the session, and it is the copy `stateSave` reads. \see
    /// LE::Parameters::isAnEvent, and issue #65.
    ///                                       (16.08.2026.) (SW port)
    ///
    ////////////////////////////////////////////////////////////////////////////

    template <class Parameter>
    static typename Parameter::param_type valueToStream(Parameter const &parameter)
    {
        if constexpr (LE::Parameters::isAnEvent<Parameter>)
            return false;
        else
            return parameter.getValue();
    }

    template <class Parameter> void operator()(Parameter const &parameter) const
    {
        const_cast<ParametersSaver &>(*this). //...mrmlj...because of forEach()...
            saveParameter<typename Parameter::param_type>(
                LE::Parameters::streamingName<Parameter>(), valueToStream(parameter));
    }
    template <class Parameter> void operator()(Parameter const &parameter, LFO const &lfo) const
    {
        const_cast<ParametersSaver &>(*this). //...mrmlj...because of forEach()...
            saveParameter<typename Parameter::param_type>(
                LE::Parameters::streamingName<Parameter>(), valueToStream(parameter), lfo);
    }

    template <typename T>
    void LE_NOINLINE saveParameter(char const *const parameterName, T const parameterValue)
    {
        saveParameter(parameterName, makeString<T>(parameterValue));
    }

    template <typename T>
    void LE_NOINLINE saveParameter(char const *const parameterName, T const parameterValue,
                                   LFO const &parameterLFO)
    {
        saveParameter(parameterName, makeString<T>(parameterValue), parameterLFO);
    }

  private:
    /// \note Both write the same shape -- `<p n="…" v="…">` -- and differ only in
    /// whether the LFO's settings join it as further attributes. In 2.x they
    /// wrote two different things, an attribute on the parent and an element
    /// with the value as its text, which is why the reader still has to look for
    /// both.
    void saveParameter(char const *parameterName, std::string const &parameterValue);
    void saveParameter(char const *parameterName, std::string const &parameterValue,
                       LFO const &parameterLFO);

    TiXmlElement &newParameterNode(char const *parameterName, std::string const &parameterValue);

    TiXmlElement &parameters()
    {
        LE_ASSUME(pParametersNode_);
        return *pParametersNode_;
    }

    SavedPreset &preset() { return static_cast<SavedPreset &>(PresetHandler::preset()); }
    SavedPreset const &preset() const { return const_cast<ParametersSaver &>(*this).preset(); }

  private:
    /// Where saveParameter() writes: the globals node, then each module's in turn.
    TiXmlElement *LE_RESTRICT pParametersNode_;

    /// \note Whether saveEffectModuleChain() has run, which the assertions in
    /// saveTo() used to get from comparing pParametersNode_ against a
    /// pre-allocated array's bounds.
    bool moduleChainSaved_{false};
}; // class ParametersSaver

/// \note Unconditional, for the reason given over the includes at the top: a
/// conforming compiler needs these two names declared before loadPreset() below
/// mentions them, and modern MSVC is one.
namespace Engine
{
class ModuleNode;
template <class ActualModule> ActualModule &actualModule(ModuleNode &);
} // namespace Engine
namespace GlobalParameters
{
struct Parameters;
}

/// \note `juce::String::CharPointerType::CharType`, which on this build is
/// `char` and on a `JUCE_STRING_UTF_TYPE != 8` one would not be. It is a
/// `char` outright now, because a preset is UTF-8 bytes on disk whatever the
/// interface's string type happens to be -- and because this header is below
/// JUCE. presetFile.hpp is where the two meet.
///                                           (02.08.2026.) (SW port)
using char_t = char;

template <class PresetConsumer>
bool loadPreset(char *LE_RESTRICT const inMemoryPreset, bool const ignoreExternalSample,
                std::string *LE_RESTRICT const pComment, PresetConsumer const consumer,
                DawExtraState const *const pDawExtraState = nullptr)
{
    {
        Preset preset;
        if (!preset.loadFrom(inMemoryPreset))
        {
            Preset::reportPresetLoadingError();
            return false;
        }

        /// \note Refused rather than read as far as it happens to make sense. A
        /// grammar this build does not know is not a corrupt file and should not
        /// be reported as one, and a partial read of one would be worse than
        /// either -- it would silently drop whatever the new version added.
        if (preset.formatVersion() > Preset::currentFormatVersion)
        {
            reportPresetProblem(PresetProblem::FutureFormat);
            return false;
        }

        if (pComment)
        {
            auto const comment(preset.getComment());
            pComment->assign(comment.begin(), comment.end());
        }

        /// \note Only if the element is there. A `.swp` has none, and calling
        /// the hook with nothing to read would hand the caller a
        /// default-constructed session state -- which is how loading a preset
        /// would come to reset where the browser was pointing.
        if (pDawExtraState && pDawExtraState->from)
        {
            auto const *const pNode(preset.root().FirstChildElement(Preset::dawExtraStateNodeName));
            if (pNode)
                pDawExtraState->from(*pNode);
        }

        ParametersLoader parametersLoader(preset);

        auto loader(consumer.presetLoader(ignoreExternalSample));

        if (loader.wantsSampleFile())
        {
            // Implementation note:
            //   The sample file name must be fetched before switching to module
            // parameters (see the implementation of the
            // ParametersLoader::getSampleFileName() member function).
            //                                (15.12.2011.) (Domagoj Saric)
            /// \todo Clean up this spaghetti.
            ///                               (15.12.2011.) (Domagoj Saric)
            loader.setSample(parametersLoader.getSampleFileName());
        }

        GlobalParameters::Parameters newParameters;
        LE::Parameters::forEach(newParameters, parametersLoader);
        //...mrmlj...clang's early template instantiation...AutomatedModuleChain newChain;
        typename std::remove_reference<decltype(loader.targetChain())>::type newChain;
        parametersLoader.loadModuleChain(newChain);
        if (loader.onlySetParameters())
        {
            loader.targetGlobalParameters() = newParameters;
            loader.targetChain() = std::move(newChain);
            return true;
        }

        {
            auto const automationBlocker(loader.automationBlocker());
            if (!loader.setNewGlobalParameters(newParameters))
            {
                Preset::reportPresetLoadingError();
                return false;
            }
            LE::Utility::ignoreUnused(automationBlocker);
        }

        auto const initialiseModule(loader.moduleInitialiser());

        using Module = typename PresetConsumer::Module;
        std::uint8_t moduleIndex(0);
        std::for_each(newChain.begin(), newChain.end(), [&](Engine::ModuleNode &module) {
            if (!initialiseModule(Engine::actualModule<Module>(module), moduleIndex++))
                newChain.remove(module);
        });

        /// \note And this is the handover. Everything above happened on the
        /// calling thread against a chain nothing else can see; this is the one
        /// step that touches the engine, and whether it happens here or at the
        /// top of the next block is the publisher's business rather than this
        /// function's. `newChain` comes back holding whatever it displaced, and
        /// its destructor is what finally lets those modules go.
        ///                                   (02.08.2026.) (SW port)
        loader.publishChain(newChain);
        loader.moduleChainFinished(moduleIndex, parametersLoader.syncedLFOFound());
        return true;
    }

    /// \todo Add MIDI support.
    ///                                       (16.12.2009.) (Domagoj Saric)
} // loadPreset()

class Program;

/// \note The file-opening half of this -- and the `loadPreset` that reads one
/// before parsing it -- is presetStorage.hpp, beside this in `sw-dsp` and over
/// `fs::path` and `<fstream>`. This translation unit opens no files, which is
/// what `LE_NO_PRESETS` used to stand in for, and as of stage 7 it names no JUCE
/// type either.
///
/// \param externalSampleFilePath empty when no sample is loaded.
/// \param pDawExtraState null for a `.swp`, which carries only what the plugin
/// sounds like; non-null for the session state a host holds, which carries that
/// plus where the user had got to. See DawExtraState.
std::string savePreset(std::string_view externalSampleFilePath, std::string_view comment,
                       Program const &, DawExtraState const *pDawExtraState = nullptr);

} // namespace SW

} // namespace LE

#endif
