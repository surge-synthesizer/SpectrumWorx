////////////////////////////////////////////////////////////////////////////////
///
/// shared.cpp
/// ----------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "shared.hpp"
//------------------------------------------------------------------------------
#include "le/math/constants.hpp"
#include "le/math/conversion.hpp"
#include "le/math/math.hpp"
#include "le/math/vector.hpp"
#include "le/parameters/uiElements.hpp"
#include "le/spectrumworx/engine/channelDataAmPh.hpp"
#include "le/spectrumworx/engine/setup.hpp"

#include <cstdint>

namespace LE::SW::Effects::PhaseVocoderShared
{

void PitchShiftParameters::setScalingFactor(float const newScale, std::uint16_t const numberOfBins)
{
    LE_ASSUME(newScale > 0);

    float const minimumScaleForSpecifiedNumberOfBins((1.0f / Math::convert<float>(numberOfBins)) +
                                                     std::numeric_limits<float>::epsilon());
    scale_ = std::max(newScale, minimumScaleForSpecifiedNumberOfBins);
}

float PitchShiftParameters::scaleFromSemiTonesAndCents(float const &semiTones,
                                                       std::int8_t const cents)
{
    float const totalsemitones(semiTones + Math::percentage2NormalisedLinear(cents));
    return scaleFromSemiTones(totalsemitones);
}

float PitchShiftParameters::scaleFromSemiTones(float const semiTones)
{
    float const pitchScale(Math::semitone2Interval12TET(semiTones));
    return pitchScale;
}

bool PitchShiftParameters::skipProcessing() const { return Math::is<1>(scale()); }

LE_NOINLINE void BaseParameters::setup(Engine::Setup const &engineSetupParam)
{
    using Math::Constants::twoPi_d;

    Engine::Setup const *LE_RESTRICT const pEngineSetup(
        &engineSetupParam); //...mrmlj...msvc12 still w/o restricted references...

    auto const freqPerBin(pEngineSetup->frequencyRangePerBin<double>());

    freqPerBin_ = freqPerBin;
    expctRate_ = twoPi_d * pEngineSetup->stepSize<double>() / pEngineSetup->fftSize<double>();
    deviationFactor_ = freqPerBin * pEngineSetup->windowOverlappingFactor<double>() / twoPi_d;

#if defined(_DEBUG) && !defined(__clang__) && 0 //...mrmlj...started failing even on msvc10...
    float const inverseDeviationFactor(1 / deviationFactor_);
    float const inverseInverseDeviationFactor(1 / inverseDeviationFactor);
    LE_ASSERT_MSG(inverseInverseDeviationFactor == deviationFactor_,
                  "Deviation factor inverse suffers from numerical error.");
#endif // __clang__
}

void PVPitchShifter::setPitchScaleFromSemitones(float const semitones,
                                                std::uint16_t const numberOfBins)
{
    pitchShiftParameters_.setScalingFactor(PitchShiftParameters::scaleFromSemiTones(semitones),
                                           numberOfBins);
}

void PVPitchShifter::setDynamicScalingFactor(float const newScale,
                                             std::uint16_t const numberOfBins) const
{
    const_cast<PitchShiftParameters &>(pitchShiftParameters())
        .setScalingFactor(newScale, numberOfBins);
}

////////////////////////////////////////////////////////////////////////////////
//
// PitchShifter::process()
// -----------------------
//
////////////////////////////////////////////////////////////////////////////////

void LE_NOINLINE PVPitchShifter::process(Engine::ChannelData_AmPh &&data) const
{
    pitchShiftAndScale(data, pitchShiftParameters());
}

void LE_NOINLINE PVPitchShifter::process(float const pitchScale, Engine::ChannelData_AmPh &&data,
                                         Engine::Setup const &engineSetup) const
{
    setDynamicScalingFactor(pitchScale, engineSetup.numberOfBins());
    process(std::forward<Engine::ChannelData_AmPh>(data));
}

void LE_NOINLINE PitchShifter::process(float const pitchScale, ChannelState &channelState,
                                       Engine::ChannelData_AmPh &&data,
                                       Engine::Setup const &engineSetup) const
{
    setDynamicScalingFactor(pitchScale, engineSetup.numberOfBins());
    process(channelState, std::forward<Engine::ChannelData_AmPh>(data));
}

void LE_NOINLINE PitchShifter::process(ChannelState &channelState,
                                       Engine::ChannelData_AmPh &&data) const
{
    using namespace Math;

    /// \note
    ///   Initial phase setup according to equation 11 from the "Improved
    /// Phase Vocoder Time-Scale Modification of Audio" paper by Laroche and
    /// Dolson (http://homes.esat.kuleuven.be/~bdefraen/Laroche.pdf).
    /// Unfortunately it still makes no audible difference from the original
    /// approach of simply zeroing everything.
    ///                                       (18.04.2012.) (Domagoj Saric)
    /// \note
    ///   We also reset the phases on every pitch scale factor change when the
    /// scale factor becomes an integer. Reseting on every scale factor change
    /// would slightly improve phase coherence but this produces audible
    /// artefacts on smooth pitch scale changes (e.g. when using an LFO).
    ///                                       (18.04.2012.) (Domagoj Saric)
    float const scaleFactor(pitchShiftParameters().scale());
    if (channelState.reinitializePhases ||
        ((!equal(channelState.previousScaleFactor,
                 pitchShiftParameters().scale())) && // scale factor changed &&
         (                                           // scale factor is integer
             (truncate(scaleFactor) == scaleFactor) ||
             (truncate(1 / scaleFactor) == 1 / scaleFactor))))
    {
        ReadOnlyDataRange const &inputPhases(data.full().phases());
        multiply(inputPhases, scaleFactor, channelState.phaseSum());
        Detail::AnalysisBinStateData *LE_RESTRICT pBinState(channelState.binData.begin());
        for (auto const inputPhase : inputPhases)
        {
            pBinState->lastPhase = inputPhase;
            ++pBinState;
        }
        channelState.reinitializePhases = false;
    }
    LE_ASSERT(channelState.reinitializePhases == false);
    channelState.previousScaleFactor = scaleFactor;

    analysis(channelState, data.full(), baseParameters());
    pitchShiftAndScale(data, pitchShiftParameters());
    synthesis(channelState, data.full().phases(), baseParameters());
}

////////////////////////////////////////////////////////////////////////////////
//
// Phase vocoder core
//
////////////////////////////////////////////////////////////////////////////////
//
// Papers/information:
// - general:
//  http://hci.rwth-aachen.de/phavorit
//  http://www.hvass-labs.org/people/magnus/schoolwork/pvoc/phasevocoder.pdf
//  http://www.spsc.tugraz.at/sites/default/files/Bachelor%20Thesis%20Gruenwald.pdf
//  http://www.ece.uvic.ca/~peterd/48409/Dobson1999.pdf
//  http://www.ee.columbia.edu/~dpwe/papers/LaroD99-pvoc.pdf
//  http://www.ece.uvic.ca/~peterd/48409/Bernardini.pdf
//  http://recherche.ircam.fr/anasyn/roebel/amt_audiosignale/VL3.pdf
//  http://www.hjortgaard.net/projects/4/download
//  http://ccrma.stanford.edu/~scottl/papers.html
//  http://ccrma.stanford.edu/~jos/mus423h/Peak_Adaptive_Phase_Vocoder.html
//  http://tcts.fpms.ac.be/publications/papers/2011/dafx2011_pvsola_amtd.pdf
//  http://articles.ircam.fr/textes/Roebel05b/index.pdf
//  http://articles.ircam.fr/textes/Roebel10b/index.pdf
//  http://diuf.unifr.ch/main/pai/sites/diuf.unifr.ch.main.pai/files/publications/2008_Juillerat_Schubiger-Banz_Mueller_Enhancing_the_Quality.pdf
//  http://www.dspdimension.com/admin/time-pitch-overview
//  http://www.kvraudio.com/forum/viewtopic.php?p=4398391
//  http://cycling74.com/2006/11/02/the-phase-vocoder-–-part-i
//  http://recherche.ircam.fr/equipes/analyse-synthese/roebel/paper/phase-waspaa03.pdf
//  http://articles.ircam.fr/index.php?Action=ShowArticle&IdArticle=14&ViewType=1
//  http://stackoverflow.com/questions/4633203/extracting-precise-frequencies-from-fft-bins-using-phase-change-between-frames
//  http://music.informatics.indiana.edu/media/students/kyung/kyung_paper.pdf
//  http://www.arc.id.au/ZoomFFT.html
//  MSP tutorial http://cycling74.com/2006/11/02/the-phase-vocoder-–-part-i
//  Signal modifications using the STFT http://recherche.ircam.fr/anasyn/roebel/amt_audiosignale/VL3.pdf
//  Hybrid Time and Frequency Domain Audio Pitch Shifting http://diuf.unifr.ch/pai/people/juillera/PaperComp/AES125/index.html
//  Low Latency Audio Pitch Shifting in the Time Domain https://diuf.unifr.ch/main/pai/sites/diuf.unifr.ch.main.pai/files/publications/2008_Juillerat_Schubiger-Banz_Mueller_Low_Latency.pdf
//
// - phasiness
//  http://dafx04.na.infn.it/WebProc/Proc/P_083.pdf An efficient phasiness reduction technique for moderate audio time-scale modification
//  http://www.ee.columbia.edu/~dpwe/papers/LaroD97-phasiness.pdf
//  http://dafx10.iem.at/proceedings/papers/Roebel_DAFx10_P84.pdf 2010 A shape-invariant phase vocoder for speech transformation
//
// - phase/pitch analysis:
//  http://music.columbia.edu/pipermail/music-dsp/2004-May/060345.html
//  http://music.columbia.edu/pipermail/music-dsp/2004-May/060249.html
//  Accuracy of Frequency Estimates Using the Phase Vocoder:
//  http://academics.wellesley.edu/Physics/brown/pubs/pvocWmiller00661475.pdf
//
// - peak analysis
//  see the links in peakDetector.cpp
//
// - formants:
//  http://en.wikipedia.org/wiki/Formant
//  http://www.dspdimension.com/admin/formants-pitch-shifting
//  http://www.signalsguru.net/projects/formants/formant.html
//  http://person2.sol.lu.se/SidneyWood/praate/whatform.html
//  http://www.akutek.info/research_files/voice_acoustics.htm
//  https://ccrma.stanford.edu/software/stk/classstk_1_1Phonemes.html
//  Voice processing and synthesis by performance sampling and spectral models:
//  http://mtg.upf.edu/static/media/PhD_jbonada.pdf
//  Speech Processing: A Dynamic and Optimization-Oriented Approach
//  http://books.google.hr/books?id=136wRmFT_t8C
//
// - spectral envelope
//  see the links in talkingWindimpl.cpp
//
// - PV-based effect ideas
//  http://www.waveformsoftware.com/PVCX_Help/Main.htm
//  http://www.somasa.qub.ac.uk/~elyon/LyonPapers/Spectral_Tuning.pdf
//  http://jmvalin.ca/papers/valin_hscma2008.pdf
//
//
// Code/libs/examples:
// http://www.mti.dmu.ac.uk/events-conferences/0106nowalls/papers/Dobson.PDF
// http://people.bath.ac.uk/masrwd/pvplugs.html
// http://www.cs.bath.ac.uk/~jpff/NOS-DREAM/researchdev/pvocex/pvocex.html
// http://www.hvass-labs.org/people/magnus/schoolwork.php
// http://mtg.upf.edu/technologies/sms?p=libsms
// http://sethares.engr.wisc.edu/vocoders/matlabphasevocoder.html
// http://www.daimi.au.dk/~pmn/sound
// http://diuf.unifr.ch/pai/people/juillera/PitchTech/Preview.html
// http://sourceforge.net/projects/pvcplus
// http://www.waveformsoftware.com/pvc.zip
// https://ccrma.stanford.edu/software/stk/classstk_1_1PitShift.html
// https://ccrma.stanford.edu/software/stk/classstk_1_1LentPitShift.html
// https://code.google.com/p/pitch-shifting
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// analysis()
// ----------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Phase vocoder analysis phase.
///
/// \throws nothing
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
/// \note Its four call sites are commented out and have been since 2012; it is
/// the check you uncomment when the phase vocoder is misbehaving.
[[maybe_unused]] void verifyDCPhase([[maybe_unused]] float const phase)
{
    // Implementation note:
    //   Valid phases for the DC bin are 0, Pi and - Pi.
    //                                    (04.06.2010.) (Domagoj Saric)
    LE_ASSERT_MSG(phase == 0 || phase == Math::Constants::pi, "Invalid DC phase.");
}

float LE_FORCEINLINE mapTo2PiInterval(float const &phase)
{
    // Implementation note:
    //   We misuse the for-positive-floats modulo because the possible error
    // introduced is irrelevant for phases (i.e. -0.5 Pi is equivalent to
    // 1.5 Pi).
    //                                    (02.12.2010.) (Domagoj Saric)
    float const reducedPhase(Math::PositiveFloats::modulo(phase, Math::Constants::twoPi));
    //LE_ASSERT( Math::nearEqual( sin( reducedPhase ), sin( phase ) ) );
    return reducedPhase;
}

float LE_FORCEINLINE mapToPiInterval(float const phase)
{
    // Phase unwrapping or the so called "princarg" function:
    // http://en.wikipedia.org/wiki/Phase_unwrapping
    // http://www.derekroconnor.net/Software/Ng--ArgReduction.pdf
    // https://ccrma.stanford.edu/~jos/sasp/Matlab_Unwrapping_Spectral_Phase.html
    // http://www.mathworks.com/help/techdoc/ref/unwrap.html
    // http://www.mathworks.com/help/toolbox/dsp/ref/dsp.phaseunwrapperclass.html

    using namespace Math;
    using Math::Constants::twoPi;
    // pi is declared where it is used: only the assertion at the bottom and the
    // disabled princarg versions want it, so naming it here is unused in a
    // build without assertions

#if 0 // version 1 (slower)

        float const phaseIn2Pi( mapTo2PiInterval( phase ) );
        float const intervalAdjustmentValue
        (
            PositiveFloats::valueIfGreater( abs( phaseIn2Pi ), pi, twoPi )
        );
        float const properlySignedIntervalAdjustmentValue
        (
            copySign( intervalAdjustmentValue, phaseIn2Pi )
        );
        float const phaseInPi( phaseIn2Pi - properlySignedIntervalAdjustmentValue );

#elif 0 // version 2 from DAFx, RubberBand... (does not work!?)
    // http://web.uvic.ca/~loganv/ELEC484/Project/PhaseVocoder.pdf
    // http://www.elec.qmul.ac.uk/digitalmusic/audioengineering/transientmodification/transient_shaping_report.pdf
    float const phaseInPi(modulo(phase + pi, -twoPi) + pi);

#elif 1 // version 3
    // http://www.ee.columbia.edu/~dpwe/resources/matlab/pvoc/pvsample.m
    // http://www.music.informatics.indiana.edu/media/students/kyung/kyung_paper.pdf
    float const phaseInPi(phase - round(phase / twoPi) * twoPi);

#else // version 4 (from the aubio library)

    float const phaseInPi(phase + twoPi * (1 + floor(-(phase + pi) / twoPi)));

#endif // princarg implementation

#if defined(_DEBUG)
    using Math::Constants::pi;
    //...mrmlj...temporarily disabled as it seems to fail in iOS builds...
    LE_ASSERT_MSG(
        /// \note Reducing the accumulated synthesis phase for only a subset
        /// of bins per frame even further reduces the precision of this
        /// function because the input values become large.
        ///                               (21.05.2012.) (Domagoj Saric)
        abs(phaseInPi) <= (pi + 0.005), "Phase reduction broken.");
#endif // _DEBUG

    //LE_ASSERT_MSG( abs( sin( phaseInPi ) - sin( phase ) ) < 0.05, "Phase reduction broken." );

    return phaseInPi;
}
} // anonymous namespace

