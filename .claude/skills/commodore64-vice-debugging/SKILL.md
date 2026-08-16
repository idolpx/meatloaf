---
name: commodore64-vice-debugging
description: |
  Debug Commodore 64 programs running under the VICE emulator through its
  binary monitor: BASIC, cc65-compiled C, and 6502 assembly. Automates the
  unattended cycle — write/modify source → build (petcat/bc64, cl65, ca65) →
  autostart in VICE → set symbol breakpoints and watchpoints → read
  registers, memory and screen → patch → repeat. Use whenever the user
  mentions VICE, x64sc, x128, the VICE binary monitor, emulator breakpoints,
  checkpoints or watchpoints, stepping 6502 code, reading C64 screen memory,
  cc65 label files (-Ln) or map files, petcat tokenizing, c1541 disk images,
  or automated/unattended testing of a C64 program without real hardware.
  Also trigger for "why does my PRG crash in VICE", "break when something
  writes $d020", "dump memory at a symbol", or driving an emulator from a
  script. For the same cycle against real Ultimate 64 hardware and Meatloaf
  ESP32 firmware, use commodore64-debugging instead.
---

# C64 Debugging with VICE's Binary Monitor

Drives a real VICE emulator over TCP so a C64 program can be built, run,
breakpointed, inspected and patched without a human touching the emulator.

```
   source (.bas / .c / .s)
        │  build  (petcat | bc64 | cl65 | ca65+ld65)
        ▼
     foo.prg  +  foo.lbl (symbols)  +  foo.map
        │  autostart
        ▼
┌──────────────────────┐   TCP 6502 (binary monitor)   ┌──────────────┐
│   VICE  (x64sc)      │◄─────────────────────────────►│ vice_debug.py│
│   emulated C64       │  checkpoints, regs, memory    │  (this PC)   │
└──────────────────────┘                               └──────────────┘
```

**Scope:** the C64 program is what gets debugged. This skill does not talk to
Meatloaf firmware or any ESP32 — VICE has no bridge to a real IEC device. For
that loop use the **commodore64-debugging** skill.

---

## Read this first — three verified behaviours that will waste hours

All three were established empirically against VICE 3.10 on Windows. They are
not in the protocol documentation.

### 1. Never disconnect while the emulator is halted

If a monitor client disconnects while the machine is stopped at a checkpoint,
**VICE tears down its monitor listener permanently**. The emulator keeps
running and its window stays responsive, but nothing can ever connect again —
port 6502 refuses connections until VICE is restarted.

Consequence: **one CLI process per command does not work once a breakpoint
fires.** Everything that must observe a halted machine has to happen on a
single connection. That is exactly what `batch` is for:

```bash
# WRONG - the process after `wait` disconnects while halted, killing the monitor
python vice_debug.py brk _bump
python vice_debug.py wait
python vice_debug.py regs          # -> connection refused, VICE now unreachable

# RIGHT - one connection for the whole stop/inspect/resume sequence
python vice_debug.py batch "brk _bump" "autostart foo.prg" wait regs "mem _counter -n 1"
```

`ViceSession.close()` always resumes before closing, and `main()` closes the
shared session in a `finally`, so the CLI cannot trip this on its own. Custom
code using `ViceMonitor` directly must resume by hand.

### 2. Keyboard feed is only delivered after the client disconnects

`keyboard_feed` (0x72) queues text that VICE holds for as long as any binary
monitor client is connected. Polling the screen for 20 s while connected shows
nothing; the text appears the instant the connection drops.

`type` handles this for you — it feeds, drops the connection, waits, and
reconnects to show the screen. Know about it anyway, because it explains the
classic symptom: **a typed command appearing to run one invocation late**, or
seemingly duplicating itself.

Prefer `autostart` over typing `RUN` wherever possible — autostart works
normally while connected.

### 3. Register IDs are assigned by the emulator, never hardcoded

