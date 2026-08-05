# Disk Write Verification — Findings

Bugs found by the write verification suite. Per the design spec these are
recorded, not fixed; they become a separate fix spec.

| # | Format(s) | Tier | Summary | Evidence |
|---|-----------|------|---------|----------|
| 1 | D64/D71/D80/D81/D82 (shared base) | 0 | `D64MStream::initializeDirectory()` (`lib/meatloaf/media/disk/d64.h`) writes 258 bytes into a 256-byte directory sector (2 T/S-link bytes + 8 × 32 entry bytes = 258; only 254 bytes may follow the 2-byte link), spilling 2 bytes into the next sector's T/S link field. Structurally undetectable by the seven invariants on a blank image: the overflow writes zero over zero (a never-formatted sector is already all zero), so `check_invariants()` cannot see it. Catching it would need an eighth "write stayed inside its own 256-byte sector" invariant, which is not in the spec — nobody should assume coverage that does not exist. | n/a — not observable via the current invariant set |
| 2 | D64/D71/D80/D81/D82 (shared base) | 0 | `D64MStream::initializeBlockAllocationMap()` (`lib/meatloaf/media/disk/d64.h`) marks every sector of every track FREE, never reserving the header/BAM sector or the first directory sector that `formatImage()` itself just wrote into. A real 1541 format leaves track 18 with 17 free, not 19. Manifests identically (same defect, different header/directory coordinates) on all five formats — see per-format table below. | see below |
| 3 | D71 only | 0 | `D71MStream::speedZone()` (`lib/meatloaf/media/disk/d71.h`) tests `track < 35` instead of `track <= 35`. Track 35 (the last track of side 1, correctly 17 sectors) falls through to the `else` branch meant for side 2 and is misclassified into the first side-2 speed zone (boundary at track 53), which resolves to `sectorsPerTrack[3]` = 21 sectors instead of the correct `sectorsPerTrack[0]` = 17. That is exactly 4 extra blocks, matching the observed mismatch: geometry (`getSectorCount()` summed over all tracks) implies 350720 bytes (1370 blocks) but `defaultImageSize()` declares 349696 (1366 blocks) — the real, correct D71 size. | `test_tier0_declared_size_matches_geometry`: `d71: declared 349696 bytes but geometry implies 350720 (1370 blocks)` |

## Finding #2 — confirmed per-format (Tier 0, `test_tier0_format_all_media`)

`TEST_FAIL_MESSAGE`/`TEST_ASSERT_TRUE_MESSAGE` inside a Unity test longjmp past
the rest of the function, so the committed loop test can only ever report the
*first* format that fails (d64, since it's first in `all_formats()`) — it
never reaches d71/d80/d81/d82 in a single run. To confirm finding #2 actually
reproduces on every format (not assumed), each format was additionally run in
isolation via a throwaway, uncommitted diagnostic that calls the same
`formatImage()` / `check_invariants()` / `c1541_validate()` functions per
format without Unity's longjmp semantics. Verbatim results:

| Format | `formatImage()` | `check_invariants()` | `c1541_validate()` |
|--------|-----------------|-----------------------|---------------------|
| d64 | true | FAIL: `directory: block 18/1 is in a chain but marked free in BAM` | FAIL (image byte-diff after `-validate`) |
| d71 | true | FAIL: `directory: block 1/4 is in a chain but marked free in BAM` | FAIL (image byte-diff after `-validate`) |
| d80 | true | FAIL: `directory: block 39/1 is in a chain but marked free in BAM` | reports OK — see oracle limitation below, this is **not** evidence the image is actually fine |
| d81 | true | FAIL: `directory: block 40/3 is in a chain but marked free in BAM` | FAIL (image byte-diff after `-validate`) |
| d82 | true | FAIL: `directory: block 39/1 is in a chain but marked free in BAM` | reports OK — see oracle limitation below, this is **not** evidence the image is actually fine |

### Oracle limitation: `c1541_validate()` misses D80/D82's error report

Running `c1541 -attach <image> -validate` directly against a freshly
formatted (buggy) D80/D82 image prints:

```
ERR = 65, NO BLOCK, 00, 38
ERR = 65, NO BLOCK, 78, 23        (155, 23 for D82)
```

`c1541_validate()` (`test/native/test_disk_write/c1541_oracle.h`) treats a run
as failed if its output contains the substrings `"error"`, `"Error"`, or
`"wrong"`, and otherwise falls back to a before/after byte-diff. c1541's own
message here is `"ERR ="`, not `"error"`/`"Error"`, so the text check misses
it; and for D80/D82, c1541 evidently does not silently repair the BAM the way
it does for D64/D71/D81 (no bytes change), so the byte-diff also reports
"clean." The net effect is `c1541_validate()` returning `true` for a D80/D82
image that c1541 itself flagged as inconsistent. This is a gap in the test
oracle helper, not in the disk-write engine, and not fixed here (out of
Task 7's scope, which only creates fixtures/tests/findings) — noted so nobody
mistakes "d80/d82 c1541_validate passed" for "d80/d82 is correct." The
underlying defect on d80/d82 is the same finding #2, independently confirmed
by `check_invariants()` above.

## Coverage gaps

- **1581 `CBM` sub-partition writes (D81)** — not tested. Exercising it needs a D81 that already
  contains a CBM sub-partition; the write path cannot create one and `c1541` cannot either.
  Needs a committed binary fixture.
