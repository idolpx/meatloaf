# Disk Image Write Verification Harness

**Date:** 2026-08-05
**Status:** DELIVERED — all four tiers built, 21/21 passing on D64/D71/D80/D81/D82.

> This document is the original design, kept as the record of what was intended and why. Two things
> changed in execution and are worth knowing before reading it as current fact:
>
> - **The "do not fix engine bugs" non-goal was overridden** partway through, on the user's call, once
>   Tier 0 established that `format()` produced structurally invalid images on *all five* formats.
>   Writing into an invalid image tests nothing, so the engine was fixed first and the write tiers
>   were built afterwards. Eight findings were found and fixed; see
>   `docs/superpowers/findings/2026-08-05-disk-write-findings.md`.
> - **Tiers 1-3 were built against our own `formatImage()` output**, not the `c1541 -format`
>   baseline this document recommends. That recommendation existed to stop a format bug masquerading
>   as a write bug — which Tier 0 now rules out directly, since it proves the blank is valid before
>   any write tier runs.
>
> For how to run and extend the suite, read `test/native/test_disk_write/README.md` instead; it is
> maintained, this is not.

## Problem

Meatloaf can write files into container media through a single engine, `D64MStream`, in
`lib/meatloaf/media/disk/d64.cpp`. That engine has never been tested.

A defect here corrupts the user's disk image. Corruption is quiet: a desynchronized BAM or a
cross-linked block does not announce itself at save time, and the image may look fine until a
later write lands on a block the BAM claimed was free. By then the original file is gone and the
cause is invisible. Disk images are irreplaceable user data, so the cost of a latent bug in this
code is high and the feedback loop is long.

The engine is shared. `D40MFile`, `D71MFile`, `D80MFile`, `D81MFile`, `D82MFile`, `D90MFile`, and
`DNPMFile` all inherit from `D64MFile`, and DHD/D1M/D2M/D4M partitions reuse the same code through
a `DHDOffsetStream` window. One engine, driven by per-format geometry tables, serves every format.
That concentration cuts both ways: a single bug reaches every format, and a single well-tested
engine protects all of them.

## Scope

This spec covers **D64, D71, D80, D81, D82**.

**Excluded for now:** D40, D90, DNP, and DHD partitions. Each needs further work before it is ready
for this treatment. They share the same engine, so most of what the suite proves will apply to them
once they are added — the tiers and invariants are written to extend to them without redesign.

A useful consequence of this scope: **every included format has a c1541 oracle.** VICE's `c1541`
can format and validate all five natively, so no format depends solely on our own checker.

## Goals

Deliver an automated, repeatable test suite that proves two things:

1. `format()` creates a structurally valid blank image for each format, from nothing.
2. Writing files into an image never corrupts it, and the data written can be read back byte-for-byte.

The suite must run in CI and serve as a regression guard for future changes to the write path.

Two production changes are in scope as prerequisites, because the suite cannot exist without them:
the default-image-size support that lets `format()` create an image (see "Default image size"), and
the device-layer decoupling that lets `lib/meatloaf` compile natively (see "Decoupling"). Both are
behavior-preserving for existing firmware paths.

## Non-Goals

- **Fixing the bugs the suite finds.** Findings are documented and triaged; fixes are a separate
  spec informed by real evidence. Scope for fixes cannot be estimated before we know what breaks.
- **On-device testing.** Deferred to a follow-up phase (see Future Work).
- **Byte-identical fidelity with VICE.** We require valid, non-corrupting, data-preserving images —
  not images that match another implementation's allocation choices block for block.

## Approach

The suite runs **natively on the development host**, not on the ESP32.

Container corruption is a pure logic defect: BAM bit arithmetic, block-chain linking, and
free-block search operate on a byte buffer and behave identically on x86 and Xtensa. Running
natively buys a sub-second edit-test cycle, a real debugger on the exact failing block, code
coverage to prove rollback paths are exercised, and CI integration. The randomized stress tier is
only practical here — thousands of operations per second versus a flash-and-wait hardware cycle.

The accepted blind spot is integration behavior: container open modes and SD/flash write semantics.
These are covered by the follow-up on-device phase.

## Architecture

### Test target

A new PIO test suite at `test/native/test_disk_write/`, running under the existing `[env:native]`
environment (`platform = native`, `test_filter = native/*`, Unity, `-D TEST_NATIVE`).

### Container stream stub

`MStream` declares six pure virtuals: `isOpen`, `open`, `close`, `read`, `write`, `seek`. A
file-backed `FileContainerStream` implementing them is roughly 40 lines. It backs onto a real file
on disk so the resulting image can be handed to c1541 for validation without conversion.

