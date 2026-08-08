---
name: meatloaf-dos
description: |
  Reference for Meatloaf's DOS command channel — the complete set of
  drive-level commands accepted over the IEC bus secondary address 15
  on a Meatloaf ESP32 device. Use whenever the user asks about Meatloaf
  DOS commands, custom fast-loader toggles, directory navigation, tape
  image operations, time/clock access, partition switching, or the
  `auth:` credential command. Also covers the standard CBM DOS 2.6
  commands (b-r, b-w, m-r, m-w, m-e, n:, s:, r:, u1/ua, uj, etc.) as
  supported by Meatloaf, plus the full set of SD2IEC-style extensions
  (cp, md, rd, p, pwd, xr, t-*, etc.). The protocol is the same
  whether driven from BASIC V2 (open/print#/input#), a dos wedge
  (@ prefix via JiffyDOS), or a c/assembly client. For the full-mode
  HTTP client protocol (m, h, b, s, status, r-h, r-b, j — secondary
  address 2), prefer the **meatloaf-networking** skill instead.
---

# Meatloaf DOS Commands — Complete Reference

Meatloaf is an ESP32 firmware that plugs into the C64's IEC bus,
emulating one or more virtual disk drives. This document covers every
command you can send to a Meatloaf drive over its **command channel**
(secondary address 15, device number 8 by default). It documents the
standard CBM DOS 2.6 commands Meatloaf responds to, the SD2IEC-style
extensions it inherits, and the Meatloaf-specific commands added in
this firmware.

> **Not what you're looking for?** If you want to make HTTP requests
> (GET/POST/PUT) with custom headers and JSON parsing, you want the
> **full-mode HTTP client** protocol — secondary address 2, commands
> `m`, `h`, `b`, `s`, `status`, `r-h`, `r-b`, `j`. See the
> **meatloaf-networking** skill for that.

---

## How to send DOS commands

There are three ways to send commands to a Meatloaf drive:

### 1. BASIC V2 (works everywhere)

**CRITICAL: Always use lowercase BASIC keywords.** On the C64 you
normally type with shift-lock off, so `open`, `print#`, `close`,
`input#`, `get#`, `rem`, `load`, `save`, `list`, `run`, `chr$` are
all lowercase. Never write `OPEN`, `PRINT#`, `CLOSE`, `INPUT#`,
`GET#`, `REM`, `LOAD"$",8` etc. in code examples — that looks wrong.
Uppercase BASIC is never correct in examples.

```basic
open 15,8,15    : rem open command channel to device 8
print#15,"cmd"  : rem send command
close 15        : rem close channel
```

For commands that return data (like `pwd`), read the error/status
channel after sending:

```basic
open 15,8,15
print#15,"cmd"
input#15, e, e$, t, s  : rem e=status code, e$=message, t=track, s=sector
close 15
print e$ : rem prints the response
```

For commands that return multi-line data:
```basic
open 15,8,15
print#15,"cmd"
get#15, a$ : rem read one byte at a time
rem ... loop until st and 64 (eoi)
close 15
```

**Gotcha: colons and `input#`.** BASIC's `input#` treats `:` as a
field separator. Commands whose output contains colons (like `t-ra`
and `t-ri` which return `HH:MM:SS` times) will break with `input#`.
Use `get#` in a loop for those instead.

### 2. JiffyDOS `@` wedge (if JiffyDOS is active)

If the C64 has a JiffyDOS Kernal ROM, just type `@cmd` at the ready.
prompt. JiffyDOS forwards everything after `@` to the drive's command
channel and prints the response automatically.

```
@cmd     ← sends "cmd" to drive command channel, shows response
@        ← by itself, reads the error channel
```

This works for **all commands** in this reference — `@pwd`, `@md:dir`,
`@cp2`, `@t-ra`, etc.

### 3. C (cc65) / 6502 assembly

```c
#include <cbm.h>
cbm_open(15, 8, 15, "");              // open command channel
cbm_write(15, "cmd\r\n", 5);          // send command
cbm_close(15);                         // close
```

---

## Quick reference (all commands)

| Command | Category | Description |
|---------|----------|-------------|
| `i` | Standard DOS | Initialize (reset) drive |
| `uj` | Standard DOS | Cold reset drive |
| `u9` / `ui` | Standard DOS | Warm reset (re-run DOS vectors) |
| `u{shift-j}` | Standard DOS | Hard reset |
| `u{shift-j}+` | Standard DOS | **Reboot ESP32** |
| `ui+` / `ui-` | Standard DOS | Bus speed (C64 / VIC-20) |
| `n:name,id` | Standard DOS | Format disk image |
| `s:pattern` | Standard DOS | Scratch (delete) files |
| `r:new=old` | Standard DOS | Rename file |
| `c:new=old` | Standard DOS | Copy file |
| `v` | Standard DOS | Validate BAM |
| `b-p ch,pos` | Standard DOS | Buffer pointer (position in stream) |
| `b-r ch,trk,sec` | Standard DOS | Read block (seek to track/sector) |
| `b-w ch,trk,sec` | Standard DOS | Write block |
| `b-a trk,sec` | Standard DOS | Allocate block in BAM |
| `b-f trk,sec` | Standard DOS | Free block in BAM |
| `b-e trk,sec` | Standard DOS | Block execute |
| `m-r addr,len` | Standard DOS | Memory read |
| `m-w addr,len,data` | Standard DOS | Memory write |
| `m-e addr` | Standard DOS | Memory execute |
| `u1 ch,drv,trk,sec` | Standard DOS | Block read (UA: buffer, UB: track/sector) |
| `cd path` | Navigation | Change directory |
| `cp n` | Partition | Change to partition n (text arg) |
| `c{shift-p}n` | Partition | Change to partition n (binary byte) |
| `pwd` | Navigation | Print working directory |
| `md:path` | Navigation | Make directory (mkdir) |
| `rd:path` | Navigation | Remove directory (rmdir) |
| `p ch,hi,mid,lo` | Navigation | Position (seek) stream pointer |
| `t-c ms\|mmm:ss` | Tape | Set tape counter position |
| `t-i` | Tape | Build tape index (.idx) |
| `t-ra` | Time | Read time as ASCII |
| `t-ri` | Time | Read time as ISO-8601 |
| `t-rd` | Time | Read time as decimal |
| `t-rb` | Time | Read time as BCD |
| `t-rz` | Time | Read timezone |
| `t-wa` | Time | Write time (ASCII — not yet implemented) |
| `t-wi` | Time | Write time (ISO-8601 — not yet implemented) |
| `t-wd` | Time | Write time (decimal — not yet implemented) |
| `t-wb` | Time | Write time (BCD — not yet implemented) |
| `t-wz:timezone` | Time | Set timezone |
| `xr:romname` | Extended | Load a custom DOS ROM |
| `auth:user,pass` | Meatloaf | Set HTTP auth credentials |
| `vd+` / `vd-` | Meatloaf | Enable / disable VDrive mode |
| `ej+` / `ej-` | Fast loader | Toggle JiffyDOS fast loader |
| `ee+` / `ee-` | Fast loader | Toggle Epyx fast loader |
| `ea+` / `ea-` | Fast loader | Toggle Action Replay 6 fast loader |
| `ef+` / `ef-` | Fast loader | Toggle Final Cartridge 3 fast loader |
| `ed+` / `ed-` | Fast loader | Toggle DolphinDOS fast loader |

---

## Standard CBM DOS 2.6 commands

These are the classic 1541/1571/1581 commands. Meatloaf implements
most of them, either natively (for local SD/flash filesystems) or
through VDrive emulation (for disk images).

### Initialize — `i`, `uj`, `u9`/`ui`

| Command | Effect |
|---------|--------|
| `i` | Initialize the drive: close all channels, reset memory, re-read directory. Sets the status to the DOS version string. |
| `uj` | Cold reset — full re-initialization like `i` but also resets VDrive state. |
| `u9` / `ui` | Warm reset — re-runs the DOS vectors from `$FFFA`. Sets status to DOS version. |
| `u{shift-j}` | Hard reset — same as cold reset, but also resets the working directory to flash root (`/`). |
| `u{shift-j}+` | **Reboot the ESP32** — triggers `fnSystem.reboot()`. The entire device restarts. |

**BASIC V2:**
```basic
open 15,8,15 : print#15,"i" : close 15
```

**Wedge:**
```
@i
```

### Bus speed — `ui+` / `ui-`

| Command | Effect |
|---------|--------|
| `ui-` | Set bus timing for VIC-20 speed (20 µs data valid). |
| `ui+` | Set bus timing for C64 speed (60 µs data valid). Default is C64. |

Switching to VIC-20 speed can help with timing-sensitive setups.
The status code is always set to `OK`.

**BASIC V2:**
```basic
open 15,8,15 : print#15,"ui-" : close 15  : rem VIC-20 mode
open 15,8,15 : print#15,"ui+" : close 15  : rem C64 mode
```

### Format — `n:name,id`

Creates a new (empty) filesystem on the current media. When VDrive
mode is active (`vd+`), `n:` creates a new **disk image file** in the
current directory — the filename before the colon determines the image
name, and the id after the comma sets the disk label. When VDrive is
off, `n:` calls `m_cwd->format()` on the current directory.

```basic
open 15,8,15,"n:mydisk,01" : close 15
```

**Wedge:** `@n:mydisk,01`

### Scratch — `s:pattern`

Deletes files matching a pattern. Supports wildcards `*` (any
characters) and `?` (single character). Returns `FILES SCRATCHED`
with the count.

```basic
open 15,8,15,"s:backup.*" : close 15
```

**Wedge:** `@s:backup.*`

### Rename — `r:newname=oldname`

Renames a file. The `=` separator splits old name (right) from new
name (left). The match uses `isMatch()` which supports `*` and `?`.

```basic
open 15,8,15,"r:newgame=oldgame" : close 15
```

**Wedge:** `@r:newgame=oldgame`

### Copy — `c:newname=oldname`

Copies a file on the same media. Same `=` syntax as rename.

```basic
open 15,8,15,"c:backup=original" : close 15
```

**Wedge:** `@c:backup=original`

### Validate — `v`

Rebuilds the Block Availability Map (BAM) on disk media. Currently a
no-op that logs "validate bam" — the actual BAM rebuild is delegated
to VDrive when active.

```basic
open 15,8,15,"v" : close 15
```

**Wedge:** `@v`

### Buffer/Block commands — `b-p`, `b-r`, `b-w`, `b-a`, `b-f`, `b-e`

These operate on the currently open file stream (not on a raw disk
buffer like a 1541 would). They're adapted for Meatloaf's streaming
model:

| Command | Arguments | Effect |
|---------|-----------|--------|
| `b-p channel,pos` | channel, position | **Buffer pointer** — positions the stream's read/write cursor to `pos`. Calls `stream->position(pos)`. |
| `b-r channel,trk,sec` | channel, track, sector | **Read block** — seeks to track/sector via `stream->seekSector(trk, sec)`, then reads the block into the status buffer. |
| `b-w channel,trk,sec` | channel, track, sector | **Write block** — seeks to track/sector (implementation pending; currently just logs). |
| `b-a trk,sec` | track, sector | **Allocate block** in BAM (currently logs only). |
| `b-f trk,sec` | track, sector | **Free block** in BAM (currently logs only). |
| `b-e trk,sec` | track, sector | **Block execute** (currently logs only). |

`b-p` is the most generally useful — it works on any stream (network
streams, file streams, disk image streams) for random-access seeking.

`b-r` reads the block content into the drive's status buffer. You
retrieve it by reading the error channel after the command.

**BASIC V2 — b-p (position):**
```basic
open 2,8,2,"http://example.com/data" : rem open file
open 15,8,15 : print#15,"b-p 2,256" : close 15  : rem seek to byte 256
rem read from channel 2 continues at position 256
```

**BASIC V2 — b-r (read block, retrieve via status):**
```basic
open 2,8,2,":myfile" : rem open file on channel 2
open 15,8,15 : print#15,"b-r 2,0,18,0" : rem seek to track 18 sector 0 on channel 2
close 15
input#15, e, e$, t, s : rem read the block data from status
```

**Wedge — b-p:**
```
@b-p 2,256
```

### Memory commands — `m-r`, `m-w`, `m-e`

These operate on Meatloaf's **internal DOS RAM** (not the C64's
memory). This is the firmware's own memory space used for ROM
patching and fast-loader detection.

