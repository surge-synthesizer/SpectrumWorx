////////////////////////////////////////////////////////////////////////////////
///
/// \file authorName.hpp
/// --------------------
///
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef authorName_hpp__3B7E1D42_08C6_4F5A_9D31_A6E0B4C29F78
#define authorName_hpp__3B7E1D42_08C6_4F5A_9D31_A6E0B4C29F78
//------------------------------------------------------------------------------
#include <cstddef>
#include <string>
#include <string_view>

namespace LE::SW
{

////////////////////////////////////////////////////////////////////////////////
///
/// \name The author's name
///
/// \brief What a byline has to be before it may reach a file. The rule is the
/// format's rather than the widget's, and every route in applies it -- the
/// settings panel, the preferences file, and the writer. \see issue #56 and
/// doc/tech/streaming_format.md §4.2.
///
////////////////////////////////////////////////////////////////////////////////
///@{

/// bytes, not characters
inline constexpr std::size_t maxAuthorLength{64};

/// \brief Everything except the two quotes, the two angle brackets and the
/// control codes.
inline constexpr bool isAllowedInAuthorName(char const character)
{
    auto const byte(static_cast<unsigned char>(character));

    // a name in somebody's own script goes through: the file is UTF-8
    if (byte >= 0x80)
        return true;

    if ((byte < 0x20) || (byte == 0x7F))
        return false;

    return (byte != '"') && (byte != '\'') && (byte != '<') && (byte != '>');
}

/// \brief \p author with the above removed, cut to maxAuthorLength bytes and
/// trimmed of surrounding blanks.
inline std::string sanitisedAuthorName(std::string_view const author)
{
    std::string kept;

    for (auto const character : author)
    {
        if (!isAllowedInAuthorName(character))
            continue;
        if (kept.size() == maxAuthorLength)
            break;
        kept += character;
    }

    // drop only a UTF-8 sequence the cut actually broke
    auto const isContinuationByte([](char const character) {
        return (static_cast<unsigned char>(character) & 0xC0) == 0x80;
    });
    std::size_t lead(kept.size());
    while ((lead > 0) && isContinuationByte(kept[lead - 1]))
        --lead;
    if (lead > 0)
    {
        auto const leadByte(static_cast<unsigned char>(kept[lead - 1]));
        std::size_t const expected((leadByte >= 0xF0)   ? 4
                                   : (leadByte >= 0xE0) ? 3
                                   : (leadByte >= 0xC0) ? 2
                                                        : 1);
        if ((lead - 1 + expected) > kept.size())
            kept.resize(lead - 1);
    }

    // by hand: isspace() answers by the process locale, and this may not
    auto const isBlank([](char const character) {
        return (character == ' ') || ((character >= '\t') && (character <= '\r'));
    });

    std::size_t first(0);
    while ((first < kept.size()) && isBlank(kept[first]))
        ++first;
    std::size_t last(kept.size());
    while ((last > first) && isBlank(kept[last - 1]))
        --last;

    return kept.substr(first, last - first);
}
///@}

} // namespace LE::SW

//------------------------------------------------------------------------------
#endif // authorName_hpp
