# Fast loader framework — design and implementation plan

Date: 2026-08-23
Scope: `lib/bus/iec/`, `lib/device/iec/`, `lib/bus/iec/protocol/`

## Goal

Support the full set of fast loaders that sd2iec supports, inside the current `IECBusHandler`,
behind one common interface so a loader can be selected at runtime once it has been detected.
Loaders fall into two families that behave differently on the bus, and the interface has to admit
both.

## Two families, not one

**Byte codecs.** JiffyDOS, Epyx FastLoad, Final Cartridge 3, Action Replay 6, DolphinDOS, SpeedDOS
and Hypra-Load replace the byte-level handshake inside an otherwise normal CBM transaction. The
device still handles OPEN / READ / CLOSE through `IECFileDevice`; only the bit timing changes.
These are the loaders already implemented in `IECBusHandler`, and they are what the existing
`Protocol::IECProtocol` sketch in `lib/bus/iec/protocol/_protocol.h` describes
(`receiveByte` / `sendByte` / `receiveBytes` / `sendBytes`).

**Session owners.** Turbodisk, Dreamload, ULoad3, ELoad1, GI Joe, Nippon, EpyxCart, the AR6 1581
loaders, and the whole GEOS / Wheels family take the bus over completely after an `M-E`, then serve
raw track/sector requests until they are finished. sd2iec models these as
`void load_xxx(uint8_t parameter)` — one call that runs the entire transfer and returns. That
cannot be expressed through `sendByte()`, so a second interface shape is required.

The common interface therefore has two virtual entry points, not one. A concrete loader implements
whichever applies and leaves the other at its default.

## Detection

sd2iec identifies a software loader from the code the host uploads over the command channel. Two
tables drive it (`doscmd.c`):

1. A CRC table mapping a running CRC-16 of every `M-W` data byte to a loader id.
2. A handler table mapping (loader id, `M-E` address) to the routine that runs the loader.

Meatloaf already computes a running hash in `driveMemory::write()` (`lib/device/iec/drive/ram.h`)
and prints it at `M-E`, but it is not yet comparable with sd2iec's tables. Four things differ and
all four must change:

- **Polynomial.** sd2iec uses AVR's `_crc16_update`: reflected polynomial `0xA001`, initial value
  `0xFFFF`, one byte at a time. `esp_rom_crc16_be` and `esp_rom_crc16_le` both use `0x1021`
  variants and cannot produce sd2iec's published constants. The routine has to be ported.
- **Excluded addresses.** sd2iec returns from `handle_memwrite()` *before* touching the CRC for
  writes to address 119 (the 1541-style device-address change) and to `0x1C06` / `0x1C07` (VIA
  timer). Meatloaf currently folds those bytes into the hash, which poisons it for any loader
  preceded by a device-address change.
- **Bytes dropped on a short write.** `driveMemory::write()` returns early when
  `addr + len > ram.size()`, so those bytes never reach the hash. sd2iec CRCs the command bytes
  regardless of where they land.
- **Multi-stage loaders.** sd2iec keeps a `previous_loader`. `handle_memwrite()` clears it;
  `handle_memexec()` falls back to it when nothing was detected this round, then saves the current
  detection into it and clears `detected_loader`. FC3 and GEOS upload once and then issue a second
  `M-E` with no upload in between — without `previous_loader` the second `M-E` sees nothing.

Two further details are load-bearing: the CRC table is consulted after **every** `M-W`, not only at
`M-E`, so a loader is identified mid-upload while later blocks keep accumulating; and GI Joe matches
on CRC `0x38A2` *and* the byte just added being `0x60`.

## Loader id space

`IECBusHandler::isFastLoaderSupported()` is `return (loader<=7) && (bit(loader) &
getSupportedFastLoaders())!=0;`, `getSupportedFastLoaders()` returns `uint8_t`, and
`IECDevice::m_flEnabled` is a `uint8_t` bitmask. The list to support is roughly 23 loaders, so ids
of 8 and above fail that guard silently. All three must widen to `uint32_t`, along with every call
site.

