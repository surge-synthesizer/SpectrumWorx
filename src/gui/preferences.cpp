////////////////////////////////////////////////////////////////////////////////
///
/// \file preferences.cpp
/// ---------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "preferences.hpp"

/// `rootPath()`, which is where the file goes.
#include "gui.hpp"

#include "le/utility/assert.hpp"
#include "le/utility/platformSpecifics.hpp"

#include <sst/plugininfra/userdefaults.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <string>

namespace LE::SW::GUI
{

namespace
{
////////////////////////////////////////////////////////////////////////////////
// The file
////////////////////////////////////////////////////////////////////////////////

/// \note What the provider makes the file name out of: it writes
/// `<productName>UserDefaults.xml`. Named here rather than spelled twice so that
/// file() cannot drift from what is actually opened.
char const productName[]{"SpectrumWorx"};

std::string defaultsFileName() { return std::string(productName) + "UserDefaults.xml"; }

enum PreferenceKey
{
    moduleUIMouseOverReactionKey,
    lfoUpdateBehaviourKey,
    hideCursorOnKnobDragKey,
    zoomPercentKey,
    paletteKey,
    numberOfPreferenceKeys
};

/// \note The provider calls this for every value in [0, numberOfPreferenceKeys)
/// when it is constructed, and these strings are the attribute names in the
/// file, so changing one silently retires whatever a user had chosen.
std::string preferenceKeyName(PreferenceKey const key)
{
    switch (key)
    {
    case moduleUIMouseOverReactionKey:
        return "moduleUIMouseOverReaction";
    case lfoUpdateBehaviourKey:
        return "lfoUpdateBehaviour";
    case hideCursorOnKnobDragKey:
        return "hideCursorOnKnobDrag";
    case zoomPercentKey:
        return "zoomPercent";
    case paletteKey:
        return "palette";
    case numberOfPreferenceKeys:
        break;
    }
    LE_UNREACHABLE_CODE();
}

/// \note stderr, which is where a DAW's log picks it up, rather than
/// warningMessageBox(). The two things that reach here are a defaults file from
/// a future version and a folder that cannot be written to; neither is worth
/// putting a box in front of somebody who has just clicked a combo box, and the
/// second would put one there on every click.
void reportPreferencesError(std::string const &message, std::string const &title)
{
    std::fputs(title.c_str(), stderr);
    std::fputs(": ", stderr);
    std::fputs(message.c_str(), stderr);
    std::fputs("\n", stderr);
}

////////////////////////////////////////////////////////////////////////////////
// The enumerations, by name
////////////////////////////////////////////////////////////////////////////////

constexpr std::array<char const *, 3> mouseOverReactionNames{"Never", "WhenParentModuleSelected",
                                                             "WhenParentOrNothingSelected"};

constexpr std::array<char const *, 4> lfoUpdateBehaviourNames{"NoUpdate", "WhenControlSelected",
                                                              "WhenControlActive", "Always"};

/// The tables are indexed by the enumerator, so they have to cover it.
static_assert(mouseOverReactionNames.size() == Preferences::WhenParentOrNothingSelected + 1);
static_assert(lfoUpdateBehaviourNames.size() == Preferences::Always + 1);

template <typename Enumeration, std::size_t count>
std::string nameOf(Enumeration const value, std::array<char const *, count> const &names)
{
    auto const index(static_cast<std::size_t>(value));
    LE_ASSERT(index < count);
    return names[index];
}

template <typename Enumeration, std::size_t count>
Enumeration valueNamed(std::string const &name, std::array<char const *, count> const &names,
                       Enumeration const valueIfUnrecognised)
{
    for (std::size_t index(0); index < count; ++index)
        if (name == names[index])
            return static_cast<Enumeration>(index);
    return valueIfUnrecognised;
}

/// \note The same, over ColourMap::nameOf() rather than over a table here: the
/// palettes are the map's own enumeration and their spellings belong with it.
ColourMap::Palette paletteNamed(std::string const &name,
                                ColourMap::Palette const valueIfUnrecognised)
{
    for (unsigned int index(0); index < ColourMap::numberOfPalettes; ++index)
    {
        auto const palette(static_cast<ColourMap::Palette>(index));
        if (name == ColourMap::nameOf(palette))
            return palette;
    }
    return valueIfUnrecognised;
}

/// \note An optional rather than a function-local static, because
/// setPreferencesFolder() has to be able to replace it. `[main-thread]`, which is
/// what makes that safe: everything that reads these runs under the host's
/// message thread.
std::optional<Preferences> instance;
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
//
// Preferences::Storage
//
////////////////////////////////////////////////////////////////////////////////

class Preferences::Storage
{
  public:
    explicit Storage(fs::path const &folder)
        : provider(folder, productName, preferenceKeyName, reportPreferencesError),
          file(folder / defaultsFileName())
    {
    }

