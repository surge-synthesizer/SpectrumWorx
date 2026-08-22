# Checks that nothing with a side effect is written inside LE_ASSERT,
# LE_ASSERT_MSG or LE_ASSUME.
#
#   All three compile their argument away under NDEBUG -- assert.hpp:54-57 makes
# LE_ASSERT and LE_ASSERT_MSG `static_cast<void>(0)`, and LE_ASSUME keeps only the
# compiler's bare hint. An expression written inside one is therefore not merely
# unchecked in a release build, it is *absent*, and the checked build the test
# suite runs in is the only place the program does what it reads as doing.
#
#   This is not hypothetical and it is not a style rule. Two ring pushes were
# written this way -- SpectrumWorxCLAP::retire() and
# Threading::publishModuleMove() -- and each was the whole of what its function
# did. Every shipped binary leaked every module, chain and sample the audio
# thread handed back, and dropped every module move on the floor, while the suite
# stayed green because the suite runs the checked build. Nothing found it for two
# weeks; this finds it in a second.
#
#   LE_VERIFY is the sanctioned way to assert on something that must still
# happen: it is `static_cast<void>(expression)` under NDEBUG rather than nothing
# at all. So the fix for anything reported here is either to hoist the call out of
# the assertion, or to say LE_VERIFY and mean it.
#
# Run: cmake -D SOURCE_DIR=<dir> -P checkNoAssertSideEffects.cmake
#
# \note A source scan rather than a compile_commands.json one, unlike the two
# gates beside it: what is being checked is what the text says, so this needs no
# build directory and works under every generator -- including the Visual Studio
# and Xcode ones the other two have to skip.
#
# SPDX-License-Identifier: GPL-3.0-or-later

# See the note in checkNoJuceInDSP.cmake: `cmake -P` sets no policies of its own.
cmake_minimum_required(VERSION 3.28)

################################################################################
#
# \note Source text is never put through a CMake list, and that is not a
# preference. `file(STRINGS)` and `foreach(IN LISTS)` both merge lines whenever
# the content contains an unmatched `[` -- C++ is full of them, and
# historyBuffer.cpp came back as 36 "lines" for 166. The first draft of this check
# reported 325 offenders, every one of them an assertion that had swallowed the
# rest of its file. So lines are peeled off a plain string with string(FIND) and
# string(SUBSTRING), which have no such parsing.
#
################################################################################

set(roots "${SOURCE_DIR}/src" "${SOURCE_DIR}/tests" "${SOURCE_DIR}/tools")

set(sources "")
foreach (root IN LISTS roots)
    if (IS_DIRECTORY "${root}")
        file(GLOB_RECURSE found "${root}/*.cpp" "${root}/*.hpp" "${root}/*.inl")
        list(APPEND sources ${found})
    endif ()
endforeach ()

set(offenderReport "")
set(offenderCount 0)
set(unterminatedReport "")
set(unterminatedCount 0)
set(checkedCount 0)

################################################################################
# What counts as a side effect
################################################################################

#   Calls that mutate whatever they are called on. Substrings rather than
# patterns, and each covers its whole family: ".push" is push()/push_back(), and
# ".emplace" is every emplace form.
#
# \note Deliberately not "any call at all". Most of the 600-odd assertions in the
# tree read state through one -- isValidValue(), size(), engineIsRunning() -- and
# a rule that flagged those would be turned off within a day. These are the ones
# whose name says they change something.
set(mutatingCalls
        ".push" "->push"
        ".pop" "->pop"
        ".emplace" "->emplace"
        ".insert" "->insert"
        ".erase" "->erase"
        ".clear(" "->clear("
        ".reset(" "->reset("
        ".swap(" "->swap("
        ".detach(" "->detach("
        ".release(" "->release("
)

# Two-character operators ending in '=' that are not assignments, plus the
# by-value lambda capture, removed before looking for an '=' that is left.
# Legible where a regex expressing "an = preceded by none of these" would not be.
set(notAssignments "==" "!=" "<=" ">=" "+=" "-=" "*=" "/=" "%=" "&=" "|=" "^=" "[=]")

