# Debugging Meatloaf Full HTTP Client

## Triple-Setup Architecture

```
┌────────────────┐      IEC         ┌──────────────┐       HTTP        ┌─────────────┐
│  C64 running   │ ◄──serial bus──► │  Meatloaf    │ ◄──────────────► │  Test server │
│  BASIC test    │                  │  (ESP32)     │                   │  (Python)    │
│                │                  │  UART@2M     │                   │  port 8080   │
└────────────────┘                  └──────┬───────┘                   └─────────────┘
                                           │ USB serial
                                           ▼
                                    ┌──────────────┐
                                    │  Serial      │
                                    │  capture     │
                                    │  (this PC)   │
                                    └──────────────┘
```

## Setup Commands (copy-paste in order)

### 1. Start Test Server
```bash
python3 test/http/test_server.py &
```

### 2. Enable verbose HTTP debug (if not already on)
In `platformio.ini`, uncomment:
```
-D VERBOSE_HTTP
```

### 3. Build and flash
```bash
pio run -e fujiloaf-rev0 -t upload --upload-port /dev/ttyUSB0
```
Wait for "Hard resetting via RTS pin..." — ESP reboots with new firmware.

### 4. Start serial capture (run these one by one)
```bash
# Kill stale captures
pkill -f "serial_capture.py" 2>/dev/null; sleep 1
```
```bash
# Start fresh
nohup python3 /tmp/serial_capture.py > /tmp/serial_capture_stdout.log 2>&1 &
echo $! > /tmp/serial_capture_running.pid
```
```bash
# Verify it's running
sleep 2 && wc -l /tmp/meatloaf_serial.log
```

### 5. Run test on C64 via U64 API

```bash
# Inject the client BASIC program into C64 memory
python3 /home/qus/.claude/skills/ultimate64-debug/scripts/c64_remote.py \
  --command inject test/http/openai_chat_client.bas

# Type RUN and read screen
python3 /home/qus/.claude/skills/ultimate64-debug/scripts/c64_remote.py \
  --command run "RUN"
```

### 6. Read serial log
```bash
cat /tmp/meatloaf_serial.log
# or tail -f
tail -f /tmp/meatloaf_serial.log
```

## Serial Capture Script

The capture script lives at `/tmp/serial_capture.py`. It:
- Opens `/dev/ttyUSB0` at **2000000 baud** (matches `DEBUG_SPEED` in `platformio.ini`)
- Strips ANSI escape codes and control chars
- Writes clean output to `/tmp/meatloaf_serial.log`
- Also prints to stdout (captured in `/tmp/serial_capture_stdout.log`)

**Baud rate:** The ESP32 serial output is at `DEBUG_SPEED` which defaults to `monitor_speed = 2000000` in `platformio.ini:102`.

**Console activation:** The ESP32 (`src/main.cpp:306`) says "Press ENTER to activate console." after boot. The serial capture doesn't need the interactive console — it reads whatever `Debug_printv()` output the firmware produces.

## Key Files

| File | Purpose |
|------|---------|
| `test/http/test_server.py` | Echo server on port 8080 |
| `test/http/http_full_client_test.bas` | C64 BASIC test suite |
| `lib/meatloaf/network/http.cpp` | Full-mode HTTP client impl |
| `lib/meatloaf/network/http.h` | HTTP class declarations |
| `lib/device/iec/drive.cpp` | IEC routing to MStreams |

## Debug Points to Check

1. **Does OPEN reach HTTP code?** — look for `"Request URL:"` or `"HTTPMSession created"` in serial log
2. **Does write() trigger full mode?** — look for command parsing in `handleCommand()`
3. **Does send() dispatch correctly?** — look for `"GET url["` or `"POST url["`
4. **Does read() return data?** — look for header/body reading

## Common Pitfalls

