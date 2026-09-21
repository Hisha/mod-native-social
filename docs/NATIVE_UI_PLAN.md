# Native UI plan: the 3.3.5a social window

Status: investigation complete (this file), implementation deferred to a
later Native Social phase. This module ships the patch through
mod-content-manager; the package content is designed to live in `content/`
(no `*.epf` is shipped in Phase 1, see `content/README.md`).

## Investigation summary

The 3.3.5a social window has no separate `SocialFrame`. The window opened by
the Social button / `ToggleFriendsFrame()` is **`FriendsFrame`**, living in
`Interface/FrameXML/FriendsFrame.xml` and `FriendsFrame.lua`. Both files were
extracted from a European 3.3.5a client:

- Source MPQ: `Data/enUS/patch-enUS-2.MPQ` (files also present in
  `patch-enUS.MPQ`).
- Extracted copies (reference only): `FriendsFrame.xml` (~4448 lines),
  `FriendsFrame.lua` (~1517 lines).

### Tab anatomy (FriendsFrame.xml, lines ~4331-4436)

Tabs inherit the `FriendsFrameTabTemplate` (which inherits
`CharacterFrameTabButtonTemplate`) and are anchored in a LEFT chain:

| Tag | tab id | Text | Notes |
| --- | --- | --- | --- |
| `FriendsFrameTab1` | 1 | FRIENDS | anchored `BOTTOMLEFT` of FriendsFrame (x=11, y=46) |
| *(commented out)* | 2 | IGNORE | a blizzard-disabled IGNORE tab kept in comments between tab 1 and WHO |
| `FriendsFrameTab2` | 2 | WHO | reuses the name `FriendsFrameTab2`; anchors to tab1 |
| `FriendsFrameTab3` | 3 | GUILD | `OnClick` guards with `InGuildCheck()` |
| `FriendsFrameTab4` | 4 | CHAT | channels |
| `FriendsFrameTab5` | 5 | RAID | |

Consequences for a native tab:

- Tab ids and button names are **hard-coded** in the Lua (`tab == 1`,
  `tab == 3`, `PanelTemplates_GetSelectedTab`), not data-driven. Adding a tab
  means touching `FriendsFrame_Update()`, `ToggleFriendsFrame()`, the
  per-tab panels and the XML anchors, not just appending one button.
- The commented-out IGNORE block is the obvious seam: a new tab button can be
  inserted there and re-shaded with the same anchors, avoiding the need to
  re-anchor every later tab.

### Relevant Lua control flow

- `ToggleFriendsFrame(tab)` — toggles/hides, calls `PanelTemplates_SetTab`.
- `PanelTemplates_SetTab(FriendsFrame, tab)` — highlights the selected tab;
  the XML `OnClick` for each tab runs `PanelTemplates_Tab_OnClick(self,
  FriendsFrame)` then `FriendsFrame_Update()` (GUILD) or a registered update
  handler.
- `FriendsFrame_Update()` — drives which panel scroll frame is shown for the
  selected tab and refreshes friend/ignore/chat/raid lists.
- `FriendsFrame_OnLoad`/`OnShow`/`OnEvent` — hook tab bookkeeping, query
  friend status, and gate availability (e.g. guild tab hidden when
  `not IsInGuild()`).
- Selected-tab pattern: `PanelTemplates_GetSelectedTab(FriendsFrame)`.

## Approach for the Phase 2 "Players" tab

The package will shadow `Interface/FrameXML/FriendsFrame.lua` and
`FriendsFrame.xml` inside the Content Manager client patch (schema 1 raw
files), with the built patch archiving over the base MPQ files.

1. **XML**: add a `FriendsFrameTab2b`-style button (or renumber cleanly)
   labelled PLAYERS, inserted at the commented-IGNORE seam; add a
   `FriendsFramePlayersPanel` (scroll frame + input/editbox + buttons)
   anchored like the existing panel templates; wire `OnClick` to a
   `FriendsFramePlayers_*` update path.
2. **Lua**: open a per-account profile at login, seed it from the server
   (display name, appearing online/offline), load the online/offline lists,
   and drive messaging.
3. **Strings**: avoid shadowing `GlobalStrings.lua` (full-copy risk); use
   inline localized literals or a small module-owned string table committed
   in the patch.
4. **Protocol**: the native client talks to the worldserver without addons.
   Standard options to evaluate in Phase 2: a module-owned chat-message
   namespace (client → server via existing chat commands, server → client via
   SMSG channels or message chat), or a dedicated world opcode pair. The
   server surface already exposed by this module
   (`SocialService`) is designed so the protocol layer can call it directly.

Nothing in `content/` currently modifies `FriendsFrame`. Phase 1 deliberately
ships no client package (no fake content): the real package — produced from
the `content/` design and declared mandatory by the server — ships together
with the first real client functionality in Phase 2.