### Decoupling `lib/meatloaf` from `lib/device/iec`

Three sites in `lib/meatloaf` reach into the IEC device layer and block native compilation:

| Site | Purpose |
|---|---|
| `meat_media.h:242-254` — `ImageBroker::is_in_use()` | Is this cached stream mounted on a drive? |
| `meat_session.h:380-390` — `is_session_in_use()` | Is this session owned by a drive/console? |
| `meatloaf.cpp:870` — `Meatloaf.use_vdrive` | Should `MFSOwner::File()` prefer the vdrive path? |

Each is replaced with an injectable hook — a `std::function` with a safe default (`false` for the
in-use predicates and for `use_vdrive`) that `main.cpp` populates at boot with the current
behavior. This unblocks native compilation and removes an inverted layering dependency, where the
storage abstraction reaches upward into the device layer.

Firmware behavior must not change. The hooks are wired at boot before any device is attached.

### Default image size

`format()` must be able to create an image from nothing. Today it cannot: it sizes the file with
`image->seek(size - 1)`, taking `size` from the `MFile` member, which is 0 for a path that does not
exist — so the seek underflows and no image is produced.

`format()` will instead use the **default image size for that media**, exposed as a new virtual on
`D64MStream`. An existing file keeps its own size, so formatting a 40- or 42-track D64 does not
truncate it to 35; the default applies only when creating.

The default is declared **explicitly per format** rather than computed from the geometry tables.
Computing it would be DRYer, but it makes the suite circular: a wrong geometry table would produce
a wrongly-sized image that the suite then validates against that same wrong table. An explicit
constant is independent, so Tier 0 can cross-check the declared size against the total derived from
`block_allocation_map` and `sectorsPerTrack` — turning a potential blind spot into a test that
catches geometry errors.

Sizes below are verified against `c1541 -format`, not computed:

| Format | Blocks | Default size |
|---|---|---|
| D64 (35 track) | 683 | 174,848 |
| D71 | 1,366 | 349,696 |
| D80 | 2,083 | 533,248 |
| D81 | 3,200 | 819,200 |
| D82 | 4,166 | 1,066,496 |

The cross-check will likely fail for D80 and D82 on first run: `getTrackCount()` returns
`block_allocation_map[0].end_track`, which is 50 for D80 rather than 77, because these formats have
multiple BAM records. `getNextFreeBlock()` correctly uses `.back().end_track`. This is a real
inconsistency and exactly the kind of finding the cross-check exists to surface; per the Non-Goals
it is documented, not fixed here.

### Validation strategy

Two independent validators, applied together on every format.

**c1541 as an external oracle.** After each operation:

- `validate` — CBM DOS's own BAM-versus-actual-chain consistency check. This is the direct
  corruption detector.
- `dir` — the directory entry appears with the correct name, type, and block count.
- `read` — extract the file and byte-compare against what was written.

**Our own invariant checker.** With every in-scope format having a c1541 oracle, this is no longer
required for coverage — it is a second, independent opinion. It earns its place two ways: it gives
far better diagnostics than c1541's pass/fail, reporting *which* block violated *which* invariant;
and it is what will carry DNP, DHD, D40, and D90 when they are added, since c1541 cannot validate
those at all. Building it now against formats where an oracle can check the checker is the right
order of operations.

### Invariants

Checked after every operation in every tier:

1. Every block in every file's chain is marked allocated in the BAM.
2. Every block marked allocated is reachable — from a file chain, the directory chain, a BAM block,
   or the header. No orphans (leaked blocks).
3. No block appears in more than one chain. No cross-links.
4. Every chain terminates properly: final block has track 0 and a sector byte holding the used-byte
   count.
5. Every track/sector reference lies within the format's geometry.
6. `blocksFree()` equals the count of free bits in the BAM.
7. Every directory entry points to a valid, allocated start block.

Invariants 2 and 3 are the ones c1541's `validate` also covers; the rest are additional.

## Test Tiers

Each tier depends on the one below it. Testing writes against a blank we cannot vouch for is
testing on sand, which is why `format()` comes first.

**Tier 0 — `format()` produces a valid blank.** For each format, call `D64MFile::format("name,id")`
on a path that does not yet exist and run both validators against the result, plus the declared-size
versus geometry-derived cross-check. If a format's BAM record table is wrong, every later tier
reports garbage and time is lost chasing phantom write bugs.

**Tier 1 — Single-file write.** Save one small file into a fresh blank. Verify the directory entry,
block chain, BAM accounting, and byte-exact contents.

