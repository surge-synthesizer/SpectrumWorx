////////////////////////////////////////////////////////////////////////////////
///
/// presets.cpp
/// -----------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "presets.hpp"

#include "configuration/versionConfiguration.hpp"
#include "core/automatedModuleChain.hpp"
#include "core/modules/factory.hpp"
#include "core/modules/moduleDSPAndGUI.hpp"

#include "le/math/conversion.hpp"
#include "le/math/math.hpp"
#include "le/parameters/parametersUtilities.hpp"
#include "le/parameters/lfo.hpp"
#include "le/parameters/uiElements.hpp" //...mrmlj...only for the warnAboutMissingParameter() temporary workaround
#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/effects/configuration/includedEffects.hpp"
#include "le/utility/countof.hpp"

#include "le/utility/assert.hpp"
#include "le/utility/intrusivePtr.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
//------------------------------------------------------------------------------

// CDATA Sections [XML Standards]
// http://msdn.microsoft.com/en-us/library/ms256076.aspx
// http://msdn.microsoft.com/en-us/library/ms256177.aspx

/// \note Malformed input used to leave here as an exception -- RapidXML threw a
/// `rapidxml::parse_error` from inside the parser and `loadPreset`'s catch-all
/// turned it into "unable to load", which meant a bad preset unwound through
/// whatever the caller happened to be holding. (A `setjmp`/`longjmp` pair stood
/// beside it for the no-exceptions build, which this one is not.) TinyXML
/// reports through `TiXmlDocument::Error()` and needs neither.

