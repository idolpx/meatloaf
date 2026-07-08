# Debugging Meatloaf Full HTTP Client

## Triple-Setup Architecture

```
┌────────────────┐      IEC        ┌──────────────┐       HTTP        ┌─────────────┐
│  C64 running    │ ◄──serial bus──► │  Meatloaf    │ ◄──────────────► │  Test server │
│  BASIC test     │                 │  (ESP32)     │                   │  (Python)    │
│                 │                 │  UART@2M     │                   │  port 8080   │
└────────────────┘                 └──────┬───────┘                   └─────────────┘
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

### 4. Start serial capture
```bash
# Kill stale captures
pkill -f "serial_capture.py" 2>/dev/null; sleep 1

# Start fresh
nohup python3 /tmp/serial_capture.py > /tmp/serial_capture_stdout.log 2>&1 &
echo $! > /tmp/serial_capture_running.pid

# Verify
sleep 2 && tail -3 /tmp/meatloaf_serial.log
```

### 5. Build BASIC test PRG and upload to SD card

The C64 loads `http_test.prg` from the meatloaf's SD card (served via WebDAV). The `.bas` source must be compiled to `.prg`:

```bash
# Compile .bas → .prg using the VS64 BASIC compiler
python3 /home/qus/.vscode/extensions/rosc.vs64-2.6.2/tools/bc.py \
  -o test/http/build/http_test.prg \
  test/http/http_full_client_test.bas

# Verify the PRG contains the correct server IP
strings test/http/build/http_test.prg | grep 192.168
```

Then upload the compiled PRG to the meatloaf SD card via WebDAV PUT or directly.

### 6. Run BASIC test on C64
- Ensure `s$` in the BASIC program points to this machine's IP
- Run the program from the C64
- Select test option

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

# POST Body Debugging Field Guide

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