| Command | Arguments | Effect |
|---------|-----------|--------|
| `m-r addr,len` | `addr` (2 bytes), `len` (1 byte) | **Memory read** — reads `len` bytes from firmware address `addr` and returns them through the status channel. |
| `m-w addr,len,data` | `addr` (2 bytes), `len` (1 byte), `data` (up to 34 bytes) | **Memory write** — writes `data` to firmware address `addr`. Used for soft-loading fast-loader ROM patches. |
| `m-e addr` | `addr` (2 bytes) | **Memory execute** — calls a function at firmware address `addr`. Used to activate detected fast loaders. Also checks `m_memory.mw_hash` against known fast-loader hashes for auto-detection. |

**m-r — read from firmware memory:**
```basic
open 15,8,15 : print#15,"m-r" + chr$(0) + chr$(3) + chr$(4) : rem read 4 bytes from $0300
input#15, a$ : rem read returned bytes
close 15
```

**m-w — write to firmware memory:**
```basic
rem This is binary — you send raw bytes after m-w
open 15,8,15
print#15,"m-w" + chr$(0) + chr$(3) + chr$(2) + chr$(1) + chr$(2) : rem write bytes $01 $02 to $0300
close 15
```

> These commands use Petroski's "M-W" protocol from the original 1541
> and are commonly used by fast-loader cartridges to install custom
> drive code on-the-fly.