`IECDevice::m_flProtocol` packs `loader << 3 | protocol` into a `uint8_t`; that survives to loader
31 and does not need to change.

## Blocking defect in `IECConfig.h`

Line 67's `#endif` closes the `#ifndef IECCONFIG_H` include guard early. Everything from line 68
down — including `IEC_DEFAULT_FASTLOAD_BUFFER_SIZE` and `IEC_SUPPORT_PARALLEL` — falls outside the
guard, and there is no closing `#endif` at end of file. This is why `iec-nugget` and
`fujiloaf-rev0` do not build (on `fujiloaf-rev0` it surfaces one level on, as
`IEC_SUPPORT_FASTLOAD` never being defined and `m_buffer` / `m_bufferSize` being undeclared
throughout `IECBusHandler.cpp`). Until those boards build there is no way to tell whether new work
broke them.

## Existing sector surface

`IECDevice::epyxReadSector()` / `epyxWriteSector()` already exist and `IECFileDevice` implements
them, so session-owner loaders have a track/sector service to call. They are named for Epyx only
because Epyx was the first user; the session-owner loaders need the same two calls. A sector
request against media that is not sector-addressable (T64, TAP, ARC) has no meaning and must fail
rather than return garbage.

## Constraints particular to this codebase

- **No printing inside a transfer loop.** Under `ENABLE_CONSOLE` both `Debug_printf` and
  `Debug_printv` expand to `console.printf`, whose newlib lock calls `abort()` when it cannot
  yield. `AGENTS.md` records this rebooting the board from `iecClock`. Command-channel context
  (a single print per `M-W`) is fine; the transfer loops must have none.
- **Watchdog.** These loaders block for seconds on `bus_iec` (priority 17, core 1). Budget for
  feeding the task watchdog the way the tapclean scan does.
- **`_protocol.h` has two defects to fix before building on it.** It declares
  `static DRAM_ATTR ... timer_start_cycles, timer_cycles_per_us;` at file scope in a header, so
  every translation unit gets its own copy and `timer_init()` in one leaves
  `timer_cycles_per_us == 0` in another, making `timer_wait_until()` return immediately. And the
  `IEC_INVERTED_LINES` branch of `IEC_IS_ASSERTED` reads `!!(reg) & mask`, where precedence applies
  the `!!` to the register read; the non-inverted branch has the parentheses right.

## Plan

Status as of 2026-08-23: phases 0, 1 and 2 are done and building; phase 3 (Turbodisk) is written but
not hardware verified. Phase 4 is not started.

Phase 0 — unblock (must land first)

1. Move `IECConfig.h`'s stray `#endif` to end of file. Verify `iec-nugget` and `fujiloaf-rev0`
   build. Verify `lolin-d32-pro` and `esp32-s3-devkitc-1` still build.
2. Widen the loader bitmask to `uint32_t`: `getSupportedFastLoaders()`, `isFastLoaderSupported()`,
   `IECDevice::m_flEnabled`, and every call site.

Phase 1 — detection

3. Port `crc16_update` (reflected `0xA001`, init `0xFFFF`) into a small header and use it in
   `driveMemory`.
4. Apply sd2iec's `handle_memwrite()` semantics: skip the excluded addresses, CRC bytes that fall
   outside RAM, clear `previous_loader`, run the table lookup after every `M-W`.
5. Apply sd2iec's `handle_memexec()` semantics: fall back to `previous_loader`, dispatch on
   (loader, address), then roll `previous_loader` and clear `detected_loader`.
6. Add the CRC and handler tables, with each entry compiled in only when its loader is enabled.

Phase 2 — interface

7. Define the common interface in `lib/bus/iec/protocol/`: the byte-codec shape already sketched,
   plus a session-owner shape (`run(parameter)` taking the bus and returning when done).
8. Fix the two `_protocol.h` defects above.
9. Move the existing loaders behind the interface without changing their behaviour.

Phase 3 — first vertical slice

