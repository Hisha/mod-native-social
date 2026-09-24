#include "SocialAdmin.h"

#include <utility>

namespace nativesocial
{
namespace
{
void Wipe(std::string& value)
{
    volatile char* bytes = value.empty() ? nullptr : &value[0];
    for (std::size_t i = 0; i < value.size(); ++i)
        bytes[i] = 0;
    value.clear();
}

AdminResult Result(AdminResultCode code, std::uint32_t accountId, std::string message)
{
    return { code, accountId, std::move(message) };
}
}

AdminResult CreateManagedAccount(bool authorized, std::string const& accountName,
    std::string password, std::string displayName,
    std::uint32_t displayNameMinLength, std::uint32_t displayNameMaxLength,
    ProfilePreflightFn const& preflightProfile,
    AccountCreateFn const& createAccount, ProfileSetFn const& setProfile)
{
    if (!authorized)
    {
        Wipe(password);
        return Result(AdminResultCode::Unauthorized, 0, "Administrator authorization required");
    }
    if (accountName.empty())
    {
        Wipe(password);
        return Result(AdminResultCode::InvalidAccount, 0, "Account name is required");
    }
    if (password.empty())
    {
        Wipe(password);
        return Result(AdminResultCode::InvalidPassword, 0, "Password is required");
    }

    NameResult const nameResult = ValidateDisplayName(
        displayName, displayNameMinLength, displayNameMaxLength);
    if (nameResult != NameResult::Ok)
    {
        Wipe(password);
        return Result(nameResult == NameResult::AlreadyTaken
                ? AdminResultCode::DisplayNameTaken : AdminResultCode::InvalidDisplayName,
            0, "Display name " + NameResultToString(nameResult));
    }

    NameResult const available = preflightProfile(displayName);
    if (available != NameResult::Ok)
    {
        Wipe(password);
        return Result(available == NameResult::AlreadyTaken
                ? AdminResultCode::DisplayNameTaken : AdminResultCode::ProfileSetupFailed,
            0, "Display name " + NameResultToString(available));
    }

    AccountCreateResult const account = createAccount(accountName, password);
    Wipe(password);
    if (account.code != CoreAccountCreateResult::Ok)
    {
        if (account.code == CoreAccountCreateResult::NameAlreadyExists)
            return Result(AdminResultCode::AccountAlreadyExists, 0, "Account name already exists");
        if (account.code == CoreAccountCreateResult::NameTooLong ||
            account.code == CoreAccountCreateResult::InvalidInput)
            return Result(AdminResultCode::InvalidAccount, 0, "Account name is invalid");
        if (account.code == CoreAccountCreateResult::PasswordTooLong)
            return Result(AdminResultCode::InvalidPassword, 0, "Password is invalid");
        return Result(AdminResultCode::AccountCreateFailed, 0, "Authentication account creation failed");
    }

    NameResult const profile = setProfile(account.accountId, displayName);
    if (profile != NameResult::Ok)
    {
        AdminResultCode const code = profile == NameResult::AlreadyTaken
            ? AdminResultCode::DisplayNameTaken : AdminResultCode::ProfileSetupFailed;
        return Result(code, account.accountId,
            "Authentication account created, but Native Social profile setup failed; repair account id " +
            std::to_string(account.accountId));
    }
    return Result(AdminResultCode::Success, account.accountId,
        "Account and Native Social profile created");
}

char const* AdminResultCodeName(AdminResultCode code)
{
    switch (code)
    {
        case AdminResultCode::Success: return "SUCCESS";
        case AdminResultCode::Unauthorized: return "UNAUTHORIZED";
        case AdminResultCode::InvalidAccount: return "INVALID_ACCOUNT";
        case AdminResultCode::InvalidPassword: return "INVALID_PASSWORD";
        case AdminResultCode::InvalidDisplayName: return "INVALID_DISPLAY_NAME";
        case AdminResultCode::DisplayNameTaken: return "DISPLAY_NAME_TAKEN";
        case AdminResultCode::AccountAlreadyExists: return "ACCOUNT_EXISTS";
        case AdminResultCode::AccountCreateFailed: return "ACCOUNT_CREATE_FAILED";
        case AdminResultCode::ProfileSetupFailed: return "PROFILE_SETUP_FAILED";
    }
    return "UNKNOWN";
}
}