namespace LE
{

namespace GUI
{
void warningMessageBox(std::string_view title, std::string_view message, bool canBlock);
}

namespace SW
{

using PresetModule = SW::Module;

PresetHeader::PresetHeader(std::string_view const commentParam)
{
    LE_ASSERT(commentParam.size() < _countof(comment) - 1);

/// \note Two levels, so that the argument is macro-expanded before it is
/// stringized. Was BOOST_PP_STRINGIZE, which is the same two lines.
#define LE_STRINGIZE_(x) #x
#define LE_STRINGIZE(x) LE_STRINGIZE_(x)
    std::strcpy(version, LE_STRINGIZE(SW_VERSION_MAJOR) "." LE_STRINGIZE(SW_VERSION_MINOR)
#if SW_VERSION_PATCH
                             LE_STRINGIZE(SW_VERSION_PATCH)
#endif // SW_VERSION_PATCH
    );
#undef LE_STRINGIZE
#undef LE_STRINGIZE_
    setCurrentTime();

    auto const kept(std::min(commentParam.size(), sizeof(comment) - 1));
    std::memcpy(comment, commentParam.data(), kept);
    comment[kept] = '\0';
}

/// \note One implementation, not two. The Windows arm called GetSystemTime(),
/// GetDateFormatA() and GetTimeFormatA() to assemble the same "dd.MM.yyyy HH:mm"
/// that the four lines below produce, and it did not compile: it needs
/// <windows.h>, which this file has never included and which has no business in
/// sw-dsp -- the layer whose whole point is that it depends on nothing above it.
/// Two ways to write a timestamp is also two ways to get it wrong; the standard
/// one is the same on every platform this builds for.
void PresetHeader::setCurrentTime()
{
    std::time_t const currentUTCTime(std::time(nullptr));
    LE_VERIFY(std::strftime(timeStamp, sizeof(timeStamp), "%d.%m.%Y %H:%M",
                            std::gmtime(&currentUTCTime)) > 0);
}

namespace
{
char const headerNodeName_[] = "SpectrumWorxPreset";
char const globalParametersNodeName_[] = "Global";
char const moduleParametersNodeName_[] = "Modules";
char const sampleAttributeName_[] = "Sample";

/// \note Beside the sample and keyed like it, because it answers the same
/// question -- \see sideChainSource.hpp. No 2.x file has one; `Input_mode` below
/// is what those are migrated from.
char const sideChainSourceAttributeName_[] = "Side chain source";

/// \note 2016's parameter, which every shipped preset carries
/// and which is no longer a parameter here. Read once, at load, and never
/// written. \see doc/tech/sidechain-approach.md.
char const legacyInputModeAttributeName_[] = "Input mode";

/// \name The 3.0 grammar's own names.
/// \note Short on purpose: `<p>` and its two attributes are repeated once per
/// parameter, up to 55 times per preset, and this is a format read by machines
/// and skimmed by people rather than the other way round.
/// @{
char const moduleNodeName_[] = "Module";
char const moduleEffectAttributeName_[] = "effect";
char const parameterNodeName_[] = "p";
char const parameterNameAttributeName_[] = "n";
char const parameterValueAttributeName_[] = "v";
/// @}

} // namespace

char const Preset::formatAttributeName[] = "Format";
char const Preset::dawExtraStateNodeName[] = "dawExtraState";

char const PresetHeader::AttributeNames::version[] = "Version";
char const PresetHeader::AttributeNames::timeStamp[] = "LastModified";
char const PresetHeader::AttributeNames::comment[] = "Comment";

////////////////////////////////////////////////////////////////////////////////
//
// Preset::loadFrom()
// ------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \return whether the buffer parsed. Throws nothing.
///
/// \note The buffer is `char const *` now. RapidXML parsed destructively, in
/// place, and every string in the document pointed back into it -- so the buffer
/// had to be writable and had to outlive the document. TinyXML copies.
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
char const mangledSpace = '_';

/// \name XML name rules
/// \note Deliberately narrower than the specification, which allows a great deal
/// of Unicode: what has to pass is the ASCII in 57 effect names and their
/// parameters, and what has to be rejected is everything TinyXML's own
/// `ReadName` rejects. `:` is legal and excluded anyway -- it means a namespace
/// and no name here wants one.
/// @{
bool isNameStart(char const character)
{
    return std::isalpha(static_cast<unsigned char>(character)) || (character == mangledSpace);
}

bool isNameCharacter(char const character)
{
    return std::isalnum(static_cast<unsigned char>(character)) || (character == mangledSpace) ||
           (character == '-') || (character == '.');
}
/// @}

/// \brief The name-to-XML-name mapping, as a free function so that the read-side
/// repair can apply exactly the same one.
///
/// \note Idempotent, which is what lets the repair run over a name that is
/// already fine and over one that is not without having to tell them apart.
std::string mangleXmlName(std::string_view const name)
{
    std::string mangled;
    mangled.reserve(name.size() + 1);
    for (auto const character : name)
        mangled += isNameCharacter(character) ? character : mangledSpace;

    if (mangled.empty() || !isNameStart(mangled.front()))
        mangled.insert(mangled.begin(), mangledSpace);
    return mangled;
}
} // anonymous namespace

namespace
{
////////////////////////////////////////////////////////////////////////////////
//
// repairLegacyElementNames()
// --------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Rewrites every element name the 2016 writer emitted that is not a
/// legal XML name.
///
/// \note A parameter's or effect's name becomes an element name verbatim, and
/// the 2016 writer replaced spaces and nothing else. Three kinds of name got
/// through: `<1>` .. `<12>` (TuneWorx's semitones), `<Pitch_Shifter_(pvd)>` and
/// its six siblings, and `<Center_(LFO_me!)>`. No conforming parser will read
/// any of them. RapidXML's `parse_fastest` mode never checked a name, which is
/// why it went unnoticed for fifteen years and why some of the committed
/// presets need this. The writer mangles properly now (mangleName), so this is
/// for files written before it did.
///
/// \note Runs only after a strict parse has already failed, so a well-formed
/// preset never comes through here and never pays for it. A raw `<` in text
/// would be malformed anyway, which is what makes the pattern safe to key on.
///
/// \return nothing if there was nothing to repair, so that a genuinely broken
/// preset is not parsed twice.
///
////////////////////////////////////////////////////////////////////////////////

std::optional<std::string> repairLegacyElementNames(char const *const pBuffer)
{
    std::string_view const source(pBuffer);

    auto const endsName([](char const character) {
        return (character == ' ') || (character == '\t') || (character == '\r') ||
               (character == '\n') || (character == '>') || (character == '/');
    });

    std::string repaired;

    /// \note Two cursors, not one. Reusing the scan position as the copy
    /// position drops everything before the first repaired tag -- which is the
    /// root element, so the retry parses a document with no root and reports
    /// "document empty" from three steps away.
    std::size_t copied(0);
    std::size_t scan(0);
    for (;;)
    {
        auto const tag(source.find('<', scan));
        if (tag == source.npos)
            break;

        auto nameStart(tag + 1);
        if ((nameStart < source.size()) && (source[nameStart] == '/'))
            ++nameStart;

        // A declaration, a comment or a doctype: not an element name.
        if ((nameStart >= source.size()) || (source[nameStart] == '?') ||
            (source[nameStart] == '!'))
        {
            scan = tag + 1;
            continue;
        }

        auto nameEnd(nameStart);
        while ((nameEnd < source.size()) && !endsName(source[nameEnd]))
            ++nameEnd;

        auto const name(source.substr(nameStart, nameEnd - nameStart));
        auto const mangled(mangleXmlName(name));
        if (mangled != name)
        {
            repaired.append(source, copied, nameStart - copied);
            repaired += mangled;
            copied = nameEnd;
        }
        scan = nameEnd;
    }

    if (repaired.empty())
        return std::nullopt;

    repaired.append(source, copied, source.npos);
    return repaired;
}
} // anonymous namespace

bool Preset::loadFrom(char const *const pBuffer)
{
    /// \brief Parsed, and a preset.
    ///
    /// \note The second half is not pedantry. `root()` asserts its node exists
    /// and then dereferences it, so a well-formed document that is not a preset
    /// -- somebody else's XML, or a state blob from a plugin that used to have
    /// this plugin's id -- was a null dereference on the first thing to ask.
    /// Refusing it here covers every caller at once, and "not a preset" is
    /// exactly what LoadFailed already means.
    auto const parsedAsPreset([this] {
        return !document_.Error() && (document_.FirstChildElement(headerNodeName_) != nullptr);
    });

    document_.Clear();
    document_.Parse(pBuffer);
    if (parsedAsPreset())
        return true;

    if (auto const repaired(repairLegacyElementNames(pBuffer)); repaired)
    {
        document_.Clear();
        document_.Parse(repaired->c_str());
        if (parsedAsPreset())
            return true;
    }

    return false;
}

////////////////////////////////////////////////////////////////////////////////
//
// Preset::saveTo()
// ----------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note Tabs, and a trailing newline after the root: that is what the 2016
/// writer emitted and what the committed presets look like, so it is what
/// TiXmlPrinter is told to emit. The terminator is written too, because the
/// 2016 writer put one on disk, and most of the committed files carry it.
///
////////////////////////////////////////////////////////////////////////////////

std::string Preset::saveTo() const
{
    //...mrmlj...an ugly temporary way to verify that the header was set before saving...
    /// \note Braces, not parentheses. `PresetHeader dummyHeader( juce::String() );`
    /// is the most vexing parse -- it declares a function -- so this check had
    /// never once run: the first build to compile this file is the one that
    /// rejected it.
#ifndef NDEBUG
    PresetHeader dummyHeader{std::string_view()};
    getHeader(dummyHeader);
#endif // NDEBUG

    TiXmlPrinter printer;
    printer.SetIndent("\t");
    document_.Accept(&printer);

    /// \note No terminator in the string. The 2016 writer put one on disk and
    /// Most committed files end in a NUL byte, so `writePresetFile()`
    /// still appends one -- but a `std::string` that carries its own NUL in
    /// `size()` is a trap for every caller that is not writing a file, and the
    /// state stream is now one of those.
    return printer.CStr();
}

unsigned int Preset::formatVersion() const
{
    auto const *const pFormat(root().Attribute(formatAttributeName));
    if (!pFormat)
        return 0;
    int format{0};
    if (root().QueryIntAttribute(formatAttributeName, &format) != TIXML_SUCCESS)
        return 0;
    return static_cast<unsigned int>(std::max(format, 0));
}

TiXmlElement &Preset::root()
{
    auto *const pHeaderNode(document_.FirstChildElement(headerNodeName_));
    LE_ASSERT_MSG(pHeaderNode, "Preset has no root node.");
    return *pHeaderNode;
}

TiXmlElement const &Preset::root() const { return const_cast<Preset &>(*this).root(); }

namespace
{
void copyAndNullTerminate(TiXmlElement const &headerNode, char const *const attributeName,
                          char *const pTargetBuffer, std::size_t const capacity)
{
    auto const *const pValue(headerNode.Attribute(attributeName));
    LE_ASSERT(pValue);
    if (!pValue)
    {
        *pTargetBuffer = '\0';
        return;
    }
    std::string_view const value(pValue);
    auto const copied(std::min(value.size(), capacity - 1));
    *std::copy_n(value.begin(), copied, pTargetBuffer) = '\0';
}
} // anonymous namespace

void Preset::getHeader(PresetHeader &header) const
{
    auto const &headerNode(root());
    copyAndNullTerminate(headerNode, header.attributeNames.version, header.version,
                         sizeof(header.version));
    copyAndNullTerminate(headerNode, header.attributeNames.timeStamp, header.timeStamp,
                         sizeof(header.timeStamp));
    copyAndNullTerminate(headerNode, header.attributeNames.comment, header.comment,
                         sizeof(header.comment));
}

/// \note \p header need not outlive this call: TinyXML copies the strings.
void Preset::setHeader(PresetHeader const &header)
{
    auto &headerNode(root());
    headerNode.SetAttribute(header.attributeNames.version, header.version);
    headerNode.SetAttribute(header.attributeNames.timeStamp, header.timeStamp);
    headerNode.SetAttribute(header.attributeNames.comment, header.comment);
}

std::string_view Preset::getComment() const
{
    auto const *const pComment(root().Attribute(PresetHeader::AttributeNames::comment));
    LE_ASSERT(pComment);
    return pComment ? std::string_view(pComment) : std::string_view();
}

////////////////////////////////////////////////////////////////////////////////
//
// Problem reporting
// -----------------
//
////////////////////////////////////////////////////////////////////////////////

namespace
{
/// \note Not a dialog. See PresetLoadReport for the whole argument; the short
/// version is that one of these per problem meant a wall of them for the factory
/// banks, from a layer that has no business knowing what a dialog is, on whatever
/// thread a host chose to restore a session on.
PresetLoadReport report_;

void defaultPresetProblemReporter(PresetProblem const problem, std::string_view const detail)
{
    if (report_.firstDetail.empty() && !detail.empty())
        report_.firstDetail.assign(detail);

    switch (problem)
    {
    case PresetProblem::LoadFailed:
    case PresetProblem::SaveFailed:
    case PresetProblem::FutureFormat:
        ++report_.failures;
        return;
    case PresetProblem::UnknownEffect:
        ++report_.unknownEffects;
        return;
    case PresetProblem::EffectNotAvailable:
        ++report_.unavailableEffects;
        return;
    case PresetProblem::MissingParameter:
        ++report_.missingParameters;
        return;
    case PresetProblem::UnknownParameter:
        ++report_.unknownParameters;
        return;
    case PresetProblem::SampleNotLoaded:
        ++report_.samplesNotLoaded;
        return;
    case PresetProblem::TooManyModules:
        ++report_.modulesDropped;
        return;
    }
}

PresetProblemReporter presetProblemReporter{&defaultPresetProblemReporter};
} // anonymous namespace

PresetLoadReport takePresetLoadReport()
{
    PresetLoadReport taken;
    std::swap(taken, report_);
    return taken;
}

PresetProblemReporter setPresetProblemReporter(PresetProblemReporter const reporter)
{
    auto *const previous(presetProblemReporter);
    presetProblemReporter = reporter ? reporter : &defaultPresetProblemReporter;
    return previous;
}

void reportPresetProblem(PresetProblem const problem, std::string_view const detail)
{
    presetProblemReporter(problem, detail);
}

void Preset::reportPresetLoadingError() { reportPresetProblem(PresetProblem::LoadFailed); }

std::string PresetHandler::mangleName(std::string_view const parameterName)
{
    LE_ASSERT(!parameterName.empty());
    return mangleXmlName(parameterName);
}

template <> std::string PresetHandler::makeString<bool>(bool const binarySource)
{
    return binarySource ? "1" : "0";
}

/// \note Nine significant figures rather than std::to_chars' shortest
/// round-trip: nine round-trips every float too, and the availability question
/// that made that a wary choice has since been answered -- `to_chars` for a
/// floating point type is a libc++ dylib symbol introduced in macOS 13.3, and
/// this ships to 10.15. parameterTableTests.cpp prints its defaults exactly this
/// way for the same reason.
///
/// \note Through a stream imbued with the classic locale rather than `snprintf`,
/// which spells the point whichever way the *host's* locale says. That put a
/// comma in this file -- the one number in it that has to be read back by
/// something other than the machine that wrote it. \see lexicalCast.cpp.
template <> std::string PresetHandler::makeString<float>(float const binarySource)
{
    std::ostringstream value;
    value.imbue(std::locale::classic());
    value << std::setprecision(9) << static_cast<double>(binarySource);
    return value.str();
}

/// \note A preset with no `<Global>` node used to throw from this constructor,
/// which `loadPreset`'s catch-all turned into "unable to load". It reports the
/// same thing without the throw: `pParameters_` is null and every getter misses,
/// which the missing-parameter path already handles.
ParametersLoader::ParametersLoader(Preset const &preset)
    : PresetHandler(const_cast<Preset &>(preset)),
      grammar_(preset.formatVersion() >= 3 ? Grammar::V3 : Grammar::Legacy), syncedLFOFound_(false)
{
    pParameters_ = preset.root().FirstChildElement(globalParametersNodeName_);
    LE_ASSERT_MSG(pParameters_, "Preset node not found");
}

/// \note It took the live chain and stole any module whose effect matched,
/// leaving the rest to be created -- an allocation saved, at the price of the
/// only mutation of the live chain a preset load performed, from the message
/// thread, under the processing lock. The chain belongs to the audio thread now,
/// so the reuse cannot survive and the lock has nothing left to hold: every
/// module in the new chain is built here, the finished chain is published, and
/// what it displaces comes back to be destroyed. See
/// doc/tech/threading_model.md §5.
///
///   It also fixes a smaller thing on the way. A reused module kept its channel
/// state -- its analysis history, its LFO phase -- so loading a preset over one
/// that happened to use the same effect started it mid-flight, and loading the
/// same preset twice in a row did not give the same result as loading it once.
void ParametersLoader::loadModuleChain(ModuleChain &newChain)
{
    LE_ASSERT_MSG(!switchedToModuleParameters(), "Already switched to module parameters.");
    LE_ASSERT(newChain.empty());

    {
        auto const *const pModuleParameters(
            preset().root().FirstChildElement(moduleParametersNodeName_));
#if 0
        if ( !pModuleParameters )
            RAPIDXML_PARSE_ERROR( "Module parameters node not found", nullptr );
#else
        LE_ASSERT_MSG(pModuleParameters, "Module parameters node not found");
        if (!pModuleParameters)
            return;
#endif
        /// \note Unfiltered for 2.x, because there the element's *name* is the
        /// effect and no two are alike; filtered for 3.0, so that anything a
        /// later version puts beside a `<Module>` is skipped rather than read as
        /// a module with an effect this build does not have.
        pParameters_ = (grammar_ == Grammar::V3)
                           ? pModuleParameters->FirstChildElement(moduleNodeName_)
                           : pModuleParameters->FirstChildElement();
    }

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The chain this build can hold is five modules and the file says how
    /// many it wants, so the two have to be reconciled here -- and this only
    /// *asserted* that they matched, which is a release build with no bound at
    /// all on a number a file supplies.
    ///
    ///   What a longer chain then reaches is not a longer chain. Every index in
    /// the addressing scheme is a byte (`ParameterID`), the parameter list is
    /// built once for `maxNumberOfModules` slots and a module past the fifth has
    /// no id for a host to reach it by; the rack has five strips. And
    /// `ModuleChainBase::size()` is a `std::uint8_t`, so the 256th module makes a
    /// full chain report itself as **empty** -- one of the two truncations in
    /// this finding, the other being right here.
    ///
    ///   So the extra modules are refused rather than built, and said so. A
    /// preset that wants more slots than this build has is a legitimate thing for
    /// a *later* version to have written, which is why it reads as a report about
    /// the file rather than a failure to load it: everything up to the fifth
    /// module loads and plays.
    ///
    ////////////////////////////////////////////////////////////////////////////

    std::int8_t const noModule(-1);
    std::uint8_t moduleIndex(0);
    while (pParameters_)
    {
        using namespace Effects;
        auto const [effectIndex, effectName](currentEffect());
        bool const foundEffect(effectIndex != noModule);
        bool const effectEnabled(foundEffect && includedEffects[effectIndex]);
        if (moduleIndex >= SW::Constants::maxNumberOfModules)
        {
            reportPresetProblem(PresetProblem::TooManyModules, effectName);
        }
        else if (foundEffect && effectEnabled)
        {
            LE_ASSUME(effectIndex >= 0);
            using namespace Engine;
            auto pModule(ModuleFactory::create<PresetModule>(effectIndex));
            if (pModule)
            {
                newChain.push_back(*pModule);
                readParameters_.clear();
                pModule->loadPresetParameters(*this);
                /// \note Here rather than after the whole chain: the cursor moves
                /// to the next module below, and what was read is per module.
                reportUnreadParameters();
                ++moduleIndex;
            }
        }
        else
        {
            reportPresetProblem(foundEffect ? PresetProblem::EffectNotAvailable
                                            : PresetProblem::UnknownEffect,
                                effectName);
        }
        pParameters_ = (grammar_ == Grammar::V3) ? pParameters_->NextSiblingElement(moduleNodeName_)
                                                 : pParameters_->NextSiblingElement();
    }

    LE_ASSERT(moduleIndex <= SW::Constants::maxNumberOfModules);
}

bool ParametersLoader::switchedToModuleParameters() const
{
    return pParameters_ &&
           (std::string_view(parameters().Value()) /*...mrmlj...== moduleParametersNodeName_*/
            != globalParametersNodeName_);
}

/// \brief Which effect an element name belongs to.
///
/// \note Mangled-to-mangled, rather than unmangling the element name and looking
/// it up. The mangling is not invertible -- "Pitch Shifter (pvd)" and
/// "Pitch_Shifter__pvd_" both come from characters that all map to `_` -- so the
/// comparison has to happen on the side that is a function of the other. 57
/// string compares per module in a cold path.
///
/// \note This is also what makes a repaired legacy preset work: the repair
/// applies the same mangling the writer does, so `<Pitch_Shifter_(pvd)>` from
/// 2013 and `<Pitch_Shifter__pvd_>` written today both arrive here as the
/// latter.
///
/// \note The streaming name, not the title. They are the same string for every
/// effect today -- that is how the table was seeded -- and the point of asking
/// for the streaming one is that a retitled effect keeps loading its presets
/// rather than quietly becoming an effect this build does not have.
std::int8_t ParametersLoader::effectIndexFromMangledName(std::string_view const mangledName)
{
    for (std::uint8_t effect(0); effect < Effects::Constants::numberOfEffects; ++effect)
        if (mangleName(Effects::effectStreamingName(effect)) == mangledName)
            return static_cast<std::int8_t>(effect);
    return -1;
}

/// \note The 2016 version wrote a NUL over the byte after the element's name --
/// into the parse buffer, because RapidXML's names are not terminated and the
/// mangling wanted a C string. TinyXML's are.
///
/// \note 3.0 spells the effect out in an attribute, so it needs neither the
/// mangling nor the 57-way search: the name in the file is the streaming name
/// exactly, and `effectIndexFromStreamingName()` is one comparison per effect
/// against a string it does not have to transform first.
std::pair<std::int8_t, char const *> ParametersLoader::currentEffect() const
{
    LE_ASSERT_MSG(switchedToModuleParameters(), "Not yet switched to module parameters.");

    if (grammar_ == Grammar::V3)
    {
        auto const *const pEffect(parameters().Attribute(moduleEffectAttributeName_));
        if (!pEffect)
            return {-1, ""};
        return {Effects::effectIndexFromStreamingName(pEffect), pEffect};
    }

    auto const *const pMangledName(parameters().Value());
    return {effectIndexFromMangledName(pMangledName), pMangledName};
}

// sampleAttributeName_ contains no spaces
std::string_view ParametersLoader::getSampleFileName()
{
    LE_ASSERT_MSG(!switchedToModuleParameters(),
                  "Sample file name must be fetched before switching to module parameters.");
    auto const *const pSampleFileName(getParameterAttribute(sampleAttributeName_));
    return pSampleFileName ? std::string_view(pSampleFileName) : std::string_view();
}

std::string_view ParametersLoader::getSideChainSource()
{
    LE_ASSERT_MSG(!switchedToModuleParameters(),
                  "The side chain source must be fetched before switching to module parameters.");
    auto const *const pSource(getParameterAttribute(sideChainSourceAttributeName_));
    return pSource ? std::string_view(pSource) : std::string_view();
}

std::optional<unsigned int> ParametersLoader::getLegacyInputMode()
{
    LE_ASSERT_MSG(!switchedToModuleParameters(),
                  "The input mode must be fetched before switching to module parameters.");
    auto const *const pInputMode(getParameterAttribute(legacyInputModeAttributeName_));
    if (!pInputMode)
        return std::nullopt;
    return Utility::lexical_cast<unsigned int>(pInputMode);
}

namespace
{
class LFODataLoader
{
  public:
    LFODataLoader(TiXmlElement const &parameterNode, Parameters::LFOImpl const &lfo)
        : parameterNode_(parameterNode), lfo_(lfo)
    {
    }

