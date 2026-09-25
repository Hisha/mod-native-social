#include "SocialProfileStore.h"

#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"

#include <string>

namespace nativesocial
{
SocialProfileStore& SocialProfileStore::Instance()
{
    static SocialProfileStore instance;
    return instance;
}

bool SocialProfileStore::LoadAll()
{
    Clear();

    // Verify the table exists and is readable by using COUNT(*) which always
    // returns a row (even when table is empty), distinguishing a missing table
    // from a successfully queried empty table.
    QueryResult schemaCheck = LoginDatabase.Query(
        "SELECT COUNT(*) FROM native_social_account ns INNER JOIN account a ON a.id = ns.account_id");
    if (!schemaCheck)
    {
        // Table does not exist or is not readable
        return false;
    }

    // Get the count from the result
    Field const* countRow = schemaCheck->Fetch();
    std::uint32_t count = countRow[0].Get<std::uint32_t>();
    
    // If count is zero, the table is verified readable and we can return success
    // immediately with zero loaded profiles
    if (count == 0)
    {
        return true;
    }

    QueryResult result = LoginDatabase.Query(
        "SELECT ns.account_id, ns.display_name, ns.appear_offline "
        "FROM native_social_account ns INNER JOIN account a ON a.id = ns.account_id");
    if (!result)
        return false;

    do
    {
        Field const* row = result->Fetch();
        SocialProfile profile;
        profile.accountId = row[0].Get<std::uint32_t>();
        profile.displayName = row[1].Get<std::string>();
        profile.appearOffline = row[2].Get<std::uint32_t>() != 0;
        Apply(profile.accountId, profile);
    }
    while (result->NextRow());

    return true;
}

void SocialProfileStore::Clear()
{
    _profiles.clear();
    _nameKeys.clear();
}

std::vector<SocialProfile> SocialProfileStore::AllProfiles() const
{
    std::vector<SocialProfile> out;
    out.reserve(_profiles.size());
    for (auto const& entry : _profiles)
        out.push_back(entry.second);
    return out;
}

bool SocialProfileStore::FindAccount(std::uint32_t accountId, SocialProfile& out) const
{
    auto const it = _profiles.find(accountId);
    if (it == _profiles.end())
        return false;
    out = it->second;
    return true;
}

bool SocialProfileStore::FindByNameKey(std::string const& nameKey, SocialProfile& out) const
{
    auto const key = _nameKeys.find(nameKey);
    if (key == _nameKeys.end())
        return false;
    return FindAccount(key->second, out);
}

NameResult SocialProfileStore::SetDisplayName(std::uint32_t accountId, std::string const& displayName)
{
    std::string const key = DisplayNameKey(displayName);
    if (key.empty())
        return NameResult::InvalidCharacter;

    NameResult const available = CheckDisplayNameAvailable(accountId, displayName);
    if (available != NameResult::Ok)
        return available;

    std::string const selfStr = std::to_string(accountId);
    std::string escName = displayName;
    std::string escKey = key;
    LoginDatabase.EscapeString(escName);
    LoginDatabase.EscapeString(escKey);

    if (_profiles.count(accountId) > 0)
    {
        // DirectExecute (not Execute) is used on purpose: Execute() is
        // asynchronous, so a subsequent sync Query() would not be ordered
        // after the write. DirectExecute() commits before returning, which
        // makes the post-write verification below meaningful.
        LoginDatabase.DirectExecute(
            "UPDATE native_social_account SET display_name = '" + escName + "', "
            "display_name_key = '" + escKey + "' WHERE account_id = " + selfStr);
    }
    else
    {
        LoginDatabase.DirectExecute(
            "INSERT INTO native_social_account (account_id, display_name, display_name_key, appear_offline) "
            "VALUES (" + selfStr + ", '" + escName + "', '" + escKey + "', 0)");
    }

    // Post-write verification. We never trust Execute() in isolation: the
    // SELECT below reflects what the database actually committed. Races
    // between realms that share an auth DB resolve deterministically here.
    QueryResult check = LoginDatabase.Query(
        "SELECT account_id, display_name_key FROM native_social_account "
        "WHERE account_id = " + selfStr + " OR display_name_key = '" + escKey + "'");
    if (!check)
        return NameResult::StorageFailure;

    bool ownRow = false;
    bool ownKey = false;
    bool conflictKey = false;
    do
    {
        Field const* row = check->Fetch();
        std::uint32_t const candidate = row[0].Get<std::uint32_t>();
        std::string const candidateKey = row[1].Get<std::string>();
        if (candidate == accountId)
        {
            ownRow = true;
            if (candidateKey == key)
                ownKey = true;
        }
        else if (candidateKey == key)
            conflictKey = true;
    }
    while (check->NextRow());

    if (conflictKey || (ownRow && !ownKey))
        return NameResult::AlreadyTaken;
    if (!ownRow || !ownKey)
        return NameResult::StorageFailure;

    SocialProfile updated;
    if (FindAccount(accountId, updated))
        updated.displayName = displayName;
    else
        updated = { accountId, displayName, false };
    Apply(accountId, updated);
    return NameResult::Ok;
}

NameResult SocialProfileStore::CheckDisplayNameAvailable(
    std::uint32_t accountId, std::string const& displayName) const
{
    std::string const key = DisplayNameKey(displayName);
    if (key.empty())
        return NameResult::InvalidCharacter;

    // Fast path against the local index. The database is the authority for
    // uniqueness (see below); this only avoids a statement when the answer is
    // already known locally.
    auto const existing = _nameKeys.find(key);
    if (existing != _nameKeys.end() && existing->second != accountId)
        return NameResult::AlreadyTaken;

    std::string const selfStr = std::to_string(accountId);
    std::string escKey = key;
    LoginDatabase.EscapeString(escKey);

    // Authoritative pre-check against the shared auth database. A display-name
    // key can be held by an account that only exists in another realm sharing
    // this auth DB, so it is invisible to the local index; the unique column
    // is what actually enforces the name. This catches that case before we
    // attempt the write.
    if (LoginDatabase.Query(
            "SELECT account_id FROM native_social_account "
            "WHERE display_name_key = '" + escKey + "' AND account_id <> " + selfStr))
    {
        return NameResult::AlreadyTaken;
    }
    return NameResult::Ok;
}

bool SocialProfileStore::SetAppearOffline(std::uint32_t accountId, bool value)
{
    auto it = _profiles.find(accountId);
    if (it == _profiles.end() || it->second.displayName.empty())
        return false;

    // DirectExecute keeps this synchronous so a failure cannot silently diverge
    // the in-memory state from the database (the value is only applied on an
    // existing row; a successful call is recorded next).
    LoginDatabase.DirectExecute(
        "UPDATE native_social_account SET appear_offline = " +
        std::string(value ? "1" : "0") + " WHERE account_id = " + std::to_string(accountId));

    QueryResult check = LoginDatabase.Query(
        "SELECT appear_offline FROM native_social_account WHERE account_id = " +
        std::to_string(accountId));
    if (!check || (check->Fetch()[0].Get<std::uint32_t>() != 0) != value)
        return false;

    it->second.appearOffline = value;
    return true;
}

void SocialProfileStore::Apply(std::uint32_t accountId, SocialProfile const& profile)
{
    auto const it = _profiles.find(accountId);
    if (it != _profiles.end() && it->second.displayName != profile.displayName)
        _nameKeys.erase(DisplayNameKey(it->second.displayName));
    _profiles[accountId] = profile;
    if (!profile.displayName.empty())
        _nameKeys[DisplayNameKey(profile.displayName)] = accountId;
}
}
