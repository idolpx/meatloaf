# Partition Listing, Access and Selection — IDE64 CFS (.hdd)

**Date:** 2026-08-13
**Status:** DESIGN — approved, not yet implemented
**Applies to:** `lib/meatloaf/media/hd/hdd.h`, `hdd.cpp`, `lib/device/iec/drive.cpp`,
`lib/console/Commands/VFSCommands.cpp`

Brings the IDE64 CFS filesystem to the same partition model the CMD HD/FD
filesystem already uses: a persistent per-image selection, `CP<n>`, the
`partition` console command, `LOAD"$=P"`, and paths that may name a partition
without changing the selection.

The reference implementation is `media/hd/dhd.h/.cpp`, and the reasoning behind
it is recorded in `2026-08-07-dhd-partition-command-design.md` and
`2026-08-08-dhd-partition-paths-design.md`. Read those first — this document
records only what CFS does *differently*, and why.

## Problem

CFS partitions are handled entirely inside `HDDMStream`, with no state outside
it:

- The image root lists partitions as directories.
- A leading path component is matched **by name only** against the 16 partition
  entries, falling back to the boot sector's default partition
  (`selectPartitionByName("")`).
- There is no persistent selection, no partition numbers, no `$=P`.
- `.hdd` is invisible to `CP<n>` and to the `partition` console command, because
  `DHDImageRegistry::containerOf()` recognises only `.dhd`, `.d1m`, `.d2m` and
  `.d4m`.

So a CFS image cannot be driven the way a CMD image can, and the C64 has no way
to ask which partitions exist.

## Prerequisite: the boot sector struct is wrong

Independent of everything below, and landing first as its own change.

