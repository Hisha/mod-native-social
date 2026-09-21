# mod-native-social

An AzerothCore module providing **account-level presence** — identity, online
state and privacy — built from the ground up with a hard dependency on
**mod-content-manager** for its native 3.3.5a client patch.

Presence works per **account**, not per character: account-wide when online
means any account, any realm character, any race/faction. This is the
foundation (Phase 1) for the account-level messaging system planned for later
phases.

## Non-negotiables

- **Hard dependency on mod-content-manager.** The module is compiled next to
  content-manager and discovers it at runtime through its
  `ContentCapabilitiesV1::Provider` world-script ABI (vendored in
  `src/api/ContentCapabilityApiV1.h`; same convention as the other native
  modules). There is **no** unpatched-client or addon fallback: presence,
  messaging and privacy are native-client features.
- **No supported degraded/unpatched mode.** When mod-content-manager is
  unavailable or required native client content is unavailable/invalid, the
  module **fails clearly**: it logs the specific reason at startup, surfaces
  it to operators and Game Masters (`.social diag` stays available), and
  refuses to pretend it can serve its functionality through a command-only
  substitute. The temporary `.social` commands are development/test tooling
  while the native UI is under construction — they are **not** a supported
  frontend, and Phase 2 hard-fails on missing client content rather than
  degrading to them.
- **No placeholder client packages.** Phase 1 deliberately ships no `*.epf`:
  there is no real client UI content yet, so we do not produce a fake package
  to exercise the Content Manager pipeline. `content/` documents the intended
  package design; the real package lands with the first client functionality
  in Phase 2 and is then declared mandatory by the server.
- No Battle.net / Real ID terminology in user-facing or operational artifacts.

## What this phase implements

- **Account display names** — unique per realm, case-insensitive, length
  validated (`3..24` by default), stored in the auth database
  (`native_social_account`). Identity in the game comes from the account, not
  from the current character.
- **Presence** — derived live from connected sessions (never persisted as a
  boolean; an account is "online" while it holds at least one confirmed
  human session). Presence state auto-rebuilds from login events after a
  restart. Social presence appears only through the display name.
- **Appear Offline** — a per-account privacy flag. Others only ever see the
  advertised state; your own view and Game Master diagnostics still see the
  true presence.
- **Playerbot exclusion** — bot sessions (mod-playerbots) never count as
  account presence. Identity is confirmed with a short settling delay so bot
  AI attach timing cannot distort it. (See "Playerbots" below.)
- **Content Manager integration** — a hard dependency: if the capability
  provider is missing the module fails clearly. Required client content is
  verified against `content_manager_package` (world DB) via
  `content::RequiredSocialContent()`; while that set is empty (Phase 1,
  no client content yet) the backend runs, and the moment real content is
  declared the same check turns an absent/invalid package into a hard failure.
- **Temporary developer commands** — `.social name/online/offline/status/list`
  (plus `.social diag` for Game Masters). These are dev/test interfaces used
  to exercise the backend while the native UI is under construction; they are
  NOT a supported frontend. The Phase 2 client UI will use these same
  `SocialService` facilities.

## Not implemented yet (planned)

- Account-level messaging, offline queue, unread indicators.
- Friends / following, block & ignore, notifications, permissions.
- The native client "Players" social tab (design + 3.3.5a client
  investigation: `docs/NATIVE_UI_PLAN.md`).
- Multi-realm shared-auth optimizations (see "Limitations").

## Architecture

```
src/
  api/ContentCapabilityApiV1.h   vendored ABI (presence detection only)
  SocialProfile.h/.cpp           profile struct, name validation, UTF-8 checks
  SocialProfileStore.h/.cpp      auth persistence + in-memory index
  SocialPresence.h/.cpp          live account presence + Playerbot exclusion
  SocialService.h/.cpp           facade: integration probe, diagnostics, privacy queries
  NativeSocialModule.cpp         world/player/command scripts + config
  mod_native_social_loader.cpp   module loader (Addmod_native_socialScripts)
conf/mod_native_social.conf.dist config template
data/sql/db-auth/base/          auth SQL (auto-applied by the core updater)
content/                        client content package design (docs only in Phase 1)
```

`SocialService` is the single public surface the Phase 2 client protocol will
talk to. Everything else stays behind it.

### Presence model

An `OnPlayerLogin` event only *queues* a session for classification. A short
settling period (1 s pacing, plus an immediate flush before any presence
query) lets the session fully attach (including any Playerbot AI) before the
session is confirmed. Logout removes the session regardless of its
classification state. Because presence is derived, there is nothing to
"reset" — after a restart every session simply re-queues on login.

