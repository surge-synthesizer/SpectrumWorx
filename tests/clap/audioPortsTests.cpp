////////////////////////////////////////////////////////////////////////////////
///
/// \file audioPortsTests.cpp
/// -------------------------
///
///   The port layout, and the one thing that moves it.
///
///   SpectrumWorx has a single channel width and every port carries it: main in,
/// side chain in, main out. `clap.configurable-audio-ports` is how a host asks
/// for the other one, and these cases hold both ends of that -- which requests
/// are accepted, and that the engine and the rendered block follow.
///
/// \note The interesting request is the *asymmetric* one. clap-wrapper's AUv2
/// probe varies only the main busses and leaves the side chain at whatever it
/// currently has, so "please go mono" reaches this plugin as 1/2/1 -- a mono main
/// with a stereo side chain, which is not a layout this engine has. Accepting it
/// and taking the side chain along is what makes AU offer mono at all, and the
/// case named for it is the one that would silently cost that.
///
/// \see doc/tech/how-mono-ports-work.md
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "clap/testHost.hpp"

#include "goldens/engineHarness.hpp"
#include "gui/editor/spectrumWorxEditor.hpp"
#include "spectrumWorxCLAP.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

using namespace SWTest;

constexpr float sampleRate{48000};
constexpr std::uint32_t blockSize{512};
/// Past the engine's latency, so the output is signal rather than the fill-up.
constexpr unsigned int blocks{32};

/// A sentinel no rendered block produces, for a buffer that must not be written.
constexpr float untouched{-12345.0f};

clap_plugin_configurable_audio_ports const &configurablePorts(clap_plugin const &plugin)
{
    auto const *const extension(static_cast<clap_plugin_configurable_audio_ports const *>(
        plugin.get_extension(&plugin, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS)));
    REQUIRE(extension != nullptr);
    return *extension;
}

/// \brief What the port at \p index in \p isInput's direction says it carries.
std::uint32_t portChannels(clap_plugin const &plugin, bool const isInput, std::uint32_t const index)
{
    clap_audio_port_info info{};
    REQUIRE(ActivePlugin::audioPorts(plugin).get(&plugin, index, isInput, &info));
    return info.channel_count;
}

/// \brief One request per port, spelt out rather than derived, so a case can ask
/// for arrangements the plugin does not have.
struct Layout
{
    std::uint32_t mainIn;
    std::uint32_t sideIn;
    std::uint32_t mainOut;

    std::vector<clap_audio_port_configuration_request> requests() const
    {
        auto const type([](std::uint32_t const channels) -> char const * {
            return (channels == 1) ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
        });
        return {{true, 0, mainIn, type(mainIn), nullptr},
                {true, 1, sideIn, type(sideIn), nullptr},
                {false, 0, mainOut, type(mainOut), nullptr}};
    }
}; // struct Layout

bool canApply(clap_plugin const &plugin, Layout const layout)
{
    auto const requests(layout.requests());
    return configurablePorts(plugin).can_apply_configuration(
        &plugin, requests.data(), static_cast<std::uint32_t>(requests.size()));
}

/// \brief The engine's own channel counts, which the C API has no call for.
std::pair<std::uint8_t, std::uint8_t> engineChannels(clap_plugin const &plugin)
{
    auto *const pHelper(static_cast<LE::SW::PluginHelper *>(plugin.plugin_data));
    REQUIRE(pHelper != nullptr);
    auto const &implementation(*static_cast<LE::SW::SpectrumWorxCLAP *>(pHelper));
    return {implementation.numberOfInputChannels(), implementation.numberOfSideChannels()};
}

void fillWithSine(std::vector<float> &buffer, float const frequency, std::uint32_t const startFrame)
{
    for (std::size_t frame(0); frame < buffer.size(); ++frame)
        buffer[frame] = 0.5f * std::sin(2 * std::numbers::pi_v<float> * frequency *
                                        static_cast<float>(startFrame + frame) / sampleRate);
}

float peak(std::vector<float> const &buffer)
{
    float largest{0};
    for (auto const sample : buffer)
        largest = std::max(largest, std::abs(sample));
    return largest;
}

