# Disk Image Write Verification Suite

Host-side tests for the disk-image write engine (`D64MStream`, `lib/meatloaf/media/disk/d64.cpp`),
covering **D64, D71, D80, D81, D82 and DNP**. They run natively on your development machine, not on the
ESP32 — the write engine manipulates a byte buffer and behaves identically on x86 and Xtensa, so
running here buys a sub-second edit-test cycle and a real debugger.

## Running

From the repo root:

```bash
pio test -e native -f native/test_disk_write     # this suite, ~50s
pio run -e lolin-d32-pro                         # firmware, to confirm nothing broke on-device
```

If `pio` is not on your PATH:

```bash
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_disk_write
```

In VS Code, the PlatformIO toolbar's **Test** task under the `native` environment does the same.

To run every native suite rather than just this one:

```bash
pio test -e native
```

Expect `test_EdUrlParser` to fail there — it includes a header that does not exist in the
repository, broken long before this suite and unrelated to it.

There is no per-test filter. The file uses a hand-written `process()` that calls `RUN_TEST` for
each case, so `-f` selects the *folder* only. To run a single test, comment out the other
`RUN_TEST` lines in `test_disk_write.cpp`. To run the tier tests against one format only, narrow
the loop bounds on `g_format_index` in `process()`.

If a run fails with `program.exe: Access is denied` at the build step, a previous test binary is
still running and holding the file — `Get-Process program | Stop-Process -Force` clears it. This is
easy to misread as the suite hanging.

## Requirements

**VICE's `c1541`** must be reachable — it is the external oracle. Point at it explicitly if it is
not on your PATH:

```bash
C1541=/c/vice/bin/c1541 pio test -e native -f native/test_disk_write
```

Without it the c1541-dependent tests **skip rather than fail**, so you get a green run with much
weaker coverage. If the skip count jumps, check this first.

**`platformio.ini` needs the `[env:native]` block.** That file is gitignored, so a fresh clone has
to copy it from `platformio.ini.sample`, which carries the block.

## Reading the output

A healthy run is `73 test cases: 5 skipped, 68 succeeded`, taking roughly 55 seconds — most of that
is Tier 3 and the disk-filling scenarios in Tier 2, which reopen the image once per save.

**73 comes from 13 format-independent tests plus 10 tier tests run once per format across six
formats.** Each tier test takes a single format via `g_format_index`; `process()` drives the loop.
The obvious alternative — looping over `all_formats()` *inside* each test — silently hides formats,
because Unity's assert macros longjmp out of the whole test function, so the first format to fail
stops the rest from ever running. A regression in D82 would sit behind a D64 failure and look as
though it had been covered.

**The 5 skips are deliberate scenario exclusions, not blocked findings** — three for
`test_tier2_bam_record_boundary` (D64, D81 and DNP each have a single BAM record, so there is no
boundary to cross) and two for DNP, explained below. Skips that name a *finding* are the other
thing entirely; see below.

`SKIPPED` is the mechanism for tracking known-broken behavior: the message names the finding that
blocks the test, and the test body is left intact. **Lift the `TEST_IGNORE_MESSAGE` and re-run to
check whether a fix actually landed** — that is how we caught a fix attempt that compiled, looked
right, and changed nothing (its allocation call had landed in unreachable code).

Findings are recorded in `docs/superpowers/findings/2026-08-05-disk-write-findings.md`.

## What it checks

Every produced image is judged by **two independent validators that deliberately do not share an
implementation**:

- **`c1541_oracle.h`** — wraps VICE. `c1541_validate()`, `c1541_dir()`, `c1541_read()`. Only the
  classic floppy formats have this; **DNP does not** (VICE cannot attach a CMD native partition),
  so `FormatFixture::has_c1541_oracle` gates every c1541 call. DNP is therefore checked by our
  invariants alone, which is measurably weaker - see the note below about what c1541 has caught.
- **`image_invariants.h`** — our own seven structural invariants, with per-block diagnostics
  (`check_invariants()` reports *which* block violated *which* rule).

That redundancy has already earned its cost: at one point D71, D80 and D81 satisfied all seven of
our invariants while c1541 still rejected them. An invariants-only suite would have declared three
broken formats correct.

