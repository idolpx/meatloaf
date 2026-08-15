# CSM tape image support

**Date:** 2026-08-15
**Status:** Approved

## Problem

Meatloaf cannot read `.csm` cassette images. Samples live in `.archive/csm` (12 VIC-20
tapes, 2002-2003). A decoder exists in the jsvic20 reference bundle
(`.reference/jsvic20-csm/index-Ci1vc3_z.js`, minified line 1303, classes `Kbe`/`Xbe`).

## Format

Verified empirically against all 12 samples. A CSM file is a flat run of already-decoded
CBM tape blocks. There is no magic number, no version, no header and no directory:

```
┌────────────────────────┬──────────────────┐
│ 192-byte header block  │ data block       │  … repeated …
│  0     file type       │  (end - start)   │
│  1-2   start addr, LE  │  bytes, raw,     │
│  3-4   end addr, LE    │  NO load-address │
│  5-20  name, 16 PETSCII│  prefix          │
│  21-191 padding        │                  │
└────────────────────────┴──────────────────┘
… terminated by a type-$05 header block (192 bytes, no data block)
```

Entry *n*'s offset depends on every preceding entry's size, so entries are found by
**walking** the file, not by indexing it.

File types: `1` (BASIC, relocatable) and `3` (non-relocatable) → PRG; `2` and `4` → SEQ;
`5` → end of tape.

Two facts that drive the design:

- The data block holds **raw program bytes with no load-address prefix**. Abductor's data
  block begins `1E 10`, a BASIC link pointer, while its header records start `$1001`. The
  two-byte load address must be synthesized from the header on read.
- Names are a fixed 16-byte space-padded PETSCII field, occasionally `$A0`-padded
  (`Motor Mouse`).

All 12 samples walk to exactly their file length with no slack.

## Reference decoder divergences

The jsvic20 decoder is a pulse *synthesizer* — it turns CSM back into datasette pulses for
an emulated VIC-20. Meatloaf needs the opposite direction, so only its layout knowledge is
reused. Two behaviours are deliberately not copied:

- It does not recognise the type-`$05` end-of-tape block; it reads that block's addresses
  as a real data length and runs off the end of the file. Four samples end this way.
- It synthesizes the per-byte checksum during encoding (`Xbe.hasAddedByteChecksum`). CSM
  stores no checksums, so there is nothing to verify on read.

## Design

### Architecture

CSM is a decoded, random-access container, so it is modeled on **T64**, not TAP. Nothing
from `tape_decoder` / TAPClean is involved and CSM does not require PSRAM.

New `lib/meatloaf/media/tape/csm.h` and `csm.cpp`:

**`CSMMStream : MMediaStream`**

- `readHeader()` — walks the container once, building a `std::vector<Entry>` of
  `{file_type, start_address, end_address, filename, data_offset, data_length}`.
  Idempotent: returns early when the index is already built, because
  `rewindDirectory()` calls it on every listing and each walk step costs a seek plus a
  read — one HTTP range request per entry over the network. Publishes `entry_count` so
  `seekEntry()` can distinguish a read header from an unread one, per the existing
  `T64MStream` note.
- `seekEntry(uint16_t)` — index into the vector.
- `seekEntry(std::string)` — linear scan via `mstr::compareFilename` with wildcard
  support. **First match wins**, so `Abductor`'s two `ABDUCTOR` entries resolve to the
  earlier one.
- `seekPath()` — sets `_size = data_length + 2`, `_load_address` from `start_address`,
  and seeks the container to the entry's data offset.
- `readFile()` — emits the two synthesized load-address bytes, then streams raw bytes
  from the container.
- `writeFile()` — returns 0. Read-only, as T64 is.
- `decodeType()` — `1`/`3` → PRG, `2`/`4` → SEQ.

**`CSMMFile : MFile`** — `rewindDirectory()` / `getNextFileInDir()` over
`ImageBroker::obtain<CSMMStream>("csm", url)`. `media_id = " CSM "`; `media_header` comes
from the image's own name, since CSM carries no tape title.

**`CSMMFileSystem`** — `byExtension(".csm")`, registered as `csmFS` beside `t64FS` in
`meatloaf.cpp`. No existing filesystem claims `.csm`.

Header fields are decoded byte by byte rather than through a packed struct, matching the
newer `media/hd/` code and avoiding alignment assumptions for a five-field header.

### Names

Listed as the tape stores them, with only the fixed-width padding removed via
`mstr::rtrimPad()` (`$A0` and trailing spaces — its own comment names CBM tape headers as
its use case). Blank names stay blank and duplicates stay duplicated; nothing is
synthesized or disambiguated. An entry unreachable by name is still reachable by
`LOAD"*"` and by index from the console.

### Corrupt input

The walk stops cleanly on any of: `end_address < start_address`, a short read,
`offset + 192` past the container end, a type-`$05` block, or a 256-entry cap. A final
entry whose data runs past EOF is clamped to the bytes that exist. An empty index yields
an empty listing — no fallback and no fabricated entry.

### Testing

`test/native/test_csm_read/`, following `test_m2i_read`: an `engine_sources.cpp` that
`#include`s the real `.cpp` files, `FileContainerStream` and `native_stubs.cpp` reused
from `test_disk_write`, and a subclass exposing the protected members. Images are
synthesized by `setUp()` and removed in `tearDown()`.

Cases: the multi-entry walk, `$05` termination, the synthesized load-address prefix, size
and type decoding, blank-name and duplicate-name resolution, wildcard lookup, and the
corrupt-input guards.

Tests must set `mode` explicitly before `seekPath()` — a directly constructed
`MMediaStream` leaves it uninitialised, and garbage carrying the `out` bit makes
`seekPath()` *create* the entry it was asked to find.

`CSMMFile` itself is not covered natively: `MFSOwner::File()` aborts under the native
stubs, as it does for the M2I and HDD suites.

## Out of scope

Write support, the `T-C` / `T-I` tape-counter commands, `.idx` sidecars, and any change to
TAP, T64 or TCRT.

## Note on an adjacent pre-existing bug

`T64MStream::seekPath()` computes `_load_address[1] = entry.start_address & 0xFF00`, which
should be `>> 8`. It is left alone here; the new file writes it correctly.
