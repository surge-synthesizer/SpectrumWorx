////////////////////////////////////////////////////////////////////////////////
///
/// presetStorage.cpp
/// -----------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "presetStorage.hpp"

#include "le/utility/assert.hpp"

#include <cstring>
#include <fstream>
#include <new>
#include <system_error>

namespace LE::SW
{

////////////////////////////////////////////////////////////////////////////////
//
// readPresetFile()
// ----------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The buffer is one byte longer than the file and terminated, because the
/// parser is handed a C string. Most shipped presets already end in a
/// NUL -- Preset::saveTo() appends one and the 2016 writer put it on disk -- and
/// the rest do not. Appending unconditionally is correct for both: a second NUL after
/// the first is never reached.
///
/// \note `<fstream>` where this was `juce::File::loadFileAsData`. Same bytes,
/// and it is what lets the reader sit beside the format in `sw-dsp` -- see the
/// note on the header.
///
////////////////////////////////////////////////////////////////////////////////

Preset::InMemoryPreset readPresetFile(std::filesystem::path const &file)
{
    std::error_code error;
    auto const fileSize(std::filesystem::file_size(file, error));
    if (error)
        return {};

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note The size is checked before it is narrowed, which it was not.
    /// `static_cast<unsigned int>(fileSize)` on a file over 4 GiB kept
    /// `size mod 2^32` and then parsed that prefix as a whole preset; at exactly
    /// 4 GiB - 1 the `presetSize + 1` below wrapped to zero, so a zero-length
    /// allocation was handed a 4 GiB read.
    ///
    ///   Neither needs a hostile file to reach -- any large file renamed to
    /// `.swp`, or picked in the browser, is one.
    ///
    ////////////////////////////////////////////////////////////////////////////

    if (fileSize > maximumPresetSize)
        return {};

    auto const presetSize(static_cast<unsigned int>(fileSize));

    std::ifstream stream(file, std::ios::binary);
    if (!stream)
        return {};

    Preset::InMemoryPreset pInMemoryPreset(new (std::nothrow) char[presetSize + 1]);
    if (!pInMemoryPreset)
        return {};

    stream.read(pInMemoryPreset.get(), presetSize);
    if (stream.gcount() != static_cast<std::streamsize>(presetSize))
        return {};
    pInMemoryPreset[presetSize] = '\0';
    return pInMemoryPreset;
}

/// \note Writes a temporary and renames it, so an interrupted save leaves the
/// previous preset intact -- which is what `juce::File::replaceWithData` did and
/// what the `map_file`-and-memcpy before it did not: that one truncated the file
/// to the new length first, and a failure anywhere after that lost it.
bool writePresetFile(std::filesystem::path const &file, char const *const data,
                     unsigned int const size)
{
    auto temporary(file);
    temporary += ".tmp";

    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.write(data, size))
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary, file, error);
    if (error)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

void copyPresetName(char const *const name, std::span<char> const target)
{
    LE_ASSERT(!target.empty());
    if (target.empty())
        return;

    std::size_t written(0);
    if (name)
    {
        auto const limit(target.size() - 1);
        while ((written < limit) && (name[written] != '\0'))
        {
            target[written] = name[written];
            ++written;
        }
    }
    target[written] = '\0';
}

void savePreset(std::filesystem::path const &file, std::filesystem::path const &externalSample,
                SideChainSource const sideChainSource, std::string_view const comment,
                Program const &program)
{
    std::error_code error;
    LE_ASSERT(std::filesystem::is_directory(file.parent_path(), error));

    /// \note `u8string()`, not `string()`: the sample path goes into the document
    /// as UTF-8 bytes on every platform, and `string()` is the active code page
    /// on Windows. This conversion used to be `getFullPathName().toRawUTF8()` a
    /// layer up, in presetFile.cpp, and it was the only thing that file did.
    auto const sample(externalSample.u8string());

    auto const preset(savePreset(sample, sideChainSource, comment, program));

    if (!writePresetFile(file, preset.c_str(), static_cast<unsigned int>(preset.size() + 1)))
        reportPresetProblem(PresetProblem::SaveFailed);
}

} // namespace LE::SW
