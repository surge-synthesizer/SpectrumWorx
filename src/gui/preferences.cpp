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

/// `sanitisedAuthorName()`, the rule on a byline.
#include "le/spectrumworx/authorName.hpp"

#include "le/utility/assert.hpp"
#include "le/utility/platformSpecifics.hpp"

#include <sst/plugininfra/userdefaults.h>

#include <algorithm>
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
    showLFOAnimationKey,
    previewLFOOnHoverKey,
    hideCursorOnKnobDragKey,
    zoomPercentKey,
    paletteKey,
    animationStyleKey,
    authorKey,
    numberOfPreferenceKeys
};

/// \note The provider calls this for every value in [0, numberOfPreferenceKeys)
/// when it is constructed, and these strings are the attribute names in the
/// file, so changing one silently retires whatever a user had chosen.
std::string preferenceKeyName(PreferenceKey const key)
{
    switch (key)
    {
    case showLFOAnimationKey:
        return "showLFOAnimation";
    case previewLFOOnHoverKey:
        return "previewLFOOnHover";
    case hideCursorOnKnobDragKey:
        return "hideCursorOnKnobDrag";
    case zoomPercentKey:
        return "zoomPercent";
    case paletteKey:
        return "palette";
    case animationStyleKey:
        return "animationStyle";
    case authorKey:
        return "author";
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
// The palette, by name
////////////////////////////////////////////////////////////////////////////////

/// \note Over ColourMap::nameOf() rather than a table here: the palettes are the
/// map's own enumeration and their spellings belong with it.
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

////////////////////////////////////////////////////////////////////////////////
// The animation style, by name
////////////////////////////////////////////////////////////////////////////////

AnimationStyle animationStyleNamed(std::string const &name,
                                   AnimationStyle const valueIfUnrecognised)
{
    for (unsigned int index(0); index < numberOfAnimationStyles; ++index)
    {
        auto const style(static_cast<AnimationStyle>(index));
        if (name == nameOf(style))
            return style;
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

    showLFOAnimation_ = provider.getUserDefaultValue(showLFOAnimationKey, showLFOAnimation_) != 0;
    previewLFOOnHover_ =
        provider.getUserDefaultValue(previewLFOOnHoverKey, previewLFOOnHover_) != 0;

    palette_ = paletteNamed(provider.getUserDefaultValue(paletteKey, ColourMap::nameOf(palette_)),
                            palette_);

    animationStyle_ = animationStyleNamed(
        provider.getUserDefaultValue(animationStyleKey, nameOf(animationStyle_)), animationStyle_);

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

    /// \note Sanitised, this file being the user's to edit: a quote typed in
    /// here may no more reach a preset than one typed into the panel.
    author_ = sanitisedAuthorName(provider.getUserDefaultValue(authorKey, author_));
}

bool Preferences::isOfferedZoom(unsigned int const percent)
{
    return std::find(zoomPercentages.begin(), zoomPercentages.end(), percent) !=
           zoomPercentages.end();
}

Preferences::~Preferences() = default;

void Preferences::setShowLFOAnimation(bool const value)
{
    showLFOAnimation_ = value;
    storage_->provider.updateUserDefaultValue(showLFOAnimationKey, value ? 1 : 0);
}

void Preferences::setPreviewLFOOnHover(bool const value)
{
    previewLFOOnHover_ = value;
    storage_->provider.updateUserDefaultValue(previewLFOOnHoverKey, value ? 1 : 0);
}

void Preferences::setPalette(ColourMap::Palette const value)
{
    palette_ = value;
    storage_->provider.updateUserDefaultValue(paletteKey, ColourMap::nameOf(value));
}

void Preferences::setAnimationStyle(AnimationStyle const value)
{
    animationStyle_ = value;
    storage_->provider.updateUserDefaultValue(animationStyleKey, nameOf(value));
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

void Preferences::setAuthor(std::string_view const name)
{
    author_ = sanitisedAuthorName(name);
    storage_->provider.updateUserDefaultValue(authorKey, author_);
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
