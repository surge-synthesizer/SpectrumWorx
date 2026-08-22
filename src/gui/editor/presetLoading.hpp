////////////////////////////////////////////////////////////////////////////////
///
/// \file presetLoading.hpp
/// -----------------------
///
///   Loading a preset into whatever is hosting the editor.
///
///   presets.hpp does the reading: XML in, module chain and parameter values
/// out. What it needs from a caller is a "PresetConsumer" -- where to put the
/// new program, how to lock the audio thread out while the chain is swapped,
/// and how to build a module's UI region as each slot is filled.
///
///   All of that is reachable through `EditorHost` and the editor itself, so
/// there is one implementation rather than one per plugin format. The 2016 code
/// had it as a member of the VST2/AU plugin class, which is why it looked like
/// something only a plugin could supply.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef presetLoading_hpp__5E29B7D4_1A63_4C08_B7F2_9D4E60C381AB
#define presetLoading_hpp__5E29B7D4_1A63_4C08_B7F2_9D4E60C381AB

/// `fs`, for the preset file. \see io/jucePath.hpp -- a forward declaration of
/// `juce::File` stood beside `juce::String` here and is not needed by anything
/// any more.
#include "filesystem/import.h"

namespace juce
{
class String;
} // namespace juce

namespace LE::SW
{

struct DawExtraState;

namespace GUI
{

class EditorHost;
class SpectrumWorxEditor;

////////////////////////////////////////////////////////////////////////////////
///
/// loadPreset()
/// ------------
///
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Reads \p presetFile and makes it the host's current program.
///
/// \param pEditor  the open editor, or **null**. A preset the user picked from
/// the browser has one; the session state a host restores usually does not, and
/// a plugin whose window has never been opened must load it just the same. The
/// editor's part is building each module's UI region as its slot is filled and
/// moving the slot marker afterwards -- both of which are simply skipped, which
/// is what `EditorModuleInitialiser` already documents its own null pointer as
/// meaning. Everything below that line is the engine's and happens either way.
/// \param comment  the preset's comment, if it has one and the caller wants it.
/// \param presetName goes into Program::name(), truncated to fit.
///
/// \note `[main-thread]`. The audio thread is locked out only for the two
/// moments the module chain is actually swapped; everything else -- parsing,
/// creating modules, building their UI -- happens outside the lock.
///
////////////////////////////////////////////////////////////////////////////////

/// \param pDawExtraState null for a preset the user opened; the session's own,
/// for state a host restored. See SW::DawExtraState.
bool loadPreset(EditorHost &, SpectrumWorxEditor *pEditor, fs::path const &presetFile,
                bool ignoreExternalSample, juce::String *comment, char const *presetName,
                DawExtraState const *pDawExtraState = nullptr);

/// \brief The same, from a buffer that is already in memory.
///
/// \note Which is where a factory preset comes from: the banks are compiled into
/// the binary (factoryPresets.hpp) and have no file for the overload above to
/// open. That one reads the file and calls this. It is also where session state
/// comes from -- a `clap_istream` is not a file either.
bool loadPreset(EditorHost &, SpectrumWorxEditor *pEditor, char *inMemoryPreset,
                bool ignoreExternalSample, juce::String *comment, char const *presetName,
                DawExtraState const *pDawExtraState = nullptr);

} // namespace GUI

} // namespace LE::SW

#endif // presetLoading_hpp
