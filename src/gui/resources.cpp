////////////////////////////////////////////////////////////////////////////////
///
/// \file resources.cpp
/// -------------------
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "resources.hpp"

#include "gui/colourMap.hpp"
#include "gui/painters/waveformPainter.hpp"

#include "le/utility/assert.hpp"

#include <cmrc/cmrc.hpp>

// juce::Drawable, for the skin files that have been redrawn as vectors: it
// lives in gui_basics rather than in graphics, which resources.hpp includes.
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdio>
//------------------------------------------------------------------------------
CMRC_DECLARE(swAssets);

namespace LE::SW::GUI
{

namespace
{
cmrc::embedded_filesystem skin() { return cmrc::swAssets::get_filesystem(); }

/// \brief Reads one embedded file, or returns an empty span if it is not there.
///
/// \note assets/skin has gaps -- 2, 3, 12, 15, 18, 19, 20, 25, 26, 29, 36 to 39
/// and 54 are not there -- so a miss is a normal answer for a number in range,
/// not an error.
std::pair<char const *, std::size_t> embeddedFile(juce::String const &path)
{
    auto const filesystem(skin());
    auto const name(path.toStdString());
    if (!filesystem.exists(name))
        return {nullptr, 0};
    auto const file(filesystem.open(name));
    return {file.begin(), static_cast<std::size_t>(file.end() - file.begin())};
}

/// \brief One skin file, by name.
///
/// \note The resource library is rooted at assets/ and holds the factory
/// presets and samples too, hence the prefix. The one file that does not take
/// it is the logo, which lives beside skin/ rather than in it. \see
/// logoArtwork().
std::pair<char const *, std::size_t> skinFile(juce::String const &name)
{
    return embeddedFile("skin/" + name);
}

/// \brief Draws one embedded SVG through a juce::Drawable.
///
/// \note The canvas is taken from the <svg> width/height, not from
/// Drawable::getDrawableBounds(): a DrawableComposite computes that as the
/// union of its children, which is the ink rather than the page. The two
/// differ for every one of these buttons -- 09.svg's pill stops short of its
/// own edges -- and sizing by the ink would crop the file and move every
/// widget that lays itself out from image.getWidth(). JUCE's parser has
/// already set the drawable's content area to the viewBox (juce_SVGParser.cpp,
/// parseSVGElement), so drawing it at the origin lands where the file says.
Artwork loadVector(char const *const data, std::size_t const size)
{
    auto const svg(
        juce::parseXML(juce::String::createStringFromData(data, static_cast<int>(size))));
    if (svg == nullptr)
    {
        LE_ASSERT_MSG(false, "Malformed skin vector.");
        return {};
    }

    auto drawable(juce::Drawable::createFromSVG(*svg));
    if (drawable == nullptr)
    {
        LE_ASSERT_MSG(false, "Unrenderable skin vector.");
        return {};
    }

    auto width(svg->getDoubleAttribute("width"));
    auto height(svg->getDoubleAttribute("height"));
    if ((width <= 0) || (height <= 0)) // width/height are optional; the viewBox is the fallback
    {
        auto const box(juce::StringArray::fromTokens(svg->getStringAttribute("viewBox"), " ,", {}));
        if (box.size() == 4)
        {
            width = box[2].getDoubleValue();
            height = box[3].getDoubleValue();
        }
    }
    LE_ASSERT_MSG((width > 0) && (height > 0), "Skin vector has no size.");
    if ((width <= 0) || (height <= 0))
        return {};

    return Artwork(std::move(drawable), juce::roundToInt(width), juce::roundToInt(height));
}

////////////////////////////////////////////////////////////////////////////////
//
// loadArtwork()
// -------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief One skin file, which is a vector.
///
/// \note **`NN.png` was the other half of this and there is no longer any.**
/// The skin was converted file by file, so both forms were embedded and the
/// vector won where there was one; the last bitmap went with the module knobs'
/// film strips and the About page, and what was left was a PNG decoder, a
/// debug-only check that a redrawn file had kept its bitmap's canvas, and a
/// glob that had nothing to pick up. Restoring the bitmap path is a dozen lines
/// if the skin ever wants a photograph.
///                                       (18.08.2026.)
///
/// \note Two things went with it that are worth not having to rediscover. The
/// 2016 loader ran an in-place `pow( x, 2.2 / 1.8 )` over every byte of every
/// bitmap on macOS, to convert artwork authored for a PC's gamma to the Mac's
/// -- a correction Apple made wrong in 2009 and which walked the buffer with no
/// regard for pixel stride, so it gamma-corrected the alpha channel too. And
/// the canvas check was not hypothetical: a vector that comes back a different
/// size from the bitmap it replaced moves controls around the editor silently.
/// What guards that now is the size assertion in loadVector() and the case in
/// skinTests.cpp that walks the whole numbering.
///
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The eleven LFO waveforms, which are drawn rather than read.
///
/// \note They keep their file numbers -- 43 to 53 -- so that
/// `resourceArtwork<LFOSine>()` and the waveform menu did not have to change
/// when the files went. What a number means here is "the mark that used to be
/// in that file", and this is the only place that knows the difference.
///                                       (19.08.2026.)
///
/// \note ColourMap::LFOWaveform, which is the white every one of those files
/// stroked in. It is neutral by design and has to stay so -- \see the note on
/// the enumerator: this drawable is cached and the cache outlives a palette
/// change. Tinting is Artwork::draw()'s and stays there.
///
////////////////////////////////////////////////////////////////////////////////

Artwork loadWaveform(unsigned int const number)
{
    auto const waveform(static_cast<WaveformPainter::Waveform>(number - unsigned{GUI::LFOSine}));
    LE_ASSERT(waveform < WaveformPainter::Waveform::count);

    return {WaveformPainter::drawable(waveform, ColourMap::getColour(ColourMap::LFOWaveform)),
            WaveformStyle::iconWidth, WaveformStyle::iconHeight};
}

Artwork loadArtwork(unsigned int const number)
{
    if ((number >= GUI::LFOSine) && (number <= GUI::LFOdIRAC))
        return loadWaveform(number);

    // "01" ... "62": zero padded to two digits, as the files are named.
    auto const stem(juce::String(number).paddedLeft('0', 2));

    auto const [vector, size](skinFile(stem + ".svg"));
    if (!vector)
        return {}; // a gap in the numbering, not an error -- see the header

    return loadVector(vector, size);
}

std::array<Artwork, numberOfResourceBitmaps + 1> artworkCache;

juce::Typeface::Ptr loadTypeface(juce::String const &name, juce::Typeface::Ptr &cache)
{
    if (cache == nullptr)
    {
        auto const [data, size](skinFile(name));
        if (data)
            cache = juce::Typeface::createSystemTypefaceFor(data, size);
        LE_ASSERT_MSG(cache != nullptr, "Cannot load the skin typeface.");
    }
    return cache;
}

juce::Typeface::Ptr regularTypefaceCache;
juce::Typeface::Ptr boldTypefaceCache;

Artwork logoCache;
} // anonymous namespace

Artwork::Artwork() = default;
Artwork::Artwork(Artwork &&) noexcept = default;
Artwork &Artwork::operator=(Artwork &&) noexcept = default;
Artwork::~Artwork() = default;

Artwork::Artwork(std::unique_ptr<juce::Drawable> drawable, int const width, int const height)
    : drawable_(std::move(drawable)), width_(width), height_(height)
{
}

bool Artwork::isValid() const { return (drawable_ != nullptr) || image_.isValid(); }
bool Artwork::isVector() const { return drawable_ != nullptr; }

void Artwork::draw(juce::Graphics &graphics, int const x, int const y, float const opacity,
                   juce::Colour const overlay) const
{
    if (!isValid())
        return;

    //   The vector path, and the reason any of this exists: the Drawable is
    // painted into the caller's context, so it is rasterised at whatever scale
    // that context is really rendering at rather than at the 1x the skin was
    // drawn for.
    if (isVector() && overlay.isTransparent())
    {
        drawable_->draw(
            graphics, opacity,
            juce::AffineTransform::translation(static_cast<float>(x), static_cast<float>(y)));
        return;
    }

    //   Tinting means filling the artwork's alpha with a colour, which needs
    // the pixels. Three buttons ask for it, on mouse-over, and none of their
    // files are vectors yet; if one becomes one this rasterises it at 1x,
    // which is what it did before it was a vector at all.
    auto const &pixels(image());
    if (!pixels.isValid())
        return;

    auto const target(juce::Rectangle<int>(x, y, width_, height_).toFloat());
    auto const placement(juce::RectanglePlacement(juce::RectanglePlacement::stretchToFit)
                             .getTransformToFit(pixels.getBounds().toFloat(), target));

    if (!overlay.isOpaque())
    {
        graphics.setOpacity(opacity);
        graphics.drawImageTransformed(pixels, placement, false);
    }
    if (!overlay.isTransparent())
    {
        graphics.setColour(overlay);
        graphics.drawImageTransformed(pixels, placement, true);
    }
}

void Artwork::drawScaled(juce::Graphics &graphics, juce::Rectangle<int> const target,
                         juce::Rectangle<int> const source, float const opacity) const
{
    if (!isValid() || target.isEmpty() || source.isEmpty())
        return;

    if (isVector())
    {
        juce::Graphics::ScopedSaveState const state(graphics);
        graphics.reduceClipRegion(target);
        auto const scaleX(static_cast<float>(target.getWidth()) /
                          static_cast<float>(source.getWidth()));
        auto const scaleY(static_cast<float>(target.getHeight()) /
                          static_cast<float>(source.getHeight()));
        drawable_->draw(
            graphics, opacity,
            juce::AffineTransform::translation(static_cast<float>(-source.getX()),
                                               static_cast<float>(-source.getY()))
                .scaled(scaleX, scaleY)
                .translated(static_cast<float>(target.getX()), static_cast<float>(target.getY())));
        return;
    }

    auto const &pixels(image());
    if (!pixels.isValid())
        return;
    graphics.setOpacity(opacity);
    graphics.drawImage(pixels, target.getX(), target.getY(), target.getWidth(), target.getHeight(),
                       source.getX(), source.getY(), source.getWidth(), source.getHeight());
}

void Artwork::drawWithin(juce::Graphics &graphics, juce::Rectangle<float> const area) const
{
    if (drawable_ != nullptr)
        drawable_->drawWithin(graphics, area, juce::RectanglePlacement::centred, 1.0f);
}

std::unique_ptr<juce::Drawable> Artwork::drawableCopy() const
{
    return (drawable_ != nullptr) ? drawable_->createCopy() : nullptr;
}

juce::Image const &Artwork::image() const
{
    if ((drawable_ != nullptr) && !image_.isValid() && (width_ > 0) && (height_ > 0))
    {
        image_ = juce::Image(juce::Image::ARGB, width_, height_, true);
        juce::Graphics graphics(image_);
        drawable_->draw(graphics, 1.0f);
    }
    return image_;
}

Artwork const &resourceArtwork(unsigned int const number)
{
    LE_ASSERT(number <= numberOfResourceBitmaps);
    static Artwork const invalid;
    if (number > numberOfResourceBitmaps)
        return invalid;

    auto &cached(artworkCache[number]);
    if (!cached.isValid())
        cached = loadArtwork(number);
    return cached;
}

juce::Image const &resourceBitmap(unsigned int const number)
{
    return resourceArtwork(number).image();
}

bool hasResourceBitmap(unsigned int const number)
{
    return (number <= numberOfResourceBitmaps) && resourceArtwork(number).isValid();
}

bool resourceIsVector(unsigned int const number)
{
    return (number <= numberOfResourceBitmaps) && resourceArtwork(number).isVector();
}

Artwork const &logoArtwork()
{
    if (!logoCache.isValid())
    {
        auto const [data, size](embeddedFile("LOGO.svg"));
        LE_ASSERT_MSG(data, "The logo is not embedded.");
        if (data)
            logoCache = loadVector(data, size);
    }
    return logoCache;
}

juce::Typeface::Ptr regularTypeface() { return loadTypeface("Vera.ttf", regularTypefaceCache); }
juce::Typeface::Ptr boldTypeface() { return loadTypeface("VeraBd.ttf", boldTypefaceCache); }

void releaseCachedResources()
{
    for (auto &artwork : artworkCache)
        artwork = Artwork();
    logoCache = Artwork();
    regularTypefaceCache = nullptr;
    boldTypefaceCache = nullptr;
}

} // namespace LE::SW::GUI
