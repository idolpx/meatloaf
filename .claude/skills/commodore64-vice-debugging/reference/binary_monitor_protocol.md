# VICE Binary Monitor Protocol Reference

Wire-level reference for the protocol implemented by `scripts/vice_monitor.py`.
Source: <https://vice-emu.sourceforge.io/vice_13.html> (Binary monitor),
cross-checked against VICE 3.10 on Windows.

Read `SKILL.md` first — it documents three behaviours of the *live* monitor
that this protocol spec does not mention and that will otherwise cost hours.

## Enabling

```bash
x64sc -binarymonitor -binarymonitoraddress ip4://127.0.0.1:6502
```

Default port is **6502**. `-remotemonitor` is the separate *text* monitor and
speaks a different, line-oriented protocol — do not confuse them.

## Packet framing

All multi-byte integers are little-endian. There is no terminator; length
fields delimit everything.

**Command** — 11-byte header:

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | STX, always `0x02` |
| 1 | 1 | API version, currently `0x02` |
| 2 | 4 | Body length (excludes header) |
| 6 | 4 | Request ID |
| 10 | 1 | Command type |
| 11 | … | Body |

**Response** — 12-byte header:

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | STX, always `0x02` |
| 1 | 1 | API version |
| 2 | 4 | Body length (excludes header) |
| 6 | 1 | Response type — usually mirrors the command type |
| 7 | 1 | Error code |
| 8 | 4 | Request ID, echoed |
| 12 | … | Body |

**Events** carry request ID `0xFFFFFFFF` and arrive interleaved with
responses. Any client must read through a single pump that routes packets by
request ID, or a checkpoint firing mid-command will be mistaken for a reply.

## Error codes

| Code | Meaning |
|------|---------|
| `0x00` | OK |
| `0x01` | Object does not exist |
| `0x02` | Invalid memspace |
| `0x80` | Incorrect command length |
| `0x81` | Invalid parameter value |
| `0x82` | API version not understood |
| `0x83` | Command type not understood |
| `0x8f` | General failure (parameters valid, operation failed) |

## Memspaces

`0x00` main, `0x01` drive 8, `0x02` drive 9, `0x03` drive 10, `0x04` drive 11.

## Commands

Field sizes in bytes. `[...]` marks a variable-length array.

| Type | Name | Body |
|------|------|------|
| `0x01` | Memory Get | side-effects(1), start(2), end(2), memspace(1), bank(2) |
| `0x02` | Memory Set | side-effects(1), start(2), end(2), memspace(1), bank(2), data[1+end-start] |
| `0x11` | Checkpoint Get | number(4) |
| `0x12` | Checkpoint Set | start(2), end(2), stop-when-hit(1), enabled(1), operation(1), temporary(1), memspace(1, optional) |
| `0x13` | Checkpoint Delete | number(4) |
| `0x14` | Checkpoint List | *empty* |
| `0x15` | Checkpoint Toggle | number(4), enabled(1) |
| `0x22` | Condition Set | number(4), length(1), expression[length] |
| `0x31` | Registers Get | memspace(1) |
| `0x32` | Registers Set | memspace(1), count(2), then count × [ item-size(1), reg-id(1), value(2) ] |
| `0x41` | Dump | save-roms(1), save-disks(1), name-length(1), filename[…] |
| `0x42` | Undump | name-length(1), filename[…] |
| `0x51` | Resource Get | name-length(1), name[…] |
| `0x52` | Resource Set | type(1), name-length(1), name[…], value-length(1), value[…] |
| `0x71` | Advance Instructions | step-over(1), count(2) |
| `0x72` | Keyboard Feed | length(1), PETSCII text[length] |
| `0x73` | Execute Until Return | *empty* |
| `0x81` | Ping | *empty* |
| `0x82` | Banks Available | *empty* |
| `0x83` | Registers Available | memspace(1) |
| `0x84` | Display Get | use-vic(1), format(1) — format `0x00` = indexed 8-bit |
| `0x85` | VICE Info | *empty* |
| `0x91` | Palette Get | use-vic(1) |
| `0xa2` | Joyport Set | port(2), value(2) |
| `0xb2` | Userport Set | value(2) |
| `0xaa` | Exit | *empty* — leave the monitor, resume emulation |
| `0xbb` | Quit | *empty* — quit VICE |
| `0xcc` | Reset | mode(1): `0x00` soft, `0x01` power cycle, `0x08`-`0x0b` drive 8-11 |
| `0xdd` | Autostart | run-after-load(1), file-index(2), name-length(1), filename[…] |

