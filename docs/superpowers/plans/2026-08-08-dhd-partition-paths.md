# Partition References in Paths Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a path name a CMD HD/FD partition — enabling LOAD/SAVE across partitions — without that reference changing the image's globally selected partition.

**Architecture:** The partition becomes a property of the `MFile` (`m_part`, where **0 means "use the globally selected partition"**, matching `vdrive.c:1324`) instead of being read from `DHDImageRegistry::Image::selected` at every use. One shared resolver, `DHDResolvePartition()`, is used by both `DHDCreatePartitionFile()` (which picks the base class from the partition's type at construction) and `normalizePath()` (which records the number and strips the component), so the two can never disagree. Directory listings emit entry URLs that name their partition, which is required for a non-selected partition to list correctly.

**Tech Stack:** C++17, ESP-IDF via PlatformIO.

**Spec:** `docs/superpowers/specs/2026-08-08-dhd-partition-paths-design.md` — read its "two meanings of 0" table before writing code.

## Global Constraints

- **Partition numbering:** a CMD HD holds a **maximum of 254** partitions, numbered 1-254. Table entry 0 is the system partition (listed as type `$FF`, never selectable). CMD FD is 1-31. Do not change these bounds.
- **The two meanings of 0** — both correct, never conflate:
  - *table entry index* `img->parts[0]` = the system partition;
  - *partition number in a path or command* `0` = "the currently selected partition".
  `m_part == 0` therefore means *unspecified → use `img->current()`*, and must **never** be resolved via `byNumber(0)`, which returns the system partition.
- **Resolving a partition from a path must never call `DHDImageRegistry::select()`.** Only `CP<n>` and the `partition` console command change the global selection.
- **Never `atoi`/`std::stoi`** on C64- or network-sourced input. Use `strtol`, verify `end != start && *end == '\0'`, range-check **before** any narrowing cast. ESP-IDF builds `-fno-exceptions`.
- **Resolution order** for the first in-image path component: in-range number → `byName()` → otherwise a file/dir in the effective partition. **Partition wins over a same-named file.** A colliding file stays reachable as `<image>/<partition-number>/<file>`.
- **Build:** `export PLATFORMIO_DATA_DIR=data/BUILD_IEC.16m` then `~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/b.log 2>&1; echo "EXIT=$?"`. `pio` is NOT on PATH. Never pipe pio through `tail`/`head` — redirect to a file, then grep it. Builds take 2-9 minutes; run in the FOREGROUND with a 600000 ms timeout. Do not edit `platformio.ini`.
- **Searching:** use `mcp__semble__search` to locate code and `mcp__serena__*` for symbol reads/edits. Grep only for exhaustive literal sweeps. (Note: serena's C++ LSP does not resolve every symbol in this project — fall back to reading the file when it returns nothing.)
- **Do not stage `include/version.h`** — a build artifact.

## Testing reality — read before starting

No automated harness reaches `dhd.h`, `dhd.cpp`, or `lib/console`. The native suite (`pio test -e native -f native/test_disk_write`) compiles only `d64.cpp`, `meat_media.cpp`, `string_utils.cpp`, `punycode.cpp`, `U8Char.cpp`, and `MFSOwner::File()` is an `abort()` stub there — DHD code cannot run natively. **Task 1 changes `d64.cpp`, which the native suite DOES build, so that suite is a real regression gate for Task 1.** Baseline exactly: 75 test cases, 5 skipped, 70 succeeded.

Do not invent tests. Hardware steps cannot be performed by an implementer — they are listed per task for the user and must be reported as pending, never claimed.

## Design decision the implementer must not re-litigate

**Emitted entry URLs use the partition NUMBER, not its name.** Partition names may contain `/` (they are replaced with `\` for display), spaces, and PETSCII bytes, none of which survive a round trip through a URL path component. Numbers always parse and always resolve. Consequence: after `cd subs`, `pwd` shows `.../x.dhd/2`. This is deliberate.

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `lib/meatloaf/media/disk/d64.h/.cpp` | Base disk MFile | Extract the entry-URL construction into a `virtual std::string entryUrlFor(const std::string&)` so subclasses can inject a prefix |
| `lib/meatloaf/media/hd/dhd.h/.cpp` | CMD partition layer | Add `DHDResolvePartition()`; `DHDPartitionMFile` gains `m_part`; `normalizePath()`, `getDecodedStream()`, `entryUrlFor()` use it; `DHDCreatePartitionFile()` resolves from the path |
| `AGENTS.md`, `lib/meatloaf/AGENTS.md`, `.claude/skills/commodore64-meatloaf/SKILL.md` | Docs | Describe the new path model |

Task order matters: Task 1 is a pure refactor with no behaviour change and its own regression gate; Task 2 builds on it.

---

### Task 1: Make entry-URL construction overridable

Pure refactor. `D64MFile::getNextFileInDir()` hardcodes how a directory entry's URL is built. Task 2 needs to inject a partition component. Extract it now, with no behaviour change, so the native suite proves the extraction is faithful before any semantics move.

**Files:**
- Modify: `lib/meatloaf/media/disk/d64.h` (declare the new virtual, ~line 779 beside `getNextFileInDir`)
- Modify: `lib/meatloaf/media/disk/d64.cpp` (`D64MFile::getNextFileInDir`, the block at ~1454-1459)

**Interfaces:**
- Consumes: nothing.
- Produces: `virtual std::string D64MFile::entryUrlFor(const std::string& filename)` — returns the absolute URL for a directory entry of this MFile. Task 2 overrides it in `DHDPartitionMFile`.

- [ ] **Step 1: Declare the virtual**

In `lib/meatloaf/media/disk/d64.h`, in `class D64MFile`, immediately after the `MFile* getNextFileInDir() override;` declaration:

```cpp
    // Builds the URL for one entry of THIS directory. Virtual so a subclass can
    // inject a component the base class knows nothing about - DHDPartitionMFile
    // re-inserts the partition, without which a listing of a non-selected
    // partition would emit entries that resolve into the selected one.
    virtual std::string entryUrlFor(const std::string& filename);
```

- [ ] **Step 2: Define it and call it**

In `lib/meatloaf/media/disk/d64.cpp`, add the definition immediately above `D64MFile::getNextFileInDir()`:

```cpp
std::string D64MFile::entryUrlFor(const std::string& filename)
{
    // Entry URL must include the in-image path (partition/subdirectory)
    std::string entryUrl;
    entryUrl.reserve(url.size() + pathInStream.size() + 2 + filename.size());
    entryUrl = url;
    if (pathInStream.size()) { entryUrl += '/'; entryUrl += pathInStream; }
    entryUrl += '/'; entryUrl += filename;
    return entryUrl;
}
```

Then in `getNextFileInDir()`, replace exactly these six lines:

```cpp
        // Entry URL must include the in-image path (partition/subdirectory)
        std::string entryUrl;
        entryUrl.reserve(url.size() + pathInStream.size() + 2 + filename.size());
        entryUrl = url;
        if (pathInStream.size()) { entryUrl += '/'; entryUrl += pathInStream; }
        entryUrl += '/'; entryUrl += filename;
```

with:

```cpp
        std::string entryUrl = entryUrlFor(filename);
```

Leave the following `auto file = MFSOwner::File(entryUrl);` and everything after it untouched.

- [ ] **Step 3: Compile**

```bash
export PLATFORMIO_DATA_DIR=data/BUILD_IEC.16m
~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/t1.log 2>&1; echo "EXIT=$?"
grep -iE "error:" /tmp/t1.log | head
tail -4 /tmp/t1.log
```

Expected: `EXIT=0`, `SUCCESS`, no `error:` lines.

- [ ] **Step 4: Regression gate — the native suite builds d64.cpp**

```bash
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_disk_write > /tmp/t1n.log 2>&1
echo "EXIT=$?"; tail -4 /tmp/t1n.log
```

Expected exactly: `75 test cases: 5 skipped, 70 succeeded`. Report the actual numbers. If it fails with "Access is denied" on `program.exe`, run `powershell -Command "Get-Process program -ErrorAction SilentlyContinue | Stop-Process -Force"` and retry.

- [ ] **Step 5: Commit**

```bash
git add lib/meatloaf/media/disk/d64.h lib/meatloaf/media/disk/d64.cpp
git commit -m "refactor: make D64MFile entry-URL construction overridable

Extracts the entry URL built by getNextFileInDir() into a virtual
entryUrlFor(), with no behaviour change, so a subclass can inject a
component the base class does not know about. DHDPartitionMFile needs
this to name the partition in emitted entry URLs.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: Per-MFile partition resolved from the path

**Files:**
- Modify: `lib/meatloaf/media/hd/dhd.h` (declare `DHDResolvePartition`; `DHDPartitionMFile` members and overrides; `DHDCreatePartitionFile`)
- Modify: `lib/meatloaf/media/hd/dhd.cpp` (define `DHDResolvePartition`)

**Interfaces:**
- Consumes from Task 1: `virtual std::string D64MFile::entryUrlFor(const std::string&)`.
- Produces: `const DHDPartition* DHDResolvePartition(const std::string& containerUrl, const std::string& in_path, std::string* out_rest, bool* out_explicit)`.

- [ ] **Step 1: Declare the resolver**

In `lib/meatloaf/media/hd/dhd.h`, after the `DHDImageRegistry` class definition and before the Streams section:

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
// otherwise it is not a partition at all and the currently selected partition
// applies. Partition wins over a same-named file; such a file is still
// reachable as "<image>/<partition-number>/<file>".
//
// NOTE the two meanings of 0: a partition NUMBER of 0 in a path means "the
// currently selected partition" (as vdrive.c:1324 does it), NOT table entry 0,
// which is the system partition. Never resolve 0 through byNumber().
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
                // Range-check before narrowing. 0 means "currently selected"
                // and must NOT go through byNumber(), which would return the
                // system partition (table entry 0).
                char *end = nullptr;
                long v = strtol(comp.c_str(), &end, 10);
                if (end != comp.c_str() && *end == '\0' && v >= 0 && v <= 254)
                    p = (v == 0) ? img->current() : img->byNumber((uint8_t)v);
            }
            else
            {
                p = img->byName(comp);
            }

            // Never let a path select the system partition.
            if (p != nullptr && p->number == 0 && !(numeric && comp == "0"))
                p = nullptr;

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

- [ ] **Step 3: Give `DHDPartitionMFile` a partition**

In `lib/meatloaf/media/hd/dhd.h`, in `DHDPartitionMFile`, add beside the existing `normalized` / `listing_partitions` / `part_index` members:

```cpp
    // The partition this MFile refers to. 0 means "whatever the image has
    // selected", so an MFile created before a CP<n> follows the new selection.
    // Set only when the PATH named a partition.
    uint8_t m_part = 0;

    // The partition resolved for THIS MFile: m_part when the path named one,
    // otherwise the image's current selection.
    const DHDPartition* effectivePartition()
    {
        auto img = DHDImageRegistry::obtain(DHDImageRegistry::containerOf(this->url));
        if (img == nullptr)
            return nullptr;
        return (m_part == 0) ? img->current() : img->byNumber(m_part);
    }
```

- [ ] **Step 4: Resolve in `normalizePath()` without selecting**

Replace the body of `DHDPartitionMFile::normalizePath()` with:

```cpp
    void normalizePath()
    {
        if (normalized)
            return;
        normalized = true;

        if (this->pathInStream.empty())
            return;

        // "$=P" switches to partition-list mode.
        if (mstr::startsWith(this->pathInStream, "$=P") || mstr::startsWith(this->pathInStream, "$=p"))
        {
            listing_partitions = true;
            this->pathInStream.clear();
            return;
        }

        // A leading component naming a partition binds THIS MFile to that
        // partition and is stripped, so the base class resolves the rest inside
        // it. It deliberately does NOT call select(): referencing a partition by
        // path must not change what the image has selected. That coupling is
        // what let a directory listing switch partitions part-way through.
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
```

- [ ] **Step 5: Decode through the resolved partition**

In `DHDPartitionMFile::getDecodedStream()`, replace:

```cpp
        auto img = DHDImageRegistry::obtain(DHDImageRegistry::containerOf(this->url));
        const DHDPartition* p = img ? img->current() : nullptr;
        if (p == nullptr)
            return nullptr;
```

with:

```cpp
        normalizePath();
        const DHDPartition* p = effectivePartition();
        if (p == nullptr)
            return nullptr;
```

- [ ] **Step 6: Name the partition in emitted entry URLs**

Add this override to `DHDPartitionMFile` (it is why Task 1 exists):

```cpp
    // Emit entry URLs that name their partition. Without this, listing a
    // partition other than the selected one produces entries that resolve into
    // the SELECTED partition - the base class strips the partition from
    // pathInStream, so a bare entry URL carries no partition at all.
    // The NUMBER is used, not the name: names may contain '/', spaces and
    // PETSCII bytes, none of which survive a URL path component.
    std::string entryUrlFor(const std::string& filename) override
    {
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

- [ ] **Step 7: Pick the base class from the path**

`DHDCreatePartitionFile()` currently chooses the MFile subclass from `img->current()`. It must use the path, or `x.dhd/subs/game` decodes a 1571 partition through a DNP stream. Replace its partition lookup:

```cpp
inline MFile* DHDCreatePartitionFile(std::string path)
{
    uint8_t type = 1;
    // Resolve from the PATH, not from the current selection: the base class is
    // chosen by the partition's type, and the path may name a partition other
    // than the selected one. Shares one resolver with normalizePath() so the
    // two can never disagree about which partition a path means.
    std::string container = DHDImageRegistry::containerOf(path);
    std::string in_path = path.size() > container.size()
                        ? path.substr(container.size() + 1) : std::string();
    const DHDPartition* p = DHDResolvePartition(container, in_path);
    if (p != nullptr)
        type = p->type;

    switch (type)
    {
```

Leave the `switch` body unchanged.

- [ ] **Step 8: Compile**

```bash
export PLATFORMIO_DATA_DIR=data/BUILD_IEC.16m
~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/t2.log 2>&1; echo "EXIT=$?"
grep -iE "error:" /tmp/t2.log | head
tail -4 /tmp/t2.log
```

Expected: `EXIT=0`, `SUCCESS`.

- [ ] **Step 9: Native regression gate**

```bash
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_disk_write > /tmp/t2n.log 2>&1
echo "EXIT=$?"; tail -4 /tmp/t2n.log
```

Expected exactly: `75 test cases: 5 skipped, 70 succeeded`.

- [ ] **Step 10: Confirm no path can still mutate the selection**

```bash
grep -n "DHDImageRegistry::select" lib/ -r
```

Expected exactly two callers: `lib/console/Commands/VFSCommands.cpp` (the `partition` command) and `lib/device/iec/drive.cpp` (`CP<n>`). If `dhd.h` appears, the change is incomplete — stop and report.

- [ ] **Step 11: Commit**

```bash
git add lib/meatloaf/media/hd/dhd.h lib/meatloaf/media/hd/dhd.cpp
git commit -m "feat: reference CMD partitions by path without changing the selection

A leading path component naming a partition now binds that MFile to the
partition instead of mutating the image's selected partition, so LOAD and
SAVE can cross partitions. Partition 0 in a path means 'the currently
selected partition' (as vdrive.c:1324 does it), not table entry 0, which
is the system partition.

DHDResolvePartition() is the single resolver, shared by normalizePath()
and DHDCreatePartitionFile() - the latter picks the MFile base class from
the partition type at construction, so the two disagreeing would decode a
partition through the wrong stream class.

Emitted entry URLs name their partition by number, without which listing a
non-selected partition produces entries resolving into the selected one.
Naming the partition in generated paths also makes the earlier mid-listing
hijack impossible: a generated path is never ambiguous.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

**Pending user hardware verification** (cannot be done by an implementer): `cd bible` then `ls` lists BIBLE while `partition` still marks the previously selected partition; listing a non-selected partition yields only its own entries; LOAD and SAVE across partitions; a 1571 partition referenced by path decodes as D71; `CP<n>` and `LOAD"$=P"` unchanged; CMD FD images.

---

### Task 3: Document the path model

**Files:**
- Modify: `AGENTS.md` (CMD media images note + a 2026-08-08 changelog entry)
- Modify: `lib/meatloaf/AGENTS.md` (DHD entry)
- Modify: `.claude/skills/commodore64-meatloaf/SKILL.md` (CD/partition sections)

**Interfaces:** Consumes the behaviour from Tasks 1-2. Produces documentation only.

- [ ] **Step 1: Root `AGENTS.md` — replace the path-selection sentence**

Find the sentence in the **CMD media images** bullet beginning "A partition name or number appearing in a path is NOT a selection" and replace it with:

```
A partition name or number in a path binds THAT PATH to that partition without changing the selection (`DHDResolvePartition()`, shared by `normalizePath()` and `DHDCreatePartitionFile()` — the latter picks the MFile base class from the partition type, so a disagreement would decode through the wrong stream class). Resolution order for the first in-image component: in-range number, then `byName()`, else it is a file — **partition wins over a same-named file**, which stays reachable as `<image>/<number>/<file>`. **A partition NUMBER of 0 in a path means "the currently selected partition"** (as `vdrive.c:1324` does it), NOT table entry 0, which is the system partition — never resolve 0 via `byNumber()`. Emitted entry URLs name their partition BY NUMBER (names may contain `/`, spaces and PETSCII), which is required for a non-selected partition to list correctly and makes a listing-time partition hijack impossible.
```

- [ ] **Step 2: Root `AGENTS.md` — add the changelog entry**

Add a `## Recent Changes (August 8, 2026)` section immediately above `## Recent Changes (August 7, 2026)`:

```markdown
## Recent Changes (August 8, 2026)

- **Partitions can be referenced by path again — without changing the selection** (`lib/meatloaf/media/hd/dhd.h/.cpp`, `lib/meatloaf/media/disk/d64.h/.cpp`): the 2026-08-07 work removed path-based partition references entirely, which fixed the mid-listing hijack but also removed any way to LOAD/SAVE across partitions. The partition is now a property of the MFile (`m_part`, 0 = follow the global selection) resolved by a single shared `DHDResolvePartition()`, and resolving one never calls `select()`. `D64MFile::entryUrlFor()` was extracted as a virtual so `DHDPartitionMFile` can name the partition in emitted entry URLs — required, because the base class strips the partition from `pathInStream`, so bare entry URLs would resolve into the SELECTED partition when listing any other one. `cd <partition>` works again; `CP<n>` and the `partition` console command remain the only things that change the selection.
```

- [ ] **Step 3: `lib/meatloaf/AGENTS.md`**

In the DHD entry, replace "`CP<n>` (drive) and the `partition` console command select — path-based selection was removed on 2026-08-07, see that entry." with:

```
`CP<n>` (drive) and the `partition` console command are the only things that change the SELECTED partition; a partition named in a path binds only that path (`DHDResolvePartition()`, `m_part` on `DHDPartitionMFile`, 0 = follow the selection). See the 2026-08-08 entry.
```

- [ ] **Step 4: `.claude/skills/commodore64-meatloaf/SKILL.md`**

In the **Change Partition** section, after the sentence about the working directory being set to that partition's root, add:

```
A partition can also be named directly in a path — `//2/game` or
`//subs/game` — which loads or saves from that partition without
changing the selected one. A partition number of `0` in a path means
"the currently selected partition". Where a file has the same name as
a partition, the partition wins; reach the file by giving the
partition number explicitly, e.g. `//2/subs`.
```

- [ ] **Step 5: Verify no stale claim remains**

```bash
grep -rn "path is NOT a selection\|path-based selection was removed" --include=*.md . | grep -v docs/superpowers
```

Expected: no output. Hits under `docs/superpowers/` are historical specs/plans and are correct to leave.

- [ ] **Step 6: Commit**

```bash
git add AGENTS.md lib/meatloaf/AGENTS.md .claude/skills/commodore64-meatloaf/SKILL.md
git commit -m "docs: describe partition references in paths

Records the new model: a partition named in a path binds that path only,
0 means the currently selected partition, partition wins over a same-named
file, and emitted entry URLs carry the partition number.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Self-review notes

- **Spec coverage:** shared resolver → Task 2 Steps 1-2, 7; per-MFile partition with 0 = selected → Steps 3-5; no `select()` from paths → Step 4 + the Step 10 grep; generated paths name the partition → Task 1 + Step 6; base class by type → Step 7; docs → Task 3.
- **Type consistency:** `DHDResolvePartition` is declared once (Task 2 Step 1) with defaulted `out_rest`/`out_explicit`, and called with two args in Step 7 and four in Step 4. `entryUrlFor` is declared in Task 1 Step 1, defined in Step 2, overridden in Task 2 Step 6 — same signature, `const std::string&` parameter, `std::string` return.
- **Known gap:** none of this is verifiable without hardware. Task 1 alone has a real automated gate (the native suite builds `d64.cpp`); Tasks 2-3 have only a compile gate. Do not claim otherwise.
