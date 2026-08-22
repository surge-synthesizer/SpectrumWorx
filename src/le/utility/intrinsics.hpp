////////////////////////////////////////////////////////////////////////////////
///
/// \file intrinsics.hpp
/// --------------------
///
/// Copyright (c) 2012 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef intrinsics_hpp__B8B5626D_0778_4184_BA27_3999746AEA17
#define intrinsics_hpp__B8B5626D_0778_4184_BA27_3999746AEA17
//------------------------------------------------------------------------------
// LE_HAS_SSE1 decides what SIMDVector *is*, so a translation unit that misses it
// disagrees about the layout of everything holding one -- silently. Included
// rather than left to the force-include for that reason alone.
#include "le/build/leConfigurationAndODRHeader.h"

#if defined(_XBOX)
#include "ppcintrinsics.h"
#include "vectorintrinsics.h"
#elif defined(__ARM_NEON__) || defined(__aarch64__)
#include "arm_neon.h"
#else
#include "platformSpecifics.hpp"
#if defined(_MSC_VER)
#include "intrin.h"
#endif // _MSC_VER
#ifdef LE_HAS_SSE1
#include "xmmintrin.h"
#endif // LE_HAS_SSE1
#ifdef LE_HAS_SSE2
#include "emmintrin.h"
#endif // LE_HAS_SSE1
#endif

namespace LE::Utility
{

#if defined(_XBOX)
typedef __vector4 SIMDVector;
#elif defined(__ARM_NEON__)
typedef float32x4_t SIMDVector;
#elif defined(LE_HAS_SSE1)
typedef __m128 SIMDVector;
#elif defined(__GNUC__)
typedef float SIMDVector __attribute__((vector_size(16)));
#else
/// \note This arm used to be `typedef LE_ALIGN(16) float SIMDVector[4]`, and it
/// is unreachable: MSVC defines LE_HAS_SSE1 for _M_X64 and for _M_IX86 with
/// /arch:SSE, Clang and GCC answer __GNUC__, and abi.hpp #errors on anything
/// that is neither. It cannot be written in standard C++ either -- alignas may
/// not be applied to a typedef -- which is what makes an #error the honest arm
/// rather than a fallback nothing has ever compiled.
#error SpectrumWorx has no SIMDVector for this compiler
#endif

namespace Constants
{
unsigned int const vectorAlignment = sizeof(SIMDVector);
}

} // namespace LE::Utility

#endif // intrinsics_hpp
