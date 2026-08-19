---
name: c64-meatloaf-debug
description: |
  Debug Commodore 64 BASIC programs, cc65-compiled C PRGs, and Meatloaf
  ESP32 firmware in tandem, using the Ultimate 64's REST API and
  Meatloaf's UART serial debug output. Automates the full unattended
  cycle: write/modify BASIC, C, or C++ → deploy (inject BASIC, write
  PRG to native load address, or pio build+flash) → run on real
  hardware → read screen + serial logs → diagnose → fix → repeat. Use
  whenever the user mentions debugging C64/Meatloaf interactions,
  Meatloaf firmware issues, C64 BASIC or cc65 C networking, POST/PUT
  body problems, PETSCII encoding, U64 remote control, cc65 symbol
  table / map file / VICE label file debugging, or any "unattended
  debug cycle" for the C64 → Meatloaf → API pipeline. Also trigger
  when the user talks about the Ultimate 64 REST API, serial capture
  from an ESP32, automated C64 program testing, or uploading compiled
  .prg files. This is the primary skill for anyone doing C64 HTTP
  debugging with Meatloaf — do not defer to other skills for these
  scenarios.
---

# C64 / Meatloaf Unattended Debug Cycle

This skill guides the full iterative loop for debugging C64 BASIC programs
that use Meatloaf's Full-Mode HTTP Client:

```
┌──────────────────┐    IEC serial   ┌──────────────┐   HTTP   ┌─────────────┐
│  Ultimate 64     │ ◄──────────────►│  Meatloaf    │◄────────►│  API /      │
│  (BASIC program) │                 │  (ESP32)     │          │  Test Svr   │
└──────────────────┘                 └──────┬───────┘          └─────────────┘
                                            │ USB serial (monitor_speed)
                                            ▼
                                     ┌──────────────┐
                                     │  Serial      │
                                     │  capture     │
                                     │  (this PC)   │
                                     └──────────────┘
```

**Two codebases are debugged in the same loop:**
- **BASIC** — runs on the C64, talks to Meatloaf via IEC bus
- **Meatloaf C++** — the ESP32 firmware in this repo (the directory holding
  `platformio.ini`; `MEATLOAF_DIR` overrides it)

**Which one to fix?** Claude decides based on evidence from the screen and
serial log. The skill provides tools to gather evidence and deploy fixes to
either side.

---

## Architecture

### Ultimate 64 REST API

The Ultimate 64 exposes a REST API on port 80. Key endpoints:

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/v1/machine:reset` | PUT | Hardware reset |
| `/v1/machine:writemem` | PUT | Write hex bytes (≤128 bytes) |
| `/v1/machine:writemem` | POST | Write binary (unlimited, for BASIC programs) |
| `/v1/machine:readmem` | GET | Read binary from memory |
| `/v1/info` | GET | System info |
| `/v1/machine:menu_button` | PUT | Toggle menu |
| `/v1/machine:debugreg` | GET/PUT | Read/write debug register ($D7FF) |

Memory locations used:
- **$0277** — Keyboard buffer (10-byte FIFO). Key codes go here.
- **$00C6** — Keyboard buffer count. Set after writing to $0277.
- **$0400** — Screen memory. 40×25 = 1000 bytes. READ that.
- **$0801** — BASIC program start. POST tokenized BASIC here. C
  PRGs use this address too by default (cc65 EXEHDR at $0801).
- **$002D-002E** — Variable pointer. BASIC-only — set to end of BASIC
  program after injection. **Do NOT touch for C PRGs** (clobbers the
  zero-page globals cc65 set up).
- **$D7FF** — U64 debug register. Single free byte in the cartridge
  expansion area. Useful as a runtime checkpoint signal from C code
  (see "cc65 Debug Options" below).
- **$DD00-$DD03** — CIA#2. `$DD00` is the IEC serial bus (ATN/CLK/DATA),
  `$DD01`/`$DD03` the user port data and direction used by parallel speeders
  (DolphinDOS, SpeedDOS), `$DD02` the Port A direction. Decoded by the `bus`
  command — see "F. IEC bus and user port signals".
- **$DC0D** — CIA#1 interrupt control/status. Bit 4 is the /FLAG pin, where
  the IEC **SRQ** line arrives (shared with the datasette input). **Reading it
  is destructive** on a 6526 — it clears latched interrupt flags.

Full register reference: <https://csdb.idolpx.com/tools/mem64>

### Meatloaf Source Structure

| File | Purpose |
|------|---------|
| `platformio.ini` | Build config, verbose flags |
| `lib/network/http.cpp` | Full-mode HTTP client implementation |
| `lib/network/http.h` | HTTP class declarations |
| `lib/device/iec/drive.cpp` | IEC routing to MStreams (bug location: `m_len`) |
| `test/http/test_server.py` | Echo server on port 8080 |
| `test/http/openai_chat_client.bas` | Example BASIC chat client |
| `test/http/http_full_client_test.bas` | Test suite |

### Build Configuration

**All build settings are read from the project's `platformio.ini`** — never
hardcode them, and never assume the values below. The board, serial port and
baud rate change per machine and per user; `platformio.ini` is where the
project documents them, so it is the single source of truth.

Ask for the resolved settings before doing anything that touches the device:

```bash
python3 scripts/meatloaf_debug.py config
```

```json
{
  "project_root": "/Users/you/src/meatloaf",
  "ini_path": "/Users/you/src/meatloaf/platformio.ini",
  "environment": "fujiloaf-rev0",
  "board": "esp32-wrover-16m",
  "mcu": "esp32",
  "flash_size": "16m",
  "u64_ip_address": "192.168.1.176",
  "upload_port": "/dev/cu.usbserial-14520",
  "serial_port": "/dev/cu.usbserial-14520",
  "monitor_speed": "2000000",
  "verbose_flags": [],
  "defines": ["BUILD_IEC", "ENABLE_CONSOLE", "SD_CARD", "..."],
  "overrides": {}
}
```

| Setting | Where it comes from |
|---------|--------------------|
| Active env | `[meatloaf] environment` (falls back to `[platformio] default_envs`) |
| Board / MCU / flash size | `[env:<environment>] board`, then `boards/<board>.json` |
| Upload port | `[env:<environment>] upload_port`, else `[env] upload_port` |
| Serial capture port | `monitor_port` if set, else `upload_port` |
| Serial debug baud | `monitor_speed` |
| Verbose flags | active `-D` flags in the env's `build_flags` (commented-out flags are correctly ignored) |
| Ultimate 64 address | `[meatloaf] u64_ip_address` (debug tooling only — not compiled into firmware) |

`scripts/pio_config.py` does the parsing (`${section.key}` interpolation and
`;` comments included) and is imported by both `meatloaf_debug.py` and
`serial_capture.py`. The project root is found by walking up from the script,
so the skill works from any working directory.

**Checking `verbose_flags` matters.** If the serial log is missing the HTTP
diagnostics this skill's tables refer to, `VERBOSE_HTTP` is probably not
enabled — it ships commented out. Uncomment it in `platformio.ini` under
`[env] build_flags` and reflash, rather than concluding the code path was
never reached.

To target a different board or port for one run without editing
`platformio.ini`, set the `MEATLOAF_*` env vars (see "Environment Variables").

---

## Skill Components

```
commodore64debugging/
├── SKILL.md                            ← This file: workflow + reference
├── scripts/
│   ├── meatloaf_debug.py               ← Python automation + CLI
│   ├── pio_config.py                   ← Reads build settings from platformio.ini
│   └── serial_capture.py               ← Robust serial capture daemon
└── evals/
    └── evals.json                      ← Test cases

