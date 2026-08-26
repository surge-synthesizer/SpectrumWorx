////////////////////////////////////////////////////////////////////////////////
///
/// \file animation.hpp
/// -------------------
///
///   How a module strip moves when the rack changes under it, and the one place
/// the timings are written down.
///
///   The rack is recomputed rather than updated -- resyncModuleRack() makes it a
/// function of the chain -- so every strip that has to move is told a new slot
/// and every strip that has to go is destroyed. Both are instantaneous, and this
/// is what puts a few frames between the two states so a user can see which
/// strip went where.
///
/// \note Three motions, and they compose: a strip *slides* to a slot it did not
/// use to be in, *grows* when it is new, and *shrinks* when it is destroyed. An
/// add is a grow with the strips after it sliding right, a delete is a shrink
/// with the strips after it sliding left, and a drag is sliding on its own.
///
/// \note Two mechanisms behind them, and not by preference. The slide and the
/// shrink are juce::ComponentAnimator's, which is where the shrink already was.
/// The grow cannot be: the animator's proxy takes its snapshot at the
/// component's *current* size, so it can animate away from a full-size strip and
/// never towards one, and animating the real bounds is no good either because a
/// strip's children sit at fixed offsets and a half-height one would show its
/// top half rather than a small copy. So the grow is a scale transform, which is
/// how ZoomedEditor already draws the whole editor at 150%.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef animation_hpp__A3F1C0D5_7B24_4E68_9C1A_5D0E82B6F413
#define animation_hpp__A3F1C0D5_7B24_4E68_9C1A_5D0E82B6F413
//------------------------------------------------------------------------------
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>

namespace LE::SW::GUI
{

////////////////////////////////////////////////////////////////////////////////
///
/// \brief What the user picked on the Interface page.
///
/// \note Streamed by name, like the palette, so a value added here cannot change
/// what an existing preferences file means. \see Preferences::animationStyle().
///
////////////////////////////////////////////////////////////////////////////////

enum AnimationStyle : std::uint8_t
{
    NoAnimation,
    SlowAnimation,
    MediumAnimation,
    FastAnimation,

    numberOfAnimationStyles
};

////////////////////////////////////////////////////////////////////////////////
// Calibration
//
//   Everything about how the rack feels is these numbers. A style is a duration
// and nothing else, so a fourth one is a name and a number.
////////////////////////////////////////////////////////////////////////////////

/// \brief What a style means, and what the other timings here are relative to:
/// how long a strip takes to slide one slot.
inline constexpr unsigned int slowAnimationMilliseconds{250};
inline constexpr unsigned int mediumAnimationMilliseconds{175};
inline constexpr unsigned int fastAnimationMilliseconds{100};

/// \brief How much longer a strip takes to grow or shrink than to slide.
/// \note Equal durations are not equal speeds: a slide crosses a whole slot and
/// a grow crosses half a strip, so matching the two makes an add read as a
/// flicker next to a drag that reads as a move.
inline constexpr float growAndShrinkFactor{1.25f};

/// \note juce::ComponentAnimator's easing, for the slide and the shrink: zero at
/// both ends accelerates from rest and settles rather than stopping dead.
inline constexpr double slideStartSpeed{0.0};
inline constexpr double slideEndSpeed{0.0};

/// \brief How small a strip starts before it grows, as a fraction of itself.
/// \note Not zero: a zero scale is a singular transform, and JUCE maps points
/// through this one to decide what the mouse is over.
inline constexpr float initialGrowScale{0.075f};

/// \note 60 Hz, which is a frame on the displays this runs on and is the rate
/// juce::ComponentAnimator picks for itself.
inline constexpr int animationFrameMilliseconds{1000 / 60};

////////////////////////////////////////////////////////////////////////////////

/// \brief The enumerator's own spelling, which is what goes in the file.
char const *nameOf(AnimationStyle);

/// \brief How long \p style gives a slide, and zero for NoAnimation -- which is
/// how everything here asks "should I". A grow and a shrink take
/// growAndShrinkFactor of it.
unsigned int millisecondsFor(AnimationStyle);

////////////////////////////////////////////////////////////////////////////////
///
/// \brief The style \p component would move with, which is NoAnimation whenever
/// moving it could not be seen.
///
/// \note The screen test rather than the preference alone, and it is doing three
/// jobs: it keeps the rack the editor builds in its own constructor from growing
/// in one strip at a time, it keeps an offscreen render from catching a strip
/// mid-scale, and it keeps the headless suites -- which never put the editor on
/// a desktop -- reading final positions instead of interpolated ones.
///
////////////////////////////////////////////////////////////////////////////////

AnimationStyle styleFor(juce::Component const &);

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Slides \p component to (\p x, \p y), or puts it there.
///
/// \note Idempotent, which matters because the rack is recomputed: a resync
/// tells every surviving strip its slot whether or not it changed, and several
/// can be queued. A component already there, or already on its way there, is
/// left alone rather than restarted from wherever the last one had got to.
///
////////////////////////////////////////////////////////////////////////////////

void moveTo(juce::Component &, int x, int y);

////////////////////////////////////////////////////////////////////////////////
///
/// \brief Shrinks \p component to nothing where it stands.
///
/// \note For a component about to be destroyed, and it must be called from the
/// destructor rather than before it: what animates is a snapshot the animator
/// takes here and owns, so the strip itself can go as soon as this returns.
///
////////////////////////////////////////////////////////////////////////////////

void shrinkAway(juce::Component &);

////////////////////////////////////////////////////////////////////////////////
///
/// \class GrowIn
///
/// \brief Scales a component up from nothing about its own centre.
///
/// \note Held by the component it grows, so that a strip destroyed mid-grow takes
/// its timer with it. The transform is cleared at the end and by the destructor,
/// because a component left transformed is one whose getBounds() no longer says
/// where it is on screen.
///
////////////////////////////////////////////////////////////////////////////////

class GrowIn final : private juce::Timer
{
  public:
    GrowIn() = default;
    ~GrowIn() override;

    GrowIn(GrowIn const &) = delete; // makes non-copyable
    GrowIn &operator=(GrowIn const &) = delete;

    /// \brief Starts \p component at initialGrowScale and runs it up to itself.
    /// \note Does nothing under NoAnimation, which includes a component that is
    /// not on screen -- so a strip built for the rack the editor opens with is
    /// simply there. \see styleFor().
    void start(juce::Component &);

  private:
    void timerCallback() override;

    /// \brief Puts the transform back and stops.
    void finish();

    juce::Component::SafePointer<juce::Component> pComponent_;
    int elapsed_{0};
    int duration_{0};
}; // class GrowIn

//------------------------------------------------------------------------------
} // namespace LE::SW::GUI
//------------------------------------------------------------------------------
#endif // animation_hpp
