#include "SocialDirectory.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nativesocial
{
namespace
{

// Deterministic order key: online flag first, then ASCII-folded display-name
// key, then account id as a tiebreaker. Produces a stable total order for any
// input ordering of profiles.
struct SortKey
{
    std::uint32_t online = 0;
    std::string nameKey;
    std::uint32_t accountId = 0;

    bool operator<(SortKey const& other) const
    {
        if (online != other.online)
            return online > other.online; // online first
        int const cmp = nameKey.compare(other.nameKey);
        if (cmp != 0)
            return cmp < 0;
        return accountId < other.accountId;
    }
};

} // namespace

std::vector<DirectoryEntry> BuildDirectory(std::vector<SocialProfile> const& profiles,
    std::unordered_set<std::uint32_t> const& excludedAccounts,
    DirectoryPresenceMap const& presence)
{
    std::vector<std::pair<SortKey, DirectoryEntry>> indexed;
    indexed.reserve(profiles.size());

    for (SocialProfile const& profile : profiles)
    {
        // Accounts are keyed only by the stable auth id; an empty display
        // name is an authoring bug (the store only persists validated names).
        if (profile.displayName.empty())
            continue;
        // Configured/runtime bots and the authenticated viewer are never
        // participants in this public directory response.
        if (excludedAccounts.count(profile.accountId) != 0)
            continue;

        DirectoryEntry entry;
        entry.accountId = profile.accountId;
        entry.displayName = profile.displayName;

        auto const found = presence.find(profile.accountId);
        bool const online = found != presence.end() && found->second.online;
        if (online && profile.appearOffline)
        {
            // Appear Offline: the account advertised presence but suppresses
            // it. The entry is indistinguishable from a genuinely offline
            // account on the wire - nothing leaks the choice.
            entry.presence.online = false;
        }
        else if (online)
        {
            entry.presence = found->second;
            entry.presence.online = true;
        }

        SortKey key;
        key.online = entry.presence.online ? 1u : 0u;
        key.nameKey = DisplayNameKey(entry.displayName);
        key.accountId = entry.accountId;
        indexed.emplace_back(std::move(key), std::move(entry));
    }

    std::sort(indexed.begin(), indexed.end(),
        [](std::pair<SortKey, DirectoryEntry> const& a, std::pair<SortKey, DirectoryEntry> const& b)
        {
            return a.first < b.first;
        });

    std::vector<DirectoryEntry> result;
    result.reserve(indexed.size());
    for (auto& pair : indexed)
        result.push_back(std::move(pair.second));
    return result;
}

std::vector<std::string> DirectoryEntryIdentityFields(DirectoryEntry const& entry)
{
    std::vector<std::string> fields;
    fields.reserve(3);
    fields.push_back(entry.presence.online ? "1" : "0");
    fields.push_back(std::to_string(entry.accountId));
    fields.push_back(entry.displayName);
    return fields;
}

std::vector<std::string> DirectoryEntryPresenceFields(DirectoryEntry const& entry)
{
    std::vector<std::string> fields;
    if (!entry.presence.online)
        return fields;
    fields.reserve(6);
    fields.push_back(entry.presence.characterName);
    fields.push_back(std::to_string(entry.presence.level));
    fields.push_back(std::to_string(entry.presence.race));
    fields.push_back(std::to_string(entry.presence.charClass));
    fields.push_back(entry.presence.faction == 'H' ? "H" : "A");
    fields.push_back(entry.presence.zone);
    return fields;
}

} // namespace nativesocial
