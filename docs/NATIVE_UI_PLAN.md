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

The same panel includes a **My Profile** view. `PROFILE_GET` shows the current
account Display Name and Appear Offline state; `PROFILE_SAVE` sends only the
new name and privacy flag. The authenticated server session supplies the
account ID. Successful saves update the persistent row and in-memory store,
then refresh the directory without restarting worldserver.

Authorized administrators also see an **Admin** view backed by the existing
`ADMIN_CAPS`, `ADMIN_LIST`, and `ADMIN_SET_NAME` operations. It lists eligible
account IDs, including unconfigured profiles, and can assign a Display Name.
The button is hidden for ordinary players, while every operation independently
rechecks authorization on the server.

Top navigation is page-specific: Directory shows My Profile, optional Admin,
and Refresh; My Profile shows Directory and optional Admin; Admin shows
Directory and My Profile. Buttons are packed left-to-right at runtime so an
unauthorized player never gets an empty Admin gap. Refresh remains a
directory-only action. My Profile drafts are retained across navigation until
saved or replaced by a successful server response.

The server supplies the final ordering. The client does not infer identity,
authorization, privacy, or bot state. A refresh button starts a new request;
stale request IDs and malformed frames are ignored or converted into a safe
panel error. The server removes the authenticated viewer's account ID before
directory framing, while the administrator list remains unfiltered by viewer.

The protected files require the semantic `protected-framexml` client
capability declared by the EPF. The module contains no executable hashes,
offsets, patch recipes, or binary-generation logic. Portalkeeper owns how the
realm supplies that capability.

Graphical account creation remains deliberately deferred. Although the legacy
authorized server operation exists, the management frame contains no password
field and emits no credential-bearing request. A separate review and explicit
approval are required before adding credentials to the native UI transport.
