#include "SocialProfile.h"

#include <cctype>
#include <string>

namespace nativesocial
{
namespace
{
// Returns the number of Unicode code points in name; sets valid=false on the
// first malformed UTF-8 sequence or on any control code point. ASCII '|' and
// '%' are rejected because the 3.3.5a client interprets them as chat/format
// escapes and they could otherwise be abused in native UI rendering.
std::size_t Utf8Count(std::string const& name, bool& valid)
{
    valid = true;
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < name.size())
    {
        unsigned char const c = static_cast<unsigned char>(name[i]);
        if (c < 0x80)
        {
            if (c < 0x20 || c == 0x7F || c == 0x7C || c == 0x25)
            {
                valid = false;
                return 0;
            }
            ++i;
            ++count;
            continue;
        }

        std::size_t extra = 0;
        if ((c & 0xE0) == 0xC0)        // 2-byte lead (0xC2-0xDF)
            extra = 1;
        else if ((c & 0xF0) == 0xE0)   // 3-byte lead
            extra = 2;
        else if ((c & 0xF8) == 0xF0)   // 4-byte lead
            extra = 3;
        else
        {
            // 0x80-0xC1 (standalone continuation / overlong lead) and 0xF5-0xFF.
            valid = false;
            return 0;
        }

        if (i + extra >= name.size())
        {
            valid = false;
            return 0;
        }
        for (std::size_t k = 1; k <= extra; ++k)
            if ((static_cast<unsigned char>(name[i + k]) & 0xC0) != 0x80)
            {
                valid = false;
                return 0;
            }

        unsigned int cp = 0;
        switch (extra)
        {
            case 1: cp = c & 0x1Fu; break;
            case 2: cp = c & 0x0Fu; break;
            default: cp = c & 0x07u; break;
        }
        for (std::size_t k = 1; k <= extra; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(name[i + k]) & 0x3Fu);

        if (cp < 0x80 ||                              // overlong encoding
            (cp >= 0xD800 && cp <= 0xDFFF) ||         // surrogate halves
            (cp >= 0x80 && cp <= 0x9F) ||             // C1 controls
            cp > 0x10FFFF)
        {
            valid = false;
            return 0;
        }
        ++count;
        i += extra + 1;
    }
    return count;
}

void TrimAsciiWhitespace(std::string& value)
{
    std::size_t const begin = [&]()
    {
        std::size_t p = 0;
        while (p < value.size() && std::isspace(static_cast<unsigned char>(value[p])))
            ++p;
        return p;
    }();
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    if (begin == 0 && end == value.size())
        return;
    value.erase(end);
    value.erase(0, begin);
}
}

std::string NameResultToString(NameResult result)
{
    switch (result)
    {
        case NameResult::Ok: return "accepted";
        case NameResult::TooShort: return "is too short";
        case NameResult::TooLong: return "is too long";
        case NameResult::InvalidCharacter: return "contains characters that are not allowed";
        case NameResult::AlreadyTaken: return "is already taken";
        case NameResult::StorageFailure: return "could not be saved to the database";
    }
    return "is unavailable";
}

NameResult ValidateDisplayName(std::string& candidateName, std::uint32_t minLength, std::uint32_t maxLength)
{
    TrimAsciiWhitespace(candidateName);
    bool valid = false;
    std::size_t const count = Utf8Count(candidateName, valid);
    if (!valid)
        return NameResult::InvalidCharacter;
    if (count == 0)
        return NameResult::TooShort;
    if (count < minLength)
        return NameResult::TooShort;
    if (count > maxLength)
        return NameResult::TooLong;
    return NameResult::Ok;
}

std::string DisplayNameKey(std::string const& name)
{
    std::string key;
    key.reserve(name.size());
    for (char const c : name)
    {
        unsigned char const u = static_cast<unsigned char>(c);
        if (u >= 'A' && u <= 'Z')
            key.push_back(static_cast<char>(u - 'A' + 'a'));
        else
            key.push_back(c);
    }
    return key;
}
}