Checkpoint `operation` is a bitfield: `0x01` load, `0x02` store, `0x04` exec.
A store or load checkpoint is what other debuggers call a watchpoint.

## Response bodies

| Type | Name | Body |
|------|------|------|
| `0x01` | Memory Get | length(2), data[length] |
| `0x11` | Checkpoint Info | number(4), hit(1), start(2), end(2), stop-when-hit(1), enabled(1), operation(1), temporary(1), hit-count(4), ignore-count(4), has-condition(1), memspace(1) — 23 bytes |
| `0x31` | Register Info | count(2), then count × [ item-size(1), reg-id(1), value(2) ] |
| `0x61` | JAM | pc(2) |
| `0x62` | Stopped | pc(2) |
| `0x63` | Resumed | pc(2) |
| `0x82` | Banks Available | count(2), then count × [ item-size(1), bank-id(2), name-length(1), name[…] ] |
| `0x83` | Registers Available | count(2), then count × [ item-size(1), reg-id(1), size-in-bits(1), name-length(1), name[…] ] |
| `0x84` | Display Get | info-length(4), debug-width(2), debug-height(2), x-offset(2), y-offset(2), inner-width(2), inner-height(2), bits-per-pixel(1), buffer-length(4), buffer[…] |
| `0x85` | VICE Info | version-length(1), version bytes[…], svn-length(1), svn revision LE[…] |

`item-size` counts the bytes of the entry **after** the size byte itself, so
iterate with `offset += 1 + item_size`. Never assume a fixed stride — it is
how VICE adds fields without breaking clients.

**Checkpoint List (`0x14`) replies with one `0x11` per checkpoint, then a
`0x14`** carrying the count. All of them share the request ID, so a client
matching on request ID alone reads only the first and desynchronises. Collect
`0x11` packets until the `0x14` arrives.

## Observed on VICE 3.10 (x64sc)

`vice_info` → version `3.10.0.0`, svn revision `0`.

Banks: `default=0, cpu=0, ram=1, rom=2, io=3, cart=4`.

Registers available — **IDs are emulator-assigned; look them up, do not
hardcode**:

| Name | ID | Bits |
|------|----|------|
| `A` | 0 | 8 |
| `X` | 1 | 8 |
| `Y` | 2 | 8 |
| `PC` | 3 | 16 |
| `SP` | 4 | 8 |
| `FL` | 5 | 8 |
| `LIN` | 53 | 16 |
| `CYC` | 54 | 16 |
| `00` | 55 | 8 |
| `01` | 56 | 8 |

`LIN`/`CYC` are the raster line and cycle within it — useful for raster timing
work. `00`/`01` are the 6510 port registers (`$0000`/`$0001`), which is where
the ROM/RAM banking configuration lives.

## Conditions

`Condition Set` (`0x22`) takes a VICE **text monitor** expression as raw bytes,
not null-terminated, 255 bytes maximum. Register names work unprefixed
(`A == 3`), and memory is addressed with the text monitor's own syntax.
Verified: `A == 99` on a checkpoint that is reached with `A == 1` never fires;
`A == 1` fires with `A=1` in the register dump at the stop.

## Side effects

The side-effects byte on Memory Get/Set decides whether the access behaves like
a real CPU access. Reading an I/O register such as `$D019` with side effects
enabled clears latched bits and changes what the running program observes, so
a debugger should default it to off — which `vice_monitor.py` does.

## Addressing beyond 16 bits

Start and end addresses are 2 bytes, so one request cannot straddle the 16-bit
space. `read_memory`/`write_memory` split at the `$FFFF` boundary rather than
letting the end address wrap below the start.
