import pathlib
import xml.etree.ElementTree as ET

root = pathlib.Path(__file__).resolve().parents[1]
framexml = root / "content/sources/assets/Interface/FrameXML"
friends_lua = (framexml / "FriendsFrame.lua").read_text()
native_lua = (framexml / "NativeSocial.lua").read_text()
xml_root = ET.parse(framexml / "FriendsFrame.xml").getroot()

names = {element.attrib.get("name") for element in xml_root.iter() if element.attrib.get("name")}
assert "FriendsFrameTab6" in names
assert "NativeSocialPlayersPanel" in names
assert "NativeSocialAdminButtonTemplate" in names
assert "$parentWhisper" in names
for child in {
    "$parentDirectoryButton", "$parentProfileButton", "$parentAdminButton",
    "$parentProfile", "$parentAdmin", "$parentAppearOffline",
}:
    assert child in names

navigation_buttons = {
    element.attrib["name"]: element
    for element in xml_root.iter()
    if element.attrib.get("name") in {
        "$parentDirectoryButton", "$parentProfileButton", "$parentAdminButton", "$parentRefresh"
    }
}
assert all(button.attrib.get("hidden") == "true" for button in navigation_buttons.values())

assert "PanelTemplates_SetNumTabs(self, 6)" in friends_lua
assert 'FriendsFrame.selectedTab == 6' in friends_lua
for command in {
    "DIR_LIST", "PROFILE_GET", "PROFILE_SAVE", "ADMIN_CAPS", "ADMIN_LIST",
    "ADMIN_SET_NAME",
    "WHISPER_RESOLVE", "WHISPER_TARGET",
}:
    assert f'"{command}"' in native_lua

assert "NativeSocialProfile_Save" in native_lua
assert "NativeSocialAdmin_SaveName" in native_lua
assert "NativeSocialPlayers_Whisper" in native_lua
assert "ChatFrame_SendTell(value)" in native_lua
assert "NativeSocial_RequestDirectory()" in native_lua
assert "NativeSocial_LayoutNavigation" in native_lua
assert 'currentView == "directory"' in native_lua
assert 'currentView == "profile"' in native_lua
assert 'currentView == "admin"' in native_lua
assert "NativeSocial_ProfileDraftIsDirty" in native_lua
assert "Unsaved profile edits are preserved until you return." in native_lua

layout = native_lua[native_lua.index("local function NativeSocial_LayoutNavigation"):
                    native_lua.index("local function NativeSocial_ProfileDraftIsDirty")]
assert "for index = #visible, 1, -1 do" in layout
assert 'button:SetPoint("TOPRIGHT", NativeSocialPlayersPanel, "TOPRIGHT", -45, -38)' in layout
assert 'button:SetPoint("RIGHT", buttonToRight, "LEFT", -4, 0)' in layout
assert "TOPLEFT" not in layout

# Lua string.gsub returns both the new string and a replacement count. The
# escaped name is the final ADMIN_SET_NAME argument, so returning gsub directly
# adds a seventh server-side field and triggers ErrorFields.
escape_body = native_lua[native_lua.index("local function NativeSocial_Escape"):
                         native_lua.index("local function NativeSocial_Unescape")]
assert "return string.gsub" not in escape_body
assert "return value" in escape_body
admin_save = native_lua[native_lua.index("function NativeSocialAdmin_SaveName") :]
assert 'NativeSocial_Send("adminSave", "ADMIN_SET_NAME", tostring(admin.selectedId), NativeSocial_Escape(displayName))' in admin_save

admin_response = native_lua[native_lua.index("local function NativeSocial_HandleAdmin"):
                             native_lua.index("local function NativeSocial_HandleResponse")]
success = admin_response[admin_response.index('if fields[4] == "SUCCESS" then'):
                         admin_response.index("else", admin_response.index('if fields[4] == "SUCCESS" then'))]
failure = admin_response[admin_response.index("else", admin_response.index('if fields[4] == "SUCCESS" then')):]
assert 'NativeSocialPlayersPanelAdminName:SetText("")' in success
assert "admin.afterSave = true" in success
assert 'NativeSocialPlayersPanelAdminName:SetText("")' not in failure
assert "admin.selectedId = nil" not in admin_response
assert "ADMIN_CREATE_ACCOUNT" not in native_lua
assert "password" not in native_lua.lower()

whisper_handler = native_lua[native_lua.index("local function NativeSocial_HandleWhisper"):
                             native_lua.index("local function NativeSocial_HandleAdmin")]
assert 'command ~= "WHISPER_TARGET"' in whisper_handler
assert "ChatFrame_SendTell(value)" in whisper_handler
assert "SendChatMessage" not in whisper_handler

whisper_action = native_lua[native_lua.index("function NativeSocialPlayers_Whisper"):
                            native_lua.index("function NativeSocialProfile_Save")]
assert 'NativeSocial_Send("whisper", "WHISPER_RESOLVE", tostring(accountId))' in whisper_action
assert "row.characterName" not in whisper_action

print("native Social FrameXML/UI static checks passed")
