////////////////////////////////////////////////////////////////////////////////
///
/// \file glyphPainter.hpp
/// ----------------------
///
///   The small marks that stand for a word: the four in the preset browser's
/// navigation row, and the folder in front of a directory in its list.
///
///   None of these came out of the skin -- there was no navigation row to draw
/// them in. They are issue #44's mock-up, measured off it.
///
/// \note The numbers below *are* the mock-up's, and were the mock-up's divided
/// by 1.5 until 19.08.2026: the editor was drawn through that transform, so a
/// mark had to be written down at two thirds of its intended size to land at
/// the right one. The skin is its own coordinate system now and the division is
/// gone -- a nine pixel head on the user is nine here and nine on screen.
///
/// \note Colour is the caller's, unlike every other painter here. A glyph in
/// this row *is* its state -- white for off and the accent for on, which is
/// what issue #44 asks for -- so there is nothing for the painter to look up.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef glyphPainter_hpp__5C1A8E37_49B2_4D6F_A0E3_2B7C4F91D6A8
#define glyphPainter_hpp__5C1A8E37_49B2_4D6F_A0E3_2B7C4F91D6A8
//------------------------------------------------------------------------------
#include <juce_graphics/juce_graphics.h>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \namespace GlyphStyle
///
////////////////////////////////////////////////////////////////////////////////

/// \brief Each mark's ink, in skin pixels, inside the widget that holds it.
///
/// \note The ink is centred in its widget rather than filling it: a glyph is a
/// click target as well as a drawing, and sixteen pixels square is the smaller
/// of those two jobs done. \see GlyphPainter, every entry point of which insets
/// to the size below before drawing.
namespace GlyphStyle
{
/// \name The widgets the marks are drawn in
///
/// \note All one height, which is the row's. The widths differ because the
/// marks do: what they have in common is that each is a little wider than its
/// ink, so that a pointer aimed at a five pixel arrowhead lands on something.
///
/// \note Save and Delete joined the row as marks in issue #56, and the frame
/// that held them as words -- with the list rows it cost -- went. \see
/// PresetBrowser's constructor for the x positions.
///@{
int constexpr rowTop{39};
int constexpr rowHeight{24};
int constexpr upWidgetWidth{24};
int constexpr saveWidgetWidth{24};
int constexpr trashWidgetWidth{24};
int constexpr userWidgetWidth{26};
int constexpr jogWidgetWidth{17};
///@}

/// \brief The padlock, which is not in that row: it is the editor's, beside the
/// sidechain source. \see BackgroundPainter::sideChainLockBounds().
///@{
int constexpr lockWidgetWidth{21};
int constexpr lockWidgetHeight{24};
///@}

////////////////////////////////////////////////////////////////////////////////
/// \name Undo and redo
///
///   A hook: up and round from the foot of the bend, back along the top, and a
/// head on the end of it.
///
///     <====.
///          |
///          '
///
/// \note The head is centred *on* the bar rather than hanging off one side of
/// it, and is a good deal wider than the bar is thick. Both are what make it
/// read as an arrow at sixteen pixels rather than as a line that thickens.
///
////////////////////////////////////////////////////////////////////////////////
///@{
float constexpr undoWidth{18.0f};
float constexpr undoHeight{16.0f};
float constexpr undoStroke{3.0f};
float constexpr undoHeadLength{6.5f};
float constexpr undoHeadWidth{11.0f};
///@}

////////////////////////////////////////////////////////////////////////////////
/// \name Up one folder
///
///   A stem with an arrowhead on top of it and a foot turning right at the
/// bottom, which is what the issue drew as
///
///     ^
///     |
///     +--
///
////////////////////////////////////////////////////////////////////////////////
///@{
float constexpr upWidth{15.0f};
float constexpr upHeight{16.05f};
/// The stem and the foot, which are one pen.
float constexpr upStroke{3.6f};
float constexpr upHeadWidth{8.1f};
float constexpr upHeadHeight{7.05f};
///@}

////////////////////////////////////////////////////////////////////////////////
/// \name Save
///
///   An arrow coming down onto a tray.
////////////////////////////////////////////////////////////////////////////////
///@{
float constexpr saveWidth{14.0f};
float constexpr saveHeight{16.0f};
// the stem takes what the other three leave, so the mark keeps its height
float constexpr saveStemWidth{2.0f};
float constexpr saveHeadWidth{10.0f};
float constexpr saveHeadHeight{5.0f};
/// The tray, and the air between it and the point coming down at it.
///@{
float constexpr saveTrayHeight{3.0f};
float constexpr saveTrayGap{2.0f};
///@}
///@}

////////////////////////////////////////////////////////////////////////////////
/// \name Delete
///
///   A trash can: a handle, a lid across the whole width, and a body under it.
///
/// \note Body and handle are stroked, not filled -- a solid can at fourteen
/// pixels is a blob with a line over it.
////////////////////////////////////////////////////////////////////////////////
///@{
float constexpr trashWidth{14.0f};
float constexpr trashHeight{14.0f};
float constexpr trashStroke{2.0f};
float constexpr trashHandleWidth{6.0f};
float constexpr trashHandleHeight{3.0f};
float constexpr trashLidHeight{2.0f};
float constexpr trashBodyWidth{10.0f};
///@}

////////////////////////////////////////////////////////////////////////////////
/// \name The user
///
///   A head over a pair of shoulders, and nothing else -- the issue asked for
/// the simplified mark rather than the stylised one.
////////////////////////////////////////////////////////////////////////////////
///@{
float constexpr userHeadDiameter{9.0f};
float constexpr userBodyWidth{14.1f};
float constexpr userBodyHeight{5.1f};
/// \note Not half the height: at half, the shoulders come out a capsule. The
/// mock-up rounds the top two corners hard and leaves the foot nearly square,
/// which is the difference between shoulders and a pill.
float constexpr userBodyRadius{2.1f};
/// The neck, which is the whole of what makes the two shapes read as one.
float constexpr userGap{1.95f};
///@}

////////////////////////////////////////////////////////////////////////////////
/// \name The jog
///
///   Two triangles pointing away from each other, one per button. Each is
/// centred in its own widget and the widgets abut, so what separates them is
/// the pair of insets and nothing else -- which is what makes the two read as
/// one control rather than as two buttons that happen to be near each other.
////////////////////////////////////////////////////////////////////////////////
///@{
float constexpr jogTriangle{14.1f}; ///< as wide as it is tall
///@}

////////////////////////////////////////////////////////////////////////////////
/// \name The folder in front of a directory's name in the list
///
/// \note juce::LookAndFeel::getDefaultFolderImage() until now, which JUCE 8
/// answers with a Drawable, may answer with nothing at all, and draws in
/// whatever its own look and feel says rather than in this skin's accent.
////////////////////////////////////////////////////////////////////////////////
///@{
float constexpr folderWidth{16.5f};
float constexpr folderHeight{13.5f};
/// The tab along the back of it: how much of the width it takes and how far it
/// stands above the body.
///@{
float constexpr folderTabWidth{6.9f};
float constexpr folderTabRise{3.0f};
///@}
float constexpr folderCornerRadius{1.8f};
///@}

////////////////////////////////////////////////////////////////////////////////
/// \name The padlock that says a preset may not bring its own audio file
///
///   A body with a shackle standing on it. Shut and open are the same drawing
/// -- what says which is the colour, as everywhere else in this row.
////////////////////////////////////////////////////////////////////////////////
///@{
float constexpr lockWidth{14.1f};
float constexpr lockHeight{16.05f};
float constexpr lockBodyHeight{9.0f};
/// The shackle, measured down the middle of its wire rather than around the
/// outside of it, because that is what a stroked path is placed by.
///@{
float constexpr lockShackleWidth{7.95f};
float constexpr lockShackleStroke{2.1f};
///@}
float constexpr lockCornerRadius{1.65f};
///@}
} // namespace GlyphStyle

