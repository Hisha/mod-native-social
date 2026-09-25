#include "SocialService.h"

#include "ContentCapabilityApiV1.h"

#include "AccountMgr.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Field.h"
#include "QueryResult.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nativesocial
{
SocialService* sSocialService = nullptr;

namespace content
{
std::vector<RequiredContent> const& RequiredSocialContent()
{
    // This legacy provider surface is for ACTIVE/APPLIED vendor capabilities.
    // The Players UI is raw FrameXML whose protected-framexml requirement is
    // declared in the schema-3 EPF and propagated by the build/realm/launcher
    // chain, so inventing a vendor declaration here would be incorrect.
    static std::vector<RequiredContent> const required;
    return required;
}
}

SocialService& SocialService::Instance()
{
    // The service is allocated once by the module loader and lives for the
    // whole process; falling back to a function-local singleton keeps the
    // accessor safe for every call site.
    static SocialService fallback;
    return sSocialService ? *sSocialService : fallback;
}

void SocialService::Configure(bool enabled, std::uint32_t displayNameMinLength,
    std::uint32_t displayNameMaxLength, std::string playerbotAccountPrefix,
    std::string excludedAccounts, bool reload)
{
    _enabled = enabled;
    _displayNameMinLength = std::max<std::uint32_t>(1, displayNameMinLength);
    _displayNameMaxLength = std::min<std::uint32_t>(48, std::max<std::uint32_t>(_displayNameMinLength, displayNameMaxLength));
    _accountEligibility.Configure(std::move(playerbotAccountPrefix), std::move(excludedAccounts));

    if (!_enabled)
    {
        _available = false;
        _reason = "configuration disables Native Social";
        if (_initialized)
            Shutdown();
        return;
    }
    // The initial flow initializes in OnStartup. A reload can switch the
    // module on after the server is already running, so re-initialize here.
    if (reload)
    {
        if (!_initialized)
            Initialize();
        else if (_available)
            LoadConfiguredAccountExclusions();
    }
}

bool SocialService::Initialize()
{
    if (_initialized)
        return _available;
    _initialized = true;
    _available = false;
    _contentRequirementErrors.clear();

    if (!_enabled)
    {
        _reason = "configuration disables Native Social";
        LOG_INFO("module.native_social", "mod-native-social is disabled by configuration.");
        return false;
    }

    if (!ProbeContentManager())
    {
        LOG_ERROR("module.native_social",
            "mod-native-social will not run: {}", _reason);
        return false;
    }

    if (!ResolveRequiredContent())
    {
        LOG_ERROR("module.native_social",
            "mod-native-social will not run: required native client content is unavailable: {}", _reason);
        return false;
    }

    if (!SocialProfileStore::Instance().LoadAll())
    {
        _reason = "the native_social_account table in the auth database is missing or unreadable; "
                  "apply the module auth SQL (data/sql/db-auth/base) and restart the core";
        LOG_ERROR("module.native_social", "mod-native-social will not run: {}", _reason);
        return false;
    }

    LoadConfiguredAccountExclusions();

    SocialPresence::Instance().Clear();

    _available = true;
    _reason.clear();

    LOG_INFO("server.loading", "Native Social backend ready: {} profile(s) loaded, {} accounts online, "
        "Content Manager operational, {} required client content requirement(s).",
        SocialProfileStore::Instance().LoadedCount(),
        SocialPresence::Instance().OnlineAccountCount(),
        content::RequiredSocialContent().size());

    if (content::RequiredSocialContent().empty())
    {
        LOG_INFO("server.loading",
            "Native Social has no vendor-backed server content requirements. The Players "
            "FrameXML package declares protected-framexml through EPF schema 3; capability "
            "publication and launcher selection are enforced downstream.");
    }
    return true;
}

void SocialService::Shutdown()
{
    _initialized = false;
    _available = false;
    _contentManagerPresent = false;
    _contentManagerOperationState.clear();
    _contentProvider = nullptr;
    _packageInstalled = false;
    _packageVersion.clear();
    _contentRequirementErrors.clear();
    _excludedAccountIds.clear();
    SocialPresence::Instance().Clear();
    SocialProfileStore::Instance().Clear();
}

void SocialService::Update(std::uint32_t diff)
{
    if (!_available)
        return;
    _updateTimer += diff;
    if (_updateTimer < 1000)
        return;
    _updateTimer -= 1000;
    SocialPresence::Instance().Flush();
}

void SocialService::FlushPresence()
{
    if (!_available)
        return;
    SocialPresence::Instance().Flush();
}

void SocialService::HandlePlayerLogin(Player* player)
{
    if (!_available)
        return;
    SocialPresence::Instance().OnLogin(player);
}

void SocialService::HandlePlayerLogout(Player* player)
{
    if (!_available)
        return;
    SocialPresence::Instance().OnLogout(player);
}

bool SocialService::GetProfile(std::uint32_t accountId, SocialProfile& out) const
{
    if (SocialProfileStore::Instance().FindAccount(accountId, out))
        return true;
    out = SocialProfile();
    out.accountId = accountId;
    return false;
}

NameResult SocialService::SetDisplayName(std::uint32_t accountId, std::string const& displayName)
{
    std::string candidate = displayName;
    NameResult const result = ValidateDisplayName(candidate, _displayNameMinLength, _displayNameMaxLength);
    if (result != NameResult::Ok)
        return result;
    return SocialProfileStore::Instance().SetDisplayName(accountId, candidate);
}

bool SocialService::SetAppearOffline(std::uint32_t accountId, bool value, std::string& message)
{
    if (!SocialProfileStore::Instance().SetAppearOffline(accountId, value))
    {
        message = "the appear-offline setting could not be saved; configure a display name first.";
        return false;
    }
    message = value
        ? "you will appear offline to other players; your own view and Game Master "
          "diagnostics still see your true presence."
        : "you will appear online to other players.";
    return true;
}

bool SocialService::GetOwnProfile(WorldSession const* session, SocialProfile& out) const
{
    if (!session)
        return false;
    std::uint32_t const accountId = session->GetAccountId();
    if (!IsEligibleHumanAccount(accountId))
        return false;
    GetProfile(accountId, out);
    return true;
}

NameResult SocialService::SaveOwnProfile(WorldSession const* session,
    std::string const& displayName, bool appearOffline, std::string& message)
{
    if (!session)
    {
        message = "an authenticated session is required.";
        return NameResult::StorageFailure;
    }

    std::uint32_t const accountId = session->GetAccountId();
    if (!IsEligibleHumanAccount(accountId))
    {
        message = "this account is not eligible for Native Social.";
        return NameResult::StorageFailure;
    }

    NameResult const nameResult = SetDisplayName(accountId, displayName);
    if (nameResult != NameResult::Ok)
    {
        message = "display name " + NameResultToString(nameResult) + ".";
        return nameResult;
    }

    std::string appearOfflineMessage;
    if (!SetAppearOffline(accountId, appearOffline, appearOfflineMessage))
    {
        message = "display name saved, but " + appearOfflineMessage;
        return NameResult::StorageFailure;
    }

    message = "profile saved.";
    return NameResult::Ok;
}

bool SocialService::IsAccountOnline(std::uint32_t accountId) const
{
    return SocialPresence::Instance().IsOnline(accountId);
}

bool SocialService::IsAccountAdvertisedOnline(std::uint32_t accountId, SocialProfile const& profile) const
{
    if (profile.appearOffline || IsExcludedAccount(accountId))
        return false;
    return SocialPresence::Instance().IsOnline(accountId);
}

std::vector<SocialProfile> SocialService::ListAdvertisedOnline() const
{
    std::vector<SocialProfile> online;
    SocialPresence::Instance().ForEachOnlineAccount(
        [&](std::uint32_t accountId)
        {
            SocialProfile profile;
            GetProfile(accountId, profile);
            if (!profile.displayName.empty() && IsAccountAdvertisedOnline(accountId, profile))
                online.push_back(profile);
            return true;
        });
    std::sort(online.begin(), online.end(),
        [](SocialProfile const& a, SocialProfile const& b) { return a.displayName < b.displayName; });
    return online;
}

std::string SocialService::PresenceLocation(Player const* player, WorldSession const* viewer) const
{
    if (!player || !viewer)
        return "Unknown";
    LocaleConstant const locale = viewer->GetSessionDbcLocale();
    if (Map const* map = player->FindMap())
    {
        if (map->Instanceable())
            if (MapEntry const* mapEntry = sMapStore.LookupEntry(player->GetMapId()))
                return mapEntry->name[locale];
    }
    if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(player->GetZoneId()))
        return area->area_name[locale];
    return "Unknown";
}

