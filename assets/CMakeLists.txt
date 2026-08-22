# sw-assets — everything the plugin ships, compiled into the binary.
#
# In 2016 these were installed to disk and found through a `SpectrumWorx.paths`
# file that the installer wrote and the plugin mmapped at startup: no installer,
# no GUI and no factory presets. Embedding them removes that dependency
# entirely, and with it GUI::initializePaths(), mapPathsFile(), rootPath() and
# resourcesPath().
#
# CMakeRC rather than juce_add_binary_data, because add_clap_juce_shim() sets
# JUCE_MODULES_ONLY and none of the juce_add_* helpers exist under it.
#
# \note One library rooted at assets/, not one per kind: what the consumers want
# is a path ("skin/01.svg", "presets/Echoes/LE Boiler.swp", "samples/Carrier.mp3"),
# and the skin and the presets are wanted by the same binary in every
# configuration that has either.
#
# SPDX-License-Identifier: GPL-3.0-or-later

include(CmakeRC)

# CONFIGURE_DEPENDS so that adding a skin file or a factory preset does not also
# mean editing this file. The sets are small and change rarely; the reconfigure
# cost is noise.
#
# \note *.svg and not *.png. The skin was redrawn as vectors a few files at a
# time and both forms were embedded while that was happening; there is no bitmap
# left, and resources.cpp no longer knows how to decode one.
file(GLOB SW_SKIN_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/skin/*.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/skin/*.ttf"
)

# \note And the logo, by name rather than by glob, because it is the one file
# under assets/ that is on both sides of the line above: it is the mark the
# installers cut their icons from *and* the mark the editor draws down its left
# edge. Named here so there is one of it -- a copy in skin/ would be a second
# logo free to drift from this one.
list(APPEND SW_SKIN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/spectrumworx_logo.svg")
list(APPEND SW_SKIN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/spectrumworx_logo_full.svg")

file(GLOB_RECURSE SW_PRESET_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/presets/*.swp"
)

# The factory samples for the external-audio side channel: 1.4 MB of MP3, in the
# binary rather than installed beside it. In 2016 the installer wrote them to
# <install>/Samples and the loader looked there by file name when a preset named
# one it could not find; embedding keeps that fallback working with no installer
# and no path file, exactly as it does for the presets.
#
# The size is the whole of the argument against, and 1.4 MB against a ~20 MB
# binary is not an argument. Revisit if the library grows, at which point the
# answer is a downloadable expansion, not a loose directory nothing can find.
file(GLOB SW_SAMPLE_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/samples/*.mp3"
        "${CMAKE_CURRENT_SOURCE_DIR}/samples/*.wav"
)

list(LENGTH SW_SKIN_FILES SW_SKIN_FILE_COUNT)
if (SW_SKIN_FILE_COUNT EQUAL 0)
    message(FATAL_ERROR "assets/skin is empty -- did the submodule/checkout complete?")
endif()

list(LENGTH SW_PRESET_FILES SW_PRESET_FILE_COUNT)
if (SW_PRESET_FILE_COUNT EQUAL 0)
    message(FATAL_ERROR "assets/presets is empty -- did the submodule/checkout complete?")
endif()

list(LENGTH SW_SAMPLE_FILES SW_SAMPLE_FILE_COUNT)
if (SW_SAMPLE_FILE_COUNT EQUAL 0)
    message(FATAL_ERROR "assets/samples is empty -- did the submodule/checkout complete?")
endif()

message(STATUS "SpectrumWorx assets: ${SW_SKIN_FILE_COUNT} skin files, "
        "${SW_PRESET_FILE_COUNT} factory presets, ${SW_SAMPLE_FILE_COUNT} factory samples")

cmrc_add_resource_library(sw-assets
        ALIAS sw::assets
        NAMESPACE swAssets
        WHENCE "${CMAKE_CURRENT_SOURCE_DIR}"
        ${SW_SKIN_FILES}
        ${SW_PRESET_FILES}
        ${SW_SAMPLE_FILES}
)
# cmrc_add_resource_library makes a static library of generated .cpp files; they
# are not ours and have nothing to say about our warnings.
set_target_properties(sw-assets PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        UNITY_BUILD FALSE
        FOLDER assets
)
