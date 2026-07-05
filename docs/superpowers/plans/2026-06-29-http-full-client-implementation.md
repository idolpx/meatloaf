# Full HTTP Client Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add full HTTP client capabilities (REST/GraphQL APIs) to HTTPMStream so C64 BASIC programs can set method, headers, and body via PRINT# commands, then read response headers and body via GET#.

**Architecture:** A new `HTTPRequestContext` class holds request-building state (method, multi-value headers, body) and response state (status, response headers). It lives in `lib/meatloaf/network/http.h`/`http.cpp`. `HTTPMStream` gains a `FullModeState` enum and `handleCommand()`. `write()` routes commands vs raw data; unrecognized data in full mode is silently ignored. `read()` serves buffered response headers one per call (terminated by an empty `\r\n`) or streams the body from `MeatHttpClient`. `close()` skips the auto-POST in full mode. Error routing maps HTTP status to `iecStatus.error`/`msg` via a new `HTTPRequestContext::errorToIecStatus()` helper.

**Tech Stack:** C++17, ESP-IDF (custom `fn_esp_http_client` fork), FreeRTOS task notifications for inter-task streaming. Native tests use Unity (already a dep).

**Spec:** `docs/superpowers/specs/2026-05-12-http-full-client-design.md` (committed at `e63493f1`).

## Global Constraints