# \note Takes the *name* of the variable holding the argument, not its value: an
# argument containing a semicolon would otherwise arrive as several parameters.
function(sideEffectIn argumentVariable outVerdict)
    set(argument " ${${argumentVariable}}")

    foreach (marker IN LISTS mutatingCalls)
        string(FIND "${argument}" "${marker}" position)
        if (NOT position EQUAL -1)
            set(${outVerdict} "calls ${marker}" PARENT_SCOPE)
            return()
        endif ()
    endforeach ()

    string(FIND "${argument}" "++" position)
    if (NOT position EQUAL -1)
        set(${outVerdict} "increments" PARENT_SCOPE)
        return()
    endif ()

    string(FIND "${argument}" "--" position)
    if (NOT position EQUAL -1)
        set(${outVerdict} "decrements" PARENT_SCOPE)
        return()
    endif ()

    # The leading space above is what makes a `new` or `delete` at the very start
    # of the argument still follow a non-word character.
    if (argument MATCHES "[^A-Za-z0-9_]new[^A-Za-z0-9_]")
        set(${outVerdict} "allocates" PARENT_SCOPE)
        return()
    endif ()
    if (argument MATCHES "[^A-Za-z0-9_]delete[^A-Za-z0-9_]")
        set(${outVerdict} "frees" PARENT_SCOPE)
        return()
    endif ()

    set(stripped "${argument}")
    foreach (operator IN LISTS notAssignments)
        string(REPLACE "${operator}" "" stripped "${stripped}")
    endforeach ()
    string(FIND "${stripped}" "=" position)
    if (NOT position EQUAL -1)
        set(${outVerdict} "assigns" PARENT_SCOPE)
        return()
    endif ()

    set(${outVerdict} "" PARENT_SCOPE)
endfunction()

################################################################################
# Reading the argument out of the source
################################################################################

#   Accumulated across lines until the parentheses balance, because 85 of the
# tree's assertions do not close on the line they open -- clang-format wraps them
# at the column limit like anything else, and a line-at-a-time check would be
# blind to one call site in seven.
#
#   The argument ends at the parenthesis that balances the macro's own, not at the
# end of that line: `LE_ASSERT( x ); // ...the new ComboBox...` is otherwise read
# as allocating, which is exactly the false positive that makes a check like this
# get switched off.
#
# \note Parentheses inside a string literal would run the accumulator past the end
# of the invocation and report something from a later line. No assertion message
# in the tree contains one, and that failure direction is the right one: it
# reports rather than falls silent.

#   Walks \p lineVariable's parentheses in order from \p enteringDepth. Sets
# \p outCut to how many of its characters belong to the argument once the macro's
# own parenthesis is balanced, or -1 if it still is not, and \p outDepth to the
# depth carried to the next line.
#
# \note By name, and one iteration per parenthesis rather than per character: a
# few per line, against the eighty a character walk would cost.
function(scanParentheses lineVariable enteringDepth outCut outDepth)
    set(text "${${lineVariable}}")
    set(depth ${enteringDepth})
    set(offset 0)

    while (TRUE)
        string(FIND "${text}" "(" openAt)
        string(FIND "${text}" ")" closeAt)

        if (openAt EQUAL -1 AND closeAt EQUAL -1)
            break()
        endif ()

        if (closeAt EQUAL -1 OR (NOT openAt EQUAL -1 AND openAt LESS closeAt))
            set(at ${openAt})
            math(EXPR depth "${depth} + 1")
        else ()
            set(at ${closeAt})
            math(EXPR depth "${depth} - 1")
        endif ()

        math(EXPR consumed "${at} + 1")
        math(EXPR offset "${offset} + ${consumed}")
        string(SUBSTRING "${text}" ${consumed} -1 text)

        if (depth EQUAL 0)
            set(${outCut} ${offset} PARENT_SCOPE)
            set(${outDepth} 0 PARENT_SCOPE)
            return()
        endif ()
    endwhile ()

    set(${outCut} -1 PARENT_SCOPE)
    set(${outDepth} ${depth} PARENT_SCOPE)
endfunction()

