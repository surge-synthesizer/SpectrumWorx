////////////////////////////////////////////////////////////////////////////////
///
/// \file conversion.hpp
/// --------------------
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef conversion_hpp__D017E02E_44C0_40FE_BC6B_4493C53BF028
#define conversion_hpp__D017E02E_44C0_40FE_BC6B_4493C53BF028
//------------------------------------------------------------------------------
#include "tag.hpp"

#include "le/math/conversion.hpp"
#include "le/math/math.hpp"

namespace LE::Parameters::Detail
{

template <typename Source, int sourceRangeOffset, unsigned int sourceRangeSize,
          unsigned int sourceRangeScaleFactor, class Parameter, typename Target>
Target convertLinearValueToParameterValue(Source const sourceValue, BooleanParameterTag)
{
    int const minimum(sourceRangeOffset);
    int const maximum(sourceRangeOffset + sourceRangeSize);

    if ((minimum == false) && (maximum == true) && (sourceRangeScaleFactor == 1))
        return Math::convert<Target>(sourceValue);

    return Math::PositiveFloats::isGreater(
        sourceValue, (sourceRangeOffset + (static_cast<Source>(sourceRangeSize) / 2)) /
                         static_cast<Source>(sourceRangeScaleFactor));
}

template <typename Source, int sourceRangeOffset, unsigned int sourceRangeSize,
          unsigned int sourceRangeScaleFactor, class Parameter, typename Target>
Target convertLinearValueToParameterValue(bool const sourceValue, BooleanParameterTag)
{
    return sourceValue;
}

template <typename Target, int targetRangeOffset, unsigned int targetRangeSize,
          unsigned int targetRangeScaleFactor, class Parameter, typename Source>
Target convertParameterValueToLinearValue(Source const sourceValue, BooleanParameterTag)
{
    LE_ASSERT(sourceValue == 0 || sourceValue == 1);

    int const minimum(targetRangeOffset);
    int const maximum(targetRangeOffset + targetRangeSize);

    // Implementation note:
    //   This produces the smallest code with MSVC.
    //                                    (15.03.2011.) (Domagoj Saric)
    if ((minimum == false) && (maximum == true) && (targetRangeScaleFactor == 1))
        return sourceValue;

    if (sourceValue)
        return minimum / static_cast<Target>(targetRangeScaleFactor);
    else
        return maximum / static_cast<Target>(targetRangeScaleFactor);
}

} // namespace LE::Parameters::Detail

#endif // conversion_hpp