bool allFinite(std::vector<float> const &buffer)
{
    return std::all_of(buffer.begin(), buffer.end(),
                       [](float const sample) { return std::isfinite(sample); });
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Renders \p blocks through a side-chain effect at the plugin's current
/// width and hands back the last block's first channel.
///
/// \note Colorifer in slot 0, as sampleFeedTests.cpp uses it: an effect measured
/// to read the side chain at its defaults, so what arrives on port 1 is audible
/// in the answer.
///
/// \param sideFrequency what the port carries, 0 leaving it unpatched.
///
////////////////////////////////////////////////////////////////////////////////

std::vector<float> render(ActivePlugin &plugin, float const sideFrequency)
{
    OneParameterEvent const fillSlotZero(parameterID(moduleChainType, 0),
                                         SWTest::effectByStreamingName("Colorifer"));
    plugin.flush(&*fillSlotZero);

    std::vector<float> leftIn(blockSize), rightIn(blockSize);
    std::vector<float> sideLeft(blockSize), sideRight(blockSize);
    std::vector<float> leftOut(blockSize), rightOut(blockSize, untouched);

    if (sideFrequency > 0)
    {
        plugin.connectSideChain(sideLeft, sideRight);
        editorHostOf(*plugin).setSideChainSource(LE::SW::SideChainSource::Host);
    }

    for (unsigned int block(0); block < blocks; ++block)
    {
        fillWithSine(leftIn, 440.0f, block * blockSize);
        rightIn = leftIn;
        if (sideFrequency > 0)
        {
            fillWithSine(sideLeft, sideFrequency, block * blockSize);
            sideRight = sideLeft;
        }

        plugin.process(leftIn, rightIn, leftOut, rightOut);
        REQUIRE(allFinite(leftOut));
    }

    // the second output buffer is the host's to keep at one channel: a plugin
    // that wrote to it would be writing past the port it declared
    if (plugin.channelWidth() == 1)
        REQUIRE(std::all_of(rightOut.begin(), rightOut.end(),
                            [](float const sample) { return sample == untouched; }));

    return leftOut;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

TEST_CASE("A plugin nobody has configured is stereo throughout", "[clap][audio-ports]")
{
    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);

    auto const &ports(ActivePlugin::audioPorts(*plugin));
    REQUIRE(ports.count(&*plugin, true) == 2);
    REQUIRE(ports.count(&*plugin, false) == 1);

    for (auto const &[isInput, index] :
         {std::pair{true, 0u}, std::pair{true, 1u}, std::pair{false, 0u}})
    {
        clap_audio_port_info info{};
        REQUIRE(ports.get(&*plugin, index, isInput, &info));
        CHECK(info.channel_count == 2);
        CHECK(std::string_view(info.port_type) == CLAP_PORT_STEREO);
    }

    // two main and two side, which is what "four in, two out" means to the engine
    CHECK(engineChannels(*plugin) == std::pair<std::uint8_t, std::uint8_t>{2, 2});
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The grid is what clap-wrapper's AUv2 wrapper walks at PostConstructor to
/// build the `AUChannelInfo` list, and every pair it accepts becomes a layout
/// auval will try. So this case is the AU's advertised capability, written down.
///
/// \note Swept with the side chain following the main pair, because that is the
/// only shape this plugin has. The probe sends two per candidate -- the other
/// leaves non-main ports where they are -- and the one that gets a yes here is
/// what puts a width on the AU's menu. The other is refused, which is what the
/// case below holds.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Two widths are offered and no others", "[clap][audio-ports]")
{
    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);

    plugin.whileDeactivated([](clap_plugin const &deactivated) {
        for (std::uint32_t in(0); in <= 4; ++in)
            for (std::uint32_t out(0); out <= 4; ++out)
            {
                auto const expected((in == out) && (in >= 1) && (in <= 2));
                INFO("main in " << in << ", main out " << out);
                CHECK(canApply(deactivated, {in, in, out}) == expected);
            }
    });
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note One width, every port. A request that would leave the three
/// disagreeing is refused rather than quietly rounded to something the engine
/// can do -- including one that does not name the side chain at all, because a
/// port the requests do not name keeps what it has.
///
/// \note 1x2->1 is the shape clap-wrapper's AUv2 probe sends when it moves only
/// the main busses. Refusing it is what makes the probe try its second shape,
/// where the side chain comes along; a plugin that accepted it would be
/// promising a layout it does not have.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Every port carries the same width or the layout is refused", "[clap][audio-ports]")
{
    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);

    plugin.whileDeactivated([](clap_plugin const &deactivated) {
        CHECK(canApply(deactivated, {1, 1, 1}));
        CHECK(canApply(deactivated, {2, 2, 2}));

        CHECK_FALSE(canApply(deactivated, {1, 2, 1}));
        CHECK_FALSE(canApply(deactivated, {2, 1, 2}));
        CHECK_FALSE(canApply(deactivated, {1, 1, 2}));
    });
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note A request that leaves the side chain unnamed is not a request to leave
/// it *alone*: the extension says an unnamed port keeps what it has, so asking a
/// stereo plugin for a mono main and saying nothing about the side chain
/// describes a mono/stereo pair, which is refused like any other.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A request that names only the main ports is refused", "[clap][audio-ports]")
{
    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);

    plugin.whileDeactivated([](clap_plugin const &deactivated) {
        clap_audio_port_configuration_request const mainOnly[]{
            {true, 0, 1, CLAP_PORT_MONO, nullptr}, {false, 0, 1, CLAP_PORT_MONO, nullptr}};
        CHECK_FALSE(
            configurablePorts(deactivated).can_apply_configuration(&deactivated, mainOnly, 2));
    });

    CHECK(portChannels(*plugin, true, 0) == 2);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note What `can_apply_configuration` promises, `apply_configuration` has to
/// produce -- port for port. Saying yes and then taking every port to some other
/// width is what clap-validator's `layout-configurable-audio-ports` catches, and
/// what it caught here.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Every port ends up carrying exactly what was asked for", "[clap][audio-ports]")
{
    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);

    auto const applyAndRead([&plugin](Layout const layout) {
        bool applied{false};
        plugin.whileDeactivated([&](clap_plugin const &deactivated) {
            auto const requests(layout.requests());
            applied = configurablePorts(deactivated)
                          .apply_configuration(&deactivated, requests.data(),
                                               static_cast<std::uint32_t>(requests.size()));
        });
        REQUIRE(applied);
        return Layout{portChannels(*plugin, true, 0), portChannels(*plugin, true, 1),
                      portChannels(*plugin, false, 0)};
    });

    for (Layout const &wanted : {Layout{1, 1, 1}, Layout{2, 2, 2}})
    {
        INFO("asked for " << wanted.mainIn << "x" << wanted.sideIn << "->" << wanted.mainOut);
        auto const got(applyAndRead(wanted));
        CHECK(got.mainIn == wanted.mainIn);
        CHECK(got.sideIn == wanted.sideIn);
        CHECK(got.mainOut == wanted.mainOut);
    }
}

TEST_CASE("A request naming a port that is not there is refused", "[clap][audio-ports]")
{
    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);

    plugin.whileDeactivated([](clap_plugin const &deactivated) {
        auto const &ports(configurablePorts(deactivated));

        clap_audio_port_configuration_request const third{true, 2, 1, CLAP_PORT_MONO, nullptr};
        CHECK_FALSE(ports.can_apply_configuration(&deactivated, &third, 1));

        clap_audio_port_configuration_request const secondOut{false, 1, 1, CLAP_PORT_MONO, nullptr};
        CHECK_FALSE(ports.can_apply_configuration(&deactivated, &secondOut, 1));
    });

    // and the layout it was refused from is the layout it still has
    CHECK(portChannels(*plugin, true, 0) == 2);
}

