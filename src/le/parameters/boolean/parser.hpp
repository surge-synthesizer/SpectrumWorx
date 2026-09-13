////////////////////////////////////////////////////////////////////////////////
///
/// \file boolean/parser.hpp
/// ------------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef parser_hpp__C05B7E19_2A64_4D83_86F1_9E4B2D07AC35
#define parser_hpp__C05B7E19_2A64_4D83_86F1_9E4B2D07AC35
//------------------------------------------------------------------------------
#include "tag.hpp"

#include "le/parameters/parser_fwd.hpp"
#include "le/utility/lexicalCast.hpp"

#include <cstring>

namespace LE::Parameters::Detail
{

////////////////////////////////////////////////////////////////////////////////
///
/// \brief "yes" and "no", which is what the boolean printer writes -- and a
/// number, which is what a host's own editing field is more likely to hand back.
///
/// \note Trigger parameters arrive here too, through the tag hierarchy, exactly
/// as they are printed.
///
////////////////////////////////////////////////////////////////////////////////

template <class Parameter>
ParsedValue parse(char const *const text, SW::Engine::Setup const &, BooleanParameterTag)
{
    if (!text)
        return {};

    if (std::strcmp(text, "yes") == 0)
        return 1.0f;
    if (std::strcmp(text, "no") == 0)
        return 0.0f;

    auto const number(Utility::parseNumber(text));
    if (!number)
        return {};
    return (*number != 0) ? 1.0f : 0.0f;
}

} // namespace LE::Parameters::Detail

#endif // parser_hpp
