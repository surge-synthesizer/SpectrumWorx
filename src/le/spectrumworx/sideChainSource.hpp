////////////////////////////////////////////////////////////////////////////////
///
/// \file sideChainSource.hpp
/// -------------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef sideChainSource_hpp__0A3F5C81_6D24_4B93_9E7A_2C15D0F8B647
#define sideChainSource_hpp__0A3F5C81_6D24_4B93_9E7A_2C15D0F8B647
//------------------------------------------------------------------------------
#include <cstdint>
#include <optional>
#include <string_view>

namespace LE::SW
{

////////////////////////////////////////////////////////////////////////////////
///
/// \enum SideChainSource
///
/// \brief What feeds the engine's side channel. Three answers, and there is no
/// fourth.
///
///   This is the patch's, and it is the user's: it is what the audio-file
/// selector answers. It is deliberately **not** a statement about the plugin's
/// bus topology -- how many ports the plugin has and how many channels each
/// carries is a handshake between the plugin, the host and the track, and a
/// setting that claimed to decide it would be claiming something it does not
/// get to decide. \see doc/tech/sidechain-approach.md and issue #113.
///
/// \note 2016 spelled this as an `InputMode` parameter -- a *bus* setting --
/// crossed with "is a file loaded", which produced a truth table with one corner
/// nobody could explain (2x2 with no file, which quietly meant `Main`). The three
/// values below are that table with the corner named. `presets.hpp` migrates an
/// old file's `Input_mode` into one of them.
///
////////////////////////////////////////////////////////////////////////////////

enum struct SideChainSource : std::uint8_t
{
    /// A decoded audio file, read forwards and wrapping at its end.
    File,
    /// The main input, so that an effect side-chains against itself.
    Main,
    /// The host's second input port.
    Host
};

/// \brief What a new patch gets.
///
/// \note `Host`, so that a user who has patched a send hears it without going
/// looking for a setting first. With nothing patched it reads as `Main` anyway --
/// \see the fallbacks in sidechain-approach.md -- so the cost of being wrong
/// about it is nothing.
inline constexpr SideChainSource defaultSideChainSource{SideChainSource::Host};

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The spelling that goes into a file.
///
/// \note **By name, not by ordinal.** A fourth value appended later cannot then
/// change what an existing file means, which an ordinal would leave to luck. It
/// is the rule the preferences file already follows for its two enumerations
/// (`streaming_format.md` §4.4), and the reason it is worth restating here is
/// that every *parameter* in this tree streams as a number -- so this is the
/// exception, and it is one deliberately.
///
////////////////////////////////////////////////////////////////////////////////

constexpr char const *toString(SideChainSource const source)
{
    switch (source)
    {
    case SideChainSource::File:
        return "file";
    case SideChainSource::Main:
        return "main";
    case SideChainSource::Host:
        return "host";
    }
    return "host";
}

/// \brief `toString()` backwards. Nothing for a spelling this build does not
/// know, which is what lets the caller tell "the patch does not say" from "the
/// patch says something".
constexpr std::optional<SideChainSource> sideChainSourceFromString(std::string_view const text)
{
    if (text == "file")
        return SideChainSource::File;
    if (text == "main")
        return SideChainSource::Main;
    if (text == "host")
        return SideChainSource::Host;
    return std::nullopt;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief 2016's `Input_mode`, as a source.
///
/// \note The four values were `(Stereo)(StereoSideChain)(Mono)(MonoSideChain)`,
/// so the odd ones are the two that asked the host for a side chain. Every one of
/// the 288 shipped presets carries one, and this is the whole of what it means
/// once a file is out of the picture -- when a patch names a file, the file is
/// the source whatever the mode said, which is what 2016 did.
///
/// \note Mono is not a source and never was. It says how many channels each of
/// these carries, which is issue #114.
///
////////////////////////////////////////////////////////////////////////////////

constexpr SideChainSource sideChainSourceFromLegacyInputMode(unsigned int const inputMode)
{
    return (inputMode % 2 != 0) ? SideChainSource::Host : SideChainSource::Main;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief What a patch being loaded means, whether or not it says so.
///
/// \param recorded what the patch wrote, empty when it wrote nothing -- which is
///        every 2.x file and every 3.0 file older than 18.08.2026.
/// \param legacyInputMode 2016's `Input_mode`, where the patch has one.
/// \param haveSample whether a file ended up loaded. Not "whether the patch named
///        one": a patch loaded with the browser's "Ignore external audio" on
///        names one and gets none, and a named file that will not decode is
///        cleared and reported.
///
/// \note **The migration is 2016's truth table, recovered exactly.** That table
/// read as a bus setting crossed with "is a file loaded":
///
///     file loaded -> the file, in either mode
///     no file     -> 2x2: the main input, 4x2: the host's port
///
/// so the file wins where there is one, and `Input_mode` decides the rest. Its
/// odd values are the two that asked the host.
///
/// \note Shared by the plugin's loader and by the test harness deliberately: a
/// migration each of them implemented separately would be a migration only one of
/// them was testing.
///
/// \note A `File` that cannot be honoured is not returned. Nothing downstream has
/// to cope with a source naming a file that is not there -- \see the same rule in
/// `SpectrumWorxCLAP::setSideChainSource()`.
///
////////////////////////////////////////////////////////////////////////////////

constexpr SideChainSource resolveSideChainSource(std::string_view const recorded,
                                                 std::optional<unsigned int> const legacyInputMode,
                                                 bool const haveSample)
{
    auto const source(sideChainSourceFromString(recorded));
    if (source)
        return ((*source == SideChainSource::File) && !haveSample) ? SideChainSource::Main
                                                                   : *source;

    if (haveSample)
        return SideChainSource::File;

    return sideChainSourceFromLegacyInputMode(legacyInputMode.value_or(0));
}

} // namespace LE::SW

#endif // sideChainSource_hpp
