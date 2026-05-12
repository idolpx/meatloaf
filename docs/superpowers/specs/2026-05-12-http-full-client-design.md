# Full HTTP Client Mode for Meatloaf

## Status

Approved

## Overview

Extend HTTPMStream to support full HTTP client capabilities (REST/GraphQL APIs) while maintaining backward compatibility with simple URL loading.

## Architecture

### New Class: HTTPRequestContext

Located in `http.cpp`, holds request building state:

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
    bool sendRequest(Session* session);  // executes request via MeatHttpClient
};
```

### HTTPMStream Changes

```cpp
class HTTPMStream : public MStream {
private:
    HTTPRequestContext ctx;
    enum class ResponseMode { BODY, HEADERS } responseMode = ResponseMode::BODY;
    bool fullMode = false;  // switches from simple to full on first command
    bool keepAlive = true;

public:
    bool handleCommand(const std::string& cmd);  // returns true if cmd was recognized
    bool isFullMode() { return fullMode; }
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
- `k on` / `k off` - keep-alive
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
| `status` | Return HTTP status code | `status` |
| `c` | Clear request context (headers + body + response) | `c` |
| `k on` / `k off` | Enable/disable HTTP keep-alive | `k on` |

## Mode Transitions

```
SIMPLE → (any command) → FULL_BUILDING
FULL_BUILDING → (S) → RESPONSE_HEADERS
RESPONSE_HEADERS → (R-B) → RESPONSE_BODY
```

## Response Reading

### Header Mode (R-H)
- Each `read()` returns one header line: `Name: value`
- Empty string signals end of headers
- No EOI after last header

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
- `code`: numeric error code
- `message$`: human readable ("OK", "Connection refused", "404 Not Found")
- `extra$`: optional extra info

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
- `k`: followed by space and on/off

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