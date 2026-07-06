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
