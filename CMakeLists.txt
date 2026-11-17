# SpectrumWorx — top level build
#
# Copyright (c) 2010 - 2016. Little Endian Ltd.
# Copyright (c) 2026 the SpectrumWorx contributors.
# SPDX-License-Identifier: GPL-3.0-or-later

# 3.28 because clap-wrapper sets policy CMP0149, which is unknown before 3.27.
cmake_minimum_required(VERSION 3.28)

cmake_policy(SET CMP0091 NEW) # MSVC runtime via CMAKE_MSVC_RUNTIME_LIBRARY
cmake_policy(SET CMP0063 NEW) # visibility presets honoured for all target types
cmake_policy(SET CMP0141 NEW) # MSVC debug info via CMAKE_MSVC_DEBUG_INFORMATION_FORMAT

# Must precede project() — project() runs the compiler checks that consume them.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Debug info in the object files (/Z7) rather than a per-target program database
# (/Zi). A .pdb is one file that every compiler invocation for that target must
# write to, so a target with enough sources to be worth compiling in parallel
# fails: "cannot open program database ...; if multiple CL.EXE write to the same
# .PDB file, please use /FS". /FS is the documented remedy and serialises those
# writes; /Z7 removes the shared file, and with it the thing to contend over.
# The linker still produces a program database for the binary, so debugging is
# unaffected -- what changes is that the object files carry the information into
# it instead of the compiler writing it there directly.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15 CACHE STRING "Minimum macOS version")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(SpectrumWorx VERSION 3.0.0 LANGUAGES C CXX)

set(PRODUCT_NAME "SpectrumWorx")

# The plugin's identity, in one place. These reach the C++ as compile
# definitions (src/CMakeLists.txt) and the bundle identifiers through
# make_clapfirst_plugins(), so nothing needs to repeat a string.
#
# SW_CLAP_ID is load bearing beyond the CLAP: clap-wrapper derives the VST3
# class id from it with a SHA-1 (wrapasvst3_entry.cpp), unless the plugin
# declares a componentId of its own. So changing it after a release would
# orphan every saved VST3 session -- which is why it was changed now, before
# anything shipped, rather than later.
set(SW_CLAP_ID "org.surge-synth-team.spectrumworx")
set(SW_VENDOR "Surge Synth Team")
set(SW_VENDOR_URL "https://surge-synth-team.org")

# The JUCE shim compiles .mm. Enable ObjC here, not in a subdirectory — CMake
# does not populate CMAKE_OBJCXX_COMPILE_OBJECT for subdirectory-scoped
# enable_language() and generation fails.
if (APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
    set(CMAKE_OBJC_VISIBILITY_PRESET hidden)
    set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)

    # AppleClang 21's libc++ #warns below macOS 11, which -Werror would make fatal
    if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
            AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 21)
        string(APPEND CMAKE_CXX_FLAGS " -Wno-error=#warnings")
        string(APPEND CMAKE_OBJCXX_FLAGS " -Wno-error=#warnings")
    endif ()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC reports __cplusplus as 199711L unless told not to -- a compatibility
