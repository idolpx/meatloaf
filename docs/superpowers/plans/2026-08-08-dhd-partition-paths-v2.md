# Partition References in Paths — Implementation Plan (v2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a path name a CMD HD/FD partition — enabling LOAD/SAVE and directory listing across partitions — without that reference changing the image's globally selected partition.

**Architecture:** The partition lives on the **stream**, not the MFile. `ImageBroker` caches streams, and the five operations that matter (`rewindDirectory`, `getNextFileInDir`, `isDirectory`, `exists`, `getCreationTime`) all hold that cached stream — so stream-side state reaches them for free. `D64MStream::partition` already exists for this and is vestigial today. One partition-stream is cached per image; asking for a different partition disposes and rebuilds it.

**Tech Stack:** C++17, ESP-IDF via PlatformIO.

**Spec:** `docs/superpowers/specs/2026-08-08-dhd-partition-paths-design.md` — read its revision note and its "two meanings of 0" table before writing code.

**Supersedes:** `2026-08-08-dhd-partition-paths.md`. Its Task 1 (extracting `D64MFile::entryUrlFor()`) is already landed as `54abd623` and is still needed. Its Task 2 was reverted (`8951dd49`) — the partition was put on the MFile, where only `getDecodedStream()` could see it.

## Global Constraints

- **The two meanings of 0** — both correct, never conflate:
  - *table entry index* `img->parts[0]` = the system partition (type `$FF`, listed, never mountable);
  - *partition number in a path or command* `0` = "the currently selected partition" (as `vdrive.c:1324`).
  Never resolve a path's `0` through `byNumber(0)`.
- **Resolving a partition from a path must never call `DHDImageRegistry::select()`.** Only `CP<n>` (`drive.cpp`) and the `partition` console command (`VFSCommands.cpp`) change the selection. Neither may be touched.
- A CMD HD holds a **maximum of 254** partitions (1-254); CMD FD 1-31. Do not change these bounds.
- **Never `atoi`/`std::stoi`** on C64- or network-sourced input. `strtol`, verify `end != start && *end == '\0'`, range-check **before** any narrowing cast. ESP-IDF builds `-fno-exceptions`.
- **No slash-based partition marker.** `util_get_canonical_path()` collapses runs of `/`, so `//` cannot survive in a path. The partition is an ordinary first path component.
- **Build:** `export PLATFORMIO_DATA_DIR=data/BUILD_IEC.16m` then `~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/b.log 2>&1; echo "EXIT=$?"`. `pio` is NOT on PATH. Never pipe pio through `tail`/`head` — redirect to a file, then grep it. Builds take 2-9 min: FOREGROUND, 600000 ms timeout. Do not edit `platformio.ini`.
- **Native suite:** `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_disk_write`. Baseline exactly `75 test cases: 5 skipped, 70 succeeded`. If it fails with "Access is denied" on `program.exe`: `powershell -Command "Get-Process program -ErrorAction SilentlyContinue | Stop-Process -Force"` then retry.
- **Do not stage `include/version.h`** — build artifact.
- **Searching:** `mcp__semble__search` to locate code, `mcp__serena__*` for symbol reads/edits. serena's C++ LSP does not resolve every symbol here — fall back to reading the file. Grep only for exhaustive literal sweeps (Task 1 is one).

## Testing reality

`dhd.h`/`dhd.cpp` are **not** built by any automated test; for Task 2 the compile is the only gate. **Task 1 is different and well gated:** the native suite compiles `d64.cpp` and includes `d71.h`/`d80.h`/`d81.h`/`d82.h`/`dnp.h`, and its 70 passing cases exercise geometry, BAM allocation and the write path across six formats — all of which read `partitions[partition]`. A mistake in Task 1's mechanical change will be caught there. `g64.cpp`/`nib.cpp` are firmware-only (compile gate).

Hardware steps cannot be performed by an implementer. Report them as pending; never claim them.

---

### Task 1: Repurpose `D64MStream::partition` as the CMD partition number

`partition` is declared at `d64.h:350`, read at **73 sites** as an index into `partitions[]`, and **never assigned anywhere**. Every format pushes exactly one `Partition`, so it is always 0. The multi-entry hook was never driven (D81 CBM sub-partitions and DNP `DIR` entries use `dir_track`/`dir_sector`). This task frees the field to mean "the CMD partition this stream decodes" with **no behaviour change**.

