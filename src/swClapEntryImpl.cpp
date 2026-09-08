////////////////////////////////////////////////////////////////////////////////
///
/// \file swClapEntryImpl.cpp
/// ------------------------
///
/// The CLAP plugin factory, plus the two sub-factories clap-wrapper asks for:
/// the AUv2 one it probes at build time to generate the Audio Unit's
/// Info.plist, and the VST3 one that supplies the vendor block a CLAP
/// descriptor has nowhere to put.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "swClapEntryImpl.hpp"
#include "spectrumWorxCLAP.hpp"

#include <clap/clap.h>
#include <clapwrapper/auv2.h>
#include <clapwrapper/vst3.h>

#include <cstring>

namespace LE::SW::ClapFirst
{
namespace
{
std::uint32_t pluginCount(clap_plugin_factory const *) { return 1; }

clap_plugin_descriptor const *pluginDescriptor(clap_plugin_factory const *,
                                               std::uint32_t const index)
{
    return index == 0 ? descriptor() : nullptr;
}

clap_plugin const *create(clap_plugin_factory const *, clap_host const *const host,
                          char const *const pluginID)
{
    if (std::strcmp(pluginID, descriptor()->id) == 0)
        return createPlugin(host);
    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
//
// The Audio Unit's four-character codes
// -------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \note The manufacturer is the Surge Synth Team's, so the AU aggregates with
/// the rest of the family in Logic's browser -- shortcircuit XT and Surge XT 2
/// both pass exactly this pair. The subtype is per product, and `SpWx` is
/// SpectrumWorx's since 2010; neither of those two uses it (`ScXT`, `sxt2`).
///
/// \note Four characters, not four-and-a-NUL: `au_subt` is `char[5]` and the
/// wrapper reads all four. A three-character code would leave the fourth byte
/// whatever `strncpy` padded it with, which is why the length is asserted here
/// rather than trusted to the literal.
///
////////////////////////////////////////////////////////////////////////////////

constexpr char auManufacturerCode[]{"SSTx"};
constexpr char auSubtypeCode[]{"SWrx"};
constexpr char auTypeCode[]{"aumf"};

static_assert(sizeof(auManufacturerCode) == 5, "An AU manufacturer code is four characters.");
static_assert(sizeof(auSubtypeCode) == 5, "An AU subtype code is four characters.");
static_assert(sizeof(auTypeCode) == 5, "An AU type code is four characters.");

bool auv2Info(clap_plugin_factory_as_auv2 const *, std::uint32_t const index,
              clap_plugin_info_as_auv2_t *const info)
{
    if (index != 0)
        return false;

    // Force type to an AUMF
    std::strncpy(info->au_type, auTypeCode, sizeof(info->au_type));
    std::strncpy(info->au_subt, auSubtypeCode, sizeof(info->au_subt));
    return true;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The identity this plugin used to have, kept resolvable.
///
///   3.0 shipped as an `aufx`. Taking a note port meant becoming an `aumf` --
/// Logic routes no MIDI to an effect -- and an AU's type is part of its
/// identity, so `aufx/SWrx/SSTx` and `aumf/SWrx/SSTx` are two components as far
/// as macOS is concerned. A session saved against the first would open without
/// the plugin.
///
///   So the old triple is declared here and clap-wrapper writes it into the
/// bundle as a second, hidden AudioComponents entry sharing this one's factory.
/// A host reopening an old session resolves it by full description and gets the
/// plugin; nothing enumerates it, so it is not listed twice.
///
/// \note One entry, and it is not a list that grows lightly: every identity
/// here is one more thing that has to keep working. \see doc/tech/midi-input.md
///
////////////////////////////////////////////////////////////////////////////////

constexpr char legacyAuTypeCode[]{"aufx"};
static_assert(sizeof(legacyAuTypeCode) == 5, "An AU type code is four characters.");

std::uint32_t auv2LegacyCount(clap_plugin_factory_auv2_legacy const *, std::uint32_t const index)
{
    return (index == 0) ? 1 : 0;
}

bool auv2LegacyIdentity(clap_plugin_factory_auv2_legacy const *, std::uint32_t const index,
                        std::uint32_t const n, clap_plugin_auv2_legacy_identity_t *const identity)
{
    if ((index != 0) || (n != 0))
        return false;

    // the subtype and the manufacturer never moved; only the type did
    std::strncpy(identity->au_type, legacyAuTypeCode, sizeof(identity->au_type));
    std::strncpy(identity->au_subt, auSubtypeCode, sizeof(identity->au_subt));
    std::strncpy(identity->au_manu, auManufacturerCode, sizeof(identity->au_manu));
    return true;
}

/// \note Returning no per-plugin information is the whole point of declaring
/// this one: the factory itself carries the vendor block that VST3 shows, and
/// without it clap-wrapper invents one from the CLAP descriptor. What a
/// `clap_plugin_info_as_vst3` would add is a **fixed** `componentId`, and this
/// plugin has never shipped a VST3 -- so there is no id to preserve, and
/// pinning one now would only freeze whatever the SHA-1 of the CLAP id happens
/// to be today. It becomes worth filling in the moment a VST3 is released:
/// run the validator, read the `cid` back, and hand it over with COMPONENT_ID.
/// That is the same shape surge-xt2 uses.
clap_plugin_info_as_vst3 const *vst3Info(clap_plugin_factory_as_vst3 const *, std::uint32_t)
{
    return nullptr;
}

constexpr clap_plugin_factory factory{pluginCount, pluginDescriptor, create};

constexpr clap_plugin_factory_as_auv2 auv2Factory{auManufacturerCode, SW_VENDOR, auv2Info};

constexpr clap_plugin_factory_auv2_legacy auv2LegacyFactory{auv2LegacyCount, auv2LegacyIdentity};

constexpr clap_plugin_factory_as_vst3 vst3Factory{SW_VENDOR, SW_VENDOR_URL, "", vst3Info, nullptr};
} // namespace

bool clapInit(char const *) { return true; }

void clapDeinit() {}

void const *getFactory(char const *const factoryID)
{
    if (std::strcmp(factoryID, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &factory;
    if (std::strcmp(factoryID, CLAP_PLUGIN_FACTORY_INFO_AUV2) == 0)
        return &auv2Factory;
    if (std::strcmp(factoryID, CLAP_PLUGIN_FACTORY_INFO_AUV2_LEGACY) == 0)
        return &auv2LegacyFactory;
    /// \note Both versions, deliberately: the struct grew a
    /// get_vst3_compatibility member at /1, and vst3.h asks a plugin that
    /// supports the newer one to answer the older query with the same struct.
    /// Ours has the member and it is null, which is what "no old class ids to
    /// replace" looks like.
    if ((std::strcmp(factoryID, CLAP_PLUGIN_FACTORY_INFO_VST3) == 0) ||
        (std::strcmp(factoryID, CLAP_PLUGIN_FACTORY_INFO_VST3_V1) == 0))
        return &vst3Factory;
    return nullptr;
}

} // namespace LE::SW::ClapFirst