### User commands — `u1`–`u9`, `ua`–`uh`, `ui`, `uj`

These are the classic user-command vectors from 1541 DOS:

| Command | Effect |
|---------|--------|
| `u1 ch,drv,trk,sec` / `ua ch,drv,trk,sec` | **Block read** — seeks to track/sector on the stream for `channel`, then reads the block. Like `b-r` but uses the U1 vector. |
| `u2 ch,drv,trk,sec` / `ub ch,drv,trk,sec` | **Block write** — seeks to track/sector, then writes. |
| `u3`–`u9` / `uc`–`uh` | **User command** — execute firmware code at address `$0500 + (N*3)` where N is 0–6 (`u3`=0, `u4`=3, `u5`=6, `u6`=9, `u7`=12, `u8`=15, `u9`=18; `uc`=21, `ud`=24, `ue`=27, `uf`=30, `ug`=33, `uh`=36). These are the standard 1541 user vectors. |
| `u9` / `ui` | Warm reset — see Initialize section above. |
| `uj` | Cold reset — see Initialize section above. |

**BASIC V2 — u1 block read:**
```basic
open 15,8,15,"u1 2,0,18,0" : rem read block at track 18 sector 0 into channel 2's stream
close 15
```

---

## Navigation and directory commands

### Change Directory — `cd`

