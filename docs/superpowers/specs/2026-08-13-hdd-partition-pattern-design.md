# Partition Listing, Access and Selection — IDE64 CFS (.hdd)

**Date:** 2026-08-13
**Status:** IMPLEMENTED on branch `feat/hdd-partition-pattern`
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

### Numbering, and the one real divergence from DHD

**Partitions are numbered from 1, counting only VALID table entries.** An
invalid slot is skipped rather than consuming a number, so a table with slots
0 and 2 used presents partitions 1 and 2. This is already the convention in
`HDDMStream::seekPartitionEntry()`, which counts valid entries and rejects
index 0, and the registry must agree with it entry for entry.

There are therefore **two numbering spaces** and they must not be confused:

- the **slot index** 0-15, which is what the partition directory is physically
  laid out by and what the boot sector's DP byte holds;
- the **partition number** 1-N, which is what a path, `CP<n>`, `$=P` and the
  `partition` command all speak.

`parse()` converts DP from the first space to the second exactly once, and
`Image::default_part` holds the converted **number**. Nothing downstream ever
sees a slot index.

**Partition 0 behaves exactly as it does in DHD**, and for the same reason —
with numbering based at 1, zero is free to carry the meaning `vdrive.c:1324`
gives it:

- **`0` in a path means "the currently selected partition"**, so
  `DHDResolvePartition()`'s `v == 0` case is copied verbatim rather than
  dropped.
- **`select()` refuses 0**, so `CP0` is a syntax error, as on a CMD HD.

**The one real divergence: no `cached_part`, no `brokerUrl()`, no
dispose-on-select.** DHD needs all
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

Mirrors `DHDImageRegistry`, minus the caching machinery named above.

```c
struct HDDPartition {
    uint8_t     number;      // partition NUMBER: 1-based over valid entries
    uint8_t     slot;        // table SLOT index 0-15 (what DP holds)
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
records every VALID slot — assigning each the next partition number from 1 —
converts the boot sector's DP slot index into a partition number, and sets
`selected` to that number when it names a CFS partition, else to the first CFS
partition. **An image with
no valid CFS partition fails to parse** (`valid` stays false), mirroring DHD's
"No usable partitions" path — there is nothing to select and nothing to mount,
and the alternative is a `selected` that names a partition `select()` would
refuse. It uses the same `s_probing` guard as DHD so
`HDDMFileSystem::handles()` declines the path while the raw bytes are being
read.

`HDDResolvePartition()` resolves the FIRST path component: an in-range number
`0-16` — where **0 means the currently selected partition**, as in DHD — then
`byName()`, otherwise it is not a partition and the current selection applies.
Numbers are parsed with `strtol` + `*end == '\0'` + a range check before
narrowing to `uint8_t`, per the project rule against `atoi`/`std::stoi` on C64-
or network-sourced input — and because an unchecked `int` truncated into
`byNumber()` is exactly the bug that once made `1571` resolve to partition 35
in DHD. A partition wins over a same-named file; such a file stays reachable as
`<image>/<number>/<file>`.

`select()` refuses partition 0 and any non-CFS partition. It needs no
cached-stream disposal — see the divergence above.

### `HDDMStream`

- `seekDirectory(path)` with no partition component resolves to the **selected**
  partition's root rather than entering `partition_list` mode.
- `partition_list` becomes a mode entered only explicitly, to serve `$=P`.
- New `selectPartitionByNumber(uint8_t)`; `selectPartitionByName()` is kept for
  name lookups and for the `$=P` listing.
- The selection is read from `HDDImageRegistry::obtain(containerOf(url))`,
  falling back to `boot_sector.default_partition` when the registry has no entry.

**Resolved during planning: the stream does not consult the registry.** The
container URL is recoverable from the stream's `url`, but using it would make
`HDDMStream` call `MFSOwner::File()`, which `abort()`s under the native test
stubs — and `FileContainerStream` sets `url` to a path ending in `.hdd`, so the
lookup would fire and break the existing `test_hdd_read` suite. `HDDMStream`
therefore carries a plain `selected_partition` member (`0` = fall back to the
boot sector's DP, unambiguous because partitions are numbered from 1) that
`HDDMFile::applyPartition()` writes at all five sites that touch a stream:
`getDecodedStream()`, `rewindDirectory()`, `getNextFileInDir()`, `isDirectory()`,
and `exists()`. This is also the better layering: the stream is about CFS bytes,
the registry is about selection policy.

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
| `brokerUrl()` / `cached_part` | See the divergence above. |

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
- that partition numbers count only VALID slots, starting at 1, and that DP is
  converted from its slot index into that space;
- that `select()` refuses partition 0 and any non-CFS partition, and that `0`
  in a path resolves to the currently selected partition instead.

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
