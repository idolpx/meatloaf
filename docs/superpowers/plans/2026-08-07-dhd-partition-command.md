# `partition` Console Command Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `partition` console command for switching CMD HD/FD partitions, remove the
path-based partition selection that corrupts directory listings, and make partition 255 reachable.

**Architecture:** CMD partition selection is *modal* — a per-image "selected partition" held in
`DHDImageRegistry`, which `DHDCreatePartitionFile()` and `getDecodedStream()` read to pick the MFile
subclass and the `DHDOffsetStream` window. That mechanism is correct and unchanged. This plan
removes the implicit way of changing modes (a partition name appearing in a path) and adds an
explicit one (the console command), leaving `CP<n>` as the C64-side equivalent.

**Tech Stack:** C++17, ESP-IDF via PlatformIO, `esp_console`-based console (`ConsoleCommand`
factories in `lib/console/Commands/`).

**Spec:** `docs/superpowers/specs/2026-08-07-dhd-partition-command-design.md`

## Global Constraints

- **Partition numbering:** a CMD HD image has partitions **1 to 255**; **0 is the reserved system
  partition** carrying the partition table. CMD FD has 1 to 31 — unchanged by this plan.
- **Never `atoi`/`std::stoi` on C64- or network-sourced input.** Use `strtol`, verify
  `end != start && *end == '\0'`, and range-check *before* any narrowing cast. ESP-IDF builds with
  `-fno-exceptions`, so `std::stoi` becomes `std::terminate`; `atoi` silently truncates.
- **Partition names are PETSCII.** Convert with `mstr::toUTF8()` before displaying or comparing.
  `Image::byName()` already does this internally and its comparison is **case-sensitive** with
  `*`/`?` wildcard support — the name to type is the lowercase form the listing prints.
- **Build command:** `~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro`. `pio` is not on
  PATH. Never pipe it through `tail`/`head` — redirect to a file and grep that.
- **`platformio.ini` may not parse** if `flash_size` is commented out in `[meatloaf]` while line 91
  interpolates it. Work around with `export PLATFORMIO_DATA_DIR=data/BUILD_IEC.16m` rather than
  editing the user's config.
- **Tooling:** use `mcp__semble__search` to locate code and `mcp__serena__*` for symbol-level reads
  and edits. Grep only for exhaustive literal sweeps.

## Testing reality — read before starting

**There is no automated test harness that covers this code.** The native suite
(`pio test -e native -f native/test_disk_write`) compiles only `d64.cpp`, `meat_media.cpp`,
`string_utils.cpp`, `punycode.cpp` and `U8Char.cpp` — it does **not** build `dhd.h`, `dhd.cpp`, or
anything under `lib/console`. `MFSOwner::File()` is an `abort()` stub there, so DHD code cannot even
run natively.

So this plan does not fake a TDD cycle. Each task instead has:

1. A **ground-truth step** — establish what the on-disk image actually contains with a host-side
   Python script, before trusting any device output. This is how the `settings.ihf` question was
   settled, and it is the only real verification available for the parser change.
2. A **compile gate**.
3. **Exact hardware steps with expected output.**

The native suite must still be run once at the end to prove nothing regressed (70 passed / 5
skipped is the baseline).

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `lib/meatloaf/media/hd/dhd.cpp` | Partition table parser | `maxpart` 254 → 255; fix comment |
| `lib/device/iec/drive.cpp` | `CP<n>` DOS command | Upper bound 254 → 255 |
| `lib/console/Commands/VFSCommands.cpp` | Console commands | Add `partition` + factory |
| `lib/console/Commands/VFSCommands.h` | Command factory decls | Add `getPartitionCommand()` |
| `lib/console/Console.cpp` | Command registration | Register the new command |
| `lib/meatloaf/media/hd/dhd.h` | DHD MFile wrapper | Remove selection from `normalizePath()`; fix header comment |
| `AGENTS.md`, `lib/meatloaf/AGENTS.md` | Docs | Correct partition count and selection behavior |

