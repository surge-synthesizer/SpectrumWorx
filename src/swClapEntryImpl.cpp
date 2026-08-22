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

static_assert(sizeof(auManufacturerCode) == 5, "An AU manufacturer code is four characters.");
static_assert(sizeof(auSubtypeCode) == 5, "An AU subtype code is four characters.");

bool auv2Info(clap_plugin_factory_as_auv2 const *, std::uint32_t const index,
              clap_plugin_info_as_auv2_t *const info)
{
    if (index != 0)
        return false;

    info->au_type[0] = 0; // derived from the CLAP features
    std::strncpy(info->au_subt, auSubtypeCode, sizeof(info->au_subt));
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