### BASIC Compilation

**Only bc64 is supported** for BASIC compilation. The built-in BASIC V2
tokenizer has been removed — all `.bas` files go through the `bc64` CLI:

```bash
bc64 -a -o /tmp/output.prg source.bas
```

The 2-byte PRG load address header (`01 08`) is stripped automatically by
`inject_basic()`, which POSTs the raw tokenized body directly to $0801.

bc64 features used across all test programs:
- `@alias` variables for unlimited-length names (via `-a` flag)
- Labels for control flow (`Start:`, `ShowMenu:`, `HttpFetch`)
- Auto-numbering (no manual line numbers needed)
- Case-insensitive keywords

### C Compilation (cc65)

C programs use the **cc65** toolchain (`cl65`, `ca65`, `ld65`). Deploying
a .prg requires: **reset → upload body-only to $0801 → type RUN**. The
full protocol is in the "C-PRG / cc65 Programs" section below.

Recommended build flags:
```bash
cl65 -t c64 -O -I. --mapfile foo.map -o foo.prg foo.c
# Debug build: add --listing foo.lst and -g (skip -O)
```

The `--mapfile` is essential — see "cc65 Debug Options" below.

### Python Modules

**`scripts/meatloaf_debug.py`** — The main automation module. Use it for
every operation. Run commands via CLI or import as a Python module.

**`scripts/serial_capture.py`** — A self-contained daemon for reading
Meatloaf's UART debug output. **Does not require `meatloaf_debug.py`**
to run — it's a standalone script.

### Python dependencies

Both scripts need `requests` (U64 REST API) and `pyserial` (capture daemon).

A modern system Python is usually PEP 668 "externally managed", so
`pip install --user requests pyserial` fails outright. Use a venv and call
its interpreter explicitly rather than `python3`:

```bash
python3 -m venv /tmp/c64dbg && /tmp/c64dbg/bin/pip install requests pyserial
/tmp/c64dbg/bin/python scripts/meatloaf_debug.py config
```

Symptom if you skip this: `Error: 'requests' library required. pip install
requests` from `meatloaf_debug.py`, or `ModuleNotFoundError: No module named
'serial'` from the capture daemon.

---

## Robust Serial Capture

The serial capture is the most important diagnostic tool. It reads the
ESP32's `Debug_printv()` output over USB UART, on the port and baud rate
resolved from `platformio.ini` (`monitor_port` / `monitor_speed` — run
`meatloaf_debug.py config` to see them).

### Why It's Robust

| Feature | What it does |
|---------|-------------|
| **Auto-reconnect** | If the port disappears (ESP reboot, USB unplug, flash cycle), it retries with exponential backoff (1s → 2s → 4s → ... → 30s max) |
| **Heartbeat** | Writes a timestamp every time data arrives. The health check uses this to detect a dead capture within 30 seconds |
| **PID tracking** | Writes its PID to a file so the management script can find and kill it reliably |
| **Graceful shutdown** | Handles SIGTERM/SIGINT to close the log file cleanly |
| **Stale cleanup** | `start_capture()` always kills the previous instance first, then also runs `pkill -f serial_capture.py` as a backup |
| **Non-exclusive port** | Opens with `exclusive=False` so monitoring tools can coexist |
| **Strips ANSI/control chars** | Produces clean readable log output |
| **Timestamped markers** | Writes `=== CAPTURE STARTED/STOPPED <timestamp> ===` lines |

### What survives

- **ESP32 crash/reboot** — the capture detects the port error, enters backoff
  mode, and reconnects automatically when the port reappears.
- **USB cable disconnect** — reconnects with backoff when cable goes back in.

What does **not** survive:

- **Flashing firmware** — stop the capture first. esptool needs the port to
  itself on every platform, not just Windows (see "When to stop it").

### When to start it

**Start capture before running any tests:**
```bash
python3 scripts/meatloaf_debug.py capture start
```

**After flashing firmware**, wait for the ESP32 to boot and start logging:
```bash
python3 scripts/meatloaf_debug.py capture wait --timeout 20
```

This polls until new data appears in the serial log (up to 20s timeout).

### When to stop it

**Stop the capture when the debug session ends** — it holds the serial port
open, and on Windows that port is exclusive, so nothing else can attach until
it lets go:

```bash
python3 scripts/meatloaf_debug.py capture stop
```

Leaving it running blocks the two things a person reaches for next: their own
serial monitor (`pio device monitor`, PuTTY, the IDE terminal), and the next
flash.

**Flashing fails while the capture holds the port — on every platform.** The
error differs but the cause is the same:

| Platform | What esptool reports |
|---|---|
| Windows | `Could not open COM7, the port is busy` / `PermissionError(13, 'Access is denied.')` |
| macOS / Linux | `A serial exception error occurred: device reports readiness to read but returned no data (device disconnected or multiple access on port?)` |