    sst::plugininfra::defaults::Provider<PreferenceKey, numberOfPreferenceKeys> provider;
    fs::path const file;
}; // class Preferences::Storage

////////////////////////////////////////////////////////////////////////////////
//
// Preferences
//
////////////////////////////////////////////////////////////////////////////////

Preferences::Preferences(fs::path const &folder) : storage_(std::make_unique<Storage>(folder))
{
    auto &provider(storage_->provider);

    moduleUIMouseOverReaction_ = valueNamed(
        provider.getUserDefaultValue(moduleUIMouseOverReactionKey,
                                     nameOf(moduleUIMouseOverReaction_, mouseOverReactionNames)),
        mouseOverReactionNames, moduleUIMouseOverReaction_);

    lfoUpdateBehaviour_ =
        valueNamed(provider.getUserDefaultValue(
                       lfoUpdateBehaviourKey, nameOf(lfoUpdateBehaviour_, lfoUpdateBehaviourNames)),
                   lfoUpdateBehaviourNames, lfoUpdateBehaviour_);

    palette_ = paletteNamed(provider.getUserDefaultValue(paletteKey, ColourMap::nameOf(palette_)),
                            palette_);

    hideCursorOnKnobDrag_ =
        provider.getUserDefaultValue(hideCursorOnKnobDragKey, hideCursorOnKnobDrag_) != 0;

    /// \note Checked against the offered list rather than clamped to its ends.
    /// A file naming a zoom this build does not have is a file from another
    /// build or from a text editor, and there is no reading of "300" that the
    /// combo box could then show.
    auto const zoom(static_cast<unsigned int>(
        provider.getUserDefaultValue(zoomPercentKey, static_cast<int>(zoomPercent_))));
    if (Preferences::isOfferedZoom(zoom))
        zoomPercent_ = zoom;
}

bool Preferences::isOfferedZoom(unsigned int const percent)
{
    return std::find(zoomPercentages.begin(), zoomPercentages.end(), percent) !=
           zoomPercentages.end();
}

Preferences::~Preferences() = default;

void Preferences::setModuleUIMouseOverReaction(ModuleUIMouseOverReaction const value)
{
    moduleUIMouseOverReaction_ = value;
    storage_->provider.updateUserDefaultValue(moduleUIMouseOverReactionKey,
                                              nameOf(value, mouseOverReactionNames));
}

void Preferences::setLFOUpdateBehaviour(LFOUpdateBehaviour const value)
{
    lfoUpdateBehaviour_ = value;
    storage_->provider.updateUserDefaultValue(lfoUpdateBehaviourKey,
                                              nameOf(value, lfoUpdateBehaviourNames));
}

void Preferences::setPalette(ColourMap::Palette const value)
{
    palette_ = value;
    storage_->provider.updateUserDefaultValue(paletteKey, ColourMap::nameOf(value));
}

void Preferences::setHideCursorOnKnobDrag(bool const value)
{
    hideCursorOnKnobDrag_ = value;
    storage_->provider.updateUserDefaultValue(hideCursorOnKnobDragKey, value ? 1 : 0);
}

void Preferences::setZoomPercent(unsigned int const percent)
{
    LE_ASSERT_MSG(isOfferedZoom(percent), "a zoom the Interface page cannot show");
    if (!isOfferedZoom(percent))
        return;

    zoomPercent_ = percent;
    storage_->provider.updateUserDefaultValue(zoomPercentKey, static_cast<int>(percent));
}

fs::path const &Preferences::file() const { return storage_->file; }

////////////////////////////////////////////////////////////////////////////////

Preferences &preferences()
{
    if (!instance)
        instance.emplace(rootPath());
    return *instance;
}

void setPreferencesFolder(fs::path const &folder) { instance.emplace(folder); }

} // namespace LE::SW::GUI
