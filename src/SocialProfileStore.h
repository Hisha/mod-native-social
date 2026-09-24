#ifndef NATIVESOCIAL_SOCIALPROFILESTORE_H
#define NATIVESOCIAL_SOCIALPROFILESTORE_H

#include "SocialProfile.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace nativesocial
{
// Owns the authoritative in-memory view of native_social_account (auth).
// Every mutation is written to LoginDatabase and the local maps are updated
// only after the statement is accepted, so reads are consistent with what was
// actually persisted.
class SocialProfileStore
{
public:
    static SocialProfileStore& Instance();

    // Reads the whole table into memory. Returns false when the table cannot
    // be read (for example the module SQL has not been applied yet).
    bool LoadAll();
    void Clear();

    std::size_t LoadedCount() const { return _profiles.size(); }

    // Snapshots all profile rows (used to compile the player directory).
    std::vector<SocialProfile> AllProfiles() const;

    bool FindAccount(std::uint32_t accountId, SocialProfile& out) const;
    bool FindByNameKey(std::string const& nameKey, SocialProfile& out) const;

    // Authoritative local + auth-database uniqueness preflight. The unique
    // database key still resolves races at write time.
    NameResult CheckDisplayNameAvailable(std::uint32_t accountId, std::string const& displayName) const;

    // Persists an already-validated display name for the account. On success
    // the local state reflects the change.
    NameResult SetDisplayName(std::uint32_t accountId, std::string const& displayName);

    // Requires an existing profile row (the account already chose a display
    // name). Returns false when there is nothing to update.
    bool SetAppearOffline(std::uint32_t accountId, bool value);

private:
    void Apply(std::uint32_t accountId, SocialProfile const& profile);

    std::unordered_map<std::uint32_t, SocialProfile> _profiles;
    std::unordered_map<std::string, std::uint32_t> _nameKeys;
};
}

#endif
