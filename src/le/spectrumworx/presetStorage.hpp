////////////////////////////////////////////////////////////////////////////////
///
/// \file presetStorage.hpp
/// -----------------------
///
///   A preset file, read and written, with no JUCE in sight.
///
///   presets.hpp is the *format* -- XML in a writable buffer, in and out of the
/// parameter tree -- and deliberately opens nothing. This is the half that
/// touches the file system, over `std::filesystem::path` and `<fstream>` so that
/// it can live in `sw-dsp` beside the format it serves.
///
/// \note **No `juce::File` on any of these.** doc/tech/threading_model.md §6
/// requires preset loading tests to link without JUCE, and this is the whole of
/// what such a test needs.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef presetStorage_hpp__3D75A0C6_41E9_4F28_B1D3_8C60E7A4295B
#define presetStorage_hpp__3D75A0C6_41E9_4F28_B1D3_8C60E7A4295B
//------------------------------------------------------------------------------
#include "presets.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace LE::SW
{

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The largest thing this reader will treat as a preset.
///
///   A preset is a page of XML -- the largest shipped one is a couple of KB
/// and the format has no element that grows with anything but the module count,
/// which is capped at five -- so this is about seven thousand times the real
/// worst case and exists only to bound what a file can ask to be allocated.
///
/// \note Session state is read by the other reader, `readWholeStream`, and is
/// held to the same number for the same reason: it is the same grammar, and the
/// sample is a filename in it rather than the audio.
///
////////////////////////////////////////////////////////////////////////////////

inline constexpr std::uintmax_t maximumPresetSize{16u << 20};

/// \brief Reads a preset file into the writable, NUL-terminated buffer that the
/// destructive parse in Preset::loadFrom() requires.
/// \return an empty pointer if the file cannot be read, or is larger than
/// \ref maximumPresetSize.
Preset::InMemoryPreset readPresetFile(std::filesystem::path const &);

/// \brief Replaces a preset file's contents, creating the file if it is absent.
bool writePresetFile(std::filesystem::path const &, char const *data, unsigned int size);

/// \brief Truncating copy of a NUL-terminated name into a Program::Name.
void copyPresetName(char const *name, std::span<char> target);

////////////////////////////////////////////////////////////////////////////////
///
/// \brief presets.hpp's savePreset(), written to \p file.
///
/// \note No size limit, and so no "does not fit" to report: the writer builds the
/// whole document in a string of its own, and a fixed buffer here would bound
/// nothing except what could be saved. Five TuneWorx modules pass 4 KB.
///
/// \note The terminator is written to the file, because the 2016 writer put one
/// there, and most committed presets carry it.
///
/// \param externalSample empty when no sample is loaded. Stored as UTF-8 bytes,
/// which is the one spelling that survives the preset being opened on another
/// machine.
/// \param author who is saving it, from the settings panel. Empty writes no
/// byline at all. \see presets.hpp's sanitisedAuthorName().
///
////////////////////////////////////////////////////////////////////////////////

void savePreset(std::filesystem::path const &file, std::filesystem::path const &externalSample,
                SideChainSource sideChainSource, std::string_view comment, std::string_view author,
                Program const &);

////////////////////////////////////////////////////////////////////////////////
///
/// loadPreset()
/// ------------
///
///   presets.hpp's, with the file read first.
///
////////////////////////////////////////////////////////////////////////////////

template <class PresetConsumer>
bool loadPreset(std::filesystem::path const &presetFile, bool const ignoreExternalSample,
                std::string *const pComment, char const *const presetName, PresetConsumer consumer)
{
    auto const pPresetData(readPresetFile(presetFile));
    if (!pPresetData)
        return false;
    consumer
        .notifyHostAboutPresetChangeBegin(); //...mrmlj...assumes host initiated change != loading from file
    bool const success(loadPreset(pPresetData.get(), ignoreExternalSample, pComment, consumer));
    if (success)
    {
        /// \note The program name mattered under VST2.4, where hosts reacted to
        /// audioMasterUpdateDisplay only if it had changed. That protocol is
        /// gone; the name is kept because the editor still shows it.
        ///                                   (12.09.2014.) (Domagoj Saric)
        copyPresetName(presetName, consumer.program().name());
    }
    consumer.notifyHostAboutPresetChangeEnd();
    return success;
}

} // namespace LE::SW

#endif // presetStorage_hpp
