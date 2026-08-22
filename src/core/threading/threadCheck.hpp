////////////////////////////////////////////////////////////////////////////////
///
/// \file threadCheck.hpp
/// ---------------------
///
///   Which thread is this, and may it do that.
///
///   CLAP annotates every entry point `[main-thread]`, `[audio-thread]` or
/// `[thread-safe]`, and those annotations are contractual. This is what answers
/// them: `GUI::isThisTheGUIThread()` asks JUCE, which is only an answer once
/// there is a message thread, and there is no processing lock to ask instead.
/// The rules these two functions serve are doc/tech/threading_model.md §2.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef threadCheck_hpp__0B4E7D91_2C68_4A35_9F07_5E1D8A62B4C3
#define threadCheck_hpp__0B4E7D91_2C68_4A35_9F07_5E1D8A62B4C3

namespace LE::SW::Threading
{

////////////////////////////////////////////////////////////////////////////////
// The main thread
//
// \note Recorded rather than asked for. `clap.thread-check` is an optional
// extension and a host that does not offer it cannot be asked; `clap_plugin::init`
// is `[main-thread]` by contract, so the thread that runs it is the answer for the
// life of the plugin. A host that offers the extension is still the better source
// and callers that have one should prefer it -- this is what there is otherwise.
////////////////////////////////////////////////////////////////////////////////

/// \brief Records the calling thread as the main thread.
void markMainThread();

/// \brief False before markMainThread() has ever been called, which is the honest
/// answer for a process that has not yet been told (`sw-tests`, `sw-show-ui`).
bool isMainThread();

/// \brief Forgets the main thread. For tests that drive several plugins.
void forgetMainThread();

////////////////////////////////////////////////////////////////////////////////
// The audio thread
////////////////////////////////////////////////////////////////////////////////

/// \brief Whether this call is inside one of CLAP's `[audio-thread]` entry
/// points.
///
/// \note Deliberately not "is this thread the audio thread". A host with a worker
/// pool -- Bitwig and Reaper both have one -- delivers successive blocks of the
/// same plugin on different threads, so there is no such thread to name. What can
/// be answered is whether the host is currently inside a call it is entitled to
/// make on the audio thread.
///
/// \note **Not "underneath process()"**. `plugin.h` puts four entry points in the
/// `[audio-thread]` set and only one of them is `process()`: `reset()` is
/// `[audio-thread & active]` and runs between blocks, so the narrower reading is
/// a hole a correct host walks straight into. \see ScopedAudioThreadEntry.
bool isAudioThread();

////////////////////////////////////////////////////////////////////////////////
///
/// \class ScopedAudioThreadEntry
///
/// \brief One line at the top of **every** `[audio-thread]` entry point. Two jobs:
///
///   - makes isAudioThread() true for the duration, on this thread only;
///   - opens a RealtimeSanitizer realtime region, so that an allocation, a lock or
///     a syscall reached from anywhere underneath is reported with a stack.
///
/// \note Named for the contract rather than for `process()`: "callback" reads as
/// "the audio callback", and the other three entry points in `plugin.h`'s
/// `[audio-thread]` set do not look like one.
///
///   What owes one, from `clap/plugin.h`:
///
///   | | annotation | ours |
///   |---|---|---|
///   | `process` | `[audio-thread & active & processing]` | overridden |
///   | `reset` | `[audio-thread & active]` | overridden |
///   | `start_processing` | `[audio-thread & active & !processing]` | not overridden |
///   | `stop_processing` | `[audio-thread & active & processing]` | not overridden |
///
///   Plus one that is not in that table because its annotation is conditional:
/// `clap_plugin_params::flush` is `[active ? audio-thread : main-thread]`
/// (ext/params.h:303). It takes this **only while active** -- opening it
/// unconditionally would be wrong in the other direction, telling an inactive
/// plugin that the audio thread owns an engine the main thread owns.
///
/// \note The sanitizer half is the runtime entry point rather than the
/// `[[clang::nonblocking]]` attribute. The attribute would do the same at runtime
/// *and* run a static analysis whose diagnostics this tree cannot answer: the one
/// allocation `process()` still reaches is deliberate and recorded in
/// issue #9. The runtime region costs nothing in a build without the
/// sanitizer, where both calls compile away.
///
////////////////////////////////////////////////////////////////////////////////

class ScopedAudioThreadEntry
{
  public:
    ScopedAudioThreadEntry();
    ~ScopedAudioThreadEntry();

    // makes non-copyable
    ScopedAudioThreadEntry(ScopedAudioThreadEntry const &) = delete;
    ScopedAudioThreadEntry &operator=(ScopedAudioThreadEntry const &) = delete;
}; // class ScopedAudioThreadEntry

} // namespace LE::SW::Threading

#endif // threadCheck_hpp
