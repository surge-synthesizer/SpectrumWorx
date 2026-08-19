////////////////////////////////////////////////////////////////////////////////
///
/// \file main.cpp
/// --------------
///
///   sw-show-ui: a window that hosts SpectrumWorx's GUI with no plugin, no host
/// and no audio underneath it.
///
///   Stage 6 ports 10 k lines of JUCE 2.1.2 widget code to JUCE 8, and the
/// plugin those widgets belong to does not exist yet -- the CLAP host layer is
/// stage 5. Waiting for it would put the largest stage on the critical path for
/// no reason, so the GUI is built against a mock instead, in a process a
/// developer can launch, resize and quit in a second.
///
///   Pages are registered by the code they exercise, so a widget becomes
/// viewable the moment it compiles rather than when the whole editor does:
///
///     sw-show-ui                          the default page, in a window
///     sw-show-ui --list                   what there is
///     sw-show-ui gallery                  a named page
///     sw-show-ui --render gallery out.png offscreen, no window server
///
///   --render exists because a window is not always available: CI has no
/// display, and neither does a sandboxed agent. It paints the page into an
/// image and writes a PNG, which is the only way some environments can see
/// whether a widget draws at all. It is also what makes a rendering regression
/// test possible later.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "page.hpp"

#include "gui/colourMap.hpp"
#include "gui/preferences.hpp"
#include "gui/theme.hpp"
#include "io/jucePath.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <system_error>
//------------------------------------------------------------------------------
namespace
{
//------------------------------------------------------------------------------

void listPages()
{
    std::fputs("sw-show-ui pages:\n", stderr);
    for (auto const &page : SWShowUI::pages())
        std::fprintf(stderr, "  %-16s %s\n", page.name, page.description);
}

/// Resolves a page name to a page, complaining usefully when it cannot.
SWShowUI::Page const *resolve(juce::String const &name)
{
    if (auto const *const page = SWShowUI::findPage(name))
        return page;
    std::fprintf(stderr, "sw-show-ui: no page named '%s'.\n", name.toRawUTF8());
    listPages();
    return nullptr;
}

//------------------------------------------------------------------------------
// Windowed mode
//------------------------------------------------------------------------------

/// A plain resizable desktop window. The editor is not an AudioProcessorEditor
/// and never will be -- this is the clap-first route -- so there is nothing to
/// attach here beyond a Component.
class HarnessWindow final : public juce::DocumentWindow
{
  public:
    HarnessWindow(juce::String const &name, std::unique_ptr<juce::Component> content)
        : juce::DocumentWindow(name, juce::Colours::darkgrey, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        auto const width(content->getWidth() ? content->getWidth() : 900);
        auto const height(content->getHeight() ? content->getHeight() : 600);
        setContentOwned(content.release(), true);
        setResizable(true, false);
        centreWithSize(width, height);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
}; // class HarnessWindow

class ShowUIApplication final : public juce::JUCEApplication
{
  public:
    juce::String const getApplicationName() override { return "sw-show-ui"; }
    juce::String const getApplicationVersion() override { return "0.1"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(juce::String const &commandLine) override
    {
        auto const requested(firstToken(commandLine));
        if (requested == "--list")
        {
            listPages();
            quit();
            return;
        }

        auto const *const page(resolve(requested));
        if (!page)
        {
            setApplicationReturnValue(2);
            quit();
            return;
        }

        window_ = std::make_unique<HarnessWindow>("SpectrumWorx -- " + juce::String(page->name),
                                                  page->construct());
    }

    void shutdown() override
    {
        window_.reset();
        // Before JUCE's leak detector runs: the skin cache holds juce::Images
        // and Typefaces, and a namespace-scope cache would otherwise be
        // destroyed after the JUCE that allocated them.
        LE::SW::GUI::Theme::destroySingleton();
    }
    void systemRequestedQuit() override { quit(); }

  private:
    /// The command line arrives as one string; take the first non-empty token.
    static juce::String firstToken(juce::String const &commandLine)
    {
        for (auto const &token : juce::StringArray::fromTokens(commandLine, true))
            if (token.isNotEmpty())
                return token;
        return SWShowUI::defaultPageName();
    }

    std::unique_ptr<HarnessWindow> window_;
}; // class ShowUIApplication

//------------------------------------------------------------------------------
// Offscreen mode
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
///
/// \brief What a rendered page actually put on the canvas.
///
///   `--render` used to write a PNG and return 0, so a page that painted solid
/// black passed and so did a page that painted nothing at all -- which is most of
/// how GUI code fails.
///
/// \note The modal colour rather than a distinct-colour count. A count is easy to
/// satisfy by accident: antialiasing one glyph over an otherwise empty panel
/// produces dozens of colours and would pass a "more than sixteen" rule. "What
/// fraction of the canvas is *not* the single commonest colour" is the question
/// being asked -- how much was drawn -- and it degrades gracefully: a flat
/// background is the modal colour whatever that background happens to be.
///
////////////////////////////////////////////////////////////////////////////////

struct Ink
{
    std::size_t pixels{0};
    std::size_t distinctColours{0};
    /// The commonest colour and how much of the canvas it covers.
    juce::uint32 modalColour{0};
    std::size_t modalPixels{0};
    /// Pixels the page left fully transparent.
    std::size_t transparentPixels{0};

