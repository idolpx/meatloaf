# Partition References in Paths — CMD HD/FD

**Date:** 2026-08-08
**Status:** DESIGN — REVISED 2026-08-08 after a failed first implementation. Read the revision note.

> **Revision — the first implementation was reverted (`128df503`, reverted by `8951dd49`).**
>
> The *model* below (a partition resolved from the path, 0 meaning "the selected
> partition", no `select()` mutation) was right. The *mechanism* was wrong: it put the
> partition solely on the `MFile`, where only `getDecodedStream()` could see it.
>
> `ImageBroker` caches **streams**, and the five operations that matter —
> `rewindDirectory`, `getNextFileInDir`, `isDirectory`, `exists`, `getCreationTime` —
> all fetch the cached stream via `ImageBroker::obtain("d64", url)` where `url` is the
> container only. `obtain()` then rebuilds the stream from a fresh `MFSOwner::File(url)`
> whose `pathInStream` is empty, so the MFile's partition never reached any of them.
> Listing a non-selected partition read the *selected* one and stamped its entries with
> the wrong partition number. A whole-branch review caught it; both causes were verified
> against the source.
>
> **The corrected mechanism is in "Design" below: the partition lives on the STREAM.**
> That is what makes it visible to all five operations for free, because they all hold
> the stream. See `D64MStream::partition`, which already exists for this purpose.
**Supersedes:** the "Remove path-based selection" section of
`2026-08-07-dhd-partition-command-design.md`. Everything else in that document
(the `partition` console command, the documentation corrections, the 254 bound,
listing the system partition) stands.

## Problem

The 2026-08-07 work removed path-based partition selection entirely, making
partitions purely modal: `CP<n>` or the `partition` console command, nothing
else. That fixed the reported bug — a directory listing switching partitions
part-way through — but it went too far. It also removed the ability to
**load or save across partitions**, because a path can no longer name one:

```
LOAD"//subs/game",8     — no way to express this
```

The right model is not "paths cannot name partitions". It is "**naming a
partition in a path does not change the selected partition**". The bug was
never that paths referenced partitions; it was that referencing one *mutated
global state* and *stripped the partition from the path*, which together made
listing-generated entry URLs ambiguous.

## Model

A partition reference becomes a property of the **MFile**, not of the image.

- `DHDPartitionMFile` carries a resolved partition number.
- **Partition `0` means "use the globally selected partition."** This matches
  the reference implementation: `lib/vdrive/vdrive.c:1324-1327` —
  *"if it has a partition table and the part is 0, make it whatever the
  selected partition is"*.
- Resolving a partition from a path **never calls `select()`**. The global
  selection changes only via `CP<n>` and the `partition` console command.

### The two meanings of 0 — read this before touching the code

`0` means different things in different roles, and both are correct:

| Role | Meaning of 0 |
|---|---|
| **Table entry index** (`parse()`, the partition listing) | The system partition — holds the drive label and the partition table. Listed as type `$FF` (`sys`/`SYS`); `select()` refuses it. |
| **Partition number in a path or command** | "The currently selected partition." Not a reference to the system partition. |

So `DHDPartitionMFile::m_part == 0` means *unspecified → use `img->selected`*,
while `img->parts[0]` is the system partition. Anything that conflates the two
will mount the partition table as if it were a disk.

### Resolution rule

For the first component of the in-image path:

1. If it parses as a number in range (via `strtol`, `*end == '\0'`, `0..254` —
   never `atoi`), and that partition exists → it is a partition reference.
2. Otherwise, if `byName()` matches a partition → it is a partition reference.
3. Otherwise → it is a file or subdirectory inside the effective partition.

**Partition wins over a same-named file** (project owner's ruling). The cost is
that a file whose name matches a partition is unreachable from the image root;
that is accepted. That file will have to be reference using partition number rather than partition name.

### Generated paths always name their partition

**Assumption — flag it if wrong.** Paths that Meatloaf *emits* (directory
listing entry URLs, and therefore `pwd`) always include the partition
component. Paths that Meatloaf *accepts* keep the rule above.

This is a correctness requirement, not a style choice. `D64MFile::getNextFileInDir()`
builds each entry as `url + "/" + pathInStream + "/" + filename`, and
`normalizePath()` strips the partition from `pathInStream`. Without
re-inserting it, every listing of a non-selected partition emits entries that
resolve into the *selected* one:

```
cwd:        /sd/x.dhd/subs        (path partition SUBS, selected BIBLE)
entry URL:  /sd/x.dhd/game        → resolves into BIBLE — wrong partition
```

Emitting the partition also makes the 2026-08-07 bug impossible by
construction: a generated path is never ambiguous, so no listing can be
hijacked by an entry whose name happens to match a partition.

## Design

### The partition lives on the stream

`D64MStream::partition` (`d64.h:350`) already exists and is **vestigial**: it is read at
~20 sites as an index into `partitions[]`, but it is never assigned anywhere, and every
format pushes exactly one `Partition`, so it is always 0. The multi-entry index hook was
never driven — D81 CBM sub-partitions and DNP `DIR` entries use `dir_track`/`dir_sector`,
not this vector.

It is **repurposed** to mean *"the CMD partition number this stream decodes"* (0 = not a
CMD image). The ~20 read sites move to an accessor returning `partitions[0]`.

This is what fixes the failure above: `ImageBroker` caches streams, so every operation
that holds the stream sees the partition without any per-call plumbing.

### One partition-stream cached per image

The stream for a CMD image decodes exactly one partition — a `DHDOffsetStream` window at
that partition's offset, wrapped in the class matching its type (unchanged). Only one such
stream is cached per image at a time.

Two pieces are needed, and **both** are required — either alone leaves the bug in place:

1. **A partition-carrying broker URL.** `ImageBroker::obtain()` rebuilds the stream from
   `MFSOwner::File(url)`. Given only the container it produces a partition-less MFile and
   decodes the *selected* partition. `D64MFile` gains a virtual for the URL it hands the
   broker (default: `url`); `DHDPartitionMFile` overrides it to append the partition
   number, so the rebuild resolves the intended partition.
2. **Dispose on mismatch.** The broker key is derived from the container's `sourceFile`,
   so it does **not** distinguish partitions — two partitions collide on one key. Before
   any operation, if a stream is cached for this image whose `partition` differs from the
   one wanted, dispose it so `obtain()` rebuilds. `DHDPartitionMFile::normalizePath()` is
   the single choke point: it already runs from all five entry points.

Consequence: interleaving access to two partitions of the same image re-parses on each
switch. Accepted — `select()` already disposes on every partition change today.

### A single shared resolver

```cpp
// Resolve the partition a DHD/DXM in-image path refers to.
//   in_path  — pathInStream as given
//   out_rest — the remainder after any partition component (may be null)
// Returns the partition, or nullptr if the image has no partition table.
// A leading component naming a partition is consumed; otherwise the currently
// selected partition is returned and out_rest == in_path.
const DHDPartition* DHDResolvePartition(const std::string& containerUrl,
                                        const std::string& in_path,
                                        std::string* out_rest);
```

One implementation, two callers — this must not be duplicated:

- **`DHDCreatePartitionFile()`** picks the base class (`D64MFile`, `D71MFile`,
  `D81MFile`, `DNPMFile`) from the partition's **type**, at construction, before
  the object exists. Today it reads `img->current()`. It must read the path, or
  `x.dhd/subs/game` decodes a 1571 partition through a DNP stream.
- **`DHDPartitionMFile::normalizePath()`** records the number and strips the
  component.

### Changes by file

| File | Change |
|---|---|
| `media/hd/dhd.h/.cpp` | Add `DHDResolvePartition()`. `DHDPartitionMFile` gains `m_part` (0 = selected). `normalizePath()` resolves and strips without calling `select()`. `getDecodedStream()` uses `m_part` rather than `img->current()`. `getNextFileInDir()` re-inserts the partition into entry URLs. |
| `media/disk/dxm.h` | Uses the shared `DHDCreatePartitionFile()`, so it follows automatically — verify, do not duplicate. |
| `console/Commands/VFSCommands.cpp` | `partition` command unchanged; it still sets the global selection. |
| `device/iec/drive.cpp` | `CP<n>` unchanged. |

### What this reverses

`cd bible` works again. Task 3 deliberately made it report
`cd: not a directory: bible`; under this design it navigates into the partition
without changing the selection. The `partition` command and `CP<n>` remain the
only ways to change what is *selected*.

## Non-goals

- Changing `LOAD"$=P"`, the partition table parser, the 254 bound, or the
  system-partition listing.
- Changing how `CP<n>` or the `partition` command behave.
- A distinct partition syntax (e.g. `//name`). Meatloaf already uses `//` for
  "from root" (`cd//sd/games`), so it is unavailable.

## Risks

- **The base-class-by-type path is the sharp edge.** If `DHDCreatePartitionFile()`
  and `normalizePath()` ever disagree about which partition a path names, the
  MFile decodes through the wrong stream class. That is why they must share one
  resolver.
- **`m_part == 0` conflated with the system partition** would mount the
  partition table as a disk. See the two-meanings table above.
- **Entry-URL rewriting** is the fiddliest part: the base class creates each
  entry via `MFSOwner::File(entryUrl)` and then overwrites its fields, so the
  partition must be in the URL *before* that call, not patched afterwards.

## Testing

No automated harness reaches `dhd.h`/`dhd.cpp` (the native suite does not build
them). Verification is a compile gate, the native suite as a regression check on
the shared `d64.cpp` engine (baseline 75 / 5 skipped / 70 succeeded), and
hardware:

1. `cd bible`, `ls` — full BIBLE listing, no partition switch, `partition` still
   reports the previously selected partition as `*`.
2. With BIBLE selected, list a *different* partition by path — every entry must
   come from that partition, not BIBLE. This is the case the entry-URL rule
   exists for.
3. `LOAD` a file from a non-selected partition by path; confirm the selection is
   unchanged afterwards.
4. `SAVE` to a non-selected partition by path; confirm it lands there.
5. A 1571 partition referenced by path decodes as D71, not DNP (base-class
   selection from the path).
6. `CP<n>` and `partition <n>` still change the selection; `LOAD"$=P"` unchanged.
7. The 2026-08-07 regression: a file whose name matches a partition, inside a
   listing, must not hijack the rest of it.
8. CMD FD (`.d1m`/`.d2m`/`.d4m`) — same checks; shares the resolver.