////////////////////////////////////////////////////////////////////////////////
///
/// \class GlyphPainter
///
////////////////////////////////////////////////////////////////////////////////

class GlyphPainter
{
  public:
    /// \brief Up one folder: draws GlyphStyle's arrow centred in \p bounds.
    static void paintFolderUp(juce::Graphics &, juce::Rectangle<float> bounds, juce::Colour);

    /// \brief An arrow onto a tray: keep this. Centred in \p bounds.
    static void paintSave(juce::Graphics &, juce::Rectangle<float> bounds, juce::Colour);

    /// \brief A trash can, centred in \p bounds.
    static void paintTrash(juce::Graphics &, juce::Rectangle<float> bounds, juce::Colour);

    /// \brief A head and shoulders, centred in \p bounds.
    static void paintUser(juce::Graphics &, juce::Rectangle<float> bounds, juce::Colour);

    /// \brief One half of the jog, centred in \p bounds.
    static void paintJog(juce::Graphics &, juce::Rectangle<float> bounds, bool pointsRight,
                         juce::Colour);

    /// \brief The mark in front of a directory in the preset list.
    static void paintFolder(juce::Graphics &, juce::Rectangle<float> bounds, juce::Colour);

    /// \brief A padlock, centred in \p bounds.
    static void paintLock(juce::Graphics &, juce::Rectangle<float> bounds, juce::Colour);

    /// \brief The undo mark: an arc over the top with an arrowhead on the end it
    /// came back to, pointing left for undo and right for redo.
    static void paintUndoArrow(juce::Graphics &, juce::Rectangle<float> bounds, bool pointsLeft,
                               juce::Colour);

  public:
    GlyphPainter() = delete; // a drawing, not an object
}; // class GlyphPainter

} // namespace LE::SW::GUI

#endif // glyphPainter_hpp
