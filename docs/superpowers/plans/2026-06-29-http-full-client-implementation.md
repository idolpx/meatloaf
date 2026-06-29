# Full HTTP Client Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add full HTTP client capabilities (REST/GraphQL APIs) to HTTPMStream so C64 BASIC programs can set method, headers, and body via PRINT# commands, then read response headers and body via GET#.

**Architecture:** A new `HTTPRequestContext` class holds request-building state (method, headers, body) and response state (status, response headers). It lives in the existing `http.h`/`http.cpp`. `HTTPMStream` gains a `fullMode` flag and command routing in `write()`: unrecognized data in simple mode passes through unchanged (backward compatible), while commands (m, h, b, s, r-h, r-b, c, k, status) activate full mode and control the context. `sendRequest()` drives `MeatHttpClient` to execute the built request. Response headers are captured via the existing `onHeader` callback and buffered in `HTTPRequestContext`, served line-by-line via `read()` when in `RESPONSE_HEADERS` mode.

**Tech Stack:** C++17, ESP-IDF (custom `fn_esp_http_client` fork), FreeRTOS task notifications for inter-task streaming.

**Spec:** `docs/superpowers/specs/2026-05-12-http-full-client-design.md`

## Global Constraints

- All new code goes in existing files: `lib/meatloaf/network/http.h` (declarations) and `lib/meatloaf/network/http.cpp` (implementations).
- Command parsing is case-insensitive.
- Non-command data in full mode is silently ignored.
- Response headers are `\r\n` terminated (C64 `GET#` requirement).
- EOI (bit 6 of ST) sent after entire body transmitted; error (bit 7) when `responseStatus` is non-zero.
- Error codes use the existing `NETWORK_ERROR_*` constants from `lib/network-protocol/status_error_codes.h`.
- Commands arrive via `HTTPMStream::write()` — the C64's `PRINT#` writes to the stream.
- Response headers are captured using `MeatHttpClient::setOnHeader()` callback; request headers are set via `MeatHttpClient::setHeader("Name: value")`.

---

### Task 1: HTTPRequestContext class

**Files:**
- Modify: `lib/meatloaf/network/http.h` — add class declaration after `#include` block, before `MeatHttpClient`
- Modify: `lib/meatloaf/network/http.cpp` — add method implementations (placement: near the top, before `HTTPMSession` impl)

**Interfaces:**
- Consumes: (none — standalone helper class)
- Produces: `class HTTPRequestContext` with the public API below

- [ ] **Step 1: Add the class declaration to http.h**

After the `#include` block (around line 55, before `MeatHttpClient`), add:

```cpp
/********************************************************
 * HTTP Request Context — full-mode request builder
 ********************************************************/

class HTTPRequestContext {
public:
    // Request state
    std::string method = "GET";                              // GET, POST, PUT, DELETE, HEAD, etc.
    std::map<std::string, std::vector<std::string>> headers; // multi-value headers
    std::string body;                                        // request body

    // Response state
    std::vector<std::string> responseHeaders;                // buffered response header lines ("Name: value")
    int responseStatus = 0;                                  // 0 = success, <0 = local err, >0 = HTTP status
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
};
```

- [ ] **Step 2: Implement HTTPRequestContext methods in http.cpp**