**Task order matters.** Task 2 adds the replacement mechanism *before* Task 3 removes the old one,
so the tree is never in a state where partitions cannot be switched at all.

---

### Task 1: Make partition 255 reachable

Two places encode the same off-by-one. `parse()` loops `i <= maxpart` with entry 0 consumed as the
system partition, so `maxpart = 254` yields partitions 1..254 and never reads table entry 255.
`changePartition()` then rejects `CP255` anyway.

**Files:**
- Modify: `lib/meatloaf/media/hd/dhd.cpp:139` (and the comment at ~178-180)
- Modify: `lib/device/iec/drive.cpp:2358`

**Interfaces:**
- Consumes: nothing.
- Produces: nothing new. `DHDImageRegistry::Image::parts` may now contain a partition numbered 255;
  `DHDImageRegistry::select(url, 255)` and `iecDrive::changePartition(255)` become reachable.

- [ ] **Step 1: Establish ground truth — does the test image even have high partitions?**

Write and run this. It reads the partition table directly, independent of any firmware code.

```bash
python - <<'PY'
import sys, glob
# Point this at the image under test. Adjust the path if hdbackup.dhd lives elsewhere.
cands = glob.glob('.archive/**/*.dhd', recursive=True) + glob.glob('.archive/**/*.d[124]m', recursive=True)
print("candidate images:", cands)
for path in cands:
    img = open(path, 'rb').read()
    # CMD HD: system partition on a 64 KiB boundary, magic at +0x5F0
    sys_base = None
    for base in range(0, len(img) - 0x600, 65536):
        if img[base+0x500+0xF0: base+0x500+0xF0+6] == b'CMD HD':
            sys_base = base
            break
    if sys_base is None:
        print(f"{path}: no CMD HD system partition"); continue
    table = sys_base + 65536
    print(f"\n{path}: sys_base={sys_base} table={table}")
    for i in range(0, 256):
        off = table + i*32
        if off + 32 > len(img): break
        e = img[off:off+32]
        t = e[2]
        name = e[5:21].split(b'\xa0')[0].decode('latin-1').rstrip()
        if i == 0:
            print(f"  entry 0 (system): {name!r}")
        elif 1 <= t <= 4:
            print(f"  partition {i}: type={t} name={name!r}")
    print("  ^ note the HIGHEST partition number above")
PY
```

Expected: a list of partitions. **Record the highest number.** If it is ≤ 254, this image cannot
demonstrate the fix on hardware, and Step 6's hardware check is limited to confirming `CP255`
returns "no such partition" rather than a syntax error. Note that in the commit message.

- [ ] **Step 2: Widen the parser bound**

In `lib/meatloaf/media/hd/dhd.cpp`, change line 139:

```cpp
    uint16_t maxpart = 254;
```

to:

```cpp
    // Partitions are numbered 1-255; entry 0 is the reserved system partition
    // (it supplies disk_label below). The loop runs i <= maxpart, so 255 here
    // yields partitions 1..255. Both this and the loop counter must stay
    // uint16_t - as uint8_t, "i <= 255" would never be false.
    uint16_t maxpart = 255;
```

Then fix the stale comment above the loop (~line 178). Change:

```cpp
    // Partition table on track 1 of the system partition: 32-byte entries,
    // 8 per 256-byte sector, laid out contiguously. Entry 0 is the system
    // partition itself.
```

to:

```cpp
    // Partition table on track 1 of the system partition: 32-byte entries,
    // 8 per 256-byte sector, laid out contiguously. Entry 0 is the system
    // partition itself; entries 1..255 are the selectable partitions (CMD FD
    // caps at 31).
```

- [ ] **Step 3: Fix the `CP<n>` upper bound**

In `lib/device/iec/drive.cpp`, in `iecDrive::changePartition()`, change:

```cpp
    if (container.empty() || pnum < 1 || pnum > 254)
```

