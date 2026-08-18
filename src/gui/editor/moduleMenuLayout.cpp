////////////////////////////////////////////////////////////////////////////////
///
/// moduleMenuLayout.cpp
/// --------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "moduleMenuLayout.hpp"

#include "le/spectrumworx/effects/configuration/constants.hpp"
#include "le/spectrumworx/effects/configuration/effectNames.hpp"

#include <array>
#include <exception>
#include <iterator>
#include <string>
#include <vector>

#include <cstdint>
#include <cstdio>

namespace LE::SW::GUI
{

//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
//
// The menu
// --------
//
//   One array per group, in the order the group lists them, and then the groups
// in the order the menu lists *them*. Both orders are free: this table is read
// by nothing but the menu, and the effect indices it resolves to are looked up
// rather than counted.
//
//   The names are streaming names. For all but nine effects that is the title as
// well; the nine phase-vocoder ones were retitled after presets had named them,
// so what stands here is the older spelling -- "PVD start" is what the menu shows
// as "To PV", and "(pvd)" is what it shows as "(PV)". \see effectNames.cpp, which
// is where those nine pins are, and issue #80.
//
////////////////////////////////////////////////////////////////////////////////

constexpr char const *pitch[]{"Pitch Shifter", "Pitch Follower", "TuneWorx", "Pitch Magnet",
                              "Sumo Pitch",    "Pitch Spring",   "Octaver"};

constexpr char const *timbre[]{"Bandpass", "Bandstop", "Ah-ah", "Smoother",
                               "Sharper",  "Centroid", "Tonal", "Atonal"};

constexpr char const *time[]{"Freeze", "Slicer", "Wobbler", "Reverser", "Imploder", "Exploder"};

constexpr char const *space[]{"Frecho", "Frevcho", "Freqverb"};

constexpr char const *phase[]{"Robotizer", "Whisperer", "Phasevolution", "Phlip"};

constexpr char const *loudness[]{"Gain", "Exaggerator", "Denoiser", "Quiet Boost", "Freqnamics"};

constexpr char const *combine[]{"Talking Wind", "Convolver", "Ethereal", "Vaxateer", "Shapeless",
                                "Colorifer",    "Merger",    "Blender",  "Inserter", "Burrito"};

constexpr char const *phaseVocoder[]{
    "PVD start",      "Pitch Shifter (pvd)", "Pitch Follower (pvd)",
    "TuneWorx (pvd)", "Pitch Magnet (pvd)",  "Pitch Spring (pvd)",
    "Imploder (pvd)", "Exploder (pvd)",      "PVD stop"};

constexpr char const *miscellaneous[]{"Armonizer", "Slew Limiter", "Shifter", "Swappah",
                                      "Quantizer"};

struct GroupSource
{
    char const *title;
    Utility::Span<char const *const> effects;
}; // struct GroupSource

/// \note **This is the order the menu is drawn in.** Moving a group is moving a
/// line here; moving an effect between groups is moving a name above.
constexpr GroupSource layout[]{
    {"Pitch", pitch},
    {"Timbre", timbre},
    {"Time", time},
    {"Space", space},
    {"Phase", phase},
    /// Straight after Phase, which is the move issue #121 was opened for and
    /// which the effect list could not express: these nine are the last nine
    /// effects by index and used to be the last group by consequence.
    {"Phase Vocoder", phaseVocoder},
    {"Loudness", loudness},
    {"Combine", combine},
    {"Miscellaneous", miscellaneous},
};

constexpr std::size_t numberOfGroups{std::size(layout)};

////////////////////////////////////////////////////////////////////////////////
///
/// \class ResolvedLayout
///
/// \brief The table above with its names turned into effect indices, and
/// whatever is wrong with it.
///
/// \note The indices live in one flat vector and each group spans a stretch of
/// it, so a Group's span stays valid for the life of this object -- which is the
/// life of the process, since the one instance is a function-local static.
///
////////////////////////////////////////////////////////////////////////////////

class ResolvedLayout
{
  public:
    ResolvedLayout()
    {
        indices_.reserve(Effects::Constants::numberOfEffects);
        std::array<bool, Effects::Constants::numberOfEffects> listed{};

        std::size_t groupIndex{0};
        for (auto const &group : layout)
        {
            auto const first(indices_.size());
            for (auto const *const streamingName : group.effects)
            {
                auto const effect(Effects::effectIndexFromStreamingName(streamingName));
                if (effect < 0)
                {
                    fail(std::string("no effect streams as \"") + streamingName + "\", but \"" +
                         group.title + "\" lists one");
                    continue;
                }
                if (listed[static_cast<std::size_t>(effect)])
                {
                    fail(std::string("\"") + streamingName + "\" is listed more than once");
                    continue;
                }
                listed[static_cast<std::size_t>(effect)] = true;
                indices_.push_back(static_cast<std::uint8_t>(effect));
            }
            extents_[groupIndex++] = {first, indices_.size() - first};
        }

        for (std::uint8_t effect{0}; effect < Effects::Constants::numberOfEffects; ++effect)
            if (!listed[effect])
                fail(std::string("\"") + Effects::effectStreamingName(effect) +
                     "\" is in no menu group");

        // After the vector has stopped growing, so that the spans point at where
        // the indices ended up rather than at where they once were.
        for (std::size_t index{0}; index < numberOfGroups; ++index)
            groups_[index] = {
                layout[index].title,
                Utility::makeSpan(indices_.data() + extents_[index].first, extents_[index].count)};
    }

    Utility::Span<ModuleMenuLayout::Group const> groups() const
    {
        return Utility::makeSpan(groups_);
    }
    std::string const &diagnostic() const { return diagnostic_; }

  private:
    void fail(std::string what)
    {
        if (!diagnostic_.empty())
            diagnostic_ += "; ";
        diagnostic_ += std::move(what);
    }

  private:
    struct Extent
    {
        std::size_t first;
        std::size_t count;
    };

    std::vector<std::uint8_t> indices_;
    std::array<Extent, numberOfGroups> extents_{};
    std::array<ModuleMenuLayout::Group, numberOfGroups> groups_{};
    std::string diagnostic_;
}; // class ResolvedLayout

/// \note A function-local static, so the resolution happens on the first menu
/// rather than during static initialisation -- the streaming-name table it reads
/// lives in another translation unit, and their order is not ours to choose.
ResolvedLayout const &resolvedLayout()
{
    static ResolvedLayout const resolved;
    return resolved;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

Utility::Span<ModuleMenuLayout::Group const> ModuleMenuLayout::groups()
{
    auto const &resolved(resolvedLayout());
    if (!resolved.diagnostic().empty()) [[unlikely]]
    {
        /// \note Loud and terminal. An effect missing from this table is an
        /// effect nobody can add, and every other part of the plugin -- the
        /// preset loader, the parameter table, the engine -- would go on working
        /// perfectly with it, so nothing downstream is going to notice. The
        /// table is a constant of the build, so this cannot depend on what a
        /// user did: it either holds on every machine or on none.
        std::fprintf(stderr, "SpectrumWorx: the module menu does not list every effect -- %s\n",
                     resolved.diagnostic().c_str());
        std::fflush(stderr);
        std::terminate();
    }
    return resolved.groups();
}

std::string ModuleMenuLayout::diagnose() { return resolvedLayout().diagnostic(); }

} // namespace LE::SW::GUI
