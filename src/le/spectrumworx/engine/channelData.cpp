////////////////////////////////////////////////////////////////////////////////
///
/// channelData.cpp
/// ---------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "channelData.hpp"

#ifndef NDEBUG
#include "le/math/math.hpp"
#else //...mrmlj...
#ifndef LE_MATH_VERIFY_VALUES
#define LE_MATH_VERIFY_VALUES(fpClasses, range, valueName)
#endif // LE_MATH_VERIFY_VALUES
#endif // NDEBUG
#include "le/math/conversion.hpp"
#include "le/math/dft/domainConversion.hpp"
#include "le/math/dft/fft.hpp"
#include "le/math/vector.hpp"
#include "le/utility/platformSpecifics.hpp"

#include "le/utility/assert.hpp"

namespace LE::SW::Engine
{

ChannelData::ChannelData() : amphDataFreshness_(0), dftDataFreshness_(0) {}

void ChannelData::setNewTimeDomainData(float const *const mainChannel,
                                       float const *const sideChannel,
                                       Math::FFT_float_real_1D const &fft,
                                       ReadOnlyDataRange const &window)
{
    amphDataFreshness_ = 0;
    dftDataFreshness_ = 0;
    LE_ASSERT(fftSize() == fft.size());

    dftAndTimeData_.setToDFTDomain();
    time2DFT(mainChannel, dftData().main(), window, fft);
    dftDataFreshness_ = 1;

    if (sideChannel)
    {
        time2DFT(sideChannel, dftData().mutableSide(), window, fft);

        dft2AmPh(dftData().mutableSide(), amphData().mutableSide());
    }
#if 0  //...mrmlj...synth...
    else
    {
        LE_ASSERT_MSG
        (
            Math::max( amphData_.side().jointView() ) == 0,
            "Sidechain data not cleared."
        );
    }
#endif // disabled

#ifndef NDEBUG
    auto const sideRealsSize(dftData().side().reals().size());
    auto const sideImagsSize(dftData().side().imags().size());
    auto const mainRealsSize(dftData().main().reals().size());
    LE_ASSERT_MSG(sideRealsSize == sideImagsSize, "Buffer sizes mismatched.");
    LE_ASSERT_MSG(sideRealsSize == mainRealsSize, "Buffer sizes mismatched.");
#endif // NDEBUG
}

float const *ChannelData::getNewTimeDomainData(Math::FFT_float_real_1D const &fft,
                                               bool const fftShift)
{
    updateReImData();
    ReadOnlyDataRange const &imaginarySubRange(dftData().main().imags());
    dftAndTimeData_.setToTimeDomain();
    fft.inverseTransform(dftAndTimeData_.timeDomainData(), imaginarySubRange, fftShift);
    return dftAndTimeData_.timeDomainData();
}

void ChannelData::clearSideChannelData()
{
    dftAndTimeData_.setToDFTDomain();
    Math::clear(amphData().mutableSide().jointView());
    Math::clear(dftData().mutableSide().jointView());
}

void ChannelData::time2DFT(float const *const pInputData, FullChannelData_ReIm &dftData,
                           ReadOnlyDataRange const &window, Math::FFT_float_real_1D const &fft)
{
    auto const frameSize(fft.size());

    LE_ASSERT_MSG(frameSize < dftData.size() * 2, "Buffer size incorrect.");

    /// \note The magnitude bound as well as the finiteness one -- this is the
    /// frame the FFT is about to be handed, and a huge finite value passes every
    /// check here and both of the ones on the transform's output, becoming a NaN
    /// only when squared inside the amplitude conversion. \see
    /// Math::hundredDecibels.
    LE_MATH_VERIFY_VALUES(Math::ImplausibleAudio,
                          ReadOnlyDataRange(pInputData, pInputData + fft.size()), "time domain");
    LE_MATH_VERIFY_VALUES(Math::ImplausibleAudio, window, "window");

    float *const windowedTimeData(dftData.jointView().begin());

    Math::multiply(pInputData, &window[0], windowedTimeData, frameSize);

    fft.transform(windowedTimeData, dftData.imags(), true /*FFT shift*/);

    LE_MATH_VERIFY_VALUES(Math::InvalidOrSlow, dftData.reals(), "FFT reals");
    LE_MATH_VERIFY_VALUES(Math::InvalidOrSlow, dftData.imags(), "FFT imags");
}

void ChannelData::dft2AmPh(FullChannelData_ReIm const &reImData, FullChannelData_AmPh &amPhData)
{
    using namespace Math;

    LE_MATH_VERIFY_VALUES(InvalidOrSlow, reImData.reals(), "reals");
    LE_MATH_VERIFY_VALUES(InvalidOrSlow, reImData.imags(), "imags");

    reim2AmPh(reImData.reals().begin(), reImData.imags().begin(), amPhData.amps().begin(),
              amPhData.phases().begin(), amPhData.numberOfBins());

    LE_MATH_VERIFY_VALUES(InvalidOrSlow | Negative, amPhData.amps(), "amplitudes");
    LE_MATH_VERIFY_VALUES(InvalidOrSlow, amPhData.phases(), "phases");

    //...mrmlj...need not hold if input data is amplified/out of range...fix this...
    //LE_ASSERT_MSG
    //(
    //    max( amPhData.amps() ) <= FFT_float_real_1D::maximumAmplitude( convert<float>( ( amPhData.numberOfBins() - 1 ) * 2 ) ),
    //    "FFT reports wrong maximum amplitude."
    //);
}

void ChannelData::amph2DFT(FullChannelData_AmPh const &amPhData, FullChannelData_ReIm &reImData)
{
    using namespace Math;

    LE_MATH_VERIFY_VALUES(InvalidOrSlow | Negative, amPhData.amps(), "amplitudes");
    LE_MATH_VERIFY_VALUES(InvalidOrSlow, amPhData.phases(), "phases");

    amph2ReIm(amPhData.amps().begin(), amPhData.phases().begin(), reImData.reals().begin(),
              reImData.imags().begin(), amPhData.numberOfBins());

    LE_MATH_VERIFY_VALUES(InvalidOrSlow, reImData.reals(), "reals");
    LE_MATH_VERIFY_VALUES(InvalidOrSlow, reImData.imags(), "imags");
}

void ChannelData::updateAmPhData()
{
    LE_MATH_VERIFY_VALUES(Math::InvalidOrSlow, dftData().main().reals(), "reals");
    LE_MATH_VERIFY_VALUES(Math::InvalidOrSlow, dftData().main().imags(), "imags");

    if (amphDataFreshness_ < dftDataFreshness_)
    {
        dft2AmPh(dftData().main(), amphData().main());
        amphDataFreshness_ = dftDataFreshness_;
    }
    else
    {
        LE_MATH_VERIFY_VALUES(Math::InvalidOrSlow | Math::Negative, amphData().main().amps(),
                              "amplitudes");
        LE_MATH_VERIFY_VALUES(Math::InvalidOrSlow, amphData().main().phases(), "phases");
    }
}

void ChannelData::updateReImData()
{
    if (dftDataFreshness_ < amphDataFreshness_)
    {
#if defined(__ANDROID__) &&                                                                        \
    defined(__i386__) //...mrmlj...TalkingWind...even though denormals are disabled!?
        LE_MATH_VERIFY_VALUES(Math::Invalid | Math::Negative, amphData().main().amps(),
                              "amplitudes");
#else
        // Commented out due to customer complaint. (2016-05-18) (Danijel Domazet)
        // LE_MATH_VERIFY_VALUES( Math::InvalidOrSlow | Math::Negative, amphData().main().amps  (), "amplitudes" );
#endif // __ANDROID__
        LE_MATH_VERIFY_VALUES(Math::InvalidOrSlow, amphData().main().phases(), "phases");

        amph2DFT(amphData().main(), dftData().main());
        dftDataFreshness_ = amphDataFreshness_;
    }
}

void ChannelData::saveCurrentReImDataForBlending()
{
    Math::copy(currentReImData().jointView(), currentAmPhData().jointView());
}

FullMainSideChannelData_AmPh &ChannelData::freshAmPhData(bool const saveForDryWetBlending)
{
    updateAmPhData();
    if (saveForDryWetBlending)
    {
        updateReImData();
    }
    ++amphDataFreshness_;
    return amphData();
}

FullMainSideChannelData_ReIm &ChannelData::freshReImData(bool const saveForDryWetBlending)
{
    updateReImData();
    if (saveForDryWetBlending)
    {
        saveCurrentReImDataForBlending();
    }
    ++dftDataFreshness_;
    return dftData();
}

ChannelData::AmPhReImData ChannelData::freshAmPh2ReImData(
    bool /*saveForDryWetBlending...see the amPh2ReIm blend quick fix in ModuleDSP::process*/)
{
    updateAmPhData();
    dftDataFreshness_ = amphDataFreshness_ + 1;
    return AmPhReImData(amphData(), dftData());
}

ChannelData::AmPhReImData ChannelData::freshReIm2AmPhData(bool /*saveForDryWetBlending*/)
{
    updateReImData();
    amphDataFreshness_ = dftDataFreshness_ + 1;
    return AmPhReImData(amphData(), dftData());
}

void ChannelData::blendWithPreviousData(float const currentDataWeight, bool const amPh2ReIm)
{
    if (!amPh2ReIm && reImDataIsFresh())
    {
        DataRange const wetData(currentReImData().jointView());
        ReadOnlyDataRange const dryData(currentAmPhData().jointView());
        Math::mix(wetData.begin(), dryData.begin(), wetData.begin(), wetData.end(),
                  currentDataWeight);
    }
    else
    {
        FullChannelData_AmPh const &amphData(currentAmPhData());
        FullChannelData_ReIm &reImData(currentReImData());
        Math::mix(amphData.amps(), amphData.phases(), reImData.reals(), reImData.imags(),
                  amPh2ReIm ? (1 - currentDataWeight) : currentDataWeight);
        /// \note The mixed data is stored as ReIm data so dftDataFreshness_ has
        /// to be updated to reflect this.
        ///                                   (29.05.2012.) (Domagoj Saric)
        dftDataFreshness_ = amphDataFreshness_ + 1;
    }
    LE_ASSERT(dftDataFreshness_ > amphDataFreshness_);
}

void ChannelData::amplifyCurrentData(float const gain)
{
    DataRange const data(reImDataIsFresh() ? currentReImData().jointView()
                                           : currentAmPhData().amps());
    Math::multiply(data, gain);
}

std::uint32_t ChannelData::requiredStorage(StorageFactors const &factors)
{
    return FullMainSideChannelData_AmPh::requiredStorage(factors) +
           InPlaceDFTBuffer ::requiredStorage(factors);
}

void ChannelData::resize(StorageFactors const &factors, Storage &storage)
{
    amphData_.resize(factors, storage);
    dftAndTimeData_.resize(factors, storage);
#ifndef NDEBUG
    // Required for "no negative amps assertions".
    Math::clear(amphData_.main().amps());
#endif // NDEBUG
}

float *ChannelData::InPlaceDFTBuffer::timeDomainData()
{
    LE_ASSERT_MSG(isTimeDomainDataValid(), "Incorrect usage or buffer in wrong state.");
    return this->main().jointView().begin();
}

FullMainSideChannelData_ReIm &ChannelData::InPlaceDFTBuffer::dftData()
{
    LE_ASSERT_MSG(isDFTDomainDataValid(), "Incorrect usage or buffer in wrong state.");
    return *this;
}

#pragma warning(push)
#pragma warning(disable : 4702) // Unreachable code.
bool ChannelData::InPlaceDFTBuffer::isDFTDomainDataValid() const
{
#ifdef NDEBUG
    LE_UNREACHABLE_CODE();
    return false;
#else
    return dataIsDFTDomain_;
#endif // NDEBUG
}
#pragma warning(pop)

void ChannelData::InPlaceDFTBuffer::setToDFTDomain()
{
#ifndef NDEBUG
    dataIsDFTDomain_ = true;
#endif // NDEBUG
}

void ChannelData::InPlaceDFTBuffer::setToTimeDomain()
{
#ifndef NDEBUG
    dataIsDFTDomain_ = false;
#endif // NDEBUG
}

} // namespace LE::SW::Engine