Add after the global constants at the top of http.cpp (after `const char *webdav_depths[]` line ~21):

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
        responseStatus = -1;
        return false;
    }

    auto& client = *session->client;

    // Reset response header buffer
    responseHeaders.clear();

    // Set up onHeader callback to capture response headers
    // The callback is called during HTTP_EVENT_ON_HEADER with key and value
    client.setOnHeader([this](char* key, char* value) -> int {
        if (key && value) {
            std::string headerLine = std::string(key) + ": " + std::string(value);
            responseHeaders.push_back(headerLine);
            Debug_printv("Captured response header: %s", headerLine.c_str());
        }
        return 0;
    });

    // Apply request headers to MeatHttpClient's header map
    for (const auto& [key, values] : headers) {
        for (const auto& val : values) {
            std::string headerLine = key + ":" + val;
            client.setHeader(headerLine);
        }
    }

    // Set body if present
    if (!body.empty()) {
        client.postBuffer.clear();
        client.postBuffer.insert(client.postBuffer.end(), body.begin(), body.end());
    }

    // Determine method and send
    bool result = false;
    if (method == "GET") {
        result = client.GET(client.url);
    } else if (method == "POST") {
        result = client.POST(client.url);
        // For POST, close() sends the buffered body and captures response
        if (result && !client.postBuffer.empty()) {
            client.close();
            // After close, preservedPostResponse holds the body
            responseStatus = client.lastRC;
        }
    } else if (method == "PUT") {
        result = client.PUT(client.url);
        if (result && !client.postBuffer.empty()) {
            client.close();
            responseStatus = client.lastRC;
        }
    } else if (method == "HEAD") {
        result = client.HEAD(client.url);
    } else {
        Debug_printv("Unsupported method: %s", method.c_str());
        responseStatus = -1;
        return false;
    }

    // Capture response status from the perform result
    if (responseStatus == 0) {
        responseStatus = client.lastRC;
    }

    return result;
}

std::string HTTPRequestContext::popResponseHeader() {
    if (responseHeaders.empty()) return {};
    std::string line = responseHeaders.front();
    responseHeaders.erase(responseHeaders.begin());
    return line + "\r\n";
}
```

- [ ] **Step 3: Commit**

```bash
git add lib/meatloaf/network/http.h lib/meatloaf/network/http.cpp
git commit -m "feat: add HTTPRequestContext class for full HTTP client mode"
```

---

### Task 2: Command routing in HTTPMStream + full mode activation

**Files:**
- Modify: `lib/meatloaf/network/http.h` — add `enum class FullModeState`, command-related members to `HTTPMStream`
- Modify: `lib/meatloaf/network/http.cpp` — modify `HTTPMStream::write()` to detect and route commands

**Interfaces:**
- Consumes: `HTTPRequestContext` from Task 1
- Produces: updated `HTTPMStream` with `write()` that routes commands

- [ ] **Step 1: Add new members to HTTPMStream in http.h**

Replace the existing `HTTPMStream` class in http.h (lines 244-304) with the enhanced version:

```cpp
class HTTPMStream: public MStream {
public:
    enum class FullModeState {
        SIMPLE,             // backward-compatible mode, no commands active
        BUILDING_REQUEST,   // full mode activated, accumulating commands
        RESPONSE_HEADERS,   // request sent, reading response headers
        RESPONSE_BODY       // reading response body
    };

