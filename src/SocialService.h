#ifndef NATIVESOCIAL_SOCIALSERVICE_H
#define NATIVESOCIAL_SOCIALSERVICE_H

#include "SocialPresence.h"
#include "SocialProfile.h"
#include "SocialProfileStore.h"
#include "SocialAdmin.h"
#include "SocialAccountEligibility.h"
#include "SocialDirectory.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

class Player;
class WorldSession;

namespace ContentCapabilitiesV1
{
class Provider;
}

namespace nativesocial
{
namespace content
{
// Package identity of the native client content this module produces (matches
// the EPF manifest "package" field and content_manager_package.world rows).
// The schema-3 EPF declares its protected-framexml client requirement. The
// older capability-provider ABI below validates semantic vendor declarations,
// not build-level client requirements, so that set remains separate.
inline constexpr char const* Package = "mod-native-social";

// One declared piece of native client content the module needs to operate
// normally. There is NO supported degraded/unpatched mode: when a required
// package is unavailable or invalid the module fails clearly and the
// temporary .social commands report the specific reason instead of serving
// as a substitute frontend. Game Master diagnostics stay available.
//
// Activation of a package is not proven by a row in content_manager_package
// (that only records that it was registered). The module validates the
// requirement through the Content Manager capability provider, which only
// reports Ready when the package's vendor declaration is ACTIVE for the
// current realm and server-APPLIED. `vendorSymbol` identifies the declaration
// inside the package that makes the requirement usable.
struct RequiredContent
{
    std::string package;      // content_manager_package.package_key
    std::string vendorSymbol; // ACTIVE/APPLIED vendor declaration inside the package
    std::string note;         // why it is required / what it ships
};

// Vendor-backed server content requirements, if any. The Players UI is raw
// FrameXML and therefore intentionally is not represented as a fake vendor.
std::vector<RequiredContent> const& RequiredSocialContent();
}

// Facade for the server facilities. Commands and the native client protocol
// consume this surface only; storage, presence and
// content-manager validation stay behind it.
class SocialService
{
public:
    // Public so the module loader can allocate the process-lifetime singleton
    // once; Instance() also falls back to a function-local object, so every
    // call site stays valid even before the loader runs.
    SocialService() = default;

    static SocialService& Instance();

    void Configure(bool enabled, std::uint32_t displayNameMinLength,
        std::uint32_t displayNameMaxLength, std::string playerbotAccountPrefix,
        std::string excludedAccounts, bool reload);

    // Startup hook (OnStartup). Probes the Content Manager integration,
    // verifies required client content and loads auth profiles. Returns
    // whether the module is operational.
    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return _initialized; }

    // Called from WorldScript::OnUpdate to pace presence confirmation.
    void Update(std::uint32_t diff);
    void FlushPresence();

    bool Enabled() const { return _enabled; }
    bool Available() const { return _available; }
    std::string const& FailureReason() const { return _reason; }

    bool ContentManagerPresent() const { return _contentManagerPresent; }
    std::string const& ContentManagerOperationState() const { return _contentManagerOperationState; }
    // Registered package state (diagnostic only; activation/capability is
    // enforced by the schema-3 build/publication/launcher chain).
    bool PackageInstalled() const { return _packageInstalled; }
    std::string PackageVersion() const { return _packageInstalled ? _packageVersion : ""; }
    std::vector<std::string> const& ContentRequirementErrors() const { return _contentRequirementErrors; }

    std::uint32_t DisplayNameMinLength() const { return _displayNameMinLength; }
    std::uint32_t DisplayNameMaxLength() const { return _displayNameMaxLength; }

    // Player lifecycle entry points (PlayerScript hooks).
    void HandlePlayerLogin(Player* player);
    void HandlePlayerLogout(Player* player);

    // Profile operations.
    bool GetProfile(std::uint32_t accountId, SocialProfile& out) const;
    NameResult SetDisplayName(std::uint32_t accountId, std::string const& displayName);
    bool SetAppearOffline(std::uint32_t accountId, bool value, std::string& message);
    bool GetOwnProfile(WorldSession const* session, SocialProfile& out) const;
    NameResult SaveOwnProfile(WorldSession const* session, std::string const& displayName,
        bool appearOffline, std::string& message);

    // Presence, honouring the appear-offline privacy flag.
    bool IsAccountOnline(std::uint32_t accountId) const;
    bool IsAccountAdvertisedOnline(std::uint32_t accountId, SocialProfile const& profile) const;
    std::vector<SocialProfile> ListAdvertisedOnline() const;

    // Public account directory. Only configured profiles are returned; login
    // usernames never enter this model. Presence is localized for the viewer.
    std::vector<DirectoryEntry> BuildPublicDirectory(WorldSession const* viewer);

    struct AdminProfileState
    {
        std::uint32_t accountId = 0;
        std::string displayName;
        bool configured = false;
    };

    bool IsAuthorizedAdmin(WorldSession const* session) const;
    std::vector<AdminProfileState> ListAdminProfiles(WorldSession const* session) const;
    NameResult AdminSetDisplayName(WorldSession const* session, std::uint32_t accountId,
        std::string const& displayName);
    AdminResult AdminCreateAccount(WorldSession* session, std::string const& accountName,
        std::string password, std::string const& displayName);
    bool IsEligibleHumanAccount(std::uint32_t accountId) const;

    std::string Diagnostics() const;

private:
    // Discovers the (single) Content Manager capability-provider WorldScript
    // and verifies the integration is actually operational (content-manager
    // enabled in configuration, not merely compiled in), filling _reason on
    // failure. Retains the resolved provider for ResolveRequiredContent().
    bool ProbeContentManager();
    void ProbePackage();
    // Verifies every entry of content::RequiredSocialContent() through the
    // Content Manager capability provider (ACTIVE/APPLIED resolution for the
    // current realm) and fills _contentRequirementErrors on failure.
    bool ResolveRequiredContent();
    void LoadConfiguredAccountExclusions();
    bool IsExcludedAccount(std::uint32_t accountId) const;
    std::string PresenceLocation(Player const* player, WorldSession const* viewer) const;

    bool _enabled = false;
    bool _available = false;
    bool _initialized = false;
    bool _contentManagerPresent = false;
    std::string _contentManagerOperationState;
    ContentCapabilitiesV1::Provider const* _contentProvider = nullptr;
    bool _packageInstalled = false;
    std::string _packageVersion;
    std::vector<std::string> _contentRequirementErrors;
    std::string _reason = "configuration disables Native Social";

    std::uint32_t _displayNameMinLength = 3;
    std::uint32_t _displayNameMaxLength = 24;
    SocialAccountEligibility _accountEligibility;
    std::unordered_set<std::uint32_t> _excludedAccountIds;
    std::uint32_t _updateTimer = 0;
};

// Singleton accessor mirroring the sContentManager convention.
extern SocialService* sSocialService;
}

#endif
