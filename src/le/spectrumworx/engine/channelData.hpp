////////////////////////////////////////////////////////////////////////////////
///
/// \file channelData.hpp
/// ---------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef channelData_hpp__D601616B_6FED_492A_BB15_73E6FBBCBB22
#define channelData_hpp__D601616B_6FED_492A_BB15_73E6FBBCBB22
//------------------------------------------------------------------------------
#include "channelDataAmPh.hpp"
#include "channelDataReIm.hpp"

#include "le/utility/buffers.hpp"

#include <cstdint>

namespace LE
{

namespace Math
{
class FFT_float_real_1D;
} // namespace Math

namespace SW::Engine
{

////////////////////////////////////////////////////////////////////////////////
///
/// \struct ChannelData_AmPh2ReIm
///
///   A small proxy struct holding references to channel data in both domains.
/// Intended for effects that must perform calculations in both domains to
/// reduce the number of conversions they must make. It is important to note
/// that only the AmPh data is valid on input.
///
////////////////////////////////////////////////////////////////////////////////

struct ChannelData_AmPh2ReIm
{
    MainSideChannelData_AmPh const input;
    ChannelData_ReIm output;
}; // struct ChannelData_AmPh2ReIm

////////////////////////////////////////////////////////////////////////////////
///
/// \struct ChannelData_ReIm2AmPh
///
/// \brief Same as ChannelData_AmPh2ReIm only with the AmPh and ReIm data roles
/// switched.
///
////////////////////////////////////////////////////////////////////////////////

struct ChannelData_ReIm2AmPh
{
    MainSideChannelData_ReIm const input;
    MainSideChannelData_AmPh output;
}; // struct ChannelData_ReIm2AmPh

////////////////////////////////////////////////////////////////////////////////
///
/// \class ChannelData
///
/// \brief Holds both DFT and AmPh domain data and provides automatic conversion
/// to either of those with automatic tracking and updating/synchronization
/// between the two (e.g. if one effect uses and modifies DFT data and the
/// effect after it requests AmPh data, the AmPh data will be automatically
/// updated/recreated from the latest DFT data before passing it to the
/// effect).
///
////////////////////////////////////////////////////////////////////////////////
/// \todo When porting this to the Mac check the old/Alex's Mac code in
/// ChannelProcessor::process() member function (chanelProcessor.cpp SVN
/// revision 636).
///                                           (15.01.2010.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////
// Implementation notes:
//   For performance reasons the setNewTimeDomainData() does not clear the
// side channel data when passed NULL for side channel time domain data (it only
// asserts that it was done manually/from outside beforehand) to avoid doing it
// on every call to the plugin's main process call. Rather this must be done
// manually when side channel data 'disappears'.
//                                            (15.01.2010.) (Domagoj Saric)
//
//   To emphasize the fact that side channel data should be considered read-only
// by effects and to prevent erroneous modifications the respective buffers
// are declared as const. This in turn required some unhappy const_casting in
// this class. A cleaner solution should be devised in due time.
//                                            (18.01.2010.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

class ChannelData
{
  public:
    ChannelData();

    void setNewTimeDomainData(float const *mainChannel, float const *sideChannel,
                              Math::FFT_float_real_1D const &fft, ReadOnlyDataRange const &window);

    float const *getNewTimeDomainData(Math::FFT_float_real_1D const &, bool fftShift);

    void clearSideChannelData();

    bool sourceTimeDomainDataWasConsumed() const { return dftDataFreshness_ != 0; }

    FullChannelData_AmPh const &currentAmPhData() const
    {
        return const_cast<ChannelData &>(*this).currentAmPhData();
    }
    FullChannelData_ReIm const &currentReImData() const
    {
        return const_cast<ChannelData &>(*this).currentReImData();
    }

    FullMainSideChannelData_AmPh &freshAmPhData(bool saveForDryWetBlending);
    FullMainSideChannelData_ReIm &freshReImData(bool saveForDryWetBlending);

    using AmPhReImData = std::pair<FullMainSideChannelData_AmPh &, FullMainSideChannelData_ReIm &>;
    AmPhReImData freshAmPh2ReImData(bool saveForDryWetBlending);
    AmPhReImData freshReIm2AmPhData(bool saveForDryWetBlending);

    void blendWithPreviousData(float currentDataWeight, bool amPh2ReIm);
    void amplifyCurrentData(float gain);

  private:
    ////////////////////////////////////////////////////////////////////////////
    /// \class InPlaceDFTBuffer
    ///
    /// A helper buffer class that abstracts an FFT implementation detail: the
    /// internal (ACML) implementation provides only in-place operation for real
    /// FFTs. This forces the user to either create temporary buffers, when
    /// destroying the source data is not allowed, or, more importantly, to keep
    /// track of which data is actually present in the buffer (time- or
    /// frequency- domain). This class tries to solve the second problem by
    /// asserting correct usage (access of correct data) by providing different
    /// access functions for different domain data.
    ///
    /// ...mrmlj...we no longer use ACML...clean this up...
    ///
    ////////////////////////////////////////////////////////////////////////////

    class InPlaceDFTBuffer : private FullMainSideChannelData_ReIm
    {
      public:
        float *timeDomainData();
        FullMainSideChannelData_ReIm &dftData();

        void setToDFTDomain();
        void setToTimeDomain();

      private:
        bool isDFTDomainDataValid() const;
        bool isTimeDomainDataValid() const { return !isDFTDomainDataValid(); }

#ifndef NDEBUG
      private:
        bool dataIsDFTDomain_;
#endif // NDEBUG

      public:
        using FullMainSideChannelData_ReIm::requiredStorage;
        using FullMainSideChannelData_ReIm::resize;
        using FullMainSideChannelData_ReIm::size;
    };

  private:
    static void time2DFT(float const *pInputData, FullChannelData_ReIm &dftData,
                         ReadOnlyDataRange const &window, Math::FFT_float_real_1D const &fft);

    static void dft2AmPh(FullChannelData_ReIm const &input, FullChannelData_AmPh &output);

    static void amph2DFT(FullChannelData_AmPh const &input, FullChannelData_ReIm &output);

    void updateAmPhData();
    void updateReImData();

    void saveCurrentReImDataForBlending();

    std::uint16_t numberOfBins() const { return amphData_.main().numberOfBins(); }
    std::uint16_t halfFFTSize() const { return numberOfBins() - 1; }
    std::uint16_t fftSize() const { return halfFFTSize() * 2; }

    FullMainSideChannelData_AmPh &amphData() { return amphData_; }
    FullMainSideChannelData_ReIm &dftData() { return dftAndTimeData_.dftData(); }

    FullChannelData_AmPh &currentAmPhData() { return amphData().main(); }
    FullChannelData_ReIm &currentReImData() { return dftData().main(); }

    bool reImDataIsFresh() const { return dftDataFreshness_ >= amphDataFreshness_; }

  private:
    std::uint32_t amphDataFreshness_;
    std::uint32_t dftDataFreshness_;

    FullMainSideChannelData_AmPh amphData_;
    InPlaceDFTBuffer dftAndTimeData_;

  public:
    static std::uint32_t requiredStorage(StorageFactors const &);

    void resize(StorageFactors const &, Storage &);
}; // class ChannelData

} // namespace SW::Engine

} // namespace LE

#endif // channelData_hpp