**Files (73 sites total):**
- Modify: `lib/meatloaf/media/disk/d64.h` (39 sites + the declaration + `readHeader()`'s bounds check)
- Modify: `lib/meatloaf/media/disk/d64.cpp` (19 sites)
- Modify: `lib/meatloaf/media/disk/d71.h` (4), `d81.h` (1), `hd/dnp.h` (4), `disk/g64.cpp` (3), `disk/nib.cpp` (3)

**Interfaces:**
- Consumes: nothing.
- Produces: `Partition& D64MStream::curPartition()` — the geometry record for this stream. `uint8_t D64MStream::partition` now means the CMD partition number (0 = not a CMD image).

- [ ] **Step 1: Add the accessor and re-document the field**

In `lib/meatloaf/media/disk/d64.h`, replace the declaration at line 350:

```cpp
    uint8_t partition = 0;
```

with:

```cpp
    // The CMD partition number this stream decodes (DHD/D1M/D2M/D4M);
    // 0 for every other format, which has no partition table.
    //
    // This used to be an index into partitions[], but it was never assigned:
    // every format pushes exactly one Partition, so the index was always 0 and
    // the multi-entry hook was never driven (1581 CBM sub-partitions and CMD
    // native DIR entries navigate via dir_track/dir_sector instead). Geometry
    // is now reached through curPartition(); this field carries the partition
    // identity, which is what ImageBroker-cached streams need in order to tell
    // one partition's stream from another's.
    uint8_t partition = 0;

    // Geometry for this stream. One Partition per stream, always.
    Partition& curPartition() { return partitions[0]; }
    const Partition& curPartition() const { return partitions[0]; }
```

Place it so `Partition` and `partitions` are already declared above it; if not, move the accessor below their declarations rather than moving them.

- [ ] **Step 2: Replace every read site**

Mechanically replace `partitions[partition]` with `curPartition()` across all seven files. This is an exhaustive literal sweep, so Grep/sed is the right tool:

```bash
for f in lib/meatloaf/media/disk/d64.h lib/meatloaf/media/disk/d64.cpp \
         lib/meatloaf/media/disk/d71.h lib/meatloaf/media/disk/d81.h \
         lib/meatloaf/media/hd/dnp.h lib/meatloaf/media/disk/g64.cpp \
         lib/meatloaf/media/disk/nib.cpp; do
  sed -i 's/partitions\[partition\]/curPartition()/g' "$f"
done
grep -rn "partitions\[partition\]" lib/meatloaf/ --include=*.h --include=*.cpp
```

The final grep must print nothing. Then confirm the count moved:

```bash
grep -r "curPartition()" lib/meatloaf/ --include=*.h --include=*.cpp | wc -l
```

Expected: 73 replacements plus the 2 accessor declarations = 75 lines mentioning `curPartition()`. Report the actual number.

Note `sed -i` inside the accessor you just added would corrupt it — the accessor body is `partitions[0]`, not `partitions[partition]`, so it is unaffected. Verify by reading the accessor after the sweep.

- [ ] **Step 3: Fix `readHeader()`'s bounds check**

At `d64.h` (~line 366, inside `readHeader()`), the guard reads:

```cpp
        if (partitions.empty() || partition >= partitions.size()) {
            Debug_printv("Invalid partition index: %d", partition);
            return false;
        }
```

`partition` is no longer an index, so the second test is now wrong (a CMD partition number of 31 would fail it). Replace with:

```cpp
        if (partitions.empty()) {
            Debug_printv("No partition geometry for this stream");
            return false;
        }
```

- [ ] **Step 4: Compile**

```bash
export PLATFORMIO_DATA_DIR=data/BUILD_IEC.16m
~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/t1.log 2>&1; echo "EXIT=$?"
grep -iE "error:" /tmp/t1.log | head
tail -4 /tmp/t1.log
```

Expected `EXIT=0`, `SUCCESS`. A `const`-correctness error here means a site called `curPartition()` on a const stream — that is what the const overload is for; report it if it still fails.

- [ ] **Step 5: Regression gate — this is the real check**

```bash
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_disk_write > /tmp/t1n.log 2>&1
echo "EXIT=$?"; tail -4 /tmp/t1n.log
```

Expected exactly `75 test cases: 5 skipped, 70 succeeded`. These cases exercise geometry, BAM allocation and writes across D64/D71/D80/D81/D82/DNP, all through the replaced sites. Any deviation means the sweep changed behaviour — stop and report rather than adjusting the tests.

- [ ] **Step 6: Commit**

```bash
git add lib/meatloaf/media/disk/d64.h lib/meatloaf/media/disk/d64.cpp lib/meatloaf/media/disk/d71.h lib/meatloaf/media/disk/d81.h lib/meatloaf/media/hd/dnp.h lib/meatloaf/media/disk/g64.cpp lib/meatloaf/media/disk/nib.cpp
git commit -m "refactor: free D64MStream::partition to mean the CMD partition number

The field was read at 73 sites as an index into partitions[] but was
never assigned: every format pushes exactly one Partition, so it was
always 0 and the multi-entry hook was never driven. Geometry now goes
through curPartition(), leaving the field to carry partition identity -
which is what ImageBroker-cached streams need to tell one partition's
stream from another's. No behaviour change.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: Resolve the partition from the path, per stream

**Files:**
- Modify: `lib/meatloaf/media/hd/dhd.h`, `lib/meatloaf/media/hd/dhd.cpp`
- Modify: `lib/meatloaf/media/disk/d64.h`, `d64.cpp` (broker-URL virtual)
- Modify: `lib/meatloaf/meat_media.h` (a way to peek at a cached stream)

**Interfaces:**
- Consumes from Task 1: `D64MStream::partition` (CMD number), `curPartition()`.
- Consumes from `54abd623`: `virtual std::string D64MFile::entryUrlFor(const std::string&)`.
- Produces: `DHDResolvePartition(containerUrl, in_path, out_rest, out_explicit)`; `virtual std::string D64MFile::brokerUrl()`; `ImageBroker::peek(key)`.

- [ ] **Step 1: Declare the resolver**

In `lib/meatloaf/media/hd/dhd.h`, after the `DHDImageRegistry` class:

```cpp
// Resolve which partition an in-image path refers to, WITHOUT changing the
// image's selected partition.
//
//   containerUrl  the .dhd/.d1m/.d2m/.d4m path
//   in_path       pathInStream as given
//   out_rest      (optional) the path with any partition component removed
//   out_explicit  (optional) true if in_path actually named a partition
//
// Resolution order for the first component: an in-range number, then byName(),
// otherwise it is not a partition and the currently selected one applies.
// Partition wins over a same-named file; such a file stays reachable as
// "<image>/<partition-number>/<file>".
//
// NOTE the two meanings of 0: a partition NUMBER of 0 in a path means "the
// currently selected partition" (vdrive.c:1324), NOT table entry 0, which is
// the system partition. Never resolve 0 through byNumber().
//
// Returns nullptr only if the image has no usable partition table.
const DHDPartition* DHDResolvePartition(const std::string& containerUrl,
                                        const std::string& in_path,
                                        std::string* out_rest = nullptr,
                                        bool* out_explicit = nullptr);
```

- [ ] **Step 2: Define the resolver**

In `lib/meatloaf/media/hd/dhd.cpp`, after `DHDImageRegistry::select()`:

```cpp
const DHDPartition* DHDResolvePartition(const std::string &containerUrl,
                                        const std::string &in_path,
                                        std::string *out_rest,
                                        bool *out_explicit)
{
    if (out_rest) *out_rest = in_path;
    if (out_explicit) *out_explicit = false;

    DHDImageRegistry::Image *img = DHDImageRegistry::obtain(containerUrl);
    if (img == nullptr || !img->valid)
        return nullptr;

    const DHDPartition *p = nullptr;

    if (!in_path.empty())
    {
        std::string comp = in_path;
        size_t slash = comp.find('/');
        if (slash != std::string::npos)
            comp = comp.substr(0, slash);

        if (!comp.empty())
        {
            bool numeric = comp.find_first_not_of("0123456789") == std::string::npos;
            if (numeric)
            {
                // Range-check before narrowing. v == 0 means "currently
                // selected" and must NOT go through byNumber(), which would
                // return the system partition (table entry 0).
                char *end = nullptr;
                long v = strtol(comp.c_str(), &end, 10);
                if (end != comp.c_str() && *end == '\0' && v >= 0 && v <= 254)
                    p = (v == 0) ? img->current() : img->byNumber((uint8_t)v);
            }
            else
            {
                p = img->byName(comp);
                // byName() honours wildcards, so a bare "*" can match the
                // system partition (entry 0). It is not mountable.
                if (p != nullptr && p->number == 0)
                    p = nullptr;
            }

            if (p != nullptr)
            {
                if (out_explicit) *out_explicit = true;
                if (out_rest)
                    *out_rest = (slash == std::string::npos) ? std::string()
                                                            : in_path.substr(slash + 1);
            }
        }
    }

    if (p == nullptr)
        p = img->current();

    return p;
}
```

- [ ] **Step 3: Add `#include <cstdlib>` to `dhd.cpp`**

`strtol` currently resolves only transitively. Add it beside the existing includes.

- [ ] **Step 4: Give the broker a peek, and D64MFile a broker URL**

In `lib/meatloaf/meat_media.h`, inside `ImageBroker`, beside `exists()`:

```cpp
    // The cached stream for a key, or nullptr. Lets a caller inspect what is
    // cached (e.g. which CMD partition it decodes) before deciding to reuse it.
    static std::shared_ptr<MMediaStream> peek(const std::string& key) {
        auto it = image_repo.find(key);
        return (it != image_repo.end()) ? it->second : nullptr;
    }

    // The key obtain() would use for this url, so callers can peek/dispose it.
    static std::string keyFor(const std::string& type, const std::string& url) {
        auto newFile = std::unique_ptr<MFile>(MFSOwner::File(url));
        if (newFile == nullptr || newFile->sourceFile == nullptr) return type + url;
        std::string key = type + newFile->sourceFile->url;
        if (newFile->sourceFile->pathInStream.size() && newFile->sourceFile->pathInStream != "/")
            key += "/" + newFile->sourceFile->pathInStream;
        return key;
    }
```

In `lib/meatloaf/media/disk/d64.h`, in `class D64MFile`, beside `entryUrlFor`:

```cpp
    // The URL handed to ImageBroker::obtain(). The broker rebuilds the stream
    // from this, so a subclass whose stream depends on more than the container
    // must include it here - DHDPartitionMFile appends the partition number,
    // without which the rebuilt stream decodes whichever partition is selected.
    virtual std::string brokerUrl() { return url; }
```

In `lib/meatloaf/media/disk/d64.cpp`, change all five `ImageBroker::obtain<D64MStream>("d64", url)` call sites to use `brokerUrl()`:

```bash
sed -i 's/ImageBroker::obtain<D64MStream>("d64", url)/ImageBroker::obtain<D64MStream>("d64", brokerUrl())/g' lib/meatloaf/media/disk/d64.cpp
grep -n 'ImageBroker::obtain<D64MStream>' lib/meatloaf/media/disk/d64.cpp
```

Expected: five sites, all using `brokerUrl()`.

- [ ] **Step 5: Wire `DHDPartitionMFile`**

In `lib/meatloaf/media/hd/dhd.h`, add to `DHDPartitionMFile` (public):

```cpp
    // The partition this MFile's path names; 0 = follow the image's selection.
    uint8_t m_part = 0;

    const DHDPartition* effectivePartition()
    {
        auto img = DHDImageRegistry::obtain(DHDImageRegistry::containerOf(this->url));
        if (img == nullptr) return nullptr;
        return (m_part == 0) ? img->current() : img->byNumber(m_part);
    }

    // The broker rebuilds the stream from this URL, so it must name the
    // partition. Without it the rebuild produces a partition-less MFile and
    // decodes whichever partition happens to be selected - which is how a
    // listing of a non-selected partition silently showed the wrong one.
    std::string brokerUrl() override
    {
        normalizePath();
        const DHDPartition* p = effectivePartition();
        if (p == nullptr || p->number == 0)
            return this->url;
        return this->url + "/" + std::to_string((unsigned)p->number);
    }

    std::string entryUrlFor(const std::string& filename) override
    {
        normalizePath();
        const DHDPartition* p = effectivePartition();
        if (p == nullptr || p->number == 0)
            return BASE::entryUrlFor(filename);

        std::string u = this->url;
        u += '/'; u += std::to_string((unsigned)p->number);
        if (this->pathInStream.size()) { u += '/'; u += this->pathInStream; }
        u += '/'; u += filename;
        return u;
    }
```

Replace `normalizePath()`'s body with:

```cpp
    void normalizePath()
    {
        if (normalized)
            return;
        normalized = true;

        if (!this->pathInStream.empty())
        {
            if (mstr::startsWith(this->pathInStream, "$=P") || mstr::startsWith(this->pathInStream, "$=p"))
            {
                listing_partitions = true;
                this->pathInStream.clear();
                return;
            }

            // A leading component naming a partition binds THIS path to it and
            // is stripped. It deliberately does NOT call select(): referencing a
            // partition by path must not change what the image has selected.
            std::string rest;
            bool explicit_part = false;
            const DHDPartition *p = DHDResolvePartition(
                DHDImageRegistry::containerOf(this->url), this->pathInStream, &rest, &explicit_part);

            if (p != nullptr && explicit_part)
            {
                m_part = p->number;
                this->pathInStream = rest;
            }
        }

        // One partition-stream is cached per image and the broker key does not
        // distinguish partitions, so a stream cached for a DIFFERENT partition
        // must be dropped or the directory operations would read it.
        const DHDPartition *want = effectivePartition();
        if (want != nullptr)
        {
            std::string key = ImageBroker::keyFor("d64", brokerUrlNoNormalize());
            auto cached = ImageBroker::peek(key);
            auto d64 = std::static_pointer_cast<D64MStream>(cached);
            if (d64 != nullptr && d64->partition != want->number)
                ImageBroker::dispose(key);
        }
    }

    // brokerUrl() without the normalizePath() call, for use from inside it.
    std::string brokerUrlNoNormalize()
    {
        const DHDPartition* p = effectivePartition();
        if (p == nullptr || p->number == 0)
            return this->url;
        return this->url + "/" + std::to_string((unsigned)p->number);
    }
```

- [ ] **Step 6: Stamp the partition onto the stream**

In `DHDPartitionMFile::getDecodedStream()`, after building the stream, record which partition it decodes. Replace the body's tail so each branch sets it — build the stream into a local, set `stream->partition = p->number;`, then return it:

```cpp
        normalizePath();
        const DHDPartition* p = effectivePartition();
        if (p == nullptr)
            return nullptr;

        auto view = std::make_shared<DHDOffsetStream>(is, p->start, p->size);
        std::shared_ptr<D64MStream> stream;
        switch (p->type)
        {
            case 2: stream = std::make_shared<D64MStream>(view); break;
            case 3: stream = std::make_shared<D71MStream>(view); break;
            case 4: stream = std::make_shared<D81MStream>(view); break;
            default: stream = std::make_shared<DNPMStream>(view); break;
        }
        // Identity, not geometry: this is what lets a cached stream be
        // recognised as belonging to another partition.
        if (stream != nullptr)
            stream->partition = p->number;
        return stream;
```

Keep the existing comment about `allow_grow` staying false.

- [ ] **Step 7: Compile**

```bash
export PLATFORMIO_DATA_DIR=data/BUILD_IEC.16m
~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/t2.log 2>&1; echo "EXIT=$?"
grep -iE "error:" /tmp/t2.log | head
tail -4 /tmp/t2.log
```

Expected `EXIT=0`, `SUCCESS`.

- [ ] **Step 8: Native regression gate**

```bash
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_disk_write > /tmp/t2n.log 2>&1
echo "EXIT=$?"; tail -4 /tmp/t2n.log
```

Expected exactly `75 test cases: 5 skipped, 70 succeeded`.

- [ ] **Step 9: Hard gate — nothing but the two commands may select**

```bash
grep -rn "DHDImageRegistry::select" lib/
```

Expected exactly three lines: the definition in `dhd.cpp`, and calls in `VFSCommands.cpp` and `drive.cpp`. If `dhd.h` appears, stop and report BLOCKED.

- [ ] **Step 10: Commit**

```bash
git add lib/meatloaf/media/hd/dhd.h lib/meatloaf/media/hd/dhd.cpp lib/meatloaf/media/disk/d64.h lib/meatloaf/media/disk/d64.cpp lib/meatloaf/meat_media.h
git commit -m "feat: reference CMD partitions by path, resolved per stream

A leading path component naming a partition binds that path to it without
changing the image's selected partition, so LOAD, SAVE and directory
listing can cross partitions. Partition 0 in a path means the currently
selected partition (vdrive.c:1324), not table entry 0.

The partition is recorded on the STREAM, which is what makes it visible to
rewindDirectory/getNextFileInDir/isDirectory/exists - they all hold the
ImageBroker-cached stream. A previous attempt put it on the MFile, where
only getDecodedStream() could see it, and listing a non-selected partition
silently read the selected one.

Two pieces are needed together: brokerUrl() names the partition so the
broker's rebuild resolves the right one, and normalizePath() disposes a
stream cached for a different partition, since the broker key cannot tell
partitions apart.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

**Pending user hardware verification:** `cd 31` then `ls` lists partition 31 while `partition` still marks the previously selected one; listing a non-selected partition yields only its own entries; LOAD and SAVE across partitions; a 1571 partition by path decodes as D71; `CP<n>`, `partition` and `LOAD"$=P"` unchanged; CMD FD images.

---

### Task 3: Document it

**Files:** `AGENTS.md`, `lib/meatloaf/AGENTS.md`, `.claude/skills/commodore64-meatloaf/SKILL.md`

- [ ] **Step 1: Root `AGENTS.md`** — in the CMD media images bullet, replace the sentence beginning "A partition name or number appearing in a path is NOT a selection" with:

```
A partition name or number as the FIRST in-image path component binds that path to that partition without changing the selection (`DHDResolvePartition()`). Resolution order: in-range number, then `byName()`, else it is a file — **partition wins over a same-named file**, which stays reachable as `<image>/<number>/<file>`. **A partition NUMBER of 0 in a path means "the currently selected partition"** (`vdrive.c:1324`), NOT table entry 0, the system partition. The partition is recorded on the STREAM (`D64MStream::partition`, repurposed from a never-assigned index; geometry moved to `curPartition()`), because `ImageBroker` caches streams and `rewindDirectory`/`getNextFileInDir`/`isDirectory`/`exists` all read the cached one — putting it on the MFile instead made listings silently show the selected partition. One partition-stream is cached per image: `brokerUrl()` names the partition so the broker's rebuild resolves it, and `normalizePath()` disposes a stream cached for a different partition, since the key cannot distinguish them. No slash-based syntax is possible — `util_get_canonical_path()` collapses runs of `/`.
```

- [ ] **Step 2: Add a changelog entry** at the top of `## Recent Changes (August 8, 2026)` in `AGENTS.md` summarising the above in two sentences, including that `entryUrlFor()`/`brokerUrl()` are the two `D64MFile` virtuals a container subclass overrides.

- [ ] **Step 3: `lib/meatloaf/AGENTS.md`** — in the DHD entry, replace "path-based selection was removed on 2026-08-07, see that entry" with a pointer to the 2026-08-08 model and the stream-side partition.

- [ ] **Step 4: `.claude/skills/commodore64-meatloaf/SKILL.md`** — in the Change Partition section, document `cd 31` / `//31/game`-style *first-component* references, that they do not change the selection, and that `0` means the current partition.

- [ ] **Step 5: Verify** `grep -rn "path is NOT a selection" --include=*.md . | grep -v docs/superpowers` prints nothing.

- [ ] **Step 6: Commit** with `docs: describe stream-side partition references in paths`.

---

## Self-review notes

- **Spec coverage:** partition on the stream → Task 1 + Task 2 Step 6; resolver → Steps 1-3; broker URL + dispose-on-mismatch (both required) → Steps 4-5; no `select()` from paths → Step 9 gate; docs → Task 3.
- **Type consistency:** `curPartition()` declared in Task 1 Step 1, used by the Step 2 sweep. `brokerUrl()` declared in Task 2 Step 4, overridden in Step 5. `entryUrlFor()` comes from `54abd623`, overridden in Step 5 with the same signature. `ImageBroker::peek`/`keyFor` declared in Step 4, used in Step 5.
- **Known gap:** Task 2 has only a compile gate; nothing automated reaches `dhd.*`. Task 1 is genuinely gated by the native suite. Do not claim otherwise.
- **Known risk:** `normalizePath()` now calls `effectivePartition()` → `DHDImageRegistry::obtain()`, which parses the image on first use. Confirm during review that this does not recurse via `MFSOwner::File` in a way that re-enters `normalizePath()`.
