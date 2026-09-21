#ifndef NATIVESOCIAL_SOCIALPROFILE_H
#define NATIVESOCIAL_SOCIALPROFILE_H

#include <cstdint>
#include <string>

namespace nativesocial
{
// Canonical row of native_social_account (LoginDatabase / auth).
struct SocialProfile
{
    std::uint32_t accountId = 0;
    std::string displayName;   // pristine/canonical form as set by the account
    bool appearOffline = false;
};

enum class NameResult
{
    Ok,
    TooShort,
    TooLong,
    InvalidCharacter,
    AlreadyTaken,
    StorageFailure
};

std::string NameResultToString(NameResult result);

// Validates and canonicalizes candidateName in place (trims surrounding
// whitespace). Returns Ok when the name may be persisted.
NameResult ValidateDisplayName(std::string& candidateName, std::uint32_t minLength, std::uint32_t maxLength);

// ASCII-aware lowercase key used for case-insensitive display-name lookup.
// ASCII letters are folded to lowercase; all other bytes are preserved, so
// non-ASCII names compare byte-for-byte. Never empty when displayName is set.
std::string DisplayNameKey(std::string const& name);
}

#endif