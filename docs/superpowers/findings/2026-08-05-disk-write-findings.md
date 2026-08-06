# Disk Write Verification — Findings

> **RESOLVED — all findings fixed. `format()` now produces a valid image on all five
> formats (D64, D71, D80, D81, D82), confirmed by BOTH independent validators.**
> Suite: 13 cases, 13 passing, 0 skipped. The sections below are kept as the investigation
> record; read them for *why* each defect existed, not for current status.

## Final resolution

Beyond findings #1-#6, closing #7 needed four more fixes. Each was found the same way:
capture the image before and after `c1541 -validate` (validate rewrites in place), diff, and
compare the disagreeing bytes against c1541's own freshly formatted image.

- **D71 reserves ALL of track 53.** A 1571 allocates the entire side-2 BAM track, unlike
  track 18 where only the two sectors actually in use are allocated. Verified against
  c1541: track 53 reads `00 00 00` while track 18 reads `11 fc ff 07` (17 of 19 free), and
  `-dir` reports `1328 blocks free` = 1366 - 19 - 19, excluding both tracks whole.
  `D71MStream::initializeBlockAllocationMap()` now reserves the whole track.
- **The invariant checker had no concept of a reserved track.** It flagged
  `orphan: block 53/1 allocated in BAM but unreachable` — correct by its own rule, wrong
  about reality. `image_invariants.h` now treats a track that hosts a BAM record AND is
  allocated end to end as a system reservation. Only *fully* allocated BAM tracks qualify,
  so track 18 keeps normal orphan checking and a genuinely leaked directory block there is
  still caught.
- **D80/D82 BAM blocks need a 6-byte header** — T/S link, DOS version, reserved byte, and
  the track range (lowest, highest + 1). The blocks chain to each other and the last links
  to the first directory sector. Without it c1541 derived a bogus range and reported
  `ERR = 65, NO BLOCK, 00, 38` / `78, 23`, walking tracks 0 and 78 which do not exist. New
  shared helper `D64MStream::writeBamBlockHeaders()`, called by D80 and D82. It also zeroes
  each BAM sector's unused tail, which otherwise kept `initializeBlocks()`'s `0x4B`/`0x01`
  fill pattern (117 stray bytes on D80).
- **The header sector was never allocated on D80/D82.** `initializeDirectory()` allocated
  every `block_allocation_map` sector plus the directory sector — which covers D64/D71,
  where the header IS `block_allocation_map[0]` (18/0). On a D80 the header is at 39/0 while
  the BAM sits on track 38, so it is not in the map at all. Symptom: track 39's free count
  read 28 where c1541 said 27.

One test had to be rebuilt as a consequence: `test_c1541_validate_detects_cbm_error_channel_report`
used our own broken D80 output as a ready-made `ERR =` fixture, and stopped reproducing the
moment D80 was fixed — the soft dependency its comment warned about, failing loudly as
intended. It now builds a clean D80 with `c1541 -format` and corrupts the BAM header's
track-range byte itself, so it is engine-independent.

**Known minor issue:** `test_tier0_declared_size_matches_geometry` leaves `build_t0g_*.d*`
files in the working directory after a passing run; its cleanup only happens on some paths.
Harmless, worth tidying.


Bugs found by the write verification suite. Per the design spec these are
recorded, not fixed; they become a separate fix spec. (Exception: finding #4
below is a defect in the test oracle helper itself, not the disk-write
engine, and per the coordinator's fix-round-1 instruction it *was* fixed as
part of this work — see its entry for detail.)