`registers get` returns `(id, value)` pairs with no names. The IDs differ per
emulator and per VICE build. On this x64sc 3.10: `A=0, X=1, Y=2, PC=3, SP=4,
FL=5`, plus `LIN=53, CYC=54, 00=55, 01=56`. Always resolve them through
`registers available` (0x83) first — `vice_monitor.py` does this and caches it,
so you work with `{"PC": 2112, "A": 1, ...}`.

---

## Quick start

```bash
cd .claude/skills/commodore64-vice-debugging/scripts

python vice_debug.py config                 # what got discovered
python vice_debug.py launch                 # start VICE with -binarymonitor
python vice_debug.py cycle ../../../my.bas  # build, run, show the screen
python vice_debug.py kill                   # clean shutdown
```

`config` resolves every tool without hardcoding paths and reports what it
found. Run it before anything else when something behaves oddly:

```json
{
  "machine": "c64",
  "emulator": "C:\\vice\\bin\\x64sc.EXE",
  "petcat": "C:\\vice\\bin\\petcat.EXE",
  "c1541": "C:\\vice\\bin\\c1541.EXE",
  "cl65": "C:\\Users\\you\\Tools\\cc65\\bin\\cl65.EXE",
  "bc64": null,
  "basic_compiler": "petcat",
  "monitor_host": "127.0.0.1",
  "monitor_port": 6502,
  "monitor_reachable": true
}
```

---

## Skill components

```
commodore64-vice-debugging/
├── SKILL.md                        ← this file: workflow + reference
├── scripts/
│   ├── vice_monitor.py             ← binary monitor protocol client (standalone)
│   ├── vice_toolchain.py           ← tool discovery, building, label parsing
│   └── vice_debug.py               ← CLI + ViceSession automation
├── reference/
│   └── binary_monitor_protocol.md  ← full opcode / packet reference
└── evals/evals.json
```

`vice_monitor.py` has no dependencies beyond the standard library and imports
nothing else from the skill — import it directly to drive VICE from your own
Python.

---

## Building

`build` dispatches on extension and always emits a `.prg`.

| Source | Toolchain | Notes |
|--------|-----------|-------|
| `.bas` | `bc64` if installed, else VICE's `petcat -w2` | petcat writes the `$0801` header itself |
| `.c` | `cl65 -t c64 -g -Ln foo.lbl --mapfile foo.map` | `-g`, no `-O`, so variables survive inspection |
| `.s` `.asm` | same driver plus `-C c64-asm.cfg -u __LOADADDR__` | required — see below |
| `.prg` | none | passed through |

```bash
python vice_debug.py build game.c                # -> game.prg, game.lbl, game.map
python vice_debug.py build game.c --release      # -Oirs instead of -g
python vice_debug.py build main.s -o main.prg
```

**Assembly needs the asm linker config.** A standalone `.s` has none of the C
runtime's segments, so the default `c64.cfg` fails to link with
`Error: Start address of memory area 'BSS' is not constant`. The builder adds
`-C <machine>-asm.cfg -u __LOADADDR__` automatically, and steps aside if you
pass your own `-C`. A minimal source that links with it:

```asm
        .segment "EXEHDR"           ; BASIC stub so RUN works
        .word   nextline
        .word   10
        .byte   $9e, "2061", $00    ; SYS 2061 -> $080D
nextline:
        .word   0

        .segment "CODE"
        .export start
start:  lda #$06
        sta $d020
        jmp start
```

**Name your outputs distinctly.** `build foo.bas` and `build foo.c` both
default to `foo.prg`. Building one after the other leaves a `.prg` from one
language beside a `.lbl` from the other, and every breakpoint silently targets
addresses that are not in the running program. Use `-o`.

### Disk images

```bash
python vice_debug.py d64 loader.s game.c -o disk.d64 --name mygame
```

Needed when the program loads further files at runtime or you want true drive
emulation — autostarting a bare `.prg` gives it no disk to read from.

---

## Deploying

