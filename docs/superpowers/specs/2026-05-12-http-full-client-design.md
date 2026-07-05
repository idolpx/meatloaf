# Full HTTP Client Mode for Meatloaf

## Status

Approved (2026-05-12). Re-approved with error-routing scope and clarifications 2026-07-05 after brainstorming session.

## Overview

Extend HTTPMStream to support full HTTP client capabilities (REST/GraphQL APIs) while maintaining backward compatibility with simple URL loading.

## Architecture

### New Class: HTTPRequestContext

Declared in `http.h` (implementation in `http.cpp`), holds request building state:

```cpp
class HTTPRequestContext {
public:
    std::string method;                    // GET, POST, PUT, DELETE, etc.
    std::map<std::string, std::vector<std::string>> headers;  // headers can have multiple values
    std::string body;                     // request body

    // Response state
    std::vector<std::string> responseHeaders;  // buffered response headers
    int responseStatus = 0;               // HTTP status code

    void setMethod(const std::string& m);
    void setHeader(const std::string& name, const std::string& value);  // replaces
    void appendHeader(const std::string& name, const std::string& value);  // appends
    void setBody(const std::string& b);
    void appendBody(const std::string& b);
    void clear();  // resets all state
    bool sendRequest(std::shared_ptr<HTTPMSession> session);  // executes request via session->client
};
```

### HTTPMStream Changes

```cpp
class HTTPMStream : public MStream {
public:
    enum class FullModeState {
        SIMPLE,             // backward-compatible mode, no commands active
        BUILDING_REQUEST,   // full mode activated, accumulating commands
        RESPONSE_HEADERS,   // request sent, reading response headers
        RESPONSE_BODY       // reading response body
    };

private:
    HTTPRequestContext ctx;
    FullModeState fullMode = FullModeState::SIMPLE;
    bool _statusRequested = false;

public:
    bool handleCommand(const std::string& cmd);  // returns true if cmd was recognized
    bool isFullMode() const { return fullMode != FullModeState::SIMPLE; }
};
```

### Command Routing

Commands are detected when `write()` receives data starting with:
- `m` - set method
- `h` - set header
- `h+` - append header
- `b` - set body
- `b+` - append body
- `s` - send request
- `r-h` - switch to headers mode
- `r-b` - switch to body mode
- `c` - clear context
- `status` - return status code

## Command Reference

| Command | Description | Example |
|---------|-------------|---------|
| `m <method>` | Set HTTP method | `m post` |
| `h <name>: <value>` | Set/replace header | `h authorization: bearer token` |
| `h+ <name>: <value>` | Append header (allows duplicates) | `h+ accept: application/json` |
| `b <text>` | Replace request body | `b {"model":"gpt-4o"}` |
| `b+ <text>` | Append to request body | `b+ "messages":[{...}]` |
| `s` | Send HTTP request | `s` |
| `r-h` | Switch to response header reading mode | `r-h` |
| `r-b` | Switch to response body reading mode | `r-b` |
| `status` | Return HTTP status code (decimal text) | `status` |
| `c` | Clear request context (headers + body + response) | `c` |

## Mode Transitions

```
SIMPLE → (any command) → BUILDING_REQUEST
BUILDING_REQUEST → (S) → RESPONSE_HEADERS
RESPONSE_HEADERS → (R-B) → RESPONSE_BODY
RESPONSE_BODY → (C) → BUILDING_REQUEST       (clear and prepare next request)
```

After consuming the response body, the user sends `c` to clear context and return to BUILDING_REQUEST for the next request cycle. If `s` is sent again without `c`, the existing ctx is re-used (headers, method, and body persist).

## Response Reading

### Header Mode (R-H)
- Each `read()` returns one header line: `Name: value`
- Lines are `\r\n` terminated per header (required by C64 `GET#` which reads up to CR)
- An empty CRLF line (`\r\n`) signals end of headers
- No EOI after the empty line — body follows immediately in the stream
- Body mode begins automatically after the empty CRLF marker (or on explicit `r-b`)

### Body Mode (R-B)
- Default mode after `S`
- EOI sent after entire body transmitted

## Error Handling

### Error Codes
| Code | Meaning |
|------|---------|
| 0 | OK |
| 20-29 | Network/connection errors |
| 30-39 | HTTP errors (4xx/5xx) |
| 99 | Internal error |

### ST Variable
- Standard C64 status bits
- Bit 6 (64) = EOI reached
- Bit 7 (128) = error

### Channel 15 Format
```basic
INPUT#15, code, message$, extra$
```
- `code`: numeric error code (see Error Codes table)
- `message$`: human readable ("OK", "Connection refused", "404 Not Found")
- `extra$`: optional extra info

### Error Routing

Errors from full-mode HTTP operations (connection failure, DNS error, 4xx, 5xx) are communicated via the existing `iecStatus` mechanism in `lib/device/iec/network.cpp`. When a full-mode request fails:

1. The error code is captured into `HTTPRequestContext::responseStatus` (0 = success, < 0 = local error, > 0 = HTTP status code)
2. On the next `status` command, the value is surfaced through `iecStatus.error` and `iecStatus.msg`
3. ST variable bits are set: bit 6 (EOI) when body is fully consumed, bit 7 (error) when `responseStatus` is non-zero

The mapping from HTTP error codes to the C64 error table is:

| HTTP Result | responseStatus | iecStatus.error | iecStatus.msg |
|-------------|----------------|-----------------|---------------|
| 200-299 (success) | 200-299 | 0 | "OK" |
| connection refused | -1 | 20 | "Connection refused" |
| DNS / host not found | -2 | 20 | "Host not found" |
| 4xx status | 400-499 | 30 + (status - 400) | e.g. "404 Not Found" |
| 5xx status | 500-599 | 35 + (status - 500) | e.g. "503 Service Unavailable" |
| internal / unknown | -99 | 99 | "Internal error" (with HTTP code in extra$) |