foreach (sourceFile IN LISTS sources)
    file(READ "${sourceFile}" contents)

    # Most of the tree has no assertion in it at all, and reading a whole file to
    # peel it a line at a time is the expensive part of this check.
    string(FIND "${contents}" "LE_ASSERT" assertPosition)
    string(FIND "${contents}" "LE_ASSUME" assumePosition)
    if (assertPosition EQUAL -1 AND assumePosition EQUAL -1)
        continue()
    endif ()

    set(argument "")
    set(collecting FALSE)
    set(depth 0)
    set(startLine 0)
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

        set(remainder "${line}")

        if (NOT collecting)
            # The earliest macro opening on this line. LE_ASSERT_MSG is looked for
            # in its own right because LE_ASSERT is a prefix of it.
            set(bestPosition -1)
            set(bestLength 0)
            foreach (macroName IN ITEMS "LE_ASSERT_MSG" "LE_ASSERT" "LE_ASSUME")
                string(LENGTH "${macroName}" macroLength)
                string(FIND "${line}" "${macroName}(" position)
                set(spacing 0)
                if (position EQUAL -1)
                    string(FIND "${line}" "${macroName} (" position)
                    set(spacing 1)
                endif ()
                if (NOT position EQUAL -1)
                    math(EXPR nameLength "${macroLength} + ${spacing}")
                    if (bestPosition EQUAL -1 OR position LESS bestPosition)
                        set(bestPosition ${position})
                        set(bestLength ${nameLength})
                    endif ()
                endif ()
            endforeach ()

            if (bestPosition EQUAL -1)
                continue()
            endif ()

            # Everything from the macro's own '(' onwards.
            math(EXPR from "${bestPosition} + ${bestLength}")
            string(SUBSTRING "${line}" ${from} -1 remainder)
            set(collecting TRUE)
            set(argument "")
            set(depth 0)
            set(startLine ${lineNumber})
            math(EXPR checkedCount "${checkedCount} + 1")
        endif ()

        scanParentheses(remainder ${depth} cut depth)

        if (cut EQUAL -1)
            set(argument "${argument} ${remainder}")
            continue()
        endif ()

        # Only as far as the parenthesis that closed the macro's own.
        string(SUBSTRING "${remainder}" 0 ${cut} tail)
        set(argument "${argument} ${tail}")

        sideEffectIn(argument verdict)
        if (verdict)
            string(STRIP "${argument}" trimmed)
            math(EXPR offenderCount "${offenderCount} + 1")
            string(APPEND offenderReport
                    "\n    ${sourceFile}:${startLine}: ${verdict}\n        ${trimmed}")
        endif ()
        set(collecting FALSE)
        set(argument "")
    endwhile ()

    if (collecting)
        math(EXPR unterminatedCount "${unterminatedCount} + 1")
        string(APPEND unterminatedReport "\n    ${sourceFile}:${startLine}")
    endif ()
endforeach ()

################################################################################
# The verdict
################################################################################

if (offenderCount)
    message(FATAL_ERROR
            "${offenderCount} assertion(s) have a side effect inside them:\n${offenderReport}\n\n\
LE_ASSERT, LE_ASSERT_MSG and LE_ASSUME do not evaluate their argument under NDEBUG, so this code \
does not exist in any shipped build -- see assert.hpp:54-57. Hoist the call out of the assertion \
and assert on its result, or say LE_VERIFY, which does still evaluate. If the expression really is \
pure and this check has misread it, narrow the marker in `mutatingCalls` rather than adding an \
exemption: an exemption list here is a place for the next one of these to hide.")
endif ()

if (unterminatedCount)
    message(FATAL_ERROR
            "This check lost track of where ${unterminatedCount} assertion(s) end:\
\n${unterminatedReport}\n\n\
Their parentheses never balanced before the end of the file, which usually means an assertion \
message contains a '(' or ')' inside a string literal. See the note above.")
endif ()

# A silent pass is also what a typo in the macro names looks like.
if (checkedCount EQUAL 0)
    message(FATAL_ERROR "No assertion was examined at all. Either SOURCE_DIR does not name this "
                        "project, or the macro names in this script have moved.")
endif ()

message(STATUS "${checkedCount} assertions, none of them with a side effect inside.")