| Command | What it does | When |
|---------|--------------|------|
| `autostart <file>` | Hands the file to VICE's own autostart | Default. Handles `.prg`, `.d64`, `.t64`, `.crt`, sets up TDE, types RUN itself |
| `load <file.prg>` | Writes the body into RAM, does not run | When you need the code resident so you can set checkpoints and poke memory *before* the first instruction |

`load` strips the 2-byte little-endian load-address header and writes the body
to that address. For a program at `$0801` it also fixes the BASIC pointers at
`$2D`/`$2F`/`$31` to just past the program end — without that, `RUN` sees a
zero-length program. It is skipped automatically for anything not loading at
`$0801`, since C and assembly programs use that zero page for their own
globals. Override with `--basic` / `--no-basic`.

```bash
python vice_debug.py autostart game.prg --wait 4     # run and show the screen
python vice_debug.py load game.prg                   # resident, not running
python vice_debug.py load game.bas --run --wait 4    # build, inject, RUN
```

---

## Breakpoints, watchpoints and symbols

VICE calls them all **checkpoints**; the operation bits decide the kind:
exec (`0x04`) is a breakpoint, load (`0x01`) and store (`0x02`) are
watchpoints.

```bash
python vice_debug.py labels game.lbl        # load symbols (persists between calls)

python vice_debug.py brk _main              # break on execute (default)
python vice_debug.py brk '$d020' --store    # break when anything writes the border
python vice_debug.py brk _buf _bufend --load --trace   # log reads of a range, don't stop
python vice_debug.py brk _bump --temp       # delete after the first hit
python vice_debug.py brks                   # list
python vice_debug.py delbrk --all
```

Addresses accept `$c000`, `0xc000`, bare hex `c000`, decimal `#49152`, or any
symbol from the loaded label file. cc65 prefixes C symbols with `_`, and the
resolver tries that prefix for you, so `brk main` finds `_main`.

`--cond` attaches a VICE monitor condition expression:

```bash
python vice_debug.py brk _bump --cond "A == 3"
```

Verified: `A == 99` never fires, `A == 1` fires with `A=1` at the stop.

**Quoting on Windows:** PowerShell mangles nested quotes when passing them to
a native executable, so `--cond "A == 3"` inside a quoted batch argument
arrives split into pieces. Use a batch file, where each line is parsed by the
skill and nothing else touches it:

```bash
python vice_debug.py batch -f session.txt
```

```text
# session.txt — blank lines and # comments are skipped
labels game.lbl
delbrk --all
brk _bump --exec --cond "A == 1"
autostart game.prg
wait --timeout 20
regs
mem _counter -n 1
```

---

## Inspecting

```bash
python vice_debug.py regs                       # decoded, with symbol for PC
python vice_debug.py regs --set A=7 X=1
python vice_debug.py mem _counter -n 16         # hex dump
python vice_debug.py mem '$0400' -n 1000 --ascii
python vice_debug.py mem '$c000' -n 256 --raw > dump.bin
python vice_debug.py poke _counter 04
python vice_debug.py poke '$d020' --text HELLO
python vice_debug.py screen
```

**Memory reads default to side-effects OFF.** Reading an I/O register such as
`$D019` *with* side effects clears latched bits and changes what the running
program sees — a debugger that alters the bug is worthless. Pass
`--side-effects` only when you specifically want that.

### Screen reading

`screen` resolves where screen RAM actually is rather than assuming `$0400`:
the VIC bank comes from `$DD00` bits 0-1 (inverted) and the matrix offset from
`$D018` bits 4-7. Programs that relocate the screen — common in C and
assembly — are read correctly.

It also picks the character set from `$D018` bit 1, so uppercase/graphics mode
and mixed-case mode both decode properly. Reverse-video codes are folded onto
their normal characters so a reverse-printed line still reads as text.

```
+----------------------------------------+
|hello from cc65                         |
|counter=5                               |
```

---

## Execution control

