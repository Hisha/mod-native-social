# mod-native-social

**Native Social** adds an account-level **Players directory** to AzerothCore World of Warcraft: Wrath of the Lich King 3.3.5a.

It is designed to help real players find one another on a realm that may also contain large numbers of Playerbots. Players can see which configured human accounts are online, find their currently active characters, and start an ordinary WoW whisper directly from the Social window.

Native Social is **not** Battle.net or Real ID emulation. Its stable internal identity is the AzerothCore account ID, while its public identity is a separate **Native Social Display Name**. Authentication usernames are never exposed as public directory data.

Native Social also provides self-service profile and privacy controls, an authorized administrator interface, and headless recovery commands.

## Requirements

**Native Social requires the complete client-content delivery pipeline to function as intended. Installing the server module alone is not sufficient.**

| Component | Requirement | Purpose |
| --- | --- | --- |
| AzerothCore WotLK 3.3.5a | Required | Hosts the Native Social server module. |
| [mod-content-manager](https://github.com/Hisha/mod-content-manager) | Required | Builds and activates the Native Social client patch from its EPF package. |
| [mod-realm-config](https://github.com/Hisha/mod-realm-config) | Required | Publishes the realm configuration, including the active client patch and its capability requirements. |
| [Portalkeeper](https://github.com/Hisha/Portalkeeper) | Required for players | Manages the realm's client content and selects a compatible realm executable. |
| Playerbot | Optional | Native Social can run without Playerbot. When Playerbot is present, the module also uses available runtime bot-detection facilities. |

The Native Social EPF declares the `protected-framexml` capability requirement. The native Players interface depends on the compatible client executable and patched FrameXML delivered through this pipeline.

A stock, unmodified WoW 3.3.5a client does not provide the complete Native Social interface.

### For server administrators

Install and configure **mod-native-social**, **mod-content-manager**, and **mod-realm-config** on the target realm. Build and activate the Native Social client patch, then verify that the published realm configuration advertises the active patch and its required client capability.

Do not assume that a successfully loaded Native Social server module means the client interface is ready. The client-content build, activation, publication, and installation steps must also be completed.

### For players

Use **Portalkeeper** to enter the realm and install its required client content. Portalkeeper handles the compatible realm executable and the files needed for the native Players interface.

Do not install the Native Social EPF as a conventional WoW addon or manually replace the stock WoW executable.

## Player guide

### Open the Players directory

Open WoW's **Social** window and select the **Players** tab.

The directory lists configured, eligible human accounts by their Native Social Display Names. Online accounts appear first, followed by offline accounts. Within each group, entries are sorted alphabetically by Display Name.

Your own account does not appear in your public Players directory. Use **My Profile** to view or change your own Native Social settings.

### Find an online player

An online directory entry can show the account's currently advertised character name, level, race, class, faction, and location.

This information describes the character currently selected for that account's Native Social presence. It is not a permanent association between the account and that character.

If an account has multiple simultaneous human sessions, Native Social selects the character with the lowest GUID to provide a deterministic advertised character.

### Start a whisper

Online directory entries include a **Whisper** action.

Clicking Whisper asks the server to revalidate the selected account and resolve its currently advertised character. On success, Native Social opens an ordinary WoW whisper addressed to that character.

From that point onward, **WoW's existing chat system handles the conversation**. Sending, receiving, `/r`, chat history, colors, sounds, ignore handling, throttling, and delivery retain normal WoW whisper behavior.

Whispers are addressed to **characters, not accounts**. If someone switches characters, refresh the directory and start a new whisper to their newly advertised character.

Native Social does not provide account-to-account chat or offline messaging in this release.

### Manage your profile

Select **My Profile** to change your Native Social Display Name or your **Appear Offline** setting.

Successful changes update both the auth database and the running worldserver's profile store. The directory then refreshes to reflect the new state.

Unsaved profile edits are preserved when navigating away from My Profile and returning.

### Appear Offline

Appear Offline hides your online presence **within Native Social**. Other players see your directory entry as offline and cannot use Native Social's Whisper action to resolve your active character.

Appear Offline does **not** make your character invisible throughout WoW. It does not alter ordinary in-game encounters, existing whispers, guild functionality, or other stock WoW systems.

You can change this setting through My Profile or the `.social online` and `.social offline` commands.

## Players directory

The public directory is compiled from configured Native Social profiles, **not** directly from online `Player` objects. A configured human account therefore remains listed while offline.

Accounts without a Display Name are excluded from the ordinary directory but remain visible by account ID to authorized administrators.

At request time, the server enriches each visible online account with its active character name, level, race ID, class ID, faction, and localized zone or instance name. This information is transient and is never stored in the Native Social account table.

Directory ordering is:

1. Online accounts, followed by offline accounts.
2. Alphabetical order by case-insensitive Display Name key within each group.
3. Account ID as the final tie breaker.

The authenticated viewer's own account ID is excluded from the public directory. This does not affect the authorized administrator account list, which includes the administrator's own account.

### Whisper target resolution

The Whisper action does not trust a character name cached in an earlier directory response.

When a player clicks Whisper, an authenticated NSOC `WHISPER_RESOLVE` request asks the server to revalidate the selected account and resolve its currently advertised character.

The server rejects resolution when the target is no longer eligible or visible. Offline accounts and accounts using Appear Offline expose neither a Whisper action nor a whisper target through Native Social.

On successful resolution, the client calls WoW's stock `ChatFrame_SendTell`. Ordinary whisper text does **not** pass through Native Social's NSOC transport.

## Privacy

The persistent `appear_offline` setting is **server-authoritative**:

| Account state | Native Social directory behavior |
| --- | --- |
| No live session | Offline entry |
| Live session with Appear Offline enabled | The same offline entry |
| Live session with visible presence | Online entry with transient character presence |

The directory compiler discards live presence for Appear Offline profiles **before protocol serialization**.

Ordinary clients do not receive hidden character names, levels, race or class IDs, factions, zones, or instance fields that would then need to be concealed in Lua.

Whisper-target resolution is separately revalidated on the server. Hiding the client button is not the security boundary.

Authentication usernames are not public directory data and are never used as fallback Display Names.

## Display Names

Display Names have the following rules:

- Default length: **3–24 Unicode code points**, with a hard maximum of 48.
- Leading and trailing whitespace is removed.
- Names must be valid UTF-8.
- Control characters and the WoW chat-format characters `|` and `%` are rejected.
- ASCII case is folded for uniqueness: `Kevin` and `kevin` conflict.

The auth database's unique key is authoritative across all realms sharing that database.

Players can change their own Display Name through **My Profile**. The self-service request never contains a client-selected account ID; the server derives the account from the authenticated session.

The `.social name` command remains available as a development and recovery path. Administrators can assign or change an eligible account's Display Name with:

```text
.social admin name <accountId> <Display Name>
```

## Human-account filtering

Native Social is intended to show **human accounts**, not the realm's Playerbot population.

Offline random-bot accounts are excluded using the authentication-username prefix configured by:

```ini
NativeSocial.PlayerbotAccountPrefix = rndbot
```

Standalone service or bot accounts can be excluded with the comma-separated setting:

```ini
NativeSocial.ExcludedAccounts = AHBOT
```

The values above are the documented defaults. Administrators should review them against their own account naming and service-account setup.

Explicit exclusions are trimmed, empty entries are ignored, and both checks are case-insensitive. An account matching either exclusion rule is ineligible for Native Social.

Filtering is performed on the server. Authentication usernames are used to derive excluded account IDs and are never sent through the public Players directory.

The module does not require a link-time dependency on Playerbot. When Playerbot headers are available, live sessions are also classified using `WorldSession::IsBot()` and the AI registry. Prefixed accounts remain excluded whether online or offline, while other observed bot sessions can be excluded at runtime.

## Administration and security

Native Social provides authorized operations to:

- List eligible account IDs and profile state.
- Assign or change an eligible account's Display Name.
- Create an AzerothCore account and its Native Social profile as one administrative workflow.

Account creation calls `AccountMgr::CreateAccount`, the same core service used by `.account create`; Native Social does not synthesize a chat command to create an account.

Every NSOC request is authorized against the authenticated `WorldSession` on the server. Client-side flags and UI visibility are not trusted for authorization.

Administrative access requires `SEC_ADMINISTRATOR`. Account creation additionally requires `RBAC_PERM_COMMAND_ACCOUNT_CREATE`.

Passwords are never logged, stored by Native Social, or returned to the client.

### Account-creation failure handling

Validation failure creates nothing. If core account creation fails, no Native Social profile is created.

If core account creation succeeds but profile setup fails, the result explicitly reports **partial failure** and returns the account ID for administrator repair. Native Social does not attempt compensating account deletion.

### Admin interface

The native Social panel provides authorized administrators with account listing and Display Name assignment.

Graphical account creation is intentionally deferred because it would require credential entry and transport. The current client has no password field and sends no account-creation request.

### Headless commands

| Command | Access | Purpose |
| --- | --- | --- |
| `.social admin list` | Administrator | List human account IDs and profile state |
| `.social admin name <id> <name>` | Administrator | Assign or change a Display Name |
| `.social online` | Player | Clear Appear Offline |
| `.social offline` | Player | Enable Appear Offline |
| `.social status [name]` | Player | Inspect advertised state |
| `.social diag` | GM | Integration, database, and presence diagnostics |

### Direct database changes

Changes made through the Native Social UI or commands update the database and running profile store.

**Direct SQL edits to `native_social_account` do not automatically refresh the running worldserver's cached profiles.** If an administrator modifies profile records directly in the database, restart the affected worldserver before expecting those changes to appear in Native Social.

Prefer the supported UI and commands for routine profile administration.

## Installation and deployment

The following is the deployment sequence for a realm using the required content-delivery pipeline. Follow the installation and configuration instructions in each dependency's own repository for its specific build commands and configuration options.

### 1. Install the server modules

Install the required modules in the target AzerothCore checkout:

```text
modules/
├── mod-content-manager/
├── mod-realm-config/
└── mod-native-social/
```

Configure mod-content-manager and mod-realm-config for the target realm.

Playerbot is optional. If it is installed, review Native Social's account-filtering settings to ensure that the realm's bot and service accounts are excluded.

### 2. Apply the Native Social auth migration

Apply the Native Social auth-database SQL supplied with the module:

```text
data/sql/db-auth/base/001_native_social_account.sql
```

The existing convergent `CREATE TABLE IF NOT EXISTS` base migration is sufficient for this release. No message-storage, persistent-presence, or duplicated character/account tables are required.

**Back up the target realm's auth database before deployment.** Do not replace a live realm's auth database with a PTR copy.

### 3. Configure and build AzerothCore

Install the Native Social module configuration and review its settings, particularly:

```ini
NativeSocial.PlayerbotAccountPrefix = rndbot
NativeSocial.ExcludedAccounts = AHBOT
```

Build AzerothCore with the required modules using the build procedure appropriate for the target checkout.

Start the worldserver and check its logs for Native Social initialization, database, and integration errors.

### 4. Build and activate the client patch

The Native Social EPF is located at:

```text
content/mod-native-social.epf
```

Use **mod-content-manager** to build and activate the Native Social client content for the target realm.

The EPF uses schema 3 and declares the `protected-framexml` capability. Do not bypass that requirement or treat the EPF as a conventional addon archive.

### 5. Publish the realm configuration

Verify that **mod-realm-config** publishes the target realm's active client-content build and its required capability.

Native Social describes its semantic client requirement; it does not own executable hashes, binary offsets, recipe IDs, or patch-generation details. Those remain responsibilities of the content-delivery pipeline.

### 6. Install through Portalkeeper

Use **Portalkeeper** with the target realm's published configuration.

Verify that Portalkeeper obtains the required content, selects a compatible realm executable, and allows the player to enter the realm with the native Players interface available.

Do not assume that a successful worldserver startup validates client patch installation.

### 7. Perform an in-game acceptance test

Using two real player accounts, verify that:

- Both configured accounts appear in one another's Players directory.
- Online and offline states update correctly.
- Whisper opens a stock WoW whisper to the target's currently advertised character.
- Normal WoW replies work.
- After a character switch and directory refresh, Whisper resolves the new character.
- Appear Offline conceals the account's live Native Social presence and prevents whisper-target resolution.
- My Profile updates the Display Name and Appear Offline setting.
- Authorized Admin controls work, while ordinary players cannot invoke administrator operations.
- Playerbots and configured excluded accounts do not appear as eligible players.

For a live Eitrigg rollout, build and publish **Eitrigg's own** client-content configuration. Do not copy PTR's entire auth database or published realm configuration into production.

## Client package and integration

`content/mod-native-social.epf` is a schema-3 EPF declaring exactly the `protected-framexml` capability.

It ships patched:

```text
Interface/FrameXML/FriendsFrame.lua
Interface/FrameXML/FriendsFrame.xml
Interface/FrameXML/NativeSocial.lua
```

These files add the functional **Players** tab to the WotLK Social window.

The client localizes race and class IDs using its own strings. The server localizes location for the requesting session.

The content-delivery flow is:

```text
Native Social EPF
        |
        v
mod-content-manager
  build and activation
        |
        v
mod-realm-config
  published realm.conf
        |
        v
Portalkeeper
  content installation and
  compatible realm executable
        |
        v
Native Social Players interface
```

Native Social declares the semantic requirement and does not contain executable hashes, offsets, recipe IDs, generations, or binary-patch implementation details.

## NSOC transport

NSOC extends the existing addon-message whisper transport. It does **not** introduce a custom opcode.

`DIR_LIST` returns offline and online account entries using deterministic entry/part framing below the 254-byte transport limit.

`WHISPER_RESOLVE` performs authenticated, click-time resolution of the selected account's currently advertised character. Ordinary whisper text is handled by WoW's stock chat system and never uses NSOC.

The legacy online-only `LIST` operation remains available for compatibility.

See [docs/NSOC_PROTOCOL.md](docs/NSOC_PROTOCOL.md) for protocol details.

## Database

Native Social's persistent data resides in the auth database table:

```text
native_social_account
```

The table stores account ID, Display Name and its uniqueness key, Appear Offline, and timestamps.

Live character presence is derived at request time and is not duplicated in the Native Social table.

The auth-database Display Name uniqueness rule applies across realms that share the same auth database.

No direct-message, conversation-history, or offline-inbox schema is included in this release.

## Build and test

Install the module under AzerothCore's `modules/mod-native-social` directory and complete the requirements and deployment steps above.

Run the standalone domain and package tests with:

```bash
bash tests/run_standalone.sh
```

The standalone tests cover validation, directory membership and ordering, configured account filtering, Appear Offline suppression, secure whisper-target resolution, framing and chunking, administrative denial and failure paths, partial account-creation success, and the schema-3 EPF.

Standalone test success does **not** replace a compile against the target AzerothCore checkout or an in-game acceptance test with the actual published client patch.

## Deferred features

The following features are intentionally outside the current release:

- Favorites.
- Account-level direct messaging.
- Offline messages and unread-message queues.
- Cross-realm social features.
- Graphical administrator account creation.

Native Social currently uses **ordinary WoW whispers** for player-to-player messaging. Additional messaging features can be considered separately if the realm's players need them.