# Native Social Protocol (NSOC) v1

## Overview

NSOC is a bidirectional protocol for communicating between the 3.3.5a WoW client and the AzerothCore server using the existing addon-message transport (CHAT_MSG_ADDON with LANG_ADDON).

## Protocol Format

All messages use UTF-8 encoding and are delimited with tab characters (`\t`).

### Message Structure

```
<prefix>\t<version>\t<command>\t<data>\t<data>...
```

- **prefix**: Always `NSOC` (4 bytes)
- **version**: Protocol version (2 bytes, currently `01`)
- **command**: Operation code (see below)
- **data**: Command-specific fields

### Supported Commands

#### 1. LIST (Request)

```
NSOC\t01\tLIST\t<request_id>
```

**Fields:**
- `request_id` (1-16 ASCII alphanumeric): Transaction ID for matching responses. Must be 1-16 characters, ASCII alphanumeric only (A-Z, a-z, 0-9). Non-alphanumeric or out-of-range request IDs are rejected with an ERROR response. Recommended: 4 characters (e.g., "ABCD")

**Example:**
```
NSOC\t01\tLIST\tABCD
```

#### 2. LIST_START (Response)

```
NSOC\t01\tLIST_START\t<request_id>\t<count>
```

**Fields:**
- `request_id` (1-16 ASCII alphanumeric): Echoed from request
- `count` (decimal, no zero padding): Total number of profiles that will follow

#### 3. LIST_PROFILE (Response)

```
NSOC\t01\tLIST_PROFILE\t<request_id>\t<display_name>
```

**Fields:**
- `request_id` (1-16 ASCII alphanumeric): Echoed from request
- `display_name` (variable): UTF-8 display name (escaped, see below)

#### 4. LIST_END (Response)

```
NSOC\t01\tLIST_END\t<request_id>
```

**Fields:**
- `request_id` (1-16 ASCII alphanumeric): Echoed from request

#### 5. ERROR (Response)

```
NSOC\t01\tERROR\t<request_id>\t<error_code>\t<error_message>
```

**Fields:**
- `request_id` (1-16 ASCII alphanumeric): Echoed from request (may be the default `0000` if the request ID itself was invalid)
- `error_code` (4 uppercase hex digits): Numeric error code
- `error_message` (variable): Human-readable error description

### Error Codes

Error codes are rendered as 4 uppercase hex digits (e.g. `0005`); the defined values are:

- `0000`: Protocol error (malformed message)
- `0001`: Unsupported protocol version
- `0002`: Unknown command
- `0003`: Message too long (exceeds 255 bytes)
- `0004`: Invalid field count
- `0005`: Module unavailable

## Escaping

Display names may contain tab characters. To safely include a tab in a display name, escape backslashes first, then tabs:

1. Replace `\` with `\\` (doubled backslash)
2. Replace `\t` (tab) with `\\t` (backslash followed by 't')

Escaping backslashes before tabs keeps the two escapes unambiguous.

When parsing, decode left to right: on a backslash, take the next character into account:

1. `\\` → `\` (backslash)
2. `\t` → tab
3. Any other character is copied verbatim

This decode is order-independent and is the exact inverse of the escaping above: a display name containing a literal `\t` is escaped to `\\t` and decodes back to `\t` unchanged.

## Payload Size

Each NSOC message must be ≤ 254 bytes (255 bytes is the AzerothCore limit, minus 1 byte for safety). Oversized messages — requests and responses — are rejected by the server and never truncated: a response that would exceed the limit is discarded with a server-side log entry, preserving protocol validity.

## Chunking

The LIST operation returns one profile per message. The client receives:
1. LIST_START with the total count
2. Zero or more LIST_PROFILE messages
3. LIST_END to signal completion

## Future Extensions

The protocol namespace is designed to accommodate future operations:

- `PRESENCE`: Presence update notifications
- `SEND`: Account-to-account messaging
- `FAVORITE`: Friends/favorites management
- `BLOCK`: Ignore/block list management

Each new operation follows the same pattern:
- Request format with optional request_id
- Response format with status/error handling
- Chunked data transfer when appropriate

## Examples

### Request

```
NSOC\t01\tLIST\tABCD
```

### Successful Response (1 profile)

```
NSOC\t01\tLIST_START\tABCD\t1
NSOC\t01\tLIST_PROFILE\tABCD\tKevin
NSOC\t01\tLIST_END\tABCD
```

### Error Response

```
NSOC\t01\tERROR\tABCD\t0005\tModule unavailable
```

## Implementation Notes

1. **Protocol Versioning**: Version `01` is the initial version. Future versions will be backward-compatible where possible.

2. **Request Validation**: Messages are length-gated at 254 bytes, must start with the exact `NSOC\t01\t` frame, and the request ID must be 1-16 ASCII alphanumeric characters. The LIST command requires exactly four fields. Requests that pass the length gate but fail the grammar checks are answered with an ERROR response and consumed as NSOC traffic.

3. **Display Name Validation**: The server validates display names according to the SocialService rules (3-24 characters, UTF-8, no control characters).

4. **Privacy**: The LIST operation only returns profiles where `appear_offline` is false and the account is currently online.

5. **Playerbots**: Accounts controlled by playerbots are never included in any response.
