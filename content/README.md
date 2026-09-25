# Native Social content package

`mod-native-social.epf` is a ZIP-format mod-content-manager package built
byte-for-byte from `content/sources/`.

The manifest uses schema 3 and declares exactly one semantic client
requirement:

```json
"clientRequirements": ["protected-framexml"]
```

It installs three client files under `Interface/FrameXML/`: the patched stock
FriendsFrame Lua/XML and the module-owned NativeSocial Lua transport/UI. The
Players tab includes the public directory, self-service profile/privacy
controls, a stock-WoW Whisper action for currently advertised online characters,
and server-authorized administrator list/set-name controls. The action performs
authenticated click-time target resolution and then calls the stock whisper UI;
ordinary chat text is never carried by NSOC. It has no
graphical account-creation or credential field. This is a statement of required
capability only. No Portalkeeper recipe, executable hash, offset, generation,
or patch detail belongs in this repository.

Install the EPF through mod-content-manager, build and activate content, and
verify the build reports `Client requirements: protected-framexml`. The active
build is then published through mod-realm-config and consumed by Portalkeeper.