`test_tier0_declared_size_matches_geometry` additionally cross-checks each format's declared
`defaultImageSize()` against the total its geometry tables imply. Declaring sizes explicitly rather
than computing them is deliberate — a computed size would let a wrong geometry table validate
against itself. This check is what caught the D71 `speedZone()` bug.

## Traps — please read before changing this code

Both of these produced *green results that meant nothing* before being caught.

**`c1541 -validate` silently repairs BAM inconsistencies.** It prints only `validating in unit 8 ...`
and exits 0 while rewriting the BAM. So `c1541_validate()` detects corruption by **byte-diffing the
image before and after**, plus matching c1541's CBM error-channel format (`ERR = <code>, ...`),
which contains none of the words a naive text scan looks for. Do not "simplify" this back to
scanning output text — that reported *valid* for corrupt images.

**`D64MStream::seekEntry()` has a "same sector, not first slot" fast path** that skips reseeking and
trusts the stream is exactly where the previous call left it. `check_invariants()` therefore reads
**all** directory entries first and walks file chains only afterwards. Never interleave `seekEntry()`
with anything that moves the stream — doing so made the checker report fabricated violations on
provably clean images.

## How it builds

`engine_sources.cpp` is an adapter translation unit that `#include`s the engine's real `.cpp` files
by relative path. This is deliberate: PlatformIO's Library Dependency Finder treats `lib/<name>` as
one monolithic library and compiles **every** source under it, which drags in ESP-IDF-dependent code
that cannot build for the host. Files under `test/native/<name>/` are always compiled regardless of
`lib_ldf_mode`, so including the `.cpp` files here selects exactly the translation units needed.

`native_stubs.cpp` supplies link-only definitions for a handful of `MFSOwner`/`MFile`/`MStream`
symbols the engine references but these tests never call. They `abort()` loudly rather than
returning something plausible, so reaching one is an obvious test bug rather than a silent wrong
answer.

`lib/meatloaf` compiles for the host because `meat_media.h` guards its `device/iec` includes behind
`TEST_NATIVE`. **Keep new `lib/meatloaf` code free of device-layer includes** or this suite stops
building.

## Known wart

A run can leave `build_t*.d*` scratch images in the repo root — cleanup does not cover every exit
path, and a failing test skips its own cleanup because Unity's assert macros longjmp. Harmless;
`rm -f build_t*.d* *.out` clears them.

## What the tiers cover

Each tier builds on the one below it, so a failure low down explains failures above it.

- **Tier 0 — `format()` produces a valid blank.** Per format, plus a cross-check that the declared
  `defaultImageSize()` matches what the geometry tables imply.
- **Tier 1 — single-file write.** Drives the real SAVE path (`mode = out`, `seekPath()`, `write()`,
  `close()`), then verifies against a *reopened* image: invariants, c1541 validate, the entry in
  c1541's directory listing, byte-exact read-back, and block accounting.
- **Tier 2 — structural stress.** Multi-block files crossing track boundaries, disk-full rollback,
  directory extension past the first sector, save-over-an-existing-name, a full directory reporting
  CBM error 72 with blocks still free (proving directory-full rather than disk-full), and allocation
  across BAM record boundaries (D71's bitmap-only side-2 record, D80/D82's multiple counted records).
- **Tier 3 — seeded randomized stress.** 450 operations (6 formats × 3 seeds × 25 ops) over a small
  reused name pool, with the invariant checker run after *every* operation so a failure names the
  exact op rather than an end state to bisect. Seeds are fixed; one that finds a bug should stay in
  the list as a permanent regression case.

## Two things that will bite you when writing tests here

**One stream, one save.** After `close()` a stream's BAM state is gone — `blocksFree()` reads 0 —
and the *next* save on that same stream fails with `DISK FULL`. Give every save a fresh stream, the
way the drive does (each SAVE opens its own channel), and read free-block counts from a reopened
image too.

This one bit hard: because `close()` returns void, a helper that saved and returned `true`
unconditionally reported those failures as successes. Three Tier 2 tests passed vacuously for a
while — their file counts were just the loop limit and they never reached the conditions they
claimed to test. **A write is not verified until you inspect `error()` after `close()`**, which is
where `finalizeFileWrite()` commits the directory entry and therefore where a full directory is
discovered. `save_file_status()` exists for exactly this.