# default kept since 2017, and one that silently defeats every `__cplusplus >=`
# test in this tree and its dependencies however new the compiler is. Asking for
# C++20 above and then lying about it in the macro is the worst of both.
#
# Directory scope rather than per target: the dependencies are compiled here too,
# and a project where the macro means one thing in our sources and another in
# JUCE's is harder to reason about than one where it is simply right.
#
# Note the absence of $<COMPILE_LANGUAGE:...>. The Visual Studio generator does
# not apply it to compile options the way the Ninja and Makefile generators do --
# the same limitation that let the C++-only force-include reach JUCE's C sources
# (see src/le/build/leConfigurationAndODRHeader.h). A flag guarded that way may
# land on everything or on nothing, and which it was is not something to discover
# from a missing conformance mode. CMAKE_CXX_FLAGS is per language by
# construction, and /FS is valid in both languages, so neither needs one.
#
# /utf-8 sets both the source and the execution charset. Without it MSVC reads a
# BOM-less source in the machine's ANSI codepage and re-encodes narrow literals
# into it, so a string with a diacritic in it means one thing on the developer's
# machine and another on the build agent -- while every other compiler here has
# been UTF-8 in and UTF-8 out all along. JUCE reads `char const *` as UTF-8, and
# gui/about.cpp has six proper names that need it.
if (MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /Zc:__cplusplus")
    string(APPEND CMAKE_CXX_FLAGS " /utf-8")
    string(APPEND CMAKE_C_FLAGS " /utf-8")
    add_compile_options(/FS) # belt and braces beside /Z7 above
endif ()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
# Not CACHE: without FORCE that loses to a -DBUILD_SHARED_LIBS=ON on the
# configure line, and a shared pffft has no exported symbols (#186).
set(BUILD_SHARED_LIBS OFF)

# CI always passes one; a bare `cmake -B build` otherwise gets no optimisation
# flags at all on single-config generators.
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Here rather than in src/, so that tests/ and tools/ -- siblings, not
# subdirectories -- inherit sw_force_include_odr_header() too. It carries the
# warning baseline and -Werror as well, for the same reason and by the same
# per-source path filter: our standards, our sources.
include(sw-our-sources)

# Before add_subdirectory(libs), and before any target exists: a sanitizer has to
# be on every translation unit and on the link line, dependencies included.
include(sw-sanitizers)

option(SW_COPY_AFTER_BUILD "Install the built plugins into the user plugin folders" OFF)
option(SW_BUILD_TESTS "Build the test binaries" ON)
option(SW_BUILD_TOOLS "Build the developer tools (sw-show-ui)" ON)

# \note The bundles, not the plugin: sw-impl is the plugin and is always built.
# What this switches off is clap-wrapper's per-format wrapping, which fetches the
# VST3 and AudioUnit SDKs over the network at *configure* time. A sanitizer build
# wants the test binaries and nothing else, and a machine with no network cannot
# configure one otherwise. See doc/tech/threading_model.md §8.
option(SW_BUILD_PLUGIN_BUNDLES "Build the .clap/.vst3/.component bundles" ON)

# The .clap needs no SDK; the other formats and the standalone CPM-fetch 54MB of
# them at configure time. CI wants the bundle a validator can load and nothing
# else. \see src/clap-first/CMakeLists.txt
option(SW_BUILD_CLAP_ONLY "Of the bundles, build only the .clap" OFF)

if (SW_BUILD_TESTS)
    enable_testing()
endif()

# Before add_subdirectory(libs): sst-plugininfra configure_file()s its version
# source from these variables, silently emitting empty strings if they are unset.
include(${CMAKE_SOURCE_DIR}/libs/sst/sst-plugininfra/cmake/git-version-functions.cmake)
version_from_versionfile_or_git()
message(STATUS "SpectrumWorx version: ${GIT_IMPLIED_DISPLAY_VERSION} (${GIT_BRANCH}@${GIT_COMMIT_HASH})")

set(JUCE_PATH "${CMAKE_SOURCE_DIR}/libs/JUCE")

add_subdirectory(libs)
add_subdirectory(assets)
add_subdirectory(src)

if (SW_BUILD_PLUGIN_BUNDLES)
    add_subdirectory(src/clap-first)
endif()

# The installer packages every format, so it exists only when every format does.
# `cmake --build <dir> --target spectrumworx-installer` is what CI runs and
# what a release is cut with; it is not part of `all`.
if (SW_BUILD_PLUGIN_BUNDLES AND NOT SW_BUILD_CLAP_ONLY)
    include(basic_installer_clapfirst)
    set(SW_EXTRA_ZIP_NAME "" CACHE STRING "Appended to the installer names, e.g. -aarch64")
    add_clapfirst_installer(
            INSTALLER_TARGET spectrumworx-installer
            ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/sw_assets
            INSTALLER_PREFIX "spectrumworx"
            EXTRA_ZIP_NAME "${SW_EXTRA_ZIP_NAME}"
            PRODUCT_NAME "${PRODUCT_NAME}"
            TARGETS
                spectrumworx_clapfirst_clap
                spectrumworx_clapfirst_vst3
                spectrumworx_clapfirst_auv2
                spectrumworx_clapfirst_standalone
    )
endif()

if (SW_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if (SW_BUILD_TESTS)
    include(Catch)
    add_subdirectory(tests)
endif()
