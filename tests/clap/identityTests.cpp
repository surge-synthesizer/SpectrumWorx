////////////////////////////////////////////////////////////////////////////////
///
/// \file identityTests.cpp
/// -----------------------
///
///   What the plugin calls itself, pinned.
///
///   These strings are not decoration. `SW_CLAP_ID` is the CLAP id, the stem of
/// the `.clap`/`.vst3`/`.component` bundle identifiers, and -- through a SHA-1
/// in clap-wrapper's `wrapasvst3_entry.cpp` -- the VST3 class id, so changing it
/// after a release orphans every saved VST3 session. The AU manufacturer and
/// subtype are what `auval` and Logic key a component on, and moving either one
/// makes existing sessions lose their plugin.
///
///   The descriptor test in pluginTests.cpp checks the id is *non-empty*, which
/// is the right check for "the factory is wired up" and no check at all for
/// this. The literals below are spelled out rather than taken from the build's
/// own macros deliberately: `SW_CLAP_ID` and friends are PRIVATE to sw-impl, and
/// a test that read them could only ever agree with itself.
///
/// \note The one thing here that is *not* frozen is the vendor. If the Surge
/// Synth Team's display name or URL ever changes, change it in
/// CMakeLists.txt and here; nothing downstream keys on it.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "swClapEntryImpl.hpp"

#include <clap/clap.h>
#include <clapwrapper/auv2.h>
#include <clapwrapper/vst3.h>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

constexpr char clapID[]{"org.surge-synth-team.spectrumworx"};
constexpr char vendor[]{"Surge Synth Team"};
constexpr char vendorURL[]{"https://surge-synth-team.org"};

constexpr char auManufacturerCode[]{"SSTx"};
/// \note `SpWx` was SpectrumWorx's from 2010 and this pinned it; the shipping
/// code is `SWrx` (swClapEntryImpl.cpp) and has been since `efd7037`. Nothing
/// has been released under either, so the subtype is still free to change --
/// but once one is, changing it orphans every AU instance in a saved session.
constexpr char auSubtypeCode[]{"SWrx"};

/// \brief The least a host can be: no extensions, no-op requests.
clap_host const &minimalHost()
{
    static clap_host host{CLAP_VERSION,
                          nullptr,
                          "sw-tests",
                          "SpectrumWorx",
                          "",
                          "0",
                          [](clap_host const *, char const *) -> void const * { return nullptr; },
                          [](clap_host const *) {},
                          [](clap_host const *) {},
                          [](clap_host const *) {}};
    return host;
}

/// \brief RAII around the entry point, whose init/deinit are refcounted.
class Entry
{
  public:
    Entry() { REQUIRE(LE::SW::ClapFirst::clapInit("sw-tests")); }
    ~Entry() { LE::SW::ClapFirst::clapDeinit(); }

    Entry(Entry const &) = delete; // makes non-copyable
    Entry &operator=(Entry const &) = delete;
}; // class Entry

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("The CLAP descriptor's identity is what ships", "[clap][identity]")
{
    Entry const entry;

    auto const *const pFactory(static_cast<clap_plugin_factory const *>(
        LE::SW::ClapFirst::getFactory(CLAP_PLUGIN_FACTORY_ID)));
    REQUIRE(pFactory != nullptr);

    auto const *const pDescriptor(pFactory->get_plugin_descriptor(pFactory, 0));
    REQUIRE(pDescriptor != nullptr);

    CHECK(std::strcmp(pDescriptor->id, clapID) == 0);
    CHECK(std::strcmp(pDescriptor->name, "SpectrumWorx") == 0);
    CHECK(std::strcmp(pDescriptor->vendor, vendor) == 0);
    CHECK(std::strcmp(pDescriptor->url, vendorURL) == 0);

    /// \note create_plugin() matches on the id, so a descriptor and a factory
    /// that disagree would be a plugin no host could instantiate.
    auto const *const pPlugin(pFactory->create_plugin(pFactory, &minimalHost(), clapID));
    REQUIRE(pPlugin != nullptr);
    pPlugin->destroy(pPlugin);

    // And the identity this replaced is not still answering to its old name.
    CHECK(pFactory->create_plugin(pFactory, &minimalHost(), "com.littleendian.spectrumworx") ==
          nullptr);
}

TEST_CASE("The AUv2 sub-factory names the Surge Synth Team", "[clap][identity]")
{
    Entry const entry;

    auto const *const pFactory(static_cast<clap_plugin_factory_as_auv2 const *>(
        LE::SW::ClapFirst::getFactory(CLAP_PLUGIN_FACTORY_INFO_AUV2)));
    REQUIRE(pFactory != nullptr);

    CHECK(std::strcmp(pFactory->manufacturer_code, auManufacturerCode) == 0);
    CHECK(std::strcmp(pFactory->manufacturer_name, vendor) == 0);

    clap_plugin_info_as_auv2_t info{};
    REQUIRE(pFactory->get_auv2_info != nullptr);
    REQUIRE(pFactory->get_auv2_info(pFactory, 0, &info));

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note `aumf` -- a music effect -- and stated rather than left for the
    /// wrapper to derive. Derivation reads `features[0]`, which is
    /// audio-effect, and would give `aufx`: an AU that takes audio and no
    /// notes, which Logic routes no MIDI to. The note port would be declared
    /// and unreachable in the one host it matters most for.
    ///
    /// \note The type is part of an AU's **identity**, so this is not a
    /// setting. `aufx/SWrx/SSTx` and `aumf/SWrx/SSTx` are two different
    /// components: a session that referenced the old one does not find the new
    /// one. \see doc/tech/midi-input.md
    ///
    ////////////////////////////////////////////////////////////////////////////
    CHECK(std::strcmp(info.au_type, "aumf") == 0);
    CHECK(std::strcmp(info.au_subt, auSubtypeCode) == 0);

    // One plugin, so there is no index 1 to describe.
    CHECK(!pFactory->get_auv2_info(pFactory, 1, &info));
}

TEST_CASE("The VST3 sub-factory is offered at both versions", "[clap][identity]")
{
    Entry const entry;

    /// \note vst3.h asks a plugin that supports the /1 factory to answer the
    /// unversioned query with the same struct, so the two must be the same
    /// pointer rather than merely both non-null.
    auto const *const pFactory(static_cast<clap_plugin_factory_as_vst3 const *>(
        LE::SW::ClapFirst::getFactory(CLAP_PLUGIN_FACTORY_INFO_VST3)));
    REQUIRE(pFactory != nullptr);
    CHECK(LE::SW::ClapFirst::getFactory(CLAP_PLUGIN_FACTORY_INFO_VST3_V1) == pFactory);

    CHECK(std::strcmp(pFactory->vendor, vendor) == 0);
    CHECK(std::strcmp(pFactory->vendor_url, vendorURL) == 0);

    /// \note Deliberately no per-plugin information: no VST3 of this plugin has
    /// ever shipped, so there is no class id to preserve and pinning one now
    /// would only freeze today's hash of the CLAP id. The day a VST3 is
    /// released, this returns a componentId and this check inverts.
    REQUIRE(pFactory->get_vst3_info != nullptr);
    CHECK(pFactory->get_vst3_info(pFactory, 0) == nullptr);

    // No compatibility block either: there is nothing older to replace.
    CHECK(pFactory->get_vst3_compatibility == nullptr);
}

TEST_CASE("An unknown factory is declined", "[clap][identity]")
{
    Entry const entry;

    CHECK(LE::SW::ClapFirst::getFactory("clap.plugin-factory.no-such-thing") == nullptr);
}
