#include "SocialAccountEligibility.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace nativesocial
{
namespace
{
std::string Trim(std::string value)
{
    auto const whitespace = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), whitespace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(), value.end());
    return value;
}

std::string UsernameKey(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(character >= 'A' && character <= 'Z'
                ? character + ('a' - 'A') : character);
        });
    return value;
}
}

void SocialAccountEligibility::Configure(std::string playerbotAccountPrefix,
    std::string excludedAccounts)
{
    _playerbotAccountPrefix = UsernameKey(std::move(playerbotAccountPrefix));
    _excludedAccountNames.clear();

    std::size_t begin = 0;
    while (begin <= excludedAccounts.size())
    {
        std::size_t const comma = excludedAccounts.find(',', begin);
        std::string entry = Trim(excludedAccounts.substr(begin, comma - begin));
        if (!entry.empty())
            _excludedAccountNames.insert(UsernameKey(std::move(entry)));
        if (comma == std::string::npos)
            break;
        begin = comma + 1;
    }
}

bool SocialAccountEligibility::IsEligibleUsername(std::string const& username) const
{
    std::string const key = UsernameKey(username);
    if (!_playerbotAccountPrefix.empty() &&
        key.compare(0, _playerbotAccountPrefix.size(), _playerbotAccountPrefix) == 0)
        return false;
    return _excludedAccountNames.count(key) == 0;
}
}
