#ifndef NATIVESOCIAL_SOCIALACCOUNTELIGIBILITY_H
#define NATIVESOCIAL_SOCIALACCOUNTELIGIBILITY_H

#include <cstddef>
#include <string>
#include <unordered_set>

namespace nativesocial
{
// Server-side policy for deciding whether an authentication username belongs
// to a human account. Usernames are used only for this decision and never
// enter the public directory model.
class SocialAccountEligibility
{
public:
    void Configure(std::string playerbotAccountPrefix, std::string excludedAccounts);
    bool IsEligibleUsername(std::string const& username) const;

    std::size_t ExplicitExclusionCount() const { return _excludedAccountNames.size(); }

private:
    std::string _playerbotAccountPrefix;
    std::unordered_set<std::string> _excludedAccountNames;
};
}

#endif
