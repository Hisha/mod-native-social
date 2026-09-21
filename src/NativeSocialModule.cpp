#include "SocialService.h"

#include "Chat.h"
#include "CommandScript.h"
#include "ConfigValueCache.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
enum class NativeSocialConfig
{
    Enabled,
    DisplayNameMinLength,
    DisplayNameMaxLength,
    Count
};

class NativeSocialConfigCache final : public ConfigValueCache<NativeSocialConfig>
{
public:
    NativeSocialConfigCache() : ConfigValueCache(NativeSocialConfig::Count) { }
    void BuildConfigCache() override
    {
        SetConfigValue<bool>(NativeSocialConfig::Enabled, "NativeSocial.Enable", true);
        SetConfigValue<std::uint32_t>(NativeSocialConfig::DisplayNameMinLength, "NativeSocial.DisplayNameMinLength", 3);
        SetConfigValue<std::uint32_t>(NativeSocialConfig::DisplayNameMaxLength, "NativeSocial.DisplayNameMaxLength", 24);
    }
};

NativeSocialConfigCache socialConfig;

std::string const kSocialPrefix = "[Native Social] ";

void SendSocial(ChatHandler* handler, std::string const& message)
{
    if (handler)
        handler->SendSysMessage(kSocialPrefix + message);
}

Player* GetCommandPlayer(ChatHandler* handler)
{
    return handler && handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
}

nativesocial::SocialService* GetService()
{
    // The loader allocates the singleton before any hook can fire; fall back
    // to the static instance so commands never dereference a dangling pointer.
    return nativesocial::sSocialService ? nativesocial::sSocialService : &nativesocial::SocialService::Instance();
}

bool ModuleUsable(ChatHandler* handler)
{
    if (GetService()->Available())
        return true;
    std::string reason = GetService()->FailureReason();
    if (reason.empty())
        reason = "disabled by configuration";
    SendSocial(handler, "unavailable: " + reason);
    SendSocial(handler, "mod-native-social has no reduced or unpatched mode: required native client "
        "functionality is mandatory, and a GM can inspect the specific failure with .social diag.");
    return false;
}

class NativeSocialWorldScript final : public WorldScript
{
public:
    NativeSocialWorldScript() : WorldScript("NativeSocialWorldScript",
        { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE }) { }

    void OnAfterConfigLoad(bool reload) override
    {
        socialConfig.Initialize(reload);
        bool const enabled = socialConfig.GetConfigValue<bool>(NativeSocialConfig::Enabled);
        std::uint32_t const minLength = std::max<std::uint32_t>(1,
            socialConfig.GetConfigValue<std::uint32_t>(NativeSocialConfig::DisplayNameMinLength));
        std::uint32_t const maxLength = std::min<std::uint32_t>(48,
            std::max<std::uint32_t>(minLength,
                socialConfig.GetConfigValue<std::uint32_t>(NativeSocialConfig::DisplayNameMaxLength)));
        GetService()->Configure(enabled, minLength, maxLength, reload);

        if (!enabled)
        {
            GetService()->Shutdown();
            LOG_INFO("server.loading", "Native Social is disabled by configuration.");
            return;
        }
        if (reload)
            LOG_INFO("server.loading", "Native Social configuration reloaded.");
    }

    void OnStartup() override
    {
        if (socialConfig.GetConfigValue<bool>(NativeSocialConfig::Enabled))
            GetService()->Initialize();
    }

    void OnUpdate(std::uint32_t diff) override
    {
        if (socialConfig.GetConfigValue<bool>(NativeSocialConfig::Enabled))
            GetService()->Update(diff);
    }
};

class NativeSocialPlayerScript final : public PlayerScript
{
public:
    NativeSocialPlayerScript() : PlayerScript("NativeSocialPlayerScript",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT }) { }

    void OnPlayerLogin(Player* player) override { GetService()->HandlePlayerLogin(player); }
    void OnPlayerLogout(Player* player) override { GetService()->HandlePlayerLogout(player); }
};

class NativeSocialCommandScript final : public CommandScript
{
public:
    NativeSocialCommandScript() : CommandScript("NativeSocialCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable socialTable = {
            { "name", HandleName, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::No },
            { "online", HandleOnline, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::No },
            { "offline", HandleOffline, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::No },
            { "status", HandleStatus, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::No },
            { "list", HandleList, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::No },
            { "diag", HandleDiag, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::No }
        };
        static ChatCommandTable root = { { "social", socialTable } };
        return root;
    }