- **PETSCII vs ASCII:** C64 sends PETSCII-encoded characters. `handleCommand()` compares against ASCII. If the C64 sends PETSCII lowercase, `m` ($4D) matches ASCII `M` but other chars may differ.
- **Channel variable:** Every BASIC helper needs `ch=<channel>` set before calling. All tests now set `ch=1` (or `ch=2` for test 6).
- **BASIC V2:** No line labels (`getstatus:`), no `INSTR$()`. Fixed in current `.bas` file.
- **GOTO vs GOSUB:** Tests run via `ON t GOSUB` from menu, indexes 1→9 map to line numbers 200,400,600,800,1000,1200,1400,1600,1800.

## POST Body Debugging Field Guide

This section captures lessons from fixing the full-mode HTTP client's POST body
delivery (July 2026). If you're modifying the HTTP client and body capture stops
working, start here.

## Architecture: How POST Body Flows

```
C64 BASIC                     Meatloaf ESP32                 Server
┌───────────────┐             ┌─────────────────────┐     ┌──────────┐
│ PRINT#1,"b X" │──IEC──►    │ write() buffers cmd  │     │          │
│ PRINT#1,"s"   │──IEC──►    │ _queuedSend = true   │     │          │
│ GET#1,...     │──IEC──►    │ read() phase 1:      │     │          │
│               │            │   sendRequest()      │──►  │ POST /   │
│               │            │   → POST(url)        │     │ CL: body │
│               │            │   → openAndFetchHdr  │     │ body...  │
│               │            │   → close()/perform  │     │          │
│               │            │ read() phase 2:      │◄──  │ 200/400  │
│               │            │   serve from buffer  │     │ body     │
└───────────────┘            └─────────────────────┘     └──────────┘
```

## Critical Code Paths

| Function | File:Line | Role |
|----------|-----------|------|
| `HTTPMStream::write()` | `http.cpp:745` | Buffers PRINT# bytes, dispatches commands on CR/LF |
| `handleCommand()` | `http.cpp:214` | Parses `b`, `b+`, `s`, `c`, `status`, `r-h`, `r-b` |
| `sendRequest()` | `http.cpp:71` | Copies body→postBuffer, dispatches POST/PUT |
| `openAndFetchHeaders()` | `http.cpp:1232` | Opens HTTP connection, sets URL/method/headers |
| `processRedirectsAndOpen()` | `http.cpp:905` | Retry loop, **calls init() on error codes >399** |
| `close()` | `http.cpp:1000` | **Sends body via perform()**, captures response |
| `read()` phase 1 | `http.cpp:617` | Executes queued send, builds response buffer |
| `HTTP_EVENT_ON_DATA` | `http.cpp:1551` | Captures response body into `postResponse` |

## Root Cause: The content-Length: 0 Bug

`openAndFetchHeaders()` originally called `esp_http_client_open(_http, 0)` for
ALL methods (including POST/PUT). This sent `Content-Length: 0` to the server,
which then ignored the body sent later via `perform()` in `close()`.

**The fix** (commit `f997e35b`): skip `esp_http_client_open()` for POST/PUT
entirely and return 200 immediately. The URL, method, and headers are already
set on the `_http` handle before this function runs. `close()` → `perform()`
handles the full HTTP cycle from scratch.

```cpp
// http.cpp:1283
if (method == HTTP_METHOD_POST || method == HTTP_METHOD_PUT) {
    _is_open = true;  // mark as open so close() will run perform()
    return 200;       // provisional — real status comes from perform()
}
```

## Checklist: POST Body Not Arriving

If the server receives `Content-Length: 0` or the body is empty:

1. **Check `openAndFetchHeaders()`** — does it call `esp_http_client_open()` for POST?
   If yes, the server already saw CL:0 before the body was set.
   
