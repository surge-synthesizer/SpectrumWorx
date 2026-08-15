# Two test binaries, split by what they are allowed to link.
#
#   sw-dsp-tests    the engine, the effects and the preset format. Links sw-dsp
#                   and nothing else -- **no JUCE at all**, which is goal 3 of
#                   doc/tech/threading_model.md and the reason the split
#                   exists. If a case here starts needing juce::File it will not
#                   link, which is the point: the boundary is checked by the
#                   build rather than by a reviewer.
#
#   sw-plugin-tests everything above that -- the CLAP plugin, the editor, the
#                   audio file decoder. Links sw-impl, which brings all of it.
#
# \note One binary, sw-tests, stood here. Splitting it does not add coverage; it
# turns "the engine could be built without JUCE" from a claim into a link.
# checkNoJuceInDSP.cmake says the same thing about the compile lines, and the two
# fail differently: the gate catches a JUCE module appearing on sw-dsp's link
# line, this catches a *test* quietly reaching above the layer it is testing.
#
# SPDX-License-Identifier: GPL-3.0-or-later

################################################################################
# sw-dsp-tests
################################################################################

add_executable(sw-dsp-tests
        core/engineOwnershipTests.cpp
        core/protocolTests.cpp
        effects/amplifyingEffectsTests.cpp
        effects/effectsListTests.cpp
        effects/sideChainTests.cpp
        effects/silentDefaultsTests.cpp
        goldens/engineHarness.cpp
        goldens/goldenDigest.cpp
        goldens/goldenTests.cpp
        math/fftTests.cpp
        math/scalarTests.cpp
        math/vectorTests.cpp
        parameters/lfoTests.cpp
        parameters/parameterLayoutTests.cpp
        parameters/streamingNameTests.cpp
        presets/presetCorpusTests.cpp
        presets/presetFileTests.cpp
        presets/presetHarness.cpp
        presets/presetRenderTests.cpp
        presets/presetReportTests.cpp
        presets/presetRoundTripTests.cpp
        utility/buffersTests.cpp
        utility/circularListTests.cpp
        utility/lexicalCastTests.cpp
        utility/spanTests.cpp
)

target_link_libraries(sw-dsp-tests PRIVATE sw-dsp Catch2::Catch2WithMain)

################################################################################
# sw-plugin-tests
################################################################################

add_executable(sw-plugin-tests
        clap/engineSetupStalenessTests.cpp
        clap/hostileParameterIDTests.cpp
        clap/hostInteropTests.cpp
        clap/identityTests.cpp
        clap/parameterModelTests.cpp
        clap/parameterTextTests.cpp
        clap/pluginTests.cpp
        clap/publishProtocolTests.cpp
        clap/stateTests.cpp
        clap/threadingTests.cpp
        # Drives a real clap_plugin through its C entry point.
        core/threadCheckTests.cpp
        external_audio/sampleFeedTests.cpp
        external_audio/sampleTests.cpp
        goldens/engineHarness.cpp # the CLAP cases drive an instantiable core
        # The parameter table is the host-facing enumeration, which lives in
        # sw-impl -- plugin2Host.cpp -- rather than in the engine.
        parameters/parameterTableTests.cpp
        presets/presetHarness.cpp
        gui/aboutPageTests.cpp
        gui/buildStampTests.cpp
        gui/knobMenuTests.cpp
        gui/knobPaintingTests.cpp
        gui/lfoDisplayTests.cpp
        gui/moduleControlFocusTests.cpp
        gui/moduleDragTests.cpp
        gui/overlayPanelTests.cpp
        gui/pathsTests.cpp
        gui/skinTests.cpp
        gui/twoInstanceTests.cpp
        io/jucePathTests.cpp
)

# sw-impl brings sw-dsp with it, and brings the CLAP entry point the plugin
# tests drive. No dlopen: the tests link the same static library the .clap
# bundle does, so what they exercise is what ships.
#
# sw-gui by name, because sw-impl links it PRIVATE: the editor cases include its
# headers, and a LINK_ONLY dependency carries the library without them. Every
# target that wants a JUCE header now says which one it wants, which is what
# stops the bundles compiling JUCE a second time -- see src/CMakeLists.txt.
#                                       (09.08.2026.) (SW port)
target_link_libraries(sw-plugin-tests PRIVATE sw-impl sw-gui Catch2::Catch2WithMain)

# clapwrapper/{auv2,vst3}.h, for the two sub-factories identityTests.cpp pins.
# sw-impl links this PRIVATE -- it is a header-only interface target and the
# entry stubs get it from clap-wrapper's own side -- so it does not arrive here
# across the link line. clap-helpers is the same shape, and is what makes
# spectrumWorxCLAP.hpp includable: several cases need the class itself rather
# than the C vtable, to reach what a host cannot name.
target_link_libraries(sw-plugin-tests PRIVATE clap-helpers clap-wrapper-extensions)