to:

```cpp
    // Valid partitions are 1-255; 0 is the reserved system partition.
    if (container.empty() || pnum < 1 || pnum > 255)
```

- [ ] **Step 4: Compile**

```bash
export PLATFORMIO_DATA_DIR=data/BUILD_IEC.16m
~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/build1.log 2>&1; echo "EXIT=$?"
grep -iE "error:" /tmp/build1.log | head
tail -5 /tmp/build1.log
```

Expected: `EXIT=0`, no `error:` lines, `SUCCESS`.

- [ ] **Step 5: Verify the loop still terminates**

This is the trap the comment warns about. Confirm by inspection that both the loop counter and
`maxpart` are `uint16_t`:

```bash
grep -n "uint16_t maxpart\|for (uint16_t i = 0; i <= maxpart" lib/meatloaf/media/hd/dhd.cpp
```

Expected: two lines, both `uint16_t`. If either is `uint8_t`, the firmware will hang on the first
DHD access — stop and fix before flashing.

- [ ] **Step 6: Hardware check**

Flash, then from the C64 or the drive command channel:

- `CP255` → previously `31,SYNTAX ERROR`. Now: `02,PARTITION SELECTED` if partition 255 exists in
  the image, or `77,SELECTED PARTITION ILLEGAL` if it does not. **Either is a pass** — the point is
  that it is no longer a syntax error.
- `CP31` → still selects, unchanged.

- [ ] **Step 7: Commit**

```bash
git add lib/meatloaf/media/hd/dhd.cpp lib/device/iec/drive.cpp
git commit -m "fix: make CMD HD partition 255 reachable

Partitions are numbered 1-255 with 0 reserved as the system partition.
parse() capped maxpart at 254 and loops i <= maxpart with entry 0
consumed as the system partition, so table entry 255 was never read.
changePartition() rejected pnum > 254, so CP255 was a syntax error even
had it been parsed.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: Add the `partition` console command

**Files:**
- Modify: `lib/console/Commands/VFSCommands.cpp` (add include, `partition_type_name()`,
  `partition()`, `getPartitionCommand()`)
- Modify: `lib/console/Commands/VFSCommands.h` (declare `getPartitionCommand()`)
- Modify: `lib/console/Console.cpp` (register in `registerVFSCommands()`)

**Interfaces:**
- Consumes from Task 1: partitions 1..255 may appear in `Image::parts`.
- Produces: console command `partition`; free function
  `const ConsoleCommand getPartitionCommand()` in namespace `ESP32Console::Commands`.

- [ ] **Step 1: Add the include**

At the top of `lib/console/Commands/VFSCommands.cpp`, beside the existing
`#include "../../meatloaf/network/http.h"` (line 17), add:

```cpp
#include "../../meatloaf/media/hd/dhd.h"
```

- [ ] **Step 2: Add the command implementation**

Place this immediately after the `ls()` function in `lib/console/Commands/VFSCommands.cpp` (it ends
with `return EXIT_SUCCESS; }` around line 275, just before `int mv(...)`):