The authoritative layout is the CFS 0.11 specification
(<https://singularcrew.hu/idedos/cfs.html>), whose boot-sector table is
colspan-encoded in the HTML:

```html
<tr><TH>$0000<TD COLSPAN=3 CLASS="reserved">Unused<TD>DP<TD COLSPAN=4>@Last disk sector
```

`Unused` spans `$00-$02`, **`DP` is `$03`**, `@Last disk sector` spans
`$04-$07`. Every image in `.archive/hdd/` confirms it: all four carry the
`0x40` LBA-flagged pointer at `$04`, and its value is consistently one more
than the `@Partition directory backup` pointer at `$1C`.

`HDDMStream::BootSector` is wrong in two places:

| Field | Current | Correct |
|---|---|---|
| `default_partition` | `$01` | **`$03`** |
| `last_sector` | `$02-$05` | **`$04-$07`** |

```c
struct BootSector {
    uint8_t reserved0[3];       // $00-$02: unused
    uint8_t default_partition;  // $03:     DP (0-15)
    Pointer last_sector;        // $04-$07
    char    id[16];             // $08-$17: "C64 CFS V 0.11B "
    Pointer part_dir;           // $18-$1B
    Pointer part_dir_backup;    // $1C-$1F
    char    disk_label[16];     // $20-$2F
} __attribute__((packed));
```

`id`, `part_dir`, `part_dir_backup` and `disk_label` already land correctly,
which is why this never surfaced. It is latent rather than observable on the
sample corpus: every one of the four images has `$00-$03 = 00 00 00 00`, so DP
reads 0 whichever byte is used. `selectPartitionByName("")`'s
`boot_sector.default_partition & 0x0F` is currently masking a reserved byte.

`PartitionEntry` needs no change and is confirmed correct against the spec,
including `getType()`'s `& 0x0B` — TYPE is the high nibble of `@End` byte 0
with the LBA bit (bit 6) cleared, which is exactly what that mask does.

Two spec fields the code does not use yet, noted so they are not rediscovered:

- **WRITEABLE**, bit 4 of `@Start` byte 0. CFS support is read-only, so this is
  recorded on the partition and not acted on.
- **HIDDEN**, bit 5 of `@Start` byte 0, means *"this partition won't show up in
  partition listing"*. Recorded on the partition and surfaced through the
  existing `is_hidden` / `ls -a` mechanism rather than suppressed outright, so a
  hidden partition stays reachable. `$=P` and the `partition` command both list
  hidden partitions — marked, not omitted — on the same reasoning that makes DHD
  list its system partition: a listing that denies the existence of something
  the user can still select by name is incoherent.

## Model

Identical to DHD: the selection belongs to the **image**, not to any `MFile` or
stream, and only `CP<n>` and the `partition` console command change it. A
partition named in a path binds **that path only** and never calls `select()`.

### Image root semantics change

The root of an `.hdd` becomes the **selected partition's directory**, matching
DHD and matching a real drive. Partitions are reachable through `LOAD"$=P"`, the
`partition` console command, and by naming one in a path.

This is a visible regression for one workflow: `ls /sd/image.hdd` stops listing
partitions. It is the intended consequence of the chosen semantics.

### Three divergences from DHD

These are the reasons the two registries stay separate rather than sharing a
base class. The tables share a shape but not their semantics.

**1. Numbers are the CFS slot index 0-15.** Not a sequential count of valid
entries. The slot index is what the boot sector's DP byte indexes, and it is the
only numbering that survives an empty slot in the middle of the table.

**2. Slot 0 is a real, selectable user partition.** CFS has no
system-partition-at-entry-0. Two consequences that must not be carried over from
DHD by reflex:

- **`0` in a path is a literal slot 0**, never "the currently selected
  partition". `DHDResolvePartition()`'s special case for `v == 0` has no
  counterpart here and must not be copied.
- **`CP0` is legal** and selects slot 0. The drive's existing `pnum < 1`
  rejection is DHD-specific and must not apply to CFS.

**3. No `cached_part`, no `brokerUrl()`, no dispose-on-select.** DHD needs all
three because `ImageBroker` caches one *decoded* D64/D71/D81/DNP stream per
image and cannot tell partitions apart, so a stream cached for one partition
would silently serve another. That failure mode does not exist here:
`HDDMStream` re-derives its whole position from `seekDirectory(pathInStream)` on
every operation, so the cached stream holds no partition identity that could go
stale. The only per-image state it caches is `boot_sector` and
`partition_entries`, which a selection change does not affect.

This is the largest simplification in the port, and the reason a shared
abstraction over both registries would be an abstraction over two things that
only look alike.

### Selectability

A partition is selectable when `type == 1` (CFS). Unformatted (`0`), GEOS (`2`)
and reserved (`3`-`11`) types are **listed but never selectable** — the same
shape as DHD listing table entry 0 while `select()` refuses it. This preserves
today's behaviour, where `selectPartitionByName()` already returns false for a
non-CFS partition.

## Components

### `HDDImageRegistry` — `hdd.h` / `hdd.cpp`

Mirrors `DHDImageRegistry`, minus the machinery named in divergence 3.

```c
struct HDDPartition {
    uint8_t     number;      // CFS slot index, 0-15
    uint8_t     type;        // 0=unformatted, 1=CFS, 2=GEOS, 3-11 reserved
    std::string name;        // ASCII, $00 padding trimmed
    uint32_t    root_lba;    // @Root directory  (entry +$1C)
    uint32_t    size;        // bytes: (end - start + 1) * 512
    bool        hidden;      // @Start bit 5
    bool        writeable;   // @Start bit 4 (recorded; CFS is read-only here)
};

class HDDImageRegistry {
public:
    struct Image {
        bool        valid = false;
        uint8_t     default_part = 0;
        uint8_t     selected = 0;
        std::string disk_label;
        std::vector<HDDPartition> parts;

        const HDDPartition* byNumber(uint8_t number) const;
        const HDDPartition* byName(std::string name) const;
        const HDDPartition* current() const { return byNumber(selected); }
    };

    static Image*      obtain(const std::string& containerUrl);
    static bool        select(const std::string& containerUrl, uint8_t number);
    static bool        probing();
    static std::string containerOf(const std::string& path);   // ".hdd"

private:
    static bool parse(const std::string& containerUrl, Image& img);
    static std::map<std::string, Image> s_images;
    static bool s_probing;
};

const HDDPartition* HDDResolvePartition(const std::string& containerUrl,
                                        const std::string& in_path,
                                        std::string* out_rest = nullptr,
                                        bool* out_explicit = nullptr);
```

`parse()` reads the boot sector and the single partition-directory sector,
records all 16 slots that are VALID, and sets `selected` to `default_part` when
that slot is valid and CFS, else the first valid CFS partition. **An image with
no valid CFS partition fails to parse** (`valid` stays false), mirroring DHD's
"No usable partitions" path — there is nothing to select and nothing to mount,
and the alternative is a `selected` that names a partition `select()` would
refuse. It uses the same `s_probing` guard as DHD so
`HDDMFileSystem::handles()` declines the path while the raw bytes are being
read.

`HDDResolvePartition()` resolves the FIRST path component: an in-range number
`0-15`, then `byName()`, otherwise it is not a partition and the current
selection applies. Numbers are parsed with `strtol` + `*end == '\0'` + a range
check before narrowing to `uint8_t`, per the project rule against `atoi`/
`std::stoi` on C64- or network-sourced input — and because an unchecked `int`
truncated into `byNumber()` is exactly the bug that once made `1571` resolve to
partition 35 in DHD. A partition wins over a same-named file; such a file stays
reachable as `<image>/<number>/<file>`.

`select()` refuses a non-CFS partition. It needs no cached-stream disposal —
see divergence 3.

### `HDDMStream`

- `seekDirectory(path)` with no partition component resolves to the **selected**
  partition's root rather than entering `partition_list` mode.
- `partition_list` becomes a mode entered only explicitly, to serve `$=P`.
- New `selectPartitionByNumber(uint8_t)`; `selectPartitionByName()` is kept for
  name lookups and for the `$=P` listing.
- The selection is read from `HDDImageRegistry::obtain(containerOf(url))`,
  falling back to `boot_sector.default_partition` when the registry has no entry.

*To confirm during planning:* that the container URL is recoverable from the
stream's own `url` for every construction path, including
`ImageBroker::obtain<HDDMStream>("hdd", url)`. If it is not, the selection is
passed in by `HDDMFile` instead, at its three call sites (`rewindDirectory`,
`isDirectory`, `exists`).

### `HDDMFile`

Gains the `DHDPartitionMFile` behaviours directly rather than through a
template — CFS has one MFile type, not four.

- `normalizePath()`, run once per MFile: `$=P` (case-insensitive) switches to
  partition-list mode; otherwise a leading component resolving to a partition
  binds `m_part` and is stripped from `pathInStream`. It never calls `select()`.
- `rewindDirectory()` / `getNextFileInDir()` serve the partition list in `$=P`
  mode, and otherwise behave as today.
- `isDirectory()` / `exists()` return true in `$=P` mode.
- **Entry URLs name the partition by NUMBER**, not name: CFS names are 16 bytes
  that may contain `/` and spaces, which do not survive a URL path component.
  This replaces the inline entry-URL construction in `getNextFileInDir()`.

### `CP<n>` — `iecDrive::changePartition()`

Currently hard-codes both `DHDImageRegistry` and the CMD range `1..254`. It
gains a branch: a DHD container takes the existing path unchanged; otherwise an
HDD container goes to `HDDImageRegistry::select()` with the range `0..15`.

### `partition` console command — `VFSCommands.cpp`

The same branch, plus CFS type labels for the listing: `CFS`, `GEOS`, `----`
(unformatted), `?` (reserved). Everything else — the `*` selected marker, the
number-then-name lookup order, resetting the cwd to the image root after a
successful switch — is unchanged from the DHD path.

A single small helper resolves "which image kind is this path inside", returning
the kind and the container, so this branch is written once and used by both call
sites.

## What is deliberately NOT ported

Named so a later reader does not treat their absence as an oversight:

| DHD component | Why CFS does not need it |
|---|---|
| `DHDOffsetStream` | A CFS partition is a root-directory LBA inside the same stream, not a byte window over a different disk format. |
| `DHDPartitionMFile<BASE>` template | CFS has one MFile type. The behaviours go on `HDDMFile` directly. |
| `DHDCreatePartitionFile()` | No per-partition format selection to make. |
| `brokerUrl()` / `cached_part` | See divergence 3. |
| Partition `0` meaning "selected" | See divergence 2. |

## Verification

**Corpus.** `.archive/hdd/` holds four real images: `ide20201227.hdd` and
`ide320101231.hdd` (8 MB, Soci/Singular), `c64os v1.09-clean.hdd` (32 MB) and
`ide64CF1GB.hdd` (1 GB CF card). All four have DP = 0, so the corpus cannot by
itself prove the boot-sector fix — that rests on the spec's colspan encoding and
on `@Last disk sector` landing at `$04`, which the corpus does confirm.

**Native tests** — new `test/native/test_hdd_partition/`. `lib/meatloaf` builds
for the host (`meat_media.h` guards its `device/iec` includes behind
`TEST_NATIVE`), so this suite can cover:

- boot-sector field offsets, including `@Last disk sector` at `$04` and its
  relationship to `@Partition directory backup`;
- the parsed partition table against each corpus image;
- resolution by number and by name, including a numeric name;
- that binding a partition through a path leaves `selected` unchanged;
- that `select()` refuses a non-CFS partition, and accepts slot 0.

Per the standing test rules: artifacts are removed in `tearDown()`, never
inline; and any test constructing a stream directly must set `mode` before
calling `seekPath()`.

**Hardware** — `CP<n>` from the C64, `LOAD"$=P",8`, the `partition` console
command, and a `LOAD` of a file in a non-selected partition by path.

## Risks

- **The image-root change is the one visible regression.** Anything scripted
  against `ls <image>.hdd` listing partitions must move to `partition`.
- **`changePartition()` and `partition` become dual-format.** The range check
  differs per format (`1..254` vs `0..15`) and the two must not be merged into
  one bound.
- **Reading the selection from inside the stream** is the one piece of coupling
  the design adds; the fallback is stated above.
