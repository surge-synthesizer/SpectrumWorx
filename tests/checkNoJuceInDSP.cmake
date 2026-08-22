# Checks that nothing compiled into sw-dsp is given a JUCE include path.
#
#   doc/tech/threading_model.md §6: DSP-only tests and preset loading tests link
# without JUCE at all, because JUCE lives in a set of TUs well above the engine.
# This is what keeps it that way.
#
#   It has to be a build check rather than a code one, and it has to look at the
# *compile* lines rather than the link line. A single `target_link_libraries(
# sw-dsp PUBLIC juce::juce_core)` puts JUCE's headers in front of every engine
# source at once, and nothing about the result looks wrong until somebody
# includes one -- at which point the layering is gone and the fix is no longer a
# one-line revert. Six months of that is how the 2016 build came to have
# `juce_gui_basics` under its FFT.
#
#   Include paths rather than `#include` lines for the same reason: what makes
# the boundary real is that a JUCE header *cannot* be reached from here, not that
# nobody has reached for one yet.
#
# Run: cmake -D BUILD_DIR=<dir> -D SOURCE_DIR=<dir> -P checkNoJuceInDSP.cmake
#
# SPDX-License-Identifier: GPL-3.0-or-later

# A script run with `cmake -P` starts with no policies set, whatever the project
# that registered it asked for -- and `IN_LIST` below is CMP0057, which is an
# error rather than an operator until something sets it. CMake 4 has the policy
# removed and always behaves NEW, so this passed on a 4.x developer machine and
# failed on the 3.x that CI ships. The version is the project's own.
cmake_minimum_required(VERSION 3.28)

set(compileCommands "${BUILD_DIR}/compile_commands.json")

if (NOT EXISTS "${compileCommands}")
    # The Visual Studio and Xcode generators do not write one. Skipping is
    # honest: this is a Ninja/Makefile check, and CI runs those.
    message(STATUS "No compile_commands.json in ${BUILD_DIR} -- skipping.")
    return()
endif ()

file(READ "${compileCommands}" commandsJson)
string(JSON entryCount LENGTH "${commandsJson}")

# What sw-dsp is made of, by path. Reading the target's own source list would be
# better and is not available to a script: compile_commands.json records the
# file and the command, not which target asked for it. These two roots are
# exactly sw-dsp's sources minus the handful of files that other targets also
# compile -- see `alsoCompiledElsewhere` below.
set(dspRoots "${SOURCE_DIR}/src/le" "${SOURCE_DIR}/src/core")

# ...minus the one subtree under them that is not sw-dsp's. `core/host_interop`
# is what a plugin *format* talks to and is compiled into sw-impl, deliberately
# so -- src/CMakeLists.txt says why.
set(notDSPRoots "${SOURCE_DIR}/src/core/host_interop")

# Files under those roots that are *also* compiled into a JUCE-linking target,
# and therefore legitimately appear with JUCE include paths on some of their
# compile lines. Each is one file compiled twice, not sw-dsp reaching for JUCE.
set(alsoCompiledElsewhere
        "${SOURCE_DIR}/src/le/utility/assertionHandler.cpp" # sw-gui-resources
        "${SOURCE_DIR}/src/le/spectrumworx/presetFile.cpp"  # sw-io
)

set(offenders "")
set(checkedCount 0)

math(EXPR lastEntry "${entryCount} - 1")
foreach (index RANGE ${lastEntry})
    string(JSON entry GET "${commandsJson}" ${index})
    string(JSON file GET "${entry}" "file")
    string(JSON command GET "${entry}" "command")

    if ("${file}" IN_LIST alsoCompiledElsewhere)
        continue()
    endif ()

    set(isDSP FALSE)
    foreach (root IN LISTS dspRoots)
        # String prefix rather than a regex: a path is not a pattern, and the
        # source directory may well contain regex metacharacters.
        string(FIND "${file}" "${root}/" rootPosition)
        if (rootPosition EQUAL 0)
            set(isDSP TRUE)
            break()
        endif ()
    endforeach ()

    foreach (root IN LISTS notDSPRoots)
        string(FIND "${file}" "${root}/" rootPosition)
        if (rootPosition EQUAL 0)
            set(isDSP FALSE)
            break()
        endif ()
    endforeach ()

    if (NOT isDSP)
        continue()
    endif ()

    math(EXPR checkedCount "${checkedCount} + 1")

    # `-DJUCE_MODULE_AVAILABLE_<module>`, which JUCE's own CMake puts on every
    # consumer of every module it exports. Chosen over the include path -- which
    # is a bare `-I<somewhere>/JUCE/modules` and would also match a checkout in a
    # directory named for it -- and over `#include <juce_...>`, because the point
    # is that a JUCE header cannot be *reached* from here.
    string(FIND "${command}" "-DJUCE_MODULE_AVAILABLE_" jucePosition)
    if (NOT jucePosition EQUAL -1)
        list(APPEND offenders "${file}")
    endif ()
endforeach ()

if (offenders)
    list(REMOVE_DUPLICATES offenders)
    list(LENGTH offenders count)
    string(REPLACE ";" "\n    " fileList "${offenders}")
    message(FATAL_ERROR
            "${count} engine translation unit(s) are compiled with JUCE on the include path:\
\n    ${fileList}\n\n\
Something has put a JUCE module on sw-dsp's link line, or added a source to sw-dsp that \
belongs above it. The engine is the layer a DSP test links on its own; anything that needs \
juce::String or juce::AudioFormatManager goes in sw-io or higher. See \
doc/tech/threading_model.md and the note above sw-io in src/dsp.cmake.\n\n\
Paths are not among them: sw-dsp opens preset files itself, over fs::path and <fstream> -- \
see le/spectrumworx/presetStorage.cpp and tests/checkNoJuceFile.cmake.")
endif ()

# A silent pass is also what a typo in the search string looks like.
if (checkedCount EQUAL 0)
    message(FATAL_ERROR "No engine translation unit was checked at all. Either this build has "
                        "no sw-dsp in it, or `dspRoots` in this script no longer names one.")
endif ()

message(STATUS "${checkedCount} engine translation units, none of them with JUCE on the include path.")