```cpp
static const char *partition_type_name(uint8_t type)
{
    switch (type)
    {
        case 1:  return "NAT";
        case 2:  return "1541";
        case 3:  return "1571";
        case 4:  return "1581";
        default: return "?";
    }
}

// Switch the selected partition of the CMD HD/FD image the console is inside.
// The selection belongs to the IMAGE (DHDImageRegistry), not to any one MFile,
// which is why this takes no image argument: it always acts on the container
// in the current path. This is the console's equivalent of the drive's CP<n>.
int partition(int argc, char **argv)
{
    std::string container = DHDImageRegistry::containerOf(getCurrentPath()->url);
    if (container.empty())
    {
        Serial.printf("partition: not inside a CMD HD/FD image\r\n");
        return EXIT_FAILURE;
    }

    auto img = DHDImageRegistry::obtain(container);
    if (img == nullptr || !img->valid)
    {
        Serial.printf("partition: cannot read the partition table of '%s'\r\n", container.c_str());
        return EXIT_FAILURE;
    }

    // No argument: list. Names are PETSCII on disk, so print the UTF-8 form -
    // that is both what `ls` shows and what byName() matches against.
    if (argc < 2)
    {
        Serial.printf("  #  type   name\r\n");
        for (const auto &p : img->parts)
        {
            std::string name = mstr::toUTF8(p.name);
            Serial.printf("%c%3u  %-5s  \"%s\"\r\n",
                          (p.number == img->selected) ? '*' : ' ',
                          (unsigned)p.number,
                          partition_type_name(p.type),
                          name.c_str());
        }
        return EXIT_SUCCESS;
    }

    std::string arg = argv[1];
    const DHDPartition *p = nullptr;

    bool numeric = arg.size() && arg.find_first_not_of("0123456789") == std::string::npos;
    if (numeric)
    {
        // Valid partitions are 1-255; 0 is the reserved system partition.
        // Range-check BEFORE narrowing to uint8_t: an int silently truncated
        // into byNumber() is exactly what made "1571" resolve to partition 35.
        char *end = nullptr;
        long v = strtol(arg.c_str(), &end, 10);
        if (end != arg.c_str() && *end == '\0' && v >= 1 && v <= 255)
            p = img->byNumber((uint8_t)v);
    }
    else
    {
        // Case-sensitive, and accepts * / ? wildcards.
        p = img->byName(arg);
    }

    if (p == nullptr)
    {
        Serial.printf("partition: no such partition: %s\r\n", arg.c_str());
        return EXIT_FAILURE;
    }

    // Copy what we need before select() - it disposes cached streams, and we
    // do not want to rely on 'p' outliving that.
    uint8_t number = p->number;
    uint8_t type = p->type;
    std::string name = mstr::toUTF8(p->name);

    if (!DHDImageRegistry::select(container, number))
    {
        Serial.printf("partition: could not select partition %u\r\n", (unsigned)number);
        return EXIT_FAILURE;
    }

    // The old cwd may name a subdirectory that existed only in the previous
    // partition, so drop back to the image root.
    setCurrentPath(MFSOwner::File(container));

    Serial.printf("Selected partition %u \"%s\" (%s)\r\n",
                  (unsigned)number, name.c_str(), partition_type_name(type));
    return EXIT_SUCCESS;
}
```

- [ ] **Step 3: Add the command factory**

In the same file, in the `namespace ESP32Console::Commands` block, immediately after
`getLsCommand()` (around line 2000):

```cpp
    const ConsoleCommand getPartitionCommand()
    {
        return ConsoleCommand("partition", &partition,
            "List or switch CMD HD/FD partitions. Usage: partition [number|name]");
    }
```

- [ ] **Step 4: Declare it in the header**

In `lib/console/Commands/VFSCommands.h`, after the `getDFCommand()` declaration:

```cpp
    const ConsoleCommand getPartitionCommand();
```

- [ ] **Step 5: Register it**

In `lib/console/Console.cpp`, in `registerVFSCommands()`, after `registerCommand(getLsCommand());`:

```cpp
        registerCommand(getPartitionCommand());
```

No `#ifdef` guard: `dhdFS` and `dxmFS` are registered outside the `MIN_CONFIG` guards in
`meatloaf.cpp`, so DHD is always available.

- [ ] **Step 6: Compile**

```bash
export PLATFORMIO_DATA_DIR=data/BUILD_IEC.16m
~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/build2.log 2>&1; echo "EXIT=$?"
grep -iE "error:" /tmp/build2.log | head
tail -5 /tmp/build2.log
```

Expected: `EXIT=0`, `SUCCESS`. If `mstr::toUTF8` is unresolved, confirm `string_utils.h` is reachable
— it is already used by `ls()` in this file, so it should be.

- [ ] **Step 7: Hardware check**

Flash, then:

```
cd /sd/content/disk/dhd/hdbackup.dhd
partition
```

Expected: a table, with exactly one line marked `*`, names in lowercase matching `ls` style:

```
  #  type   name
   1  1541   "gw boot hd"
* 31  NAT    "bible"
  35  1581   "gw boot hd"
```

Then:

- `partition 31` → `Selected partition 31 "bible" (NAT)`, and `pwd` shows the image root.
- `partition 0` → `partition: no such partition: 0`
- `partition 1571` → `partition: no such partition: 1571` (**not** a silent switch to 35)
- `partition bible` → selects by name
- `partition bib*` → selects by wildcard
- `cd /` then `partition` → `partition: not inside a CMD HD/FD image`

- [ ] **Step 8: Commit**

```bash
git add lib/console/Commands/VFSCommands.cpp lib/console/Commands/VFSCommands.h lib/console/Console.cpp
git commit -m "feat: add 'partition' console command for CMD HD/FD images

Lists partitions with the selected one marked, and switches by number
(1-255, range-checked before the uint8_t cast) or by name/wildcard.
Acts on the CMD image in the current path; resets cwd to the image root
after switching, since the old cwd may name a subdirectory that existed
only in the previous partition.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Remove path-based partition selection

This is the fix for the reported bug. Selecting a partition strips its name from the path, so entry
URLs built during a listing carry no partition component and `normalizePath()` re-reads the first
component as a partition reference — switching partitions mid-listing.

**Files:**
- Modify: `lib/meatloaf/media/hd/dhd.h` — `DHDPartitionMFile::normalizePath()` (lines 263-312) and
  the file header comment (line ~32)

**Interfaces:**
- Consumes from Task 2: the `partition` command, which is now the console's only way to switch.
- Produces: `pathInStream` is always relative to the selected partition. `listing_partitions` and
  the `$=P` handling are unchanged.

- [ ] **Step 1: Replace the method body**

Use `mcp__serena__replace_symbol_body` with `name_path` `DHDPartitionMFile/normalizePath` and
`relative_path` `lib/meatloaf/media/hd/dhd.h`. New body:

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

        // A partition name or number in a path does NOT select a partition.
        // The real CMD HD requires CP<n>; resolving paths here was a Meatloaf
        // invention and an actively harmful one. Selecting strips the
        // partition from the path, so the entry URLs getNextFileInDir()
        // builds during a listing carry no partition component - and a file
        // whose name happened to match a partition (e.g. "1571" inside BIBLE)
        // re-entered this function, switched the image to another partition
        // part-way through the listing, and disposed the cached stream out
        // from under it. Use CP<n> or the "partition" console command.
        //
        // pathInStream is therefore always relative to the selected
        // partition, and nothing a listing generates can be reinterpreted.
    }
```

- [ ] **Step 2: Fix the file header comment**

In the same file, the header comment (around line 30-33) reads:

```
// Each image has a "currently selected partition" (the default partition on
// first use, like the real drive). getFile() returns a D64MFile, D71MFile,
// D81MFile or DNPMFile matched to the selected partition's type, decoding a
// window of the image at the partition's offset. The selection changes via
// the CBM DOS "CP<n>" command, or by loading / CD'ing a partition name or
// number. LOAD"$=P",8 lists the partitions.
```

Replace the last two sentences so it reads:

```
// Each image has a "currently selected partition" (the default partition on
// first use, like the real drive). getFile() returns a D64MFile, D71MFile,
// D81MFile or DNPMFile matched to the selected partition's type, decoding a
// window of the image at the partition's offset. The selection changes ONLY
// via the CBM DOS "CP<n>" command or the "partition" console command - the
// real CMD HD does not switch partitions on LOAD or CD, and neither do we.
// LOAD"$=P",8 lists the partitions. Partitions are numbered 1-255 (CMD FD:
// 1-31); 0 is the reserved system partition.
```

- [ ] **Step 3: Confirm nothing else depended on the removed behavior**