Changes Meatloaf's current working directory (the `m_cwd` pointer).
Accepts relative paths (`cd dirname`, `cd..`), absolute paths
(`cd/path/to/dir`), and URLs. The `:` separator, `_` (parent), `^`
(up/root), `/` path separators are all handled:

| Example | Effect |
|---------|--------|
| `cd dir` | Enter directory `DIR` from current location |
| `cd:dir` | Same, with `:` separator |
| `cd_` | Go up one level (parent directory) |
| `cd:_` | Same, with `:` separator |
| `cd..` | Go up one level (Unix-style) |
| `cd:..` | Same, with `:` separator |
| `cd^` | Go to root |
| `cd/` | Go to root |
| `cd/sd/` | Navigate to SD card root |
| `cd//sd/games` | Navigate to SD card games folder |
| `cd http://server/path/` | Navigate to a URL |

The directory listing is obtained by `load"$",8` (without changing the
directory), but after a `cd` command, the `$` listing reflects the new
current directory.

**BASIC V2:**
```basic
open 15,8,15 : print#15,"cd games"  : rem go into GAMES directory
open 15,8,15 : print#15,"cd.."      : rem go up one level
open 15,8,15 : print#15,"cd/"       : rem go to root
open 15,8,15 : print#15,"cd//sd"    : rem switch to SD card
```

