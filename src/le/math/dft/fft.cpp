////////////////////////////////////////////////////////////////////////////////
///
/// fft.cpp
/// -------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma runtime_checks("", off)
#pragma check_stack(off)
#endif // MSVC

#include "le/utility/platformSpecifics.hpp"

#include "fft.hpp"

#include <cmath>
#include "le/math/conversion.hpp"
#include "le/math/math.hpp"
#include "le/math/vector.hpp"
#include "le/utility/buffers.hpp"

#include "le/utility/assert.hpp"

// Implementation specific includes.
#ifdef LE_ACC_FFT
#define Point                                                                                      \
    CarbonDummyPointName // (workaround to avoid definition of "Point" by old Carbon headers)
#include <Accelerate/Accelerate.h> //vDSP.h
#undef Point
#else
#include "pffft.h"

#include "le/spectrumworx/engine/configuration.hpp"
#endif // LE_ACC_FFT

namespace LE::Math
{

////////////////////////////////////////////////////////////////////////////////
//
// FFT_float_real_1D::FFT_float_real_1D()
// --------------------------------------
//
////////////////////////////////////////////////////////////////////////////////

FFT_float_real_1D::FFT_float_real_1D() /// \throws nothing
    : size_(0)
#if defined(LE_ACC_FFT) || defined(LE_PFFFT)
      ,
      fftSetup_(nullptr)
#endif // a backend that owns a setup object
{
}

////////////////////////////////////////////////////////////////////////////////
//
// FFT_float_real_1D::~FFT_float_real_1D()
// ---------------------------------------
//
////////////////////////////////////////////////////////////////////////////////

#ifdef LE_ACC_FFT
FFT_float_real_1D::~FFT_float_real_1D() /// \throws nothing
{
    if (fftSetup_)
        vDSP_destroy_fftsetup(fftSetup_);
}
#endif // LE_ACC_FFT

#ifdef LE_PFFFT
FFT_float_real_1D::~FFT_float_real_1D() /// \throws nothing
{
    if (fftSetup_)
        pffft::pffft_destroy_setup(fftSetup_);
}
#endif // LE_PFFFT

#ifdef LE_ACC_FFT
::DSPSplitComplex &FFT_float_real_1D::workBufferSplit() const
{
    static_assert(sizeof(::DSPSplitComplex) == sizeof(FFT_float_real_1D::DSPSplitComplex),
                  "Internal inconsistency");
    static_assert(offsetof(::DSPSplitComplex, realp) ==
                      offsetof(FFT_float_real_1D::DSPSplitComplex, realp),
                  "Internal inconsistency");
    static_assert(offsetof(::DSPSplitComplex, imagp) ==
                      offsetof(FFT_float_real_1D::DSPSplitComplex, imagp),
                  "Internal inconsistency");
    return const_cast<::DSPSplitComplex &>(
        reinterpret_cast<::DSPSplitComplex const &>(workBufferSplit_));
}
#endif // LE_ACC_FFT

////////////////////////////////////////////////////////////////////////////////
//
// FFT_float_real_1D::resize()
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////

void FFT_float_real_1D::resize(SW::Engine::StorageFactors const &factors,
                               SW::Engine::Storage &storage) /// \throws nothing
{
    auto const size(factors.fftSize);

    LE_ASSERT_MSG(size <= LE::SW::Engine::Constants::maximumFFTSize, "FFT size too large.");

    /// \note The work buffer has to be resized (relocated) even if the FFT size
    /// hasn't changed because shared storage might have been reallocated.
    ///                                       (22.02.2013.) (Domagoj Saric)
    workBuffer_.resize(factors, storage);

#if defined(LE_ACC_FFT)
    workBufferSplit_.realp = workBuffer_.begin();
    workBufferSplit_.imagp = workBuffer_.begin() + (workBuffer_.size() / 2);
    LE_ASSERT_MSG((reinterpret_cast<std::size_t>(workBufferSplit_.realp) %
                   Utility::Constants::vectorAlignment) == 0,
                  "Buffer misaligned.");
    LE_ASSERT_MSG((reinterpret_cast<std::size_t>(workBufferSplit_.imagp) %
                   Utility::Constants::vectorAlignment) == 0,
                  "Buffer misaligned.");

    if (size != size_)
    {
        unsigned int const newLog2Size(log2(size));
        FFTSetup const newFFTSetup(::vDSP_create_fftsetup(newLog2Size, kFFTRadix2));
        if (newFFTSetup)
        {
            if (fftSetup_)
                ::vDSP_destroy_fftsetup(fftSetup_);
            fftSetup_ = newFFTSetup;
        }
        else
        {
            LE_ASSERT(!"FFT failure"); /*...mrmlj...proper error handling...*/
        }
    }
#endif // LE_ACC_FFT

#if defined(LE_PFFFT)
    /// \note Order matters: requiredStorage() below promises the sum of the two
    /// buffers, and SharedStorageBuffer::resize() carves them out of the span
    /// in call order.
    scratch_.resize(factors, storage);

    if (size != size_)
    {
        /// pffft only accepts N = (2^a)(3^b)(5^c) with a >= 5. Every size the
        /// engine can ask for is a power of two in [128, 8192], so this holds
        /// by construction — but a setup for a size it rejects would be a null
        /// pointer dereference in process(), so it is checked rather than
        /// assumed.
        LE_ASSERT_MSG(size >= LE::SW::Engine::Constants::minimumFFTSize, "FFT size too small.");
        LE_ASSERT_MSG((size % 32) == 0, "FFT size not a multiple of 32.");
        auto *const newFFTSetup((size >= 32) && ((size % 32) == 0)
                                    ? pffft::pffft_new_setup(size, pffft::PFFFT_REAL)
                                    : nullptr);
        if (newFFTSetup)
        {
            if (fftSetup_)
                pffft::pffft_destroy_setup(fftSetup_);
            fftSetup_ = newFFTSetup;
        }
        else
        {
            LE_ASSERT(!"FFT failure"); /*...mrmlj...proper error handling...*/
        }
    }
#endif // LE_PFFFT

    size_ = size;
}

////////////////////////////////////////////////////////////////////////////////
/// Real DFT
////////////////////////////////////////////////////////////////////////////////

void FFT_float_real_1D::transform(float *LE_RESTRICT const data /*in time, out DFT reals*/,
                                  float *LE_RESTRICT const imaginaryTargetSubRange,
                                  std::uint16_t const size) const
{
    LE_ASSERT(size <= this->size());
#ifdef LE_ACC_FFT
    std::uint16_t const halfSize(size / 2);
    vDSP_ctoz(reinterpret_cast<DSPComplex const *>(data), 2, &workBufferSplit(), 1, halfSize);
    vDSP_fft_zrip(
        fftSetup_, &workBufferSplit(), 1, log2(size),
        FFT_FORWARD); // https://developer.apple.com/library/mac/documentation/Accelerate/Reference/vDSPRef/Reference/reference.html#//apple_ref/c/func/vDSP_fft_zript
    // http://developer.apple.com/library/ios/documentation/Performance/Conceptual/vDSP_Programming_Guide/UsingFourierTransforms/UsingFourierTransforms.html#//apple_ref/doc/uid/TP40005147-CH202-15411
    float const scale(1 / (2 * std::sqrt(convert<float>(size))));
    // http://developer.apple.com/library/ios/documentation/Performance/Conceptual/vDSP_Programming_Guide/UsingFourierTransforms/UsingFourierTransforms.html#//apple_ref/doc/uid/TP40005147-CH202-15398
    multiply(workBufferSplit().realp, scale, data, halfSize);
    multiply(workBufferSplit().imagp + 1, scale, imaginaryTargetSubRange + 1, halfSize - 1);
    data[halfSize] = workBufferSplit().imagp[0] * scale;
    imaginaryTargetSubRange[0] = 0;
    imaginaryTargetSubRange[halfSize] = 0;
#else
    ////////////////////////////////////////////////////////////////////////////
    /// pffft
    ////////////////////////////////////////////////////////////////////////////
    /// The contract this has to meet, which is set by the Accelerate branch
    /// above and by what the 3.6 goldens were rendered through:
    ///
    ///   - `data` arrives holding `size` real time-domain samples and leaves
    ///     holding the `size/2 + 1` real parts, bin 0 (DC) through bin size/2
    ///     (Nyquist).
    ///   - `imaginaryTargetSubRange` receives the matching `size/2 + 1`
    ///     imaginary parts, with bins 0 and size/2 forced to an exact zero —
    ///     they are real by construction, and downstream code reads them.
    ///   - the transform is unitary rather than unnormalised: the result is the
    ///     textbook DFT divided by sqrt(size), which is what makes
    ///     `maximumAmplitude()` = sqrt(size)/2 the 0 dB reference and what makes
    ///     the inverse below its exact undo.
    ///
    /// `pffft_transform_ordered` produces the textbook DFT unscaled, in the
    /// packed real layout: slot 0 holds Re(X[0]) and slot 1 holds Re(X[size/2]),
    /// then interleaved (Re, Im) for bins 1 .. size/2-1. Verified against a
    /// naive DFT, sign convention included, rather than taken from the header
    /// comment.
    ///
    /// The transform runs inside workBuffer_ — pffft wants SIMD-aligned input
    /// and output and several callers hand us interior pointers — and pffft
    /// documents input and output as allowed to alias, so the copy in is the
    /// only one needed.
    LE_ASSERT_MSG(size == this->size(), "A pffft setup is per FFT size.");
    LE_ASSUME(fftSetup_);
    auto *const LE_RESTRICT packed(workBuffer_.begin());
    copy(data, packed, size);
    pffft::pffft_transform_ordered(fftSetup_, packed, packed, scratch_.begin(),
                                   pffft::PFFFT_FORWARD);

    std::uint16_t const halfSize(size / 2);
    /// \note Scaled on the way out rather than on the way in, so that the
    /// rounding matches the Accelerate branch step for step. The two differ by
    /// the factor of two vDSP's real forward transform carries and pffft's does
    /// not.
    float const scale(1 / std::sqrt(convert<float>(size)));
    data[0] = packed[0] * scale;
    imaginaryTargetSubRange[0] = 0;
    for (std::uint16_t bin(1); bin < halfSize; ++bin)
    {
        data[bin] = packed[2 * bin + 0] * scale;
        imaginaryTargetSubRange[bin] = packed[2 * bin + 1] * scale;
    }
    data[halfSize] = packed[1] * scale;
    imaginaryTargetSubRange[halfSize] = 0;
#endif // LE_ACC_FFT
}

void FFT_float_real_1D::inverseTransform(float *LE_RESTRICT const data /*in DFT reals, out time*/,
                                         float const *LE_RESTRICT const imaginarySourceSubRange,
                                         std::uint16_t const size) const
{
    LE_ASSERT(size <= this->size());
#ifdef LE_ACC_FFT
    std::uint16_t const halfSize(size / 2);
    // http://developer.apple.com/library/ios/documentation/Performance/Conceptual/vDSP_Programming_Guide/UsingFourierTransforms/UsingFourierTransforms.html#//apple_ref/doc/uid/TP40005147-CH202-15411
    float const scale(1 / std::sqrt(convert<float>(size)));
    // http://developer.apple.com/library/ios/documentation/Performance/Conceptual/vDSP_Programming_Guide/UsingFourierTransforms/UsingFourierTransforms.html#//apple_ref/doc/uid/TP40005147-CH202-15398
    multiply(data, scale, workBufferSplit().realp, halfSize);
    multiply(imaginarySourceSubRange + 1, scale, workBufferSplit().imagp + 1, halfSize - 1);
    workBufferSplit().imagp[0] = data[halfSize] * scale;
    vDSP_fft_zrip(fftSetup_, &workBufferSplit(), 1, log2(size), FFT_INVERSE);
    vDSP_ztoc(&workBufferSplit(), 1, reinterpret_cast<DSPComplex *>(data), 2, halfSize);
#else
    /// The exact undo of the forward above. pffft's backward transform of an
    /// unscaled forward result gives size * x, so scaling the spectrum by
    /// 1/sqrt(size) on the way in turns the X/sqrt(size) the forward produced
    /// back into x — the same place, and the same order, the Accelerate branch
    /// applies its scale.
    LE_ASSERT_MSG(size == this->size(), "A pffft setup is per FFT size.");
    LE_ASSUME(fftSetup_);
    std::uint16_t const halfSize(size / 2);
    float const scale(1 / std::sqrt(convert<float>(size)));
    auto *const LE_RESTRICT packed(workBuffer_.begin());
    packed[0] = data[0] * scale;
    packed[1] = data[halfSize] * scale;
    for (std::uint16_t bin(1); bin < halfSize; ++bin)
    {
        packed[2 * bin + 0] = data[bin] * scale;
        packed[2 * bin + 1] = imaginarySourceSubRange[bin] * scale;
    }
    pffft::pffft_transform_ordered(fftSetup_, packed, packed, scratch_.begin(),
                                   pffft::PFFFT_BACKWARD);
    copy(packed, data, size);
#endif // LE_ACC_FFT
}

/// \note The two assertions below compared a span's std::size_t against
/// `(size() / 2) + 1`, which is an int -- the signed/unsigned mismatch a
/// `#pragma warning( disable : 4389 )` used to bracket these two functions for
/// MSVC and which GCC reports as -Wsign-compare. Saying which type the bin
/// count is counted in removes the mismatch, and with it the pragma.
void FFT_float_real_1D::transform(float *const timeDomainData, DataRange const &imaginarySubRange,
                                  bool const doFFTShift) const
{
    if (doFFTShift)
        fftshift(timeDomainData);
    LE_ASSERT(imaginarySubRange.size() == static_cast<std::size_t>(size() / 2) + 1);
    transform(timeDomainData, imaginarySubRange.begin(), size());
}

void FFT_float_real_1D::inverseTransform(float *const dftData,
                                         ReadOnlyDataRange const &imaginarySubRange,
                                         bool const doFFTShift) const
{
    LE_ASSERT(imaginarySubRange.size() == static_cast<std::size_t>(size() / 2) + 1);
    inverseTransform(dftData, imaginarySubRange.begin(), size());
    if (doFFTShift)
        fftshift(dftData);
}

////////////////////////////////////////////////////////////////////////////////
/// Complex DFT
////////////////////////////////////////////////////////////////////////////////

/// \note The complex pair is unimplemented on every live backend. Accelerate
/// has always said so here; the NT2 arm that did implement it was reachable only
/// from pure-real-FFT test paths that were never compiled, so no shipped
/// configuration has ever called it and no golden depends on it. Left asserting
/// rather than filled in with pffft, because an untested complex path is worse
/// than an absent one — and note that in a release build the assert is gone and
/// these silently do nothing, which was already true on macOS.
void FFT_float_real_1D::transform([[maybe_unused]] float *LE_RESTRICT const pReals,
                                  [[maybe_unused]] float *LE_RESTRICT const pImags) const
{
    LE_ASSERT(!"Not implemented!");
}

void FFT_float_real_1D::inverseTransform([[maybe_unused]] float *LE_RESTRICT const pReals,
                                         [[maybe_unused]] float *LE_RESTRICT const pImags) const
{
    LE_ASSERT(!"Not implemented!");
}

void FFT_float_real_1D::fftshift(float *const pTimeDomainData) const
{
    /// \note
    ///   Emulate the situation where the zero-th sample is in the middle of the
    /// window. In current SW framework, windows are of the length N and causal,
    /// centered around the bin N/2. If the input sample contains only one
    /// sinusoid, its DFT will be equal to the window DFT shifted to the
    /// sinusoid frequency. If the window is symmetric around the zero-th bin,
    /// then it has zero phase and all the bins affected by this sinusoid will
    /// have the same phase. But if (as in SW) the window is symmetric around
    /// the bin N/2 then the phase of the k-th bin of window DFT is actually
    /// 2*Pi/N * (N/2) * k = k*Pi. This effect is also called "residual phase
    /// rotation" (Handbook of Digital Signal Processing, Elliot, 1987, page
    /// 658).
    ///   For effects sensitive to phase relations between neighbouring bins
    /// (e.g. the phase vocoder and the issue of vertical coherence) it is
    /// advisable that the phase behaves more smoothly, so we compensate this
    /// phase shift with the equivalent of Matlab's fftshift function.
    ///   Note that the window actually has two axes of symmetry, so this may
    /// be less important in the non-zero-padded-case, but it is advised in the
    /// cited paper (Bernardini.pdf).
    ///                                        (21.05.2010.) (Ivan Dokmanic)

    /// \note
    /// It is not clear what should be the center of rotation, the N/2 or N/2+1
    /// bin. According to most, but not all, posts/documents it should be N/2
    /// (i.e. a simple swap of the first and second halves of the signal),
    /// contrary to the centre point for "DFT-even" windows (see the related
    /// note in the calculateWindow() function). Matlab's one-based indexing
    /// further obscures the solution. The aubio and Rubber Band libraries
    /// simply swap the two halves of the signal and this is the approach that
    /// we use.
    /// Additional info on the issue:
    /// http://www.ece.uvic.ca/~peterd/48409/Bernardini.pdf (3.3 FFT centering)
    /// http://www.mathworks.com/matlabcentral/fileexchange/25473-why-use-fftshiftfftfftshiftx-in-matlab-instead-of-fftx
    /// (click Download All in the above page)
    /// http://www.dsprelated.com/dspbooks/sasp/Zero_Phase_Zero_Padding.html
    /// http://www.dsprelated.com/showmessage/41851/1.php
    /// http://www.dsprelated.com/showmessage/122711/1.php
    /// http://www.katjaas.nl/FFToutput/centeredFFT.html
    /// http://www.groupsrv.com/computers/about656453.html
    /// http://groups.google.com/group/comp.dsp/browse_thread/thread/5a2f53f28d12496a
    ///                                       (17.04.2012.) (Domagoj Saric)

    /// \todo This (fftshift) pass does not seem to be necessary when using the
    /// window presum/time aliasing technique (with sinc-ed windows).
    /// Research this more thoroughly...
    ///                                       (24.04.2012.) (Domagoj Saric)

    LE_ASSERT_MSG(size() % 2 == 0, "Even data size expected.");
    float *const pHalfPoint(pTimeDomainData + size() / 2);
    swap(pTimeDomainData, pHalfPoint, pHalfPoint);
}

////////////////////////////////////////////////////////////////////////////////
//
// FFT_float_real_1D::updateMaximumAmplitude()
// -------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// Calculates the maximum amplitude value (absolute amplitude at 0db)
/// corresponding to the current FFT size.
///
////////////////////////////////////////////////////////////////////////////////

float FFT_float_real_1D::maximumAmplitude(float const size) { return std::sqrt(size) / 2; }

std::uint32_t FFT_float_real_1D::requiredStorage(SW::Engine::StorageFactors const &factors)
{
    return WorkBuffer::requiredStorage(factors)
#ifdef LE_PFFFT
           + ScratchBuffer::requiredStorage(factors)
#endif // LE_PFFFT
        ;
}

} // namespace LE::Math
