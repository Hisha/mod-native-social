# mod-native-social

Native Social adds an account-level Players directory to AzerothCore WotLK
3.3.5a. It is not Battle.net or Real ID emulation. The stable internal identity
is the AzerothCore account ID; the public identity is a separate Native Social
Display Name. Authentication usernames are never public directory data.

## Players directory

The public directory is compiled from configured Native Social profiles, not
from online `Player` objects. A configured human account therefore remains in
the list while offline. Accounts without a Display Name are excluded from the
ordinary list but remain visible by account ID to authorized administrators.

At request time, the server enriches each visible online account with its
active character name, level, race ID, class ID, faction, and localized zone or
instance name. This data is transient and is never stored in the Native Social
table. If one account has multiple simultaneous human sessions, the character
with the lowest GUID is selected, giving a stable deterministic policy.

Ordering is online first and then offline, alphabetically by the
case-insensitive Display Name key within each group, with account ID as the
last tie breaker.

## Privacy

`appear_offline` is persistent and server-authoritative:

- no session: offline entry;
- live session plus Appear Offline: the same offline entry;
- live session and visible: online entry with transient character presence.

The directory compiler discards live presence for Appear Offline profiles
before protocol serialization. Ordinary clients receive no hidden character,
level, race, class, faction, zone, or instance fields to conceal in Lua.

## Display Names

Display Names default to 3–24 Unicode code points (hard maximum 48), are
trimmed, must be valid UTF-8, and reject control characters plus WoW chat
format characters `|` and `%`. ASCII case is folded for uniqueness, so `Kevin`
and `kevin` conflict. The auth database unique key is authoritative across all
realms sharing that database. Login usernames are never used as a fallback.

Players retain the existing `.social name` development/recovery path. An
administrator can assign or change any eligible account's name with
`.social admin name <accountId> <Display Name>`.

The native Players panel also provides **My Profile** controls for changing the
authenticated account's Display Name and Appear Offline flag. The request never
contains a client-selected account ID. Successful changes synchronously update
the auth database and the running worldserver's profile store, then refresh the
directory.

## Human-account filtering

Offline random-bot accounts are excluded by authentication-username prefix
(`NativeSocial.PlayerbotAccountPrefix`, default `rndbot`). Standalone service
or bot accounts are excluded with the comma-separated
`NativeSocial.ExcludedAccounts` setting (default `AHBOT`). Explicit entries are
trimmed, empty entries are ignored, and both checks are case-insensitive. An
account matching either rule is not eligible for Native Social.

This filtering runs only on the server. Authentication usernames are used to
derive excluded account IDs and are never sent through the public Players
directory. The module does not link against Playerbot. When Playerbot headers
are present, live sessions are also classified through `WorldSession::IsBot()`
and the AI registry, so prefixed accounts remain excluded whether online or
offline while other observed bot sessions are excluded at runtime.

## Administration and security

NSOC provides authorized operations to list eligible account IDs/profile
state, assign/change a Display Name, and create an AzerothCore account plus its
Native Social profile as one administrative workflow. Account creation calls
`AccountMgr::CreateAccount`, the same core service used by `.account create`;
it does not synthesize a chat command.

Every request is authorized against the authenticated `WorldSession` at the
server. Administrative access requires `SEC_ADMINISTRATOR`; creation also
requires `RBAC_PERM_COMMAND_ACCOUNT_CREATE`. Client flags and UI visibility
are ignored. Passwords are never logged, stored by Native Social, or returned.

Validation failure creates nothing. Core account failure creates no profile.
If core creation succeeds but profile setup fails, the result explicitly
reports partial failure and returns the account ID for repair; the module does
not attempt compensating deletion.

Headless recovery commands:

| Command | Access | Purpose |
| --- | --- | --- |
| `.social admin list` | administrator | list human account IDs and profile state |
| `.social admin name <id> <name>` | administrator | assign/change Display Name |
| `.social online` | player | clear Appear Offline |
| `.social offline` | player | enable Appear Offline |
| `.social status [name]` | player | inspect advertised state |
| `.social diag` | GM | integration/database/presence diagnostics |

The Players panel exposes authorized account listing and Display Name
assignment. Graphical account creation remains deferred because it would add
credential entry and transport; the client contains no password field and
sends no account-creation request.

## Client package and integration

`content/mod-native-social.epf` is schema 3 and declares exactly
`protected-framexml`. It ships patched `FriendsFrame.lua`, `FriendsFrame.xml`,
and `NativeSocial.lua`, adding a functional Players tab to the WotLK Social
window. The client localizes race/class IDs using its own strings while the
server localizes location for the requesting session.

The semantic flow is:

```text
module EPF -> mod-content-manager build requirements -> mod-realm-config
realm.conf -> Portalkeeper -> capability-appropriate realm executable
```

Native Social knows only the semantic requirement. It contains no executable
hashes, offsets, recipe IDs, generations, or binary patch details.

## NSOC transport

NSOC extends the existing addon-message whisper transport; there is no custom
opcode. `DIR_LIST` returns offline and online account entries using deterministic
entry/part framing below the 254-byte limit. Legacy online-only `LIST` remains
for compatibility. See [docs/NSOC_PROTOCOL.md](docs/NSOC_PROTOCOL.md).

## Database

Persistent data remains in the auth database table `native_social_account`:
account ID, Display Name/key, Appear Offline, and timestamps. The existing
convergent `CREATE TABLE IF NOT EXISTS` base migration is sufficient for this
phase; no presence or duplicated character/account data was added.

## Build and test

Install under AzerothCore `modules/mod-native-social`, apply the auth SQL, copy
the config, and build normally. mod-content-manager is required by the existing
module integration; Playerbot is optional.

Standalone domain/package tests:

```bash
bash tests/run_standalone.sh
```

These cover validation, directory membership/order, configured account filtering,
Appear Offline suppression, framing/chunking, administrative denial/failure/
partial-success paths, and the schema-3 EPF. A compile against the target
AzerothCore/Playerbot fork and live behavior remain PTR acceptance items.

## Deferred

Favorites, blocking, direct messages, offline messages, unread queues,
cross-realm social, and a graphical admin form are intentionally deferred.
