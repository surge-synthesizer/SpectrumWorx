////////////////////////////////////////////////////////////////////////////////
///
/// factoryPresets.cpp
/// ------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "factoryPresets.hpp"

#include "le/utility/assert.hpp"

#include <cmrc/cmrc.hpp>

#include <algorithm>
#include <cstring>
#include <new>
#include <optional>
//------------------------------------------------------------------------------
CMRC_DECLARE(swAssets);

namespace LE::SW::FactoryPresets
{
namespace
{
//------------------------------------------------------------------------------

/// \note The resource library is rooted at assets/, and holds the skin as well,
/// hence the prefix. See assets/CMakeLists.txt for why there is only one.
constexpr std::string_view root{"presets"};

cmrc::embedded_filesystem assets() { return cmrc::swAssets::get_filesystem(); }

std::string bankPath(std::string_view const bank)
{
    return std::string(root) + '/' + std::string(bank);
}

std::string presetPath(std::string_view const bank, std::string_view const preset)
{
    return bankPath(bank) + '/' + std::string(preset) + '.' + std::string(extension);
}

/// \brief `"LE Boiler"` from `"LE Boiler.swp"`, or nothing if it is not a preset.
///
/// \note By suffix rather than by asking the filesystem: cmrc has no notion of a
/// file's type beyond is_file(), and the banks hold nothing else anyway.
std::optional<std::string> presetName(std::string const &fileName)
{
    auto const suffix(std::string(".") + std::string(extension));
    if ((fileName.size() <= suffix.size()) || !fileName.ends_with(suffix))
        return std::nullopt;
    return fileName.substr(0, fileName.size() - suffix.size());
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

namespace
{
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Depth-first, appending every directory below \p path that holds
/// presets **or leads to one**.
///
/// \return whether \p path has any preset under it, at any depth.
///
/// \note The second half of that is not a nicety. This listed only the
/// directories that hold a `.swp` themselves, and `Martin Walker`,
/// `CinningBao` and `Syndicate Synthetique` hold nothing but a sub-folder -- so
/// the list named `Martin Walker/Gamma Shift` and never `Martin Walker`, and the
/// browser shows a child only once its parent has been opened. There was no row
/// to open: three of the fifteen banks, 110 of the 303 presets, were in the
/// binary and unreachable from the interface.
///                                           (10.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

bool collectBanks(cmrc::embedded_filesystem const &filesystem, std::string const &path,
                  std::string const &relative, std::vector<std::string> &found)
{
    bool leadsToPresets{false};
    for (auto const &entry : filesystem.iterate_directory(path))
    {
        if (entry.is_directory())
            leadsToPresets |= collectBanks(
                filesystem, path + '/' + entry.filename(),
                relative.empty() ? entry.filename() : relative + '/' + entry.filename(), found);
        else if (presetName(entry.filename()))
            leadsToPresets = true;
    }

    if (leadsToPresets && !relative.empty())
        found.push_back(relative);
    return leadsToPresets;
}
} // anonymous namespace

std::vector<std::string> banks()
{
    std::vector<std::string> found;

    auto const filesystem(assets());
    if (!filesystem.is_directory(std::string(root)))
        return found;

    collectBanks(filesystem, std::string(root), {}, found);

    std::ranges::sort(found);
    return found;
}

std::vector<std::string> presets(std::string_view const bank)
{
    std::vector<std::string> found;

    auto const filesystem(assets());
    auto const path(bankPath(bank));
    if (!filesystem.is_directory(path))
        return found;

    for (auto const &entry : filesystem.iterate_directory(path))
        if (entry.is_file())
            if (auto const name(presetName(entry.filename())); name)
                found.emplace_back(*name);

    std::ranges::sort(found);
    return found;
}

bool isBank(std::string_view const bank)
{
    return !bank.empty() && assets().is_directory(bankPath(bank));
}

////////////////////////////////////////////////////////////////////////////////
//
// load()
// ------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note A copy, rather than the embedded bytes themselves. Two reasons, and the
/// first is sufficient: the parser is handed a C string and an embedded file is
/// not terminated -- it is a span of the binary, with the next resource after
/// it. The second is that a preset written by the 2016 plugin *does* end in a
/// NUL, so 193 of these 303 have one and 110 do not, and depending on which is
/// how a preset loads on some machines and not others.
///
////////////////////////////////////////////////////////////////////////////////

Preset::InMemoryPreset load(std::string_view const bank, std::string_view const preset)
{
    auto const filesystem(assets());
    auto const path(presetPath(bank, preset));
    if (!filesystem.exists(path) || !filesystem.is_file(path))
        return {};

    auto const file(filesystem.open(path));
    auto const size(static_cast<std::size_t>(file.end() - file.begin()));

    Preset::InMemoryPreset pInMemoryPreset(new (std::nothrow) char[size + 1]);
    if (pInMemoryPreset)
    {
        std::memcpy(pInMemoryPreset.get(), file.begin(), size);
        pInMemoryPreset[size] = '\0';
    }
    return pInMemoryPreset;
}

} // namespace LE::SW::FactoryPresets
