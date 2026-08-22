////////////////////////////////////////////////////////////////////////////////
///
/// \file host2Plugin.hpp
/// ---------------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef hostInterop_hpp__33697BD4_CAA6_473F_B3D3_330E3644E2EA
#define hostInterop_hpp__33697BD4_CAA6_473F_B3D3_330E3644E2EA
//------------------------------------------------------------------------------
#include "parameters.hpp"
#include "configuration/constants.hpp"
#include "configuration/versionConfiguration.hpp"
#include "core/parameterID.hpp"

#include "le/spectrumworx/engine/configuration.hpp"
#include "le/utility/assert.hpp"
#include "le/utility/platformSpecifics.hpp"
#include "le/utility/span.hpp"

namespace LE
{

namespace SW
{

class AutomatedModuleChain;

class Host2PluginInteropControler
{
  public:
    using ModuleChain = AutomatedModuleChain;

    using Parameters = GlobalParameters::Parameters;

    using MixPercentage = GlobalParameters::MixPercentage;
    using FFTSize = GlobalParameters::FFTSize;
    using OverlapFactor = GlobalParameters::OverlapFactor;
    using WindowFunction = GlobalParameters::WindowFunction;

  protected:
    Host2PluginInteropControler() = default;

  public:
    class AutomationBlocker;

    bool blockAutomation() const { return blockAutomation_; }
    bool presetLoadingInProgress() const { return blockAutomation(); } //...mrmlj...

  private:
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \brief Whether a preset load is putting values into this program right
    /// now. `[main-thread]`
    ///
    /// \note The interface's own, and only the interface's: what asks is a
    /// widget deciding whether a write without a mouse behind it is expected
    /// (`SharedModuleControls::FrequencyRange::canUseWriteAccessIndex`). It was
    /// also read from the audio thread, by an assertion in
    /// `SpectrumWorxCore::blockAutomation()` which claimed the flag could not be
    /// raised there -- see the note on that function for why it could, and why the
    /// answer was to delete the claim rather than to synchronise it.
    ///
    ////////////////////////////////////////////////////////////////////////////

    mutable bool blockAutomation_{false};
}; // Host2PluginInteropControler

class Host2PluginInteropControler::AutomationBlocker
{
  public:
    AutomationBlocker(AutomationBlocker const &) = delete; // makes non-copyable
    AutomationBlocker &operator=(AutomationBlocker const &) = delete;

    /// \note `LE_ASSERT` where both of these were `LE_ASSUME`. `__builtin_assume`
    /// is a promise to the optimiser that the condition *holds*, not a check that
    /// it does -- so a load that nests would be undefined behaviour rather than a
    /// caught bug, which is the wrong way round for a guard whose whole job is to
    /// notice one.
    AutomationBlocker(Host2PluginInteropControler const &effect)
        : pBlockAutomation_(&effect.blockAutomation_)
    {
        LE_ASSERT(*pBlockAutomation_ == false);
        *pBlockAutomation_ = true;
    }
    ~AutomationBlocker()
    {
        LE_ASSERT(*pBlockAutomation_ == true);
        *pBlockAutomation_ = false;
    }

    /// \note Clang lame RVO support workaround.
    ///                                        (02.07.2014.) (Domagoj Saric)
    /// \note The gate was `defined(__clang__) || _MSC_VER >= 1900`, which is
    /// false on GCC, so GCC alone would have got no move constructor at all —
    /// and NRVO on a named local is permitted, not guaranteed. Unconditional.
    AutomationBlocker(AutomationBlocker &&other) : pBlockAutomation_(other.pBlockAutomation_)
    {
        static bool dummy;
        dummy = true;
        other.pBlockAutomation_ = &dummy;
    }

  private:
    bool *LE_RESTRICT pBlockAutomation_;
}; // class Host2PluginInteropControler::AutomationBlocker

} // namespace SW

//...mrmlj...orphan...
template <typename Char>
char *copyToBuffer(Char const *string, LE::Utility::Span<char> const &buffer);

template <typename Char, std::size_t N>
char *copyToBuffer(Char const *const string, std::array<char, N> &buffer)
{
    return copyToBuffer<Char>(string, LE::Utility::makeSpan(&buffer[0], buffer.size()));
}

} // namespace LE

#endif // hostInterop_hpp
