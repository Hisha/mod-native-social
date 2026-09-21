#include "SocialPresence.h"

#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"

#if __has_include("Playerbots.h")
#include "Playerbots.h"
#endif

#include <vector>

namespace nativesocial
{
SocialPresence& SocialPresence::Instance()
{
    static SocialPresence instance;
    return instance;
}

void SocialPresence::Clear()
{
    _pending.clear();
    _confirmed.clear();
    _sessionsPerAccount.clear();
}

void SocialPresence::OnLogin(Player* player)
{
    if (!player || !player->GetSession())
        return;
    _pending[player->GetGUID()] = player->GetSession()->GetAccountId();
}

void SocialPresence::OnLogout(Player* player)
{
    if (!player)
        return;
    ObjectGuid const guid = player->GetGUID();
    _pending.erase(guid);
    auto const it = _confirmed.find(guid);
    if (it == _confirmed.end())
        return;
    auto const sessions = _sessionsPerAccount.find(it->second);
    if (sessions != _sessionsPerAccount.end())
    {
        if (sessions->second > 1)
            --sessions->second;
        else
            _sessionsPerAccount.erase(sessions);
    }
    _confirmed.erase(it);
}

void SocialPresence::Flush()
{
    // Purge confirmed sessions that no longer hold a live, human session
    // (abrupt disconnects can skip the logout hook).
    for (auto it = _confirmed.begin(); it != _confirmed.end();)
    {
        Player* player = ObjectAccessor::FindPlayer(it->first);
        if (!player || !player->GetSession() || !IsHumanSession(player) ||
            player->GetSession()->GetAccountId() != it->second)
        {
            auto const sessions = _sessionsPerAccount.find(it->second);
            if (sessions != _sessionsPerAccount.end())
            {
                if (sessions->second > 1)
                    --sessions->second;
                else
                    _sessionsPerAccount.erase(sessions);
            }
            it = _confirmed.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (_pending.empty())
        return;

    std::vector<ObjectGuid> candidates;
    candidates.reserve(_pending.size());
    for (auto const& entry : _pending)
        candidates.push_back(entry.first);

    for (ObjectGuid const& guid : candidates)
    {
        auto const it = _pending.find(guid);
        if (it == _pending.end())
            continue; // the session already logged out
        std::uint32_t const account = it->second;

        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player || !player->GetSession() ||
            player->GetSession()->GetAccountId() != account)
        {
            _pending.erase(it);
            continue;
        }
        if (!IsHumanSession(player))
        {
            _pending.erase(it);
            continue;
        }

        _confirmed[guid] = account;
        ++_sessionsPerAccount[account];
        _pending.erase(it);
    }
}

bool SocialPresence::IsOnline(std::uint32_t accountId) const
{
    auto const it = _sessionsPerAccount.find(accountId);
    return it != _sessionsPerAccount.end() && it->second > 0;
}

bool SocialPresence::IsHumanSession(Player const* player) const
{
    if (!player || !player->GetSession())
        return false;

#if __has_include("Playerbots.h")
    // mod-playerbots cannot run on stock AzerothCore: it requires the Playerbot
    // core fork branch, which is also where WorldSession::IsBot() lives. A bot
    // session is flagged at construction, so this discriminates account-owned
    // and random bots the moment their session exists, independent of when the
    // PlayerbotAI is attached during login.
    if (player->GetSession()->IsBot())
        return false;

    // Defensive secondary check against the AI registry (maps character GUID
    // to an attached PlayerbotAI) for any core that builds Playerbots.
    if (GET_PLAYERBOT_AI(const_cast<Player*>(player)))
        return false;
#endif

    return true;
}
}