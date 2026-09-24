# Native Players UI

The native UI is implemented in the schema-3 EPF, not as a user addon. It
shadows the stock build-12340 `FriendsFrame.lua` and `FriendsFrame.xml`, loads
the module-owned `NativeSocial.lua`, and adds a sixth **Players** tab while
preserving the existing Friends, Who, Guild, Chat, and Raid behavior.

The Players tab requests `DIR_LIST`, validates and reassembles entry parts, and
renders WotLK-style rows:

- online marker and public Display Name;
- character, level, localized race and class;
- faction and localized zone/instance;
- or an offline marker and `Offline` with no hidden character data.

The server supplies the final ordering. The client does not infer identity,
authorization, privacy, or bot state. A refresh button starts a new request;
stale request IDs and malformed frames are ignored or converted into a safe
panel error.

The protected files require the semantic `protected-framexml` client
capability declared by the EPF. The module contains no executable hashes,
offsets, patch recipes, or binary-generation logic. Portalkeeper owns how the
realm supplies that capability.

Graphical account administration is deliberately deferred. The authorized
NSOC create/list/set-name server operations and GM recovery commands are in
place first; a later management frame can consume them without changing the
account identity or security boundary.
