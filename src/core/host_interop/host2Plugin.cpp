////////////////////////////////////////////////////////////////////////////////
///
/// host2Plugin.cpp
/// ---------------
///
/// Copyright (c) 2009 - 2016. Little Endian Ltd.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "host2Plugin.hpp"

#include "le/utility/span.hpp"

namespace LE
{

#pragma warning(push)
#pragma warning(disable : 4127) // Conditional expression is constant.

//...mrmlj...orphan...
/// \note `Char const *string`, not `Char const *const string`, and the const is
/// not merely redundant here -- it made this function unlinkable under clang-cl.
/// A top-level const on a parameter is not part of the function type
/// ([dcl.fct]/5), so the two spellings declare the same function and the two
/// declarations of it -- host2Plugin.hpp and the one plugin2Host.cpp keeps for
/// itself -- write it without. MSVC agrees and mangles both as `PEBD`; clang-cl
/// mangles the definition as `QEBD` and leaves every caller referring to a
/// symbol nothing defines. Measured, on the two manglings, not deduced.
template <typename Char>
char *copyToBuffer(Char const *string, LE::Utility::Span<char> const &buffer)
{
    //std::strncpy( buffer.begin(), string, buffer.size() - 1 );
    Char const *LE_RESTRICT pSourceCharacter(string);
    char *LE_RESTRICT pDestinationCharacter(buffer.begin());
    char const *LE_RESTRICT const pDestinationEnd(buffer.end() - 1);

    bool const same(
        !std::is_same<Char, char>::value &&
        (static_cast<void const *>(string) == static_cast<void const *>(pDestinationCharacter)));
    if (same)
    {
        LE_ASSERT(*pDestinationCharacter == '\0');
        return pDestinationCharacter;
    }
    if (string == nullptr)
    {
        *pDestinationCharacter = '\0';
        return pDestinationCharacter;
    }

    while (pDestinationCharacter != pDestinationEnd)
    {
        Char const sourceCharacter(*pSourceCharacter);
        if (sourceCharacter == '\0')
            break;
        *pDestinationCharacter = static_cast<char>(sourceCharacter);
        LE_ASSERT(*pDestinationCharacter == sourceCharacter);
        ++pSourceCharacter;
        ++pDestinationCharacter;
    }
    *pDestinationCharacter = '\0';

    return pDestinationCharacter;
}

#pragma warning(pop)

template char *copyToBuffer<char>(char const *, LE::Utility::Span<char> const &);
#ifdef _WIN32
template char *copyToBuffer<wchar_t>(wchar_t const *, LE::Utility::Span<char> const &);
#endif // _WIN32

namespace SW
{

} // namespace SW

} // namespace LE