```bash
grep -rn "normalizePath" lib/
```

Expected: only definitions and calls within `dhd.h`. If any other file calls it expecting the
selection side effect, stop and report.

- [ ] **Step 4: Compile**

```bash
export PLATFORMIO_DATA_DIR=data/BUILD_IEC.16m
~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/build3.log 2>&1; echo "EXIT=$?"
grep -iE "error:|warning: unused" /tmp/build3.log | head
tail -5 /tmp/build3.log
```

Expected: `EXIT=0`, `SUCCESS`. An "unused variable" warning here means leftover code from the
removed branch — delete it.

- [ ] **Step 5: Hardware check — the actual bug**

```
cd /sd/content/disk/dhd/hdbackup.dhd
partition bible
ls
```

Expected: the **complete** BIBLE listing with **no `select()` line anywhere in the middle**, and no
`getSourceStream()`/`open()` re-open mid-listing. Every entry from `kjv-text 1a` through the end
belongs to BIBLE. Compare against the original bug report, where the listing switched to
`GW BOOT HD` right after `1581`.

Also confirm:
- `cd bible` → `cd: not a directory: bible`
- `LOAD"$=P",8` from the C64 still lists partitions
- `CP31` from the C64 still selects

- [ ] **Step 6: Run the native suite for regressions**

```bash
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_disk_write > /tmp/native.log 2>&1
echo "EXIT=$?"; tail -6 /tmp/native.log
```

Expected: `75 test cases: 5 skipped, 70 succeeded` — the documented baseline. This suite does not
build `dhd.h`, so it is a regression check on the shared `d64.cpp` engine, not a test of this change.

- [ ] **Step 7: Commit**

```bash
git add lib/meatloaf/media/hd/dhd.h
git commit -m "fix: stop path components from switching CMD HD partitions

A partition name or number in a path selected that partition and was
stripped from the path, so entry URLs built during a directory listing
carried no partition component. A file named like a partition therefore
re-entered normalizePath() and switched the image mid-listing, disposing
the cached stream underneath the running listing.

The real CMD HD requires CP<n> to change partitions; path-based selection
was a Meatloaf invention. Removed, leaving CP<n> and the new 'partition'
console command.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: Correct the documentation

Six places state either the wrong partition count or the removed selection behavior as if it were
CMD behavior.

**Files:**
- Modify: `AGENTS.md` (CMD media images note, line ~534)
- Modify: `lib/meatloaf/AGENTS.md` (July 13-15 DHD entry line ~535; August 7 entry)
- Modify: `lib/meatloaf/media/hd/dhd.cpp` (partition-count comment, if any remains)

**Interfaces:**
- Consumes: the behavior established by Tasks 1-3.
- Produces: documentation only.

- [ ] **Step 1: Find every stale claim**

This is an exhaustive literal sweep, so Grep is the right tool:

```bash
grep -rn "254 partitions\|CD/LOAD of a partition\|CD'ing a partition" --include=*.md --include=*.h --include=*.cpp .
```

Record the list. Every hit must be resolved by Step 4.

- [ ] **Step 2: Fix the root `AGENTS.md`**

In the **CMD media images (DHD, D1M/D2M/D4M)** bullet, replace:

> HD images: system partition scanned on 64 KiB boundaries ("CMD HD" boot magic), table at sys+65536, 254 partitions.

with:

> HD images: system partition scanned on 64 KiB boundaries ("CMD HD" boot magic), table at sys+65536, partitions numbered 1-255 (entry 0 is the reserved system partition, which supplies the disk label).

and replace:

> `CP<n>` changes partitions (status `02,PARTITION SELECTED`), `LOAD"$=P"` lists them, and CD/LOAD of a partition name or number selects it.

with:

> `CP<n>` (drive) and the `partition` console command are the ONLY ways to change partitions — matching the real CMD HD, which does not switch on LOAD or CD. `LOAD"$=P"` lists them. A partition name or number appearing in a path is NOT a selection: it used to be, and because selecting strips the partition from the path, a file whose name matched a partition switched the image part-way through a directory listing.

- [ ] **Step 3: Fix `lib/meatloaf/AGENTS.md`**

In the July 13-15 DHD entry, replace:

> `LOAD"$=P"` lists partitions; CD/LOAD of a partition name/number selects it; `CP<n>` (drive command) selects by number.

with:

> `LOAD"$=P"` lists partitions; `CP<n>` (drive) and the `partition` console command select — path-based selection was removed on 2026-08-07, see that entry.

Then rewrite the August 7 DHD entry's final "**Residual, not fixed**" sentence, since this plan
fixes it. Replace that sentence with:

> **Resolved 2026-08-07** by removing path-based selection entirely (`partition` console command + `CP<n>` are now the only ways to switch), which eliminates the ambiguity by construction rather than by validating the parse. The `strtol` guard described above was deleted along with the branch it guarded.

- [ ] **Step 4: Verify no stale claims remain**

```bash
grep -rn "254 partitions\|CD/LOAD of a partition\|CD'ing a partition" --include=*.md --include=*.h --include=*.cpp .
```

Expected: no output. If a hit remains, fix it.

- [ ] **Step 5: Add the changelog entry**

At the top of the `## Recent Changes (August 7, 2026)` section in the root `AGENTS.md`:

```markdown
- **`partition` console command; path-based partition selection removed** (`lib/console/Commands/VFSCommands.cpp/.h`, `lib/console/Console.cpp`, `lib/meatloaf/media/hd/dhd.h/.cpp`, `lib/device/iec/drive.cpp`): `partition` lists a CMD HD/FD image's partitions (selected one marked) and switches by number or name/wildcard, resetting the console cwd to the image root. `DHDPartitionMFile::normalizePath()` no longer treats a leading path component as a partition reference — the real CMD HD requires `CP<n>`, and the implicit form switched partitions part-way through a directory listing because selecting strips the partition from the path, leaving listing-generated entry URLs ambiguous with partition references. Also fixed an off-by-one that made partition 255 unreachable: `parse()` capped `maxpart` at 254 (the loop runs `i <= maxpart` with entry 0 consumed as the system partition) and `changePartition()` rejected `pnum > 254`. Partitions are 1-255; 0 is the reserved system partition. Both `maxpart` and the parse loop counter must stay `uint16_t` — as `uint8_t`, `i <= 255` is never false.
```

- [ ] **Step 6: Commit**

```bash
git add AGENTS.md lib/meatloaf/AGENTS.md lib/meatloaf/media/hd/dhd.cpp
git commit -m "docs: correct CMD HD partition count and selection behavior

Partitions are 1-255 with 0 reserved, not 254. CP<n> and the new
'partition' console command are the only ways to switch; the previously
documented CD/LOAD selection was a Meatloaf invention, not CMD behavior,
and has been removed.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Self-review notes

- **Spec coverage:** every spec section maps to a task — "Remove path-based selection" → Task 3,
  "The command" → Task 2, "Partition 255 is unreachable" → Task 1, "Documentation to correct" →
  Task 4, "Testing" → distributed across each task's hardware steps plus Task 3 Step 6.
- **Type consistency:** `getPartitionCommand()` is declared in `VFSCommands.h` (Task 2 Step 4),
  defined in `VFSCommands.cpp` (Step 3), and called in `Console.cpp` (Step 5) under that exact
  name. `partition_type_name()` is defined once and used in both the listing and the success
  message. `partition()` matches the `int(int, char**)` signature `ConsoleCommand` expects, as
  `ls`/`cd` do.
- **Known gap:** partition 255 may not be verifiable on hardware if the test image has no such
  partition. Task 1 Step 1 determines this *before* the hardware step and Step 6 states what
  constitutes a pass either way. Do not claim the 255 fix is hardware-verified if the image
  cannot exercise it.