10. Turbodisk: one CRC (`0x9C9F`), one `M-E` address (`0x0303`), one handler. It is a session owner,
    so it proves detection, the interface and sector service end to end honestly.

Phase 4 — remaining loaders

11. Dreamload, ULoad3, ELoad1, GI Joe, Nippon, EpyxCart, AR6 1581 load/save, and the four that the
    updated sd2iec adds: FC3 old-freezed (PAL and NTSC, which differ only in send timing), Maniac
    Mansion / Zak McKracken, N0SDOS file read, and Sam's Journey.
12. GEOS and Wheels last, as their own phase. They are about 25 of the CRC table entries, need the
    stage-1 decryption-key capture and four distinct rx/tx timing variants — disproportionate to
    everything else combined.

## The sd2iec reference was updated mid-work

The tree under `.reference/sd2iec` was refreshed on 2026-08-24 and the tables here were re-derived
from it. What changed upstream, and what that meant here:

- **Four new loaders**: `FL_FC3_OLDFREEZED` (which belongs to the FC3 family and carries its own
  PAL/NTSC send-timing pair, `RXTX_FC3OF_PAL` / `RXTX_FC3OF_NTSC`), `FL_MMZAK`,
  `FL_N0SDOS_FILEREAD` and `FL_SAMSJOURNEY`. The last three are new families here
  (`IEC_FP_MMZAK`, `IEC_FP_N0SDOS`, `IEC_FP_SAMSJOURNEY`).
- **A new excluded address, `0x1802`** — VIA 1 DDRB, which N0SDOS writes to work around an Action
  Replay problem. Like 119 and the VIA 2 timer registers it carries no loader code and must not
  reach the CRC.
- **GI Joe now has a CRC entry too** (`0x0C92`, from a hacked-up loader in an Eidolon crack), in
  addition to the mid-stream `0x38A2`-plus-`0x60` rule, which is still there.
- **`FL_GEOS_S1` and `FL_GEOS_S1_KEY` became `FL_GEOS_S1_64` and `FL_GEOS_S1_128`**, and the
  hard-coded key-block grab became a general `fl_capture_table` — a per-loader (address, length,
  buffer) triple describing a window of the upload to keep. That table is not ported yet; it is
  GEOS-only and belongs with phase 12.
- **The loader id numbering is now a clean enum** (`fastloaderid_t`), so `IEC_FLV_*` was renumbered
  to match it exactly. The numbering is mirrored on purpose: it is what keeps these tables
  comparable with upstream, which is the only place they can be checked against.
- Upstream also split its loaders into `fl-*.c` files, which is the same shape this work arrived at
  independently.

One upstream behaviour is deliberately not copied: `run_loader()` sets
`ERROR_UNKNOWN_DRIVECODE` with the CRC in the track/sector fields whenever an `M-E` matches no
handler. Doing that here would make every `M-E` that is not a loader raise a drive error, which is a
behaviour change for paths that work today. The CRC is logged instead.

## Open question blocking a correct Turbodisk first block

sd2iec's `load_turbodisk()` sends the two load-address bytes of the first block on their own and
then still streams a full 254 bytes out of the same 254-byte sector buffer, starting two bytes
further in. Its buffers are 256 bytes allocated back to back out of one array, so the last two bytes
of that stream come from the *next* buffer, not from the file. Either

- the receiver wants 2 + 254 = 256 bytes of file on the first block, and sd2iec has a two-byte
  over-read there that happens to be invisible because the buffers are contiguous; or
- the receiver wants 2 + 252, and the two extra bytes it reads are discarded.

Nothing in sd2iec settles it and there is no second implementation to compare against. The code here
takes the first reading: the first block reads 256 bytes from the file and sends all of them, so the
bytes on the wire are the file's own. If a Turbodisk load comes back correct up to byte 253 and
corrupt from 254, the other reading is right and the fix is one constant. This is called out in the
comment on `transmitTurbodiskBlock()` as well, because that is where someone debugging it will be.

