# Disk Write Verification — Findings

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

## Coverage gaps

- **1581 `CBM` sub-partition writes (D81)** — not tested. Exercising it needs a D81 that already
  contains a CBM sub-partition; the write path cannot create one and `c1541` cannot either.
  Needs a committed binary fixture.
