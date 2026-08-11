# sw-dsp: the engine, the effects and everything they need. No host.
#
# This is the target the golden tests run against, and the one stage 4 swaps the
# SIMD/FFT backend underneath.
#
# \note It used to say "no GUI", and forbid JUCE, by way of LE_SW_GUI=0. That
# macro is gone: it decided whether four setters on Engine::ModuleParameters were
# virtual, which in a release build decided the layout of every module object, so
# the engine had two ABIs and only the debug ones matched. The GUI configuration
# is the one the plugin needs, the UI reference that forces the virtuals is an
# artefact this design will lose to a two-queue model anyway, and one engine that
# is always right beats two that agree only in debug.
#
# The consequence is that JUCE is now on this target's include path, and the
# module widget set is on its link line. Nothing here may include clap or
# src/core/host_interop.
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(sw-dsp STATIC
        # le/math
        le/math/conversion.cpp
        le/math/math.cpp
        le/math/vector.cpp
        le/math/windows.cpp
        le/math/dft/domainConversion.cpp
        le/math/dft/fft.cpp

        # le/analysis
        le/analysis/musical_scales/musicalScales.cpp
        le/analysis/peak_detector/peakDetector.cpp
        le/analysis/pitch_detector/pitchDetector.cpp

        # le/parameters
        le/parameters/implDetails.cpp
        le/parameters/lfoImpl.cpp
        le/parameters/trigger/parameter.cpp

        # le/utility
        le/utility/assertionHandler.cpp
        le/utility/lexicalCast.cpp

        # le/spectrumworx -- the preset *format*, and the factory banks that are
        # in the binary. Reading and writing an actual file is sw-io's, above;
        # so is decoding an audio file.
        le/spectrumworx/factoryPresets.cpp
        le/spectrumworx/presetStorage.cpp
        le/spectrumworx/presets.cpp

        # le/spectrumworx/engine
        le/spectrumworx/engine/channelBuffers.cpp
        le/spectrumworx/engine/channelData.cpp
        le/spectrumworx/engine/channelDataAmPh.cpp
        le/spectrumworx/engine/channelDataReIm.cpp
        le/spectrumworx/engine/module.cpp
        le/spectrumworx/engine/moduleChainImpl.cpp
        le/spectrumworx/engine/moduleParameters.cpp
        le/spectrumworx/engine/processor.cpp
        le/spectrumworx/engine/setup.cpp

        # le/spectrumworx/effects — shared
        # \note baseParametersUIElements.cpp is one function now. The parameter
        # names and enumerated-value strings it used to hold are in the headers
        # that declare the parameters, because a translation unit that builds
        # the parameter table has to see them -- see UI_NAME in
        # le/parameters/uiElements.hpp. commonParametersUIElements.cpp went the
        # same way and had nothing left.
        le/spectrumworx/effects/baseParametersUIElements.cpp
        le/spectrumworx/effects/configuration/effectNames.cpp
        le/spectrumworx/effects/effects.cpp
        le/spectrumworx/effects/historyBuffer.cpp
        le/spectrumworx/effects/indexRange.cpp
        le/spectrumworx/effects/phase_vocoder/shared.cpp
        le/spectrumworx/effects/vibrato.cpp

        # le/spectrumworx/effects — one per shipped effect, per effectsList.hpp
        le/spectrumworx/effects/ah_ah/ahAhImpl.cpp
        le/spectrumworx/effects/armonizer/armonizerImpl.cpp
        le/spectrumworx/effects/bandpass/bandpassImpl.cpp
        le/spectrumworx/effects/blender/blenderImpl.cpp
        le/spectrumworx/effects/burrito/burritoImpl.cpp
        le/spectrumworx/effects/centroid_extractor/centroidExtractorImpl.cpp
        le/spectrumworx/effects/colorifer/coloriferImpl.cpp
        le/spectrumworx/effects/convolver/convolverImpl.cpp
        le/spectrumworx/effects/denoiser/denoiserImpl.cpp
        le/spectrumworx/effects/ethereal/etherealImpl.cpp
        le/spectrumworx/effects/exaggerator/exaggeratorImpl.cpp
        le/spectrumworx/effects/eximploder/exImploderImpl.cpp
        le/spectrumworx/effects/frecho/frechoImpl.cpp
        le/spectrumworx/effects/freeze/freezeImpl.cpp
        le/spectrumworx/effects/freqnamics/freqnamicsImpl.cpp
        le/spectrumworx/effects/freqverb/freqverbImpl.cpp
        le/spectrumworx/effects/gain/gainImpl.cpp
        le/spectrumworx/effects/inserter/inserterImpl.cpp
        le/spectrumworx/effects/merger/mergerImpl.cpp
        le/spectrumworx/effects/octaver/octaverImpl.cpp
        le/spectrumworx/effects/phase_vocoder_analysis/phaseVocoderAnalysisImpl.cpp
        le/spectrumworx/effects/phase_vocoder_synthesis/phaseVocoderSynthesisImpl.cpp
        le/spectrumworx/effects/phasevolution/phasevolutionImpl.cpp
        le/spectrumworx/effects/phlip/phlipImpl.cpp
        le/spectrumworx/effects/pitch_follower/pitchFollowerImpl.cpp
        le/spectrumworx/effects/pitch_magnet/pitchMagnetImpl.cpp
        le/spectrumworx/effects/pitch_shifter/pitchShifterImpl.cpp
        le/spectrumworx/effects/pitch_spring/pitchSpringImpl.cpp
        le/spectrumworx/effects/quantizer/quantizerImpl.cpp
        le/spectrumworx/effects/quiet_boost/quietBoostImpl.cpp
        le/spectrumworx/effects/reverser/reverserImpl.cpp
        le/spectrumworx/effects/robotizer/robotizerImpl.cpp
        le/spectrumworx/effects/shapeless/shapelessImpl.cpp
        le/spectrumworx/effects/sharper/sharperImpl.cpp
        le/spectrumworx/effects/shifter/shifterImpl.cpp
        le/spectrumworx/effects/slew_limiter/slewLimiterImpl.cpp
        le/spectrumworx/effects/slicer/slicerImpl.cpp
        le/spectrumworx/effects/smoother/smootherImpl.cpp
        le/spectrumworx/effects/sumo_pitch/sumoPitchImpl.cpp
        le/spectrumworx/effects/swappah/swappahImpl.cpp
        le/spectrumworx/effects/talking_wind/talkingWindImpl.cpp
        le/spectrumworx/effects/tonal/tonalImpl.cpp
        le/spectrumworx/effects/tune_worx/tuneWorxImpl.cpp
        le/spectrumworx/effects/vaxateer/vaxateerImpl.cpp
        le/spectrumworx/effects/whisperer/whispererImpl.cpp
        le/spectrumworx/effects/wobbler/wobblerImpl.cpp

        # core — the module chain, minus its host and GUI halves
        core/automatedModuleChain.cpp
        core/spectrumWorxCore.cpp
        core/modules/automatedModule.cpp
        core/modules/factory.cpp
        core/modules/moduleDSPAndGUI.cpp

        # Which thread is this, may it do that, and how a change made on one
        # reaches the other. See doc/tech/threading_model.md.
        core/threading/threadCheck.cpp
        core/threading/publish.cpp
)