    template <class LFOParameter> void operator()(LFOParameter &element) const
    {
        using namespace Parameters;
        doLoad(std::string(streamingName<LFOParameter>()).c_str(), element);
    }

  private:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The range check is the same one `ParametersLoader::operator()`
    /// makes for every other parameter in a preset, and it was missing here.
    /// `Parameter::setValue`'s own is an assertion, so in a release build
    /// whatever `lexical_cast` produced -- 0 to 255 for the enumerated ones,
    /// any double for the rest -- was simply stored.
    ///
    ///   `wfrm` is the sharp one: it indexes `lfoFunctions[]`, eleven entries of
    /// function pointer, once per block for as long as the LFO is enabled and
    /// with no bound of its own (lfoImpl.cpp:357). A `.swp` or a session naming
    /// `wfrm="200"` was an indirect call through whatever followed that table,
    /// on the audio thread, on the first block after the load. `ph`, `lbnd` and
    /// `ubnd` out of range make `LE_ASSUME(position >= 0 && <= 1)` false inside
    /// the waveform functions, which is undefined behaviour by construction.
    ///
    ///   An out-of-range value is treated exactly as a missing one -- the
    /// parameter goes to its default -- rather than clamped: there is no
    /// meaningful nearest waveform, and the two cases are the same statement
    /// about the file, which is that it does not say what this parameter is.
    ///
    ////////////////////////////////////////////////////////////////////////////

