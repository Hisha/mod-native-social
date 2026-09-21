# Native Social content package — design (no artifact shipped yet)

This directory is the future home of the mod-native-social client-patch
package consumed by **mod-content-manager** (which auto-discovers every
`*.epf` under `<ContentManager.ModuleDirectory>/<module>/content/`, default
module directory `./modules`).

**Phase 1 intentionally ships no `*.epf`.** There is no real client UI yet,
so we do not produce a fake package merely to exercise the Content Manager
pipeline. The intended progression:

- **Phase 1** (now): account/profile/presence backend plus temporary
  `.social` developer commands. No client content, nothing required.
- **Phase 2+**: the actual Native Social client functionality (a "Players"
  social-network tab in the 3.3.5a `FriendsFrame`), shipped as a real package
  from this directory, and *declared mandatory* by the server so the module
  hard-fails if it is missing. See `docs/NATIVE_UI_PLAN.md` at the repository
  root for the client investigation and packaging approach.

## Package identity (when content ships)

| Field | Value |
| --- | --- |
| schema | 1 (raw files) |
| package | `mod-native-social` |
| target root | `Interface/FrameXML/` (shadow `FriendsFrame.lua`, `FriendsFrame.xml`) |

The server validates that the package is **ACTIVE and APPLIED for the current
realm** at startup through the mod-content-manager capability provider
(`ContentCapabilitiesV1::Provider::Resolve` in `src/SocialService.cpp`,
`ResolveRequiredContent()`). A row in `content_manager_package` alone is **not**
accepted as proof of activation — it only records that a package was
registered. Activation additionally requires a Content Manager build that is
`ACTIVE` for this realm and `APPLIED` on the worldserver, which is exactly what
the capability provider verifies. While `content::RequiredSocialContent()` is
empty no package is required and the module runs its backend, but the moment
the first real client functionality lands that function declares the package
and the same validation turns an absent, unactivated or realm-mismatched
package into a hard, clearly logged failure — there is deliberately no
configuurable "degraded" mode.

## Packaging rules (from mod-content-manager)

1. `*.epf` is a plain ZIP with `manifest.json` at the archive root (schema 1).
2. schema-1 `content` is a non-empty array of `{ "type": "file", "source": …,
   "target": … }` with safe, relative paths (no `../`, no absolute paths).
3. Package keys are lowercase and match `[a-z0-9._-]`.

## Installing on a realm (Phase 2+)

1. Keep `mod-native-social` in the build as a normal AzerothCore module.
2. Declare the package under `content::RequiredSocialContent()` in
   `src/SocialService.cpp`, including the vendor symbol the Content Manager
   must have ACTIVE/APPLIED for it (the declaration that the requirement
   actually consumes).
3. Build, install and activate the package with mod-content-manager (a build
   that is both `ACTIVE` for this realm and `APPLIED` on the worldserver).
4. Restart the worldserver; the startup log confirms the package state. If the
   required package is not ACTIVE/APPLIED for the current realm, startup fails
   clearly with the specific reason and `.social diag` (GM) reports it.