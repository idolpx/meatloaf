# `partition` Console Command — CMD HD/FD Partition Switching

**Date:** 2026-08-07
**Status:** DELIVERED, with one section retracted.

> **Retraction — "Partition 255 is unreachable" is wrong.** That section, and the partition-count
> claims in the Problem statement below, rest on the premise that a CMD HD has 254 *user*
> partitions plus a separate system partition at entry 0, making table entry 255 an unreachable
> 255th user slot. It is not: a CMD HD holds a **maximum of 254 partitions**, and the vendored
> `lib/vdrive/vdrive.c:1180` says so directly — "CMD HDs can access 254 partitions (255 is system)"
> — while `vdrive.c:1201` remaps physical entry 0 onto *logical* slot 255. VICE's "255" is the
> system partition under a different numbering, not an extra partition.
>
> The final whole-branch review caught this and rated it Critical; the project owner confirmed the
> correction. `maxpart` is back to 254 and the `CP<n>` bound back to `1..254`. Everything else in
> this document — the `partition` command, the removal of path-based selection, and the
> documentation corrections — stands as delivered and is unaffected.
>
> One thing was added after this document was written: table entry 0 (the system partition) is now
> **shown** in both partition listings, typed `sys`/`SYS`, while `select()` refuses it.

## Problem

A CMD HD image holds up to 255 partitions, numbered 1 to 255, with partition 0 reserved as the
special system partition that carries the partition table itself. (A CMD FD image holds 31, same
0-reserved convention.) Meatloaf tracks a "currently selected" partition per image in
`DHDImageRegistry`. Everything downstream reads that selection:
`DHDCreatePartitionFile()` picks the MFile subclass from it, and `getDecodedStream()` builds the
`DHDOffsetStream` window from it.

Selection could be changed two ways. One is the CBM DOS `CP<n>` command, which is how the real
hardware does it. The other is implicit: `DHDPartitionMFile::normalizePath()` reads the first
component of `pathInStream`, and if it matches a partition by name or number, selects that
partition and strips the component. That second mechanism is a Meatloaf invention — **the real CMD
HD does not switch partitions on LOAD or CD; `CP<n>` is required.**

The invention is actively harmful, because selecting a partition strips its name from the path.
After `cd bible`, `pathInStream` is empty and the partition lives only in global state, so the
entry URLs that `D64MFile::getNextFileInDir()` builds during a listing carry no partition
component:

```
/sd/content/disk/dhd/hdbackup.dhd/1571
```

`MFSOwner::File()` on that URL runs `normalizePath()`, which reads `1571` as a partition
reference. A file named `1571` inside the BIBLE partition therefore selected a different partition
**part-way through the listing of BIBLE** — and because selection also disposes the ImageBroker
entry, the stream re-opened underneath the running listing and the remaining entries came from the
wrong partition.

A `uint8_t` truncation made this reachable with names that are not even valid partition numbers
(`atoi("1571")` narrowed to `1571 & 0xFF` = 35). That truncation was fixed on 2026-08-07 with a
`strtol` range guard, but the guard only narrows the collision window. Any file whose name matches
a real partition's name, or a number in `0..255` that exists, still hijacks a listing. The parse
was never the root cause: **listing-generated entry URLs are ambiguous with partition references,
and no amount of validation fixes that.**

### Partition 255 is unreachable

Two places encode an off-by-one from the same wrong premise that the maximum is 254:

- `DHDImageRegistry::parse()` (`dhd.cpp:139`) sets `maxpart = 254` and loops `i <= maxpart`. Entry
  0 is consumed as the system partition (it supplies `disk_label`), so the loop yields partitions
  1..254 and **entry 255 is never read**. A partition 255 in a real image is invisible to Meatloaf.
- `iecDrive::changePartition()` (`drive.cpp:2358`) rejects `pnum > 254`, so **`CP255` fails** with
  a syntax error even if the partition were parsed.

Both are fixed here, since the new command shares the same bound and would otherwise inherit the
same defect. The CMD FD bound (`maxpart = 31`) is correct and unchanged.

Note for the implementer: `maxpart` and the loop counter are both `uint16_t`, so `i <= 255`
terminates correctly. Do not narrow either to `uint8_t` — `i <= 255` would then never be false.
`p.number` is `uint8_t` and holds 255 fine.

## Goals

- Give the console an explicit, discoverable way to switch partitions.
- Make listing-generated entry URLs incapable of changing partition state, by construction rather
  than by validation.
- Bring partition-switching behavior in line with real CMD HD hardware.
- Make partition 255 reachable.

## Non-goals

- Changing `LOAD"$=P"`, the partition-table entry layout, or the offset-window mechanism.
- Changing the CMD FD partition bound.
- Per-path partition scoping (browsing `image.dhd/bible/...` as a path rather than a mode). That
  would require keeping the partition in `pathInStream` and deriving the offset window per-MFile —
  a path-model change across DHD and DXM. Rejected as out of proportion; the modal model matches
  the hardware.
- A drive-side equivalent. `CP<n>` already exists and is correct.

## Design

### Remove path-based selection

Delete the third branch of `DHDPartitionMFile::normalizePath()` (`lib/meatloaf/media/hd/dhd.h`) —
the one that resolves a leading path component to a partition, selects it, and strips it. Two
branches remain:

1. Empty `pathInStream` → return.
2. `$=P` / `$=p` → set `listing_partitions`, clear `pathInStream`, so `LOAD"$=P"` keeps working.