2. **Check `close()` → `perform()`** — is the body in `postBuffer` when `perform()` runs?
   `sendRequest()` copies the body into `postBuffer`. If `postBuffer` is empty,
   the body was never set (check `handleCommand`'s `b` dispatch).

3. **Check `_performPending`** — if `perform()` ran in `openAndFetchHeaders`, set
   `_performPending = false` to prevent a second `perform()` in `close()`.

## Checklist: Response Body Not Visible on C64

If `STATUS: 200` appears but the body doesn't:

1. **Check body capture order in `read()`** — after `close()` moves `postResponse`
   into `preservedPostResponse`, the capture logic must check:
   - `preservedPostResponse` BEFORE `_is_open`
   - `postResponse` BEFORE `_is_open`
   
   Original order was wrong (checked `_is_open` first, then tried to read from a
   destroyed HTTP handle).

2. **Check `ctx.responseStatus` propagation** — `sendRequest()` records the
   provisional status from `openAndFetchHeaders`. After `close()` → `perform()`,
   `cl.lastRC` has the real status. Update `ctx.responseStatus` from `cl.lastRC`
   in the capture block.

3. **Check `init()` in `processRedirectsAndOpen`** — error codes >399 cause
   `processRedirectsAndOpen()` to call `init()`, which destroys the `_http`
   handle AND the response data. The response body must be read from `_http`
   BEFORE this happens. Solution: read response body in `openAndFetchHeaders()`
   into `postResponse`, which survives `init()`.

## Debugging Tools

### Serial capture (Meatloaf firmware logs)

```bash
# Kill stale captures
pkill -f "serial_capture.py" 2>/dev/null; sleep 1

# Start fresh
nohup python3 /tmp/serial_capture.py > /tmp/serial_capture_stdout.log 2>&1 &
echo $! > /tmp/serial_capture_running.pid

# Read log
tail -f /tmp/meatloaf_serial.log
```

Key log lines to grep for:
```
POST/PUT perform OK, status=400, response=144 bytes   ← body sent, response received
BODY-CAPTURE: method=POST result=144 bytes             ← response captured
BUFFER: total=251 bytes (statusEnd=4, headersEnd=107)  ← buffer built
sendRequest: POST url=http://...                       ← dispatch
openAndFetchHeaders: content-type:APPLICATION/JSON     ← headers set
opening stream failed, httpCode=400                    ← error path in processRedirectsAndOpen
```

### Ultimate 64 remote debug

```bash
export U64_IP_ADDRESS="192.168.1.176"

# Inject BASIC program
python3 /home/qus/.claude/skills/ultimate64-debug/scripts/c64_remote.py \
  --command inject test/http/test_program.bas

# Run and read screen
python3 /home/qus/.claude/skills/ultimate64-debug/scripts/c64_remote.py \
  --command run "RUN"

# Read screen directly (no wait)
python3 /home/qus/.claude/skills/ultimate64-debug/scripts/c64_remote.py \
  --command screen
```

**IMPORTANT**: When running inline Python commands via `python3 -c "..."` in zsh,
avoid `$(...)` in the BASIC code — zsh interprets it as command substitution.
Use script files instead of inline commands.

### Echo server for body verification

```bash
# Start the echo server (logs every request body)
python3 test/http/test_server.py &

# The echo server echoes back method, headers, and body at:
#   http://192.168.1.131:8080/echo
# Check the server stdout to see the exact Content-Length and body bytes
```

## BASIC Tokenizer Behavior

The `inject_basic()` function in `c64_remote.py` tokenizes BASIC source
directly into C64 memory. Key rules that affect debugging:

1. **ALL alphabetic characters in string literals get UPPERCASED.**
   `"content"` in BASIC source → `"CONTENT"` in memory.
   This is because the tokenizer converts alpha chars via `ch.upper()` (line 99
   of `c64_remote.py`).

2. **Case preservation workaround:** For JSON parsing or any case-sensitive
   search, build strings with `chr$()`:
   ```basic
   nd$=chr$(34)+chr$(99)+chr$(111)+chr$(110)+chr$(116)+chr$(101)+chr$(110)+chr$(116)+chr$(34)
   ```
   Or search for the uppercase version that BASIC produced.

3. **Underscores get eaten.** `max_tokens` → `MAX TOKENS` (space instead of
   underscore). Use `chr$(95)` for underscore in command strings.

4. **Two-char variable names.** Only first two characters of a variable name are
   significant. `po$`, `po`, and `power` all share the `po` prefix. Avoid names
   that collide with BASIC keywords or each other.

5. **Line length limit:** The BASIC editor limits lines to 80 characters when
   typing, but `inject_basic()` writes directly to program memory and there is
   **no line length limit**. Long lines work fine via injection but render
   wrapped in LIST.

6. **PETSCII case flip on IEC read:** When the C64 reads response data from
   meatloaf via `GET#`, **`asc(a$)` returns the raw byte** as sent by the HTTP
   server — no PETSCII conversion happens on the IEC read path. If Ollama sends
   lowercase `"content":"hello"`, the bytes 99,111,110,116,101,110,116 (`content`)
   arrive unchanged. `chr$(99)` matches lowercase `c`.
   
   PETSCII case-flip ONLY applies when data is **printed** (`PRINT chr$(asc(a$))`
   displays uppercase). For byte comparisons in state machines and parsers, use
   the HTTP server's actual ASCII values — do NOT flip case.
   
   **Correct** (Ollama sends lowercase JSON):
   ```basic
   if a$=chr$(99) then ... : rem matches 'c', not chr$(67)
   ```
   
   **Wrong** — this would only match if the server sent uppercase:
   ```basic
   if a$=chr$(67) then ... : rem matches 'C', wrong for lowercase JSON
   ```

7. **Sending data (PRINT#) is different:** The BASIC tokenizer uppercases string
   literals (rule #1 above), so `"content"` in `print#1,"b content"` becomes
   `"CONTENT"`. But `chr$()` calls bypass the tokenizer:
   ```basic
   print#1,"b ";chr$(123);chr$(34);"model";chr$(34);...
   ```
   The `chr$(34)` produces a raw `"` — never uppercase. The string literal
   `"model"` gets uppercased to `"MODEL"` in memory, but that's fine for JSON
   field names because Ollama accepts uppercase JSON.

## Response Buffer Layout

After the HTTP cycle completes, the response buffer is built in `read()`:

```
┌──────────────────────────────────────────────────────────┐
│ STATUS LINE (e.g. "200\r")                               │
│  ↑ _statusEnd                                            │
├──────────────────────────────────────────────────────────┤
│ HEADER LINE 1\r                                          │
│ HEADER LINE 2\r                                          │
│ ...                                                      │
│ \r (blank line = end of headers)                        │
│  ↑ _headersEnd                                           │
├──────────────────────────────────────────────────────────┤
│ BODY (from _bodyCapture)                                 │
│  ↑ r-h starts here                                       │
│  ↑ r-b starts here                                       │
└──────────────────────────────────────────────────────────┘
```

The `status` command sets `_responseBufPos = 0` (read from status line).
The `r-h` command sets `_responseBufPos = _statusEnd` (read from headers).
The `r-b` command sets `_responseBufPos = _headersEnd` (read from body).

## Minimal Fix Pattern

The POST body bug was fixed with a **13-line change** (7 removed, 6 added):

```
lib/meatloaf/network/http.cpp | 13 ++++++-------
```

If you're tempted to rewrite large sections of the HTTP client, pause and
ask: *"What's the smallest change that fixes this?"* Every complete rewrite
attempt during this debugging session failed. The minimal fix worked.

For reference, the full diff of the working fix:
```
- Skip esp_http_client_open() for POST/PUT in openAndFetchHeaders()
  (was sending Content-Length: 0)
- In read() body capture: always transfer preservedPostResponse
  even when empty, and propagate ctx.responseStatus from lastRC
- No changes to close(), processRedirectsAndOpen, or sendRequest()
```

## Failed Debugging Attempts Log

This section documents blind alleys from the OpenAI chat client debugging
session (July 2026) to avoid repeating them.

### 1. PETSCII Case Flip in asc(a$) (WRONG)

**Attempted:** Changed state machine bytes from lowercase (chr$(99)='c') to
uppercase (chr$(67)='C') thinking PETSCII flipped case on the IEC read path.

**Why it failed:** `asc(a$)` returns the **raw byte** from the server — no
PETSCII conversion happens on `GET#` reads. Case-flip only applies when
printing: `PRINT chr$(asc(a$))` displays uppercase if the byte was lowercase.
For byte comparisons, use the server's actual ASCII values. Ollama sends
lowercase JSON → `chr$(99)` matches 'c'. Confirmed via ASC dump test.

### 2. Firmware Keep-Alive Config Change (WRONG)

**Attempted:** Disabled `keep_alive_enable` in `MeatHttpClient::init()` (line
1618) to force fresh TCP connections per request, hypothesizing that stale
keep-alive sockets caused silent perform() failures on Q2.

**Why it failed:** Keep-alive wasn't the root cause. The single-query test
worked fine before and after keep-alive changes. The issue was reproducible
regardless of keep-alive settings.

### 3. _session = nullptr in full-mode close() (WRONG)

**Attempted:** Set `_session = nullptr` in `HTTPMStream::close()` full-mode
branch to prevent the destructor from calling releaseIO() a second time
(double-release causes io_active underflow in SessionBroker).

**Why it failed:** Broke Q1 completely — without `_session`, the destructor
path (`~HTTPMStream() → close(SIMPLE) → client->close()`) couldn't clean up
MeatHttpClient, leaving _http handles leaked and causing ESP-IDF heap
corruption.

### 4. client->close() in full-mode close() (WRONG)

**Attempted:** Called `_session->client->close()` from the full-mode
`HTTPMStream::close()` branch to get MeatHttpClient cleanup without the
double-releaseIO problem.

**Why it failed:** `MeatHttpClient::close()` calls `esp_http_client_perform()`
for POST/PUT, which blocks the IEC bus. The C64 times out waiting for data
and shows `?DEVICE NOT PRESENT ERROR`.

### 5. Timing / Script Speed (PARTIALLY WRONG)

**Attempted:** Increased wait times in test scripts, added screen polling
with timeouts, re-injected the BASIC client fresh per query.

**Why it was partially wrong:** Timing affects keystroke injection (C64 10-byte
keyboard FIFO means long strings get truncated) but the core hanging issue
occurs even with 2-character questions like "hi". Single-query injection
works reliably; the hang is specific to multi-query BASIC programs where Q2
is issued on the same session via the AGAIN loop.

### 6. Multiple firmware rebuilds with log-only changes (WRONG APPROACH)

**Attempted:** Three separate firmware rebuilds with debug logging added to
the branch-selection code in read() phase 1, each requiring a flash cycle.

**Why it failed:** Serial capture kept failing (exit code 144, `/dev/ttyUSB0`
port issues), so the logs were never collected. Debugging firmware bugs
requires reliable serial capture first, then targeted logging changes — not
the other way around.

### 7. available() returning HTTP_BLOCK_SIZE when _queuedSend (WRONG)

**Attempted:** Changed `available()` in http.h to return `HTTP_BLOCK_SIZE`
instead of 0 when `_queuedSend` is true in full mode, hypothesizing that
`readBufferData()` saw available=0 and declared the stream exhausted before
`read()` could fire Phase 1.

**Why it failed:** Caused Q1 to hang on body read. When `available()`
returned non-zero before the response was ready (before `_queuedSend` Phase 1
executed), `readBufferData()` called `m_stream->read()` which fell through
to `MeatHttpClient::read()` → `esp_http_client_read()` on a not-yet-ready
handle, blocking the IEC bus. The response data eventually arrived into
`preservedPostResponse`, but Phase 1 was never entered because `_queuedSend`
was already false by the time the response was available.

### 8. duplicate `m_len += got` in drive.cpp (CONFIRMED FIX — Jul 9)

**What was wrong:** `lib/device/iec/drive.cpp:359-360` had `m_len += got;`
appearing twice consecutively. This doubled the buffer-fill count (`m_len`).
For Ollama responses (~300 bytes body, ~360 bytes total with headers), the
doubled m_len (~720) exceeded `BUFFER_SIZE=512`, so the fill loop in
`readBufferData()` exited before polling `eos()`. The C64 read uninitialized
buffer memory beyond the real data, never seeing EOT (end-of-transmission).

**Why it was easy to miss:** The code was in the generic IEC drive handler,
not in the HTTP client. All single-query tests used small test servers that
returned tiny bodies (~20 bytes → m_len=40 → stayed under 512 → loop
continued normally). Only real Ollama responses (~300+ bytes) triggered it.

**Fix applied** (commit `51116209`): Removed the duplicate `m_len += got;`.
Comment preserved.

### Key Lessons

1. **Never guess — gather evidence first.** The `asc(a$)` dump test took
   seconds and immediately disproved the PETSCII case-flip theory. Do this
   before changing firmware.
2. **One variable at a time.** Multiple firmware changes were bundled in one
   flash cycle multiple times, making it impossible to know which change
   caused what.
3. **Test scripts must separate Q1 and Q2 concerns.** A single-query script
   that resets/injects fresh per query proves the firmware works. A multi-query
   script that shares a BASIC session reveals the state machine problem.
   These test different things.
4. **Serial capture reliability.** The capture script exits silently when
   /dev/ttyUSB0 isn't accessible (another process holds it). Always verify
   capture is running with `wc -l /tmp/meatloaf_serial.log` before starting
   the test.
5. **Firmware changes can break working features.** Even "harmless" logging
   additions broke the single-query path that was working fine. When debugging
   a multi-query BASIC issue, don't touch firmware code that handles
   single-query flow.
6. **Bugs can hide in unexpected layers.** The `m_len += got` duplication was
   in the generic IEC drive handler (`drive.cpp`), not in the HTTP client.
   All previous investigation focused on HTTP code. When a symptom is
   response-size-dependent, consider the IEC data path (readBufferData,
   m_len, BUFFER_SIZE), not just HTTP.
7. **Echo server + response size sweep is a powerful diagnostic.** Replacing
   Ollama with a local echo server and systematically varying response body
   size isolated the `m_len` bug within minutes.
8. **Q2 remains unresolved.** Even after the `m_len` fix, a second POST
   request (Q2) on the same MeatHttpClient session does not actually send a
   new HTTP request. The `sendRequest()` → `client.POST()` path returns
   status 200 from cached/stale state without calling `esp_http_client_perform()`.
   Root cause is still unknown.

## PETSCII ↔ UTF-8 Encoding Reference

Meatloaf uses two functions for character encoding conversion, defined in
`lib/utils/string_utils.cpp` and backed by the utf8map in `lib/utils/U8Char.cpp`.

### Conversion Functions

| Function | Direction | How it works |
|----------|-----------|-------------|
| `toUTF8()` | PETSCII → UTF-8 | Looks up each PETSCII byte value (0-255) in the 256-entry utf8map table, returns the Unicode codepoint as a UTF-8 byte sequence |
| `toPETSCII2()` | UTF-8 → PETSCII | Parses multi-byte UTF-8 sequences, looks up each Unicode codepoint in the reverse map, returns single PETSCII byte |

### The utf8map (U8Char.cpp:28-59)

The 256-entry table maps each PETSCII byte to its corresponding Unicode codepoint:

| PETSCII byte | Screen display | utf8map entry | Meaning |
|---|---|---|---|
| `0x00` | — | `0x00` (NUL) | Pass-through |
| `0x01`–`0x40` | control chars / space / punct | identity `0x01`–`0x40` | Same byte in and out |
| `0x41` | `A` (unshifted, PETSCII uppercase) | `0x61` `a` | ASCII lowercase |
| `0x42` | `B` | `0x62` `b` | ↓ |
| ... | ... | ... | ↓ |
| `0x5A` | `Z` | `0x7A` `z` | ASCII lowercase |
| `0x5B`–`0x60` | `[ \ ] ^ _ \`` | identity | Same byte in and out |
| `0x61` | `a` (PETSCII lowercase at this byte) | `0xE021` | Private Unicode (U+E02x range) |
| `0x62` | `b` | `0xE022` | ↓ |
| ... | ... | ... | ↓ |
| `0x7A` | `z` | `0xE03A` | Private Unicode |
| `0x7B`–`0x7F` | `{ \| } ~ DEL` | identity | Same byte in and out |
| `0x80`–`0xBF` | PETSCII graphics chars | `0xE00x` / `U+25xx` | Box drawing, tables, etc. |
| `0xC0` | (shifted space) | `0xE020` | Private Unicode |
| `0xC1` | `A` (shifted/PETSCII uppercase) | `0x41` `A` | ASCII uppercase |
| `0xC2` | `B` | `0x42` `B` | ↓ |
| ... | ... | ... | ↓ |
| `0xDA` | `Z` | `0x5A` `Z` | ASCII uppercase |
| `0xDB`–`0xFF` | PETSCII control codes | `0xE04x`–`0xE05F` | Private Unicode |

**Key insight:** PETSCII letters live at TWO byte ranges:
- **Unshifted** (`0x41-0x5A`): The screen displays `A`–`Z` in **uppercase**. The utf8map entry is **ASCII lowercase** (`0x61`–`0x7A`).
- **Shifted** (`0xC1-0xDA`): The screen displays `A`–`Z` in **lowercase** (shifted). The utf8map entry is **ASCII uppercase** (`0x41-0x5A`).

This is NOT a "case flip" — it's a **mapping between two different character encodings**. Letters at indices `0x41-0x5A` happen to show uppercase on screen but map to lowercase Unicode. Letters at indices `0xC1-0xDA` show lowercase on screen but map to uppercase Unicode.

### Why Round-Trips Work

`toUTF8()` then `toPETSCII2()` returns the original PETSCII byte:

```
PETSCII 0x41 → toUTF8 → Unicode 0x61 'a' → toPETSCII2 → PETSCII 0x41 (same as input)
PETSCII 0xC1 → toUTF8 → Unicode 0x41 'A' → toPETSCII2 → PETSCII 0xC1 (same as input)
```

Each function mirrors the other — there is no "case flip" anywhere. The tables are
bijective for the PETSCII range, so the round-trip is exact.

### Practical Guidance for Firmware Code

#### When to use `mstr::toUTF8()`

Use on data arriving **directly from the C64 via IEC write()** — in
`handleCommand()` at `http.cpp:234`. The BASIC tokenizer uppercases string
literals (PETSCII bytes `0x41-0x5A`), and `toUTF8()` maps these to proper
ASCII lowercase for case-insensitive command dispatch and JSON pointer matching.

```cpp
// http.cpp:234 — handleCommand()
c = mstr::toUTF8(c);  // PETSCII → ASCII lowercase
```

#### When to use `mstr::toPETSCII2()`

Use on data going **to the C64** — JSON pointer query results in
`handleCommand()` at `http.cpp:351`. Converts UTF-8 JSON values back to
PETSCII for correct C64 screen display.

```cpp
// http.cpp:351 — j command result
_jsonQueryResult = mstr::toPETSCII2(_jsonQueryResult);
```

#### When to use `mstr::toLower()` (NOT toUTF8)

Use in `HTTPMStream::open()` at `http.cpp:622`. The URL arriving at this
point may come from various sources:
- A BASIC string literal (already lowercased by previous UTF-8 conversion)
- A round-tripped URL from a `j` command (UTF-8 → PETSCII → back in)
- From the MFile layer (may have been normalized already)

Applying `toUTF8()` on already-UTF-8 data that contains lowercase ASCII
letters (byte values `0x61-0x7A`) would look them up in the utf8map at those
indices and return **private Unicode** (`U+E02x`) — breaking the URL.

`std::tolower()` is safe here because:
- It only touches the ASCII uppercase range `0x41-0x5A` (maps to `0x61-0x7A`)
- For already-lowercase bytes (`0x61-0x7A`), it's a no-op
- For non-letter bytes (digits, punctuation, `://`), it's a no-op
- It does NOT use the utf8map — it's a simple arithmetic shift

```cpp
// http.cpp:622 — open()
mstr::toLower(url);  // safe: basic ASCII range only
```

#### Summary Table

| Code location | Data source | Use | Why |
|---|---|---|---|
| `handleCommand()` | IEC write (C64) | `toUTF8()` | Raw PETSCII from C64, needs decoding |
| `j result` | JSON (server) | `toPETSCII2()` | UTF-8 from cJSON, needs C64 encoding |
| `open()` URL | MFile / round-trip | `toLower()` | Already UTF-8, `toUTF8` would corrupt |
