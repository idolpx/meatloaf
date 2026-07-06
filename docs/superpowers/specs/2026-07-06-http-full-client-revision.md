# Full HTTP Client Mode for Meatloaf — Revision 2026-07-06

## Status

Revision to the 2026-05-12 design, based on real-hardware testing with a C64 via the IEC serial bus. The original design was correct in API but missing critical timing constraints discovered during integration testing.

## Key Discovery: IEC Bus Timing Constraint

The IEC serial bus requires the drive (ESP32) to **acknowledge every byte within ~1ms**. The C64's LISTEN handshake waits for the drive to pull the CLOCK line low after each data byte. If the drive doesn't respond in time, the C64 raises `?DEVICE NOT PRESENT`.

Blocking TCP operations that exceed this window:
- TCP connect: 50-200ms
- SSL handshake: 200-500ms
- `esp_http_client_perform()` (POST body send + response receive): 100-500ms
- `esp_http_client_cleanup()` + re-init: 50-100ms

**Conclusion**: All blocking TCP work must complete **outside** the IEC write handler.

## Data Flow Architecture (Changed)

The original spec described a single-phase flow where `handleCommand("s")` called `sendRequest()` immediately. This blocks too long for POST/PUT with bodies.

The revised design splits each request into two phases:

```
C64: PRINT#1,"m post"       ─→  write()  ─→  _cmdLine buffering ─→ handleCommand("m post")
C64: PRINT#1,"h content-..." ─→  write()  ─→  _cmdLine buffering ─→ handleCommand("h ...")
C64: PRINT#1,"b {body}"     ─→  write()  ─→  _cmdLine buffering ─→ handleCommand("b {body}")
C64: PRINT#1,"s"            ─→  write()  ─→  _cmdLine buffering ─→ handleCommand("s")
                                    [fast phase]                              sets _queuedSend=true, returns
                                                                              
C64: GET#1,A$               ─→  read()   ─→  execute queued sendRequest(session)
                                    [slow phase — TCP work here, safe timing]
                                          ─→  serve "200\r"
                                          ─→  serve "Server: ...\r"
                                          ─→  serve "\r" (end of headers)
                                          ─→  serve body bytes
```

### Fast Phase (write handler, ~1ms max)
- Line-buffer incoming bytes in `_cmdLine` — IEC delivers `"m post\r"` as 7 single-byte writes
- Dispatch complete lines to `handleCommand()` on `\r`
- All commands (`m`, `h`, `h+`, `b`, `b+`, `status`, `c`, `r-h`, `r-b`) execute immediately — they only set string/map/enum state
- `s` command: sets `_queuedSend = true` — **no TCP work**

### Slow Phase (read handler, relaxed timing)
- First `read()` after `_queuedSend`: executes `sendRequest()`, POST/PUT `perform()`, captures response
- Serve status (`\r`-terminated decimal from `ctx.responseStatus`)
- Serve headers one per `read()` via `popResponseHeader()`, each `"\r"`-terminated
- Auto-transition to `RESPONSE_BODY` after empty header line
- Serve body bytes from `preservedPostResponse` or `esp_http_client_read()`

### IEC Buffer Invalidation (New)
The IEC channel handler has an internal buffer (`m_data[]`) that pre-fetches stream data on `readBufferData()`. After header reading, this buffer contains stale header bytes. When entering `RESPONSE_BODY`, the first call to `read()` returns 0 — this forces `readBufferData()` to re-fill the buffer from fresh stream body data.

## State Machine (Unchanged)

```
SIMPLE → (first command byte) → BUILDING_REQUEST
BUILDING_REQUEST → (s) → RESPONSE_HEADERS       [queued, not executed]
[first read() executes queued sendRequest()]
RESPONSE_HEADERS → (auto: empty header line) → RESPONSE_BODY
RESPONSE_BODY → (c) → BUILDING_REQUEST
```

The auto-transition from RESPONSE_HEADERS to RESPONSE_BODY happens when `popResponseHeader()` returns empty (no more headers). A `"\r"`-only line is served to the C64, and `_queuedFlush` is set.

## Changes from Original Design

