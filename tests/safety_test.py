import pathlib

root = pathlib.Path(__file__).resolve().parents[1]
handler = (root / "src/NsocHandler.cpp").read_text()
service = (root / "src/SocialService.cpp").read_text()
directory = (root / "src/SocialDirectory.cpp").read_text()
profile = (root / "src/SocialProfile.cpp").read_text()
profile_store = (root / "src/SocialProfileStore.cpp").read_text()
client = (root / "content/sources/assets/Interface/FrameXML/NativeSocial.lua").read_text()

# Credential-bearing frames must not be logged or echoed.
assert "LOG_DEBUG" not in handler
assert 'message.assign(message.size(), \'\\0\')' in handler
assert "password" not in handler[handler.index("void SendAdminResult"):handler.index("bool NsocHandler::ParseRequest")]

# Every administrative mutation has a server-side authenticated-session gate.
assert "!SocialService::Instance().IsAuthorizedAdmin(session)" in handler
assert "!IsAuthorizedAdmin(session) || !IsEligibleHumanAccount(accountId)" in service
assert "RBAC_PERM_COMMAND_ACCOUNT_CREATE" in service

# Self-service identity is derived from the authenticated session. PROFILE_SAVE
# has no account-id parser or field, so adding one produces a field-count error.
self_save = handler[handler.index("cmd == command::ProfileSave"):handler.index("cmd == command::AdminCaps")]
assert "SaveOwnProfile(" in self_save
assert "ParseAccountId" not in self_save
own_save = service[service.index("NameResult SocialService::SaveOwnProfile"):service.index("bool SocialService::IsAccountOnline")]
assert "session->GetAccountId()" in own_save
assert "IsEligibleHumanAccount(accountId)" in own_save

# Public directory filtering derives the viewer from the authenticated session
# and does not alter the separately authorized Admin list.
public_directory = service[service.index("std::vector<DirectoryEntry> SocialService::BuildPublicDirectory"):
                           service.index("bool SocialService::IsAuthorizedAdmin")]
assert "viewer->GetAccountId()" in public_directory
assert "excludedAccounts.insert(viewer->GetAccountId())" in public_directory
admin_list = service[service.index("ListAdminProfiles"):service.index("NameResult SocialService::AdminSetDisplayName")]
assert "viewer" not in admin_list

# ADMIN_SET_NAME keeps both its handler and service authorization gates, and
# accepts exactly request id + account id + display name after the command.
admin_set = handler[handler.index("cmd == command::AdminSetName"):handler.index("cmd == command::AdminCreate")]
assert "fields.size() == 6" in admin_set
assert "IsAuthorizedAdmin(session)" in admin_set
assert "AdminSetDisplayName(" in admin_set

# The native UI deliberately exposes no credential or graphical account-create
# request. Existing server compatibility remains separate pending review.
assert "ADMIN_CREATE_ACCOUNT" not in client
assert "password" not in client.lower()

# Privacy is applied in the pure compiler and offline fields serialize empty.
assert "online && profile.appearOffline" in directory
assert "if (!entry.presence.online)" in directory
assert "SELECT appear_offline FROM native_social_account" in profile_store
assert profile_store.index("SELECT appear_offline FROM native_social_account") < profile_store.index("it->second.appearOffline = value")

# No authentication username fallback exists in display-name logic.
assert "username" not in profile.lower()

print("security/privacy static checks passed")