| Command | Effect |
|---------|--------|
| `wait --timeout N` | Block until a stopped/JAM event; prints registers, PC symbol, and which checkpoint fired |
| `step [n] [--over]` | Advance n instructions; `--over` steps over `JSR` |
| `ret` | Run until the current subroutine returns |
| `cont` | Resume |
| `reset [--hard]` | Soft reset, or `--hard` for a power cycle |

`wait` exits non-zero if nothing stopped within the timeout, so a script can
tell "hit the breakpoint" from "ran to completion".

---

## The unattended debug cycle

**Phase 1 — set up once per session**

```bash
python vice_debug.py diag      # tools, reachability, current checkpoints, screen preview
python vice_debug.py launch    # if diag says the monitor is unreachable
```

**Phase 2 — one iteration, in a single `batch`**

```bash
python vice_debug.py batch \
  "build game.c -o game.prg" \
  "labels game.lbl" \
  "delbrk --all" \
  "brk _update_sprites" \
  "autostart game.prg" \
  "wait --timeout 20" \
  regs \
  "mem _sprite_x -n 8" \
  screen
```

**Phase 3 — read the evidence**

| Observation | Likely meaning |
|-------------|----------------|
| `wait` times out, screen shows `READY.` | Program ran and exited, or never started — check the screen for a BASIC error |
| `wait` times out, screen shows program output | Breakpoint address is wrong: stale `.lbl`, or `.prg` and `.lbl` from different builds |
| Stops immediately at an address far from your code | You broke inside the KERNAL/BASIC ROM; check the symbol resolved as expected |
| `?SYNTAX ERROR` after a manual `load --run` | BASIC pointers not fixed, or a non-BASIC PRG loaded to `$0801` and RUN typed |
| `JAM` event | Illegal opcode — usually a JMP/JSR through an uninitialised pointer |
| Screen full of graphics glyphs | Charset/`$D018` differs from what the program set; the decoder follows it, so this is real |
| Connection refused, VICE window alive | A client disconnected while halted — restart VICE (gotcha 1) |

**Phase 4 — fix and repeat.** Edit source, rerun the same batch. Nothing needs
restarting unless gotcha 1 was hit.

---

## Using it as a library

```python
import sys
sys.path.insert(0, ".claude/skills/commodore64-vice-debugging/scripts")
from vice_debug import ViceSession

with ViceSession() as s:                 # resumes + closes cleanly on exit
    s.use_labels("game.lbl")
    s.brk("_update_sprites")
    s.autostart("game.prg")
    if s.mon.wait_for_stop(20):
        print(s.where())                 # registers + PC symbol
        print(s.mon.read_memory(s.addr("_sprite_x"), 8).hex())
    print("\n".join(s.screen()))
```

The lower layer is usable on its own:

```python
from vice_monitor import ViceMonitor, OP_STORE

with ViceMonitor() as m:
    m.set_checkpoint(0xD020, operation=OP_STORE)
    ev = m.wait_for_stop(30)
    print(ev.name, hex(m.last_pc))
    m.resume()                            # REQUIRED before the connection drops
```

---

## Command reference

```
config      show discovered tools, monitor state, loaded labels
diag        full health check incl. screen preview and live checkpoints
launch      start VICE with the binary monitor (--prg, --warp, --no-run, --extra)
kill        quit VICE over the monitor, falling back to the pid
ping        check the connection
info        VICE version, banks, registers, register table, screen geometry

build       compile .bas/.c/.s to .prg (-o, --release, --extra)
d64         build a .d64 from several programs (-o, --name)
labels      load or show a VICE label file

autostart   let VICE load and RUN (--no-run, --wait)
load        write a .prg into RAM (-a, --run, --basic, --no-basic, --wait)
type        feed text to the keyboard (--no-return, --wait)

screen      decode screen memory (--raw, --json)
regs        read or write registers (--set NAME=VALUE ...)
mem         read memory (-n, --raw, --ascii, --side-effects)
poke        write memory (hex bytes, --file, --text)

brk         set a checkpoint (--exec, --load, --store, --temp, --trace, --cond)
brks        list checkpoints
delbrk      delete checkpoints (numbers, or --all)

step        advance N instructions (--over)
ret         run until subroutine return
cont        resume
wait        block until stopped (--timeout, --screen)
reset       reset the machine (--hard, --wait, --screen)

cycle       build + launch-if-needed + reset + autostart + screen
batch       run several subcommands on ONE connection (-f FILE, --stop-on-error)
```

