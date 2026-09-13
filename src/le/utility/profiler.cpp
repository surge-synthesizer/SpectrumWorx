////////////////////////////////////////////////////////////////////////////////
///
/// profiler.cpp
/// ------------
///
/// Copyright (c) 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "profiler.hpp"

#include "platformSpecifics.hpp"

namespace LE::Utility
{

// Evaluating Performance of Android Platform Using Native C for Embedded Systems
// http://anibal.gyte.edu.tr/hebe/AblDrive/69276048/w/Storage/104_2011_1_601_69276048/Downloads/m33.pdf
// http://stackoverflow.com/questions/17372110/clock-thread-cputime-id-on-macosx
// http://stackoverflow.com/questions/5167269/clock-gettime-alternative-in-mac-os-x
// http://stackoverflow.com/questions/3564161/thread-performance-in-android/3570077#3570077 (CLOCK_THREAD_CPUTIME_ID)
// https://google.github.io/fplutil/android_ndk_perf.html

DSPProfiler DSPProfiler::singleton_;

DSPProfiler::DSPProfiler() : totalSamples_(0), sampleRate_(0), totalCPUTime_(0), lastTime_()
{
#if defined(_MSC_VER)
    static_assert(sizeof(totalCPUTime_) == sizeof(std::uint64_t), "");
    static_assert(sizeof(lastTime_) == sizeof(std::uint64_t), "");
#endif // MSVC version
}

void DSPProfiler::setSignalSampleRate(std::uint32_t const sampleRate)
{
    LE_ASSERT(sampleRate < 200000);
    sampleRate_ = static_cast<float>(sampleRate);
    reset();
}

void DSPProfiler::reset()
{
    totalSamples_ = 0;
    totalCPUTime_ = totalCPUTime_.zero();
}

void DSPProfiler::beginInterval() { lastTime_ = std::chrono::steady_clock::now(); }

void DSPProfiler::endInterval(std::uint32_t const intervalLengthInSampleFrames)
{
    auto const newTimeStamp(std::chrono::steady_clock::now());
    auto const timeInterval(newTimeStamp - lastTime_);
#if defined(__ANDROID__) || defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__)
    LE_ASSERT(timeInterval.count() > 0);
#else  //...mrmlj...desktop machines seem be able to process small buffers in 'zero' time...
    LE_ASSERT(timeInterval.count() >= 0);
#endif // __ANDROID__
    totalSamples_ += intervalLengthInSampleFrames;
    totalCPUTime_ += timeInterval;
}

float DSPProfiler::cpuUsagePercentage() const
{
    using namespace std::chrono;
    auto const totalSignalTime(totalSamples_ / sampleRate_);
    auto const ratio(totalCPUTime_ / totalSignalTime);
    return duration_cast<duration<float>>(ratio).count() * 100;
}

} // namespace LE::Utility

#ifdef __ANDROID__
/// \note Provide a default implementation (c/p from libc++ sources) in order to
/// avoid a dependency on libc++. Also serves as a workaround for undefined
/// reference linker errors for this very function even if we do link with
/// libc++ when targeting x86 with NDK r10e.
///                                           (09.10.2015.) (Domagoj Saric)
_LIBCPP_BEGIN_NAMESPACE_STD
namespace chrono
{
__attribute__((weak)) extern system_clock::time_point system_clock::now() _NOEXCEPT
{
    timeval tv;
    gettimeofday(&tv, 0);
    return time_point(seconds(tv.tv_sec) + microseconds(tv.tv_usec));
}
__attribute__((weak)) extern steady_clock::time_point steady_clock::now() _NOEXCEPT
{
    struct timespec tp;
    LE_VERIFY(::clock_gettime(CLOCK_MONOTONIC, &tp) == 0);
    return time_point(seconds(tp.tv_sec) + nanoseconds(tp.tv_nsec));
}
} // namespace chrono
_LIBCPP_END_NAMESPACE_STD
#endif // __ANDROID__
