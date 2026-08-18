# Checks that nothing under src/ names a colour except the one file whose job it
# is.
#
#   A skin painted in code has to write its palette down somewhere, and the
# tempting somewhere is a constant beside each drawing. That is how this tree
# came to spell its accent blue four ways -- 0x13B5EA, 0x13B7EA, #13b4e9,
# #12b4ea -- none of them chosen and no two of them three parts in 255 apart.
# One of those was a knob, one a module strip and one a button rim, so nobody
# looking at any single file could see it.
#
#   src/gui/colourMap.hpp is the palette. Everything else asks it by name.
#
#   The fix for anything reported here is to add an enumerator to ColourMap for
# whatever the colour is *for* and call getColour(). Deriving from one that is
# already there is fine and does not match: `.withAlpha()`, `.brighter()` and
# friends say what they do. A gradient that fades a colour out wants
# `getColour( X ).withAlpha( 0.0f )` rather than ColourMap::Transparent -- a
# gradient interpolates the channels as well as the alpha.
#
# Run: cmake -D SOURCE_DIR=<dir> -P checkNoColourLiterals.cmake
#
# \note A source scan, like checkNoJuceFile.cmake beside it: what is checked is
# what the text says, so this needs no build directory and runs under every
# generator. The line-peeling and the comment skipping are that file's, for the
# reasons it gives at length.
#
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.28)

# The palette itself, and nothing else.
set(allowed
        "src/gui/colourMap.hpp"
        "src/gui/colourMap.cpp"
)

file(GLOB_RECURSE sources
        "${SOURCE_DIR}/src/*.cpp"
        "${SOURCE_DIR}/src/*.hpp"
        "${SOURCE_DIR}/src/*.inl"
)

set(offenderReport "")
set(offenderCount 0)
set(checkedCount 0)

foreach (sourceFile IN LISTS sources)
    file(RELATIVE_PATH relativePath "${SOURCE_DIR}" "${sourceFile}")
    if (relativePath IN_LIST allowed)
        continue()
    endif ()

    file(READ "${sourceFile}" contents)

    # Most of the tree never mentions either, and peeling a whole file a line at
    # a time is the expensive part of this check.
    string(FIND "${contents}" "juce::Colour" position)
    if (position EQUAL -1)
        continue()
    endif ()

    math(EXPR checkedCount "${checkedCount} + 1")

    set(lineNumber 0)
    while (NOT contents STREQUAL "")
        string(FIND "${contents}" "\n" newlinePosition)
        if (newlinePosition EQUAL -1)
            set(line "${contents}")
            set(contents "")
        else ()
            string(SUBSTRING "${contents}" 0 ${newlinePosition} line)
            math(EXPR afterNewline "${newlinePosition} + 1")
            string(SUBSTRING "${contents}" ${afterNewline} -1 contents)
        endif ()
        math(EXPR lineNumber "${lineNumber} + 1")

        # Prose is skipped: this tree explains what used to stand where, so a
        # good many notes have to quote the colour they replaced.
        string(REGEX REPLACE "^[ \t]+" "" trimmed "${line}")
        if (trimmed MATCHES "^(//|\\*|/\\*)")
            continue()
        endif ()

        # \note `juce::Colour` as a *type* is not the point and does not match:
        # a member, a parameter and a return value all name it and all hold
        # whatever the map handed over. What is being kept out is the two ways
        # to conjure one from nothing -- a constructor call and JUCE's own table
        # of named colours.
        if (line MATCHES "juce::Colour[ \t]*\\(" OR line MATCHES "juce::Colours::")
            string(APPEND offenderReport "\n    ${relativePath}:${lineNumber}: ${trimmed}")
            math(EXPR offenderCount "${offenderCount} + 1")
        endif ()
    endwhile ()
endforeach ()

if (offenderCount GREATER 0)
    string(REPLACE ";" "\n    " allowedList "${allowed}")
    message(FATAL_ERROR
            "${offenderCount} colour(s) named outside the palette:\
${offenderReport}\n\n\
The palette is src/gui/colourMap.hpp. Add an enumerator for what the colour is \
*for* and ask ColourMap::getColour() for it; deriving from one that is already \
there -- withAlpha(), brighter() -- is fine and does not match.\n\n\
Only these may name a colour:\n    ${allowedList}")
endif ()

message(STATUS "${checkedCount} source(s) mention juce::Colour; none names one outside the palette.")