**Wedge:**
```
@cd games
@cd..
@cd/
@cd//sd
```

### Print Working Directory — `pwd`

Returns the current working directory URL through the error channel.
The response is PETSCII-encoded and ends with `CR`.

**BASIC V2:**
```basic
open 15,8,15
print#15,"pwd"
input#15, e, e$, t, s
print e$          : rem prints the current directory path
close 15
```

**Wedge:**
```
@pwd
```

### Make Directory — `md:path`

Creates a new directory at the given path. If the path already exists,
returns `FILE EXISTS`. If the filesystem is read-only, returns
`WRITE PROTECT`. On failure, returns `WRITE ERROR`.

**BASIC V2:**
```basic
open 15,8,15,"md:newdir" : close 15
```

**Wedge:**
```
@md:newdir
```

### Remove Directory — `rd:path`

Deletes an empty directory. Standard CBM DOS error codes apply:
`FILE NOT FOUND` if it doesn't exist, `WRITE PROTECT` if read-only,
`WRITE ERROR` on failure.

**BASIC V2:**
```basic
open 15,8,15,"rd:olddir" : close 15
```

**Wedge:**
```
@rd:olddir
```

### Position (Seek) — `p ch,hi,mid,lo`

Positions the read/write cursor of an open channel's stream. The
position is calculated as `(hi * 65536) + (mid * 256) + lo`. This is
the same `p` command from SD2IEC and works on any open stream,
including network streams and disk image streams.

```basic
open 2,8,2,"http://example.com/bigfile"
open 15,8,15 : print#15,"p 2,0,1,0" : rem seek to byte 256
close 15
```

**Wedge:**
```
@p 2,0,1,0
```

### Change Partition — `cp n` / `c{shift-p}n`

Switches to a different partition on mounted CMD media (DHD hard
disks, D1M/D2M/D4M floppy images). The partition number must be
1–254 (a CMD HD holds at most 254 partitions); partition 0 is the
system partition and cannot be selected, though it does appear in
the `$=P` listing. Returns `PARTITION SELECTED` on success or `ILLEGAL
PARTITION` if the partition doesn't exist.

Two forms:
- **Text form**: `cp 2`, `cp 5` — the number follows `cp`.
- **Binary form**: `c` + `{shift-p}` + `n` where `n` is a raw byte.

After a successful partition change, the working directory is set to
that partition's root.

A partition can also be named directly as the first component of an
in-image path — `hdbackup.dhd/2/game` or `hdbackup.dhd/subs/game`. That
loads or saves from that partition **without changing the selected
one**; only `cp n` and the `partition` console command change the
selection. A partition number of `0` in a path means "the currently
selected partition". Where a file has the same name as a partition the
partition wins — reach the file by giving the partition number
explicitly, e.g. `hdbackup.dhd/2/subs`.

**BASIC V2:**
```basic
open 15,8,15,"cp 2" : close 15   : rem switch to partition 2
```

**Wedge:**
```
@cp 2
```

---

## Meatloaf-specific commands

### Authentication — `auth:user,pass`

Sets HTTP Basic authentication credentials for the current working
directory (used when accessing password-protected URLs). The user and
password are comma-separated after the `auth:` prefix. These
credentials are stored in the current directory's `MFile` object and
applied to all subsequent HTTP requests from that location.

```basic
open 15,8,15,"auth:myuser,mypassword" : close 15
```

**Wedge:**
```
@auth:myuser,mypassword
```

### VDrive Mode — `vd+` / `vd-`

Enables or disables Meatloaf's **Virtual Drive** mode. VDrive mode
intercepts file operations to present disk images (D64, D81, DHD,
etc.) as if they were real Commodore floppy drives — directory
listings show the image's internal directory, load/save operate on
the image's internal files, and commands like `n:` create new disk
image files.

| Setting | Effect |
|---------|--------|
| `vd+` | Enable VDrive mode — disk images are mounted as virtual drives with full internal filesystem emulation |
| `vd-` | Disable VDrive mode — files are accessed directly from the underlying filesystem (default) |

