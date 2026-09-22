#include "SocialService.h"

#include "ContentCapabilityApiV1.h"

#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include "Log.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <vector>

namespace nativesocial
{
SocialService* sSocialService = nullptr;

namespace content
{
std::vector<RequiredContent> const& RequiredSocialContent()
{
    // Phase 1 ships no client UI, so nothing is required yet. The moment the
    // first real client functionality exists (Phase 2+), declare its package
    // here; ResolveRequiredContent() then hard-fails the module when it is
    // absent or invalid. There is deliberately no configuration switch to
    // relax this later.
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

void SocialService::Configure(bool enabled, std::uint32_t displayNameMinLength, std::uint32_t displayNameMaxLength, bool reload)
{
    _enabled = enabled;
    _displayNameMinLength = std::max<std::uint32_t>(1, displayNameMinLength);
    _displayNameMaxLength = std::min<std::uint32_t>(48, std::max<std::uint32_t>(_displayNameMinLength, displayNameMaxLength));

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
    if (reload && !_initialized)
        Initialize();
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
            "Native Social phase-1 backend test state: no client content is shipped yet "
            "(RequiredSocialContent() is empty). The .social commands are development/test "
            "interfaces only and are not a supported frontend; phase 2+ declares required "
            "content and this module will hard-fail if it is unavailable.");
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
        message = "set a display name first (.social name <name>) before using appear offline.";
        return false;
    }
    message = value
        ? "you will appear offline to other players; your own view and Game Master "
          "diagnostics still see your true presence."
        : "you will appear online to other players.";
    return true;
}

bool SocialService::IsAccountOnline(std::uint32_t accountId) const
{
    return SocialPresence::Instance().IsOnline(accountId);
}

bool SocialService::IsAccountAdvertisedOnline(std::uint32_t accountId, SocialProfile const& profile) const
{
    if (profile.appearOffline)
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
            if (!profile.displayName.empty() && !profile.appearOffline)
                online.push_back(profile);
            return true;
        });
    std::sort(online.begin(), online.end(),
        [](SocialProfile const& a, SocialProfile const& b) { return a.displayName < b.displayName; });
    return online;
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
    text += _packageInstalled ? ("installed (v" + _packageVersion + ")") : "not installed (none is required in phase 1)";
    text += "\n";
    text += "  Required native content: " + std::to_string(content::RequiredSocialContent().size()) + "\n";
    for (auto const& error : _contentRequirementErrors)
        text += "    error: " + error + "\n";
    text += "  Auth profiles loaded: " + std::to_string(SocialProfileStore::Instance().LoadedCount()) + "\n";
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