**Conflict resolution**: the existing `NETWORK_ERROR_*` constants in `lib/network-protocol/status_error_codes.h` use a different numeric range (e.g. `NETWORK_ERROR_CONNECTION_REFUSED = 200`). The full-mode HTTP error table in this spec is **independent** — `iecStatus.error` follows the table above for HTTP full-mode operations. Existing `NETWORK_ERROR_*` constants remain in use for other (non-HTTP) network operations. Both code paths share the `iecStatus` field but produce values from their respective tables.

**Implementation helper**: a new method `HTTPRequestContext::errorToIecStatus(int& errOut, std::string& msgOut)` performs the table lookup. Called by `handleCommand("status")` immediately before returning the decimal status string. A `bool HTTPRequestContext::isHttpError() const` predicate (`responseStatus < 200 || responseStatus >= 300`) drives the ST error bit.

## Backward Compatibility

Simple mode continues to work unchanged. Any URL opened and read without commands uses the existing behavior.

### Simple Mode (unchanged)
```basic
OPEN 2,8,2,"https://example.com/data.json"
INPUT#2,A$
CLOSE 2
```

### Full Mode (new)
```basic
OPEN 1,8,2,"https://api.example.com/endpoint"
PRINT#1,"m post"
PRINT#1,"h content-type: application/json"
PRINT#1,"b {""query"":""{users}""}"
PRINT#1,"s"
PRINT#1,"r-h"
GET#1,A$:IF A$="" GOTO 100
PRINT#1,"r-b"
GET#1,A$
CLOSE 1
```

### Command Detection

Commands are detected by the first character(s) in the `write()` buffer:
- `m`, `h`, `b` commands: full string is command + argument
- `s`, `c` commands: single character
- `r-h`, `r-b`: two characters

**Non-command data handling:** Any data written via `write()` in full mode that does not match a recognized command prefix is silently ignored. The body must be explicitly set via `b` or `b+` commands. This prevents accidental garbage from the C64 IEC bus from corrupting state.

### Request Sending Flow

When `S` command is executed:

1. `ctx.sendRequest()` is called
2. Headers from `ctx.headers` are applied to `MeatHttpClient`
3. Body from `ctx.body` is set via `setPostField()` or written
4. Method from `ctx.method` is set on client
5. Request is sent via `client.GET/POST/PUT()`
6. Response headers are captured into `ctx.responseHeaders`
7. Mode switches to `RESPONSE_HEADERS`

## Implementation Notes

1. Commands are case-insensitive
2. If URL opened but `S` never sent, no action on close
3. Redirects handled automatically by existing MeatHttpClient
4. Chunked transfer encoding handled transparently
5. Multiple simultaneous requests (different LFNs) fully supported

## Out of Scope (this plan)

- **DELETE method**: `MeatHttpClient` has no `DELETE()` method and no BASIC/Python test exercises it. Users can send `m delete` but it will fail at dispatch. Documented gap for future work.
- **Keep-alive toggle (`k on` / `k off`)**: removed from the command set entirely. Keep-alive behavior is controlled by `MeatHttpClient`'s existing connection-reuse mechanism via `HTTPMSession`. No user-facing toggle.
- **Channel-15 protocol layer wiring**: only the local `iecStatus` fields are populated. Surfacing them to the C64 command channel is the responsibility of existing `lib/device/iec/network.cpp` consumers, not this plan.
- **Refactoring `MeatHttpClient`**: the existing class has everything we need (`setOnHeader`, `setHeader`, `postBuffer`). No internal changes.

## Testing

### Native Unit Test
- Location: `test/native/test_http_full_client/test_http_full_client.cpp`
- Coverage: `HTTPRequestContext` semantics only (`setMethod`, `setHeader`, `appendHeader`, `setBody`, `appendBody`, `clear`, `popResponseHeader`, `hasMoreResponseHeaders`, `errorToIecStatus` table lookup).
- Build target: `pio test -e native`.
- Acceptance: all tests pass with 0 failures.

### BASIC Integration Test
- Location: `test/http/http_full_client_test.bas` (already exists, 9 scenarios).
- Coverage: basic GET echo, POST with body, header set + multi-value append, status code, response header mode, multi-request cycle, 404 handling, PUT, body append.
- Companion server: `python3 test/http/test_server.py` (echo endpoint, status simulation, hex logging).
- Companion client: `python3 test/http/test_client.py` (server-side validation, C64 flow simulation).
- Acceptance: all 9 tests pass on real C64 hardware.

### Build Verification
- `pio test -e native` — native unit tests pass.
- `pio run -e fujiloaf-rev0` — embedded firmware builds.

## Plan Tasks (high-level)

1. Discard `http-client-phase3` branch (local + remote tracking).
2. Add `HTTPRequestContext` class declaration + implementation in `http.h`/`http.cpp`.
3. Add `FullModeState` enum + `handleCommand()` + modified `HTTPMStream::write()`/`read()`/`close()`.
4. Implement error routing (`errorToIecStatus`, `isHttpError`, status command ST-bit setting).
5. Add native unit test (`test/native/test_http_full_client/`).
6. Update plan document with the 7-task structure (this is item 6; see `docs/superpowers/plans/2026-06-29-http-full-client-implementation.md`).
7. Build verification: native tests pass + embedded firmware builds.

Execution method: **subagent-driven-development** — one subagent per task, verified in the main loop before advancing.