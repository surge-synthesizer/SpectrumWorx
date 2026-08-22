# Checks that leConfigurationAndODRHeader.h is force-included into our
# translation units and into no others.
#
# Both directions matter, and they fail differently:
#
#   - Into someone else's, and their build breaks somewhere unrelated. Stage 7.5
#     removed the PUBLIC compile option that did this to 81 JUCE translation
#     units; it had already cost five separate Windows failures, none of them in
#     our code and none naming the cause.
#   - Out of one of ours, and nothing breaks at all until it does. The header
#     decides whether NDEBUG survives, and NDEBUG decides whether ModuleNode has
#     a virtual -- so one file quietly missing it is a layout disagreement about
#     every module object, which is not a thing a test is going to catch.
#
# Run: cmake -D BUILD_DIR=<dir> -D SOURCE_DIR=<dir> -P checkODRHeaderScope.cmake
#
# SPDX-License-Identifier: GPL-3.0-or-later

# See the note in checkNoJuceInDSP.cmake: `cmake -P` sets no policies, and the
# `IN_LIST` below needs CMP0057.
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

set(ourRoots "${SOURCE_DIR}/src" "${SOURCE_DIR}/tests" "${SOURCE_DIR}/tools")

# The one source of ours that does not get it, and why. swClapEntry.cpp includes
# <clap/clap.h> and swClapEntryImpl.hpp, and the latter is written to declare
# three functions and nothing else. It is compiled into the clap-wrapper targets,
# where reaching it per source would mean reaching into targets made by
# make_clapfirst_plugins() -- so the cheaper guarantee is that it needs nothing.
set(exempt "${SOURCE_DIR}/src/clap-first/swClapEntry.cpp")

set(trespassers "")
set(deprived "")
set(ourCount 0)

math(EXPR lastEntry "${entryCount} - 1")
foreach (index RANGE ${lastEntry})
    string(JSON entry GET "${commandsJson}" ${index})
    string(JSON file GET "${entry}" "file")
    string(JSON command GET "${entry}" "command")

    set(isOurs FALSE)
    foreach (root IN LISTS ourRoots)
        # String prefix rather than a regex: a path is not a pattern, and the
        # source directory may well contain regex metacharacters.
        string(FIND "${file}" "${root}/" rootPosition)
        if (rootPosition EQUAL 0)
            set(isOurs TRUE)
            break()
        endif ()
    endforeach ()

    string(FIND "${command}" "leConfigurationAndODRHeader.h" headerPosition)
    if (headerPosition EQUAL -1)
        set(hasHeader FALSE)
    else ()
        set(hasHeader TRUE)
    endif ()

    if (isOurs AND hasHeader)
        math(EXPR ourCount "${ourCount} + 1")
    elseif (isOurs AND NOT "${file}" IN_LIST exempt)
        list(APPEND deprived "${file}")
    elseif (hasHeader AND NOT isOurs)
        list(APPEND trespassers "${file}")
    endif ()
endforeach ()

function(reportFailure heading files remedy)
    list(REMOVE_DUPLICATES files)
    list(LENGTH files count)
    string(REPLACE ";" "\n    " fileList "${files}")
    message(FATAL_ERROR "${count} translation unit(s) ${heading}:\n    ${fileList}\n\n${remedy}")
endfunction()

if (trespassers)
    reportFailure("outside src/, tests/ and tools/ are force-included with leConfigurationAndODRHeader.h"
            "${trespassers}"
            "It is our configuration header, not theirs. Apply it with \
sw_force_include_odr_header(<target>), which filters by path -- a target-wide compile option \
cannot express \"our sources\", because linking a JUCE module adds that module's own sources to \
the consuming target.")
endif ()

if (deprived)
    reportFailure("of ours are compiled without leConfigurationAndODRHeader.h"
            "${deprived}"
            "Their target needs a sw_force_include_odr_header(<target>) call, placed after the \
last target_sources() for it. If the file genuinely depends on nothing in the header, add it to \
`exempt` in this script and say why.")
endif ()

# A silent pass is also what a typo in the search string looks like.
if (ourCount EQUAL 0)
    message(FATAL_ERROR "No translation unit at all is force-included with the ODR header. "
                        "Either this build has nothing of ours in it, or this check is looking "
                        "for the wrong thing.")
endif ()

message(STATUS "ODR header force-included into ${ourCount} of our translation units, "
               "and no others.")