std::vector<DirectoryEntry> SocialService::BuildPublicDirectory(WorldSession const* viewer)
{
    FlushPresence();

    std::vector<SocialProfile> const profiles = SocialProfileStore::Instance().AllProfiles();
    std::unordered_set<std::uint32_t> excludedAccounts;
    for (SocialProfile const& profile : profiles)
        if (IsExcludedAccount(profile.accountId))
            excludedAccounts.insert(profile.accountId);

    DirectoryPresenceMap live;
    for (SocialProfile const& profile : profiles)
    {
        if (excludedAccounts.count(profile.accountId) != 0)
            continue;
        Player* player = SocialPresence::Instance().ActiveCharacterForAccount(profile.accountId);
        if (!player)
            continue;
        PresenceInfo presence;
        presence.online = true;
        presence.characterName = player->GetName();
        presence.level = player->GetLevel();
        presence.race = player->getRace();
        presence.charClass = player->getClass();
        presence.faction = player->GetTeamId() == TEAM_HORDE ? 'H' : 'A';
        presence.zone = PresenceLocation(player, viewer);
        live.emplace(profile.accountId, std::move(presence));
    }
    return BuildDirectory(profiles, excludedAccounts, live);
}

bool SocialService::IsAuthorizedAdmin(WorldSession const* session) const
{
    return session && session->GetSecurity() >= SEC_ADMINISTRATOR;
}

