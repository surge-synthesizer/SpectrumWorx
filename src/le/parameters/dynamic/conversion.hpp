////////////////////////////////////////////////////////////////////////////////
///
/// \file dynamic/conversion.hpp
/// ----------------------------
///
/// Copyright (c) 2011 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef conversion_hpp__616A36CA_66D2_41CD_B907_0BA22A99E3EA
#define conversion_hpp__616A36CA_66D2_41CD_B907_0BA22A99E3EA
//------------------------------------------------------------------------------
#include "tag.hpp"

#include "le/math/conversion.hpp"

namespace LE::Parameters::Detail
{

template <typename Source, int sourceRangeOffset, unsigned int sourceRangeSize,
          unsigned int sourceRangeScaleFactor, class Parameter, typename Target>
Target convertLinearValueToParameterValue(Source const sourceValue, DynamicRangeParameterTag)
{
    if (sourceRangeSize == 0) //...mrmlj...au/non-normalised automation parameters...
        return Math::convert<Target>(sourceValue);

    return Math::convertLinearRange<Target, Source, sourceRangeOffset, sourceRangeSize,
                                    sourceRangeScaleFactor>(sourceValue, Parameter::minimum(),
                                                            Parameter::maximum());
}

template <typename Target, int targetRangeOffset, unsigned int targetRangeSize,
          unsigned int targetRangeScaleFactor, class Parameter>
Target convertParameterValueToLinearValue(typename Parameter::value_type const sourceValue,
                                          DynamicRangeParameterTag)
{
    if (targetRangeSize == 0) //...mrmlj...au/non-normalised automation parameters...
        return Math::convert<Target>(sourceValue);

    return Math::convertLinearRange<Target, targetRangeOffset, targetRangeSize,
                                    targetRangeScaleFactor>(sourceValue, Parameter::minimum(),
                                                            Parameter::maximum());
}

} // namespace LE::Parameters::Detail

#endif // conversion_hpp
