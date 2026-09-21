#ifndef NATIVESOCIAL_SOCIALPRESENCE_H
#define NATIVESOCIAL_SOCIALPRESENCE_H

#include "ObjectGuid.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

class Player;

namespace nativesocial
{
// Derived, in-memory account presence. The module does not store "online" as
// durable state: an account is online while it holds at least one confirmed
// human session, and the state rebuilds naturally from login events after a
// restart.
//
// Confirmation is deferred in batches (see Flush) because login hooks run for
// every session, including random/self Playerbots sessions that only attach
// their AI during their own login processing. Evaluating bot identity at
// login time can therefore be premature; classifying after a short settling
// period keeps the signal deterministic.
class SocialPresence
{
public:
    static SocialPresence& Instance();

    void Clear();

    void OnLogin(Player* player);
    void OnLogout(Player* player);

    // Confirms pending human sessions (Playerbot-excluded) into the online
    // accounting. Cheap and safe to call before any presence query.
    void Flush();

    bool IsOnline(std::uint32_t accountId) const;

    std::size_t OnlineAccountCount() const { return _sessionsPerAccount.size(); }
    std::size_t ConfirmedCount() const { return _confirmed.size(); }
    std::size_t PendingCount() const { return _pending.size(); }

    // Invokes fn(accountId) for every account holding at least one confirmed
    // human session. Return false from fn to stop iterating.
    template <typename Fn>
    void ForEachOnlineAccount(Fn&& fn) const
    {
        for (auto const& entry : _sessionsPerAccount)
            if (!fn(entry.first))
                break;
    }

private:
    bool IsHumanSession(Player const* player) const;

    // guid -> account, sessions awaiting classification.
    std::unordered_map<ObjectGuid, std::uint32_t> _pending;
    // guid -> account, sessions confirmed as real human sessions.
    std::unordered_map<ObjectGuid, std::uint32_t> _confirmed;
    // account -> number of confirmed human sessions.
    std::unordered_map<std::uint32_t, std::uint32_t> _sessionsPerAccount;
};
}

#endif