# configure_file into the build tree, never back into src/ — that is what the
# 2016 build did and what stage 3.4 removed.
set(swGeneratedIncludeDir "${CMAKE_CURRENT_BINARY_DIR}/geninclude")
set(versionMajor ${PROJECT_VERSION_MAJOR})
set(versionMinor ${PROJECT_VERSION_MINOR})
set(versionPatch ${PROJECT_VERSION_PATCH})
set(versionDescription "${GIT_IMPLIED_DISPLAY_VERSION}")
set(fullVersionString "${PROJECT_VERSION}")
set(editionString "") # editions went with the licence manager
configure_file(
        configuration/versionConfiguration.hpp.in
        "${swGeneratedIncludeDir}/configuration/versionConfiguration.hpp"
        @ONLY
)

target_include_directories(sw-dsp PUBLIC . "${swGeneratedIncludeDir}")

# The 2016 build force-included this, and this is also where the warning
# baseline lands. Ours only -- see cmake/sw-our-sources.cmake for why a
# target-wide option cannot express that, PUBLIC or PRIVATE.
sw_force_include_odr_header(sw-dsp)

# The preset parser. PUBLIC because presets.hpp holds a TiXmlDocument by value.
#
# \note sst-plugininfra's, not a submodule of our own: it is already configured
# and built (SST_PLUGININFRA_PROVIDE_TINYXML defaults ON), and a second XML
# library to pin and vendor-audit is a poor trade for an API preference. It is
# TinyXML 1, which the plan called tinyxml2 -- the plan was writing from the
# option's name.
target_link_libraries(sw-dsp PUBLIC sst-plugininfra::tinyxml)