A second Turbodisk case is unverified for the same reason. A file whose length lands exactly on a
block boundary ends with a final block carrying a count of 1 and no data bytes. sd2iec cannot emit
that shape — its `lastused`/`position` bookkeeping never leaves an empty final buffer — so whether
the receiver accepts it is unknown. A file of 2 + a multiple of 254 bytes is what reaches it.

## One loader per source file

`IECBusHandler.cpp` was 4500 lines with every loader in it. Each loader now has its own translation
unit under `lib/bus/iec/protocol/`: `jiffydos.cpp`, `dolphindos.cpp`, `speeddos.cpp`, `epyx.cpp`,
`fc3.cpp`, `ar6.cpp`, `hypraload.cpp`, `turbodisk.cpp`, plus `parallel.cpp` for the parallel cable
that DolphinDOS and SpeedDOS share. They hold `IECBusHandler` member definitions, so they reach the
handler's private state without any friendship or accessor layer, and the class interface did not
change. `IECBusHandler.cpp` keeps the bus state machine, the ATN sequence, the generic fast-load
plumbing (`getSupportedFastLoaders`, `enableFastLoader`, `fastLoadRequest`,
`handleFastLoadProtocols`) and the standard IEC byte transfer.

What every loader file needs comes from the new `lib/bus/iec/IECBusHandlerInternal.h`: the
per-platform timer macros, the fast-GPIO macros, `RAMFUNC`, the `P_*`/`S_*`/`TC_*` state bits, and
the pin accessors. Two things about that header are load-bearing:

- **The pin accessors are inline function definitions there, not declarations.** A loader's bit loop
  sets CLK and DATA at microsecond-resolution absolute times and cannot afford a call per edge.
  They used to be declared `inline` in `IECBusHandler.h` and defined once in the .cpp, which is why
  `isResetPinIdle()` carries a comment about deliberately *not* being inline so it would link from
  `IECHost.cpp` — that hazard is what moving them here removes.
- **The variables the timer macros use are one definition, not one per file.**
  `IECBusHandler.cpp` defines `IEC_BUSHANDLER_DEFINE_GLOBALS` before including the header and so
  provides them; everyone else gets `extern`. Making them file-scope statics in a header — which is
  what the old `protocol/_protocol.h` did — gives every translation unit its own copy, so a
  `timer_init()` in one file leaves another file's `timer_cycles_per_us_div2` at zero and every
  `timer_wait_until()` there returns immediately. The same applies to `haveInterrupts`, which
  `waitPinDATA`/`waitPinCLK` read to decide whether they may feed the watchdog: per-file copies go
  out of step with the `noInterrupts()` that set them.

The eight legacy files that used to be in that directory (`_protocol.h/.cpp`, `cpbstandardserial`,
`cpbfastserial`, `jiffydos`, `epyxfastload`, `dolphindos`, `saucedos`, `speeddos` and a set of empty
header stubs) were the pre-dhansel Meatloaf protocol layer. They compiled but nothing included any
of them — the only reference anywhere was a commented-out include in `lib/bus/userport/parallel.cpp`
— and they have been deleted; git holds them if they are ever wanted.

`_protocol.h`'s `IECProtocol` was a byte-codec interface (`receiveByte`/`sendByte`). That shape
cannot carry these loaders: their bit loops need direct register access, so a virtual call per pin
is not affordable. What unifies the loaders instead is the dispatch — one detection path, one
`runFastLoader()` entry point, and one `handleFastLoadProtocols()` service loop keyed on
`m_flProtocol`.

## Verification

Each phase ends with a build of `lolin-d32-pro` (ESP32-WROVER, the small `iram0_2_seg` flash window,
and the board every hardware check in `AGENTS.md` was done on), `esp32-s3-devkitc-1`,
`iec-nugget` and `fujiloaf-rev0`. Detection is testable off-device: the CRC routine and the table
lookup are pure functions of the uploaded bytes, so a native test can replay an `M-W` sequence and
assert the loader id. The transfer halves need real hardware and a C64.
