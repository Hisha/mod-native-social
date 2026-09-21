#include "SocialProfileStore.h"

#include "DatabaseEnv.h"

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

    // A missing table surfaces as a failed query (nullptr), which we report
    // back to the caller so the operator is told to apply the auth SQL.
    QueryResult result = LoginDatabase.Query(
        "SELECT account_id, display_name, appear_offline "
        "FROM native_social_account");
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

    // Fast path against the local index. The database is the authority for
    // uniqueness (see below); this only avoids a statement when the answer is
    // already known locally.
    auto const existing = _nameKeys.find(key);
    if (existing != _nameKeys.end() && existing->second != accountId)
        return NameResult::AlreadyTaken;

    std::string const selfStr = std::to_string(accountId);
    std::string escName = displayName;
    std::string escKey = key;
    LoginDatabase.EscapeString(escName);
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
    // between realms that share an auth DB resolve deterministically here —
    // exactly one writer ends up owning the key, and everyone else reports
    // AlreadyTaken — because the unique constraint is enforced by the server.
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
        {
            conflictKey = true;
        }
    }
    while (check->NextRow());

    if (conflictKey || (ownRow && !ownKey))
        return NameResult::AlreadyTaken;
    if (!ownRow || !ownKey)
        return NameResult::StorageFailure;

    // The write is verified committed; only now reflect it in the in-memory
    // state. First registration has no loaded profile, so build the profile
    // from scratch (appear-offline defaults to off). An existing profile keeps
    // its privacy setting through a rename.
    SocialProfile updated;
    if (FindAccount(accountId, updated))
    {
        updated.displayName = displayName;
    }
    else
    {
        updated.accountId = accountId;
        updated.displayName = displayName;
        updated.appearOffline = false;
    }
    Apply(accountId, updated);
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