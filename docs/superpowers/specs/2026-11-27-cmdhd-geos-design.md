# GEOS from a CMD HD image — design

## Intent

Boot GEOS on a C64 (tested on a C64 Ultimate) from a CMD HD image served by
Meatloaf: partition 20 (`GEOS BOOT`, native) of `/sd/hdbackup.dhd`, a gateWay
setup using CMD's `HardDrive` driver. Today the boot stops right after
`GEOBOOT` loads: the drive answers `GEOS` with `31,INVALID COMMAND` and does
nothing for the `M-E $040F` that follows, so the C64 waits forever.

## What the C64 does (from disassembling GEOBOOT)

CMD's GEOS drivers expect the 1581-compatible GEOS turbo to be built into the
CMD HD's DOS, so they upload no drive code:

1. First turbo entry per drive: `GEOS\0` on the command channel (status ignored).
2. Every turbo entry: `M-E $040F`, then the 1581 GEOS turbo handshake.
3. Turbo commands are exactly the 1581 GEOS set: `$049B` init, `$04CC` read,
   `$047C` write, `$032B` status, `$031F` transmit, `$04B9` flush, `$0457` quit.

Partition 38's GEOBOOT (gateWay, `HD1581` driver, 1581-type partition) does
exactly the same, so it is covered by the same change.

`lib/bus/iec/protocol/geos_session.cpp` (`runGeosLoader`, variant
`IEC_FLV_GEOS_S23_1581`) already serves all seven commands through
`epyxReadSector`/`epyxWriteSector`, which fall back to the drive's
`sectorStream()` — for a DHD, the selected partition's decoded stream.

## Goals

- `GEOS` and `M-E $040F` on a CMD HD image start the existing 1581 GEOS session.
- Re-entering turbo after a quit (bare `M-E $040F`) keeps working.
- gateWay on partition 20 boots, opens and saves files.

## Non-goals

- Wheels on CMD HD: there is no Wheels setup on this image and no reference
  for Wheels' CMD HD driver protocol (sd2iec supports only its 1541/1581
  drivers). Separate work if wanted.
- Native partitions over 127 tracks (unverified limit, not a decided one).
  The HardDrive driver's "read link bytes only" sets bit 7 of the track byte
  on a `$04CC` read, and the following `$031F` transmit (which carries no
  track of its own and sees the read's byte left in the command buffer) sends
  2 bytes instead of 256. The session masks bit 7 off before reading, as
  sd2iec does. On the wire, a full read of track 130 therefore looks the same
  as a link-only read of track 2. How the real CMD HD resolves that is not
  known; partition 20 has 13 tracks, so it does not arise here.
- `GEOS` on anything other than a CMD HD image (D64/D81/DNP keep answering 31).

## Anti-goals

- No change to how uploaded GEOS loaders are detected (CRC path untouched).
- No bus-timing code in the DOS layer.

## Approaches

- **A (chosen): `GEOS` arms the detector.** `CmdHdDos` answers `00, OK` and,
  through `DosHost`, tells `IECFastLoadDetect` that `IEC_FLV_GEOS_S23_1581` is
  resident (sets its previous-loader and rx/tx). The following `M-E $040F`
  resolves through the existing previous-loader fallback and handler row
  (`0x040f -> GEOS_S23_1581`), so dispatch, logging and the fast-loader
  enable mask all apply unchanged. Repeat entries work because `memExec()`
  carries the previous loader forward until a new `M-W`.
- B (rejected): `CmdHdDos` starts the session itself on `M-E $040F` — bypasses
  the enable mask and detection logging and couples the DOS to the bus handler.
- Running the HD's own turbo: impossible, no 6502 emulation.

## Decided

- Approach A, scoped to `CmdHdDos` (CMD HD and FD images).
- rx/tx variant: the original call was `IEC_FLRX_GEOS_2MHZ`. On hardware it
  failed at the first status byte; the 1581-2.1 layout works, and it ships as
  its own variant, `IEC_FLRX_GEOS_CMDHD`, with sample/write points derived from
  the HardDrive driver's C64 code (sd2iec's points failed 1 boot in 2-3 against
  ~4 us ESP32 stalls). See AGENTS.md.
- Verification on the C64 Ultimate: boot partition 20, open gateWay, open a
  file, save a file (exercises writes into a DHD native partition); then the
  same on partition 38.

## Left to the builder

- Exact names of the new `DosHost` / detector entry points.
- Whether the armed state also needs clearing on drive reset (it should not
  survive a RESET; check what `IECFastLoadDetect::reset()` already covers).
- Fixing the dispatch at `IECFileDevice.cpp:1096` so unmatched `M-E`s reach
  `iecDrive::startFastLoader()` and get logged, as AGENTS.md says they do —
  small, related, and useful when the next driver does something different.