    template <class LFOParameter>
    void doLoad(char const *const elementName, LFOParameter &element) const
    {
        auto const *const pElementValue(parameterNode_.Attribute(elementName));
        if (pElementValue)
        {
            auto const value(lfo_.adjustValueFromPreset<LFOParameter>(
                static_cast<typename LFOParameter::value_type>(
                    Utility::lexical_cast<typename LFOParameter::binary_type>(pElementValue))));
            if (element.isValidValue(value))
            {
                element = value;
                return;
            }
        }

        /// \note If the preset does not specify a specific LFO
        /// parameter we need to explicitly reset it to default in order
        /// to properly handle reused module instances (which might have
        /// the particular parameter set to a non-default value). This
        /// also covers the case of old/pre-synced-LFOs presets (for
        /// which the sync type parameter needs to be set to the default
        /// 'free' value).
        ///                               (09.10.2014.) (Domagoj Saric)
        element = LFOParameter::default_();
    }

  private:
    TiXmlElement const &parameterNode_;
    Parameters::LFOImpl const &lfo_;
}; // class LFODataLoader
} // anonymous namespace

bool ParametersLoader::loadLFO(TiXmlElement const &parameterNode, LFO &lfo) const
{
    /// \todo Clean up this coupling by removing any special internal LFO class
    /// knowledge from this function/class.
    ///                                       (18.02.2011.) (Domagoj Saric)

    // Implementation note:
    //   The LFO parameters have to be loaded in reverse order in order to load
    // the SyncTypes parameter before the PeriodScale parameter because the
    // LFO::adjustvalueFromPreset<PeriodScale>() function assumes the SyncTypes
    // parameter to already be loaded/set.
    //                                        (18.02.2011.) (Domagoj Saric)
    LE::Parameters::forEachReversed(lfo.parameters(), LFODataLoader(parameterNode, lfo));
    syncedLFOFound_ |= lfo.enabled() && (lfo.syncTypes() != LFO::Free);
    return lfo.enabled();
}

namespace
{
/// \brief The `<p n="…">` under \p parent, or null.
///
/// \note A linear walk, because a module has at most ten parameters and the
/// globals six or eight. The 2.x reader's own lookups are a TinyXML attribute
/// scan and a child-element scan, which are the same shape.
TiXmlElement const *findParameterNode(TiXmlElement const &parent, char const *const parameterName)
{
    for (auto const *pNode(parent.FirstChildElement(parameterNodeName_)); pNode;
         pNode = pNode->NextSiblingElement(parameterNodeName_))
    {
        auto const *const pName(pNode->Attribute(parameterNameAttributeName_));
        if (pName && (std::strcmp(pName, parameterName) == 0))
            return pNode;
    }
    return nullptr;
}
} // anonymous namespace

TiXmlElement const *ParametersLoader::noteAsRead(TiXmlElement const *const pNode) const
{
    if (pNode &&
        (std::find(readParameters_.begin(), readParameters_.end(), pNode) == readParameters_.end()))
        readParameters_.push_back(pNode);
    return pNode;
}

LE_NOINLINE char const *
ParametersLoader::getParameterAttribute(char const *const parameterName) const
{
    if (!pParameters_)
        return nullptr;
    if (grammar_ == Grammar::V3)
    {
        auto const *const pNode(noteAsRead(findParameterNode(parameters(), parameterName)));
        return pNode ? pNode->Attribute(parameterValueAttributeName_) : nullptr;
    }
    /// \note An attribute of the module element rather than a child of it, so
    /// there is nothing to record: reportUnreadParameters() walks elements. A 2.x
    /// plain parameter and a 2.x LFO-able one are two different shapes and the
    /// latter goes through getParameterNode() below.
    return parameters().Attribute(mangleName(parameterName).c_str());
}

LE_NOINLINE TiXmlElement const *
ParametersLoader::getParameterNode(char const *const parameterName) const
{
    if (!pParameters_)
        return nullptr;
    if (grammar_ == Grammar::V3)
        return noteAsRead(findParameterNode(parameters(), parameterName));
    return noteAsRead(parameters().FirstChildElement(mangleName(parameterName).c_str()));
}

char const *ParametersLoader::parameterValueText(TiXmlElement const &parameterNode) const
{
    /// \note `GetText()` on a 3.0 node would answer null -- `<p/>` has no text
    /// child -- which getParameterValue() reports as a missing parameter. Every
    /// LFO-able parameter in the file, silently defaulted.
    return (grammar_ == Grammar::V3) ? parameterNode.Attribute(parameterValueAttributeName_)
                                     : parameterNode.GetText();
}

////////////////////////////////////////////////////////////////////////////////
///
/// ParametersLoader::reportUnreadParameters()
/// ------------------------------------------
///
////////////////////////////////////////////////////////////////////////////////
///
/// \note Elements only, and only the current module's direct children. A 2.x
/// plain parameter is an *attribute* of the module element and a 2.x LFO-able one
/// is a child element; 3.0 makes everything a `<p>` child. So this catches the
/// LFO-able half of a 2.x file and all of a 3.0 one -- which is where a rename
/// would land, because the mangled element name is what the reader looks up.
///
/// \note An element the reader deliberately declines is still unread and is still
/// reported. There is exactly one such case in the shipped format -- a `<Gate>`
/// for a build compiled without it -- and no shipped bank has one,
/// which is why this can be an error rather than a list of exemptions. If a build
/// ever ships without it, this is where it will say so.
///
////////////////////////////////////////////////////////////////////////////////

void ParametersLoader::reportUnreadParameters() const
{
    if (!pParameters_)
        return;

    for (auto const *pNode(parameters().FirstChildElement()); pNode;
         pNode = pNode->NextSiblingElement())
    {
        if (std::find(readParameters_.begin(), readParameters_.end(), pNode) !=
            readParameters_.end())
            continue;

        /// \note The 3.0 name, where there is one: `<p n="Wet">` says more than
        /// `p`. A 2.x element's own name is the mangled parameter name.
        auto const *const pName((grammar_ == Grammar::V3)
                                    ? pNode->Attribute(parameterNameAttributeName_)
                                    : pNode->Value());
        reportPresetProblem(PresetProblem::UnknownParameter, pName ? pName : "");
    }
}

void ParametersLoader::warnAboutMissingParameter(char const *const pParameterName)
{
    LE_ASSERT(pParameterName);
    std::string_view const parameterName(pParameterName);
    if (parameterName != "Gate")
    {
        reportPresetProblem(PresetProblem::MissingParameter, parameterName);
    }
}

SavedPreset::SavedPreset()
{
    auto *const pHeaderNode(new TiXmlElement(headerNodeName_));
    xml().LinkEndChild(pHeaderNode);

    /// \note On the document rather than in setHeader(), because it is a
    /// property of the shape being built and not of the header data being put
    /// into it -- setHeader() is also what saveDirtyComment() calls on a
    /// *reparsed* file, and stamping 3 onto a 2.6 document there would claim a
    /// grammar the rest of that file is not written in.
    pHeaderNode->SetAttribute(formatAttributeName, int(currentFormatVersion));

    pGlobalParametersNode_ = new TiXmlElement(globalParametersNodeName_);
    pModuleParametersNode_ = new TiXmlElement(moduleParametersNodeName_);
    pHeaderNode->LinkEndChild(pGlobalParametersNode_);
    pHeaderNode->LinkEndChild(pModuleParametersNode_);
}

void SavedPreset::setHeader(PresetHeader const &header) { Preset::setHeader(header); }

ParametersSaver::ParametersSaver(SavedPreset &preset)
    : PresetHandler(preset), pParametersNode_(&preset.globalParametersNode())
{
}

std::string ParametersSaver::saveTo() const
{
    LE_ASSERT_MSG(moduleChainSaved_, "Module chain parameters not yet saved/parsed.");
    return preset().saveTo();
}

/// \note `<Module effect="Ah-ah">`, where 2.x wrote `<Ah-ah>`. The name is no
/// longer mangled because it is no longer an element name: an attribute value
/// takes any character, which is what the whole of `mangleName()` and the
/// read-side repair beside it exist to work around. 3.0 cannot emit a document
/// that needs repairing.
void ParametersSaver::saveEffectModuleChain(AutomatedModuleChain const &moduleChain)
{
    LE_ASSERT_MSG(!moduleChainSaved_, "Already switched to modules."); //...mrmlj...
    moduleChainSaved_ = true;

    moduleChain.forEach<PresetModule>([&](PresetModule const &module) {
        auto *const pModuleNode(new TiXmlElement(moduleNodeName_));
        pModuleNode->SetAttribute(moduleEffectAttributeName_,
                                  Effects::effectStreamingName(module.effectTypeIndex()));
        preset().moduleParametersNode().LinkEndChild(pModuleNode);
        pParametersNode_ = pModuleNode;
        module.savePresetParameters(*this);
    });
}

TiXmlElement &ParametersSaver::newParameterNode(char const *const parameterName,
                                                std::string const &parameterValue)
{
    auto *const pParameterNode(new TiXmlElement(parameterNodeName_));
    pParameterNode->SetAttribute(parameterNameAttributeName_, parameterName);
    pParameterNode->SetAttribute(parameterValueAttributeName_, parameterValue);
    parameters().LinkEndChild(pParameterNode);
    return *pParameterNode;
}

void ParametersSaver::saveParameter(char const *const parameterName,
                                    std::string const &parameterValue)
{
    newParameterNode(parameterName, parameterValue);
}

// ...mrmlj...cannot put LFODataSaver into the anonymous namespace because it is
// declared as friend in the PresetHandler class...clean this up...
//namespace
//{
class LFODataSaver
{
  public:
    using LFO = Parameters::LFOImpl;

