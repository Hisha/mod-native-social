import pathlib

root = pathlib.Path(__file__).resolve().parents[1]
handler = (root / "src/NsocHandler.cpp").read_text()
service = (root / "src/SocialService.cpp").read_text()
directory = (root / "src/SocialDirectory.cpp").read_text()
profile = (root / "src/SocialProfile.cpp").read_text()

# Credential-bearing frames must not be logged or echoed.
assert "LOG_DEBUG" not in handler
assert 'message.assign(message.size(), \'\\0\')' in handler
assert "password" not in handler[handler.index("void SendAdminResult"):handler.index("bool NsocHandler::ParseRequest")]

# Every administrative mutation has a server-side authenticated-session gate.
assert "!SocialService::Instance().IsAuthorizedAdmin(session)" in handler
assert "!IsAuthorizedAdmin(session) || !IsEligibleHumanAccount(accountId)" in service
assert "RBAC_PERM_COMMAND_ACCOUNT_CREATE" in service

# Privacy is applied in the pure compiler and offline fields serialize empty.
assert "online && profile.appearOffline" in directory
assert "if (!entry.presence.online)" in directory

# No authentication username fallback exists in display-name logic.
assert "username" not in profile.lower()

print("security/privacy static checks passed")