The `strtol` guard added earlier is deleted along with the branch it guarded; its logic moves into
the new command, where a number genuinely is user input.

Nothing downstream changes. `DHDCreatePartitionFile()` and `getDecodedStream()` continue to read
`img->current()`. The modal mechanism *is* the design; only the implicit way of changing modes
disappears.

After this, `pathInStream` is always relative to the selected partition, so an entry URL can no
longer contain anything the DHD layer will interpret. The collision class is eliminated by
construction.

### The command

`partition`, in `lib/console/Commands/VFSCommands.cpp`, registered in `registerVFSCommands()`.
The name follows the existing noun-with-subcommand convention of `iec` and `led`; `cp` was
unavailable (it is copy).

```
partition            list the image's partitions, marking the selected one
partition 31         select by number
partition bible      select by name
partition bib*       select by wildcard
```

Name matching goes through `Image::byName()`, which converts each partition's PETSCII name with
`mstr::toUTF8()` and compares using `mstr::compareFilename()`. That comparison is **case-sensitive**
and accepts `*`/`?` wildcards. PETSCII renders as lowercase, so the name to type is the one the
listing prints — `bible`, not `BIBLE`. The listing must therefore print `mstr::toUTF8(p.name)`, not
the raw PETSCII, so that what is shown is what can be typed.

The target image is always the one the console is currently in, resolved with
`DHDImageRegistry::containerOf(getCurrentPath()->url)`. That helper already recognizes `.dhd`,
`.d1m`, `.d2m` and `.d4m`, so CMD HD and CMD FD are both covered with no extra work.

Listing output marks the selection and names the type (1=NAT, 2=1541, 3=1571, 4=1581):

```
  #  type   name
  1  1541   GW BOOT HD
* 31  NAT    BIBLE
 35  1581   GW BOOT HD
```

Selection parses a numeric argument with `strtol` + `*end == '\0'` + range **`1..255`** before the
`uint8_t` cast, per the standing rule against `atoi`/`std::stoi` on C64- or network-sourced input.
0 is rejected: it is the reserved system partition, not selectable, and `byNumber(0)` returns null
in any case since `parse()` never adds entry 0 to `parts`. A non-numeric argument goes to
`byName()`. On success it calls `DHDImageRegistry::select()` — which
already disposes the ImageBroker entry so the next access re-decodes — then **resets the console
cwd to the image root** via `setCurrentPath()`, because the previous cwd may name a subdirectory
that existed only in the previous partition.

`dhdFS` and `dxmFS` are registered outside the `MIN_CONFIG` guards, so the command needs no build
guard.

### Errors

All non-fatal, returning `EXIT_FAILURE` with a one-line message: not inside a CMD HD/FD image;
partition table unreadable; no such partition.

## Consequences

- `cd bible` from the console stops selecting a partition. It falls through to the base class,
  finds no such subdirectory, and the console reports `cd: not a directory: bible`. `partition
  bible` replaces it.
- `LOAD"BIBLE",8` from the C64 stops selecting a partition. This **matches** real CMD HD, which
  requires `CP<n>`; the previous behavior was the deviation.
- `CP<n>`, `LOAD"$=P"`, and the drive status codes are unaffected.

### Documentation to correct

The partition-count claim is wrong in the same places that describe the parser: `dhd.h`'s header
comment, `dhd.cpp`'s comment above the parse loop, and both `AGENTS.md` files all say "254
partitions". Each should read 255 (1–255, 0 reserved as the system partition). CMD FD's 31 is
correct and stays.

Four places also document the selection behavior being removed, and state it as if it were CMD
behavior:

- `lib/meatloaf/media/hd/dhd.h` — header comment: "or by loading / CD'ing a partition name or
  number".
- `AGENTS.md` — CMD media images note: "CD/LOAD of a partition name or number selects it".
- `lib/meatloaf/AGENTS.md` — DHD entry under Recent Changes (July 13-15, 2026): "CD/LOAD of a
  partition name/number selects it".
- `lib/meatloaf/AGENTS.md` — the 2026-08-07 entry describing the truncation fix, whose "Residual,
  not fixed" paragraph is resolved by this change.

Each should state that `CP<n>` and the `partition` console command are the only ways to switch, and
that this matches the hardware.

## Testing

Compile check with `pio run -e lolin-d32-pro`. The native suite
(`pio test -e native -f native/test_disk_write`) does not build `dhd.h` and is unaffected, but
should stay green.

On hardware, against `/sd/content/disk/dhd/hdbackup.dhd`:

1. `partition` lists partitions with the selected one marked.
2. `partition 31` selects BIBLE; `ls` then lists BIBLE start to finish with **no `select()` line
   part-way through** — the original defect.
3. `partition 1571` reports "no such partition" rather than silently landing on partition 35.
4. `partition 0` is rejected as out of range.
5. `partition bible` selects by name.
6. `cd bible` reports `cd: not a directory: bible`.
7. `LOAD"$=P"` from the C64 still lists partitions; `CP31` still selects.

Partition 255 needs an image that actually has one. If `hdbackup.dhd` does not, build the check
against the raw image instead: confirm `parse()` now reads table entry 255 (offset
`table_base + 255*32`) and that `CP255` no longer returns a syntax error. The
`.data/media/dnp/ned128test.dnp` style of direct image inspection with a short script is the cheapest
way to establish what the test image contains before trusting a hardware result.