| # | Format(s) | Tier | Summary | Evidence |
|---|-----------|------|---------|----------|
| 1 | D64/D71/D80/D81/D82 (shared base) | 0 | `D64MStream::initializeDirectory()` (`lib/meatloaf/media/disk/d64.h`) writes 258 bytes into a 256-byte directory sector (2 T/S-link bytes + 8 × 32 entry bytes = 258; only 254 bytes may follow the 2-byte link), spilling 2 bytes into the next sector's T/S link field. Structurally undetectable by the seven invariants on a blank image: the overflow writes zero over zero (a never-formatted sector is already all zero), so `check_invariants()` cannot see it. Catching it would need an eighth "write stayed inside its own 256-byte sector" invariant, which is not in the spec — nobody should assume coverage that does not exist. | n/a — not observable via the current invariant set |
| 2 | D64/D71/D80/D81/D82 (shared base) | 0 | `D64MStream::initializeBlockAllocationMap()` (`lib/meatloaf/media/disk/d64.h`) marks every sector of every track FREE, never reserving the header/BAM sector or the first directory sector that `formatImage()` itself just wrote into. A real 1541 format leaves track 18 with 17 free, not 19. Manifests identically (same defect, different header/directory coordinates) on all five formats, and now confirmed to fail **both** independent oracles (`check_invariants()` and `c1541_validate()`) on all five — see per-format table below. | see below |
| 3 | D71 only | 0 | `D71MStream::speedZone()` (`lib/meatloaf/media/disk/d71.h`) tests `track < 35` instead of `track <= 35`. Track 35 (the last track of side 1, correctly 17 sectors) falls through to the `else` branch meant for side 2 and is misclassified into the first side-2 speed zone (boundary at track 53), which resolves to `sectorsPerTrack[3]` = 21 sectors instead of the correct `sectorsPerTrack[0]` = 17. That is exactly 4 extra blocks, matching the observed mismatch: geometry (`getSectorCount()` summed over all tracks) implies 350720 bytes (1370 blocks) but `defaultImageSize()` declares 349696 (1366 blocks) — the real, correct D71 size. | `test_tier0_declared_size_matches_geometry`: `d71: declared 349696 bytes but geometry implies 350720 (1370 blocks)` |
| 4 | Test infra only (`c1541_oracle.h`) — **FIXED**, not an engine bug | 0 | `c1541_validate()` (`test/native/test_disk_write/c1541_oracle.h`) had two detection paths — a text scan for `"error"`/`"Error"`/`"wrong"`, and a before/after byte-diff — and **both** missed c1541's CBM error-channel report format, `"ERR = <code>, <MESSAGE>, <track>, <sector>"` (e.g. `"ERR = 65, NO BLOCK, 00, 38"`), which is what c1541 prints on D80/D82 `-validate` when it hits a track/sector reference it can't repair: the text doesn't contain any of the three substrings (uppercase `ERR`, no `rror`), and c1541 aborts on the bad block rather than rewriting bytes, so the diff is also clean. Net effect: `c1541_validate()` silently reported **VALID** for D80/D82 images c1541 itself had just rejected — discovered because the D80/D82 rows of finding #2's per-format table looked inconsistent with D64/D71/D81 for what should be the identical defect. Fixed by additionally matching the literal `"ERR ="` / `"ERR="` (deliberately not a case-insensitive `"err"` search — that would false-positive on unrelated output). Regression test `test_c1541_validate_detects_cbm_error_channel_report` added (uses our own D80 `formatImage()` output, which reproduces finding #2's `ERR =` report today, as a ready-made fixture — see the test's comment for why that's a soft dependency worth watching). Mutation-tested: reverting the two new lines makes the regression test fail with the expected message; restoring them makes it pass again. | `test_c1541_validate_detects_cbm_error_channel_report`; manual `c1541 -attach diag_keep.d80 -validate` showing `ERR = 65, NO BLOCK, 00, 38` / `ERR = 65, NO BLOCK, 78, 23` (`155, 23` for D82) |

## Finding #2 — confirmed per-format (Tier 0, `test_tier0_format_all_media`)

`TEST_FAIL_MESSAGE`/`TEST_ASSERT_TRUE_MESSAGE` inside a Unity test longjmp past
the rest of the function, so the committed loop test can only ever report the
*first* format that fails (d64, since it's first in `all_formats()`) — it
never reaches d71/d80/d81/d82 in a single run. To confirm finding #2 actually
reproduces on every format (not assumed), each format was additionally run in
isolation via a throwaway, uncommitted diagnostic that calls the same
`formatImage()` / `check_invariants()` / `c1541_validate()` functions per
format without Unity's longjmp semantics. Verbatim results, **after** the
finding #4 oracle fix above (D80/D82's `c1541_validate()` column changed from
a false "OK" to the correct "FAIL" once the fix landed — see the
superseded/original readings in git history of this file if needed):

| Format | `formatImage()` | `check_invariants()` | `c1541_validate()` |
|--------|-----------------|-----------------------|---------------------|
| d64 | true | FAIL: `directory: block 18/1 is in a chain but marked free in BAM` | FAIL (image byte-diff after `-validate`) |
| d71 | true | FAIL: `directory: block 1/4 is in a chain but marked free in BAM` | FAIL (image byte-diff after `-validate`) |
| d80 | true | FAIL: `directory: block 39/1 is in a chain but marked free in BAM` | FAIL (`"ERR = 65, NO BLOCK, 00, 38"` from c1541 — only detectable after the finding #4 fix) |
| d81 | true | FAIL: `directory: block 40/3 is in a chain but marked free in BAM` | FAIL (image byte-diff after `-validate`) |
| d82 | true | FAIL: `directory: block 39/1 is in a chain but marked free in BAM` | FAIL (`"ERR = 65, NO BLOCK, 00, 38"` / `"78, 23"`→`"155, 23"` from c1541 — only detectable after the finding #4 fix) |

All five formats now fail **both** independent oracles, consistently. Before
the finding #4 fix, D80/D82 misleadingly showed `c1541_validate()` as OK —
that was never evidence those two formats were actually fine; it was a gap
in the oracle helper, corrected as described in finding #4.

## Finding #2 — status update after commit `b9584efc`

`b9584efc` ("refactor: reorganize writeHeader method and add BAM/Directory sector
allocation") attempted a fix. **It does not take effect.** Verified by lifting the
`TEST_IGNORE_MESSAGE` guards and re-running: `test_invariants_pass_on_blank_image`,
`test_tier0_format_all_media` and `test_c1541_validates_our_formatted_image` all still
fail with `directory: block 18/1 is in a chain but marked free in BAM`.

Two reasons, both in `D64MStream::writeHeader()` (`lib/meatloaf/media/disk/d64.h`):

1. **The new `setBlockAllocation()` call is unreachable.** The function reads:

   ```cpp
   if (writeContainer((uint8_t*)&header, sizeof(header)))
       return true;                       // returns on SUCCESS
   ...
   if (!setBlockAllocation(header_track, header_sector, true))   // never reached
       return false;
   return false;
   ```

   `writeContainer()` returns the number of bytes written, so it is truthy on success and
   the function returns before reaching the allocation. (The return values also read
   inverted: `true` on the early success path, `false` on every other path.)

2. **The directory sector is never allocated at all.** Only `header_track`/`header_sector`
   is passed to `setBlockAllocation()`. The first directory sector — `18/1` on D64, and the
   coordinates in the per-format table above for the others — also has to be reserved,
   which is exactly what the failing message names.

A third, latent issue on the same path: `writeContainer((uint8_t*)bam_message.c_str(),
sizeof(bam_message))` uses `sizeof` on a `std::string`, which is the size of the string
OBJECT (~32 bytes on most builds), not its text length. It would write the string object's
internal bytes into the image. Currently harmless only because that line is unreachable too;
it needs `bam_message.size()` when the control flow is fixed.

The guards remain in place with this diagnosis in their message. Lift them and re-run to
verify any future fix — that is what they are for.

## Fix pass — findings #1, #2, #3 addressed; #5 opened

Findings #1, #2 and #3 were fixed and verified with this suite. **D64 now passes both
validators** (`check_invariants()` and `c1541_validate()`) on a freshly formatted blank.

What changed, all in `lib/meatloaf/media/disk/`:

- **#1** `d64.h` `initializeDirectory()` — the entry-clearing write is now `block_size - 2`
  bytes instead of `8 × 32`. A directory sector is 8 × 32 = 256 bytes TOTAL and the 2-byte
  T/S link occupies the head of the first entry slot, so only 254 may follow it.
- **#2** `d64.h` `initializeDirectory()` now allocates the first **directory** sector
  (`directory_track`/`directory_sector`) in addition to the BAM/header sector it already
  reserved. That missing allocation was the whole of finding #2.
- **#2** `d64.h` `writeHeader()` — the control flow was inverted: `if (writeContainer(...))
  return true;` returned on SUCCESS because `writeContainer()` yields a byte count, making
  everything after it dead code (including commit `b9584efc`'s allocation attempt). Now
  checks against the expected byte count and returns `true` only at the end.
- **#3** `d71.h` `speedZone()` — `track < 35` → `track <= 35`, so track 35 stays on side 1
  with 17 sectors.

One deliberate omission: a `bam_message` ("meatloaf!!! https://meatloaf.cc") write sat in the
dead region of `writeHeader()`, using `sizeof()` on a `std::string` (the OBJECT size, not the
text length). Fixing the control flow would have made it live for the first time — and testing
confirmed **c1541 rejects the image when it is enabled**, because it writes into the header
sector where nothing was written before. It is left out, with a comment explaining why.
Re-enabling it needs its own decision and verification.

### Finding #5 — D71 `Partition` is misconfigured (NEW, open)

`D71MStream`'s constructor (`lib/meatloaf/media/disk/d71.h`) declares:

```cpp
1,     // header_track      <- should be 18
0,     // header_sector
0x04,  // header_offset     <- should be 0x90
1,     // directory_track   <- should be 18
4,     // directory_sector  <- should be 1
```

A 1571 uses the same layout as a 1541: BAM/header at 18/0, directory starting at 18/1, disk
name at offset 0x90. The struct also contradicts its own `block_allocation_map[0]`, which
correctly names track 18. This is why D71's original failure message pointed at `block 1/4` —
it was walking a "directory chain" that starts in a data block. After the #1/#2/#3 fixes it
reports `orphan: block 36/1 allocated in BAM but unreachable`.

D80/D81/D82 remain **unverified**: `test_tier0_format_all_media` stops at the first failing
format, and D71 now fails ahead of them. Fixing #5 will reveal their true state.

## Second fix pass — #5 and #6 closed, #7 opened

**Finding #5 — FIXED** (by the user): `D71MStream`'s `Partition` now reads header `18/0`,
header_offset `0x90`, directory `18/1`, matching the 1541-compatible 1571 layout and its own
`block_allocation_map[0]`.

**Finding #6 — NEW, FIXED**: `D64MStream::initializeBlockAllocationMap()`
(`lib/meatloaf/media/disk/d64.h`) unconditionally wrote a leading free-sector count byte for
every track and did `byte_count--`, ignoring whether the record actually has one. Records carry
a count only when they have more bytes than the bitmap needs — `getBAMRecord()` already computes
exactly this as `byte_count > (getSectorCount(track) + 7) / 8`. D71's side-2 record (tracks
36-70, `byte_count 3`, 21 sectors → 3 bitmap bytes) is bitmap-ONLY, so the spurious count byte
shifted the whole bitmap one byte over and truncated its last byte. Blocks then read back as
allocated that were never allocated — the symptom was `orphan: block 36/1 allocated in BAM but
unreachable`, which survived the #5 fix. The initializer now uses the same has-count test the
readers use, so writer and readers agree on each record's shape, and keeps the record's fixed
width either way so the next track's entry lands at the right offset.

### Finding #7 — non-D64 formats still rejected by c1541 (NEW, open)

Current per-format state after all fixes above:

| Format | `check_invariants()` | `c1541_validate()` |
|--------|----------------------|--------------------|
| d64 | **PASS** | **PASS** |
| d71 | PASS | FAIL |
| d80 | PASS | FAIL |
| d81 | PASS | FAIL |
| d82 | unverified | unverified |

D71/D80/D81 now satisfy every one of our seven structural invariants but c1541 still rejects
their blank images, so there is format-specific header/BAM detail c1541 checks that our
invariants do not. D82 sits behind them because `test_tier0_format_all_media` stops at the
first failing format (see the deferred note about that loop's design — it should become
per-format tests).

Two partial fixes have landed against #7 (both correct, neither sufficient on their own):

- **`setBlockAllocation()` / `initializeBlockAllocationMap()` are now virtual**, and
  `D71MStream` overrides both to maintain the side-2 free COUNTS in `18/0` at `0xDD` (one byte
  per track 36-70) alongside the side-2 BITMAPS at `53/0`. `BlockAllocationMap` describes one
  contiguous run of bytes and cannot express a split record, so the base class owns the bitmap
  and D71 owns the counts.
- **`initializeDirectory()` now allocates EVERY sector the BAM occupies**, not just
  `block_allocation_map[0]`. D71's BAM spans `18/0` and `53/0`; D80's spans `38/0` and `38/3`;
  D82's adds `38/6` and `38/9`. Leaving those unallocated made the BAM advertise its own
  storage as free.

**D71 still fails, and there is byte-level evidence of where.** Capturing the image before and
after `c1541 -validate` (temporary instrumentation, since validate rewrites in place) shows
exactly four bytes differ:

| Offset | Location | Ours | After validate |
|--------|----------|------|----------------|
| 91631 | `18/0` + `0xEF` — side-2 count for track 54 | 18 | 0 |
| 266292 | `53/0` + `0x34` | `0xFE` | 0 |
| 266293 | `53/0` + `0x35` | `0xFF` | 0 |
| 266294 | `53/0` + `0x36` | `0x07` | 0 |

The side-2 record is 3 bytes per track from offset `0x00`, so track 53's bitmap belongs at
`0x33`-`0x35` and track 54's at `0x36`-`0x38`. Our `0xFE, 0xFF, 0x07` — the correct pattern for
a 19-sector track with sector 0 allocated — starts at `0x34` instead of `0x33`, i.e. **shifted
one byte late**, and the corruption straddles into track 54. Something in the side-2 write path
is still off by one past track 52. That is the next thing to chase.

(For reference: `18/0` begins at byte 91392 — tracks 1-17 × 21 sectors × 256 — and `53/0` at
byte 266240.)

D80/D82 and D81 have not been analysed at this level yet; each needs checking against its own
DOS layout.

This is also a useful demonstration of why the suite runs two independent validators: our
checker passing is necessary but not sufficient, and a suite built on invariants alone would
have declared these three formats correct.

## Findings #8 and #9 — 8-bit truncation on 256-sector tracks (FIXED)

Found by extending the suite to DNP. A CMD native track holds **256** sectors, which is exactly
one more than a `uint8_t` can represent — so both of these were invisible on every floppy format
(D64's largest track is 21 sectors) and broke DNP completely.

**#8 — `initializeBlockAllocationMap()` (`d64.h`)** read the sector count into a `uint8_t`:

```cpp
uint8_t sectors = getSectorCount(t);   // 256 -> 0
```

With `sectors == 0` the bitmap loop wrote all-zero bytes, marking **every block allocated** on a
freshly formatted image, and `bitmap_bytes` computed as 0, which flipped `has_count` on so the
writer emitted a leading count byte the reader does not expect — the same writer/reader
disagreement as finding #6, arriving by a different route. Now `uint16_t`.

**#9 — `getTrackFreeCount()` returned `uint8_t`.** A fully free CMD native track counts 256 free
sectors, truncating to 0. That made `blocksFree()` report 0, and worse, made `getNextFreeBlock()`
treat every track as full — so no write could allocate anything at all. Now returns `uint16_t`.
D71's split side-2 count byte still narrows explicitly, which is correct: those tracks hold at
most 21 sectors.

Both are a reminder that `getSectorCount()` returns `uint16_t` for a reason, and that anything
storing a sector count or a per-track free count needs the same width.

### DNP-specific: directory location on a blank image

`DNPMStream`'s constructor reads the directory T/S out of the image at offset `0x100`, which on a
zeroed container reads back `0/0` — so `formatImage()` would have laid the directory down on track
0. It now falls back to `1/34` when the read yields track 0. That is where CMD native partitions
put it: the BAM is 32 bytes per track for 255 tracks starting at `1/2` offset `0x20`, i.e.
`544 + 8160 = 8704` bytes = exactly 34 sectors, and the area is reserved at full size regardless of
how many tracks the partition actually has.

### What DNP is and is not checked against

DNP has **no c1541 oracle** — VICE cannot attach a CMD native partition — so it is verified by our
seven structural invariants alone. `FormatFixture::has_c1541_oracle` makes that explicit at every
call site rather than letting c1541 fail on an image it cannot read. This is a genuinely weaker
check: c1541 has caught things the invariants accept (see finding #7).

Two Tier 2 scenarios skip DNP, both because the scenario cannot exist there:

- **Directory full** — DNP's directory lives on track 1 and can extend across ~220 sectors
  (~1760 entries), but every entry also consumes a data block and the partition has 1024. The disk
  fills first at any size that keeps the exhaustive per-operation checks affordable.
- **BAM record boundary** — DNP has a single BAM record, so there is no boundary to cross.

DNP's default size for creation is 4 tracks (256 KB). It has no canonical size, and the suite's
exhaustive checks scan every block, so each extra track costs 256 block reads per scan.

## Coverage gaps

- **1581 `CBM` sub-partition writes (D81)** — not tested. Exercising it needs a D81 that already
  contains a CBM sub-partition; the write path cannot create one and `c1541` cannot either.
  Needs a committed binary fixture.
