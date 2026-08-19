////////////////////////////////////////////////////////////////////////////////
///
/// \file waveformPainter.cpp
/// -------------------------
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "gui/painters/waveformPainter.hpp"

// juce::Drawable and juce::DrawablePath. \see the note on the declaration.
#include <juce_gui_basics/juce_gui_basics.h>

#include "le/utility/assert.hpp"

#include <algorithm>
#include <cstddef>

namespace LE::SW::GUI
{

using Waveform = WaveformPainter::Waveform;

namespace
{
using namespace WaveformStyle;

////////////////////////////////////////////////////////////////////////////////
///
/// \brief One point on the artwork's grid.
///
////////////////////////////////////////////////////////////////////////////////

struct Point
{
    float x, y;
};

////////////////////////////////////////////////////////////////////////////////
///
/// \name The seven marks that are a run of straight lines
///
/// \note Written as the polylines they are, rather than as the `H` and `V`
/// commands the files used: a horizontal line to 12.12 and a vertical one to
/// 13.14 are the same corner said twice, and the second form cannot be read
/// against a picture.
///
////////////////////////////////////////////////////////////////////////////////
///@{
Point constexpr triangle[]{{2.49f, 13.64f}, {12.11f, 3.99f}, {21.41f, 13.63f}};

Point constexpr sawtooth[]{{2.0f, 13.7f}, {20.88f, 4.79f}, {20.88f, 14.2f}};

Point constexpr reverseSaw[]{{3.12f, 14.2f}, {3.12f, 4.88f}, {21.61f, 13.6f}};

Point constexpr square[]{{2.19f, 4.62f}, {12.12f, 4.62f}, {12.12f, 13.14f}, {21.93f, 13.14f}};

Point constexpr randomHold[]{{2.88f, 10.92f},  {2.88f, 3.88f},  {9.62f, 3.88f}, {9.62f, 13.12f},
                             {17.12f, 13.12f}, {17.12f, 6.61f}, {21.99f, 6.61f}};

Point constexpr randomSlide[]{
    {2.31f, 9.46f}, {6.32f, 4.13f}, {11.84f, 13.07f}, {17.11f, 6.99f}, {21.77f, 12.12f}};

Point constexpr dirac[]{
    {2.01f, 13.19f}, {6.06f, 13.19f}, {8.91f, 5.09f}, {11.95f, 13.19f}, {21.99f, 13.19f}};

Point constexpr dIRAC[]{
    {2.01f, 3.81f}, {6.06f, 3.81f}, {8.91f, 11.91f}, {11.95f, 3.81f}, {21.99f, 3.81f}};
///@}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief One cubic segment: two control points and the point it ends on.
///
////////////////////////////////////////////////////////////////////////////////

struct Cubic
{
    Point control1, control2, end;
};

/// \brief The sine, which is one and a bit periods of a hand-drawn curve.
///@{
Point constexpr sineStart{2.82f, 9.4f};
Cubic constexpr sine[]{
    {{3.36f, 6.95f}, {3.46f, 4.18f}, {7.5f, 4.18f}},
    {{14.77f, 4.18f}, {9.29f, 13.14f}, {16.56f, 13.14f}},
    {{20.56f, 13.14f}, {20.7f, 10.42f}, {21.23f, 7.98f}},
};
///@}

////////////////////////////////////////////////////////////////////////////////
///
/// \name The exponent, which is a spike
///
///   Nine segments rising from the left to the point at x = 12, and then the
/// same nine mirrored -- which is what the file was, to the hundredth: it holds
/// eighteen and its two halves agree.
///
////////////////////////////////////////////////////////////////////////////////
///@{
float constexpr exponentAxis{12.0f}; ///< what the second half is mirrored about
Point constexpr exponentStart{2.61f, 13.04f};
Cubic constexpr exponentRise[]{
    {{3.35f, 13.0f}, {4.08f, 12.93f}, {4.81f, 12.78f}},
    {{5.45f, 12.65f}, {6.08f, 12.47f}, {6.72f, 12.13f}},
    {{7.26f, 11.85f}, {7.8f, 11.46f}, {8.33f, 10.88f}},
    {{8.77f, 10.4f}, {9.21f, 9.79f}, {9.65f, 8.99f}},
    {{10.0f, 8.37f}, {10.34f, 7.64f}, {10.68f, 6.8f}},
    {{10.92f, 6.2f}, {11.17f, 5.54f}, {11.41f, 4.89f}},
    {{11.56f, 4.49f}, {11.71f, 4.09f}, {11.85f, 3.76f}},
    {{11.9f, 3.65f}, {11.95f, 3.48f}, {12.0f, 3.48f}},
};
///@}

/// \brief RandomWhacko's ten discs, as centres on the grid.
///
/// \note Scattered rather than laid out, which is the joke: the file's ten sit
/// at no interval anything else in the skin uses, and reproducing them exactly
/// is the only way the mark stays the one a user recognises.
Point constexpr whackoDiscs[]{{4.82f, 4.18f},   {12.41f, 5.06f}, {19.71f, 4.62f}, {7.0f, 6.86f},
                              {16.02f, 7.5f},   {10.47f, 9.64f}, {3.32f, 10.1f},  {13.61f, 12.4f},
                              {17.86f, 11.73f}, {8.0f, 12.86f}};

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Maps the artwork's grid onto \p bounds.
///
////////////////////////////////////////////////////////////////////////////////

class Grid
{
  public:
    explicit Grid(juce::Rectangle<float> const bounds)
        : originX_(bounds.getX()), originY_(bounds.getY()), scaleX_(bounds.getWidth() / gridWidth),
          scaleY_(bounds.getHeight() / gridHeight)
    {
    }