private:
    static std::uint32_t SelfAccount(ChatHandler* handler)
    {
        Player* const player = GetCommandPlayer(handler);
        return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
    }

    static bool HandleName(ChatHandler* handler, Optional<std::string> name)
    {
        if (!ModuleUsable(handler))
            return true;
        std::uint32_t const accountId = SelfAccount(handler);
        if (!accountId)
            return true;
        if (!name || name.value().empty())
        {
            SendSocial(handler, "usage: .social name <display name> ("
                + std::to_string(GetService()->DisplayNameMinLength()) + ".."
                + std::to_string(GetService()->DisplayNameMaxLength())
                + " characters, unique per realm, case-insensitive).");
            return true;
        }

        nativesocial::NameResult const result = GetService()->SetDisplayName(accountId, name.value());
        switch (result)
        {
            case nativesocial::NameResult::Ok:
            {
                nativesocial::SocialProfile own;
                GetService()->GetProfile(accountId, own);
                std::string const shown = own.displayName.empty() ? "your name" : "'" + own.displayName + "'";
                SendSocial(handler, "display name set to " + shown
                    + ". Use .social online or .social offline to control availability.");
                break;
            }
            case nativesocial::NameResult::TooShort:
                SendSocial(handler, "that name must be at least " + std::to_string(GetService()->DisplayNameMinLength()) + " characters.");
                break;
            case nativesocial::NameResult::TooLong:
                SendSocial(handler, "that name must be at most " + std::to_string(GetService()->DisplayNameMaxLength()) + " characters.");
                break;
            case nativesocial::NameResult::InvalidCharacter:
                SendSocial(handler, "that name contains characters that are not allowed (no control characters, '|' or '%').");
                break;
            case nativesocial::NameResult::AlreadyTaken:
                SendSocial(handler, "that name is already taken by another account on this realm.");
                break;
            case nativesocial::NameResult::StorageFailure:
                SendSocial(handler, "the name could not be saved to the auth database; see the world server log.");
                break;
        }
        return true;
    }

    static bool HandleOnline(ChatHandler* handler)
    {
        if (!ModuleUsable(handler))
            return true;
        std::uint32_t const accountId = SelfAccount(handler);
        if (!accountId)
            return true;
        std::string message;
        GetService()->SetAppearOffline(accountId, false, message);
        SendSocial(handler, message);
        return true;
    }

    static bool HandleOffline(ChatHandler* handler)
    {
        if (!ModuleUsable(handler))
            return true;
        std::uint32_t const accountId = SelfAccount(handler);
        if (!accountId)
            return true;
        std::string message;
        GetService()->SetAppearOffline(accountId, true, message);
        SendSocial(handler, message);
        return true;
    }

    static bool HandleStatus(ChatHandler* handler, Optional<std::string> target)
    {
        if (!ModuleUsable(handler))
            return true;
        std::uint32_t const selfAccount = SelfAccount(handler);
        if (!selfAccount)
            return true;

        GetService()->FlushPresence();

        if (target && !target.value().empty())
        {
            nativesocial::SocialProfile profile;
            if (!nativesocial::SocialProfileStore::Instance().FindByNameKey(
                    nativesocial::DisplayNameKey(target.value()), profile))
            {
                SendSocial(handler, "no player uses the display name '" + target.value()
                    + "' on this realm.");
                return true;
            }
            if (profile.accountId == selfAccount)
            {
                nativesocial::SocialProfile own;
                GetService()->GetProfile(profile.accountId, own);
                if (own.displayName.empty())
                {
                    SendSocial(handler, "you have not set a display name yet. Use .social name <name>.");
                    return true;
                }
                SendSocial(handler, "you are using display name '" + own.displayName + "' and you currently "
                    + (GetService()->IsAccountAdvertisedOnline(profile.accountId, own)
                        ? "appear online." : "appear offline."));
                return true;
            }
            SendSocial(handler, "'" + profile.displayName + "' is "
                + (GetService()->IsAccountAdvertisedOnline(profile.accountId, profile) ? "online." : "offline."));
            return true;
        }

        // Self-status without an argument.
        nativesocial::SocialProfile own;
        GetService()->GetProfile(selfAccount, own);
        if (own.displayName.empty())
        {
            SendSocial(handler, "you have not set a display name yet. Use .social name <name>.");
            return true;
        }
        SendSocial(handler, "you use display name '" + own.displayName + "' and you currently "
            + (GetService()->IsAccountAdvertisedOnline(selfAccount, own) ? "appear online" : "appear offline")
            + ". Others see any of your accounts as"
            + (own.appearOffline ? " offline." : " online."));
        return true;
    }

    static bool HandleList(ChatHandler* handler)
    {
        if (!ModuleUsable(handler))
            return true;
        GetService()->FlushPresence();
        std::vector<nativesocial::SocialProfile> const online = GetService()->ListAdvertisedOnline();
        if (online.empty())
        {
            SendSocial(handler, "no players are online with a Native Social display name right now.");
            return true;
        }
        SendSocial(handler, "online (advertised): " + std::to_string(online.size()));
        std::size_t const shown = std::min<std::size_t>(online.size(), 50);
        for (std::size_t i = 0; i < shown; ++i)
            SendSocial(handler, "  - " + online[i].displayName);
        if (online.size() > shown)
            SendSocial(handler, "  ... and " + std::to_string(online.size() - shown) + " more.");
        return true;
    }

    static bool HandleDiag(ChatHandler* handler)
    {
        Player* const player = GetCommandPlayer(handler);
        if (!player || !player->IsGameMaster())
        {
            SendSocial(handler, "diagnostics are restricted to Game Masters.");
            return true;
        }
        handler->SendSysMessage(GetService()->Diagnostics());
        return true;
    }
};
}

void AddNativeSocialModuleScripts()
{
    nativesocial::sSocialService = new nativesocial::SocialService();
    new NativeSocialWorldScript();
    new NativeSocialPlayerScript();
    new NativeSocialCommandScript();
}