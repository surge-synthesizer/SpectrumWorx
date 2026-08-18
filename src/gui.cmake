# The editor, in layers, so that a layer can be built and looked at before the
# one above it compiles. Stage 6 has ~10 k lines of JUCE 2.1.2 to port and the
# harness (tools/show-ui) shows whatever is ready.
#
#   sw-gui-resources    skin bitmaps and fonts        <- builds
#   sw-gui-widgets      gui.{hpp,cpp}, the widget set <- builds
#   sw-gui              editor, modules, preset browser
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(sw-gui-resources STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/colourMap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/resources.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/theme.cpp

        # Every drawing the skin used to be a file of. One per shape, and none
        # of them a juce::Component: a widget calls into these and holds no
        # artwork. \see gui/painters/knobPainter.hpp for the arrangement.
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/painters/arrowPainter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/painters/backgroundPainter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/painters/buttonPainter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/painters/capsulePainter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/painters/editorKnobPainter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/painters/ejectPainter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/painters/framePainter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/painters/knobPainter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/painters/moduleKnobPainter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/painters/moduleStripPainter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/painters/panelPainter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/painters/sliderThumbPainter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/le/utility/assertionHandler.cpp
)

sw_force_include_odr_header(sw-gui-resources)

target_include_directories(sw-gui-resources PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# sw-juce rather than juce::juce_gui_basics -- Theme is a LookAndFeel_V2, so
# juce_graphics is not enough, and naming a module target here is what used to
# make this target compile JUCE. See libs/CMakeLists.txt.
target_link_libraries(sw-gui-resources
        PUBLIC sw-juce
        # assertionHandler.cpp prints a stack trace with the message.
        PRIVATE sw::assets sst-plugininfra
)

target_compile_definitions(sw-gui-resources PRIVATE LE_ENABLE_ASSERT_HANDLER)

################################################################################
# sw-gui-widgets -- the widget set: knobs, buttons, combo boxes, menus.
#
# The two JUCE 8 rewrites live here and are done: asynchronous menus and
# dialogs, and a PopupMenu that owns its items instead of reaching into JUCE's
# private state.
################################################################################

add_library(sw-gui-widgets STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/gui.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/preferences.cpp
)

# sw-io rather than sw-dsp: the editor opens preset files and lists the factory
# samples, and sw-io is what brings JUCE's file and audio-format modules with it.
target_link_libraries(sw-gui-widgets PUBLIC sw-gui-resources sw-io)

# Where the user's presets live -- ~/Documents/SpectrumWorx and its platform
# equivalents, XDG included. PRIVATE: gui.cpp answers with the fs::path
# sst-plugininfra handed it, so nothing above needs to know where it came from --
# and, as of 09.08.2026, nothing has to convert it either. See the note on
# rootPath() and tests/checkNoJuceFile.cmake.
#
# tinyxml with it, for preferences.cpp: sst-plugininfra's userdefaults.h parses
# the preferences file with it. Also PRIVATE, and preferences.hpp keeps the
# provider behind a pimpl, so the header does not arrive above this target
# either.
target_link_libraries(sw-gui-widgets PRIVATE sst-plugininfra sst-plugininfra::tinyxml)

# \note gui/gui.mm and "-framework Cocoa" stood here. The .mm held one function,
# initialiseMac(), which detached a throwaway NSThread to put Cocoa into
# multithreaded mode -- a Mac OS X-era requirement that Foundation has not needed
# for many releases, and JUCE has threads running long before an editor opens
# anyway. With it went the last Objective-C in the GUI layer, and the frameworks
# JUCE actually needs it links for itself.
sw_force_include_odr_header(sw-gui-widgets)

################################################################################
# sw-gui -- the module layer: a module's UI and its parameter controls.
#
# \note core/modules/moduleDSPAndGUI.cpp used to live here, because it was
# SW::Module's out-of-line half and every one of its virtuals existed to push a
# value into a widget. Those virtuals are gone and the file names no widget, so
# it is back in sw-dsp -- which is what lets a test that merely destroys a module
# link without JUCE. (The file's name is now a misnomer; renaming it is a
# fifteen-include diff and a separate change.)
################################################################################

add_library(sw-gui STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/about.cpp

        ${CMAKE_CURRENT_SOURCE_DIR}/gui/modules/moduleControl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/modules/moduleUI.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/modules/moduleWidgets.cpp

        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editor/auxiliaryComponents.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editor/editorHost.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editor/moduleMenuHolder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editor/presetLoading.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editor/spectrumWorxEditor.cpp

        ${CMAKE_CURRENT_SOURCE_DIR}/gui/preset_browser/presetBrowser.cpp
)

sw_force_include_odr_header(sw-gui)

target_link_libraries(sw-gui PUBLIC sw-gui-widgets)

# The date, time and commit the editor draws along its bottom edge, and that the
# About page's "copy info" link puts on the clipboard. PRIVATE: the strings are
# named by spectrumWorxEditor.cpp and about.cpp and by nothing above them.
# \see src/buildStamp.cmake.
target_link_libraries(sw-gui PRIVATE sw-build-stamp)

# The release the tree was *configured* as -- version, branch, commit, toolchain
# -- which is the other half of what the About page shows and what a bug report
# needs. \see gui/about.cpp, and the note in configuration/buildStamp.hpp on why
# both exist.
target_link_libraries(sw-gui PRIVATE sst-plugininfra::version_information)