# LE::Utility::assertionFailed lives in assertionHandler.cpp; without this the
# asserts degrade to the CRT's and the DAW never sees them.
target_compile_definitions(sw-dsp PUBLIC LE_ENABLE_ASSERT_HANDLER)

# \note The 2016 build's configuration macros are gone rather than configurable:
# LE_SW_SDK_BUILD, LE_SW_FMOD, LE_SW_FULL, LE_SW_PURE_ANALYSIS,
# LE_SW_TW_RETUNE_TEST and LE_MELODIFY_SDK_BUILD named an SDK, an audio backend
# and two SKUs that no longer exist; LE_SW_ENGINE_INPUT_MODE named a parameter
# that reconfigured the plugin's I/O -- which is audio-ports-config's job in
# CLAP; and LE_SW_ENGINE_WINDOW_PRESUM was an SW_ENGINE_WINDOW_PRESUM option,
# always OFF, over the plumbing that let the analysis fold factor be something
# other than one, plus a WindowSizeFactor global parameter to set it with.
# Turning the option on grew the parameter table by a row and overflowed the
# std::uint16_t window size at the maximum FFT size, so the factor could not
# exceed one anyway; the fold and unfold have since been collapsed to that
# single-frame case.
#
# LE_SIMPLE_TUNEWORX is gone the other way: it was *set* here in 2016, and both
# of the arms it and LE_SW_SDK_BUILD selected have been resolved to the plugin's
# in the source. Dropping it silently is how the extra Tune Worx parameters
# reached a release -- see the note in tuneWorx.hpp.

# rtsan_support.h, for the RealtimeSanitizer region ScopedAudioThreadEntry opens, and
# for the RTSAN_DISABLE guards the conceded audio-thread allocations will carry.
# PUBLIC: sw-impl marks those sites and the header is where the macro comes from.
# It is header only and compiles to nothing without -fsanitize=realtime.
target_link_libraries(sw-dsp PUBLIC sst-cpputils)

# ...and it prints a stack trace with the message, through sst-plugininfra --
# which has a Windows arm over DbgHelp, the platform whose failures reach us as
# a log rather than a debugger session. PUBLIC: sw-gui-resources compiles the
# same file.
target_link_libraries(sw-dsp PUBLIC sst-plugininfra)

# \note **sw-dsp links no JUCE, and that is the point of the target.**
#
#   `target_link_libraries(sw-dsp PUBLIC juce::juce_gui_basics sw-gui-resources)`
# stood here, because SW::Module embedded a GUI::ModuleUI and Module::Impl<Effect>
# inherited that effect's widget storage -- so the engine's headers named JUCE
# components and the factory had to know how big a rack of knobs is. Then
# juce_audio_formats stood here for the sample decoder, and presets.{hpp,cpp}
# carried a juce::String and a juce::File.
#
#   None of it is true now. A module's strip belongs to the editor and holds a
# counted reference to the module rather than the other way round; the preset
# format speaks std::string_view and the conversion happens in sw-io; and the two
# files that open files are in sw-io as well. The engine is what a DSP test can
# link on its own. See doc/tech/threading_model.md §6, and
# checkNoJuceInDSP.cmake, which fails the build if any of it comes back.

# The factory banks. PRIVATE: factoryPresets.cpp is the only thing that names
# cmrc, and no header says anything about where its bytes come from.
target_link_libraries(sw-dsp PRIVATE sw::assets)

