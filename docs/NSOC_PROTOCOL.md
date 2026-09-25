# NSOC protocol 01

NSOC uses the existing WoW 3.3.5a addon-message whisper path. It does not add
an opcode. The addon prefix is `NSOC`; the payload is tab-delimited and begins
with `01`. AzerothCore's chat hook observes the combined
`NSOC\t01\t...` frame. Frames are limited to 254 bytes.

Text fields escape `\\` as `\\\\` and a tab as `\\t`. Request IDs contain
1–16 ASCII alphanumeric characters. Malformed, oversized, unknown, or
wrong-field-count requests are consumed safely and never mutate state.

## Public directory

Request:

```text
NSOC  01  DIR_LIST  requestId
```

(`  ` represents a tab throughout this document.) The response is:

```text
NSOC  01  DIR_START  requestId  entryCount
NSOC  01  DIR_ENTRY  requestId  entryIndex  partIndex  ...fields
NSOC  01  DIR_END    requestId
```

Part zero fields are:

```text
online(0|1), accountId, escapedDisplayName, [presence fields...]
```

Continuation parts contain remaining presence fields. Parts and entries start
at zero and are sent deterministically. Online presence has exactly six fields:

```text
escapedCharacterName, level, raceId, classId, faction(A|H), escapedLocation
```

Race and class are DBC IDs localized by FrameXML. Location is the requester's
localized zone name, or the localized map name inside an instance. An offline
entry has no presence fields. An Appear Offline account is serialized in that
same offline form—the live character name, level, race, class, faction, and
location never enter the response.

Entries are online first, then offline, with case-insensitive display-name
ordering and account ID as the final tie breaker. Only configured human
profiles appear. Accounts rejected by the centralized server-side eligibility
policy (the Playerbot prefix or `NativeSocial.ExcludedAccounts`) never appear.
The authenticated viewer's account ID is also excluded server-side before the
entry count and chunked responses are generated. Authentication usernames
never occur in this response.

The earlier `LIST` / `LIST_START` / `LIST_PROFILE` / `LIST_END` exchange is
retained as a compatibility surface and still returns visible online display
names only. New clients use `DIR_LIST`.

## Own profile

Self-service requests never contain an account ID. The server derives identity
only from the authenticated `WorldSession`, repeats the human-account
eligibility check, and then uses the same profile store as the `.social`
commands and administrator operations.

```text
PROFILE_GET requestId
  -> PROFILE_RESULT requestId configured(0|1) escapedDisplayName appearOffline(0|1)

PROFILE_SAVE requestId escapedDisplayName appearOffline(0|1)
  -> PROFILE_SAVE_RESULT requestId nameResult escapedMessage
       configured(0|1) escapedDisplayName appearOffline(0|1)
```

`PROFILE_SAVE` validates and canonicalizes the display name with the existing
length, character, and case-insensitive uniqueness rules. Its Appear Offline
write is read back synchronously before the in-memory profile changes. After a
successful response, the client refreshes the public directory. Supplying an
extra account-ID field is a wrong-field-count request and cannot target another
account.

## Administration

All authorization is repeated server-side against the authenticated
`WorldSession`. `ADMIN_CREATE_ACCOUNT` additionally requires AzerothCore's
normal account-create RBAC permission. UI visibility and request fields do not
grant authority.

```text
ADMIN_CAPS
  -> ADMIN_CAPS_RESULT requestId authorized(0|1)

ADMIN_LIST
  -> ADMIN_START requestId count
     ADMIN_ENTRY requestId accountId configured(0|1) escapedDisplayName
     ADMIN_END requestId

ADMIN_SET_NAME requestId accountId escapedDisplayName
  -> ADMIN_RESULT requestId resultCode accountId escapedMessage

ADMIN_CREATE_ACCOUNT requestId escapedAccountName escapedPassword escapedDisplayName
  -> ADMIN_RESULT requestId resultCode accountId escapedMessage
```

`ADMIN_LIST` contains account IDs and profile state only; it deliberately omits
login names. Passwords are passed only to `AccountMgr::CreateAccount`, are
overwritten in temporary request storage where practical, are never logged,
and never appear in a response. Display-name validation and uniqueness run
before account creation. A profile failure after core account creation returns
`PROFILE_SETUP_FAILED` and the new account ID for repair; the authentication
account is not deleted.

The native management frame currently exposes `ADMIN_LIST` and
`ADMIN_SET_NAME`. It intentionally does not expose graphical account creation
or any password field. The pre-existing `ADMIN_CREATE_ACCOUNT` compatibility
operation remains server-authorized, but adding it to the UI requires a
separate credential-transport review and approval.

`ADMIN_SET_NAME` contains exactly the request ID, target account ID, and
escaped Display Name after the command. The server parses the numeric account
ID, repeats administrator authorization and human-account eligibility checks,
then uses the shared profile store so persistence and live memory update
together. Success triggers both an Admin-list reload and a public-directory
refresh.

Errors use:

```text
NSOC  01  ERROR  requestId  fourDigitHexCode  escapedMessage
```

Codes cover protocol, command, length, field, unavailable, unauthorized, and
validation failures. Responses never echo credentials.
