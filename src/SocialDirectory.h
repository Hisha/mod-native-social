#ifndef NATIVESOCIAL_SOCIALDIRECTORY_H
#define NATIVESOCIAL_SOCIALDIRECTORY_H

#include "SocialProfile.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Pure directory compiler, independent of AzerothCore so the sorting, privacy
// and Playerbot-exclusion rules are unit-tested without a running core. The
// game-facing SocialService supplies the inputs (profiles, bot accounts, live
// presence) and maps entries onto wire fields per THIS module.

namespace nativesocial
{
// Live presence for one account as resolved by the auth/preloaded service.
// All fields except online/level/characterName are already localized for the
// requesting session's DBC locale.
struct PresenceInfo
{
    bool online = false;
    std::string characterName;
    std::uint32_t level = 0;
    std::uint32_t race = 0;       // client localizes the DBC race id
    std::uint32_t charClass = 0;  // client localizes the DBC class id
    char faction = '\0';   // 'A' (Alliance) or 'H' (Horde)
    std::string zone;
};

// One advertised directory row.
struct DirectoryEntry
{
    std::uint32_t accountId = 0;
    std::string displayName;
    PresenceInfo presence;

    bool Online() const { return presence.online; }
};

using DirectoryPresenceMap = std::unordered_map<std::uint32_t, PresenceInfo>;

// Compiles the player directory for one request.
//   profiles    - every native_social_account row (accounts that adopted a
//                 display name); entries appear exactly once in this order
//   botAccounts - account ids that currently hold Playerbot sessions; these
//                 are ALWAYS excluded from the directory
//   presence    - live presence keyed by account id
// Privacy is enforced here so it is testable: an appear-offline account is
// always represented as an offline entry even when presence exists for it
// (the wire-level presence is suppressed entirely; no hidden flags leak).
// Ordering is deterministic: online entries first, then alphabetical by
// display-name key within each group; ties resolve by account id.
std::vector<DirectoryEntry> BuildDirectory(std::vector<SocialProfile> const& profiles,
    std::unordered_set<std::uint32_t> const& botAccounts,
    DirectoryPresenceMap const& presence);

// Wire identity/presence fields for a single entry (docs/NSOC_PROTOCOL.md):
// identity := "0"|"1", accountId, displayName
// presence (online only) := characterName, level, raceId, classId, "A"|"H", location
// The presence vector is empty for offline/appear-offline entries, which lets
// the chunked codec emit exactly one DIR_ENTRY part for them.
std::vector<std::string> DirectoryEntryIdentityFields(DirectoryEntry const& entry);
std::vector<std::string> DirectoryEntryPresenceFields(DirectoryEntry const& entry);
}

#endif