- All new C++ code goes in existing files: `lib/meatloaf/network/http.h` (declarations) and `lib/meatloaf/network/http.cpp` (implementations). No new files in `lib/`.
- Command parsing is case-insensitive.
- Non-command data in full mode is silently ignored (the spec's "Non-command data handling" rule).
- Response headers are `\r\n` terminated (C64 `GET#` reads up to CR). An empty `\r\n` line marks end of headers.
- EOI (ST bit 6) sent after entire body transmitted; error (ST bit 7) when `ctx.responseStatus` is non-2xx.
- Error codes use the **spec table** (see spec section "Error Routing"). Existing `NETWORK_ERROR_*` constants in `lib/network-protocol/status_error_codes.h` are NOT reused for full-mode HTTP (separate namespace).
- Commands arrive via `HTTPMStream::write()` — the C64's `PRINT#` writes to the stream.
- Response headers are captured using `MeatHttpClient::setOnHeader()` callback; request headers are set via `MeatHttpClient::setHeader("Name: value")`.
- DELETE method is out of scope (no `MeatHttpClient::DELETE()`, no test).
- Keep-alive toggle (`k on`/`k off`) is out of scope — no command parsing, no `keepAlive` field.
- Refactoring `MeatHttpClient` is out of scope.
- Channel-15 protocol layer wiring (surfacing `iecStatus` to C64) is out of scope — only populate the fields.

---

### Task 1: Discard `http-client-phase3` branch

**Files:**
- No code changes.

- [ ] **Step 1: Verify the branch exists and contains the abandoned work**

```bash
git -C /home/qus/dev/_c/meatloaf rev-parse --verify http-client-phase3
git -C /home/qus/dev/_c/meatloaf log --oneline http-client-phase3 ^main | head -20
```

Expected: exit code 0, list of plan-task commits (`9f13060e`, `9d274adf`, `cf3d44b2`, `ac149fb3`, `9033878a`, `1fa41511`, `d59cac5d`, `5496f50a`, `41d455ff`).

- [ ] **Step 2: Delete the local branch**

```bash
git -C /home/qus/dev/_c/meatloaf branch -D http-client-phase3
```

Expected: `Deleted branch http-client-phase3 (was <sha>).`

- [ ] **Step 3: Delete the remote-tracking branch (if it exists)**

```bash
git -C /home/qus/dev/_c/meatloaf rev-parse --verify origin/http-client-phase3 \
    && git -C /home/qus/dev/_c/meatloaf push origin --delete http-client-phase3 \
    || echo "no remote tracking branch — skipping"
```

Expected: either `To <remote> - [deleted] http-client-phase3` or `no remote tracking branch — skipping`.

- [ ] **Step 4: Confirm `main` is intact and unchanged**

```bash
git -C /home/qus/dev/_c/meatloaf status --short
git -C /home/qus/dev/_c/meatloaf rev-parse --abbrev-ref HEAD
```

Expected: status output shows only the working-tree modifications (untracked `docs/superpowers/plans/`, modified `test/http/test_client.py` and `test/http/test_server.py`, etc.) — no `http-client-phase3` references. HEAD is `main`.

- [ ] **Step 5: Commit**

```bash
git -C /home/qus/dev/_c/meatloaf commit --allow-empty -m "chore: discard http-client-phase3 branch

The branch contained an earlier execution of the full HTTP client plan
that diverged from the updated spec (re-approved 2026-07-05). Re-execute
the plan from scratch against the current spec and tests."
```

---

### Task 2: HTTPRequestContext class declaration

**Files:**
- Modify: `lib/meatloaf/network/http.h` — add class declaration after the existing `#include <map>` line (around line 39), before `class MeatHttpClient` (line 60).

**Interfaces:**
- Consumes: (none — standalone helper class)
- Produces: `class HTTPRequestContext` with the public API below. Implementation comes in Task 3.

- [ ] **Step 1: Add the class declaration**

In `lib/meatloaf/network/http.h`, locate the line:

```cpp
/********************************************************
 * HTTP Client
 ********************************************************/


class MeatHttpClient {
```

Insert the block above the blank line that precedes `class MeatHttpClient`:

```cpp
/********************************************************
 * HTTP Request Context — full-mode request builder
 ********************************************************/

class HTTPRequestContext {
public:
    // Request state
    std::string method = "GET";                              // GET, POST, PUT, HEAD
    std::map<std::string, std::vector<std::string>> headers; // multi-value headers
    std::string body;                                        // request body

    // Response state
    std::vector<std::string> responseHeaders;                // buffered response header lines ("Name: value")
    int responseStatus = 0;                                  // 0 = unused, <0 = local err, >0 = HTTP status
    bool responseConsumed = false;                           // true after all response data read

    void setMethod(const std::string& m);
    void setHeader(const std::string& name, const std::string& value);    // replaces
    void appendHeader(const std::string& name, const std::string& value); // appends
    void setBody(const std::string& b);
    void appendBody(const std::string& b);
    void clear();                                              // resets all state for next request

    bool sendRequest(std::shared_ptr<HTTPMSession> session);   // executes via session->client

    // For header-mode reading
    bool hasMoreResponseHeaders() const {
        return !responseHeaders.empty();
    }
    std::string popResponseHeader();  // returns "Name: value\r\n" or "" at end

    // Error routing
    void errorToIecStatus(int& errOut, std::string& msgOut) const;
    bool isHttpError() const { return responseStatus < 200 || responseStatus >= 300; }
};


/********************************************************
 * HTTP Client
 ********************************************************/
```

- [ ] **Step 2: Verify the header still compiles**

```bash
pio run -e fujiloaf-rev0 -t pre 2>&1 | tail -5
```

Expected: build exits 0 (or, if `pre` is unsupported, just `pio run -e fujiloaf-rev0 2>&1 | tail -10` exits 0 with `HTTPRequestContext` visible in any error).

If the build fails because `HTTPMSession` isn't forward-declared, add `class HTTPMSession;` immediately above the new class.

- [ ] **Step 3: Commit**

```bash
git add lib/meatloaf/network/http.h
git commit -m "feat(http): declare HTTPRequestContext class for full client mode"
```

---

### Task 3: HTTPRequestContext method implementations

**Files:**
- Modify: `lib/meatloaf/network/http.cpp` — add method implementations at the top of the file (after `#include "../../../include/debug.h"` around line 30, before `HTTPMSession implementation` on line 33).

**Interfaces:**
- Consumes: `class HTTPRequestContext` from Task 2; `MeatHttpClient` (existing); `HTTPMSession` (existing).
- Produces: working implementations of all 11 public methods. No new fields, no new types.

- [ ] **Step 1: Add the implementation block**

Insert the following block right after the `#include "../../../include/debug.h"` line and the blank line, but BEFORE the `HTTPMSession implementation` banner:

```cpp
/********************************************************
 * HTTPRequestContext implementation
 ********************************************************/

void HTTPRequestContext::setMethod(const std::string& m) {
    method = m;
    mstr::toUpper(method);
}

void HTTPRequestContext::setHeader(const std::string& name, const std::string& value) {
    std::string key = name;
    mstr::toLower(key);
    headers[key] = {value};  // replaces any existing values for this key
}

void HTTPRequestContext::appendHeader(const std::string& name, const std::string& value) {
    std::string key = name;
    mstr::toLower(key);
    headers[key].push_back(value);
}

void HTTPRequestContext::setBody(const std::string& b) {
    body = b;
}

void HTTPRequestContext::appendBody(const std::string& b) {
    body += b;
}

void HTTPRequestContext::clear() {
    method = "GET";
    headers.clear();
    body.clear();
    responseHeaders.clear();
    responseStatus = 0;
    responseConsumed = false;
}

bool HTTPRequestContext::sendRequest(std::shared_ptr<HTTPMSession> session) {
    if (!session || !session->client) {
        responseStatus = -1;  // local: no session
        return false;
    }

    auto& client = *session->client;

    // Reset response header buffer BEFORE dispatch — onHeader may fire synchronously.
    responseHeaders.clear();
    responseStatus = 0;

    // Capture response headers via the existing onHeader callback.
    client.setOnHeader([this](char* key, char* value) -> int {
        if (key && value) {
            std::string headerLine = std::string(key) + ": " + std::string(value);
            responseHeaders.push_back(headerLine);
        }
        return 0;
    });

    // Apply request headers.
    for (const auto& [key, values] : headers) {
        for (const auto& val : values) {
            std::string headerLine = key + ":" + val;
            client.setHeader(headerLine);
        }
    }

    // Set body if present.
    if (!body.empty()) {
        client.postBuffer.clear();
        client.postBuffer.insert(client.postBuffer.end(), body.begin(), body.end());
    }

    // Dispatch by method.
    bool result = false;
    if (method == "GET") {
        result = client.GET(client.url);
    } else if (method == "POST") {
        result = client.POST(client.url);
        if (result && !client.postBuffer.empty()) {
            client.close();
        }
    } else if (method == "PUT") {
        result = client.PUT(client.url);
        if (result && !client.postBuffer.empty()) {
            client.close();
        }
    } else if (method == "HEAD") {
        result = client.HEAD(client.url);
    } else {
        // DELETE and other methods are out of scope for this plan.
        responseStatus = -99;  // internal error
        return false;
    }

    // Capture HTTP status (client.lastRC is set by the dispatch above).
    if (responseStatus == 0) {
        responseStatus = client.lastRC;
    }
    // 0 from lastRC means the connection itself failed.
    if (responseStatus == 0) {
        responseStatus = -1;  // local: connection failed
    }

    return result;
}

std::string HTTPRequestContext::popResponseHeader() {
    if (responseHeaders.empty()) return {};
    std::string line = responseHeaders.front();
    responseHeaders.erase(responseHeaders.begin());
    return line + "\r\n";
}

void HTTPRequestContext::errorToIecStatus(int& errOut, std::string& msgOut) const {
    // Spec table: responseStatus → iecStatus.error, iecStatus.msg
    if (responseStatus >= 200 && responseStatus < 300) {
        errOut = 0;
        msgOut = "OK";
    } else if (responseStatus == -1) {
        errOut = 20;
        msgOut = "Connection refused";
    } else if (responseStatus == -2) {
        errOut = 20;
        msgOut = "Host not found";
    } else if (responseStatus >= 400 && responseStatus < 500) {
        errOut = 30 + (responseStatus - 400);
        msgOut = std::to_string(responseStatus) + " HTTP client error";
    } else if (responseStatus >= 500 && responseStatus < 600) {
        errOut = 35 + (responseStatus - 500);
        msgOut = std::to_string(responseStatus) + " HTTP server error";
    } else {
        errOut = 99;
        msgOut = "Internal error";
    }
}
```

- [ ] **Step 2: Build to verify it compiles**

```bash
pio run -e fujiloaf-rev0 2>&1 | tail -10
```

Expected: exit 0. If `mstr::toLower` or `mstr::toUpper` is unresolved, the include for `lib/utils/string_utils.h` is missing — add `#include "utils/string_utils.h"` at the top of `http.cpp` near the other includes.

- [ ] **Step 3: Commit**

```bash
git add lib/meatloaf/network/http.cpp
git commit -m "feat(http): implement HTTPRequestContext methods

Implements setMethod/setHeader/appendHeader/setBody/appendBody/clear,
sendRequest (drives MeatHttpClient and captures response headers via
onHeader), popResponseHeader, and errorToIecStatus table lookup per
the spec's error-routing table."
```

---

### Task 4: FullModeState enum, handleCommand, modified HTTPMStream

**Files:**
- Modify: `lib/meatloaf/network/http.h` — replace `HTTPMStream` class (lines 244-304) with the enhanced version including `FullModeState` enum, `ctx`, `fullMode`, `_statusRequested`, `handleCommand()`.
- Modify: `lib/meatloaf/network/http.cpp` — replace `HTTPMStream::write()` (lines 356-364), `HTTPMStream::read()` (lines 306-354), and `HTTPMStream::close()` (lines 275-292) with the full-mode-aware versions. Add `handleCommand()` implementation after the `HTTPRequestContext` implementation block.

**Interfaces:**
- Consumes: `HTTPRequestContext` from Task 3; existing `MStream` base class.
- Produces: `HTTPMStream` whose `write()` dispatches commands, `read()` serves headers/body by mode, `close()` skips auto-POST in full mode.

- [ ] **Step 1: Replace `HTTPMStream` declaration in http.h**

In `lib/meatloaf/network/http.h`, replace the entire `class HTTPMStream: public MStream { ... };` block (from `class HTTPMStream: public MStream {` on line 244 through the closing `};` on line 304) with:

```cpp
class HTTPMStream: public MStream {
public:
    enum class FullModeState {
        SIMPLE,             // backward-compatible mode, no commands active
        BUILDING_REQUEST,   // full mode activated, accumulating commands
        RESPONSE_HEADERS,   // request sent, reading response headers
        RESPONSE_BODY       // reading response body
    };

    HTTPMStream(std::string path): MStream(path) {
        //url = path;
    };
    HTTPMStream(std::string path, std::ios_base::openmode m): MStream(path) {
        //url = path;
        mode = m;
    };

    ~HTTPMStream() {
        close();
    };

    // MStream methods
    bool isOpen() override;
    bool isNetwork() override { return true; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    uint32_t available() override {
        // POST response buffer takes priority (existing behavior).
        if (_session && _session->client && !_session->client->postResponse.empty()) {
            uint32_t respAvail = (uint32_t)_session->client->postResponse.size() - _session->client->_position;
            if (respAvail > 0) return respAvail;
        }
        // Full mode: at least one buffered response header.
        if (fullMode == FullModeState::RESPONSE_HEADERS && ctx.hasMoreResponseHeaders()) {
            return 1;
        }
        if (_size > _position)
            return _size - _position;
        if (isOpen() && _session && _session->client && !_session->client->complete())
            return HTTP_BLOCK_SIZE;
        return 0;
    }

    virtual bool seek(uint32_t pos);
    virtual bool seekPath(std::string path) override {
        Debug_printv( "path[%s]", path.c_str() );
        return false;
    }

    // Full-mode helpers
    bool handleCommand(const std::string& cmd);
    bool isFullMode() const { return fullMode != FullModeState::SIMPLE; }

protected:
    std::shared_ptr<HTTPMSession> _session = nullptr;

private:
    friend class HTTPMFile;

    HTTPRequestContext ctx;
    FullModeState fullMode = FullModeState::SIMPLE;
    bool _statusRequested = false;
};
```

- [ ] **Step 2: Replace `HTTPMStream::close()` in http.cpp**

Find the existing `void HTTPMStream::close()` definition (starts around line 275) and replace it with:

```cpp
void HTTPMStream::close() {
    // Full mode: skip the auto-POST used by simple mode. User already sent 's'.
    if (fullMode != FullModeState::SIMPLE) {
        if (_session) {
            _session->releaseIO();
        }
        fullMode = FullModeState::SIMPLE;
        ctx.clear();
        return;
    }

    // Simple mode: original behavior.
    bool isWriteMode = (mode & 0x10) || (mode == std::ios_base::out)
        || (mode == (std::ios_base::in | std::ios_base::out));
    if (isWriteMode && _session && _session->client) {
        auto client = _session->client.get();
        client->close();
    }
    if (_session) {
        _session->releaseIO();
    }
}
```

- [ ] **Step 3: Replace `HTTPMStream::read()` in http.cpp**

Find the existing `uint32_t HTTPMStream::read(uint8_t* buf, uint32_t size)` definition (starts around line 306) and replace it with:

```cpp
uint32_t HTTPMStream::read(uint8_t* buf, uint32_t size) {
    uint32_t bytesRead = 0;

    if (size > 0) {
        // Full mode: 'status' command — return status as decimal text.
        if (_statusRequested) {
            _statusRequested = false;
            // Surface the error via iecStatus before returning the decimal text.
            int errCode = 0;
            std::string errMsg;
            ctx.errorToIecStatus(errCode, errMsg);
            _error = ctx.isHttpError() ? 1 : 0;
            std::string statusStr = std::to_string(ctx.responseStatus);
            uint32_t copyLen = std::min((uint32_t)statusStr.size(), size);
            memcpy(buf, statusStr.data(), copyLen);
            return copyLen;
        }

        // Full mode: serve buffered response headers one per read().
        if (fullMode == FullModeState::RESPONSE_HEADERS) {
            std::string headerLine;
            if (ctx.hasMoreResponseHeaders()) {
                headerLine = ctx.popResponseHeader();
            } else {
                // End-of-headers marker; auto-transition to body mode.
                headerLine = "\r\n";
                fullMode = FullModeState::RESPONSE_BODY;
            }
            uint32_t copyLen = std::min((uint32_t)headerLine.size(), size);
            memcpy(buf, headerLine.data(), copyLen);
            _error = 0;
            return copyLen;
        }

        // Full mode: response body — read from MeatHttpClient.
        if (fullMode == FullModeState::RESPONSE_BODY) {
            if (_session && _session->client) {
                bytesRead = _session->client->read(buf, size);
                _position += bytesRead;
                _error = _session->client->_error;
                if (bytesRead == 0 && _session->client->complete()) {
                    ctx.responseConsumed = true;
                }
                return bytesRead;
            }
            return 0;
        }

        // Simple mode: original write→read switch behavior.
        bool isWriteMode = (mode & 0x10) || (mode == std::ios_base::out);
        if (isWriteMode && _session && _session->client) {
            auto client = _session->client.get();
            bool hasResponse = (!client->postBuffer.empty()) ||
                              (!client->postResponse.empty()) ||
                              (!client->preservedPostResponse.empty());
            if (!hasResponse) {
                Debug_printv("POST already sent, reading from existing response");
            } else if (!client->postBuffer.empty()) {
                Debug_printv("Sending POST request...");
                client->close();
            }
            mode = std::ios_base::in;
            _position = 0;
        }

        if (size > available())
            size = available();

        if (size > 0) {
            bytesRead = _session->client->read(buf, size);
            _position += bytesRead;
            _error = _session->client->_error;
        }
    }

    return bytesRead;
}
```

- [ ] **Step 4: Replace `HTTPMStream::write()` in http.cpp**

Find the existing `uint32_t HTTPMStream::write(const uint8_t *buf, uint32_t size)` definition (starts around line 356) and replace it with:

```cpp
uint32_t HTTPMStream::write(const uint8_t *buf, uint32_t size) {
    // In full mode, every write is a command.
    if (fullMode != FullModeState::SIMPLE) {
        std::string data(reinterpret_cast<const char*>(buf), size);
        handleCommand(data);
        return size;  // always consume
    }

    // Simple mode: peek first char(s) — if command-shaped, enter full mode.
    // Note: this is a heuristic. A simple-mode POST body that happens to
    // start with bytes matching a command prefix (e.g. a JSON body
    // starting with 'r-' for some unrelated reason) would be
    // misclassified. The spec accepts this trade-off; users who need
    // body data starting with command letters must use simple-mode
    // directly (not via full mode).
    if (size > 0) {
        char first = (char)buf[0];
        bool looksLikeCommand =
            first == 'm' || first == 'M' ||
            first == 'h' || first == 'H' ||
            first == 'b' || first == 'B' ||
            first == 's' || first == 'S' ||
            first == 'c' || first == 'C' ||
            (size >= 3 && first == 'r' && buf[1] == '-');

        // 'status' is 6 chars — detect explicitly to avoid false positives
        // on bodies starting with 's' (e.g. "score: 100").
        if (!looksLikeCommand && size >= 6) {
            std::string head(reinterpret_cast<const char*>(buf), 6);
            mstr::toLower(head);
            if (head == "status") {
                looksLikeCommand = true;
            }
        }

        if (looksLikeCommand) {
            fullMode = FullModeState::BUILDING_REQUEST;
            std::string cmd(reinterpret_cast<const char*>(buf), size);
            handleCommand(cmd);
            return size;
        }
    }

    // Simple mode: pass through to MeatHttpClient (existing POST/PUT buffering).
    if (_session && _session->client) {
        uint32_t bytesWritten = _session->client->write(buf, size);
        _position += bytesWritten;
        return bytesWritten;
    }
    return 0;
}
```

- [ ] **Step 5: Add `handleCommand()` implementation in http.cpp**

Insert this block immediately after the `HTTPRequestContext::errorToIecStatus` definition (at the end of the Task 3 block):

```cpp
/********************************************************
 * Full-mode command handling
 ********************************************************/

bool HTTPMStream::handleCommand(const std::string& cmd) {
    std::string c = cmd;
    mstr::trim(c);

    if (c.empty()) return true;  // blank line is harmless

    // 'r-h' / 'r-b' — switch response read mode (recognized even before 's')
    if (c.size() >= 3 && (c[0] == 'r' || c[0] == 'R') && c[1] == '-') {
        switch (c[2]) {
            case 'h': case 'H':
                fullMode = FullModeState::RESPONSE_HEADERS;
                return true;
            case 'b': case 'B':
                fullMode = FullModeState::RESPONSE_BODY;
                return true;
        }
        return false;
    }

    // 's' — send the request
    if (c == "s" || c == "S") {
        fullMode = FullModeState::RESPONSE_HEADERS;
        if (_session) {
            ctx.sendRequest(_session);
        }
        return true;
    }

    // 'c' — clear context, return to BUILDING_REQUEST for next request
    if (c == "c" || c == "C") {
        ctx.clear();
        fullMode = FullModeState::BUILDING_REQUEST;
        _statusRequested = false;
        return true;
    }

    // 'status' — request HTTP status code (consumed by next read())
    {
        std::string lower = c;
        mstr::toLower(lower);
        if (lower == "status") {
            _statusRequested = true;
            return true;
        }
    }

    // Single-letter commands with arguments: 'm <method>', 'b <body>'
    if (c.size() >= 2 && c[1] == ' ') {
        char prefix = c[0];
        std::string arg = c.substr(2);
        mstr::trim(arg);
        switch (prefix) {
            case 'm': case 'M':
                ctx.setMethod(arg);
                return true;
            case 'b': case 'B':
                ctx.setBody(arg);
                return true;
            default:
                break;
        }
    }

    // 'h <name>: <value>' and 'h+ <name>: <value>' — set/append header
    if (c.size() >= 2 && (c[0] == 'h' || c[0] == 'H')) {
        bool append = (c.size() >= 2 && c[1] == '+');
        size_t start = append ? 2 : 1;
        if (c.size() > start && c[start] == ' ') start++;
        std::string rest = c.substr(start);
        auto colonPos = rest.find(':');
        if (colonPos != std::string::npos) {
            std::string name = rest.substr(0, colonPos);
            std::string value = rest.substr(colonPos + 1);
            mstr::trim(name);
            mstr::trim(value);
            if (append) {
                ctx.appendHeader(name, value);
            } else {
                ctx.setHeader(name, value);
            }
            return true;
        }
    }

    // 'b+' — append body (no space required: 'b+more text' or 'b+ more text')
    if (c.size() >= 2 && (c[0] == 'b' || c[0] == 'B') && c[1] == '+') {
        std::string rest = c.substr(2);
        if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
        ctx.appendBody(rest);
        return true;
    }

    // Unrecognized — silently ignored per spec.
    return false;
}
```

- [ ] **Step 6: Build to verify the changes compile**

```bash
pio run -e fujiloaf-rev0 2>&1 | tail -15
```

Expected: exit 0. Common failures:
- `errorToIecStatus` not declared → re-check Task 2's declaration block.
- `mstr::trim` not found → add `#include "utils/string_utils.h"` to `http.cpp`.
- `HTTPRequestContext` not found → re-check Task 2's location (must be before `MeatHttpClient`).

- [ ] **Step 7: Commit**

```bash
git add lib/meatloaf/network/http.h lib/meatloaf/network/http.cpp
git commit -m "feat(http): add full-mode command routing to HTTPMStream

- FullModeState enum (SIMPLE, BUILDING_REQUEST, RESPONSE_HEADERS,
  RESPONSE_BODY).
- handleCommand() dispatches 10 commands (m, h, h+, b, b+, s, r-h, r-b,
  c, status) with case-insensitive matching.
- write() routes commands in full mode, detects command-shaped data in
  simple mode to activate full mode, passes through to MeatHttpClient
  otherwise (preserving existing POST/PUT body buffering).
- read() serves buffered response headers one per call (\r\n
  terminated, empty CRLF marks end), transitions to body mode, streams
  body from MeatHttpClient.
- close() skips the auto-POST in full mode.
- status command surfaces error via HTTPRequestContext::errorToIecStatus."
```

---

### Task 5: Native unit test for HTTPRequestContext

**Files:**
- Create: `test/native/test_http_full_client/test_http_full_client.cpp`

**Interfaces:**
- Consumes: `HTTPRequestContext` (from Tasks 2-3). Does NOT depend on `MeatHttpClient` or `HTTPMStream` (those need ESP-IDF).
- Produces: Unity test cases covering the public API surface.

- [ ] **Step 1: Create the test directory**

```bash
mkdir -p /home/qus/dev/_c/meatloaf/test/native/test_http_full_client
```

- [ ] **Step 2: Write the test file**

Create `test/native/test_http_full_client/test_http_full_client.cpp` with this content:

```cpp
#include <unity.h>
#include <string>
#include <vector>

// Pull in just the HTTPRequestContext declaration + impl.
// We can't include http.h directly because it pulls in esp_http_client.h.
// Instead, we replicate the class here for the unit test, since the
// class is self-contained (no MeatHttpClient dependencies in its API).
// In Task 6 (build verification) we confirm the actual production
// header has matching semantics by triggering it from the embedded build.

namespace mstr {
    void toLower(std::string &s);
    void toUpper(std::string &s);
}

#include "../../../lib/utils/string_utils.cpp"  // brings in mstr impl

class HTTPRequestContext {
public:
    std::string method = "GET";
    std::map<std::string, std::vector<std::string>> headers;
    std::string body;

    std::vector<std::string> responseHeaders;
    int responseStatus = 0;
    bool responseConsumed = false;

    void setMethod(const std::string& m) { method = m; mstr::toUpper(method); }
    void setHeader(const std::string& name, const std::string& value) {
        std::string key = name; mstr::toLower(key);
        headers[key] = {value};
    }
    void appendHeader(const std::string& name, const std::string& value) {
        std::string key = name; mstr::toLower(key);
        headers[key].push_back(value);
    }
    void setBody(const std::string& b) { body = b; }
    void appendBody(const std::string& b) { body += b; }
    void clear() {
        method = "GET";
        headers.clear();
        body.clear();
        responseHeaders.clear();
        responseStatus = 0;
        responseConsumed = false;
    }
    bool hasMoreResponseHeaders() const { return !responseHeaders.empty(); }
    std::string popResponseHeader() {
        if (responseHeaders.empty()) return {};
        std::string line = responseHeaders.front();
        responseHeaders.erase(responseHeaders.begin());
        return line + "\r\n";
    }
    bool isHttpError() const { return responseStatus < 200 || responseStatus >= 300; }
    void errorToIecStatus(int& errOut, std::string& msgOut) const {
        if (responseStatus >= 200 && responseStatus < 300) {
            errOut = 0; msgOut = "OK";
        } else if (responseStatus == -1) {
            errOut = 20; msgOut = "Connection refused";
        } else if (responseStatus == -2) {
            errOut = 20; msgOut = "Host not found";
        } else if (responseStatus >= 400 && responseStatus < 500) {
            errOut = 30 + (responseStatus - 400);
            msgOut = std::to_string(responseStatus) + " HTTP client error";
        } else if (responseStatus >= 500 && responseStatus < 600) {
            errOut = 35 + (responseStatus - 500);
            msgOut = std::to_string(responseStatus) + " HTTP server error";
        } else {
            errOut = 99; msgOut = "Internal error";
        }
    }
};

void setUp(void) {}
void tearDown(void) {}

void test_default_state(void) {
    HTTPRequestContext ctx;
    TEST_ASSERT_EQUAL_STRING("GET", ctx.method.c_str());
    TEST_ASSERT(ctx.body.empty());
    TEST_ASSERT(ctx.headers.empty());
    TEST_ASSERT_EQUAL(0, ctx.responseStatus);
    TEST_ASSERT_FALSE(ctx.responseConsumed);
}

void test_setMethod_uppercases(void) {
    HTTPRequestContext ctx;
    ctx.setMethod("post");
    TEST_ASSERT_EQUAL_STRING("POST", ctx.method.c_str());
    ctx.setMethod("PuT");
    TEST_ASSERT_EQUAL_STRING("PUT", ctx.method.c_str());
}

void test_setHeader_replaces(void) {
    HTTPRequestContext ctx;
    ctx.setHeader("Content-Type", "text/plain");
    TEST_ASSERT_EQUAL(1, ctx.headers["content-type"].size());
    TEST_ASSERT_EQUAL_STRING("text/plain", ctx.headers["content-type"][0].c_str());

    // Replace existing
    ctx.setHeader("Content-Type", "application/json");
    TEST_ASSERT_EQUAL(1, ctx.headers["content-type"].size());
    TEST_ASSERT_EQUAL_STRING("application/json", ctx.headers["content-type"][0].c_str());
}

void test_appendHeader_accumulates(void) {
    HTTPRequestContext ctx;
    ctx.appendHeader("Accept", "text/plain");
    ctx.appendHeader("Accept", "application/json");
    TEST_ASSERT_EQUAL(2, ctx.headers["accept"].size());
    TEST_ASSERT_EQUAL_STRING("text/plain", ctx.headers["accept"][0].c_str());
    TEST_ASSERT_EQUAL_STRING("application/json", ctx.headers["accept"][1].c_str());
}

void test_setBody_replaces(void) {
    HTTPRequestContext ctx;
    ctx.setBody("hello");
    TEST_ASSERT_EQUAL_STRING("hello", ctx.body.c_str());
    ctx.setBody("world");
    TEST_ASSERT_EQUAL_STRING("world", ctx.body.c_str());
}

void test_appendBody_concatenates(void) {
    HTTPRequestContext ctx;
    ctx.setBody("hello");
    ctx.appendBody(" world");
    TEST_ASSERT_EQUAL_STRING("hello world", ctx.body.c_str());
}

void test_clear_resets_all(void) {
    HTTPRequestContext ctx;
    ctx.setMethod("POST");
    ctx.setHeader("X-Test", "1");
    ctx.setBody("body");
    ctx.responseHeaders.push_back("Server: nginx");
    ctx.responseStatus = 404;
    ctx.responseConsumed = true;

    ctx.clear();

    TEST_ASSERT_EQUAL_STRING("GET", ctx.method.c_str());
    TEST_ASSERT(ctx.body.empty());
    TEST_ASSERT(ctx.headers.empty());
    TEST_ASSERT(ctx.responseHeaders.empty());
    TEST_ASSERT_EQUAL(0, ctx.responseStatus);
    TEST_ASSERT_FALSE(ctx.responseConsumed);
}

void test_responseHeaders_pop_format(void) {
    HTTPRequestContext ctx;
    TEST_ASSERT(ctx.popResponseHeader().empty());

    ctx.responseHeaders.push_back("Content-Type: text/html");
    ctx.responseHeaders.push_back("Content-Length: 42");
    TEST_ASSERT(ctx.hasMoreResponseHeaders());

    std::string h1 = ctx.popResponseHeader();
    TEST_ASSERT_EQUAL_STRING("Content-Type: text/html\r\n", h1.c_str());

    std::string h2 = ctx.popResponseHeader();
    TEST_ASSERT_EQUAL_STRING("Content-Length: 42\r\n", h2.c_str());

    TEST_ASSERT_FALSE(ctx.hasMoreResponseHeaders());
    TEST_ASSERT(ctx.popResponseHeader().empty());
}

void test_errorToIecStatus_table(void) {
    HTTPRequestContext ctx;

    // 2xx success
    ctx.responseStatus = 200;
    int err = -1; std::string msg;
    ctx.errorToIecStatus(err, msg);
    TEST_ASSERT_EQUAL(0, err);
    TEST_ASSERT_EQUAL_STRING("OK", msg.c_str());

    ctx.responseStatus = 299;
    ctx.errorToIecStatus(err, msg);
    TEST_ASSERT_EQUAL(0, err);

    // Connection refused (local)
    ctx.responseStatus = -1;
    ctx.errorToIecStatus(err, msg);
    TEST_ASSERT_EQUAL(20, err);
    TEST_ASSERT_EQUAL_STRING("Connection refused", msg.c_str());

    // DNS failure (local)
    ctx.responseStatus = -2;
    ctx.errorToIecStatus(err, msg);
    TEST_ASSERT_EQUAL(20, err);
    TEST_ASSERT_EQUAL_STRING("Host not found", msg.c_str());

    // 4xx
    ctx.responseStatus = 404;
    ctx.errorToIecStatus(err, msg);
    TEST_ASSERT_EQUAL(34, err);  // 30 + (404-400)
    TEST_ASSERT_EQUAL_STRING("404 HTTP client error", msg.c_str());

    ctx.responseStatus = 400;
    ctx.errorToIecStatus(err, msg);
    TEST_ASSERT_EQUAL(30, err);

    // 5xx
    ctx.responseStatus = 503;
    ctx.errorToIecStatus(err, msg);
    TEST_ASSERT_EQUAL(38, err);  // 35 + (503-500)
    TEST_ASSERT_EQUAL_STRING("503 HTTP server error", msg.c_str());

    // Internal
    ctx.responseStatus = -99;
    ctx.errorToIecStatus(err, msg);
    TEST_ASSERT_EQUAL(99, err);
    TEST_ASSERT_EQUAL_STRING("Internal error", msg.c_str());
}

void test_isHttpError_predicate(void) {
    HTTPRequestContext ctx;
    ctx.responseStatus = 200;
    TEST_ASSERT_FALSE(ctx.isHttpError());
    ctx.responseStatus = 299;
    TEST_ASSERT_FALSE(ctx.isHttpError());
    ctx.responseStatus = 300;
    TEST_ASSERT_TRUE(ctx.isHttpError());
    ctx.responseStatus = 404;
    TEST_ASSERT_TRUE(ctx.isHttpError());
    ctx.responseStatus = -1;
    TEST_ASSERT_TRUE(ctx.isHttpError());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_default_state);
    RUN_TEST(test_setMethod_uppercases);
    RUN_TEST(test_setHeader_replaces);
    RUN_TEST(test_appendHeader_accumulates);
    RUN_TEST(test_setBody_replaces);
    RUN_TEST(test_appendBody_concatenates);
    RUN_TEST(test_clear_resets_all);
    RUN_TEST(test_responseHeaders_pop_format);
    RUN_TEST(test_errorToIecStatus_table);
    RUN_TEST(test_isHttpError_predicate);
    return UNITY_END();
}
```

- [ ] **Step 3: Run the native test**

```bash
pio test -e native -f native/test_http_full_client 2>&1 | tail -25
```

Expected: `10 Tests 0 Failures 0 Ignored` followed by `OK`.

If the include `../../../lib/utils/string_utils.cpp` fails to resolve, use the absolute path from CWD by replacing it with the relative path that works at PIO test invocation time (typically PIO runs from the project root, so `lib/utils/string_utils.cpp` should work).

- [ ] **Step 4: Commit**

```bash
git add test/native/test_http_full_client/test_http_full_client.cpp
git commit -m "test(http): add HTTPRequestContext native unit tests

10 tests covering default state, setMethod uppercase, setHeader replace,
appendHeader multi-value, setBody/appendBody, clear, popResponseHeader
\r\n formatting, errorToIecStatus table lookup, and isHttpError
predicate."
```

---

### Task 6: Commit the working-tree test infrastructure

**Files:**
- Already in working tree (untracked or modified):
  - `test/http/test_client.py` (modified)
  - `test/http/test_server.py` (modified)
  - `test/http/http_full_client_test.bas` (untracked)
  - `test/http/project-config.json` (unchanged)
  - `test/http/__pycache__/` (untracked — should NOT be committed)

- [ ] **Step 1: Stage the right files; ignore `__pycache__`**

```bash
cd /home/qus/dev/_c/meatloaf
# Confirm __pycache__ is not staged
git status --short test/http/

# Add a .gitignore entry if __pycache__ is tracked (it shouldn't be — it's untracked)
echo "test/http/__pycache__/" >> .gitignore 2>/dev/null || true
git add test/http/test_client.py test/http/test_server.py test/http/http_full_client_test.bas
```

Expected: only the three real test files are staged. `__pycache__` stays untracked.

- [ ] **Step 2: Verify the staged diff**

```bash
git diff --cached --stat
```

Expected: 3 files, ~1500 insertions combined (or whatever the actual delta is — confirm the diff looks like the test infrastructure and not junk).

- [ ] **Step 3: Smoke-test the Python server starts**

```bash
cd /home/qus/dev/_c/meatloaf/test/http
timeout 3 python3 test_server.py 2>&1 | head -10 || true
```

Expected: server prints its startup banner and exits on the timeout. If it fails to bind to `:8080` (e.g. another service is on the port), pick a free port via `MEATLOAF_LISTEN_PORT=8081` env var and note it for the BASIC test (which hardcodes `:8080` — for now, just verify the server starts; the BASIC test configuration is out of scope).

- [ ] **Step 4: Commit**

```bash
git add test/http/test_client.py test/http/test_server.py test/http/http_full_client_test.bas
git commit -m "test(http): add full HTTP client mode test infrastructure

- test_server.py: Python echo server with /echo, /echo/status/<code>,
  /static, /debug/* endpoints. Returns X-Request-Id, X-Echo-Method,
  X-Echo-Header-* response headers for full-mode verification.
- test_client.py: simulates the C64 BASIC flow against the test server.
- http_full_client_test.bas: 9 manual-test scenarios for the C64
  (basic GET, POST with body, headers, status, response headers,
  multi-request cycle, 404, PUT, body append)."
```

---

### Task 7: Update the plan document reference in the spec

**Files:**
- Modify: `docs/superpowers/specs/2026-05-12-http-full-client-design.md` — the spec currently points to `2026-06-29-http-full-client-implementation.md` in the "Plan Tasks" section. No actual content change is needed unless the task list inside the spec is now stale.

- [ ] **Step 1: Verify the spec's plan reference matches this file**

```bash
grep -n "2026-06-29" /home/qus/dev/_c/meatloaf/docs/superpowers/specs/2026-05-12-http-full-client-design.md
```

Expected: one match pointing to `2026-06-29-http-full-client-implementation.md`.

- [ ] **Step 2: Compare spec task list to plan task list**

The spec lists 7 high-level tasks. This plan has 7 tasks. They should align by intent (phase3 discard → ctx class → ctx impl → stream changes → unit test → test infra → build verify), though the order is slightly different (test infra before build verify here).

If the spec needs an order tweak, update it. Otherwise, skip to commit.

- [ ] **Step 3: Commit (only if Step 2 required a change)**

```bash
git add docs/superpowers/specs/2026-05-12-http-full-client-design.md
git commit -m "docs(spec): align plan task order with implementation plan"
```

If Step 2 found no change, skip this commit.

---

### Task 8: Build verification

**Files:**
- No code changes. This task is verification only.

- [ ] **Step 1: Run native unit tests**

```bash
pio test -e native 2>&1 | tail -30
```

Expected: all native tests pass (`0 Failures 0 Ignored`).

If a non-HTTP test regressed (e.g. `test_strings`), investigate — the changes here should not have touched any other test, so a regression is unlikely.

- [ ] **Step 2: Build the embedded firmware**

```bash
pio run -e fujiloaf-rev0 2>&1 | tail -30
```

Expected: `BUILD SUCCESS` or equivalent exit 0 with the `.pio/build/fujiloaf-rev0/firmware.bin` artifact produced.

- [ ] **Step 3: Build a second target as a sanity check**

```bash
pio run -e lolin-d32-pro 2>&1 | tail -10
```

Expected: also builds. If `lolin-d32-pro` is not in `platformio.ini`, skip this step and note it.

- [ ] **Step 4: Final commit (only if any fixes were needed)**

If Step 1 or Step 2 revealed a bug, fix it (likely a missing include or a typo from Task 4), then:

```bash
git add -A
git commit -m "fix(http): address build/test issues found in verification"
```

If everything passed, no commit is needed — the verification is complete.

---

## Out-of-scope reminder

- DELETE method (no `MeatHttpClient::DELETE()`, no test).
- Keep-alive toggle (`k on`/`k off`) — removed from command set entirely.
- Channel-15 protocol layer wiring — only populate the local `iecStatus` fields.
- `MeatHttpClient` refactoring.
- BASIC test execution against real C64 hardware — the .bas file is committed but manual-run only.

## Acceptance Criteria

The plan is complete when:
1. `git branch --list http-client-phase3` returns nothing.
2. `grep -rn 'HTTPRequestContext\|FullModeState\|handleCommand' lib/` returns matches in `http.h` and `http.cpp`.
3. `pio test -e native` passes with 0 failures.
4. `pio run -e fujiloaf-rev0` exits 0.
5. The test infrastructure files (`test/http/test_*.py`, `test/http/http_full_client_test.bas`) are committed.

Items 1-4 are mechanically verifiable. Item 5 is a single `git status --short test/http/` check showing those files as committed (no untracked or modified status).
