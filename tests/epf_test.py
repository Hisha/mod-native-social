import json
import pathlib
import zipfile

root = pathlib.Path(__file__).resolve().parents[1]
source = json.loads((root / "content/sources/manifest.json").read_text())
assert source["schema"] == 3
assert source["version"] == "2.2.0"
assert source["clientRequirements"] == ["protected-framexml"]
targets = {item["target"] for item in source["content"]}
assert targets == {
    "Interface/FrameXML/FriendsFrame.lua",
    "Interface/FrameXML/FriendsFrame.xml",
    "Interface/FrameXML/NativeSocial.lua",
}

with zipfile.ZipFile(root / "content/mod-native-social.epf") as archive:
    packaged = json.loads(archive.read("manifest.json"))
    assert packaged == source
    names = set(archive.namelist())
    assert names == {"manifest.json"} | {item["source"] for item in source["content"]}
    for item in source["content"]:
        expected = (root / "content/sources" / item["source"]).read_bytes()
        assert archive.read(item["source"]) == expected

print("schema-3 EPF tests passed")
