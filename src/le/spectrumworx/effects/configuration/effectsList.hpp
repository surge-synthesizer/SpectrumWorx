////////////////////////////////////////////////////////////////////////////////
///
/// \file effectsList.hpp
/// ---------------
///
/// The single source of truth for which effects exist and in which order.
/// **The order is ABI**: presets and host automation refer to effects by index,
/// so entries may be appended but never reordered or removed.
///
/// \note **It is not what the menu looks like.** The menu's groups and their
/// order are gui/editor/moduleMenuLayout.cpp's, precisely so that they can move
/// when this table's order may not. The `/* … */` markers below are a reading aid
/// and nothing reads them.
///
/// \note Every effect is always built; there is no per-SKU subset.
///
/// Consumers define a three-argument macro and expand LE_SW_EFFECT_LIST over it:
///
/// \code
///     #define LE_SW_AUX_IMPL(folder, module, name) name##Impl,
///     using EffectImpls = std::tuple<LE_SW_EFFECT_LIST(LE_SW_AUX_IMPL) void>;
///     #undef LE_SW_AUX_IMPL
/// \endcode
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef effectsList_hpp__F3A21C64_9E17_4A0B_8D55_1E7C0B94A2D6
#define effectsList_hpp__F3A21C64_9E17_4A0B_8D55_1E7C0B94A2D6
//------------------------------------------------------------------------------

/// \brief x( folder, module, EffectName )
///
/// \note **One entry per line, and clang-format is told to leave it that way.**
/// tools/show-ui/CMakeLists.txt parses this table to register one UI-render test
/// per effect, and its regex matches the first x(...) after a newline -- so a
/// reflowed table silently drops tests without failing. WhitespaceSensitiveMacros
/// does not cover this: that governs *uses* of a macro and this is a definition.
// clang-format off
#define LE_SW_EFFECT_LIST(x)                                                          \
    /* Pitch */                                                                       \
    x(pitch_shifter,           pitchShifter,          PitchShifter)                   \
    x(pitch_follower,          pitchFollower,         PitchFollower)                  \
    x(tune_worx,               tuneWorx,              TuneWorx)                       \
    x(pitch_magnet,            pitchMagnet,           PitchMagnet)                    \
    x(sumo_pitch,              sumoPitch,             SumoPitch)                      \
    x(pitch_spring,            pitchSpring,           PitchSpring)                    \
    x(octaver,                 octaver,               Octaver)                        \
                                                                                      \
    /* Timbre */                                                                      \
    x(bandpass,                bandpass,              Bandpass)                       \
    x(bandpass,                bandpass,              Bandstop)                       \
    x(ah_ah,                   ahAh,                  AhAh)                           \
    x(smoother,                smoother,              Smoother)                       \
    x(sharper,                 sharper,               Sharper)                        \
    x(centroid_extractor,      centroidExtractor,     CentroidExtractor)              \
    x(tonal,                   tonal,                 Tonal)                          \
    x(tonal,                   tonal,                 Atonal)                         \
                                                                                      \
    /* Time */                                                                        \
    x(freeze,                  freeze,                Freeze)                         \
    x(slicer,                  slicer,                Slicer)                         \
    x(wobbler,                 wobbler,               Wobbler)                        \
    x(reverser,                reverser,              Reverser)                       \
    x(eximploder,              exImploder,            Imploder)                       \
    x(eximploder,              exImploder,            Exploder)                       \
                                                                                      \
    /* Space */                                                                       \
    x(frecho,                  frecho,                Frecho)                         \
    x(frecho,                  frecho,                Frevcho)                        \
    x(freqverb,                freqverb,              Freqverb)                       \
                                                                                      \
    /* Phase */                                                                       \
    x(robotizer,               robotizer,             Robotizer)                      \
    x(whisperer,               whisperer,             Whisperer)                      \
    x(phasevolution,           phasevolution,         Phasevolution)                  \
    x(phlip,                   phlip,                 Phlip)                          \
                                                                                      \
    /* Loudness */                                                                    \
    x(gain,                    gain,                  Gain)                           \
    x(exaggerator,             exaggerator,           Exaggerator)                    \
    x(denoiser,                denoiser,              Denoiser)                       \
    x(quiet_boost,             quietBoost,            QuietBoost)                     \
    x(freqnamics,              freqnamics,            Freqnamics)                     \
                                                                                      \
    /* Combine */                                                                     \
    x(talking_wind,            talkingWind,           TalkingWind)                    \
    x(convolver,               convolver,             Convolver)                      \
    x(ethereal,                ethereal,              Ethereal)                       \
    x(vaxateer,                vaxateer,              Vaxateer)                       \
    x(shapeless,               shapeless,             Shapeless)                      \
    x(colorifer,               colorifer,             Colorifer)                      \
    x(merger,                  merger,                Merger)                         \
    x(blender,                 blender,               Blender)                        \
    x(inserter,                inserter,              Inserter)                       \
    x(burrito,                 burrito,               Burrito)                        \
                                                                                      \
    /* PV Domain */                                                                   \
    x(phase_vocoder_analysis,  phaseVocoderAnalysis,  PhaseVocoderAnalysis)           \
    x(pitch_shifter,           pitchShifter,          PVPitchShifter)                 \
    x(pitch_follower,          pitchFollower,         PitchFollowerPVD)               \
    x(tune_worx,               tuneWorx,              TuneWorxPVD)                    \
    x(pitch_magnet,            pitchMagnet,           PitchMagnetPVD)                 \
    x(pitch_spring,            pitchSpring,           PitchSpringPVD)                 \
    x(eximploder,              exImploder,            PVImploder)                     \
    x(eximploder,              exImploder,            PVExploder)                     \
    x(phase_vocoder_synthesis, phaseVocoderSynthesis, PhaseVocoderSynthesis)          \
                                                                                      \
    /* Misc */                                                                        \
    x(armonizer,               armonizer,             Armonizer)                      \
    x(slew_limiter,            slewLimiter,           SlewLimiter)                    \
    x(shifter,                 shifter,               Shifter)                        \
    x(swappah,                 swappah,               Swappah)                        \
    x(quantizer,               quantizer,             Quantizer)
// clang-format on

#define LE_SW_NUMBER_OF_EFFECTS 57

//------------------------------------------------------------------------------
#endif // effectsList_hpp