## Requirements

- AzerothCore (wotlk) with this module under `modules/`.
- **mod-content-manager** as a sibling module (REQUIRED at build and runtime).
- mod-playerbots — optional; the module compiles with or without it.
- The auth SQL below (`data/sql/db-auth/base`) — the core applies it
  automatically on the next startup.

## Install

1. Clone into `modules/mod-native-social` and build AzerothCore normally
   (static or `MODULES=default`). No CMake file is required; the module is a
   standard script module and the loader is generated as
   `Addmod_native_socialScripts()`.
2. Copy `conf/mod_native_social.conf.dist` to your config dir as
   `mod_native_social.conf` and adjust as needed.
3. Apply the auth SQL (or let the core updater do it). The updater picks up
   `data/sql/db-auth/base/` automatically.
4. Restart the worldserver. The startup log reports the profile count, presence
   and the client-content requirement state.

Verification: `.social diag` in-game (Game Master) prints provider presence,
required-content state and counters.

Phase 1 ships **no** client package (`content/` is design-only until real
client functionality exists). From Phase 2 onward, install the package with
mod-content-manager (see `content/README.md`); a missing required package then
fails the module clearly rather than degrading to the developer commands.

## Configuration (`mod_native_social.conf`)

| Option | Default | Meaning |
| --- | --- | --- |
| `NativeSocial.Enable` | `1` | master switch |
| `NativeSocial.DisplayNameMinLength` | `3` | min display-name length |
| `NativeSocial.DisplayNameMaxLength` | `24` | max length (hard cap 48) |

## Commands

| Command | Access | Meaning |
| --- | --- | --- |
| `.social name <name>` | all | set your account display name |
| `.social online` | all | clear appear-offline (you are visible) |
| `.social offline` | all | appear offline to other players |
| `.social status [<name>]` | all | your own profile, or another account's advertised presence |
| `.social list` | all | advertised-online accounts with display names |
| `.social diag` | GM | module/provider/package/presence diagnostics |

Privacy rule: `status`/`list` only ever show the **advertised** state. The
flagged-away state of an online, appear-offline account is indistinguishable
from being offline.

## Playerbots

When built with mod-playerbots, sessions are classified as bots via their
`WorldSession::IsBot()` flag (mod-playerbots only runs against the Playerbot
fork branch of AzerothCore, which provides that flag) with the
`GET_PLAYERBOT_AI` AI-registry lookup as a defensive secondary check, both
compiled in under `#if __has_include("Playerbots.h")`. Account-owned and
random-bot sessions never contribute to account presence. This works even if
our `OnPlayerLogin` runs before Playerbots finishes attaching a `PlayerbotAI`,
because the session itself is flagged at construction. Without mod-playerbots
the guard is compiled out and every real session counts.

## Database (`auth`)

`native_social_account` — one row per account that has adopted a display name.

- `account_id` (PK), `display_name`, `display_name_key` (unique,
  case-insensitive lookup key), `appear_offline`, `created_at`, `updated_at`.
- Re-runnable SQL; safe for the core updater on every boot.
- Rows are never deleted by the module; presence is derived, so restarting
  does not clear anyone's profile.

## Limitations

- **Display-name uniqueness scope**: `display_name_key` is a `UNIQUE` column
  in the shared auth database. It is unique across every account in every
  realm that mounts that auth DB, not just this realm's players. The module
  treats the database unique constraint as authoritative: uniqueness is
  re-checked against the DB (not just the local in-memory index) before a
  write, and the result of a write is verified by reading back what actually
  committed before the local state is updated. Two realms sharing one auth DB
  *can* race on the same name; the unique key resolves the race deterministically
  (one writer owns the name, the other gets a clear "name taken" error). This
  is not a concern on a single-realm server.
- **Non-ASCII names**: case-insensitivity folds only ASCII; two names
  differing solely by non-ASCII case are treated as distinct. Validated as
  UTF-8 and stored in the utf8mb4 column (display width = code points), but
  the 3.3.5a client renders these best-effort.
- **Compiled verify**: this repository was developed and reviewed in an
  environment without a local AzerothCore build tree; API usage mirrors the
  current AzerothCore master (hook registration, `ConfigValueCache`,
  `Acore::ChatCommands`, DB pools, module SQL updater paths). A real compile
  on the target tree is still expected as part of deployment.

## License

Apache-2.0 (see `LICENSE`). The vendored
`src/api/ContentCapabilityApiV1.h` is owned by the Content Manager project and
is reproduced verbatim for ABI discovery.