# One call assembles the CLAP, the VST3, the AUv2 and the standalone out of
# sw-impl plus swClapEntry.cpp.
#
# Deliberately no project() call here: it would reset PROJECT_VERSION, and
# clap-wrapper reads that for the bundle versions.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set(SW_CLAPFIRST_TARGET spectrumworx_clapfirst)

set(SW_FORMATS CLAP VST3)
if (APPLE)
    list(APPEND SW_FORMATS AUV2)
endif()

message(STATUS "SpectrumWorx plugin formats: ${SW_FORMATS}")

add_compile_definitions(CLAP_WRAPPER_LOGLEVEL=0)

make_clapfirst_plugins(
        TARGET_NAME ${SW_CLAPFIRST_TARGET}
        IMPL_TARGET sw-impl

        OUTPUT_NAME "${PRODUCT_NAME}"

        ENTRY_SOURCE swClapEntry.cpp

        BUNDLE_IDENTIFIER "${SW_CLAP_ID}"
        BUNDLE_VERSION ${PROJECT_VERSION}

        COPY_AFTER_BUILD ${SW_COPY_AFTER_BUILD}

        PLUGIN_FORMATS ${SW_FORMATS}

        WINDOWS_FOLDER_VST3 TRUE

        ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/sw_assets

        # target-postfix, output name, clap id
        STANDALONE_CONFIGURATIONS
        standalone "${PRODUCT_NAME}" "${SW_CLAP_ID}"

        # Both generated from assets/LOGO.png by scripts/make_icons.sh, which
        # is macOS-only -- so they are committed rather than built, and the
        # Windows leg gets an .ico it could not have produced.
        STANDALONE_MACOS_ICON "${CMAKE_SOURCE_DIR}/assets/installer/SpectrumWorxIcon.icns"
        STANDALONE_WINDOWS_ICON "${CMAKE_SOURCE_DIR}/assets/installer/SpectrumWorxIcon.ico"
)

# Read back by the installer; clap-wrapper does not set it.
set_target_properties(${SW_CLAPFIRST_TARGET}_all PROPERTIES
        ARTEFACT_DIRECTORY ${CMAKE_BINARY_DIR}/sw_assets)

# \note The standalone's macOS bundle identifier is *not* ours, until the
# clap-wrapper fix for it lands upstream. The standalone is the one wrapper that
# takes a BUNDLE_IDENTIFIER argument and ignores it: wrap_standalone.cmake parses
# it, nothing reads it, and its Info.plist.in hardcodes
# "${MACOSX_BUNDLE_BUNDLE_NAME}.standalone" where every sibling format sets
# MACOSX_BUNDLE_GUI_IDENTIFIER from the argument. Nothing here can override that
# one key, so the fix belongs upstream rather than in a fork of their template;
# tech_debt.md carries the patch and what it measures.

# Four -Wdeprecated-declarations from std::wstring_convert, deprecated in C++17
# and removed in C++26, in the VST3 SDK's own string conversion. Not ours and not
# editable: the SDK is a CPM fetch into the build tree, wiped on reconfigure. The
# real fix belongs upstream in the SDK; this keeps four warnings a Linux build log
# has to be read past from being mistaken for ours.
#
# Guarded on the target existing, because SW_FORMATS decides whether the VST3
# wrapper is built at all.
if (TARGET base-sdk-vst3 AND NOT MSVC)
    target_compile_options(base-sdk-vst3 PRIVATE -Wno-deprecated-declarations)
endif ()
