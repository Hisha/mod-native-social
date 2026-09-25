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
}:
    assert f'"{command}"' in native_lua

assert "NativeSocialProfile_Save" in native_lua
assert "NativeSocialAdmin_SaveName" in native_lua
assert "NativeSocial_RequestDirectory()" in native_lua
assert "NativeSocial_LayoutNavigation" in native_lua
assert 'currentView == "directory"' in native_lua
assert 'currentView == "profile"' in native_lua
assert 'currentView == "admin"' in native_lua
assert "NativeSocial_ProfileDraftIsDirty" in native_lua
assert "Unsaved profile edits are preserved until you return." in native_lua

# Lua string.gsub returns both the new string and a replacement count. The
# escaped name is the final ADMIN_SET_NAME argument, so returning gsub directly
# adds a seventh server-side field and triggers ErrorFields.
escape_body = native_lua[native_lua.index("local function NativeSocial_Escape"):
                         native_lua.index("local function NativeSocial_Unescape")]
assert "return string.gsub" not in escape_body
assert "return value" in escape_body
admin_save = native_lua[native_lua.index("function NativeSocialAdmin_SaveName") :]
assert 'NativeSocial_Send("adminSave", "ADMIN_SET_NAME", tostring(admin.selectedId), NativeSocial_Escape(displayName))' in admin_save
assert "ADMIN_CREATE_ACCOUNT" not in native_lua
assert "password" not in native_lua.lower()

print("native Social FrameXML/UI static checks passed")