**The CBM `@:` save-and-replace prefix never reaches the engine.** `drive.cpp` strips it, sets an
overwrite flag, and passes the bare name down; `seekPath()` then decides on its own — a name that
resolves to an existing file is scratched and its slot reused. Saving a literal `"@:doc"` just
creates a second file called `@:doc`. That mistake looked exactly like a leaked chain, and *both*
validators passed on the result, because two complete files genuinely existed. Only the block
accounting was surprising.

## DNP — the one that is not a floppy

DNP is a CMD **native partition**, and it differs from the five floppy formats in ways that shape
the tests. Worth reading before touching either the DNP code or its fixture.

| | Floppy formats | DNP |
|---|---|---|
| Size | fixed per format | derived from the container, and **grows on demand** |
| Directory location | compile-time constant | read from the image at `1/1` |
| Directory track | dedicated; data never goes there | shared — the directory is a plain chain |
| BAM | counted and/or bitmap records | one bitmap-only record, 32 bytes per track |
| c1541 oracle | yes | **none** |

**No oracle.** VICE cannot attach a CMD native partition, so DNP is verified by our seven
invariants alone. `FormatFixture::has_c1541_oracle` is false for it and gates every c1541 call, so
the gap is visible at each call site rather than c1541 quietly failing on an image it cannot read.
This is measurably weaker: finding #7 was a case where D71, D80 and D81 satisfied every invariant
while c1541 still rejected them.

**It can grow — but only if you ask.** Extending a partition as it fills is a **Meatloaf
extension, not CMD behaviour**: a real CMD native partition is fixed at the size it was created
with. So `D64MStream::allow_grow` defaults to **false** and `DNPMStream::growImage()` refuses
unless it is set, reporting `DISK FULL` like any other medium.

That default is a safety property, not a preference. **A DNP embedded in a DHD occupies a fixed
window at a fixed offset**, so a partition that grew there would write straight over its neighbour.
`DHDPartitionMFile::getDecodedStream()` builds its streams over a `DHDOffsetStream` and must never
set this flag.

The mechanism: `getNextFreeBlock()` calls `growImage()` once when it can find no free block, then
retries the search — so all three callers (first block, next block, directory extension) get the
behaviour uniformly rather than each handling it.

Two tests cover this, and they are each other's control — same setup, opposite flag, opposite
expectation:

- `test_dnp_grows_beyond_its_initial_track` — with `allow_grow` set, writing more than a track of
  data must extend the container by a whole number of 64 KB tracks and leave it sound.
- `test_dnp_does_not_grow_by_default` — with the flag left alone, the same writes must fail with
  CBM error 72 and the container must be **byte-for-byte the size it was created with**.

**Its system area is the first 34 sectors of track 1:** `1/0` autoboot, `1/1` partition info,
`1/2`–`1/33` BAM (255 tracks × 32 bytes from `1/2` offset `0x20` is 8160 bytes, ending exactly at
the start of sector 34), with `1/34` the first directory block. Those are reserved whatever the
partition's size, because the BAM is laid out for the maximum 255 tracks up front. They are
allocated but belong to no chain, so `D64MStream::isReservedBlock()` lets the format declare them
and the invariant checker's orphan sweep skips them.

**`dedicated_directory_track`** (true for the floppies, false for DNP) governs two things that turn
out to be one question: whether directory blocks are confined to a single track, and whether file
data is kept off it. On a 1-track DNP that is not academic — reserving track 1 would leave nowhere
at all to put data.

**Two Tier 2 scenarios skip DNP** because they cannot exist there:

- *Disk full* — a native partition grows instead of filling. Its real ceiling is 255 tracks
  (~65280 blocks), far too many to fill in a test. Growth is covered by its own test instead.
- *Directory full* — DNP's directory can extend across ~220 sectors (~1760 entries), but every
  entry also consumes a data block, so the disk fills first at any size that keeps the exhaustive
  per-operation checks affordable.

DNP also has no canonical size, so `defaultImageSize()` returning 64 KB is a *creation* size, not a
property of the format. Sizing it up costs 256 block reads per extra track in every exhaustive scan.

## Not covered

Writes into 1581 `CBM` sub-partitions — needs a committed binary fixture, since neither the write
path nor `c1541` can create one. The D40, D90 and DHD formats remain out of scope for this suite.