# gui/aboutPageTests.cpp names VersionInformation directly, to check the About
# page reports what the tree was configured as. sw-impl and sw-gui both link this
# PRIVATE, so the archive arrives across the link line but its include directory
# and SST_PLUGININFRA_HAS_VERSION_INFORMATION do not -- CMake records a private
# dependency as $<LINK_ONLY:...>, which is the library without its interface.
target_link_libraries(sw-plugin-tests PRIVATE sst-plugininfra::version_information)

# X11 itself, and only here. JUCE reaches libX11 through dlopen() and a symbol
# table, so nothing it exports puts the library on a consumer's link line --
# but gui/moduleControlFocusTests.cpp has to ask the server one question
# *before* JUCE is allowed near it (whether a window manager is running; see the
# note on aWindowCanBeMade()), and that call has to resolve at link time. The
# headers are not wanted, only the three symbols, so this is the library alone.
if (UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(sw-plugin-tests PRIVATE ${X11_LIBRARIES})
endif ()

################################################################################
# What both need
################################################################################

foreach (testTarget sw-dsp-tests sw-plugin-tests)
    # The tests include the engine's headers, so they need what those headers
    # assume just as much as the engine does. It used to arrive across the link
    # line from sw-dsp, along with everything else that linked it.
    sw_force_include_odr_header(${testTarget})

    # \note LE_CHECKED_BUILD is deliberately *not* forced here. It decides
    # whether NDEBUG survives, and forcing it on for the test objects only would
    # leave them calling an assertionFailed() that a release libsw-dsp.a does not
    # define. The test runner follows CMAKE_BUILD_TYPE like everything else:
    # Debug runs the ~1200 asserts, Release renders the goldens at shipping
    # speed.
    target_compile_definitions(${testTarget} PRIVATE
            SW_GOLDEN_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/goldens/data"
            SW_PARAMETER_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/parameters/data"
            SW_PRESET_SNAPSHOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets/data"
            # \note The shipping banks themselves, not a copy: what the sweeps
            # ask is whether *those* files load, translate and render, and a copy
            # is one more thing that can rot.
            SW_PRESET_DATA_DIR="${CMAKE_SOURCE_DIR}/assets/presets"
            # \note And the frozen copies, which are the opposite decision for
            # the opposite reason: a digest has to be over something that does
            # not move, and the banks above are edited as ordinary work. \see the
            # note at the top of presets/presetCorpusTests.cpp.
            SW_PRESET_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets/data/fixtures"
            # Where a test that has to write a file writes it. The build tree,
            # not the system temporary directory: it is cleaned with the build,
            # two configurations cannot collide in it, and a sandboxed CI can
            # reach it.
            SW_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output")

    # So that clap/ can reach goldens/engineHarness.hpp: the CLAP-side tests need
    # an instantiable SpectrumWorxCore, and that is exactly what the golden
    # harness is.
    target_include_directories(${testTarget} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    catch_discover_tests(${testTarget})
endforeach ()

################################################################################
# Properties of the build rather than of the code
################################################################################

# Not a Catch2 case: what it checks is a property of the build, not of the code,
# and it has to run against the generated compile lines rather than against
# anything that got compiled.
add_test(NAME odr-header-stays-in-our-sources
        COMMAND ${CMAKE_COMMAND}
        -D "BUILD_DIR=${CMAKE_BINARY_DIR}"
        -D "SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/checkODRHeaderScope.cmake")

# Likewise: the engine must be linkable without JUCE, which is goal 3 of
# doc/tech/threading_model.md. sw-dsp-tests above is the other half of the
# same claim -- this one catches a JUCE module arriving on sw-dsp's link line
# even while nothing includes a JUCE header yet.
add_test(NAME engine-links-no-juce
        COMMAND ${CMAKE_COMMAND}
        -D "BUILD_DIR=${CMAKE_BINARY_DIR}"
        -D "SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/checkNoJuceInDSP.cmake")

# And the one that no Catch2 case can express, because what it is about is code
# that a Catch2 case -- which runs the checked build -- cannot see. LE_ASSERT and
# friends do not evaluate their argument under NDEBUG, so anything with a side
# effect written inside one is missing from every shipped binary. Two ring pushes
# were, for two weeks, under a green suite.
#
# \note No BUILD_DIR: this one reads the sources rather than the compile lines, so
# it also runs under the generators the two above have to skip.
add_test(NAME asserts-have-no-side-effects
        COMMAND ${CMAKE_COMMAND}
        -D "SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/checkNoAssertSideEffects.cmake")

# And the path type. `fs::path` from the engine to the editor, with juce::File
# confined to io/jucePath.hpp and the two juce::FileChoosers that are handed one
# and answer with one -- because every conversion between the two is a chance to
# get the encoding wrong, which is what issue #28 was. Also a source scan, so it
# runs under every generator.
add_test(NAME no-juce-file-outside-the-edge
        COMMAND ${CMAKE_COMMAND}
        -D "SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/checkNoJuceFile.cmake")

# \note check_gui_flag_parity.py was wired in here, to compile every sw-dsp
# source at both settings of LE_SW_GUI and compare the objects. It did its job:
# it kept the two configurations honest for exactly as long as there were two.
# The macro is gone, so the check went with it.
