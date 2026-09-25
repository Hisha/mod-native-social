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
assert "ADMIN_CREATE_ACCOUNT" not in native_lua
assert "password" not in native_lua.lower()

print("native Social FrameXML/UI static checks passed")
