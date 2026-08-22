////////////////////////////////////////////////////////////////////////////////
///
/// publish.cpp
/// -----------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "publish.hpp"

#include "core/automatedModuleChain.hpp"
#include "core/modules/moduleDSPAndGUI.hpp"
#include "core/spectrumWorxCore.hpp"

#include "le/utility/assert.hpp"

namespace LE::SW::Threading
{
namespace
{
void releaseModule(Module *const pModule)
{
    if (pModule)
        intrusive_ptr_release(&Engine::node(*pModule));
}
} // anonymous namespace

Module *createModuleForSlot(SpectrumWorxCore &core, std::int8_t const effectIndex,
                            std::uint8_t const slot)
{
    if (effectIndex == noModule)
        return nullptr;

    auto pModule(ModuleFactory::create<Module>(effectIndex));
    if (!pModule)
        return nullptr;

    /// \note The DSP initialiser and only that. A module has no interface of its
    /// own any more -- stage 5 -- so there is nothing else to give it, and the
    /// editor builds its strip when the chain reports the change.
    if (!core.moduleInitialiser()(*pModule, slot))
        return nullptr;

    return pModule.detach();
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note All three answer whether the engine got it, where they answered
/// nothing. A refusal is not a detail the caller can be indifferent to: every
/// one of them is called *after* the same change has been made to the main
/// thread's `Program`, so a drop is a divergence between the two copies and the
/// caller is the only thing in a position to say so. `SpectrumWorxCLAP::pushed()`
/// is what counts them.
///
/// \note The `LE_ASSERT_MSG(false, ...)` that stood at these sites is gone for
/// the reason given there: an assertion answers differently in a checked build
/// and a shipped one, and this whole branch has been about that.
///
////////////////////////////////////////////////////////////////////////////////

bool publishSlot(SpectrumWorxCore &core, ToEngineQueue &toEngine, std::uint8_t const slot,
                 std::int8_t const effectIndex, Module *const pModule)
{
    if (!core.engineIsRunning())
    {
        releaseModule(core.installModuleInSlot(slot, pModule));
        return true;
    }

    if (toEngine.push(setSlot(slot, effectIndex, pModule)))
        return true;

    /// \note A full command ring drops the request, and the module built for it
    /// has to go with it -- the alternative is a leak per dropped slot change.
    /// 1024 deep against a handful of clicks, so this is a "something is very
    /// wrong" path rather than a rate limit.
    releaseModule(pModule);
    return false;
}

bool publishModuleMove(SpectrumWorxCore &core, ToEngineQueue &toEngine, std::uint8_t const from,
                       std::uint8_t const to)
{
    if (!core.engineIsRunning())
    {
        core.moveModule(from, to);
        return true;
    }

    /// \note Nothing to undo, unlike its two neighbours -- a move owns nothing --
    /// so answering is the whole of it. Written this way round regardless,
    /// because the push was inside an assert and `LE_ASSERT_MSG` is
    /// `static_cast<void>(0)` under NDEBUG: every shipped build reordered the
    /// rack and the saved state and left the engine playing the old order.
    return toEngine.push(moveModule(from, to));
}

bool publishChain(SpectrumWorxCore &core, ToEngineQueue &toEngine, AutomatedModuleChain &newChain)
{
    if (!core.engineIsRunning())
    {
        // the same exchange the audio thread would do, after which the
        // caller's own chain holds what was live and its destructor frees it
        core.swapModuleChain(newChain);
        return true;
    }

    /// \note Raw, and deliberately: the message *is* the owner from the push
    /// until the retire, and dressing that up as a smart pointer on one side of a
    /// ring that carries `void *` would only hide where the handover is.
    auto *const pOutgoing(new AutomatedModuleChain);
    pOutgoing->swap(newChain);

    if (toEngine.push(swapChain(pOutgoing)))
        return true;

    /// \note And the new chain goes back where it came from, so that a caller
    /// that meant to install it still owns what it built.
    pOutgoing->swap(newChain);
    delete pOutgoing;
    return false;
}

} // namespace LE::SW::Threading