void LE_NOINLINE analysis(AnalysisChannelState &state, Engine::FullChannelData_AmPh &data,
                          BaseParameters const &parameters)
{
    Engine::FullChannelData_AmPh *LE_RESTRICT const pData(&data);
    AnalysisChannelState *LE_RESTRICT const pState(&state);

    float *LE_RESTRICT const pPhases(pData->phases().begin());
    Detail::AnalysisBinStateData *LE_RESTRICT const pBinData(pState->binData.begin());

    // Doing anything to/with the zeroth bin/DC is nonsensical so we explicitly
    // remove it from the working range.
    //...mrmlj...some effects are currently broken and modify the DC bin
    //phase...uncomment this when these get fixed...
    //verifyDCPhase( phaseInAnaFreqOut.front() );

    // Omega_k * Ra (hop size), DAFx, p. 263, eq. 8.42
    float const deviationFactor(parameters.deviationFactor());
    float const normalisedOmega(parameters.freqPerBin() / deviationFactor);

    /// \note
    ///   The following code assumes a zero-phase windowed FFT procedure. See
    /// the notes in the FFT_float_real_1D::fftshift() member function for more
    /// information. For code that worked without this assumption and explicitly
    /// adjusted the phases of odd bins to emulate the zero phase windowed FFT
    /// checkout SVN revision 6079 or earlier.
    ///                                       (17.04.2012.) (Domagoj Saric)

    // Bin 0 skipped:
    std::uint16_t const startBin(1);
    std::uint16_t const endBin(pData->size());
    LE_ASSUME(endBin < 5000);
    LE_ASSUME(endBin > 64);
#if defined(__clang__) &&                                                                          \
    /*...mrmlj...LTO warnings displayed as errors with Xcode7.3*/ !defined(__APPLE__)
#pragma clang loop vectorize(enable) interleave(enable)
#endif // __clang__
    for (auto bin(startBin); bin != endBin; ++bin)
    {
        using namespace Math;
        using Math::PositiveFloats::isGreater;
        using Math::PositiveFloats::valueIfGreater;

        float const binf(bin);

        float *LE_RESTRICT const pPhFq(&pPhases[bin]);
        float *LE_RESTRICT const pLastPhase(&pBinData[bin].lastPhase);

        float const phase(*pPhFq);
        float const lastPhase(*pLastPhase);

        //...mrmlj...reinvestigate this (does not need to hold if there is
        //...mrmlj...another phase modifying effect between the FFT and this PV
        //...mrmlj...instance...
        //LE_ASSERT_MSG
        //(
        //    Math::abs( phase ) <= Math::Constants::pi,
        //    "PV expects input phases in the [-Pi, +Pi] interval."
        //);

        float const phaseDifference(phase - lastPhase);

        float const currentBinNormalisedOmega(binf * normalisedOmega);
        float const deltaPhi(mapToPiInterval(phaseDifference - currentBinNormalisedOmega));
        float const frequency((currentBinNormalisedOmega + deltaPhi) * deviationFactor);

        *pLastPhase = phase;
        *pPhFq = frequency; // store estimated frequency (overwriting the phase data)
    }

    //LE_ASSERT( pPhFq == pData->phases().end() );

    //verifyDCPhase( phaseInAnaFreqOut.front() );
}

