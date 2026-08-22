////////////////////////////////////////////////////////////////////////////////
///
/// \file effectNames.hpp
/// ---------------------
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef effectNames_hpp__0A58D044_6AE5_42F8_9081_104708AA2175
#define effectNames_hpp__0A58D044_6AE5_42F8_9081_104708AA2175
//------------------------------------------------------------------------------
#include <string_view>

#include <cstdint>

namespace LE::SW::Effects
{

/// \brief The effect's title, as shown to a user.
char const *effectName(std ::uint8_t effectIndex);
std::int8_t effectIndex(std::string_view effectName);

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The name the effect is written to a preset or to session state under.
///
/// \note Separate from the title for the same reason a parameter's streaming
/// name is separate from its display name: a preset names its modules, so
/// retitling an effect would orphan every file that used it. Defaults to the
/// title -- which is what the shipped presets contain -- and is pinned by
/// specialising `EffectStreamingName` (effectStreamingNames.hpp) when a title
/// has to move. Pinned as a set by tests/parameters/streamingNameTests.cpp.
///
////////////////////////////////////////////////////////////////////////////////

template <class Effect> struct EffectStreamingName
{
    static constexpr char const *string_{Effect::title};
};

/// \brief Pins one, holding it still while the title moves. Goes in
/// effectNames.cpp, above the table, which is where the pins are auditable as a
/// set.
#define LE_SW_EFFECT_STREAMING_NAME(effect, name)                                                  \
    template <> struct EffectStreamingName<effect>                                                 \
    {                                                                                              \
        static constexpr char const *string_{name};                                                \
    };

char const *effectStreamingName(std::uint8_t effectIndex);
std::int8_t effectIndexFromStreamingName(std::string_view streamingName);

} // namespace LE::SW::Effects

#endif // effectNames_hpp