The macOS/Linux message is misleading — it reads like a cable or driver fault,
and esptool's own note points you at the hardware. It is neither. The port is
shared there, so the capture keeps reading bytes out from under esptool's
`Connecting....` handshake and the sync never completes. Verified on macOS
2026-08-19: `build --flash` failed this way with the capture running, and the
identical `pio run -t upload` succeeded seconds later once it was stopped.

So the flash sequence is always: **stop capture, flash, start capture.**
And the last thing any session does is stop it.

### Health check

```bash
# Quick check (exit code 0 = running)
python3 scripts/meatloaf_debug.py capture status
# Or independently:
python3 scripts/serial_capture.py --check /tmp/serial_capture.pid
```

### Reading the log

```bash
# Last 50 lines
python3 scripts/meatloaf_debug.py capture tail

# Grep for specific patterns
python3 scripts/meatloaf_debug.py capture grep --pattern "perform OK"

# Read raw log
cat /tmp/meatloaf_serial.log
```

---

## Keystroke Injection (Keyboard Buffer)

Sending keys to the C64 uses the **keyboard buffer at $0277**, a 10-byte
hardware FIFO. This is the **only** way to inject keystrokes on the U64.

**Why not CIA registers?** The U64 API docs explicitly state *"Writing to
the I/O registers of the 6510 is not possible"*, which blocks direct
CIA #1 ($DC00/$DC01) keyboard matrix manipulation. The keyboard buffer
at $0277 is the sole injection path.

### How it works

1. **Flush** stale keys from the buffer by zeroing $00C6 first
2. Write ≤8 PETSCII key codes to $0277 (leaving margin in the 10-byte FIFO)
3. Set the count at $00C6 — this triggers the KERNAL IRQ to read the buffer
4. Poll $00C6 until it returns to 0 (C64 has consumed the keystrokes)
5. Write the next chunk
6. **If drain times out:** flush stale keys, retry in 1-byte chunks

### Robustness features

| Feature | What it does |
|---------|-------------|
| **Flush before write** | Zeroes $00C6 before every new batch to discard stale keys from previous operations |
| **Write-then-set** | Writes key codes to $0277 *before* setting $00C6, so the IRQ never sees empty buffer slots |
| **Drain polling** | Polls $00C6 between chunks. The C64 processes keys at IRQ rate (~60Hz), so each chunk takes ~1/60s |
| **Bogus count guard** | If $00C6 reads ≥10 (buffer overflow indicator), force-flushes immediately |
| **1-byte fallback** | If a chunk fails to drain within 3s, switches to 1-byte-at-a-time mode to minimize per-write risk |
| **Panic STOP ×3** | press_stop() sends RUN/STOP 3 times + ENTER — the KERNAL checks for STOP during I/O at specific points |

### Known limitations

- **Blocking I/O blocks the IRQ** — if the C64 is stuck in KERNAL IEC
  transfer (LOAD/SAVE/OPEN), the keyboard IRQ won't fire and the buffer
  won't drain. press_stop() sends STOP repeatedly because the KERNAL
  checks for RUN/STOP at specific I/O polling points.