////////////////////////////////////////////////////////////////////////////////
//
// synthesis()
// -----------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Phase vocoder synthesis phase.
///
/// \throws nothing
///
////////////////////////////////////////////////////////////////////////////////

namespace
{
float LE_FORCEINLINE reconstructPhase(float const estimatedFrequency, float const binFrequency,
                                      float const invDeviationFactor,
                                      float const expectedPhaseDifference,
                                      float const currentPhaseSum)
{
    float const phase((estimatedFrequency - binFrequency) * invDeviationFactor +
                      expectedPhaseDifference);

    float const phaseSum(currentPhaseSum + phase);
    return phaseSum;
}
} // namespace

void LE_NOINLINE synthesis(SynthesisChannelState &state, DataRange const &anaFreqInSynthPhaseOut,
                           BaseParameters const &parameters)
{
    // Implementation note:
    //   We alter phases only for bins that may have non-zero phase (that is,
    // don't touch bins 0 and Nyquist).
    //                                        (19.05.2010.) (Ivan Dokmanic)

    // Implementation note:
    //   Analysis should have skipped the DC bin so its phase should be zero or
    // Pi. The Nyquist bin may have been altered during pitch shifting
    // operations but its resulting phase must be zero so we set it so
    // explicitly and skip its further processing.
    //                                        (04.06.2010.) (Domagoj Saric)
    //...mrmlj...some effects are currently broken and modify the DC bin
    //phase...uncomment this when these get fixed...
    //verifyDCPhase( anaFreqInSynthPhaseOut.front() );

    float const expctRate(parameters.expctRate());
    float const freqPerBin(parameters.freqPerBin());
    float const invDeviationFactor(parameters.invDeviationFactor());

    // Bin 0 skipped:
    auto const numberOfBins(static_cast<std::uint16_t>(anaFreqInSynthPhaseOut.size() - 1));
    auto counter(numberOfBins);
    float bin(1);

    float *LE_RESTRICT pFqPh(anaFreqInSynthPhaseOut.begin() + 1);
    float *LE_RESTRICT pPhaseSum(state.phaseSum().begin() + 1);

    LE_ASSUME(counter % 2 == 0);
    while (counter--)
    {
        LE_ASSERT(Math::truncate(bin) == bin);
        float const expectedPhaseDifference(bin * expctRate);
        float const currentBinFrequency(bin * freqPerBin);
        float const phaseSum(reconstructPhase(*pFqPh, currentBinFrequency, invDeviationFactor,
                                              expectedPhaseDifference, *pPhaseSum));
        *pFqPh++ = phaseSum;
        *pPhaseSum++ = phaseSum;
        bin++;
    }

    // Implementation note:
    //   Since phaseSum is used only for resynthesis, there is no need to
    // accumulate it endlessly so we do modulo 2Pi addition here to avoid
    // accumulating big numbers (which, with floating point numbers, have low
    // precision/high quantization errors).
    //                                        (19.02.2010.) (Ivan Dokmanic)
    /// \note It is not necessary to reduce the phase of every bin on every
    /// frame so, for greater efficiency, we reduce only a subset of bins per
    /// frame (this idea was taken from Richard Dobson's open-sourced VST
    /// plugins).
    ///                                       (21.05.2012.) (Domagoj Saric)
    {
        std::uint16_t const binsToReduce(numberOfBins / 16);
        // Bin 0 skipped:
        std::uint16_t const startBinToReduce(++state.binToReduce);
        state.binToReduce = (state.binToReduce + binsToReduce - 1) % numberOfBins;
        float *LE_RESTRICT pPhaseToReduce(&state.phaseSum()[startBinToReduce]);
        float *LE_RESTRICT pOutputPhase(&anaFreqInSynthPhaseOut[startBinToReduce]);
        for (std::uint16_t counter(binsToReduce); counter; --counter)
        {
            LE_ASSERT(*pPhaseToReduce == *pOutputPhase);
            float const reducedPhase(mapTo2PiInterval(*pPhaseToReduce));
            *pPhaseToReduce++ = reducedPhase;
            *pOutputPhase++ = reducedPhase;
        }
    }

    //verifyDCPhase( anaFreqInSynthPhaseOut.front() );
    anaFreqInSynthPhaseOut.back() = 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// pitchShiftAndScale()
// --------------------
//
////////////////////////////////////////////////////////////////////////////////

void LE_NOINLINE
pitchShiftAndScale(Engine::ChannelData_AmPh &data,
                   PitchShiftParameters const &pitchShiftParameters) /// \throws nothing
{
    /// \note Workaround for MSVC's lack of support for restricted references.
    ///                                       (26.04.2012.) (Domagoj Saric)
    Engine::ChannelData_AmPh *LE_RESTRICT const pData(&data);

    float const scale(pitchShiftParameters.scale());
    float const scaleInverse(1 / scale);

    float *LE_RESTRICT const amplitudes(pData->amps().begin());
    float *LE_RESTRICT const frequencies(pData->phases().begin());

    using namespace Math;

    /// \note Indexing is used instead of pointer arithmetics because the x86
    /// register file is too small to track all the pointers.
    ///                                       (03.04.2012.) (Domagoj Saric)

    // Shift up (stretching the spectrum):
    if (scale > 1)
    {
        // "Backward propagation", section 9.1 in
        // http://www.hvass-labs.org/people/magnus/schoolwork/pvoc/phasevocoder.pdf
        std::uint16_t const startOutputIndex(pData->size() - 1);
        float floatOutputIndex(convert<float>(startOutputIndex));

        for (;;)
        {
            std::uint16_t const inputIndex(round(floatOutputIndex * scaleInverse));
            std::uint16_t const outputIndex(round(floatOutputIndex));
            --floatOutputIndex;
            if (!inputIndex)
                break;

            LE_ASSERT_MSG(inputIndex < pData->numberOfBins(), "Index out of range.");
            LE_ASSERT_MSG(outputIndex < pData->numberOfBins(), "Index out of range.");

#ifndef __APPLE__
            //...mrmlj...this sometimes fails in SDK builds on Apple platforms...
            LE_ASSERT_MSG(amplitudes[inputIndex] >= 0, "Negative amplitude");
#endif // __APPLE__
            //LE_ASSERT_MSG( frequencies[ inputIndex ] >= 0, "Negative frequency" );//...mrmlj...

            frequencies[outputIndex] = frequencies[inputIndex] * scale;
            amplitudes[outputIndex] = amplitudes[inputIndex];
        }
    }
    else
        // Shift down (compressing the spectrum):
        if (scale < 1)
        {
            // 'Shifting' to/from the DC bin is nonsensical so we explicitly remove
            // it from the working range.
            std::uint16_t const startOutputIndex(1);

            float floatInputIndex(startOutputIndex * scaleInverse);
            float const endInputIndex(convert<float>(pData->size()));

            std::uint16_t lastOutputIndex(startOutputIndex);
            while (floatInputIndex < endInputIndex)
            {
                /// \note The outputIndex conversion has to use truncation,
                /// otherwise "weirdness happens" with small pitch scale amounts
                /// (see
                /// http://www.dspdimension.com/admin/pitch-shifting-using-the-ft
                /// and the "goes even farther away from the correct frequency" part
                /// for a possible explanation).
                ///                               (03.04.2012.) (Domagoj Saric)
                /// \todo The inputIndex conversion has to use truncation to avoid
                /// going one pass the endInputIndex. Reinvestigate and document
                /// these rounding issues...
                ///                               (05.04.2012.) (Domagoj Saric)
                std::uint16_t const inputIndex(truncate(floatInputIndex));
                std::uint16_t const outputIndex(truncate(floatInputIndex * scale));
                ++floatInputIndex;

                LE_ASSERT_MSG(inputIndex < pData->numberOfBins(), "Index out of range.");
                LE_ASSERT_MSG(outputIndex < pData->numberOfBins(), "Index out of range.");

                LE_ASSERT_MSG(amplitudes[inputIndex] >= 0, "Negative amplitude");
                //LE_ASSERT_MSG( frequencies[ inputIndex ] >= 0, "Negative frequency" );

                /// \note If we are writing/moving into the same bin as in the
                /// previous iteration we need to "combine" the frequencies somehow.
                /// Because we cannot easily add them (while working with polar
                /// coordinates) we overwrite the old (previously stored) frequency
                /// if the new one has a larger amplitude.
                ///                               (21.05.2012.) (Domagoj Saric)
                if ((lastOutputIndex != outputIndex) ||
                    (amplitudes[inputIndex] > amplitudes[outputIndex]))
                {
                    frequencies[outputIndex] = frequencies[inputIndex] * scale;
                    amplitudes[outputIndex] = amplitudes[inputIndex];
                }
                lastOutputIndex = outputIndex;
            }

            /// \note Zero the "abandoned" high frequencies.
            ///                                   (03.04.2012.) (Domagoj Saric)
            clear(&amplitudes[lastOutputIndex], pData->amps().end());
        }
        else
        // Skip (scale == 1):
        {
            // Implementation note:
            //   The current algorithm suffers from 'phasiness' artifacts because it
            // expects that the same data will pass through both the analysis and
            // synthesis (i.e. they will start processing at the same time and the
            // data produced by the analysis phase will not be altered before the
            // synthesis phase). This is clearly not the case in real world usage so
            // their channel state data gets out of sync and because of this even
            // when all the controls are reset to 'zero' the sound is still
            // 'mangled' in some way because the synthesis phase no longer functions
            // as the 'perfect inverse' of the analysis phase. This happens due to
            // the accumulated phase errors.
            // As a workaround we detect the 'default'/'zero' condition and skip any
            // processing. We could clear the channel state when the algorithm is
            // reset to defaults (no pitch shifting/scaling) but this would produce
            // no meaningful difference because that would only give the algorithm a
            // different new starting state which, in the current state of the
            // algorithm, does not make a difference precisely because the analysis
            // and synthesis states quickly "get out of sync", due to the individual
            // treatment of each bin, instead of each sinusoidal component.
            //                                    (14.05.2010.) (Domagoj Saric)

            LE_ASSERT(pitchShiftParameters.skipProcessing());
        }
}

} // namespace LE::SW::Effects::PhaseVocoderShared