### What stayed the same
- FullModeState enum values
- Command set and syntax
- HTTPRequestContext API
- Error routing table
- Backward compatibility rules
- Test suite structure (9 scenarios)

### What changed
| Area | Original | Revised |
|------|----------|---------|
| `s` command timing | Immediate `sendRequest()` | Set `_queuedSend`, deferred to `read()` |
| POST/PUT body send | `client.close()` in `sendRequest()` | `client._performPending = true` (deferred) |
| Header line terminator | `"\r\n"` | `"\r"` (prevents `\n` orphan) |
| End-of-headers marker | `"\r\n"` | `"\r"` (consistency, prevents `\n` leak) |
| Status string | `to_string(status)` | `to_string(status) + "\r"` (C64 terminates on CR) |
| IEC write handling | String-based `handleCommand(data)` | Byte-buffered via `_cmdLine`, dispatch on `\r` |
| State isolation | None | Clear `preservedPostResponse` on each `sendRequest()` |
| Body read hang | None | Return 0 from `available()` when `responseConsumed` |

### New flags in HTTPMStream
```cpp
std::string _cmdLine;          // IEC byte accumulator, dispatched on \r
bool _queuedSend = false;      // "s" received, sendRequest() pending
```

### New flag in MeatHttpClient
```cpp
bool _performPending = false;  // POST/PUT body send deferred to read()
```

### complete() guard (MeatHttpClient)
```cpp
bool complete() {
    if (_http == nullptr) {
        if (!preservedPostResponse.empty())
            return _position >= (uint32_t)preservedPostResponse.size();
        return true;
    }
    return esp_http_client_is_complete_data_received(_http);
}
```

### available() guard (HTTPMStream)
```cpp
uint32_t available() override {
    // ... existing checks ...
    if (fullMode == FullModeState::RESPONSE_BODY && ctx.responseConsumed)
        return 0;
    // ...
}
```

## File-by-File Change Summary

### lib/meatloaf/network/http.h
- Add `_cmdLine`, `_queuedSend` members to `HTTPMStream`
- Guard `complete()` for null `_http`
- Guard `available()` for consumed RESPONSE_BODY

### lib/meatloaf/network/http.cpp
- `sendRequest()`: add `preservedPostResponse.clear()` before dispatch
- `sendRequest()`: replace `client.close()` with `client._performPending = true` for POST/PUT
- `handleCommand("s")`: set `_queuedSend = true` instead of immediate `sendRequest()`
- `handleCommand("r-b")`: reset positions, set `_queuedFlush`
- `popResponseHeader()`: return `"\r"` terminator, not `"\r\n"`
- `read()`: execute queued send at top (before status/headers/body dispatch)
- `read()`: flush return-0 when entering body mode
- `read()`: status with `\r` terminator
- End-of-headers: `"\r"` marker + `_position` reset
- `write()`: line-buffer in `_cmdLine`, dispatch on `\r`
- Simple-mode command detection: buffer into `_cmdLine`, don't dispatch immediately

### lib/device/iec/drive.h
- `iecChannelHandler::write()`: add `virtual`
- `iecChannelHandlerFile::write()`: add override declaration

### lib/device/iec/drive.cpp
- `iecDrive::open()`: detect `"://"` in filename → set `mode = in | out`
- `iecChannelHandlerFile::write()`: override that forwards to `m_stream->write()` when stream has `out` mode

### test/http/http_full_client_test.bas
- Body read helper: 250-byte guard with `bc` counter
- Test 2: fix `nd$="POST"` before `gosub 1903`
- Header helper: always drain to empty line (don't exit early at 230-char limit)

## Implementation Tasks

1. Apply all changes to `http.h` (flags, guards)
2. Apply all changes to `http.cpp` (queue/execute, line buffering, terminators)
3. Apply `virtual` keyword and override to `drive.h`
4. Apply network URL mode and write forwarding to `drive.cpp`
5. Apply BASIC test fixes
6. Build and flash
7. Run all 9 tests on U64 hardware
8. Verify each test passes individually

Execution method: direct implementation (not subagent — too many interdependent file changes).
