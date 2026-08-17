////////////////////////////////////////////////////////////////////////////////
///
/// moduleWidgets.cpp
/// -----------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "moduleWidgets.hpp"

#include "gui/modules/moduleUI.hpp"

#include "le/spectrumworx/effects/pitch_magnet/pitchMagnet.hpp"
#include "le/spectrumworx/effects/tune_worx/tuneWorx.hpp"

#include "le/spectrumworx/effects/configuration/constants.hpp"
#include "le/spectrumworx/effects/configuration/includedEffects.hpp"
#include "le/spectrumworx/effects/configuration/indexToEffectImplMapping.hpp"
#include "le/spectrumworx/effects/effects.hpp"
#include "le/utility/typeList.hpp"

#include "le/utility/assert.hpp"

#include <memory>

namespace LE::SW::GUI
{

// Implementation note:
//   The (PVD)PitchMagnet::Target parameter is so far the only parameter
// that is not handled correctly by the simple ModuleKnob::QuantizationFor
// logic (which detects quantized parameters simply by their units). It is a
// Hertz parameter but its precision is not bound by the DFT engine
// parameters ando so it does not need to be quantized accordingly.
//                                        (13.12.2011.) (Domagoj Saric)
/// \todo Think of a cleaner solution.
///                                       (13.12.2011.) (Domagoj Saric)
/// \note Moved here from core/modules/factory.cpp, which is in sw-dsp and has no
/// business naming a knob. It has to be visible before WidgetsFor<> below
/// instantiates the widgets that read it, which is what putting it at the top of
/// the one file that builds them guarantees.
///                                       (02.08.2026.) (SW port)
template <> struct ModuleKnob::QuantizationFor<Effects::Detail::PitchMagnetBase::Target>
{
    static ModuleKnob::Quantization const value = ModuleKnob::Fixed;
};

////////////////////////////////////////////////////////////////////////////////
//
// fillComboBoxForParameter< TuneWorx::Key >()
// -------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Tune Worx's scale root, listed from C rather than from A.
///
///   The declaration order is A first, because the value *is* the index and the
/// DSP counts semitones up from a 27.5 Hz A -- so it is what presets, sessions
/// and automation lanes have always meant and it does not move. What a musician
/// reads down is a chromatic scale, and that one starts at C. \see issue #89, and
/// the note beside the value strings in tuneWorx.hpp.
///
/// \note Here, in the one translation unit that builds module widgets, for the
/// same reason the quantisation above is: it has to be declared before
/// `WidgetInitialiser` instantiates the primary template for this parameter, and
/// this file is the only place that happens.
///                                           (17.08.2026.)
///
////////////////////////////////////////////////////////////////////////////////

template <> void fillComboBoxForParameter<Effects::Detail::TuneWorxBase::Key>(ComboBox &comboBox)
{
    using Key = Effects::Detail::TuneWorxBase::Key;
    auto const &names(LE::Parameters::DiscreteValues<Key>::strings);

    LE_ASSERT_MSG(comboBox.numberOfItems() == 0, "ComboBox already filled.");
    for (auto const value : {Key::C, Key::Cis, Key::D, Key::Dis, Key::E, Key::F, Key::Fis, Key::G,
                             Key::Gis, Key::A, Key::Ais, Key::B})
        comboBox.addItem(value, names[value]);

    /// \note The parameter's default rather than the first row, which is what the
    /// generic filler's `setValue( 0 )` amounts to only while the two coincide.
    comboBox.setValue(Key::default_());
}

//------------------------------------------------------------------------------
namespace
{
////////////////////////////////////////////////////////////////////////////////
///
/// \class WidgetsFor
///
/// \brief One effect's controls, and the storage they live in.
///
/// \note This is `SW::ModuleWidgets<Effect>` from finalImplementations.hpp, with
/// the create/destroy pair turned into a constructor and a destructor -- the two
/// were only separate because the storage belonged to an object with a longer
/// life than the widgets in it.
///
////////////////////////////////////////////////////////////////////////////////

template <class Effect> class WidgetsFor final : public ModuleWidgets
{
  public:
    explicit WidgetsFor(ModuleUI &region)
    {
        region.setUpForEffect(Effect::title, Effect::description);
        parameterWidgets_.construct(region);
    }

    ~WidgetsFor() override { parameterWidgets_.destroy(); }

  private:
    ParameterWidgets<typename Effect::Parameters> parameterWidgets_;
}; // class WidgetsFor

/// \note The same `switchOn` over `Effects::ValidIndices` the module factory uses
/// to pick a module's size and constructor -- one compiled instantiation per
/// effect, chosen by an index at runtime.
struct Builder
{
    using result_type = std::unique_ptr<ModuleWidgets>;

    template <class EffectIndex> result_type operator()(EffectIndex) const
    {
        using Effect = typename Effects::ImplForIndex<EffectIndex::value>::type;
        return std::make_unique<WidgetsFor<Effect>>(region);
    }

    ModuleUI &region;
}; // struct Builder
} // anonymous namespace

std::unique_ptr<ModuleWidgets> createModuleWidgets(std::uint8_t const effectIndex, ModuleUI &region)
{
    LE_ASSERT(effectIndex < Effects::Constants::numberOfEffects);
    if (effectIndex >= Effects::Constants::numberOfEffects)
        return nullptr;

    return LE::Utility::switchOn<Effects::ValidIndices>(effectIndex, Builder{region});
}

} // namespace LE::SW::GUI