    /// \note No handler: this used to hold a PresetHandler & and never ask it
    /// anything, because the one thing it wants from one -- makeString() -- is
    /// static. The reference outlived every use of it.
    LFODataSaver(TiXmlElement &parameterNode, LFO const &lfo)
        : parameterNode_(parameterNode), lfo_(lfo)
    {
    }

#pragma warning(push)
#pragma warning(disable : 4127) // Conditional expression is constant.

    template <class LFOParameter> void operator()(LFOParameter const &element) const
    {
        // Implementation note:
        //   A preset with a bank of five TuneWorx modules breaches the 4096
        // bytes limit. As a workaround we save only LFO parameters that
        // have non default values to reduce the size of the presets.
        //                                (21.07.2011.) (Domagoj Saric)
        // Implementation note:
        //   The SyncTypes parameter has to be saved always, otherwise the
        // preset gets loaded as an 'old'/'pre-synced-LFOs' preset (with the
        // default sync type set to 'Free' for all parameters of all
        // modules, see the note in ParametersLoader::loadLFO() for more
        // info).
        //                                (26.07.2011.) (Domagoj Saric)
        if (!std::is_same<LFOParameter, LFO::SyncTypes>::value &&
            Math::equal(element.getValue(), element.default_()))
            return;

        using namespace Parameters;
        parameterNode_.SetAttribute(std::string(streamingName<LFOParameter>()),
                                    PresetHandler::makeString(lfo_.adjustValueForPreset(element)));
    }

#pragma warning(pop)