bool SocialService::IsEligibleHumanAccount(std::uint32_t accountId) const
{
    if (!accountId || IsExcludedAccount(accountId))
        return false;
    QueryResult account = LoginDatabase.Query(
        "SELECT id FROM account WHERE id = " + std::to_string(accountId));
    return static_cast<bool>(account);
}

std::vector<SocialService::AdminProfileState> SocialService::ListAdminProfiles(WorldSession const* session) const
{
    std::vector<AdminProfileState> states;
    if (!IsAuthorizedAdmin(session))
        return states;
    QueryResult accounts = LoginDatabase.Query("SELECT id FROM account ORDER BY id");
    if (!accounts)
        return states;
    do
    {
        std::uint32_t const accountId = accounts->Fetch()[0].Get<std::uint32_t>();
        if (!IsEligibleHumanAccount(accountId))
            continue;
        SocialProfile profile;
        bool const configured = SocialProfileStore::Instance().FindAccount(accountId, profile)
            && !profile.displayName.empty();
        states.push_back({ accountId, configured ? profile.displayName : "", configured });
    }
    while (accounts->NextRow());
    return states;
}

NameResult SocialService::AdminSetDisplayName(WorldSession const* session,
    std::uint32_t accountId, std::string const& displayName)
{
    if (!IsAuthorizedAdmin(session) || !IsEligibleHumanAccount(accountId))
        return NameResult::StorageFailure;
    return SetDisplayName(accountId, displayName);
}

