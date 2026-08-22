////////////////////////////////////////////////////////////////////////////////
///
/// \file fft.hpp
/// -------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef fft_hpp__3EFDE32C_A81E_4A2B_9BDB_252EC2DD74ED
#define fft_hpp__3EFDE32C_A81E_4A2B_9BDB_252EC2DD74ED
//------------------------------------------------------------------------------
#include "le/spectrumworx/engine/buffers.hpp"

/// \note Two backends, one interface. Apple keeps Accelerate/vDSP, which is
/// what the stage 3.6 goldens were rendered through; everywhere else uses
/// pffft, replacing the NT2 `static_fft` that stage 0.3 retained as reference
/// material only. The two are held to the same layout and the same
/// normalisation — see the notes over the transform pair in fft.cpp.
///
/// \note `-DSW_FORCE_PFFFT=ON` builds the pffft branch on Apple too. It exists
/// for one question, and answered it: when a golden drifts between this machine
/// and another platform, is that the FFT backend or the platform? Nothing else
/// can separate the two, because every non-Apple build changes both at once.
/// Asked of the 05.08.2026 Windows drift, it said "the platform" — 463 of 464
/// fixtures held across backends on one machine where 455 held across the
/// platform boundary, and the eight Ethereal fixtures that fail on Windows all
/// pass here. Not a shipping configuration: the goldens are minted against
/// Accelerate on Apple and this renders them differently on purpose.
#if defined(__APPLE__) && !defined(SW_FORCE_PFFFT)
#define LE_ACC_FFT
#else
#define LE_PFFFT
#endif

#ifdef LE_ACC_FFT
typedef struct OpaqueFFTSetup *FFTSetup;
typedef unsigned long vDSP_Length;
struct DSPSplitComplex;
#endif // LE_ACC_FFT

#ifdef LE_PFFFT
/// \note pffft.h wraps its C declarations in `namespace pffft`, so the setup
/// type is ::pffft::PFFFT_Setup and can be forward declared — nothing that
/// includes this header has to see pffft.h.
namespace pffft
{
struct PFFFT_Setup;
} // namespace pffft
#endif // LE_PFFFT

#include <cstdint>
#include "le/utility/span.hpp"

namespace LE::Math
{

//...mrmlj...cleanup these duplicated typedefs (also in effects.hpp)...
typedef LE::Utility::Span<float> DataRange;
typedef LE::Utility::Span<float const> ReadOnlyDataRange;

////////////////////////////////////////////////////////////////////////////////
///
/// \class FFT_float_real_1D
///
/// \brief Performs single precision 1D forward and inverse FFT on real data.
///
////////////////////////////////////////////////////////////////////////////////
// Implementation note:
//   Check revision 5921 for the last version containing the ACML based
// implementation. Other alternative implementations can be found in the SVN
// revision 746 of this module.
//                                            (24.02.2012.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

class FFT_float_real_1D
{
  public:
    FFT_float_real_1D();
#if defined(LE_ACC_FFT) || defined(LE_PFFFT)
    ~FFT_float_real_1D();
#endif // a backend that owns a setup object

    // real
    void transform(float *data /*inplace: in time     , out DFT reals*/,
                   DataRange const &imaginaryTargetSubRange, bool doFFTShift) const;
    void inverseTransform(float *data /*inplace: in DFT reals, out time     */,
                          ReadOnlyDataRange const &imaginarySourceSubRange, bool doFFTShift) const;

    void transform(float *data /*inplace: in time     , out DFT reals*/,
                   float *imaginaryTargetSubRange, std::uint16_t size) const;
    void inverseTransform(float *data /*inplace: in DFT reals, out time     */,
                          float const *imaginarySourceSubRange, std::uint16_t size) const;

    // complex
    void transform(float *pReals, float *pImags) const;
    void inverseTransform(float *pReals, float *pImags) const;

    void resize(SW::Engine::StorageFactors const &factors, SW::Engine::Storage &);

    std::uint16_t size() const { return size_; } //...mrmlj...actually "maximum allowed size"...

    static float maximumAmplitude(float size);

  private:
#if defined(LE_ACC_FFT)
    ::DSPSplitComplex &workBufferSplit() const;
#endif // LE_ACC_FFT

    void fftshift(float *pTimeDomainData) const;

  private:
    std::uint16_t size_;

#if defined(LE_ACC_FFT)
    FFTSetup fftSetup_;

    struct DSPSplitComplex
    {
        float *realp;
        float *imagp;
    };
    DSPSplitComplex workBufferSplit_;
    typedef SW::Engine::DoubleFFTBuffer<> WorkBuffer;
#elif defined(LE_PFFFT)
    pffft::PFFFT_Setup *fftSetup_;

    /// \note pffft transforms N contiguous floats into its own packed layout,
    /// so the transform runs inside workBuffer_ and is (de)interleaved out of
    /// it rather than out of the caller's buffers. That also means neither
    /// caller-supplied pointer has to carry pffft's SIMD alignment, which not
    /// all of them do.
    typedef SW::Engine::FFTBuffer<> WorkBuffer;
    /// pffft's own scratch. Passing nullptr makes it use the stack instead,
    /// which at the maximum FFT size is 32 KB on the audio thread.
    typedef SW::Engine::FFTBuffer<> ScratchBuffer;
    mutable ScratchBuffer scratch_;
#else
#error No FFT backend selected
#endif // FFT implementation

    mutable WorkBuffer workBuffer_;

  public:
    static std::uint32_t requiredStorage(SW::Engine::StorageFactors const &);
}; // class FFT_float_real_1D

} // namespace LE::Math

#endif // fft_hpp
