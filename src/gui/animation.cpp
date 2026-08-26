////////////////////////////////////////////////////////////////////////////////
///
/// \file animation.cpp
/// -------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "animation.hpp"

/// `fadeOutComponent()`, which is the shrink and already guards the display list.
#include "gui.hpp"
#include "preferences.hpp"

#include "le/utility/assert.hpp"

namespace LE::SW::GUI
{

namespace
{
/// \note getBounds() is the untransformed rectangle -- JUCE keeps the two apart --
/// so the pivot does not drift as the scale is reapplied frame by frame.
void scaleAboutCentre(juce::Component &component, float const scale)
{
    auto const centre(component.getBounds().getCentre().toFloat());
    component.setTransform(juce::AffineTransform::scale(scale, scale, centre.x, centre.y));
}
/// \note Zero stays zero, so the "should I" test survives the scaling.
unsigned int growAndShrinkMilliseconds(AnimationStyle const style)
{
    return static_cast<unsigned int>(millisecondsFor(style) * growAndShrinkFactor);
}
} // namespace

char const *nameOf(AnimationStyle const style)
{
    switch (style)
    {
    case NoAnimation:
        return "NoAnimation";
    case SlowAnimation:
        return "SlowAnimation";
    case MediumAnimation:
        return "MediumAnimation";
    case FastAnimation:
        return "FastAnimation";
    case numberOfAnimationStyles:
        break;
    }
    LE_UNREACHABLE_CODE();
}

unsigned int millisecondsFor(AnimationStyle const style)
{
    switch (style)
    {
    case NoAnimation:
        return 0;
    case SlowAnimation:
        return slowAnimationMilliseconds;
    case MediumAnimation:
        return mediumAnimationMilliseconds;
    case FastAnimation:
        return fastAnimationMilliseconds;
    case numberOfAnimationStyles:
        break;
    }
    LE_UNREACHABLE_CODE();
}

AnimationStyle styleFor(juce::Component const &component)
{
    // nothing to see, so nothing to spend frames on
    if (!component.isShowing())
        return NoAnimation;

    // asking is fatal rather than merely pointless with no displays: the
    // animator's proxy dereferences getDisplayForRect(), which is null then
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        return NoAnimation;

    return preferences().animationStyle();
}

void moveTo(juce::Component &component, int const x, int const y)
{
    auto &animator(juce::Desktop::getInstance().getAnimator());
    auto const target(component.getBounds().withPosition(x, y));

    // where it is when nothing is running, so this is both "already there" and
    // "already going there"
    if (animator.getComponentDestination(&component) == target)
        return;

    auto const milliseconds(millisecondsFor(styleFor(component)));
    if (milliseconds == 0)
    {
        // the style can be turned off with one in flight
        animator.cancelAnimation(&component, false);
        component.setTopLeftPosition(x, y);
        return;
    }

    animator.animateComponent(&component, target, 1.0f, static_cast<int>(milliseconds), false,
                              slideStartSpeed, slideEndSpeed);
}

void shrinkAway(juce::Component &component)
{
    if (auto const milliseconds = growAndShrinkMilliseconds(styleFor(component)); milliseconds != 0)
        fadeOutComponent(component, 0, milliseconds, true);
}

////////////////////////////////////////////////////////////////////////////////
//
// GrowIn
//
////////////////////////////////////////////////////////////////////////////////

/// \note Stops, and deliberately does not put the transform back: this is held by
/// the component it grows, so by the time it runs that component is already
/// inside its own destructor and is in no state to be told it moved.
GrowIn::~GrowIn() { stopTimer(); }

void GrowIn::start(juce::Component &component)
{
    auto const milliseconds(growAndShrinkMilliseconds(styleFor(component)));
    if (milliseconds == 0)
        return;

    finish(); // a strip told to grow twice starts again rather than jumping

    pComponent_ = &component;
    duration_ = static_cast<int>(milliseconds);
    elapsed_ = 0;
    scaleAboutCentre(component, initialGrowScale);
    startTimer(animationFrameMilliseconds);
}

void GrowIn::timerCallback()
{
    elapsed_ += animationFrameMilliseconds;

    if (!pComponent_ || (elapsed_ >= duration_))
    {
        finish();
        return;
    }

    auto const progress(static_cast<float>(elapsed_) / static_cast<float>(duration_));
    // smoothstep, so it settles instead of stopping dead -- the ease
    // juce::ComponentAnimator gives the slide running beside it
    auto const eased(progress * progress * (3.0f - 2.0f * progress));
    scaleAboutCentre(*pComponent_, initialGrowScale + (1.0f - initialGrowScale) * eased);
}

void GrowIn::finish()
{
    stopTimer();
    if (pComponent_)
        pComponent_->setTransform(juce::AffineTransform());
    pComponent_ = nullptr;
    elapsed_ = duration_ = 0;
}

//------------------------------------------------------------------------------
} // namespace LE::SW::GUI
//------------------------------------------------------------------------------