AdminResult SocialService::AdminCreateAccount(WorldSession* session,
    std::string const& accountName, std::string password, std::string const& displayName)
{
    bool const authorized = IsAuthorizedAdmin(session) &&
        session->HasPermission(rbac::RBAC_PERM_COMMAND_ACCOUNT_CREATE);
    return CreateManagedAccount(authorized, accountName, std::move(password), displayName,
        _displayNameMinLength, _displayNameMaxLength,
        [](std::string const& name)
        {
            return SocialProfileStore::Instance().CheckDisplayNameAvailable(0, name);
        },
        [this](std::string const& name, std::string const& secret)
        {
            if (!_accountEligibility.IsEligibleUsername(name))
                return AccountCreateResult{ CoreAccountCreateResult::InvalidInput, 0 };
            AccountOpResult const coreResult = sAccountMgr->CreateAccount(name, secret);
            CoreAccountCreateResult result = CoreAccountCreateResult::StorageFailure;
            switch (coreResult)
            {
                case AOR_OK: result = CoreAccountCreateResult::Ok; break;
                case AOR_NAME_TOO_LONG: result = CoreAccountCreateResult::NameTooLong; break;
                case AOR_PASS_TOO_LONG: result = CoreAccountCreateResult::PasswordTooLong; break;
                case AOR_NAME_ALREADY_EXIST: result = CoreAccountCreateResult::NameAlreadyExists; break;
                case AOR_DB_INTERNAL_ERROR: result = CoreAccountCreateResult::StorageFailure; break;
                default: result = CoreAccountCreateResult::InvalidInput; break;
            }
            std::uint32_t const accountId = result == CoreAccountCreateResult::Ok
                ? AccountMgr::GetId(name) : 0;
            if (result == CoreAccountCreateResult::Ok && accountId == 0)
                result = CoreAccountCreateResult::StorageFailure;
            return AccountCreateResult{ result, accountId };
        },
        [this](std::uint32_t accountId, std::string const& name)
        {
            return SocialProfileStore::Instance().SetDisplayName(accountId, name);
        });
}

void SocialService::LoadConfiguredAccountExclusions()
{
    _excludedAccountIds.clear();
    QueryResult result = LoginDatabase.Query("SELECT id, username FROM account");
    if (!result)
        return;
    do
    {
        Field* fields = result->Fetch();
        if (!_accountEligibility.IsEligibleUsername(fields[1].Get<std::string>()))
            _excludedAccountIds.insert(fields[0].Get<std::uint32_t>());
    }
    while (result->NextRow());
}

bool SocialService::IsExcludedAccount(std::uint32_t accountId) const
{
    return _excludedAccountIds.count(accountId) != 0 ||
        SocialPresence::Instance().IsKnownBotAccount(accountId);
}

std::string SocialService::Diagnostics() const
{
    std::string text = "[Native Social] Diagnostics\n";
    text += "  Enabled: " + std::string(_enabled ? "yes" : "no") + "\n";
    text += "  Operational: " + std::string(_available ? "yes" : "no") + "\n";
    if (!_reason.empty())
        text += "  Status: " + _reason + "\n";
    text += "  Content Manager: " + std::string(_contentManagerPresent ? "present" : "absent") + "\n";
    if (_contentManagerPresent && !_contentManagerOperationState.empty())
        text += "  Content Manager state: " + _contentManagerOperationState + "\n";
    text += "  Client package '";
    text += content::Package;
    text += "': ";
    text += _packageInstalled ? ("registered (v" + _packageVersion + ")") : "not registered";
    text += "\n";
    text += "  Required native content: " + std::to_string(content::RequiredSocialContent().size()) + "\n";
    for (auto const& error : _contentRequirementErrors)
        text += "    error: " + error + "\n";
    text += "  Auth profiles loaded: " + std::to_string(SocialProfileStore::Instance().LoadedCount()) + "\n";
    text += "  Configured account exclusions: " + std::to_string(_excludedAccountIds.size())
        + " account(s), " + std::to_string(_accountEligibility.ExplicitExclusionCount())
        + " explicit name(s)\n";
    text += "  Presence: " + std::to_string(SocialPresence::Instance().ConfirmedCount()) + " confirmed, "
        + std::to_string(SocialPresence::Instance().PendingCount()) + " pending, "
        + std::to_string(SocialPresence::Instance().OnlineAccountCount()) + " online\n";
    text += "  Display name bounds: " + std::to_string(_displayNameMinLength) + ".."
        + std::to_string(_displayNameMaxLength) + "\n";
    text += "  NSOC protocol: v1 available\n";
    return text;
}