    juce::Point<float> operator()(Point const point) const
    {
        return {originX_ + point.x * scaleX_, originY_ + point.y * scaleY_};
    }

    /// \note The smaller of the two, so that a pen on a rectangle that is not
    /// the grid's shape stays a pen rather than becoming an ellipse.
    float scalar() const { return std::min(scaleX_, scaleY_); }

  private:
    float originX_, originY_, scaleX_, scaleY_;
}; // class Grid

template <std::size_t N>
void addPolyline(juce::Path &path, Grid const &grid, Point const (&points)[N])
{
    path.startNewSubPath(grid(points[0]));
    for (std::size_t i(1); i < N; ++i)
        path.lineTo(grid(points[i]));
}

template <std::size_t N>
void addCubics(juce::Path &path, Grid const &grid, Point const start, Cubic const (&curve)[N],
               bool const mirrored = false)
{
    auto const at(
        [&](Point const p) { return grid(mirrored ? Point{2 * exponentAxis - p.x, p.y} : p); });

    if (!mirrored)
        path.startNewSubPath(at(start));

    for (std::size_t i(0); i < N; ++i)
    {
        //   Backwards when mirrored: the curve is being retraced from its apex
        // outwards, so each segment's control points swap roles with it.
        auto const &segment(curve[mirrored ? (N - 1 - i) : i]);
        if (mirrored)
        {
            auto const &previousEnd(i + 1 < N ? curve[N - 2 - i].end : start);
            path.cubicTo(at(segment.control2), at(segment.control1), at(previousEnd));
        }
        else
        {
            path.cubicTo(at(segment.control1), at(segment.control2), at(segment.end));
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The stroked marks -- everything but RandomWhacko.
///
////////////////////////////////////////////////////////////////////////////////

juce::Path strokedPath(Waveform const waveform, juce::Rectangle<float> const bounds)
{
    Grid const grid(bounds);
    juce::Path path;

    switch (waveform)
    {
    case Waveform::sine:
        addCubics(path, grid, sineStart, sine);
        break;
    case Waveform::triangle:
        addPolyline(path, grid, triangle);
        break;
    case Waveform::sawtooth:
        addPolyline(path, grid, sawtooth);
        break;
    case Waveform::reverseSaw:
        addPolyline(path, grid, reverseSaw);
        break;
    case Waveform::square:
        addPolyline(path, grid, square);
        break;
    case Waveform::exponent:
        addCubics(path, grid, exponentStart, exponentRise);
        addCubics(path, grid, exponentStart, exponentRise, true /*mirrored*/);
        break;
    case Waveform::randomHold:
        addPolyline(path, grid, randomHold);
        break;
    case Waveform::randomSlide:
        addPolyline(path, grid, randomSlide);
        break;
    case Waveform::dirac:
        addPolyline(path, grid, dirac);
        break;
    case Waveform::dIRAC:
        addPolyline(path, grid, dIRAC);
        break;

    case Waveform::randomWhacko: // filled, not stroked
    case Waveform::count:
        LE_ASSERT_MSG(false, "not a stroked waveform");
        break;
    }

    return path;
}

} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
//
// WaveformPainter::paint()
// ------------------------
//
////////////////////////////////////////////////////////////////////////////////

void WaveformPainter::paint(juce::Graphics &graphics, Waveform const waveform,
                            juce::Rectangle<float> const bounds, juce::Colour const colour)
{
    LE_ASSERT(waveform < Waveform::count);

    graphics.setColour(colour);

    Grid const grid(bounds);

    if (waveform == Waveform::randomWhacko)
    {
        auto const radius(whackoRadius * grid.scalar());
        for (auto const centre : whackoDiscs)
        {
            auto const at(grid(centre));
            graphics.fillEllipse(at.x - radius, at.y - radius, 2 * radius, 2 * radius);
        }
        return;
    }

    /// \note Rounded at both the ends and the joins, which is what the files
    /// asked for by leaving `stroke-linecap` and `stroke-linejoin` unset --
    /// SVG's default is butt and miter, and JUCE's PathStrokeType default is
    /// mitered, so this is the one place the two disagree and the artwork's
    /// square corners are what wins. \see the square and the dirac, whose
    /// corners are the whole of what they say.
    graphics.strokePath(strokedPath(waveform, bounds),
                        juce::PathStrokeType(strokeOnGrid * grid.scalar(),
                                             juce::PathStrokeType::mitered,
                                             juce::PathStrokeType::butt));
}

////////////////////////////////////////////////////////////////////////////////
//
// WaveformPainter::drawable()
// ---------------------------
//
////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<juce::Drawable> WaveformPainter::drawable(Waveform const waveform,
                                                          juce::Colour const colour)
{
    juce::Rectangle<float> const canvas(0.0f, 0.0f, static_cast<float>(iconWidth),
                                        static_cast<float>(iconHeight));
    Grid const grid(canvas);

    auto shape(std::make_unique<juce::DrawablePath>());

    if (waveform == Waveform::randomWhacko)
    {
        juce::Path discs;
        auto const radius(whackoRadius * grid.scalar());
        for (auto const centre : whackoDiscs)
        {
            auto const at(grid(centre));
            discs.addEllipse(at.x - radius, at.y - radius, 2 * radius, 2 * radius);
        }
        shape->setPath(discs);
        shape->setFill(colour);
        return shape;
    }

    //   Stroked into a filled outline rather than left as a stroke, because a
    // DrawablePath's stroke is scaled with the drawable when a menu resizes it
    // and its fill is not; the two look different at any size but the one the
    // outline was made at. \see Artwork::draw(), which does the scaling.
    juce::Path outline;
    juce::PathStrokeType(strokeOnGrid * grid.scalar(), juce::PathStrokeType::mitered,
                         juce::PathStrokeType::butt)
        .createStrokedPath(outline, strokedPath(waveform, canvas));

    shape->setPath(outline);
    shape->setFill(colour);
    return shape;
}

} // namespace LE::SW::GUI