TEST_CASE("Mono moves the ports and the engine, and comes back", "[clap][audio-ports]")
{
    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);

    REQUIRE(plugin.reconfigure(1));
    CHECK(plugin.channelWidth() == 1);
    CHECK(portChannels(*plugin, true, 0) == 1);
    CHECK(portChannels(*plugin, true, 1) == 1);
    CHECK(portChannels(*plugin, false, 0) == 1);
    CHECK(engineChannels(*plugin) == std::pair<std::uint8_t, std::uint8_t>{1, 1});

    clap_audio_port_info info{};
    REQUIRE(ActivePlugin::audioPorts(*plugin).get(&*plugin, 0, true, &info));
    CHECK(std::string_view(info.port_type) == CLAP_PORT_MONO);

    REQUIRE(plugin.reconfigure(2));
    CHECK(engineChannels(*plugin) == std::pair<std::uint8_t, std::uint8_t>{2, 2});
}

TEST_CASE("A mono plugin renders one channel from one channel", "[clap][audio-ports]")
{
    Entry const entry;
    ActivePlugin plugin(sampleRate, blockSize);
    REQUIRE(plugin.reconfigure(1));

    auto const output(render(plugin, 0));
    CHECK(allFinite(output));
    CHECK(peak(output) > 0.01f);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The claim is that the *second port* is still read once it is a mono one.
/// A mono layout halves the side chain along with everything else, and a fallback
/// to the main input would go unnoticed -- the output stays finite and loud
/// either way. Two runs whose only difference is what the port carries is what
/// separates them.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("A mono side chain reaches the engine", "[clap][audio-ports]")
{
    Entry const entry;

    auto const runAt([](std::uint32_t const width, float const sideFrequency) {
        ActivePlugin plugin(sampleRate, blockSize);
        REQUIRE(plugin.reconfigure(width));
        return render(plugin, sideFrequency);
    });

    auto const high(runAt(1, 1100.0f));
    auto const low(runAt(1, 300.0f));

    REQUIRE(peak(high) > 0.01f);
    CHECK(high != low);
}

////////////////////////////////////////////////////////////////////////////////
///
/// \note The wiring, not the wording. `hostPortName()` is checked at both widths
/// in sideChainSelectorTests.cpp by calling it; what is unproven there is that
/// anything *asks* it again when the layout moves. The width lands in
/// `activate()`, and the only thing that reaches the editor from there is
/// `updateForEngineSetupChanges()` -- so a box that named its port once, when the
/// editor was built, would pass every case in that file and be wrong in the one
/// host that reconfigures.
///
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Reconfiguring renames the ports in an open editor", "[clap][audio-ports][gui]")
{
    Entry const entry;
    TestHost host{{.threadCheck = true, .log = true, .gui = true}};
    ActivePlugin plugin(sampleRate, blockSize, host);

    auto const *const gui(
        static_cast<clap_plugin_gui const *>(plugin->get_extension(&*plugin, CLAP_EXT_GUI)));
    REQUIRE(gui != nullptr);
    REQUIRE(gui->create(&*plugin, CLAP_WINDOW_API_COCOA, false));

    auto &editor(*static_cast<LE::SW::SpectrumWorxCLAP &>(editorHostOf(*plugin)).gui());
    editor.showSettings(LE::SW::GUI::SpectrumWorxEditor::enginePageIndex);

    auto const render([&editor] {
        juce::Image image(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
        juce::Graphics graphics(image);
        editor.paintEntireComponent(graphics, true);
        return image;
    });

    auto const differing([](juce::Image const &left, juce::Image const &right) {
        juce::Image::BitmapData const a(left, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData const b(right, juce::Image::BitmapData::readOnly);
        std::size_t count{0};
        for (int y(0); y < left.getHeight(); ++y)
            for (int x(0); x < left.getWidth(); ++x)
                count += (a.getPixelColour(x, y) != b.getPixelColour(x, y));
        return count;
    });

    auto const stereo(render());

    REQUIRE(plugin.reconfigure(1));
    editor.updateEngineInformationIfChanged();

    // both the box under the LFO panel and the Engine page's fifth line
    CHECK(differing(stereo, render()) > 0);

    REQUIRE(plugin.reconfigure(2));
    editor.updateEngineInformationIfChanged();
    CHECK(differing(stereo, render()) == 0);

    gui->destroy(&*plugin);
}