Global: `--machine` (c64, c128, vic20, plus4, pet, scpu64, c64dtv),
`--port`.

---

## Environment variables

Everything is discovered automatically; these override a single run.

| Variable | Purpose |
|----------|---------|
| `VICE_MACHINE` | `c64` (default), `c128`, `vic20`, `plus4`, `pet`, `scpu64`, `c64dtv` |
| `VICE_EMULATOR` | Full path to `x64sc`/`x128`/… , skipping lookup |
| `VICE_BIN_DIR` | Directory searched first for VICE tools |
| `VICE_MONITOR_HOST` | Default `127.0.0.1` |
| `VICE_MONITOR_PORT` | Default `6502` |
| `CC65_BIN_DIR` | Directory holding `cl65`/`ca65`/`ld65` |
| `BC64` | Full path to the bc64 BASIC compiler |

Discovery checks `PATH`, then common install locations (`C:\vice\bin`,
`/usr/local/bin`, `/opt/homebrew/bin`, `~/Tools/cc65/bin`, …).

---

## Launch failures

VICE aborts during startup (Windows exit code `0xe0464645`) when it **cannot
bind the monitor address** — almost always a previous instance that was
force-killed moments earlier and whose socket has not been released. `launch`
retries once after a 2 s pause, and `kill` waits for the port to actually close
before returning. If it persists:

```bash
netstat -ano | findstr 6502          # find the holder
python vice_debug.py --port 6510 launch
```

Prefer `kill` over killing the process: it sends the monitor's QUIT command,
which shuts VICE down cleanly and releases the port.

---

## cc65 debug outputs worth generating

| Flag | Produces | Use |
|------|----------|-----|
| `-Ln foo.lbl` | VICE label file | **Always.** This is what makes `brk _main` work; `build` adds it |
| `--mapfile foo.map` | Segment addresses and sizes | Find where BSS/DATA/CODE actually landed |
| `--listing foo.lst` | Asm interleaved with C | When you suspect codegen |
| `-g` | Debug info, no optimisation | Default here; `-O` folds variables away |
| `-Wl --dbgfile,foo.dbg` | VICE/CCS64 debug info | Source-level stepping in VICE's own GUI monitor |

Reading the map file:

```text
$ grep -E "(__MAIN|__BSS|__STACK)" game.map
__MAIN_START__   00080D RLA    __MAIN_SIZE__   00C7F3 REA
__BSS_RUN__      005415 RLA    __BSS_SIZE__    000031 REA
```

`RLA` = relocatable address (a real C64 address after linking), `REA` = a
relocatable expression (may be a constant, not an address), `RLZ` = zero page.

---

## Trigger keywords

"vice", "x64sc", "x128", "xvic", "xplus4", "binary monitor", "remote monitor",
"checkpoint", "watchpoint", "breakpoint", "emulator", "petcat", "c1541",
"cc65", "cl65", "ca65", "ld65", "label file", "-Ln", "map file", "6502 step",
"screen memory", "$0400", "$d018", "$dd00", "$d020", "commodore 64", "c64",
"prg", "d64", "autostart", "jam", "6502 assembly debugging"

---

## Cross-references

- Writing C64 BASIC v2 (bc64, POKEs, sprites, SID) → **commodore64-basic**
- Same debug cycle on real hardware (Ultimate 64 + Meatloaf ESP32) →
  **commodore64-debugging**
- JiffyDOS wedge commands and fast disk I/O — VICE images often carry the
  JiffyDOS ROM, which is visible in the boot screen → **commodore64-jiffydos**
- Meatloaf DOS command channel → **commodore64-meatloaf**
- Meatloaf HTTP client protocol → **commodore64-networking**
