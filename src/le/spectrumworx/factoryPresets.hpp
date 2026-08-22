////////////////////////////////////////////////////////////////////////////////
///
/// \file factoryPresets.hpp
/// ------------------------
///
///   The fifteen factory banks, compiled into the binary.
///
///   In 2016 the plugin found them on disk, at a path an installer had written
/// into a `SpectrumWorx.paths` file beside the binary. So a plugin that had been
/// copied rather than installed had no presets, and one whose folder had been
/// moved had none either. They are in the binary now: copy the bundle and the
/// banks come with it, which is what stage 8.2 asks for.
///
/// \note Read-only, and deliberately. A user's own presets are files, under the
/// directory presetFile.hpp's paths give -- a factory bank cannot be saved into
/// or deleted, and the browser has to know the difference.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef factoryPresets_hpp__3D82A6F1_74B5_4E09_A1C6_8B05F73D2E94
#define factoryPresets_hpp__3D82A6F1_74B5_4E09_A1C6_8B05F73D2E94
//------------------------------------------------------------------------------
#include "presets.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace LE::SW::FactoryPresets
{

/// The extension the banks are written in, without the dot.
inline constexpr std::string_view extension{"swp"};

/// \brief Every bank, sorted, as a path relative to the preset root -- `"Echoes"`,
/// and, where a bank nests, `"CinningBao"` and `"CinningBao/SideChainables"`.
///
/// \note Flat rather than a tree, and **not** one level only: a bank may hold a
/// sub-folder rather than presets, which the browser shows as a folder, so
/// neither simplification is available and the number of banks is not the number
/// of directories.
///
/// \note **A bank that holds no presets of its own is still a bank.** Listing
/// only the directories with a `.swp` in them leaves the browser no row to open
/// such a bank by -- see collectBanks(). presets() answers with an empty list for
/// one; isBank() says yes.
std::vector<std::string> banks();

/// \brief The preset names in \p bank, sorted, without the extension.
/// \return an empty list for a bank that is not there.
std::vector<std::string> presets(std::string_view bank);

/// \brief One preset's bytes, terminated so that Preset::loadFrom() can take
/// them straight.
/// \return an empty pointer if there is no such preset.
Preset::InMemoryPreset load(std::string_view bank, std::string_view preset);

/// \brief Whether \p bank names a factory bank -- which is how a caller tells a
/// read-only bank from a user directory of the same name.
bool isBank(std::string_view bank);

} // namespace LE::SW::FactoryPresets

#endif // factoryPresets_hpp