################################################################################
# sw-io -- the audio file decoder.
#
#   Here rather than in sw-dsp because juce::AudioFormatManager is JUCE and
# sw-dsp is the layer that must not be, and here rather than in sw-gui because it
# draws nothing: the plugin reads a session's sample without an editor open.
#
# \note **This was "the two things that open a file"**, the other being
# le/spectrumworx/presetFile.cpp -- the preset reader and writer with a
# `juce::File` on them, and the point at which the format's `std::string_view`
# interface met the editor's `juce::String`. Both halves are gone: presetStorage
# in sw-dsp opens preset files over std::filesystem and `fs::path` goes all the
# way up to the browser, so there was no conversion left for that file to
# perform. What is left here needs JUCE for the *decoder*, not for the path.
#                                         (09.08.2026.) (SW port)
################################################################################

add_library(sw-io STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/external_audio/sample.cpp
)

sw_force_include_odr_header(sw-io)

target_link_libraries(sw-io PUBLIC sw-dsp)

# The factory samples, which are in the binary beside the banks.
target_link_libraries(sw-io PRIVATE sw::assets)

# The audio file decoder, replacing the DirectShow and ExtAudioFile ones.
# PUBLIC because sample.cpp's header names juce::AudioFormatManager, so whatever
# links sw-io has to see the same headers and the same JUCE settings it does.
#
# \note This read `juce::juce_audio_formats`, and that is what made every target
# above sw-io compile juce_audio_formats and juce_audio_basics for itself. The
# module is inside sw-juce now; see libs/CMakeLists.txt.
target_link_libraries(sw-io PUBLIC sw-juce)

# \note JUCE's module settings -- JUCE_USE_MP3AUDIOFORMAT as well as
# JUCE_USE_CURL and JUCE_WEB_BROWSER -- were set here, PUBLIC, on the reasoning
# that sw-io is what compiles those modules. It is, and so is every other target
# that links one, which is the half that was missed: sw-gui-resources takes
# juce::juce_gui_basics directly and built juce_core with JUCE's defaults.
# They are on the modules themselves now; see libs/CMakeLists.txt.
#
#   The MP3 one was not yet broken -- nothing reaches juce_audio_formats except
# through sw-io -- and is moved with the others because that is one link away
# from being the same bug, silently: a target that linked the module directly
# would compile a decoder-less copy of it into the binary beside ours.
#                                         (05.08.2026.) (SW port)

# \note LE_NO_PRESETS stood here. It compiled out ModuleParameters::{load,save}
# PresetParameters -- the whole preset serialisation -- because presets.cpp read
# and wrote preset files through boost::mmap, a library stage 0 deleted, in the
# same translation unit. Stage 8.0 moved the file half to presetFile.cpp and put
# juce::File under it, so the engine can have the serialisation without it.

# \note LE_SW_DISABLE_SIDE_CHANNEL stood here, and it was the *file* loader
# rather than the host's sidechain port -- that one has been live since the CLAP
# was written. It compiled out external_audio/, the editor's "External audio"
# box and the preset format's Sample attribute, because the only macOS
# Sample::doLoad was over ExtAudioFile and FSRef and did not build against a
# current SDK. Stage 5.0 replaced both platform decoders with one over
# juce::AudioFormatManager, so the flag has nothing left to stand for.

if (APPLE)
    # le/math/vector.cpp and le/math/dft/fft.cpp are vDSP/vForce on Apple.
    target_link_libraries(sw-dsp PUBLIC "-framework Accelerate")
    # A diagnostic, not a configuration: `-DSW_FORCE_PFFFT=ON` puts the pffft
    # branch on Apple as well, so that a golden drifting between this machine
    # and another platform can be asked which of the two variables did it. Every
    # non-Apple build changes the backend and the platform together, and this is
    # the only way to change one. See the note in le/math/dft/fft.hpp.
    #
    # PUBLIC because fft.hpp is what reads it, and the tests include it.
    if (SW_FORCE_PFFFT)
        target_compile_definitions(sw-dsp PUBLIC SW_FORCE_PFFFT)
        target_link_libraries(sw-dsp PRIVATE pffft)
    endif()
else()
    # ...and pffft everywhere else, per le/math/dft/fft.hpp's LE_PFFFT. PRIVATE:
    # fft.hpp forward declares pffft::PFFFT_Setup rather than including pffft.h,
    # so nothing outside fft.cpp needs the include path.
    target_link_libraries(sw-dsp PRIVATE pffft)
endif()