**Tier 2 — Structural stress.** The cases where corruption lives:

- Multi-block files crossing track boundaries
- Files spanning BAM *record* boundaries (D71 side-2 bitmap-only records; D80/D82 multi-record)
- Directory-block extension onto a second directory block
- Directory track full — correct error, no partial state
- Disk full — write fails with `72,DISK FULL` and rolls back every claimed block (Invariant 2
  catches leaks)
- `@:` overwrite — old chain scratched, directory slot reused
- Subdirectory writes into 1581 `CBM` sub-partitions (D81)

**Tier 3 — Randomized stress.** A seeded driver issuing random save/scratch/overwrite sequences
until the disk fills, validating after every operation. Fixed seeds keep failures reproducible; a
failing seed becomes a permanent regression case. This tier finds the interaction bugs that
hand-written scenarios miss.

## Formats Covered

| Format | Tier 0 | Tiers 1-3 | c1541 oracle |
|---|---|---|---|
| D64 | ✅ | ✅ | ✅ |
| D71 | ✅ | ✅ | ✅ |
| D80 | ✅ | ✅ | ✅ |
| D81 | ✅ | ✅ | ✅ |
| D82 | ✅ | ✅ | ✅ |
| D40, D90, DNP, DHD | excluded | excluded | — |

## Completed Prerequisite Work

The D80/D82/D90 interleave values were corrected as part of this design, since the suite would
otherwise encode the wrong behavior as expected.

D80 and D82 inherited D64's `interleave = {3, 10}`. The correct values were derived empirically
from c1541 rather than from memory: format an image, write a multi-block file, and read the block
chain via `c1541 -chain`. Simulating the allocator against the observed chains reproduced them
exactly once BAM blocks on track 38 were accounted for.

| Format | Directory | File | Was |
|---|---|---|---|
| D80 (8050) | 3 | 6 | 3, 10 |
| D82 (8250) | 3 | 5 | 3, 10 |
| D90 (D9060/9090) | 1 | 1 | 3, 10 |

The D82 derivation was cross-validated: simulating its file chain predicted BAM blocks at
`38/0, 3, 6, 9`, which the directory chain then confirmed independently.

D90 is a hard disk. It has no rotational latency to optimize against, so consecutive allocation is
correct and interleave is 1 — the same reasoning behind D81's `{1, 1}`. This value is reasoned
rather than measured, because c1541 cannot format `d90`. D90 is otherwise out of scope and the
suite makes no claim about it.

## Known Issues Flagged, Not Fixed

- **Two parallel image-creation paths.** `VDrive::createDiskImage()` at `drive.cpp:1582` and
  `MFile::format()` at `drive.cpp:1595`. The suite tests `format()`. Whether these should converge
  is a separate question.
- ~~**`getTrackCount()` is wrong for multi-BAM-record formats.**~~ **FIXED.** It returned
  `block_allocation_map[0].end_track`, which was 50 for D80 rather than 77 and similarly short for
  D82, while `getNextFreeBlock()` already used `.back().end_track`. It now returns the last
  record's `end_track`, so writer and readers agree. The Tier 0 size cross-check passes on all five
  formats.

## Risks

- **The suite may find that a format is substantially broken**, making its later tiers
  uninformative until fixed. Mitigation: Tier 0 runs first per format and failures are reported per
  format, so one broken format does not block the others.
- **Native compilation may hit blockers beyond the three known sites.** Mitigation: the decoupling
  is the first implementation step, so this surfaces immediately rather than after the scenario
  library is written.
- **c1541's `validate` may disagree with our invariant checker.** This is informative, not a
  problem — a disagreement means one of them is wrong about the format, and resolving it improves
  our understanding.

## Success Criteria

- `pio test -e native -f native/test_disk_write` runs the full suite and reports per-format,
  per-tier results.
- Tier 0 passes for all five formats, or its failures are documented per format.
- Every failure is reproducible from a fixed seed or a named scenario.
- Findings are captured as a triaged list ready to become a fix spec.
- Firmware behavior is unchanged; the device build still compiles and runs.

## Future Work

- **Fix spec** driven by the suite's findings.
- **Extend to the excluded formats** — D40, D90, DNP, DHD — once their outstanding work is done.
  The tiers and invariants are designed to extend without redesign; DHD additionally needs a
  partition-bounds invariant (no byte outside the partition window changes), which the file-backed
  stub can enforce with guard regions.
- **On-device smoke test** replaying a subset of scenarios through the real drive path to cover the
  integration surfaces the native suite cannot reach. More valuable after the native suite has
  flushed out logic bugs, so a hardware failure means something integration-specific.
