////////////////////////////////////////////////////////////////////////////////
///
/// discreteParameterTests.cpp
/// --------------------------
///
///   What an enumerated module parameter's combo box lists, and in what order.
///
/// \note The menu is not opened: a menu is a modal window and a test binary has
/// no message loop to answer one with (\see the note at the top of
/// knobMenuTests.cpp). The combo box *is* its own menu here -- GUI::ComboBox
/// derives from GUI::PopupMenuWithSelection -- so the rows are readable without
/// showing it, which is the whole of what these cases ask about.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/editorHarness.hpp"

/// \note Before anything that names SW::Module, as elsewhere: the module chain
/// downcasts a node to it and this is the header with the complete type.
#include "core/modules/moduleDSPAndGUI.hpp"

#include "gui/modules/moduleControl.hpp"
#include "gui/modules/moduleUI.hpp"

#include "le/spectrumworx/effects/configuration/effectNames.hpp"
#include "le/spectrumworx/effects/tune_worx/tuneWorx.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace LE;
using namespace LE::SW;

using Key = Effects::Detail::TuneWorxBase::Key;

/// \brief The first control of \p moduleUI that is a combo box.
///
/// \note By parameter index rather than by walking the children, as the other
/// GUI cases do and for the same reason: the widget storage is a compile-time
/// chain of one base class per parameter, so there is no runtime list to iterate.
GUI::ComboBox *firstComboBox(GUI::ModuleUI &moduleUI)
{
    auto const parameters(moduleUI.module().numberOfEffectSpecificParameters());
    for (std::uint8_t index(0); index < parameters; ++index)
    {
        auto &control(moduleUI.effectSpecificParameterControl(index));
        if (auto *const pComboBox = dynamic_cast<GUI::ComboBox *>(&control.widget()))
            return pComboBox;
    }
    return nullptr;
}

/// \brief The one strip of \p effectName, in slot 0.
GUI::ModuleUI &stripFor(GUI::SpectrumWorxEditor &editor, char const *const effectName)
{
    auto const effect(Effects::effectIndex(effectName));
    REQUIRE(effect >= 0);
    editor.addUserAddedModule(static_cast<std::uint8_t>(effect));
    editor.resyncModuleRack();
    auto *const pModuleUI(editor.regionInSlot(0));
    REQUIRE(pModuleUI != nullptr);
    return *pModuleUI;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("Tune Worx's scale root is listed from C, and still valued from A",
          "[gui][modules][combo]")
{
    ////////////////////////////////////////////////////////////////////////////
    ///
    /// \note Two claims, and they are two because they disagree by design.
    ///
    ///   What a musician reads down is a chromatic scale, which starts at C --
    /// that is issue #89 and it is a statement about the *rows*.
    ///
    ///   What the parameter holds is an index, and the index is A-based: it is
    /// what every `.swp` since 2011 has stored, what a host automates, and what
    /// the DSP adds to a note offset off a 27.5 Hz A (musicalScales.cpp). So
    /// reordering the enumerators would have silently retuned every preset that
    /// names a key. The rows move; the values do not, and each row carries its
    /// own.
    ///
    ////////////////////////////////////////////////////////////////////////////
    SWTest::HostSideJuce const juceIsUp;

    SWTest::Instance instance;
    instance.openEditor();

    auto &strip(stripFor(instance.editor(), "TuneWorx"));
    auto *const pComboBox(firstComboBox(strip));
    REQUIRE(pComboBox != nullptr);
    auto &comboBox(*pComboBox);

    std::vector<Key::value_type> const expected{Key::C,   Key::Cis, Key::D,   Key::Dis,
                                                Key::E,   Key::F,   Key::Fis, Key::G,
                                                Key::Gis, Key::A,   Key::Ais, Key::B};

    REQUIRE(comboBox.numberOfItems() == expected.size());

    auto const &names(Parameters::DiscreteValues<Key>::strings);
    for (unsigned int row(0); row < expected.size(); ++row)
    {
        INFO("row " << row);
        auto const value(expected[row]);
        CHECK(comboBox.getItemText(row) == juce::String(names[value]));

        // The row's own value, which is what selecting it writes.
        comboBox.setSelectedIndex(row);
        CHECK(comboBox.getSelectedID() == value);
    }

    // C first is the point of the reorder; A stays value zero.
    CHECK(comboBox.getItemText(0) == "C");
    CHECK(Key::A == 0);
}

TEST_CASE("Every black key is named both ways", "[gui][modules][combo]")
{
    /// \note Sharp first and flat after it, for all five, because there is no key
    /// signature here to pick one -- a chromatic root is any of the twelve. \see
    /// issue #89 and the note beside the value strings in tuneWorx.hpp.
    auto const &names(Parameters::DiscreteValues<Key>::strings);

    CHECK(std::string(names[Key::Ais]) == "A#/Bb");
    CHECK(std::string(names[Key::Cis]) == "C#/Db");
    CHECK(std::string(names[Key::Dis]) == "D#/Eb");
    CHECK(std::string(names[Key::Fis]) == "F#/Gb");
    CHECK(std::string(names[Key::Gis]) == "G#/Ab");

    // ...and the seven naturals are still just themselves.
    CHECK(std::string(names[Key::A]) == "A");
    CHECK(std::string(names[Key::B]) == "B");
    CHECK(std::string(names[Key::C]) == "C");
    CHECK(std::string(names[Key::D]) == "D");
    CHECK(std::string(names[Key::E]) == "E");
    CHECK(std::string(names[Key::F]) == "F");
    CHECK(std::string(names[Key::G]) == "G");
}