bool SocialService::ProbeContentManager()
{
    _contentManagerPresent = false;
    _contentProvider = nullptr;
    _contentManagerOperationState.clear();

    for (auto const& script : ScriptRegistry<WorldScript>::ScriptPointerList)
    {
        if (dynamic_cast<ContentCapabilitiesV1::Provider const*>(script.second))
        {
            if (_contentManagerPresent)
            {
                _reason = "multiple Content Manager capability providers are registered; "
                          "the integration is ambiguous and Native Social cannot safely run";
                _contentManagerPresent = false;
                return false;
            }
            _contentManagerPresent = true;
            _contentProvider = dynamic_cast<ContentCapabilitiesV1::Provider const*>(script.second);
        }
    }
    if (!_contentManagerPresent)
    {
        _reason = "mod-content-manager integration is required but no Content Manager "
                  "capability provider is registered; build and enable mod-content-manager "
                  "as a sibling module first";
        return false;
    }

    // Registration alone does not mean the integration is operational:
    // mod-content-manager registers its provider whenever it is compiled in,
    // even when ContentManager.Enable is off. Resolve() is the one ABI we are
    // allowed to call against it, and its first gate is the enable flag, so a
    // probe through the provider is the honest operational check. Phase 1
    // declares no required client content, so content-manager answering
    // "enabled but nothing active here" — or returning Invalid because an
    // ACTIVE build belongs to another realm of a shared auth DB — is healthy:
    // the integration is running and decision-making about our (empty)
    // required set happens per-requirement in ResolveRequiredContent().
    ContentCapabilitiesV1::Vendor vendor;
    std::vector<ContentCapabilitiesV1::Resource> resources;
    std::string reason;
    ContentCapabilitiesV1::Result const probe = _contentProvider->Resolve(
        content::Package, "", resources, vendor, reason);

    switch (probe)
    {
        case ContentCapabilitiesV1::Result::Invalid:
            _contentManagerOperationState = "operational but native content cannot be "
                "resolved for this realm (" + reason + ")";
            return true;
        case ContentCapabilitiesV1::Result::Inactive:
            if (reason == "Content Manager disabled")
            {
                _reason = "mod-content-manager is compiled in but Content Manager is disabled "
                          "(set ContentManager.Enable = 1); the provider registration alone "
                          "is not an operational integration";
                return false;
            }
            _contentManagerOperationState = "operational; no native content activated ("
                + reason + ")";
            return true;
        case ContentCapabilitiesV1::Result::Ready:
            _contentManagerOperationState = "operational; native content activated for this realm";
            return true;
    }
    _reason = "mod-content-manager provider returned an unknown state";
    return false;
}

void SocialService::ProbePackage()
{
    _packageInstalled = false;
    _packageVersion.clear();
    QueryResult result = WorldDatabase.Query(
        "SELECT version FROM content_manager_package WHERE package_key = '" +
        std::string(content::Package) + "'");
    if (!result)
        return;
    _packageInstalled = true;
    _packageVersion = result->Fetch()[0].Get<std::string>();
}

bool SocialService::ResolveRequiredContent()
{
    _contentRequirementErrors.clear();

    // Diagnostic only: whether the module's own package is registered at all.
    // A row in content_manager_package is NOT proof the package is active for
    // this realm; the per-requirement validation below goes through the
    // Content Manager capability provider, which is the real activation check.
    ProbePackage();

    for (auto const& requirement : content::RequiredSocialContent())
    {
        if (!_contentProvider)
        {
            _contentRequirementErrors.push_back(
                "required package '" + requirement.package + "' cannot be validated: "
                "no Content Manager capability provider is available");
            continue;
        }

        ContentCapabilitiesV1::Vendor vendor;
        std::vector<ContentCapabilitiesV1::Resource> resources;
        std::string reason;

        ContentCapabilitiesV1::Result const result = _contentProvider->Resolve(
            requirement.package, requirement.vendorSymbol, resources, vendor, reason);

        if (result != ContentCapabilitiesV1::Result::Ready)
        {
            _contentRequirementErrors.push_back(
                "required package '" + requirement.package + "' is not ACTIVE and APPLIED "
                "for the current realm (" + reason + "; " + requirement.note + ")");
        }
    }

    if (_contentRequirementErrors.empty())
        return true;

    _reason.clear();
    for (std::size_t i = 0; i < _contentRequirementErrors.size(); ++i)
    {
        if (i > 0)
            _reason += "; ";
        _reason += _contentRequirementErrors[i];
    }
    return false;
}
}
