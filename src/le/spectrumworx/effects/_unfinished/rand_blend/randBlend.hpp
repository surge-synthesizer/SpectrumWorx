////////////////////////////////////////////////////////////////////////////////
///
/// \file randBlend.hpp
/// -------------------
///
/// Copyright (c) 2009. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef randblend_hpp__5589E986_987D_4CAE_BC67_5D2289C5536E
#define randblend_hpp__5589E986_987D_4CAE_BC67_5D2289C5536E
//------------------------------------------------------------------------------
#include "../algorithms.hpp"
#include "../../parameters/parameters.hpp"
#include "common/buffers.hpp"

namespace LE::Algorithms
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class RandBlend
///
/// \ingroup Algorithms RC
///
/// \brief Blends randomly selected frequency bands.
///
////////////////////////////////////////////////////////////////////////////////

class RandBlend
{
  public: // LE::Algorithm required interface.
    ////////////////////////////////////////////////////////////////////////////
    // Parameters
    ////////////////////////////////////////////////////////////////////////////

    DISCRETE_VALUES_PARAMETER(Mode, (Blend)(BlendInv)(Replace));

    DEFINE_PARAMETERS(
        ((Mode))
        //( ( Amount )( float        )( MinimumValue<0> )( MaximumValue< 10> )( DefaultValue<  3> )( RangeValuesDenominator<10> ) )
        ((Amount)(float)(MinimumValue<0>)(MaximumValue<100>)(DefaultValue<30>)(DisplayValueSuffix<
                                                                               '%'>))((
            Range)(unsigned int)(MinimumValue<0>)(MaximumValue<100>)(DefaultValue<
                                                                     50>)(DisplayValueSuffix<'%'>)));

    ////////////////////////////////////////////////////////////////////////////
    // setup() and process()
    ////////////////////////////////////////////////////////////////////////////

    void setup(EngineSetup const &, Parameters const &);
    void process(ChannelData_AmPh &) const;

  public: // Algorithm traits.
    static bool const canUseTwoInputs = false;

  public:
    static char const title[];
    static char const description[];

  private:
    unsigned int num_bins_;
    unsigned int range_;
    float amount_;
    Mode::Value mode_;
};

} // namespace LE::Algorithms

#endif // randBlend_hpp
