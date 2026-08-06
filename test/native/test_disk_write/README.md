# Disk Image Write Verification Suite

Host-side tests for the disk-image write engine (`D64MStream`, `lib/meatloaf/media/disk/d64.cpp`),
covering **D64, D71, D80, D81 and D82**. They run natively on your development machine, not on the
ESP32 — the write engine manipulates a byte buffer and behaves identically on x86 and Xtensa, so
running here buys a sub-second edit-test cycle and a real debugger.

## Running

From the repo root:

```bash
pio test -e native -f native/test_disk_write     # this suite, ~8s
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
`RUN_TEST` lines in `test_disk_write.cpp`.

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

A healthy run is `13 test cases: 13 succeeded`.

`SKIPPED` is the mechanism for tracking known-broken behavior: the message names the finding that
blocks the test, and the test body is left intact. **Lift the `TEST_IGNORE_MESSAGE` and re-run to
check whether a fix actually landed** — that is how we caught a fix attempt that compiled, looked
right, and changed nothing (its allocation call had landed in unreachable code).

Findings are recorded in `docs/superpowers/findings/2026-08-05-disk-write-findings.md`.

## What it checks

Every produced image is judged by **two independent validators that deliberately do not share an
implementation**:

- **`c1541_oracle.h`** — wraps VICE. `c1541_validate()`, `c1541_dir()`, `c1541_read()`.
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

A passing run can leave `build_t0g_*.d*` files in the repo root — cleanup does not cover every exit
path. Harmless; `rm -f build_t0*.d*` clears them.

## Not covered

Writing files *into* images (the plan's Tiers 1-3) is not built yet. When it is, build each tier's
starting image with `c1541 -format` rather than our own `formatImage()`, so a format bug cannot
masquerade as a write bug — the pattern
`test_invariants_pass_on_clean_c1541_image_with_two_files` already uses.

Also untested: writes into 1581 `CBM` sub-partitions (needs a committed binary fixture; neither the
write path nor `c1541` can create one), and the D40/D90/DNP/DHD formats, which are out of scope for
this suite.
