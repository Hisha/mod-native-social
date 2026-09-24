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
Authentication usernames never occur in this response.

The earlier `LIST` / `LIST_START` / `LIST_PROFILE` / `LIST_END` exchange is
retained as a compatibility surface and still returns visible online display
names only. New clients use `DIR_LIST`.

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

Errors use:

```text
NSOC  01  ERROR  requestId  fourDigitHexCode  escapedMessage
```

Codes cover protocol, command, length, field, unavailable, unauthorized, and
validation failures. Responses never echo credentials.