    HTTPMStream(std::string path): MStream(path) {};
    HTTPMStream(std::string path, std::ios_base::openmode m): MStream(path) {
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
        // Check if we have POST response data to read
        if (_session && _session->client && !_session->client->postResponse.empty()) {
            uint32_t respAvail = (uint32_t)_session->client->postResponse.size() - _session->client->_position;
            if (respAvail > 0) return respAvail;
        }
        // Check if we have response headers buffered
        if (fullMode == FullModeState::RESPONSE_HEADERS && ctx.hasMoreResponseHeaders()) {
            return 1;  // at least one header line available
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
    bool keepAlive = true;
    bool _statusRequested = false;  // set by handleCommand when "status" received
};
```

- [ ] **Step 2: Add `handleCommand()` implementation in http.cpp**

Add after the `HTTPRequestContext` implementation (after `popResponseHeader()`):

```cpp
/********************************************************
 * Full-mode command handling
 ********************************************************/

bool HTTPMStream::handleCommand(const std::string& cmd) {
    Debug_printv("handleCommand: %s", cmd.c_str());

    // Trim trailing whitespace/newlines that C64 PRINT# may add
    std::string c = cmd;
    mstr::trim(c);

    if (c.empty()) return true;  // blank line is harmless

    // Activate full mode on first command
    if (fullMode == FullModeState::SIMPLE) {
        fullMode = FullModeState::BUILDING_REQUEST;
    }

    // "r-h" or "r-b" — switch response read mode
    if (c.size() >= 3 && c[0] == 'r' && c[1] == '-') {
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

    // "s" — send the request
    if (c == "s" || c == "S") {
        fullMode = FullModeState::RESPONSE_HEADERS;
        if (_session) {
            ctx.sendRequest(_session);
        }
        return true;
    }

    // "c" — clear context
    if (c == "c" || c == "C") {
        ctx.clear();
        fullMode = FullModeState::BUILDING_REQUEST;
        _statusRequested = false;
        return true;
    }

    // "status" — request HTTP status code
    if (c.size() >= 6 && (c[0] == 's' || c[0] == 'S') &&
        (c == "status" || c == "STATUS")) {
        _statusRequested = true;
        return true;
    }

    // "k on" / "k off" — keep-alive
    if (c.size() >= 3 && (c[0] == 'k' || c[0] == 'K') && c[1] == ' ') {
        std::string val = c.substr(2);
        mstr::trim(val);
        keepAlive = (val == "on" || val == "ON" || val == "1");
        return true;
    }

    // Single-letter commands with arguments: "m <method>", "b <body>"
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

    // "h <name>: <value>" and "h+ <name>: <value>" — set/append header
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

    // "b+" — append body (no space required: "b+more text" or "b+ more text")
    if (c.size() >= 2 && c[0] == 'b' && c[1] == '+') {
        std::string rest = c.substr(2);
        if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
        ctx.appendBody(rest);
        return true;
    }

    Debug_printv("Unrecognized command in full mode: %s", c.c_str());
    return false;  // unknown command, silently ignored
}
```

- [ ] **Step 3: Modify HTTPMStream::write() to route commands vs raw data**

Replace the current `HTTPMStream::write()` (lines 356-364) with:

```cpp
uint32_t HTTPMStream::write(const uint8_t *buf, uint32_t size) {
    Debug_printv("HTTPMStream::write called, size=%u, fullMode=%d, client=%p",
        size, (int)fullMode, _session ? _session->client.get() : nullptr);

    // In full mode, all writes are commands
    if (fullMode != FullModeState::SIMPLE) {
        std::string data(reinterpret_cast<const char*>(buf), size);
        handleCommand(data);
        return size;  // always consume the data (even if unrecognized)
    }

    // Simple mode: check if incoming data looks like a command
    if (size > 0) {
        char first = (char)buf[0];
        if (first == 'm' || first == 'M' ||
            first == 'h' || first == 'H' ||
            first == 'b' || first == 'B' ||
            first == 's' || first == 'S' ||
            first == 'c' || first == 'C' ||
            first == 'k' || first == 'K' ||
            (size >= 2 && first == 'r' && (buf[1] == '-'))) {
            // First command detected — enter full mode
            fullMode = FullModeState::BUILDING_REQUEST;
            std::string cmd(reinterpret_cast<const char*>(buf), size);
            handleCommand(cmd);
            return size;
        }
    }

    // Simple mode: pass through to MeatHttpClient (original behavior — POST/PUT body buffering)
    if (_session && _session->client) {
        uint32_t bytesWritten = _session->client->write(buf, size);
        _position += bytesWritten;
        return bytesWritten;
    }
    return 0;
}
```

- [ ] **Step 4: Commit**

```bash
git add lib/meatloaf/network/http.h lib/meatloaf/network/http.cpp
git commit -m "feat: add command routing in HTTPMStream::write for full HTTP client mode"
```

---

### Task 3: Response header reading mode + response body mode

**Files:**
- Modify: `lib/meatloaf/network/http.cpp` — modify `HTTPMStream::read()` to serve headers when in RESPONSE_HEADERS mode and body in RESPONSE_BODY mode

**Interfaces:**
- Consumes: `HTTPRequestContext::responseHeaders` vector (populated by `sendRequest()` via `onHeader` callback)
- Produces: headers served one per `read()` call, `\r\n` terminated, end marked by empty line; body served from `MeatHttpClient`

- [ ] **Step 1: Modify HTTPMStream::read() for HEADERS and BODY modes**

Replace `HTTPMStream::read()` (lines 306-354) with:

```cpp
uint32_t HTTPMStream::read(uint8_t* buf, uint32_t size) {
    uint32_t bytesRead = 0;

    if ( size > 0 )
    {
        // Full mode: handle "status" command — return status code string
        if (_statusRequested) {
            _statusRequested = false;
            std::string statusStr = std::to_string(ctx.responseStatus);
            uint32_t copyLen = std::min((uint32_t)statusStr.size(), size);
            memcpy(buf, statusStr.data(), copyLen);
            Debug_printv("Returning status: %s", statusStr.c_str());
            return copyLen;
        }

        // Full mode: RESPONSE_HEADERS — serve buffered header lines
        if (fullMode == FullModeState::RESPONSE_HEADERS) {
            std::string headerLine;
            if (ctx.hasMoreResponseHeaders()) {
                headerLine = ctx.popResponseHeader();
            } else {
                // End-of-headers marker: empty CRLF line
                headerLine = "\r\n";
                // Transition to body mode automatically
                fullMode = FullModeState::RESPONSE_BODY;
            }
            uint32_t copyLen = std::min((uint32_t)headerLine.size(), size);
            memcpy(buf, headerLine.data(), copyLen);
            _error = 0;
            Debug_printv("Serving header line: %s", headerLine.c_str());
            return copyLen;
        }

        // Full mode: RESPONSE_BODY — read from MeatHttpClient
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

        // Simple mode: original behavior (write-mode POST handling)
        bool isWriteMode = (mode & 0x10) || (mode == std::ios_base::out);
        if (isWriteMode && _session && _session->client) {
            Debug_printv("Switching from write mode to read mode");
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

        if ( size > available() )
            size = available();

        if ( size > 0 )
        {
            bytesRead = _session->client->read(buf, size);
            _position += bytesRead;
            _error = _session->client->_error;
        }
    }

    return bytesRead;
};
```

- [ ] **Step 2: Commit**

```bash
git add lib/meatloaf/network/http.cpp
git commit -m "feat: add response header reading mode in HTTPMStream::read"
```

---

### Task 4: Full-mode close() and keep-alive handling

**Files:**
- Modify: `lib/meatloaf/network/http.cpp` — modify `HTTPMStream::close()` to handle full-mode cleanup gracefully

- [ ] **Step 1: Modify HTTPMStream::close() for full mode**

Replace the current `HTTPMStream::close()` (lines 275-292) with:

```cpp
void HTTPMStream::close() {
    Debug_printv("HTTPMStream::close called, mode=%d, fullMode=%d", mode, (int)fullMode);

    // In full mode, don't trigger automatic POST send — that's for simple mode only
    if (fullMode != FullModeState::SIMPLE) {
        Debug_printv("Full mode close (state=%d), skipping automatic POST send", (int)fullMode);
        if (_session) {
            _session->releaseIO();
        }
        fullMode = FullModeState::SIMPLE;
        ctx.clear();
        return;
    }

    // Simple mode: original behavior
    bool isWriteMode = (mode & 0x10) || (mode == std::ios_base::out)
        || (mode == (std::ios_base::in | std::ios_base::out));
    if (isWriteMode && _session && _session->client) {
        Debug_printv("Closing write-mode stream, sending POST request");
        auto client = _session->client.get();
        client->close();
    }
    if (_session) {
        _session->releaseIO();
    }
}
```

- [ ] **Step 2: Commit**

```bash
git add lib/meatloaf/network/http.cpp
git commit -m "feat: handle full-mode close() and keep-alive in HTTPMStream"
```

---

### Task 5: Add test for command routing (native test environment)

**Files:**
- Create: `test/native/test_http_full_client/test_http_full_client.cpp`
- Modify: `platformio.ini` — ensure test filter includes `native/*`

**Interfaces:**
- Consumes: `HTTPRequestContext` and `HTTPMStream` APIs

- [ ] **Step 1: Verify test infrastructure exists**

```bash
ls test/native/test_*/test_*.cpp 2>/dev/null
```
Check for existing native test patterns to follow.

- [ ] **Step 2: Write command parsing test**

Create `test/native/test_http_full_client/test_http_full_client.cpp`:

```cpp
#include <unity.h>
#include <string>
#include "http.h"

void test_command_parsing(void) {
    // Test handleCommand() parsing logic by constructing a minimal test
    // Since HTTPMStream depends on session/client, test the context class
    HTTPRequestContext ctx;

    // Default state
    TEST_ASSERT_EQUAL_STRING("GET", ctx.method.c_str());
    TEST_ASSERT(ctx.body.empty());

    // setMethod
    ctx.setMethod("POST");
    TEST_ASSERT_EQUAL_STRING("POST", ctx.method.c_str());

    // setHeader replaces
    ctx.setHeader("Content-Type", "application/json");
    TEST_ASSERT(ctx.headers["content-type"].size() == 1);
    TEST_ASSERT_EQUAL_STRING("application/json", ctx.headers["content-type"][0].c_str());

    // appendHeader adds
    ctx.appendHeader("Accept", "text/plain");
    ctx.appendHeader("Accept", "application/json");
    TEST_ASSERT(ctx.headers["accept"].size() == 2);

    // setBody
    ctx.setBody("{\"key\":\"value\"}");
    TEST_ASSERT_EQUAL_STRING("{\"key\":\"value\"}", ctx.body.c_str());

    // appendBody
    ctx.appendBody(" more");
    TEST_ASSERT_EQUAL_STRING("{\"key\":\"value\"} more", ctx.body.c_str());

    // clear
    ctx.clear();
    TEST_ASSERT_EQUAL_STRING("GET", ctx.method.c_str());
    TEST_ASSERT(ctx.body.empty());
    TEST_ASSERT(ctx.headers.empty());
    TEST_ASSERT(ctx.responseHeaders.empty());
    TEST_ASSERT_EQUAL(0, ctx.responseStatus);
    TEST_ASSERT(!ctx.responseConsumed);
}

void test_response_headers(void) {
    HTTPRequestContext ctx;

    // popResponseHeader on empty list returns empty string
    TEST_ASSERT(ctx.popResponseHeader().empty());

    // Add some headers
    ctx.responseHeaders.push_back("Content-Type: text/html");
    ctx.responseHeaders.push_back("Content-Length: 42");

    TEST_ASSERT(ctx.hasMoreResponseHeaders());

    std::string h1 = ctx.popResponseHeader();
    TEST_ASSERT_EQUAL_STRING("Content-Type: text/html\r\n", h1.c_str());

    std::string h2 = ctx.popResponseHeader();
    TEST_ASSERT_EQUAL_STRING("Content-Length: 42\r\n", h2.c_str());

    TEST_ASSERT(!ctx.hasMoreResponseHeaders());
    TEST_ASSERT(ctx.popResponseHeader().empty());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_command_parsing);
    RUN_TEST(test_response_headers);
    return UNITY_END();
}
```

- [ ] **Step 3: Create test CMakeLists.txt if needed**

Check if native tests use platform.ini build system or CMake. Create if needed:
```
# test/native/test_http_full_client/ — tests build automatically via platformio test filter
```

- [ ] **Step 4: Run test to see it compile (may fail to link due to ESP-IDF deps)**

```bash
pio test -e native -f "native/test_http_full_client" 2>&1 | tail -20
```

If the test can't link due to ESP-IDF dependencies (expected), verify at least that the header compiles:
```bash
pio run -e native -t preprocess 2>&1 | grep -i "HTTPRequestContext\|error" | head -10
```

- [ ] **Step 5: Commit**

```bash
git add test/native/test_http_full_client/
git commit -m "test: add HTTPRequestContext unit tests for full HTTP client mode"
```

---

### Task 6: Build and compile check

**Files:**
- Build: verify all changes compile for both native and embedded targets

- [ ] **Step 1: Build native environment (if tests link)**

```bash
pio test -e native 2>&1 | tail -30
```

- [ ] **Step 2: Build embedded firmware**

```bash
pio run -e fujiloaf-rev0 2>&1 | tail -30
```

- [ ] **Step 3: Fix any compilation errors**

Address missing includes (e.g. `#include "network-protocol/status_error_codes.h"` in http.cpp if needed), forward declarations, or type mismatches.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "fix: address compilation issues for full HTTP client mode"
```