  private:
    TiXmlElement &parameterNode_;
    LFO const &lfo_;
};
//} // anonymous namespace

/// \note The same `<p n v>` as a parameter with no LFO, with the LFO's settings
/// alongside as further attributes -- `on`, `T`, `ph`, `lbnd`, `ubnd`, `sync`,
/// `wfrm`, unchanged and in the same place they occupied in 2.x. That is what
/// keeps LFODataSaver and LFODataLoader out of this change entirely: the
/// reversed-order load, the SyncTypes-before-PeriodScale ordering and
/// adjustValueForPreset() are the subtlest part of the format and they see the
/// same element they always did.
///
///   In 2.x the two overloads wrote two different things -- an attribute on the
/// parent, or an element with the value as its text -- and the loader still has
/// to look for both, because that is what the committed files contain.
void ParametersSaver::saveParameter(char const *const parameterName,
                                    std::string const &parameterValue, LFO const &parameterLFO)
{
    auto &parameterNode(newParameterNode(parameterName, parameterValue));
    LE::Parameters::forEach(parameterLFO.parameters(), LFODataSaver(parameterNode, parameterLFO));
}

/*
    ...mrmlj...temporarily reverting to old code for the 2.1 release...
void ParametersSaver::setSampleFileName( juce::String const & sampleFileName )
{
    std::size_t const sampleFileNameLength( sampleFileName.length() );
    char * const pSampleFileName( xml().allocate_string( sampleFileName, sampleFileNameLength + 1 ) );
    /// \todo Add checks here and in all similar places that an attribute is not
    /// saved more than once.
    ///                                       (03.02.2010.) (Domagoj Saric)
    saveParameter( sampleAttributeName_, std::string_view( pSampleFileName, sampleFileNameLength ) );
}*/

void ParametersSaver::setSampleFileName(std::string_view const &sampleFileName)
{
    /// \todo Add checks here and in all similar places that an attribute is not
    /// saved more than once.
    ///                                           (03.02.2010.) (Domagoj Saric)
    saveParameter(sampleAttributeName_, std::string(sampleFileName));
}

/// \note Written always, where the sample name is written only when there is one.
/// The sample is a thing a patch may or may not have; the source is a thing every
/// patch has, and a file that omitted it would be indistinguishable from a 2.x
/// file and get migrated rather than read.
void ParametersSaver::setSideChainSource(SideChainSource const source)
{
    saveParameter(sideChainSourceAttributeName_, std::string(toString(source)));
}

/// \note It took JUCE's file and string types and converted both to UTF-8 here,
/// under a `JUCE_STRING_UTF_TYPE` switch with an `_alloca` in one arm. The
/// conversion belongs at the interface's edge rather than in the format layer,
/// and putting it there is what takes JUCE off `sw-dsp`.
///
/// \note The path half of that conversion is gone rather than moved: everything
/// above speaks `fs::path` now, and `presetStorage.hpp`'s `savePreset()` calls
/// this with `u8string()`. Only the comment still arrives as a `juce::String`,
/// and it is converted by the editor. \see tests/checkNoJuceFile.cmake.
std::string savePreset(std::string_view const externalSampleFilePath,
                       SideChainSource const sideChainSource, std::string_view const comment,
                       Program const &program, DawExtraState const *const pDawExtraState)
{
    PresetHeader const presetHeader(comment);
    SavedPreset preset;
    ParametersSaver parametersSaver(preset);

    preset.setHeader(presetHeader);

    LE::Parameters::forEach(program.parameters(), parametersSaver);

    if (!externalSampleFilePath.empty())
    {
        /*  ...mrmlj...temporarily reverting to old code for the 2.1 release...

        // Implementation note:
        //   For "known"/"factory default" samples (that we supply with
        // SpectrumWorx and that reside in the "Samples" folder we only save
        // the file name (so that the presets do not look 'weird' to users if
        // they open them in a text editor on a Mac that has a completely
        // different folder structure than Windows).
        //                                    (06.12.2010.) (Domagoj Saric)
        juce::String const sampleFileName
        (
        sample_.sampleFile().isAChildOf( GUI::rootPath().getChildFile( "Samples" ) )
        ? sample_.sampleFile().getFileName    ()
        : sample_.sampleFile().getFullPathName()
        );
        parametersSaver.setSampleFileName( sampleFileName );
        */
        parametersSaver.setSampleFileName(externalSampleFilePath);
    }

    parametersSaver.setSideChainSource(sideChainSource);

    parametersSaver.saveEffectModuleChain(program.moduleChain());

    /// \note Last, so that the block a host reads back sits after the audio
    /// state it belongs to rather than in front of it, and written even when the
    /// hook puts nothing in it -- see the note on DawExtraState.
    if (pDawExtraState)
    {
        auto *const pNode(new TiXmlElement(Preset::dawExtraStateNodeName));
        preset.root().LinkEndChild(pNode);
        if (pDawExtraState->to)
            pDawExtraState->to(*pNode);
    }

    return parametersSaver.saveTo();
}

} // namespace SW

} // namespace LE
