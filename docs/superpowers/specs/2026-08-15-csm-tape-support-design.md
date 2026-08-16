# CSM tape image support

**Date:** 2026-08-15
**Status:** Approved (revised 2026-08-15 — see *Revision*)

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

CSM behaves as a **datasette, exactly as TAP does**. It differs from TAP only
internally: the blocks are already decoded, so nothing from `tape_decoder` / TAPClean is
involved and CSM does not require PSRAM.

New `lib/meatloaf/media/tape/csm.h` and `csm.cpp`:

**`CSMMStream : MMediaStream`**

- `readHeader()` — walks the container once, building a `std::vector<CSMEntry>` of
  `{file_type, start_address, end_address, name, data_offset, data_length}`. Idempotent
  via the shared `walked` flag, which is set even when the walk yields nothing, because
  `rewindDirectory()` calls it on every listing and each walk step costs a seek plus a
  read — one HTTP range request per entry over the network.
- `nextTapeEntry()` / `resetTape()` / `tapeEnded()` — the datasette head. One entry per
  advance, in tape order.
- `seekPath()` — serves the entry the last directory request left ready if it matches;
  otherwise searches forward from the head, wrapping **once**.
- `serveCurrent()` — sets `_size = data_length + 2`, `_load_address` from
  `start_address`, seeks the container to the data offset, and **clears `have_current`**,
  since serving moves the head past the entry.
- `readFile()` — emits the two synthesized load-address bytes, then streams raw bytes
  from the container.
- `writeFile()` — returns 0. Read-only.
- `decodeType()` — `1`/`3` → PRG, `2`/`4` → SEQ.

**`CSMState`** — the walked entry list plus the tape position, shared per container URL
through a weak_ptr registry, copied from `TapeState` and for the same reason:
`MFile::getSourceStream()` builds a fresh stream per open while directory listings use the
ImageBroker instance, so a per-instance position would rewind on every load.

**`CSMMFile : MFile`** — `rewindDirectory()` / `getNextFileInDir()` over
`ImageBroker::obtain<CSMMStream>("csm", url)`, returning ONE entry per listing and an
`END OF TAPE` marker at the end. `media_id = " CSM "`; `media_header` comes from the
image's own name, since CSM carries no tape title.

**`CSMMFileSystem`** — `byExtension(".csm")`, registered as `csmFS` beside `t64FS` in
`meatloaf.cpp`. No existing filesystem claims `.csm`.

Header fields are decoded byte by byte rather than through a packed struct, matching the
newer `media/hd/` code and avoiding alignment assumptions for a five-field header.

### Names

Listed as the tape stores them, with only the fixed-width padding removed via
`mstr::rtrimPad()` (`$A0` and trailing spaces — its own comment names CBM tape headers as
its use case). An entry the tape leaves unnamed lists under the media file's name, as TAP
does. Duplicates need no disambiguation: the sequential model resolves them positionally.

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

Cases in four groups: the walk (multi-entry, `$05` termination, padding trims), the
datasette behaviour (sequential listing, wrap, positional duplicate resolution, forward
search with one wrap, shared tape position across streams), reading (synthesized
load-address prefix, later-entry offsets, write refusal), and the corrupt-input guards.
Plus one case walking every sample in `.archive/csm`, asserting each consumes to exactly
EOF.

Three constraints the tests must respect:

- Set `mode` explicitly before `seekPath()` — a directly constructed `MMediaStream` leaves
  it uninitialised.
- Query names through `mstr::toUTF8()`, which is what `compareFilename()` matches against.
- **Give each test its own container path.** Unity's `TEST_ASSERT` failure path is a
  `longjmp` and does not unwind C++ destructors, so a failing test leaks its stream, the
  `CSMState` never expires from the registry, and every later test on that URL inherits a
  stale tape — turning one real failure into a dozen fictitious ones.

`CSMMFile` itself is not covered natively: `MFSOwner::File()` aborts under the native
stubs, as it does for the M2I and HDD suites.

## Out of scope

Write support, the `T-C` / `T-I` tape-counter commands, `.idx` sidecars, and any change to
TAP, T64 or TCRT.

## Revision — sequential listing

The first version of this design used a **T64-style full directory** with random access,
on the reasoning that CSM is already decoded so random access is free. That was reversed:
CSM should behave as a datasette like TAP — a listing shows one file at a time until the
end of tape, then wraps.

The decisive argument is that it dissolves a problem the random-access design could only
paper over. Real CSM tapes carry a BASIC loader and its payload under the **same name**
(`Abductor.csm`), and with a full directory that is an ambiguity needing an arbitrary
first-match-wins rule, leaving the payload unreachable by name. Under sequential
semantics it is not a problem at all: loads search forward from the head, so the loader
and then the payload come out in tape order. Blank names stop mattering for the same
reason — they list under the media name and load positionally.

Implementation cost was low, since the file layer is a direct copy of TAP's. The one
substantive divergence from TAP is that `serveCurrent()` clears `have_current`; see the
`lib/meatloaf/AGENTS.md` entry.

## Note on an adjacent pre-existing bug

`T64MStream::seekPath()` computes `_load_address[1] = entry.start_address & 0xFF00`, which
should be `>> 8`. It is left alone here; the new file writes it correctly.