When VDrive mode is enabled, the `m-r`, `m-w`, `m-e` commands are
**not** delegated to VDrive — they still operate on firmware memory.
Commands like `b-p`, `u1`/`ua` still go through VDrive's channel
handlers.

**BASIC V2:**
```basic
open 15,8,15 : print#15,"vd+" : close 15   : rem enable VDrive
open 15,8,15 : print#15,"vd-" : close 15   : rem disable VDrive
```

**Wedge:**
```
@vd+
@vd-
```

### Load Custom ROM — `xr:romname`

Loads a custom DOS ROM by name. The ROM name is UTF-8 decoded (the
command is `mstr::toUTF8()`'d before use). Meatloaf stores known
ROMs that can be swapped in on-the-fly. If the named ROM is not
found, returns `FILE NOT FOUND`.

```basic
open 15,8,15,"xr:dos1541" : close 15   : rem load 1541 DOS ROM
```

**Wedge:**
```
@xr:dos1541
```

---

## Fast-loader toggles

These enable or disable detection of specific fast-loader protocols.
Each fast loader has its own ROM-patch detection logic in Meatloaf's
memory. When detection is enabled, Meatloaf watches the m-r/m-w/m-e
sequence sent by the C64's fast-loader cartridge and activates the
correct drive-side fast transfer protocol. When disabled, the drive
falls back to standard IEC timing for that loader.

All fast-loader toggles follow the same pattern: `<two letters>+` to
enable, `<two letters>-` to disable.

| Command | Fast Loader | Compile guard |
|---------|-------------|---------------|
| `ej+` / `ej-` | JiffyDOS | `IEC_FP_JIFFY` |
| `ee+` / `ee-` | Epyx FastLoad | `IEC_FP_EPYX` |
| `ea+` / `ea-` | Action Replay 6 | `IEC_FP_AR6` |
| `ef+` / `ef-` | Final Cartridge 3 | `IEC_FP_FC3` |
| `ed+` / `ed-` | DolphinDOS | `IEC_FP_DOLPHIN` |

Whether a particular fast-loader toggle is available depends on the
Meatloaf firmware build — each is guarded by a compile-time
`#ifdef`. If you send an unknown toggle or one not compiled in,
the command falls through and you get `?syntax error` from the drive.

**BASIC V2:**
```basic
open 15,8,15 : print#15,"ej+" : close 15   : rem enable JiffyDOS fast loader
open 15,8,15 : print#15,"ee+" : close 15   : rem enable Epyx FastLoad
open 15,8,15 : print#15,"ej-" : close 15   : rem disable JiffyDOS fast loader
```

**Wedge:**
```
@ej+
@ee+
@ej-
```

---

## Tape commands — `t-c`, `t-i`

These commands operate on mounted tape images (`.tap`, `.dmp`,
`.htap` files). The current working directory must be inside a tape
container for these to work — otherwise the drive returns
`INVALID COMMAND`.

### t-c — Set tape counter

Sets the tape read position by time. Two argument formats:
- **Milliseconds**: `t-c 45000` — seek to 45 seconds in
- **Minutes:Seconds**: `t-c 0:45` or `t-c mmm:ss` — seek to 0:45

The command is parsed as:
```cpp
image->setCounter(arg)
```
which returns `ST_OK` on success or `ST_SYNTAX_INVALID` if the
argument is malformed.

**BASIC V2:**
```basic
open 15,8,15 : print#15,"t-c 0:45" : close 15  : rem seek to 0:45
open 15,8,15 : print#15,"t-c 45000" : close 15 : rem seek to 45 seconds
```

**Wedge:**
```
@t-c 0:45
```

### t-i — Build tape index

Scans the mounted tape image and generates its `.idx` index file
alongside the tape image. The index file speeds up subsequent tape
access by pre-computing the locations of all blocks. Uses
`TAPMFile::buildIndex()` internally.

**BASIC V2:**
```basic
open 15,8,15 : print#15,"t-i" : close 15
```

**Wedge:**
```
@t-i
```

---

## Time and date commands — `t-r*`, `t-w*`

Meatloaf has a real-time clock that can be read and (partially)
written through the command channel. The time is sourced from the
ESP32's system time (which may be set by NTP or manually).

### Read time

| Command | Format | Example output |
|---------|--------|---------------|
| `t-ra` | ASCII | `07/18/2026 14:30:00` |
| `t-ri` | ISO-8601 | `2026-07-18T14:30:00` |
| `t-rd` | Decimal | Packed decimal representation |
| `t-rb` | BCD | Binary-coded decimal representation |
| `t-rz` | Timezone | `UTC` or `America/New_York` etc. |

The time data is written to the drive's status buffer and can be read
through the error channel after issuing the command.

**BASIC V2 — use get# because t-ra contains colons (:):**
```basic
10 open 15,8,15
20 print#15,"t-ra"
30 get#15, a$ : if a$="" then 30
40 if a$ = chr$(13) then close 15 : end
50 print a$; : goto 30
```

**Wedge:**
```
@t-ra
```

### Write time / timezone

| Command | Effect | Status |
|---------|--------|--------|
| `t-wa` | Write time from ASCII string | Not yet implemented |
| `t-wi` | Write time from ISO-8601 string | Not yet implemented |
| `t-wd` | Write time from decimal | Not yet implemented |
| `t-wb` | Write time from BCD | Not yet implemented |
| `t-wz:timezone` | Set the timezone | ✅ Implemented |

**t-wz — Set timezone:**
```basic
open 15,8,15 : print#15,"t-wz:America/New_York" : close 15
```

**Wedge:**
```
@t-wz:America/New_York
```

Time zones use POSIX `TZ` format strings (e.g. `UTC`, `EST5EDT`,
`America/New_York`, `CET-1CEST`). The value is stored in the ESP32's
environment variable `TZ` and takes effect immediately.

---

## DOS error channel reference

After every command, the drive's error/status channel returns a
structured response. Reading it confirms whether the command
succeeded:

```basic
open 15,8,15 : input#15, e, e$, t, s : close 15
```

The format is always: `CODE,MESSAGE,TRACK,SECTOR`

> **Colon gotcha:** `input#` splits on `:`. If the message contains
> colons (like `t-ra` time output: `07/18/2026 14:30:00`), only the
> part before the first colon is captured. Use `get#` in a loop for
> commands that return colons in their output.

| Code | Message | Meaning |
|------|---------|---------|
| `00` | `ok` | No error |
| `01` | `files scratched` | `s:` command succeeded; the 1st status byte holds the count |
| `02` | `partition selected` | `cp:nn` succeeded; track byte holds partition number |
| `03` | `illegal partition` | Requested partition doesn't exist |
| `20` | `read error` | Block read failure (no header/sync/data/checksum) |
| `25` | `write error` | Write verification failed |
| `26` | `write protect` | Media is read-only |
| `30` | `invalid command` | Unrecognized or malformed DOS command |
| `31` | `invalid filename` | Bad file name syntax |
| `62` | `file not found` | File or directory doesn't exist |
| `63` | `file exists` | File already exists (create without overwrite) |
| `70` | `no channel` | Referenced channel doesn't exist |
| `72` | `dir error` | Directory error |
| `73` | `disk full` | Media is full |
| `74` | `drive not ready` | Drive or media not available |
| `77` | `file type mismatch` | Operation not supported for this media type |
| `83` | `permission denied` | Access denied |
| `73` | `meatloaf x.y` | `dos version` — returned on init/reset, `x.y` is the firmware version |

---

## Examples: common workflows

### Check what device Meatloaf is on

```basic
load"$",8     : rem try device 8
load"$",9     : rem try device 9 if 8 didn't respond
```

Or via wedge (JiffyDOS):
```
/$   ← try device 8 (default)
@#9  ← switch default to device 9
/$   ← list device 9
```

### Navigate to a URL directory and list it

```basic
open 15,8,15 : print#15,"cd http://example.com/games" : close 15
load"$",8
list
```

### Set up authentication and load a file

```basic
open 15,8,15,"auth:guest,letmein" : close 15
load"http://example.com/protected/game.prg",8
run
```

### Enable JiffyDOS fast loading for a game

```
@ej+       ← enable JiffyDOS fast loader
/cd games  ← navigate to games directory
/$         ← list games
/game      ← load and run a game
```

### Browse SD card

```basic
open 15,8,15 : print#15,"cd//sd" : close 15
load"$",8
list
```

### Read the real-time clock

```
@t-ra    ← read current time as ASCII
```

### Reset the drive after an error

```
@i       ← initialize (reset) drive
@        ← check that status is 00,ok now
```

### Listen to a tape at a specific position

```basic
open 15,8,15,"t-c 10:30" : rem fast-forward to 10 minutes 30 seconds
close 15
load"tap:",1,1 : rem load from tape device
```

---

## Relationship to other skills

| Situation | This skill | Other skill |
|-----------|-----------|-------------|
| Making HTTP requests (GET/POST/PUT with headers, bodies, JSON parsing) | ❌ Covers standard DOS only | ✅ **meatloaf-networking** skill — full-mode HTTP client with `m`,`h`,`b`,`s`,`status`,`r-h`,`r-b`,`j` commands |
| Writing C64 BASIC V2 programs (bc64 compiler, POKEs, SID, sprites, loops) | ❌ Covers only the DOS command channel | ✅ **c64-basic** skill — writing BASIC code, bc64 syntax |
| JiffyDOS wedge commands (`@`, `/`, `↑`, `@$`, `sys 58451`) | ✅ Explains how `@` forwards commands | ✅ **c64-jiffydos** skill — full JiffyDOS reference, wedge shortcuts, function keys |
| Debugging / unattended C64 workflow (inject BASIC, capture serial, iterate) | ❌ | ✅ **c64-meatloaf-debug** skill |
| C64 hardware reference (POKE addresses, SID, sprites, joystick) | ❌ | ✅ **c64-basic** skill, hardware reference |
| Meatloaf firmware itself (building, flashing, board configs) | ❌ | See the [idolpx/meatloaf](https://github.com/idolpx/meatloaf) repo |

The DOS command channel (this skill) lives **below** the full-mode
HTTP client in Meatloaf's architecture:

```
┌─────────────────────────────────┐
│  C64 BASIC program              │
│  ↓ open/print#/input#           │
├─────────────────────────────────┤
│  DOS command channel (sec addr 15) │ ← You are here
│  ↓ cd, pwd, md, s:, n:, auth,     │
│    ej+, t-ra, cp, etc.            │
├─────────────────────────────────┤
│  Full-mode HTTP client (sec addr 2) │ ← meatloaf-networking skill
│  ↓ m, h, b, s, status, r-h, r-b, j │
├─────────────────────────────────┤
│  IEC bus + WiFi (ESP32)         │
└─────────────────────────────────┘
```

The DOS command channel handles **drive-level operations**: navigating
directories, formatting media, deleting files, setting time, toggling
fast loaders. The full-mode HTTP client handles **application-level
networking**: making API calls with custom headers and bodies.

When both are needed (e.g., a BASIC program navigates to a URL with
`cd`, enables JiffyDOS with `ej+`, then makes an API call), use both
skills.

---

## Cross-references

- For making HTTP requests with custom headers, bodies, and JSON
  Pointer extraction from BASIC or C, see the
  [meatloaf-networking](../commodore64-networking/SKILL.md) skill.
- For writing Commodore 64 BASIC v2 programs (bc64 compiler, POKEs,
  sprites, SID, screen control), see the
  [c64-basic](../commodore64-basic/SKILL.md) skill.
- For the JiffyDOS wedge (`@` prefix, `/` load shortcut, function key
  macros, `sys 58451`), see the
  [c64-jiffydos](../commodore64-jiffydos/SKILL.md) skill.
- For the unattended debug cycle (writing Meatloaf firmware or BASIC,
  deploying, capturing serial logs, iterating via Ultimate 64 REST
  API), see the **c64-meatloaf-debug** skill.
- The DOS command implementations in this reference are drawn from
  [`lib/device/iec/drive.cpp`](https://github.com/idolpx/meatloaf/blob/main/lib/device/iec/drive.cpp)
  in the [idolpx/meatloaf](https://github.com/idolpx/meatloaf) repo.
