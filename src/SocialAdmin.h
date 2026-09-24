#ifndef NATIVESOCIAL_SOCIALADMIN_H
#define NATIVESOCIAL_SOCIALADMIN_H

#include "SocialProfile.h"

#include <cstdint>
#include <functional>
#include <string>

namespace nativesocial
{
enum class CoreAccountCreateResult
{
    Ok,
    NameTooLong,
    PasswordTooLong,
    NameAlreadyExists,
    StorageFailure,
    InvalidInput
};

enum class AdminResultCode
{
    Success,
    Unauthorized,
    InvalidAccount,
    InvalidPassword,
    InvalidDisplayName,
    DisplayNameTaken,
    AccountAlreadyExists,
    AccountCreateFailed,
    ProfileSetupFailed
};

struct AccountCreateResult
{
    CoreAccountCreateResult code = CoreAccountCreateResult::StorageFailure;
    std::uint32_t accountId = 0;
};

struct AdminResult
{
    AdminResultCode code = AdminResultCode::AccountCreateFailed;
    std::uint32_t accountId = 0;
    std::string message;

    bool Complete() const { return code == AdminResultCode::Success; }
};

using AccountCreateFn = std::function<AccountCreateResult(std::string const&, std::string const&)>;
using ProfilePreflightFn = std::function<NameResult(std::string const&)>;
using ProfileSetFn = std::function<NameResult(std::uint32_t, std::string const&)>;

// One-operation account + profile orchestration. Validation precedes the core
// mutation. A core success followed by profile failure is deliberately a
// partial failure: the authentication account is retained and its id is
// returned for repair; compensating deletion is never attempted.
AdminResult CreateManagedAccount(bool authorized, std::string const& accountName,
    std::string password, std::string displayName,
    std::uint32_t displayNameMinLength, std::uint32_t displayNameMaxLength,
    ProfilePreflightFn const& preflightProfile,
    AccountCreateFn const& createAccount, ProfileSetFn const& setProfile);

char const* AdminResultCodeName(AdminResultCode code);
}

#endif