- **No feedback** — there's no way to confirm a specific key was processed
  beyond polling $00C6 (you can't tell *which* key was consumed).
- **10-byte FIFO is small** — long text strings require multiple chunks
  with drain waits between.
- **NMI/RESTORE not accessible** — the RESTORE key (CPU /NMI pin) could
  theoretically unstick even KERNAL-blocked I/O, but the U64 API has no
  mechanism to trigger it (no CIA or NMI endpoints).

---

## C-PRG / cc65 Programs

A `.prg` file (cc65, KickAss, raw binary with a 2-byte LE header) is
uploaded very differently from a BASIC program. The existing
`inject_basic()` is BASIC-only — it always writes to `$0801` and pokes
the BASIC variable pointer at `$002D`. Both assumptions are wrong for
C.

### Why BASIC and C differ

| Aspect | BASIC (bc64) | C (cc65) |
|--------|--------------|----------|
| Load address | Always `$0801` | Whatever the linker chose (`$0801` for cc65 default, but `-S addr` can move it) |
| Variable pointer at `$002D` | Required (BASIC state) | Must NOT be touched (C uses zero page for its own globals) |
| Header size | 0 bytes (bc64 strips it) | 2 bytes (LE load address) |
| Program size | Small (<2 KB) | Often 10–40 KB (must use POST, not PUT) |
| Auxiliary data | None | Sprite patterns, lookup tables, char sets at fixed addresses |
| Entry mechanism | `RUN` | `SYS <addr>` (decimal or hex) |

### The cc65 PRG layout (default `cl65 -t c64`)

```
$0801  EXEHDR (8 bytes: jump to STARTUP at $080D)
$080D  STARTUP (zero page init, runtime init, call main)
$0840  CODE (cc65 runtime + your functions)
       ...
       RODATA (string literals, lookup tables, sin/cos)
       DATA (initialised globals)
       INIT (run once, then freed for BSS)
$5415  BSS (zeroed globals; ONCE constructors run, then this region)
$5A00  <-- your memory is free above here for sprites, bitmap, etc.
```

cc65's `--start-addr` (or `-S`) default for `-t c64` is `$0801`, so the
PRG header shows `01 08` (little-endian). That's why the first bytes
of `c64u_radar.prg` are `01 08 0B 08 ...` — load at `$0801`, EXEHDR
contains `0B 08` = `$080B` = jump to STARTUP at `$080D` (the `+2`
accounts for the JSR instruction's own size).

### Injecting a cc65 C PRG

The `inject-prg` command does **not** exist in this version of
`meatloaf_debug.py`. Use the **raw POST** to `writemem` instead.

The correct deploy sequence — always:

```bash
# 1. Reset the U64 (clears RAM)
python3 scripts/meatloaf_debug.py reset
sleep 4                      # wait for boot + READY prompt

# 2. Upload PRG body (strip the 2-byte load-address header)
U64_IP="${U64_IP_ADDRESS:-192.168.1.176}"
python3 -c "
import requests
with open('/path/to/your.prg', 'rb') as f:
    body = f.read()[2:]                    # strip LE load-address header
r = requests.post(f'http://${U64_IP}/v1/machine:writemem?address=0x0801',
                  data=body, timeout=10)
print(f'Upload {len(body)} B -> {r.status_code}')
"

# 3. RUN (type keystrokes via keyboard buffer)
python3 scripts/meatloaf_debug.py type "RUN"
sleep 3
python3 scripts/meatloaf_debug.py screen   # verify output
```

**Why reset first?** The POST writemem appends to existing RAM content,
it doesn't clear it. Stale BASIC program bytes from earlier runs corrupt
the linker's EXEHDR. A hardware reset gives a clean $0801-$7FFF space.

**Why strip the header?** The .prg file starts with a 2-byte little-endian
load address (`01 08` = `$0801`). The POST writemem writes bytes directly
into RAM starting at the given address — if you POST the header bytes too,
they end up as program data at $0801-$0802, corrupting the BASIC stub.

### How the upload works

1. Read the .prg file in binary mode.
2. Skip the first 2 bytes (LE load address).
3. POST the remaining bytes to `writemem?address=0x0801`.
4. The PRG's EXEHDR (BASIC SYS stub) at $0801-$080C handles `RUN` → `SYS2061`.
5. cc65's STARTUP runs at $080D, calls `main()`.

**Do NOT** touch the BASIC variable pointer at `$002D`. C programs use
the zero page for their own globals; clobbering it corrupts state.

### Auxiliary data uploads (sprites, tables, char sets)

Many C programs reference pre-built data at known addresses (sprite
patterns at `$5A00`, lookup tables generated by Python at `$7000`,
etc.). The PRG doesn't include them; you upload them separately:

```bash
# Upload sprite patterns to the standard sprite area
python3 scripts/meatloaf_debug.py write-mem 5A00 sprites.bin

# Upload a sin/cos table generated by a Python tool
python3 scripts/meatloaf_debug.py write-mem 7000 sincos.bin

# Read back what you wrote (to verify)
python3 scripts/meatloaf_debug.py read-mem 5A00 512 > /tmp/sprites_readback.bin
diff <(xxd -p sprites.bin) <(xxd -p /tmp/sprites_readback.bin)
```

`write-mem` and `read-mem` are arbitrary-address, arbitrary-length
wrappers around the same `writemem`/`readmem` REST endpoints used by
the BASIC path.

### Common pitfalls

| Symptom | Cause | Fix |
|---------|-------|-----|
| `?SYNTAX ERROR` after `RUN` | PRG header bytes (01 08) were POSTed as data instead of stripped | Strip the 2-byte header before POSTing the body to $0801 |
| Screen garbage, then `READY.` | PRG clobbered zero page / BASIC vectors | Don't poke `$002D-$002E` for C PRGs |
| `READY.` with no obvious effect after RUN | Program loaded but `main()` returned or fell through to `STOP`. Check your C source's startup code | Wrap infinite loop in `main()`, or verify with `/help` |
| Reset didn't fix garbage | Reset doesn't clear RAM — stale data remains at $0801 | Always do a hardware reset (`reset` endpoint, wait 4s) before uploading |

---

## cc65 Debug Options

The cc65 toolchain ships with several outputs that are pure gold for
debugging — far more useful than just the .prg.

| Option | What it produces | When to use it |
|--------|------------------|----------------|
| `--mapfile foo.map` | Full link map with all segment addresses, sizes, exports | **Always** — it tells you exactly where every symbol lives |
| `-Ln foo.lbl` | VICE label file (one label per line) | Load into VICE monitor: `source foo.lbl` |
| `-Wl --dbgfile,foo.dbg` | VICE/CCS64 debug info | Source-level stepping in VICE |
| `--listing foo.lst` | Assembler listing interleaved with C source | When you suspect compiler codegen bugs |
| `-O` vs `-Oir` | Optimisation level (none / inline-runtime / inline-known) | `-Oir` for final, no flags for debugging (preserves variables) |
| `-Wl -vm` | Verbose map with cross-reference | When `--mapfile` output isn't enough |

### The map file in practice

```text
$ grep -E "(__MAIN|__BSS|__STACK|HIMEM|EXEHDR|STARTUP)" foo.map
Exehdr                000801  00080C  00000C
Startup               00080D  00083F  000033
__MAIN_START__        00080D RLA    __MAIN_SIZE__     00C7F3 REA
__BSS_RUN__           005415 RLA    __BSS_SIZE__      000031 REA
__STACKSIZE__         000800 REA    __HIMEM__         00D000 REA
```

The columns: `Name`, `Start`, `End`, `Size`, `Align`. The suffixes
mean:

- `RLA` = relocatable address (resolved at link time) — absolute C64
  address after linking
- `REA` = relocatable expression (might be a constant, not an addr)
- `RLZ` = relocatable address in zero page
- `LL` = linker-generated local

### Debugging without VICE: the U64 debug register

The U64 exposes a `machine:debugreg` endpoint that reads/writes a
single byte at `$D7FF`. This is the cartridge expansion ROM/RAM area;
cc65 doesn't use it, so it's a free scratch byte for runtime
assertion logging:

```c
// In your C code:
#define DBG_REG (*(volatile unsigned char *)0xD7FF)

// At a checkpoint:
DBG_REG = 0x42;   // marks "I reached the JSON parser"

// On error:
DBG_REG = 0xEE;   // marks "fatal: out of memory"
```

Read it from the host:
```bash
python3 scripts/meatloaf_debug.py read-mem D7FF 1
# → 42 if the program reached your checkpoint
```

Combine with the U64 REST `debugreg` endpoint for an even faster
poll without going through readmem:
```bash
curl http://192.168.1.176/v1/machine:debugreg
```

### Building for debug vs release

| Stage | cl65 flags |
|-------|-----------|
| Development | (no `-O`), `--mapfile`, `--listing`, `-Wa -l` for asm listing |
| Profiling | `-Oir` (optimise + inline known funcs), `--mapfile` to confirm sizes |
| Release | `-Oir -Os`, strip assertions, set `__HIMEM__` carefully |

For the c64u-radar build (`/home/qus/dev/_c64/c64u-radar/c64u_radar/Makefile`):
```sh
# Debug:
cl65 -t c64 -g --mapfile foo.map --listing foo.lst -o foo.prg foo.c

# Release:
cl65 -t c64 -Oir -Os --mapfile foo.map -o foo.prg foo.c
```

---

## Quick Reference: All Commands

### C64 Control

```bash
# Read screen (40×25 PETSCII screen codes decoded to ASCII)
python3 scripts/meatloaf_debug.py screen

# Inject BASIC from file (- = stdin)
python3 scripts/meatloaf_debug.py inject test/http/my_test.bas

# Type a BASIC command and read screen
python3 scripts/meatloaf_debug.py run "RUN"
python3 scripts/meatloaf_debug.py run "LIST 100-200"
python3 scripts/meatloaf_debug.py run "PRINT PEEK(53280)" --wait 1

# Type text (no screen read, for interactive programs)
python3 scripts/meatloaf_debug.py type "hello"

# Decode IEC serial bus + user port ($DD00-$DD03)
python3 scripts/meatloaf_debug.py bus
python3 scripts/meatloaf_debug.py bus --samples 60 --interval 0.1  # is it moving?
python3 scripts/meatloaf_debug.py bus --srq      # also $DC0D.4 — DESTRUCTIVE

# STOP (interrupt running BASIC)
python3 scripts/meatloaf_debug.py stop

# Hardware reset
python3 scripts/meatloaf_debug.py reset

# Inject a compiled .prg (cc65, KickAss) — full cycle:
python3 scripts/meatloaf_debug.py reset
sleep 4
python3 -c "import requests
with open('path/to/foo.prg','rb') as f: body=f.read()[2:]
requests.post('http://192.168.1.176/v1/machine:writemem?address=0x0801', data=body, timeout=10)"
python3 scripts/meatloaf_debug.py type "RUN"
sleep 3
python3 scripts/meatloaf_debug.py screen

# Upload auxiliary data (sprite patterns, lookup tables, char sets)
python3 scripts/meatloaf_debug.py write-mem 5A00 sprites.bin
python3 scripts/meatloaf_debug.py write-mem 7000 sincos.bin

# Read back to verify
python3 scripts/meatloaf_debug.py read-mem 5A00 512
python3 scripts/meatloaf_debug.py read-mem 0801 19514 --raw > /tmp/prg_readback.bin

# Inject + RUN in one step
python3 scripts/meatloaf_debug.py test my_test.bas
python3 scripts/meatloaf_debug.py test --wait 5 < my_test.bas
```

### Serial Capture

```bash
# Start (auto-kills stale, auto-reconnects on port drops)
python3 scripts/meatloaf_debug.py capture start

# Stop
python3 scripts/meatloaf_debug.py capture stop

# Status + log size
python3 scripts/meatloaf_debug.py capture status

# Tail last N lines
python3 scripts/meatloaf_debug.py capture tail --lines 50

# Grep for diagnostics
python3 scripts/meatloaf_debug.py capture grep --pattern "Request URL"
python3 scripts/meatloaf_debug.py capture grep --pattern "perform OK"
python3 scripts/meatloaf_debug.py capture grep --pattern "sendRequest"

# Wait for data to appear (after flash)
python3 scripts/meatloaf_debug.py capture wait --timeout 20
```

### Meatloaf Console (via serial FIFO)

The serial capture daemon holds the serial port open and provides a
**command FIFO** at `/tmp/serial_capture_cmd`. Write a Meatloaf console
command to the FIFO, and the daemon relays it over serial and captures
the response in the log.

```bash
# Send a command and read the response
python3 scripts/meatloaf_debug.py send "sysinfo"
python3 scripts/meatloaf_debug.py send "ifconfig" --wait 1.5
python3 scripts/meatloaf_debug.py send "meminfo"
```

**Useful Meatloaf console commands:**

| Command | Purpose |
|---------|---------|
| `sysinfo` | Firmware version, chip, flash/PSRAM info |
| `meminfo` | Heap/PSRAM usage — check for memory leaks |
| `ifconfig` | WiFi status, IP address |
| `netstat` | Active TCP/UDP sockets — find stuck connections |
| `ping <host>` | Network reachability (isolate C64 vs network) |
| `iecdetect` | Scan IEC bus for C64 device presence |
| `config [key]` | Full or partial configuration JSON |
| `df` | Filesystem disk space |
| `ls <path>` | List files on SD/flash |
| `cat <file>` | View file content |
| `ps` | Running tasks |
| `reboot` | Reboot Meatloaf (ESP32) |
| `wget [-o <file>] <url>` | Download URL — test HTTP without the C64! |
| `help [command]` | List or describe commands |

**Using `wget` for isolated HTTP tests:** This is a powerful debugging
technique. Instead of running the full C64→Meatloaf cycle, test HTTP
directly from the Meatloaf console:

```bash
# Download a URL to Meatloaf's temp filesystem
python3 scripts/meatloaf_debug.py send "wget -o /tmp/test.txt http://192.168.1.131:8080/echo" --wait 3

# Read the downloaded content
python3 scripts/meatloaf_debug.py send "cat /tmp/test.txt" --wait 2
```

**Diagnostic logic:** If `wget` works but C64 BASIC fails → the issue is
in the BASIC code or IEC data path. If `wget` also fails → the issue is
in Meatloaf's HTTP client or network layer.

### Build & Flash

```bash
# Show build settings resolved from platformio.ini (no hardware needed)
python3 scripts/meatloaf_debug.py config

# Build only
python3 scripts/meatloaf_debug.py build

# Build + flash — STOP THE CAPTURE FIRST or the upload fails (see above).
# A full rebuild after a framework/build-flag change takes several minutes;
# a source-only change is much quicker.
python3 scripts/meatloaf_debug.py capture stop
python3 scripts/meatloaf_debug.py build --flash
python3 scripts/meatloaf_debug.py capture start
```

### Echo Test Server

```bash
# Start on port 8080
python3 scripts/meatloaf_debug.py echo start

# Stop
python3 scripts/meatloaf_debug.py echo stop
```

### Full Unattended Cycle

```bash
# One cycle: ensure capture → inject → RUN → read screen
python3 scripts/meatloaf_debug.py cycle --basic my_test.bas

# With firmware flash first
python3 scripts/meatloaf_debug.py cycle --basic my_test.bas --firmware
```

### Diagnostics

```bash
python3 scripts/meatloaf_debug.py diag
```

Example output:
```json
{
  "u64": "reachable",
  "screen_preview": "READY. | ... |",
  "u64_info": {
    "product": "Ultimate 64",
    "firmware": "3.12",
    "hostname": "ultimate-123456"
  },
  "serial_capture": {
    "running": true,
    "log_path": "/tmp/meatloaf_serial.log",
    "log_size": 18347
  },
  "echo_server": {
    "pid_file": false
  }
}
```

---

## The Unattended Debug Cycle (Full Protocol)

### Phase 1: Setup

Run once at the start of a session:

0. **`config`** — confirm the environment, port and baud resolved from
   `platformio.ini` match the hardware actually plugged in
1. **`diag`** — check U64 is reachable, serial capture status (its output
   includes `config` under `build_config`)
2. **`capture start`** — ensure serial capture is running
3. **Start echo server** if you'll do body verification: `echo start`
4. **`reset`** or **`stop` + clear** — ensure the C64 is at READY

### Phase 2: Identify what changed

| Change | Action | Wait time |
|--------|--------|-----------|
| BASIC code only | `inject` + `run "RUN"` | ~2-3s |
| C code (cc65) | `reset` → upload body → `type "RUN"` | ~8-10s |
| Meatloaf C++ | `capture stop` → `build --flash` → `capture start` → wait for serial | minutes |
| Any combination | Do the slowest (flash) first, then C, then BASIC | N/A |

### Phase 3: Run and observe

For self-contained BASIC test programs:
```python
screen = cycle.inject_and_run(basic_code, wait=wait_time)
```

For cc65 C programs (the correct deploy sequence):
```bash
# 1. Reset (clears RAM, ensures clean memory)
python3 scripts/meatloaf_debug.py reset
sleep 4

# 2. Upload PRG body (strip the 2-byte LE load-address header)
U64_IP="${U64_IP_ADDRESS:-192.168.1.176}"
python3 -c "
import requests
with open('/path/to/foo.prg', 'rb') as f:
    body = f.read()[2:]
r = requests.post(f'http://${U64_IP}/v1/machine:writemem?address=0x0801',
                  data=body, timeout=10)
print(f'Upload {len(body)} B -> {r.status_code}')
"

# 3. Launch via RUN (the BASIC EXEHDR stub at $0801 handles SYS2061)
python3 scripts/meatloaf_debug.py type "RUN"
sleep 3
python3 scripts/meatloaf_debug.py screen
```

For interactive programs (menus, chat clients):
```python
cycle.u64.type_text("What is the weather?")
time.sleep(5)  # Wait for HTTP round-trip
screen = cycle.u64.read_screen()
```

### Phase 4: Read two data sources (plus Meatloaf Console)

**Meatloaf Console** (via `send` command) — interact with the ESP32 directly.
Use `wget` to test HTTP without the C64 involved. See the "Meatloaf Console"
section above for full command reference.

**Screen** (via `screen` command) — what the C64 displays:
- BASIC errors like `?SYNTAX  ERROR IN 220`
- HTTP status: `STATUS: 200`
- Error messages: `?DEVICE NOT PRESENT  ERROR`
- Program output

**Serial log** (via `capture tail` or `capture grep`) — what Meatloaf logs:

| Log pattern | Meaning |
|-------------|---------|
| `"Request URL:"` or `"HTTPMSession created"` | OPEN reached HTTP code ✓ |
| `handleCommand` | PRINT# commands being parsed |
| `"GET url["` / `"POST url["` | HTTP method dispatched |
| `sendRequest: POST url=http://...` | POST body prepared |
| `perform OK, status=###` | HTTP response received |
| `BODY-CAPTURE: method=POST result=N` | Response body captured |
| `BUFFER: total=N (statusEnd=..., headersEnd=...)` | Response buffer built |
| `Content-Length: 0` | Known bug: POST body not sent |
| `opening stream failed, httpCode=###` | Server returned error |
| `m_len =` | Buffer size tracking |

### Phase 5: Diagnose

Common patterns from evidence:

| Screen | Serial log | Likely issue |
|--------|------------|--------------|
| `?DEVICE NOT PRESENT` | Nothing | Meatloaf not responding on IEC |
| Error with line # | (anything) | BASIC syntax bug |
| OK + status 200 | No `perform OK` | Status from cache (Q2 bug) |
| Status 200, no body | `perform OK` with body | Body capture order (read()) |
| Status 0 | Nothing | URL open failed |
| Garbage text | Content-Length: 0 | POST body not sent |
| Truncated text | `m_len` high | Duplicate `m_len += got` in drive.cpp |
| Works 1st call, hangs 2nd | No new `perform OK` | Q2 stale state (unknown root cause) |

### Phase 6: Fix

- **BASIC issue** → edit code, `inject`, `run`
- **Firmware issue** → edit C++, `build --flash`, wait, `inject`, `run`
- Loop until behavior matches expectations

### Phase 7: Release the hardware

When the session ends — fixed, or handing back for someone to look at —
release what the tooling is holding:

```bash
python3 scripts/meatloaf_debug.py capture stop   # frees the serial port
python3 scripts/meatloaf_debug.py echo stop      # if the echo server was started
```

Remove any temporary instrumentation (timing probes, checkpoint writes to
`$D7FF`) and reflash, so the device is left running the code the repo
describes rather than a debug build.

---

## Diagnostic Techniques

### A. The Byte Dump Test (asc/dump)

When you suspect PETSCII/ASCII encoding issues in the IEC read path,
inject a minimal program that prints raw byte values instead of characters:

```basic
10 open 1,8,2,"http://192.168.1.131:8080/echo"
20 print#1,"m get"
30 print#1,"s"
40 print#1,"status"
50 get#1,a$:if st<>0 then 80
55 rem *** dump raw byte value ***
60 print asc(a$);" ";
70 goto 50
80 close 1
```

This tells you exactly what bytes the C64 receives — no encoding ambiguity.
`asc(a$)` returns the raw byte from the server, NOT PETSCII-decoded.

### B. Echo Server + Response Size Sweep

When response data is truncated, swap the real API for the echo server
and test with varying body sizes:

```bash
python3 scripts/meatloaf_debug.py echo start
```

Point BASIC at `http://192.168.1.131:8080/echo`.

The echo server logs every request with hex dumps and echoes back method,
headers, and body — full round-trip visibility.

### C. Single-Query vs Multi-Query Testing

- **Single-query** (fresh inject per test) = proves firmware works for one request
- **Multi-query** (shared BASIC session, multiple round-trips) = reveals state machine bugs

If single-query passes but multi-query fails → the bug is in session/state reuse.

### D. Serial Recovery After Flash

Stop the capture before flashing (see "When to stop it"). Afterwards:
1. Build + flash (the capture must not be holding the port)
2. Wait 3s for ESP to reboot
3. Wait up to 20s for serial data to reappear
4. Proceed with inject + RUN

Manual recovery:
```bash
python3 scripts/meatloaf_debug.py capture wait --timeout 20
```

### E. Handle Stale BASIC Programs

If the C64 has a runaway program or one stuck in I/O:
```bash
python3 scripts/meatloaf_debug.py stop
# Press ENTER for READY prompt
python3 scripts/meatloaf_debug.py type ""
# Now read screen to verify READY
python3 scripts/meatloaf_debug.py screen
```

If STOP doesn't work (program in KERNAL I/O), use reset:
```bash
python3 scripts/meatloaf_debug.py reset
```

### F. IEC bus and user port signals ($DD00-$DD03)

`bus` decodes CIA#2 in one read — the IEC serial lines, the user port byte
used by parallel speeders, and both data direction registers.

```bash
python3 scripts/meatloaf_debug.py bus
```

**The polarity is not uniform, and this is the easiest thing to get wrong
reading `$DD00` by hand.** OUT bits are inverted; IN bits are not:

| Bit | Line | Sense |
|---|---|---|
| 3 | ATN **OUT** | `1` = Low (asserted), `0` = High |
| 4 | CLK **OUT** | `1` = Low (asserted), `0` = High |
| 5 | DATA **OUT** | `1` = Low (asserted), `0` = High |
| 6 | CLK **IN** | `1` = High, `0` = Low (asserted) |
| 7 | DATA **IN** | `1` = High, `0` = Low (asserted) |
| 0-1 | VIC-II bank (inverted) | bank = `3 - (bits 0-1)` |
| 2 | RS-232 TXD / user port pin M | also a handshake line for parallel cables |

**Known-good idle baseline** (C64 at READY, nothing on the bus) — compare
against this first, since a stuck line shows up immediately:

```
$DD00 = $C7   all five IEC lines released
$DD02 = $3F   Port A DDR: bits 0-5 output, 6-7 input  (KERNAL default)
$DD01 = $FF   user port idle
$DD03 = $00   user port all inputs
```

**SRQ is NOT in `$DD00`.** On the C64 the IEC SRQ line arrives at CIA#1
`/FLAG` and is latched in `$DC0D` bit 4, shared with the datasette input.
`bus --srq` samples it, but reading a 6526 ICR **clears its latched interrupt
flags** — never do it during a live transfer or while anything depends on
FLAG interrupts. That is why it is opt-in rather than part of the default read.

**Parallel speeders (DolphinDOS / SpeedDOS)** move the byte over the user
port: `$DD01` is the data, `$DD03` the direction (`$00` = all inputs, C64
receiving; `$FF` = all outputs, C64 sending). A parallel cable that is not
working usually shows as `$DD03` never leaving `$00`.

#### What it can and cannot see

Sampling goes over the U64 REST API at roughly 10 Hz, so it observes bus
**phases**, not individual bit edges — IEC bit timing is microseconds.

That is more useful than it sounds. Measured on hardware 2026-08-19, sampling
every 100 ms across a real `LOAD"ML:RCS",8`: **65 samples, zero timeouts, six
distinct `$DD00` values** (`$C7` idle, then `$07 $27 $47 $67 $87` as CLK/DATA
handshaked). So it answers the questions that actually matter:

- Is a line stuck asserted? (compare against the idle baseline)
- Is the bus doing anything at all? → `bus --samples 60 --interval 0.1`
  reports `bus active - N distinct states seen` or `bus idle or stuck`
- Is the DDR configured the way the protocol needs?

It will **not** give you a bit-level trace of a transfer. For that you need a
logic analyser on the port, or Meatloaf-side `VERBOSE_PROTOCOL` logging.

Note the C64 stays responsive to `readmem` during an IEC transfer — the
blocking-KERNAL-I/O caveat that affects *keyboard* injection does not stop
memory reads.

---

## Known Issues & Fix Patterns

### 1. POST Body Not Sent (Content-Length: 0)

**Symptom:** Server receives `Content-Length: 0`. Serial shows no
`sendRequest: POST` or shows CL:0.

**Root cause:** `openAndFetchHeaders()` calls `esp_http_client_open(_, 0)`
for POST/PUT before the body is set.

**Fix** in `lib/network/http.cpp`, function `openAndFetchHeaders()`:
```cpp
if (method == HTTP_METHOD_POST || method == HTTP_METHOD_PUT) {
    _is_open = true;
    return 200;  // provisional — real status from perform() in close()
}
```

### 2. Response Body Not Visible on C64

**Symptom:** `STATUS: 200` printed but no body appears.

**Root cause:** `read()` checks `_is_open` before checking
`postResponse`/`preservedPostResponse`. After `close()` destroys the HTTP
handle, `_is_open` is false and the body pointers are read too late.

**Fix** in `read()`: check `preservedPostResponse` BEFORE `_is_open`,
check `postResponse` BEFORE `_is_open`. Always transfer
`preservedPostResponse` even when empty.

### 3. Buffer Truncation (Duplicated `m_len += got`)

**Symptom:** Response truncated at ~256 bytes body. Only with real API
responses, not small echo server bodies.

**Root cause:** In `lib/device/iec/drive.cpp:359-360`, `m_len += got;`
appears twice. This doubles the buffer-fill count. For responses >256 bytes,
the doubled m_len exceeds `BUFFER_SIZE=512` and the fill loop exits before
`eos()` returns true.

**Fix:** Remove the duplicate `m_len += got;` (keeping the surrounding code).
Confirmed fix, commit `51116209`.

### 4. Q2: Second POST Uses Cached State

**Symptom:** First request works. Second request returns status 200 from
stale state without making a new HTTP request. Serial shows no new
`perform OK`.

**Status:** Root cause unknown. The `sendRequest()` → `client.POST()` path
may return cached status without calling `esp_http_client_perform()`.

**Workaround:** Close and reopen the channel between requests in BASIC:
```basic
close ch
open ch,8,2,ap$
```

Or: re-inject the BASIC program fresh for each query (single-query pattern).

---

## PETSCII Quick Guide

The C64 uses PETSCII, not ASCII. Here's what you actually need to know.

### Screen reading (covered — just use the script)

The `screen` command reads $0400 screen memory and decodes PETSCII screen
codes to ASCII. It always assumes **mixed-case mode** (the normal mode when
Meatloaf or BASIC programs are running), which means:
- Screen codes 1-26 → lowercase `a`-`z`
- Screen codes 65-90 → uppercase `A`-`Z`
- Everything else → punctuation, digits, or placeholder

No charset detection, no mode switching. Just works.

**Known cosmetic quirk:** What you see in the BASIC editor will have
inverted letter case for direct keyboard-typed commands (e.g. `RUN` appears
as `run`). This is normal PETSCII display behavior — BASIC parses the
commands correctly either way. It does NOT affect the screen output from
`PRINT` statements, which show the correct mixed case.

### Keyboard buffer injection (send unshifted PETSCII — do NOT case-swap)

The keyboard buffer at $0277 expects **PETSCII key codes**:

| Code range | Meaning | BASIC tokenises it? |
|---|---|---|
| `0x41`-`0x5A` | UNSHIFTED letters — same byte values as ASCII `A`-`Z` | **yes** |
| `0x61`-`0x7A` | SHIFTED letters — drawn as graphics characters | no |

So a BASIC command is injected as plain **ASCII uppercase**, byte for byte.
`type_text()` passes those through unchanged and upcases ASCII lowercase into
the same range, so `type "PRINT 6*7"` and `type "print 6*7"` both work.

**There is no keyboard case inversion — do not reintroduce a swap.** This
skill used to swap the case of every letter before sending, which put every
command in the `0x61`-`0x7A` shifted range: the text appears on screen but
BASIC answers `?SYNTAX ERROR`, so it looks like a BASIC bug rather than an
injection bug. Fixed and verified on hardware 2026-08-19 (`PRINT 6*7` → `42`).

The inversion is an artifact of reading the SCREEN back, not of the keyboard.
`_decode_screen()` renders screen codes 1-26 as lowercase (it assumes
mixed-case mode), so a command typed as uppercase reads back lowercase. That
is the cosmetic quirk described above — it is not a signal to swap on input.

Shifted/graphics characters have no ASCII spelling; pass their PETSCII byte
directly with `chr()` if you need one.

### A. String literals in BASIC (tokenizer uppercases them)

When you write `print#1,"b content"`, the C64 BASIC tokenizer converts
alphabetic characters to uppercase in memory → `"CONTENT"`.

**Use `chr$()` to preserve case:**
- `"` → `chr$(34)` → define `qu$=chr$(34)` at program top
- `{` → `chr$(123)` → define `ob$=chr$(123)`
- `}` → `chr$(125)` → define `cb$=chr$(125)`
- `_` → `chr$(95)` → for command params like `max_tokens`

```basic
10 qu$=chr$(34):ob$=chr$(123):cb$=chr$(125)
20 bd$=ob$+qu$+"name"+qu$+":"+qu$+"test"+qu$+cb$
```

### B. Reading data via GET# (raw bytes from server)

**`asc(a$)` returns the raw byte the server sent** — no PETSCII conversion.
If the server sends lowercase `"content":`, `asc(a$)=99` matches `chr$(99)`,
NOT `chr$(67)` (uppercase C). This is reliable for byte-level comparisons.

### C. Sending commands via PRINT# (firmware handles it)

The Meatloaf firmware calls `toUTF8()` on incoming command bytes to map
PETSCII uppercase back to ASCII lowercase for case-insensitive matching.
This is transparent to BASIC code.


---

## Environment Variables

Every build setting defaults to what `platformio.ini` says. These variables
exist to override it for a single run (a second board, a different port);
`config` reports which ones are in force under `overrides`.

| Variable | Default | Purpose |
|----------|---------|---------|
| `U64_IP_ADDRESS` | `[meatloaf] u64_ip_address`, else `192.168.1.176` | IP of the Ultimate 64 |
| `U64_PASSWORD` | (empty) | X-Password header if configured. Env var only — never put it in `platformio.ini`, which is shared with the sample file |
| `MEATLOAF_DIR` | auto-detected (nearest ancestor with a `platformio.ini`) | Path to meatloaf firmware source |
| `MEATLOAF_BUILD_ENV` | `[meatloaf] environment` | Build environment (e.g. fujiloaf-rev0); also selects which `[env:…]` section board/flags are read from |
| `MEATLOAF_UPLOAD_PORT` | `monitor_port` / `upload_port` | Upload and serial port |
| `MEATLOAF_MONITOR_SPEED` | `monitor_speed` | UART baud rate |
| `MEATLOAF_DEBUG_SKILL_DIR` | this skill's directory | Location of `scripts/` |

---

## Trigger Keywords

"commodore 64", "c64", "ultimate 64", "u64", "meatloaf", "esp32",
"iec", "petscii", "fujiloaf", "serial capture", "c64 remote",
"c64 debugging", "commodore networking", "commodore HTTP",
"1541u", "c64 basic", "$0801", "$0400", "keyboard buffer",
"screen memory", "dev/ttyUSB0", "ultimate 64 api",
"iec signals", "atn", "clk", "data", "srq", "$dd00", "$dd01", "$dd03",
"$dc0d", "cia2", "cia1", "user port", "parallel cable", "dolphindos",
"speeddos", "bus state", "stuck line", "data direction register",
"cc65", "cl65", "ld65", "ca65", ".prg", "load address",
"vice label", "vice monitor", "vice label file", "debugreg",
"$d7ff", "main start", "exe header", "exe hdr", "kickass"

---

## Cross-references

- For writing Commodore 64 BASIC v2 programs (bc64 compiler, POKEs,
  sprites, SID, hardware control), see the **c64-basic** skill.
- For the Meatloaf full-mode HTTP client protocol — the command
  language (`m`, `h`, `b`, `s`, `status`, `j`, `c`) and response
  reading patterns — see the **meatloaf-networking** skill.
- For JSON Pointer (RFC 6901) syntax used with the `j` command,
  see [json_pointer.md](../commodore64-networking/json_pointer.md)
  in the networking skill.
- For JiffyDOS disk shortcuts, wedge commands, and fast disk I/O
  while debugging, see the **c64-jiffydos** skill.
