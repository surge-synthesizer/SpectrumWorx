////////////////////////////////////////////////////////////////////////////////
///
/// \file ruleStyle.hpp
/// -------------------
///
///   The one pen every hairline in the skin is drawn with.
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef ruleStyle_hpp__A3F5C81D_6B24_4E97_8D03_5C1F7A94E2B6
#define ruleStyle_hpp__A3F5C81D_6B24_4E97_8D03_5C1F7A94E2B6
//------------------------------------------------------------------------------

namespace LE::SW::GUI::RuleStyle
{

////////////////////////////////////////////////////////////////////////////////
///
/// \brief What a rule, a rim or a frame edge is drawn with, in pixels.
///
///   One number, named once, because a rule is one idea: the line that says
/// "this box ends here". It was a `1.0f` written out in four painters, each of
/// which then meant something different from the others the moment one of them
/// was touched.
///
/// \note **Two, not one and a half.** The skin was drawn at 1.5 until
/// 19.08.2026 and a one pixel rule through that transform covered a pixel and a
/// half -- so every rule was laid down as one solid column and one at half
/// strength, which is a soft grey seam rather than a line. That is what did not
/// survive being looked at on a 1x monitor. Now that the skin is its own
/// coordinate system a rule can be a whole number of pixels again, and two is
/// the one that keeps the weight the eye is used to; one is measurably sharper
/// and reads as a different, finer instrument.
///                                       (19.08.2026.)
///
/// \note Whole, and drawn just *inside* a whole-pixel edge -- which is why
/// every rectangle in backgroundPainter.hpp is on integers. \see
/// BackgroundPainter's paintRule(), which is where the insetting is done.
///
////////////////////////////////////////////////////////////////////////////////

float constexpr thickness{2.0f};

} // namespace LE::SW::GUI::RuleStyle

//------------------------------------------------------------------------------
#endif // ruleStyle_hpp