    double drawnFraction() const
    {
        return pixels ? double(pixels - modalPixels) / double(pixels) : 0;
    }
}; // struct Ink

Ink measure(juce::Image const &image)
{
    Ink ink;
    std::map<juce::uint32, std::size_t> histogram;

    juce::Image::BitmapData const pixels(image, juce::Image::BitmapData::readOnly);
    for (int y(0); y < image.getHeight(); ++y)
        for (int x(0); x < image.getWidth(); ++x)
        {
            auto const colour(pixels.getPixelColour(x, y));
            ++histogram[colour.getARGB()];
            ink.transparentPixels += (colour.getAlpha() == 0);
            ++ink.pixels;
        }

    ink.distinctColours = histogram.size();
    for (auto const &[colour, count] : histogram)
        if (count > ink.modalPixels)
        {
            ink.modalPixels = count;
            ink.modalColour = colour;
        }
    return ink;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Whether \p ink is enough to call the page drawn, complaining usefully
/// when it is not.
///
/// \note One thousandth of the canvas, which is 476 pixels of the 845 x 564
/// editor. Deliberately far below what the pages actually measure -- theme is the
/// thinnest at 12.0 %, skin 13.9 %, and the five editor pages 73-81 % -- because
/// the failure this is for is *nothing was drawn*, not "less was drawn than last
/// time". A tighter bound would be a golden, and a golden over a whole editor is
/// a test that fails on every legitimate change. Which is the point of the
/// margin: skin was 17.5 % until the About page's three bitmaps left the skin on
/// 15.08.2026, and that is a change no test should have had an opinion about.
///                                           (measured 15.08.2026.) (SW port)
///
////////////////////////////////////////////////////////////////////////////////

bool drewSomething(Ink const &ink, char const *const pageName)
{
    constexpr double leastDrawnFraction{0.001};

    if (ink.distinctColours <= 1)
    {
        std::fprintf(stderr, "sw-show-ui: '%s' painted one colour (%08x) over the whole page.\n",
                     pageName, ink.modalColour);
        return false;
    }
    if (ink.transparentPixels == ink.pixels)
    {
        std::fprintf(stderr, "sw-show-ui: '%s' painted nothing -- every pixel is transparent.\n",
                     pageName);
        return false;
    }
    if (ink.drawnFraction() < leastDrawnFraction)
    {
        std::fprintf(stderr,
                     "sw-show-ui: '%s' is %.3f %% drawn, which is under the %.1f %% floor: %zu of "
                     "%zu pixels are the background colour %08x.\n",
                     pageName, 100 * ink.drawnFraction(), 100 * leastDrawnFraction, ink.modalPixels,
                     ink.pixels, ink.modalColour);
        return false;
    }
    return true;
}

int renderPage(juce::String const &pageName, juce::File const &output)
{
    auto const *const page(resolve(pageName));
    if (!page)
        return 2;

    auto const component(page->construct());
    if (component->getWidth() <= 0 || component->getHeight() <= 0)
        component->setSize(900, 600);

    /// \note SoftwareImageType explicitly, rather than whatever a juce::Image
    /// defaults to. That default is the *native* type -- CoreGraphics on macOS
    /// and the software renderer everywhere else -- and the two do not round a
    /// transformed clip region the same way, so these pages were rendering by
    /// one code path here and another on CI. The editor is drawn through a
    /// scale transform now, which is exactly where the two diverge: a preset
    /// browser row past the end of the listing got a sliver of clip to paint in
    /// under the software renderer and none under CoreGraphics, and the
    /// assertion that caught it could only ever fire on Linux. Same renderer
    /// everywhere means a red CI run can be reproduced here.
    juce::Image image(juce::Image::ARGB, component->getWidth(), component->getHeight(), true,
                      juce::SoftwareImageType{});
    {
        juce::Graphics graphics(image);
        component->paintEntireComponent(graphics, true);
    }

    /// \note Before the PNG is written rather than after, so that a failing page
    /// still leaves the image that shows why.
    auto const ink(measure(image));
    std::fprintf(stderr, "sw-show-ui: %s is %.1f %% drawn, %zu colours\n", page->name,
                 100 * ink.drawnFraction(), ink.distinctColours);

    output.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(output.createOutputStream());
    if (!stream || stream->failedToOpen())
    {
        std::fprintf(stderr, "sw-show-ui: cannot write '%s'.\n",
                     output.getFullPathName().toRawUTF8());
        return 1;
    }
    if (!juce::PNGImageFormat().writeImageToStream(image, *stream))
    {
        std::fputs("sw-show-ui: PNG encoding failed.\n", stderr);
        return 1;
    }

    std::fprintf(stderr, "sw-show-ui: wrote %d x %d to %s\n", image.getWidth(), image.getHeight(),
                 output.getFullPathName().toRawUTF8());

    /// \note Last, and it is what makes `--render` a test rather than a
    /// screenshot: the PNG is on disk either way, so a red case can be looked at.
    return drewSomething(ink, page->name) ? 0 : 1;
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Points the interface preferences at an empty folder beside the PNG.
///
///   A render has to be a function of the build and nothing else. The editor
/// reads GUI::preferences() -- the settings panel's Interface page shows them,
/// and the zoom among them decides how big the whole image is -- so without this
/// `--render` would answer differently on every machine, according to what
/// whoever ran it last chose in a DAW. `ctest` runs these.
///
/// \note Only `--render`. Run interactively, sw-show-ui is looking at the real
/// editor and should use the real preferences, zoom included.
///
/// \note `SW_SHOW_UI_ZOOM` then puts one back, for looking at a page at a size
/// other than the plugin's normal one. Same shape as SW_SHOW_UI_SETTINGS_PAGE
/// and SW_SHOW_UI_PRESET_BANK, and an unoffered percentage is ignored -- \see
/// GUI::Preferences::zoomPercentages.
///                                           (16.08.2026.) (SW port)
///
/// \note `SW_SHOW_UI_PALETTE` likewise, by ColourMap::nameOf() -- so
/// `SW_SHOW_UI_PALETTE=Reds` renders any page in that palette. Both the
/// preference and the map are set: the map because a page that builds no editor
/// has no SkinLifetime to read the preference, and the preference so that the
/// settings page shows the palette it is being drawn in.
///
////////////////////////////////////////////////////////////////////////////////

void usePreferencesOfNobodyInParticular(juce::File const &output)
{
    auto const folder(LE::IO::juceFileToPath(output.getParentDirectory()) / "show-ui-preferences");
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    LE::SW::GUI::setPreferencesFolder(folder);

    if (auto const *const requested = std::getenv("SW_SHOW_UI_ZOOM"))
    {
        auto const percent(static_cast<unsigned int>(std::atoi(requested)));
        if (LE::SW::GUI::Preferences::isOfferedZoom(percent))
            LE::SW::GUI::preferences().setZoomPercent(percent);
        else
            std::fprintf(stderr, "sw-show-ui: SW_SHOW_UI_ZOOM=%s is not an offered zoom.\n",
                         requested);
    }

    if (auto const *const requested = std::getenv("SW_SHOW_UI_PALETTE"))
    {
        using ColourMap = LE::SW::GUI::ColourMap;
        for (unsigned int index(0); index < ColourMap::numberOfPalettes; ++index)
        {
            auto const palette(static_cast<ColourMap::Palette>(index));
            if (std::strcmp(requested, ColourMap::nameOf(palette)) != 0)
                continue;

            LE::SW::GUI::preferences().setPalette(palette);
            ColourMap::setPalette(palette);
            return;
        }
        std::fprintf(stderr,
                     "sw-show-ui: SW_SHOW_UI_PALETTE=%s is not a palette. There are:", requested);
        for (unsigned int index(0); index < ColourMap::numberOfPalettes; ++index)
            std::fprintf(stderr, " %s", ColourMap::nameOf(static_cast<ColourMap::Palette>(index)));
        std::fputs("\n", stderr);
    }
}

int render(juce::String const &pageName, juce::File const &output)
{
    // No JUCEApplication and no message loop: paintEntireComponent() needs a
    // graphics context and a font engine, which ScopedJuceInitialiser_GUI
    // provides, and nothing else.
    juce::ScopedJuceInitialiser_GUI const juceInitialiser;
    usePreferencesOfNobodyInParticular(output);
    auto const result(renderPage(pageName, output));
    // Inside the initialiser's lifetime, so the images and typefaces the page
    // cached are gone before JUCE's leak detector looks. Getting this wrong is
    // not cosmetic: JUCE is a static-destruction order hazard, and the
    // resources genuinely outlive it otherwise.
    LE::SW::GUI::Theme::destroySingleton();
    return result;
}

//------------------------------------------------------------------------------
} // anonymous namespace
//------------------------------------------------------------------------------

// What START_JUCE_APPLICATION expands to, opened up so that --render can run
// before any of it: JUCEApplicationBase::main() creates an NSApplication on
// macOS, which offscreen rendering neither needs nor should require.
juce::JUCEApplicationBase *juce_CreateApplication();
juce::JUCEApplicationBase *juce_CreateApplication() { return new ShowUIApplication(); }

int main(int argc, char *argv[])
{
    if ((argc >= 2) && (std::strcmp(argv[1], "--render") == 0))
    {
        if (argc != 4)
        {
            std::fputs("usage: sw-show-ui --render <page> <output.png>\n", stderr);
            return 2;
        }
        juce::String const path(argv[3]);
        return render(juce::String(argv[2]),
                      juce::File::isAbsolutePath(path)
                          ? juce::File(path)
                          : juce::File::getCurrentWorkingDirectory().getChildFile(path));
    }

    juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;

    /// \note Two overloads, and Windows takes the other one. JUCE guards
    /// `main( argc, argv )` with `JUCE_WINDOWS && ! defined( _CONSOLE )` and
    /// compiles it only in the arm this build is not in: JUCE is built once, in
    /// sw-juce, which is a library and defines no such thing. The no-argument
    /// overload sits outside that guard and exists in every configuration.
    ///
    ///   Nothing is lost by taking it. What the argument-taking one adds is
    /// juce_argc / juce_argv and, on macOS, initialiseNSApplication() -- and in
    /// this arm JUCE reads the command line from GetCommandLineW() rather than
    /// from those two, so on Windows there is nothing left to hand it. Off
    /// Windows the guard never applies whatever _CONSOLE says, so those
    /// platforms keep the overload that sets them.
#if JUCE_WINDOWS
    return juce::JUCEApplicationBase::main();
#else
    return juce::JUCEApplicationBase::main(argc, const_cast<char const **>(argv));
#endif
}
