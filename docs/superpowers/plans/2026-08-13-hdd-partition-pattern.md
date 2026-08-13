# IDE64 CFS Partition Pattern Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `.hdd` (IDE64 CFS) images the same partition model `.dhd` (CMD HD/FD) already has — a persistent per-image selection, `CP<n>`, the `partition` console command, `LOAD"$=P"`, and paths that name a partition without changing the selection.

**Architecture:** A new `HDDImageRegistry` in `lib/meatloaf/media/hd/hdd.h/.cpp` mirrors `DHDImageRegistry` but drops the parts CFS does not need. `HDDMFile` gains the `DHDPartitionMFile` behaviours directly (CFS has one MFile type, not four). A new `lib/meatloaf/media/hd/partition_select.h/.cpp` gives `iecDrive::changePartition()` and the `partition` console command one format-agnostic surface, so neither grows a second copy of the listing loop or the range check.

**Tech Stack:** C++11/14, ESP-IDF via PlatformIO, Unity for native tests.

**Spec:** `docs/superpowers/specs/2026-08-13-hdd-partition-pattern-design.md`. Read it before Task 1 — particularly the section "Three divergences from DHD", which is what makes copying the DHD code verbatim wrong.

## Global Constraints

- **Partitions are numbered from 1, counting only VALID table entries.** An invalid slot is skipped rather than consuming a number. This is already what `HDDMStream::seekPartitionEntry()` does, and the registry must agree with it entry for entry.
- **Two numbering spaces, never to be confused.** The **slot index** 0-15 is how the partition directory is physically laid out and what the boot sector's DP byte holds. The **partition number** 1-N is what paths, `CP<n>`, `$=P` and the `partition` command speak. `parse()` converts DP from the first space to the second exactly once; nothing downstream sees a slot index.
- **Partition 0 behaves exactly as in DHD.** `0` in a path means "the currently selected partition" — copy `DHDResolvePartition()`'s `v == 0` case verbatim. `select()` refuses 0, so `CP0` is a syntax error.
- **Never call `select()` while resolving a path.** Only `CP<n>` and the `partition` console command change the selection.
- **Never use `atoi`/`std::stoi`** on C64- or network-sourced input. Use `strtol`, check `end != start && *end == '\0'`, and range-check **before** narrowing to `uint8_t`. An unchecked `int` truncated into `byNumber()` is the bug that once made `1571` resolve to partition 35 in DHD.
- **A partition is selectable only when `type == 1` (CFS).** Unformatted (0), GEOS (2) and reserved (3-11) are listed, never selected.
- **`lib/meatloaf` must stay natively compilable.** No `device/iec` includes, and nothing on a path the native suite reaches may call `MFSOwner::File()` or `MFile::getSourceStream()` — both `abort()` in `test/native/test_disk_write/native_stubs.cpp`.
- **Debug output uses `Debug_printv`**, never `console.printf()`, inside I/O paths.

### Deviation from the spec, decided during planning

The spec left one item open: *"To confirm during planning: that the container URL is recoverable from the stream's own `url`… If it is not, the selection is passed in by `HDDMFile` instead."*

**It is not usable, so take the stated fallback.** `FileContainerStream` sets `MStream::url` to the on-disk path, which ends in `.hdd`, so a registry lookup performed from inside `HDDMStream` would call `MFSOwner::File()` and `abort()` — breaking the four currently-green tests in `test/native/test_hdd_read`. `HDDMStream` therefore gains a plain `selected_partition` member that `HDDMFile` sets. This is also better layering: the stream is about CFS bytes, the registry is about selection policy.

Update the spec's `HDDMStream` section to record this once Task 4 is done (Task 7 covers it).

### Baseline

`pio test -e native -f native/test_hdd_read` passes with 4 tests in ~5 s as of this plan. Confirm that before starting.

`pio` is not on PATH on this machine — use `~/.platformio/penv/Scripts/pio.exe`. Never pipe it through `tail`/`head`; redirect to a file and grep, or the error text is lost. If the native build fails with "Access is denied" on `program.exe`, a previous test binary is still running: `Get-Process program | Stop-Process -Force`.

### Sample corpus (`.archive/` is gitignored — tests skip when absent)

| Image | Label | DP | Partitions |
|---|---|---|---|
| `.archive/hdd/ide20201227.hdd` | `SOCI/SINGULAR` | slot 0 | partition **1** `STUFF`, type 1, start 2, end 16383, root LBA **5** |
| `.archive/hdd/c64os v1.09-clean.hdd` | `C64 OS` | slot 0 | partition **1** `C64 OS`, type 1, root LBA **5**; partition **2** `DISK IMAGES`, type 1, root LBA **32773** |

The C64 OS image is the only one with two partitions, so it carries every selection test.

**Neither image can validate the numbering rule**, because both fill slots contiguously from 0, so "slot + 1" and "1-based over valid entries" produce identical numbers. The rule rests on `seekPartitionEntry()`'s existing behaviour; do not weaken it on the grounds that no test distinguishes it.

## File Structure

| File | Responsibility |
|---|---|
| `lib/meatloaf/media/hd/hdd.h` | Modify — `BootSector` fix; `HDDPartition`; `HDDImageRegistry`; resolve declarations; `HDDMStream::selected_partition`; `HDDMFile` partition members; `HDDMFileSystem::handles()` probing guard |
| `lib/meatloaf/media/hd/hdd.cpp` | Modify — registry parse/obtain/select, resolve, stream root semantics, MFile `$=P` and entry URLs |
| `lib/meatloaf/media/hd/partition_select.h` | Create — format-agnostic partition surface shared by the drive and the console |
| `lib/meatloaf/media/hd/partition_select.cpp` | Create — dispatch between `DHDImageRegistry` and `HDDImageRegistry` |
| `lib/device/iec/drive.cpp` | Modify — `changePartition()` routes through `partition_select.h` |
| `lib/console/Commands/VFSCommands.cpp` | Modify — `partition` command routes through `partition_select.h` |
| `test/native/test_hdd_read/test_hdd_read.cpp` | Modify — add boot-sector and registry tests |
| `test/native/test_hdd_read/engine_sources.cpp` | Modify — no change expected; `hdd.cpp` is already included |
| `AGENTS.md`, `lib/meatloaf/AGENTS.md` | Modify — durable rules |

---

## Task 1: Fix the BootSector field offsets

A currently-shipping bug, independent of the feature. It lands first and alone.

**Files:**
- Modify: `lib/meatloaf/media/hd/hdd.h:95-104`
- Test: `test/native/test_hdd_read/test_hdd_read.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `HDDMStream::BootSector` with `default_partition` at offset 3 and `last_sector` at offset 4. Task 2 reads `boot_sector.default_partition`; Task 4 reads it as the selection fallback.

- [ ] **Step 1: Write the failing tests**

Add to `test/native/test_hdd_read/test_hdd_read.cpp`, after the existing tests and before `main()`. Add `#include <cstddef>` to the include block at the top of the file for `offsetof`.

Also extend the `TestHDDStream` helper class near the top of the file so the tests can reach the parsed boot sector — add `using HDDMStream::boot_sector;` to its list of `using` declarations.

```cpp
// The CFS 0.11 spec's boot sector table is colspan-encoded:
//   <TH>$0000<TD COLSPAN=3>Unused<TD>DP<TD COLSPAN=4>@Last disk sector
// so "Unused" spans $00-$02, DP is $03, and @Last disk sector is $04-$07.
// The struct had DP at $01 and @Last disk sector at $02-$05.
void test_boot_sector_field_offsets(void)
{
    TEST_ASSERT_EQUAL_UINT32(3, offsetof(HDDMStream::BootSector, default_partition));
    TEST_ASSERT_EQUAL_UINT32(4, offsetof(HDDMStream::BootSector, last_sector));
    TEST_ASSERT_EQUAL_UINT32(8, offsetof(HDDMStream::BootSector, id));
    TEST_ASSERT_EQUAL_UINT32(0x18, offsetof(HDDMStream::BootSector, part_dir));
    TEST_ASSERT_EQUAL_UINT32(0x1C, offsetof(HDDMStream::BootSector, part_dir_backup));
    TEST_ASSERT_EQUAL_UINT32(0x20, offsetof(HDDMStream::BootSector, disk_label));
}

// Corpus invariant that pins @Last disk sector to $04 independently of the
// spec: the partition directory BACKUP sits on the last sector of the disk,
// so @Last disk sector is always @Partition directory backup + 1. With the
// old $02-$05 placement the pointer decodes to garbage and this fails.
void test_last_sector_is_one_past_partition_dir_backup(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());
    TEST_ASSERT_TRUE(image->readHeader());

    uint32_t last   = image->boot_sector.last_sector.getLBA();
    uint32_t backup = image->boot_sector.part_dir_backup.getLBA();

    TEST_ASSERT_TRUE_MESSAGE(image->boot_sector.last_sector.isLBA(),
        "@Last disk sector does not have the LBA bit set - wrong offset");
    TEST_ASSERT_EQUAL_UINT32(backup + 1, last);
}

// DP must come from byte $03. Read it straight out of the file so the test
// does not depend on the struct being right.
void test_default_partition_reads_byte_3(void)
{
    FILE* fp = fopen(IMAGE_PATH, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    uint8_t raw[8] = {0};
    size_t n = fread(raw, 1, sizeof(raw), fp);
    fclose(fp);
    TEST_ASSERT_EQUAL_UINT32(sizeof(raw), n);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());
    TEST_ASSERT_TRUE(image->readHeader());

    TEST_ASSERT_EQUAL_UINT8(raw[3], image->boot_sector.default_partition);
}
```

Register them in `main()`, after the four existing `RUN_TEST` lines:

```cpp
    RUN_TEST(test_boot_sector_field_offsets);
    RUN_TEST(test_last_sector_is_one_past_partition_dir_backup);
    RUN_TEST(test_default_partition_reads_byte_3);
```

- [ ] **Step 2: Run the tests and verify they fail**

```bash
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_hdd_read > /tmp/t.log 2>&1; echo "exit=$?"; grep -E "PASSED|FAILED|IGNORE" /tmp/t.log
```

Expected: `test_boot_sector_field_offsets` FAILS (offset is 1, expected 3) and `test_last_sector_is_one_past_partition_dir_backup` FAILS. `test_default_partition_reads_byte_3` may pass by accident — every corpus image has `$00-$03 = 00 00 00 00`, so both bytes read 0. That is expected and is exactly why this bug stayed latent; leave the test in as a regression pin.

- [ ] **Step 3: Fix the struct**

In `lib/meatloaf/media/hd/hdd.h`, replace the `BootSector` struct body:

```cpp
    // Boot sector (sector 0). Offsets per the CFS 0.11 spec, whose table is
    // colspan-encoded: "Unused" spans $00-$02, DP is $03, and @Last disk
    // sector spans $04-$07. Confirmed against every image in .archive/hdd/,
    // where @Last disk sector is @Partition directory backup + 1.
    struct BootSector {
        uint8_t reserved0[3];       // $00-$02: unused
        uint8_t default_partition;  // $03: DP (0-15)
        Pointer last_sector;        // $04-$07
        char id[16];                // $08-$17: "C64 CFS V 0.11B "
        Pointer part_dir;           // $18-$1B: Partition directory pointer
        Pointer part_dir_backup;    // $1C-$1F: Backup location
        char disk_label[16];        // $20-$2F: Global disk label ($20 padded)
    } __attribute__((packed));
```

- [ ] **Step 4: Run the tests and verify they pass**

```bash
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_hdd_read > /tmp/t.log 2>&1; echo "exit=$?"; grep -E "PASSED|FAILED" /tmp/t.log
```

Expected: 7 tests, all PASSED. The four pre-existing tests must still pass.

- [ ] **Step 5: Commit**

```bash
git add lib/meatloaf/media/hd/hdd.h test/native/test_hdd_read/test_hdd_read.cpp
git commit -m "fix: read the CFS default partition from byte \$03, not \$01"
```

---

## Task 2: Registry data model and parsing

The natively-testable core: parse a CFS partition table out of an open stream, with no `MFSOwner` involvement.

**Files:**
- Modify: `lib/meatloaf/media/hd/hdd.h`, `lib/meatloaf/media/hd/hdd.cpp`
- Test: `test/native/test_hdd_read/test_hdd_read.cpp`

**Interfaces:**
- Consumes: `HDDMStream::BootSector` and `HDDMStream::PartitionEntry` (Task 1).
- Produces:
  - `struct HDDPartition { uint8_t number; uint8_t slot; uint8_t type; std::string name; uint32_t root_lba; uint32_t size; bool hidden; bool writeable; };` — `number` is 1-based over valid entries, `slot` is the raw table index 0-15.
  - `HDDImageRegistry::Image` with `bool valid`, `uint8_t default_part`, `uint8_t selected`, `std::string disk_label`, `std::vector<HDDPartition> parts`, and methods `const HDDPartition* byNumber(uint8_t) const`, `const HDDPartition* byName(std::string) const`, `const HDDPartition* current() const`, `bool trySelect(uint8_t)`.
  - `static bool HDDImageRegistry::parseInto(MStream* s, Image& img)`.

- [ ] **Step 1: Write the failing tests**

Add to `test/native/test_hdd_read/test_hdd_read.cpp`. Add a second image constant beside `IMAGE_PATH` at the top of the file:

```cpp
// The only corpus image with more than one partition, so every selection
// test uses it: slot 0 "C64 OS" (root LBA 5), slot 1 "DISK IMAGES" (32773).
static const char* MULTI_IMAGE_PATH = ".archive/hdd/c64os v1.09-clean.hdd";
```

and a helper beside `openImage()`:

```cpp
// Parses a partition table straight out of a file, bypassing the registry's
// MFSOwner-based open (MFSOwner::File aborts under the native stubs).
// Returns false when the image isn't present.
static bool parseImage(const char* path, HDDImageRegistry::Image& img)
{
    auto src = std::make_shared<FileContainerStream>(path);
    if (!src->isOpen())
        return false;
    return HDDImageRegistry::parseInto(src.get(), img);
}
```

Then the tests:

```cpp
void test_registry_parses_single_partition_image(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(IMAGE_PATH, img));

    TEST_ASSERT_TRUE(img.valid);
    TEST_ASSERT_EQUAL_STRING("SOCI/SINGULAR", img.disk_label.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, img.parts.size());
    TEST_ASSERT_EQUAL_UINT8(1, img.parts[0].number);        // numbered from 1
    TEST_ASSERT_EQUAL_UINT8(0, img.parts[0].slot);          // from table slot 0
    TEST_ASSERT_EQUAL_UINT8(1, img.parts[0].type);          // CFS
    TEST_ASSERT_EQUAL_STRING("STUFF", img.parts[0].name.c_str());
    TEST_ASSERT_EQUAL_UINT32(5, img.parts[0].root_lba);
    TEST_ASSERT_TRUE(img.parts[0].writeable);
    TEST_ASSERT_FALSE(img.parts[0].hidden);
}

void test_registry_numbers_partitions_from_one(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));

    TEST_ASSERT_EQUAL_STRING("C64 OS", img.disk_label.c_str());
    TEST_ASSERT_EQUAL_UINT32(2, img.parts.size());

    TEST_ASSERT_EQUAL_UINT8(1, img.parts[0].number);
    TEST_ASSERT_EQUAL_UINT8(0, img.parts[0].slot);
    TEST_ASSERT_EQUAL_STRING("C64 OS", img.parts[0].name.c_str());
    TEST_ASSERT_EQUAL_UINT32(5, img.parts[0].root_lba);

    TEST_ASSERT_EQUAL_UINT8(2, img.parts[1].number);
    TEST_ASSERT_EQUAL_UINT8(1, img.parts[1].slot);
    TEST_ASSERT_EQUAL_STRING("DISK IMAGES", img.parts[1].name.c_str());
    TEST_ASSERT_EQUAL_UINT32(32773, img.parts[1].root_lba);

    // No partition is ever numbered 0 - that number means "the currently
    // selected partition" and must never name a table entry.
    for (const auto& p : img.parts)
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, p.number, "partition 0 must not exist");
}

// DP is a SLOT index in the boot sector and must be converted into the
// partition-number space exactly once, at parse time. Slot 0 is DP here, and
// slot 0 is partition 1.
void test_registry_converts_default_partition_from_slot_to_number(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, img.default_part,
        "default_part must hold a partition NUMBER, not the raw DP slot index");
    TEST_ASSERT_EQUAL_UINT8(1, img.selected);
    TEST_ASSERT_NOT_NULL(img.current());
    TEST_ASSERT_EQUAL_STRING("C64 OS", img.current()->name.c_str());
}

void test_registry_lookup_by_number_and_name(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));

    TEST_ASSERT_NOT_NULL(img.byNumber(2));
    TEST_ASSERT_EQUAL_STRING("DISK IMAGES", img.byNumber(2)->name.c_str());
    TEST_ASSERT_NULL(img.byNumber(0));      // never a real partition
    TEST_ASSERT_NULL(img.byNumber(3));
    TEST_ASSERT_NULL(img.byNumber(15));

    TEST_ASSERT_NOT_NULL(img.byName("DISK IMAGES"));
    TEST_ASSERT_EQUAL_UINT8(2, img.byName("DISK IMAGES")->number);
    TEST_ASSERT_NOT_NULL(img.byName("DISK*"));          // wildcards honoured
    TEST_ASSERT_EQUAL_UINT8(2, img.byName("DISK*")->number);
    TEST_ASSERT_NULL(img.byName("NO SUCH PARTITION"));
}

void test_registry_try_select(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));

    TEST_ASSERT_TRUE(img.trySelect(2));
    TEST_ASSERT_EQUAL_UINT8(2, img.selected);

    TEST_ASSERT_TRUE(img.trySelect(1));
    TEST_ASSERT_EQUAL_UINT8(1, img.selected);

    // 0 means "the currently selected partition" and is never selectable, as
    // in DHD. A number past the end leaves the selection alone too.
    TEST_ASSERT_FALSE(img.trySelect(0));
    TEST_ASSERT_EQUAL_UINT8(1, img.selected);
    TEST_ASSERT_FALSE(img.trySelect(7));
    TEST_ASSERT_EQUAL_UINT8(1, img.selected);
}
```

Register all five in `main()` with `RUN_TEST`. Guard the two-partition tests so the suite still runs on a machine that has only the first image — add beside `imageAvailable()`:

```cpp
static bool multiImageAvailable()
{
    FILE* fp = fopen(MULTI_IMAGE_PATH, "rb");
    if (fp == nullptr)
        return false;
    fclose(fp);
    return true;
}
```

and wrap the four `MULTI_IMAGE_PATH` tests in `main()`:

```cpp
    RUN_TEST(test_registry_parses_single_partition_image);
    if (multiImageAvailable())
    {
        RUN_TEST(test_registry_numbers_partitions_from_one);
        RUN_TEST(test_registry_converts_default_partition_from_slot_to_number);
        RUN_TEST(test_registry_lookup_by_number_and_name);
        RUN_TEST(test_registry_try_select);
    }
```

- [ ] **Step 2: Run and verify the failure is a compile error**

```bash
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_hdd_read > /tmp/t.log 2>&1; echo "exit=$?"; grep -E "error:" /tmp/t.log | head
```

Expected: compile errors — `HDDImageRegistry` has not been declared.

- [ ] **Step 3: Declare the data model**

In `lib/meatloaf/media/hd/hdd.h`, add `#include <map>` and `#include <vector>` to the include block, then insert this **after** the `HDDMStream` class and **before** the "File implementations" comment banner (it needs `HDDMStream::PartitionEntry` in scope):

```cpp
/********************************************************
 * Partition registry
 ********************************************************/

// TWO numbering spaces, and they must not be confused:
//   number - 1-based, counting only VALID table entries. What paths, CP<n>,
//            "$=P" and the `partition` command all speak. Never 0: that
//            number means "the currently selected partition", as in DHD.
//   slot   - the raw 0-15 index into the 16-entry partition directory, and
//            what the boot sector's DP byte holds. Converted into `number`
//            once, at parse time; nothing downstream sees a slot.
struct HDDPartition {
    uint8_t     number;      // 1-based over valid entries
    uint8_t     slot;        // raw table index, 0-15
    uint8_t     type;        // 0=unformatted, 1=CFS, 2=GEOS, 3-11 reserved
    std::string name;        // ASCII, $00 padding trimmed
    uint32_t    root_lba;    // @Root directory (entry +$1C)
    uint32_t    size;        // bytes: (end - start + 1) * 512
    bool        hidden;      // @Start bit 5: excluded from a plain listing
    bool        writeable;   // @Start bit 4 (recorded; CFS support is read-only)
};

// Per-image partition table and selection state, keyed by container URL.
//
// Deliberately SMALLER than DHDImageRegistry: there is no cached_part, no
// brokerUrl() and no dispose-on-select. DHD needs those because ImageBroker
// caches one DECODED D64/D71/D81/DNP stream per image and cannot tell
// partitions apart. HDDMStream re-derives its whole position from
// seekDirectory(pathInStream) on every operation, so a cached stream holds no
// partition identity that could go stale.
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

        // Selects a partition if it exists and is CFS. Leaves the selection
        // untouched and returns false otherwise - including for 0, which
        // means "the currently selected partition" and is never a real one.
        bool trySelect(uint8_t number);
    };

    // Parses a boot sector + partition directory from an ALREADY OPEN stream.
    // Split out from parse() so the registry is testable natively, where
    // MFSOwner::File() aborts.
    static bool parseInto(MStream* s, Image& img);

private:
    static std::map<std::string, Image> s_images;
    static bool s_probing;
};
```

- [ ] **Step 4: Implement**

In `lib/meatloaf/media/hd/hdd.cpp`, add `#include <map>` near the existing includes, and append this **after** `HDDMStream::seekPath()` and before the "File implementations" banner:

```cpp
/********************************************************
 * Partition registry
 ********************************************************/

std::map<std::string, HDDImageRegistry::Image> HDDImageRegistry::s_images;
bool HDDImageRegistry::s_probing = false;

const HDDPartition* HDDImageRegistry::Image::byNumber(uint8_t number) const
{
    for (auto &p : parts)
    {
        if (p.number == number)
            return &p;
    }
    return nullptr;
}

const HDDPartition* HDDImageRegistry::Image::byName(std::string name) const
{
    bool wildcard = (mstr::contains(name, "*") || mstr::contains(name, "?"));
    for (auto &p : parts)
    {
        if (mstr::compareFilename(p.name, name, wildcard))
            return &p;
    }
    return nullptr;
}

bool HDDImageRegistry::Image::trySelect(uint8_t number)
{
    // 0 is not a partition - it means "the currently selected one". byNumber()
    // never returns an entry for it, so this is belt-and-braces, but it states
    // the rule where a reader will look for it.
    if (number == 0)
        return false;

    const HDDPartition* p = byNumber(number);
    if (p == nullptr || p->type != 1)   // only a CFS partition is browsable
        return false;
    selected = number;
    return true;
}

bool HDDImageRegistry::parseInto(MStream* s, Image& img)
{
    if (s == nullptr || !s->isOpen())
        return false;

    HDDMStream::BootSector boot;
    if (!s->seek(0))
        return false;
    if (s->read((uint8_t*)&boot, sizeof(boot)) != sizeof(boot))
        return false;

    if (strncmp(boot.id, "C64 CFS", 7) != 0)
    {
        Debug_printv("Invalid CFS signature");
        return false;
    }

    img.disk_label = std::string(boot.disk_label, 16);
    while (!img.disk_label.empty() &&
           (img.disk_label.back() == ' ' || img.disk_label.back() == '\0'))
        img.disk_label.pop_back();

    // DP is a SLOT index here; it is converted to a partition number below,
    // once the table has been walked and the numbering is known.
    const uint8_t dp_slot = boot.default_partition & 0x0F;
    img.default_part = 0;

    // The partition directory is exactly one 512-byte sector: 16 entries of
    // 32 bytes. There is no second sector and no chaining.
    uint8_t sector[512];
    if (!s->seek(boot.part_dir.getLBA() * 512))
        return false;
    if (s->read(sector, sizeof(sector)) != sizeof(sector))
        return false;

    img.parts.clear();
    uint8_t next_number = 1;                // partitions are numbered from 1
    for (uint8_t i = 0; i < 16; i++)
    {
        HDDMStream::PartitionEntry pe;
        memcpy(&pe, sector + (i * 32), sizeof(pe));

        if (!pe.isValid())
            continue;                       // an invalid slot consumes no number

        HDDPartition p;
        p.number = next_number++;           // must match seekPartitionEntry()
        p.slot = i;
        p.type = pe.getType();
        p.name = trimEntryName(pe.name, 16, '\0');
        p.root_lba = pe.root_dir.getLBA();
        p.size = 0;
        if (pe.end.getLBA() >= pe.start.getLBA())
            p.size = (pe.end.getLBA() - pe.start.getLBA() + 1) * 512;
        p.hidden = pe.isHidden();
        p.writeable = (pe.start.b[0] & 0x10) != 0;
        img.parts.push_back(p);

        Debug_printv("partition[%d] slot[%d] type[%d] name[%s] root[%lu] size[%lu]",
                     p.number, p.slot, p.type, p.name.c_str(), p.root_lba, p.size);
    }

    // Convert DP out of slot space into partition-number space. This is the
    // ONLY place the two meet; everything downstream speaks numbers.
    for (const HDDPartition &p : img.parts)
    {
        if (p.slot == dp_slot) { img.default_part = p.number; break; }
    }

    // The default partition when it names a CFS one, else the first CFS
    // partition. An image with NO CFS partition fails to parse: there is
    // nothing to select and nothing to mount, and the alternative is a
    // `selected` naming a partition trySelect() would refuse.
    if (!img.trySelect(img.default_part))
    {
        bool any = false;
        for (const HDDPartition &p : img.parts)
        {
            if (p.type == 1) { img.selected = p.number; any = true; break; }
        }
        if (!any)
        {
            Debug_printv("No usable CFS partitions");
            return false;
        }
    }

    img.valid = true;
    Debug_printv("CFS label[%s] partitions[%d] default[%d] selected[%d]",
                 img.disk_label.c_str(), img.parts.size(),
                 img.default_part, img.selected);
    return true;
}
```

`img.default_part` stays 0 when the DP slot is invalid — 0 is not a partition
number, so `trySelect(0)` correctly fails and the first-CFS fallback runs.

`trimEntryName()` is the existing file-static helper at the top of `hdd.cpp` — no change needed.

- [ ] **Step 5: Run the tests and verify they pass**

```bash
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_hdd_read > /tmp/t.log 2>&1; echo "exit=$?"; grep -E "PASSED|FAILED" /tmp/t.log
```

Expected: 12 tests, all PASSED.

- [ ] **Step 6: Commit**

```bash
git add lib/meatloaf/media/hd/hdd.h lib/meatloaf/media/hd/hdd.cpp test/native/test_hdd_read/test_hdd_read.cpp
git commit -m "feat: parse the CFS partition table into an HDDImageRegistry image"
```

---

## Task 3: Registry surface — container, obtain, select, path resolution

**Files:**
- Modify: `lib/meatloaf/media/hd/hdd.h`, `lib/meatloaf/media/hd/hdd.cpp`
- Test: `test/native/test_hdd_read/test_hdd_read.cpp`

**Interfaces:**
- Consumes: Task 2's `HDDImageRegistry::Image`, `parseInto`, `trySelect`.
- Produces:
  - `static std::string HDDImageRegistry::containerOf(const std::string& path)`
  - `static HDDImageRegistry::Image* HDDImageRegistry::obtain(const std::string& containerUrl)`
  - `static bool HDDImageRegistry::select(const std::string& containerUrl, uint8_t number)`
  - `static bool HDDImageRegistry::probing()`
  - `const HDDPartition* hddResolvePartitionIn(const HDDImageRegistry::Image& img, const std::string& in_path, std::string* out_rest, bool* out_explicit)`
  - `const HDDPartition* HDDResolvePartition(const std::string& containerUrl, const std::string& in_path, std::string* out_rest = nullptr, bool* out_explicit = nullptr)`

- [ ] **Step 1: Write the failing tests**

Add to `test/native/test_hdd_read/test_hdd_read.cpp`:

```cpp
void test_containerOf_finds_the_hdd_component(void)
{
    TEST_ASSERT_EQUAL_STRING("/sd/x.hdd",
        HDDImageRegistry::containerOf("/sd/x.hdd").c_str());
    TEST_ASSERT_EQUAL_STRING("/sd/x.hdd",
        HDDImageRegistry::containerOf("/sd/x.hdd/STUFF/GAME").c_str());
    TEST_ASSERT_EQUAL_STRING("/sd/X.HDD",
        HDDImageRegistry::containerOf("/sd/X.HDD/STUFF").c_str());
    TEST_ASSERT_EQUAL_STRING("",
        HDDImageRegistry::containerOf("/sd/games/x.d64").c_str());
    // ".hdd" inside a NAME, not as a path component, is not a container.
    TEST_ASSERT_EQUAL_STRING("",
        HDDImageRegistry::containerOf("/sd/my.hddx/file").c_str());
}

// A path may BIND a partition; it must never change the image's selection.
void test_resolve_binds_partition_without_selecting(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));
    TEST_ASSERT_EQUAL_UINT8(1, img.selected);

    std::string rest;
    bool explicit_part = false;
    const HDDPartition* p =
        hddResolvePartitionIn(img, "DISK IMAGES/GAME", &rest, &explicit_part);

    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(explicit_part);
    TEST_ASSERT_EQUAL_UINT8(2, p->number);
    TEST_ASSERT_EQUAL_STRING("GAME", rest.c_str());
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, img.selected,
        "resolving a path must not change the selected partition");
}

void test_resolve_by_number(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));

    // "2" is DISK IMAGES, the SECOND valid entry - not table slot 2, which
    // does not exist in this image.
    std::string rest;
    bool explicit_part = false;
    const HDDPartition* p = hddResolvePartitionIn(img, "2/GAME", &rest, &explicit_part);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(explicit_part);
    TEST_ASSERT_EQUAL_UINT8(2, p->number);
    TEST_ASSERT_EQUAL_STRING("DISK IMAGES", p->name.c_str());
    TEST_ASSERT_EQUAL_STRING("GAME", rest.c_str());
}

// As in DHD (vdrive.c:1324), a partition number of 0 in a path means "the
// currently selected partition". Select 2, then check "0/..." follows it.
void test_resolve_zero_means_the_selected_partition(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));
    TEST_ASSERT_TRUE(img.trySelect(2));

    std::string rest;
    bool explicit_part = false;
    const HDDPartition* p = hddResolvePartitionIn(img, "0/GAME", &rest, &explicit_part);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(explicit_part);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, p->number,
        "0 in a path must follow the selection, as it does in DHD");
    TEST_ASSERT_EQUAL_STRING("GAME", rest.c_str());

    // ...and it is still only a reference: the selection is unchanged.
    TEST_ASSERT_EQUAL_UINT8(2, img.selected);
}

// A component that is not a partition is a FILENAME: resolution falls back to
// the current selection and reports the path unchanged.
void test_resolve_non_partition_falls_back_to_selection(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));
    TEST_ASSERT_TRUE(img.trySelect(2));

    std::string rest;
    bool explicit_part = true;
    const HDDPartition* p = hddResolvePartitionIn(img, "SOMEFILE", &rest, &explicit_part);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_FALSE(explicit_part);
    TEST_ASSERT_EQUAL_UINT8(2, p->number);              // the selection
    TEST_ASSERT_EQUAL_STRING("SOMEFILE", rest.c_str()); // untouched

    // Out-of-range and unparseable numbers are filenames too, never a
    // truncated partition number - this is the 1571-resolved-to-35 bug.
    explicit_part = true;
    TEST_ASSERT_NOT_NULL(hddResolvePartitionIn(img, "1571", &rest, &explicit_part));
    TEST_ASSERT_FALSE(explicit_part);
    TEST_ASSERT_EQUAL_STRING("1571", rest.c_str());
}

// A partition wins over a same-named file; the file stays reachable by number.
void test_resolve_empty_path_uses_selection(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));
    TEST_ASSERT_TRUE(img.trySelect(2));

    std::string rest = "unset";
    bool explicit_part = true;
    const HDDPartition* p = hddResolvePartitionIn(img, "", &rest, &explicit_part);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_FALSE(explicit_part);
    TEST_ASSERT_EQUAL_UINT8(2, p->number);
    TEST_ASSERT_EQUAL_STRING("", rest.c_str());
}
```

Register them in `main()`. `test_containerOf_finds_the_hdd_component` parses no image, so it goes **before** the `imageAvailable()` guard — that guard calls `TEST_IGNORE_MESSAGE` and returns, which would skip it:

```cpp
    RUN_TEST(test_containerOf_finds_the_hdd_component);

    if (!imageAvailable())
    {
        TEST_IGNORE_MESSAGE("sample image .archive/hdd/ide20201227.hdd not present");
        return UNITY_END();
    }
```

The other five go inside the existing `multiImageAvailable()` block, after `test_registry_try_select`:

```cpp
        RUN_TEST(test_resolve_binds_partition_without_selecting);
        RUN_TEST(test_resolve_by_number);
        RUN_TEST(test_resolve_zero_means_the_selected_partition);
        RUN_TEST(test_resolve_non_partition_falls_back_to_selection);
        RUN_TEST(test_resolve_empty_path_uses_selection);
```

- [ ] **Step 2: Run and verify failure**

```bash
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_hdd_read > /tmp/t.log 2>&1; grep -E "error:" /tmp/t.log | head
```

Expected: compile errors — `containerOf` and `hddResolvePartitionIn` are not declared.

- [ ] **Step 3: Declare the surface**

In `lib/meatloaf/media/hd/hdd.h`, add to `HDDImageRegistry`'s `public:` section, after `parseInto`:

```cpp
    static Image* obtain(const std::string& containerUrl);
    static bool   select(const std::string& containerUrl, uint8_t number);

    // True while the registry reads the raw image bytes, so
    // HDDMFileSystem::handles() declines the path and the underlying
    // filesystem serves them instead of another HDDMFile.
    static bool probing() { return s_probing; }

    // Path of the ".hdd" container within 'path', or "" if none.
    static std::string containerOf(const std::string& path);

private:
    static bool parse(const std::string& containerUrl, Image& img);
```

(the existing `private:` block with `s_images`/`s_probing` follows).

Then, after the class:

```cpp
// Resolve which partition an in-image path refers to, WITHOUT changing the
// image's selected partition.
//
// Resolution order for the FIRST component: an in-range partition number
// 0-16, then byName(), otherwise it is not a partition and the current
// selection applies. A partition wins over a same-named file; such a file
// stays reachable as "<image>/<number>/<file>".
//
// As in DHD, a partition number of 0 means "the currently selected
// partition" - it is NOT a table entry, and must never reach byNumber().
//
// hddResolvePartitionIn() is the pure core, taking an already-parsed image so
// it can be tested without MFSOwner. HDDResolvePartition() is the wrapper the
// firmware calls.
const HDDPartition* hddResolvePartitionIn(const HDDImageRegistry::Image& img,
                                          const std::string& in_path,
                                          std::string* out_rest = nullptr,
                                          bool* out_explicit = nullptr);

const HDDPartition* HDDResolvePartition(const std::string& containerUrl,
                                        const std::string& in_path,
                                        std::string* out_rest = nullptr,
                                        bool* out_explicit = nullptr);
```

- [ ] **Step 4: Implement**

Append to `lib/meatloaf/media/hd/hdd.cpp`'s registry section. Add `#include <cstdlib>` and `#include <memory>` to the includes.

```cpp
std::string HDDImageRegistry::containerOf(const std::string &path)
{
    static const char *ext = ".hdd";
    const size_t elen = 4;

    std::string lower = path;
    for (auto &c : lower)
        c = tolower(c);

    size_t p = lower.find(ext);
    while (p != std::string::npos)
    {
        size_t end = p + elen;
        if (end == lower.size() || lower[end] == '/')
            return path.substr(0, end);
        p = lower.find(ext, p + 1);
    }
    return "";
}

HDDImageRegistry::Image* HDDImageRegistry::obtain(const std::string &containerUrl)
{
    if (containerUrl.empty())
        return nullptr;

    auto it = s_images.find(containerUrl);
    if (it != s_images.end() && it->second.valid)
        return &it->second;

    // (Re-)parse: not yet seen, or the image wasn't readable last time
    Image img;
    if (!parse(containerUrl, img))
        return nullptr;

    s_images[containerUrl] = std::move(img);
    return &s_images[containerUrl];
}

bool HDDImageRegistry::parse(const std::string &containerUrl, Image &img)
{
    // Open the raw image bytes: the probing flag makes HDDMFileSystem decline
    // the path so the underlying filesystem serves it, rather than handing
    // back another HDDMFile whose stream is already decoded.
    s_probing = true;
    std::unique_ptr<MFile> f(MFSOwner::File(containerUrl));
    std::shared_ptr<MStream> s = (f != nullptr) ? f->getSourceStream() : nullptr;
    s_probing = false;

    if (s == nullptr || !s->isOpen())
    {
        Debug_printv("Cannot open CFS image [%s]", containerUrl.c_str());
        return false;
    }

    return parseInto(s.get(), img);
}

bool HDDImageRegistry::select(const std::string &containerUrl, uint8_t number)
{
    Image* img = obtain(containerUrl);
    if (img == nullptr)
        return false;

    // No cached-stream disposal here, unlike DHDImageRegistry::select(): see
    // the class comment. HDDMStream re-derives its position on every call.
    return img->trySelect(number);
}

const HDDPartition* hddResolvePartitionIn(const HDDImageRegistry::Image &img,
                                          const std::string &in_path,
                                          std::string *out_rest,
                                          bool *out_explicit)
{
    if (out_rest) *out_rest = in_path;
    if (out_explicit) *out_explicit = false;

    if (!img.valid)
        return nullptr;

    const HDDPartition *p = nullptr;

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
                // Range-check BEFORE narrowing to uint8_t. An int silently
                // truncated into byNumber() is what once made "1571" resolve
                // to partition 35 in DHD.
                //
                // v == 0 means "the currently selected partition", exactly as
                // in DHD (vdrive.c:1324) - it must NOT go through byNumber().
                // 16 is the bound because a CFS table holds 16 entries, so
                // that is the highest number the 1-based numbering reaches.
                char *end = nullptr;
                long v = strtol(comp.c_str(), &end, 10);
                if (end != comp.c_str() && *end == '\0' && v >= 0 && v <= 16)
                    p = (v == 0) ? img.current() : img.byNumber((uint8_t)v);
            }
            else
            {
                p = img.byName(comp);
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
        p = img.current();

    return p;
}

const HDDPartition* HDDResolvePartition(const std::string &containerUrl,
                                        const std::string &in_path,
                                        std::string *out_rest,
                                        bool *out_explicit)
{
    if (out_rest) *out_rest = in_path;
    if (out_explicit) *out_explicit = false;

    HDDImageRegistry::Image *img = HDDImageRegistry::obtain(containerUrl);
    if (img == nullptr)
        return nullptr;

    return hddResolvePartitionIn(*img, in_path, out_rest, out_explicit);
}
```

- [ ] **Step 5: Add the probing guard to the filesystem**

In `lib/meatloaf/media/hd/hdd.h`, replace `HDDMFileSystem::handles()`:

```cpp
    bool handles(std::string fileName) override {
        // Decline while the registry reads the raw image bytes
        if (HDDImageRegistry::probing())
            return false;
        return byExtension(".hdd", fileName);
    }
```

- [ ] **Step 6: Run the tests and verify they pass**

```bash
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_hdd_read > /tmp/t.log 2>&1; echo "exit=$?"; grep -E "PASSED|FAILED" /tmp/t.log
```

Expected: 18 tests, all PASSED.

- [ ] **Step 7: Commit**

```bash
git add lib/meatloaf/media/hd/hdd.h lib/meatloaf/media/hd/hdd.cpp test/native/test_hdd_read/test_hdd_read.cpp
git commit -m "feat: HDDImageRegistry selection and path-based partition resolution"
```

---

## Task 4: Stream root semantics — the selected partition becomes the root

**Files:**
- Modify: `lib/meatloaf/media/hd/hdd.h`, `lib/meatloaf/media/hd/hdd.cpp:151-202,386-420`
- Test: `test/native/test_hdd_read/test_hdd_read.cpp`

**Interfaces:**
- Consumes: Task 2/3's registry (indirectly — the stream never calls it; see below).
- Produces:
  - `uint8_t HDDMStream::selected_partition` — the partition NUMBER the stream treats as its root. `0` means "fall back to the boot sector's DP", which is what a directly constructed stream gets.
  - `bool HDDMStream::selectPartitionByNumber(uint8_t number)`

**Why the stream does not read the registry:** see "Deviation from the spec" in Global Constraints. `HDDMFile` writes `selected_partition` before each call.

- [ ] **Step 1: Write the failing tests**

```cpp
// With no partition named in the path, the root of the image is now the
// SELECTED partition's directory rather than the list of partitions. A
// directly constructed stream has no selection, so it falls back to the boot
// sector's DP - which is what the four original read tests rely on.
void test_seekDirectory_empty_path_enters_default_partition(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekDirectory(""));

    // Assert the label BEFORE any seekEntry: the first entry of a CFS root
    // directory is a LABEL entry, and seekEntry() overwrites dir_label from
    // it. (In this image both happen to read "STUFF", so checking afterwards
    // would pass under the old semantics too and prove nothing.)
    TEST_ASSERT_EQUAL_STRING_MESSAGE("STUFF", image->dir_label.c_str(),
        "seekDirectory(\"\") should select the default partition");

    // The real discriminator: entry 1 of the root is now the first FILE
    // inside the default partition. Under the old semantics the root was the
    // partition list, so entry 1 was the partition STUFF itself.
    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)1));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("%DELETED  FILES%", image->entry.filename.c_str(),
        "the image root should list the partition's files, not the partitions");
}

// A path that names a partition still resolves, and reaching a file inside it
// works exactly as before.
void test_seekDirectory_partition_name_still_resolves(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekDirectory("STUFF/UTILS"));
    TEST_ASSERT_EQUAL(HDDMStream::PATH_FILE, image->resolvePath(DEEP_FILE));
}

// selected_partition overrides the boot sector's DP.
void test_selected_partition_overrides_default(void)
{
    auto src = std::make_shared<FileContainerStream>(MULTI_IMAGE_PATH);
    TEST_ASSERT_TRUE(src->isOpen());
    auto image = std::make_shared<TestHDDStream>(src);
    image->mode = std::ios_base::in;

    image->selected_partition = 2;          // "DISK IMAGES"
    TEST_ASSERT_TRUE(image->seekDirectory(""));
    TEST_ASSERT_EQUAL_STRING("DISK IMAGES", image->dir_label.c_str());

    image->selected_partition = 1;          // "C64 OS"
    TEST_ASSERT_TRUE(image->seekDirectory(""));
    TEST_ASSERT_EQUAL_STRING("C64 OS", image->dir_label.c_str());

    // 0 is not a partition number - it means "no selection", so the stream
    // falls back to the boot sector's DP (slot 0, which is "C64 OS").
    image->selected_partition = 0;
    TEST_ASSERT_TRUE(image->seekDirectory(""));
    TEST_ASSERT_EQUAL_STRING("C64 OS", image->dir_label.c_str());

    // A number past the end is not a silent fallback - it fails.
    image->selected_partition = 9;
    TEST_ASSERT_FALSE(image->selectPartitionByNumber(9));
}
```

Extend the `TestHDDStream` helper with `using HDDMStream::seekEntry;`, `using HDDMStream::dir_label;` and `using HDDMStream::selectPartitionByNumber;` — all three are `protected`. (`seekDirectory`, `resolvePath` and `selected_partition` are already public, so they need no `using`.)

Register the first two beside the other single-image tests, and the third in the `multiImageAvailable()` block:

```cpp
    RUN_TEST(test_seekDirectory_empty_path_enters_default_partition);
    RUN_TEST(test_seekDirectory_partition_name_still_resolves);
    if (multiImageAvailable())
    {
        // ...existing multi-image registrations...
        RUN_TEST(test_selected_partition_overrides_default);
    }
```

- [ ] **Step 2: Run and verify failure**

```bash
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_hdd_read > /tmp/t.log 2>&1; grep -E "error:|FAILED" /tmp/t.log | head
```

Expected: a compile error for `selected_partition`, and once that is declared, `test_seekDirectory_empty_path_enters_default_partition` fails because `seekDirectory("")` still leaves the stream in `partition_list` mode.

- [ ] **Step 3: Add the member and the by-number selector**

In `lib/meatloaf/media/hd/hdd.h`, add to `HDDMStream`'s `public:` section, just after the `PathResult` enum:

```cpp
    // The partition NUMBER (1-based over valid entries) this stream treats as
    // its root. 0 means "no selection - use the boot sector's default", which
    // is unambiguous precisely because partitions are numbered from 1. A
    // directly constructed stream (tests, ImageBroker rebuilds) gets 0;
    // HDDMFile overwrites it with whatever the registry or the path resolved.
    //
    // The stream deliberately does NOT consult HDDImageRegistry itself: that
    // would need MFSOwner::File(), which the native test stubs abort on.
    uint8_t selected_partition = 0;
```

and to the `protected:` method list, beside `selectPartitionByName`:

```cpp
    bool selectPartitionByNumber(uint8_t number);   // 1-based, valid entries only
```

- [ ] **Step 4: Implement**

In `lib/meatloaf/media/hd/hdd.cpp`, add after `selectPartitionByName()`:

```cpp
// 'number' is 1-based and counts only VALID entries - identical to the walk
// in seekPartitionEntry(), and it must stay identical, or the registry and
// the stream would disagree about which partition a number names. 0 is not a
// partition: it means "the currently selected one", which the caller has
// already resolved before reaching here.
bool HDDMStream::selectPartitionByNumber(uint8_t number)
{
    if (number == 0)
        return false;

    uint8_t count = 0;
    for (int i = 0; i < 16; i++)
    {
        const PartitionEntry &pe = partition_entries[i];
        if (!pe.isValid())
            continue;

        if (++count != number)
            continue;

        if (!pe.isCFS())
            return false;   // listed, but not browsable

        partition_list = false;
        dir_start_lba = pe.root_dir.getLBA();
        dir_label = trimEntryName(pe.name, 16, '\0');
        restartDirWalk();
        entry_index = 0;
        return true;
    }
    return false;
}
```

Then replace the partition-resolution block of `seekDirectory()` (currently `if (parts.size()) { ... }`) with:

```cpp
    auto parts = splitPathComponents(path);
    size_t i = 0;

    // The image root is the SELECTED partition's directory, matching the CMD
    // HD/FD behaviour and a real drive. The partition list is served only in
    // response to "$=P", which HDDMFile handles.
    if (parts.size() && selectPartitionByName(parts[0]))
    {
        i = 1;
    }
    else if (!selectCurrentPartition())
    {
        return false;
    }
```

and add the small helper above `seekDirectory()`:

```cpp
// The partition this stream treats as its root: whatever HDDMFile put in
// selected_partition, else the boot sector's default partition, else the
// first valid CFS partition (which is what selectPartitionByName("") does).
bool HDDMStream::selectCurrentPartition()
{
    if (selected_partition != 0 && selectPartitionByNumber(selected_partition))
        return true;

    return selectPartitionByName("");
}
```

Declare `bool selectCurrentPartition();` beside `selectPartitionByNumber` in the header.

Finally, in `resolvePath()`, the `if (partition_list)` block can no longer be reached from a bare path, but it must stay for the `$=P` case Task 5 adds. Leave it exactly as it is.

- [ ] **Step 5: Run the tests and verify they pass**

```bash
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_hdd_read > /tmp/t.log 2>&1; echo "exit=$?"; grep -E "PASSED|FAILED" /tmp/t.log
```

Expected: 21 tests, all PASSED — **including the four original read tests**, which must not regress.

- [ ] **Step 6: Commit**

```bash
git add lib/meatloaf/media/hd/hdd.h lib/meatloaf/media/hd/hdd.cpp test/native/test_hdd_read/test_hdd_read.cpp
git commit -m "feat: the CFS image root is the selected partition, not the partition list"
```

---

## Task 5: `HDDMFile` — `$=P`, path binding, partition-numbered entry URLs

Not covered by the native suite: `HDDMFile` calls `MFSOwner::File()` and `ImageBroker::obtain()`, both of which abort under the native stubs. Verification is by inspection plus the hardware checks in Task 6.

**Files:**
- Modify: `lib/meatloaf/media/hd/hdd.h:243-270`, `lib/meatloaf/media/hd/hdd.cpp:634-739`

**Interfaces:**
- Consumes: `HDDImageRegistry::{containerOf,obtain}`, `HDDResolvePartition` (Task 3); `HDDMStream::selected_partition` (Task 4).
- Produces: `HDDMFile::m_part` (0xFF = follow the selection) and `HDDMFile::effectivePartition()`, used only within `hdd.cpp`.

- [ ] **Step 1: Declare the new members**

In `lib/meatloaf/media/hd/hdd.h`, replace the tail of `HDDMFile` (from `bool rewindDirectory()` to the closing brace) with:

```cpp
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    bool isDirectory() override;
    bool exists() override;

    bool isDir = true;
    bool dirIsOpen = false;

protected:
    // Handle the partition part of pathInStream once per MFile. "$=P"
    // switches to partition-list mode; a leading component naming a partition
    // binds THIS path to it and is stripped, so the rest resolves inside it.
    // It deliberately does NOT call select(): naming a partition in a path
    // must not change what the image has selected.
    void normalizePath();

    // The partition this MFile operates on, or nullptr when the image has no
    // usable partition table.
    const HDDPartition* effectivePartition();

    // Writes the resolved partition into the stream, which is how the stream
    // learns which directory is its root. Safe to call with a null stream.
    void applyPartition(const std::shared_ptr<HDDMStream>& image);

    bool normalized = false;
    bool listing_partitions = false;
    uint16_t part_index = 0;

    // The partition NUMBER this MFile's path names. 0 = the path named none,
    // so the image's current selection applies - the same convention and the
    // same sentinel value DHDPartitionMFile::m_part uses.
    uint8_t m_part = 0;
};
```

- [ ] **Step 2: Implement `normalizePath`, `effectivePartition` and `applyPartition`**

In `lib/meatloaf/media/hd/hdd.cpp`, insert before `HDDMFile::rewindDirectory()`:

```cpp
void HDDMFile::normalizePath()
{
    if (normalized)
        return;
    normalized = true;

    if (pathInStream.empty())
        return;

    // "$=P" switches to partition-list mode.
    if (mstr::startsWith(pathInStream, "$=P") || mstr::startsWith(pathInStream, "$=p"))
    {
        listing_partitions = true;
        pathInStream.clear();
        return;
    }

    std::string rest;
    bool explicit_part = false;
    const HDDPartition *p = HDDResolvePartition(
        HDDImageRegistry::containerOf(url), pathInStream, &rest, &explicit_part);

    if (p != nullptr && explicit_part)
    {
        m_part = p->number;
        pathInStream = rest;
    }
}

const HDDPartition* HDDMFile::effectivePartition()
{
    auto img = HDDImageRegistry::obtain(HDDImageRegistry::containerOf(url));
    if (img == nullptr)
        return nullptr;
    return (m_part == 0) ? img->current() : img->byNumber(m_part);
}

void HDDMFile::applyPartition(const std::shared_ptr<HDDMStream>& image)
{
    if (image == nullptr)
        return;

    // 0 when the image has no usable table, which is the stream's "use the
    // boot sector's DP" fallback - the same value m_part uses for "no
    // partition named", since partitions are numbered from 1.
    const HDDPartition *p = effectivePartition();
    image->selected_partition = (p != nullptr) ? p->number : 0;
}
```

- [ ] **Step 3: Rewrite `rewindDirectory()`**

Replace `HDDMFile::rewindDirectory()` with:

```cpp
bool HDDMFile::rewindDirectory()
{
    normalizePath();
    dirIsOpen = true;
    Debug_printv("url[%s] pathInStream[%s]", url.c_str(), pathInStream.c_str());

    if (listing_partitions)
    {
        auto img = HDDImageRegistry::obtain(HDDImageRegistry::containerOf(url));
        if (img == nullptr)
        {
            dirIsOpen = false;
            return false;
        }
        part_index = 0;
        media_header = img->disk_label;
        media_id = "cfs";
        media_blocks_free = 0;
        media_block_size = 512;
        media_image = name;
        return true;
    }

    auto image = ImageBroker::obtain<HDDMStream>("hdd", url);
    if (image == nullptr)
    {
        dirIsOpen = false;
        return false;
    }

    if (!image->readHeader())
    {
        Debug_printv("Failed to read HDD/CFS header");
        dirIsOpen = false;
        return false;
    }

    applyPartition(image);
    image->resetEntryCounter();

    if (!image->seekDirectory(pathInStream))
    {
        Debug_printv("directory not found in image [%s]", pathInStream.c_str());
        dirIsOpen = false;
        return false;
    }

    // Set Media Info Fields
    media_header = image->dir_label.empty() ? image->header.disk_label : image->dir_label;
    media_id = "cfs";
    media_blocks_free = 0;  // TODO: count usage bitmap bits
    media_block_size = image->block_size;
    media_image = name;
    if ( !sourceFile->media_archive.empty() )
        media_archive = sourceFile->media_archive;

    return true;
}
```

Note the two `dirIsOpen = false` additions on the early-return paths: the original left `dirIsOpen` true when the broker or `readHeader()` failed, which combined with the guard in `getNextFileInDir()` is the endless-listing shape described in `AGENTS.md`.

- [ ] **Step 4: Rewrite `getNextFileInDir()`**

```cpp
MFile* HDDMFile::getNextFileInDir()
{
    // Same as D64MFile::getNextFileInDir(): a failed rewind has already reset
    // the shared stream's entry counter, so reading on would restart the
    // listing from entry 0 forever.
    if (!dirIsOpen && !rewindDirectory())
        return nullptr;

    if (listing_partitions)
    {
        auto img = HDDImageRegistry::obtain(HDDImageRegistry::containerOf(url));
        if (img == nullptr || part_index >= img->parts.size())
        {
            dirIsOpen = false;
            return nullptr;
        }

        const HDDPartition &p = img->parts[part_index++];
        std::string fname = p.name;
        mstr::replaceAll(fname, "/", "\\");

        // By NUMBER, not name: CFS names are 16 bytes that may contain '/'
        // and spaces, which do not survive a URL path component.
        auto file = MFSOwner::File(url + "/" + std::to_string((unsigned)p.number));
        file->name = fname;
        file->extension = (p.type == 1) ? "CFS"
                        : (p.type == 2) ? "GEOS"
                        : (p.type == 0) ? "----" : "?";
        file->size = p.size;
        file->is_dir = (p.type == 1) ? 1 : 0;   // only CFS is browsable
        file->is_hidden = p.hidden;
        return file;
    }

    auto image = ImageBroker::obtain<HDDMStream>("hdd", url);
    if (image == nullptr)
        goto exit;

    applyPartition(image);

    if (image->getNextImageEntry())
    {
        std::string filename = image->entry.filename;
        mstr::replaceAll(filename, "/", "\\");

        // Entry URLs name the partition BY NUMBER, so an entry listed from a
        // partition other than the selected one still resolves back into its
        // own partition rather than into the selection.
        std::string entryUrl = url;
        const HDDPartition *p = effectivePartition();
        if (p != nullptr)
        {
            entryUrl += '/';
            entryUrl += std::to_string((unsigned)p->number);
        }
        if (pathInStream.size()) { entryUrl += '/'; entryUrl += pathInStream; }
        entryUrl += '/'; entryUrl += filename;

        auto file = MFSOwner::File(entryUrl);
        file->name = filename;  // Use actual entry name, not container image name
        file->extension = image->entry.type;
        file->size = image->entry.size;
        file->is_dir = image->entry.is_directory;
        file->is_hidden = image->entry.is_hidden;

        return file;
    }

exit:
    dirIsOpen = false;
    return nullptr;
}
```

- [ ] **Step 5: Update `isDirectory()` and `exists()`**

```cpp
bool HDDMFile::isDirectory()
{
    normalizePath();

    if (listing_partitions)
        return true;

    // Use cached value if set (e.g. by getNextFileInDir)
    if (is_dir != -1)
        return is_dir == 1;

    // Container root is always a directory
    if (pathInStream.empty() || pathInStream == "/")
        return true;

    auto stream = ImageBroker::obtain<HDDMStream>("hdd", url);
    if (stream != nullptr)
    {
        applyPartition(stream);
        return stream->resolvePath(pathInStream) == HDDMStream::PATH_DIR;
    }

    return false;
}

bool HDDMFile::exists()
{
    normalizePath();

    if (listing_partitions)
        return true;

    auto stream = ImageBroker::obtain<HDDMStream>("hdd", url);
    if (stream == nullptr)
        return false;

    applyPartition(stream);

    if (pathInStream.size() && pathInStream != "/")
        return stream->resolvePath(pathInStream) != HDDMStream::PATH_NOT_FOUND;

    return true;
}
```

- [ ] **Step 6: Set the partition on the decoded stream too**

`getDecodedStream()` builds a fresh stream for file reads, which never goes through the broker. In `lib/meatloaf/media/hd/hdd.h`, replace `HDDMFile::getDecodedStream()`:

```cpp
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        normalizePath();
        Debug_printv("[%s]", url.c_str());
        auto stream = std::make_shared<HDDMStream>(is);
        applyPartition(stream);
        return stream;
    }
```

`normalizePath()` must run first — it is what sets `m_part`, which
`applyPartition()` then reads. `applyPartition` is `protected` and
`getDecodedStream` is a member of the same class, so the call compiles even
though `applyPartition` is declared further down the class body.

- [ ] **Step 7: Build for hardware and verify it compiles**

```bash
~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/b.log 2>&1; echo "exit=$?"; grep -E "error:|Flash:|RAM:" /tmp/b.log | head -20
```

Expected: exit=0. Also re-run the native suite — 21 tests must still pass.

- [ ] **Step 8: Commit**

```bash
git add lib/meatloaf/media/hd/hdd.h lib/meatloaf/media/hd/hdd.cpp
git commit -m "feat: \$=P listing and partition-bound paths for CFS images"
```

---

## Task 6: `CP<n>` and the `partition` console command

**Files:**
- Create: `lib/meatloaf/media/hd/partition_select.h`, `lib/meatloaf/media/hd/partition_select.cpp`
- Modify: `lib/device/iec/drive.cpp:2363-2388`, `lib/console/Commands/VFSCommands.cpp:296-411`

**Interfaces:**
- Consumes: `DHDImageRegistry` and `HDDImageRegistry`.
- Produces, in namespace `hdpart`:
  - `enum class Kind { None, CMD, CFS };`
  - `struct Target { Kind kind; std::string container; };`
  - `struct View { uint8_t number; std::string type_label; std::string name; bool selected; bool selectable; };`
  - `Target targetFor(const std::string& path);`
  - `bool list(const Target& t, std::vector<View>& out, std::string& disk_label);`
  - `int resolve(const Target& t, const std::string& arg);` — returns a partition number, or -1
  - `bool select(const Target& t, int number);` — range-checked per kind

- [ ] **Step 1: Create the shared surface header**

`lib/meatloaf/media/hd/partition_select.h`:

```cpp
// One partition surface over both CMD (DHD/D1M/D2M/D4M) and IDE64 CFS (.hdd)
// images, so the drive's CP<n> and the console's `partition` command share a
// single implementation. The two formats have DIFFERENT valid ranges - CMD is
// 1..254, CFS is 0..15 - and they must not be merged into one bound; select()
// is the only place that knows either.
#ifndef MEATLOAF_MEDIA_PARTITION_SELECT
#define MEATLOAF_MEDIA_PARTITION_SELECT

#include <string>
#include <vector>
#include <stdint.h>

namespace hdpart {

enum class Kind { None, CMD, CFS };

struct Target {
    Kind kind = Kind::None;
    std::string container;
    explicit operator bool() const { return kind != Kind::None; }
};

struct View {
    uint8_t number;
    std::string type_label;
    std::string name;       // UTF-8
    bool selected;
    bool selectable;
};

// Which partitioned image, if any, the path is inside.
Target targetFor(const std::string& path);

// Fills 'out' with every table entry and 'disk_label' with the image label.
// False when the image has no readable partition table.
bool list(const Target& t, std::vector<View>& out, std::string& disk_label);

// A partition number for 'arg', which may be a number or a name/wildcard.
// -1 when nothing matches. Does not select.
int resolve(const Target& t, const std::string& arg);

// Range-checked per format, then delegated to the format's registry, which
// refuses a partition that is not mountable (CMD entry 0; a non-CFS slot).
bool select(const Target& t, int number);

} // namespace hdpart

#endif /* MEATLOAF_MEDIA_PARTITION_SELECT */
```

- [ ] **Step 2: Implement it**

`lib/meatloaf/media/hd/partition_select.cpp`:

```cpp
#include "partition_select.h"

#include "dhd.h"
#include "hdd.h"
#include "string_utils.h"

#include <cstdlib>

namespace hdpart {

static const char* cmd_type_label(uint8_t type)
{
    switch (type)
    {
        case 1:  return "NAT";
        case 2:  return "1541";
        case 3:  return "1571";
        case 4:  return "1581";
        case 0xFF: return "SYS";   // entry 0: listed, never selectable
        default: return "?";
    }
}

static const char* cfs_type_label(uint8_t type)
{
    switch (type)
    {
        case 0:  return "----";    // unformatted
        case 1:  return "CFS";
        case 2:  return "GEOS";
        default: return "?";       // 3-11 reserved
    }
}

Target targetFor(const std::string& path)
{
    Target t;

    std::string c = DHDImageRegistry::containerOf(path);
    if (!c.empty())
    {
        t.kind = Kind::CMD;
        t.container = c;
        return t;
    }

    c = HDDImageRegistry::containerOf(path);
    if (!c.empty())
    {
        t.kind = Kind::CFS;
        t.container = c;
    }
    return t;
}

bool list(const Target& t, std::vector<View>& out, std::string& disk_label)
{
    out.clear();
    disk_label.clear();

    if (t.kind == Kind::CMD)
    {
        auto img = DHDImageRegistry::obtain(t.container);
        if (img == nullptr || !img->valid)
            return false;
        disk_label = img->disk_label;
        for (const auto& p : img->parts)
        {
            View v;
            v.number = p.number;
            v.type_label = cmd_type_label(p.type);
            v.name = mstr::toUTF8(p.name);
            v.selected = (p.number == img->selected);
            v.selectable = (p.number != 0);
            out.push_back(v);
        }
        return true;
    }

    if (t.kind == Kind::CFS)
    {
        auto img = HDDImageRegistry::obtain(t.container);
        if (img == nullptr || !img->valid)
            return false;
        disk_label = img->disk_label;
        for (const auto& p : img->parts)
        {
            View v;
            v.number = p.number;
            v.type_label = cfs_type_label(p.type);
            v.name = p.name;           // CFS names are ASCII
            v.selected = (p.number == img->selected);
            v.selectable = (p.type == 1);
            out.push_back(v);
        }
        return true;
    }

    return false;
}

int resolve(const Target& t, const std::string& arg)
{
    if (t.kind == Kind::None || arg.empty())
        return -1;

    const long hi = (t.kind == Kind::CMD) ? 254 : 16;

    // Numbers first. Range-checked BEFORE any narrowing, per the project rule
    // against atoi/std::stoi on C64- or network-sourced input.
    bool numeric = arg.find_first_not_of("0123456789") == std::string::npos;
    if (numeric)
    {
        char* end = nullptr;
        long v = strtol(arg.c_str(), &end, 10);
        if (end != arg.c_str() && *end == '\0' && v >= 0 && v <= hi)
        {
            std::vector<View> parts;
            std::string label;
            if (list(t, parts, label))
            {
                for (const auto& p : parts)
                {
                    if (p.number == (uint8_t)v)
                        return (int)v;
                }
            }
        }
    }

    // Then names - a partition can legitimately be named "1571". Wildcards
    // are honoured, case-sensitively, by each registry's byName().
    if (t.kind == Kind::CMD)
    {
        auto img = DHDImageRegistry::obtain(t.container);
        if (img != nullptr)
        {
            const DHDPartition* p = img->byName(arg);
            if (p != nullptr)
                return (int)p->number;
        }
    }
    else
    {
        auto img = HDDImageRegistry::obtain(t.container);
        if (img != nullptr)
        {
            const HDDPartition* p = img->byName(arg);
            if (p != nullptr)
                return (int)p->number;
        }
    }

    return -1;
}

bool select(const Target& t, int number)
{
    // The valid ranges genuinely differ and must stay separate: a CMD HD holds
    // 1..254 with table entry 0 the system partition,
    // and CFS numbers its partitions 1..16 over the valid entries of a
    // 16-entry table. Both reserve 0 for "the currently selected partition",
    // so neither accepts it here.
    if (t.kind == Kind::CMD)
    {
        if (number < 1 || number > 254)
            return false;
        return DHDImageRegistry::select(t.container, (uint8_t)number);
    }

    if (t.kind == Kind::CFS)
    {
        if (number < 1 || number > 16)
            return false;
        return HDDImageRegistry::select(t.container, (uint8_t)number);
    }

    return false;
}

} // namespace hdpart
```

- [ ] **Step 3: Route `changePartition()` through it**

In `lib/device/iec/drive.cpp`, add `#include "media/hd/partition_select.h"` beside the existing `#include "media/hd/dhd.h"`, then replace `iecDrive::changePartition()`:

```cpp
// CMD "CP<n>" - change the selected partition of the mounted image. Works on
// a CMD HD/FD image (DHD, D1M/D2M/D4M) and on an IDE64 CFS image (.hdd); the
// valid range differs per format and lives in hdpart::select().
void iecDrive::changePartition(int pnum)
{
    hdpart::Target t = hdpart::targetFor(m_cwd != nullptr ? m_cwd->url : "");
    Debug_printv("change partition pnum[%d] container[%s]", pnum, t.container.c_str());

    if (!t)
    {
        setStatusCode(ST_SYNTAX_INVALID);
        return;
    }

    if (hdpart::select(t, pnum))
    {
        // The new partition's root becomes the working directory
        set_cwd(t.container, true);
        setStatusCode(ST_PARTITION_SELECTED, pnum);
    }
    else
    {
        setStatusCode(ST_PARTITION_ILLEGAL, pnum);
    }
}
```

The old `pnum < 1 || pnum > 254` guard is gone from here on purpose — it was CMD-specific and would have rejected the legal `CP0` on a CFS image. `hdpart::select()` now owns both bounds.

- [ ] **Step 4: Route the console command through it**

In `lib/console/Commands/VFSCommands.cpp`, add `#include "../../meatloaf/media/hd/partition_select.h"` beside the existing `dhd.h` include. Delete the file-static `partition_type_name()` helper — verified to have no callers outside `partition()` (only lines 343 and 409, both of which this rewrite replaces), and its type table now lives in `partition_select.cpp` and replace `int partition(int argc, char **argv)`:

```cpp
// Switch the selected partition of the CMD HD/FD or IDE64 CFS image the
// console is inside. The selection belongs to the IMAGE, not to any one
// MFile, which is why this takes no image argument: it always acts on the
// container in the current path. This is the console's equivalent of CP<n>.
int partition(int argc, char **argv)
{
    hdpart::Target t = hdpart::targetFor(getCurrentPath()->url);
    if (!t)
    {
        Serial.printf("partition: not inside a partitioned disk image\r\n");
        return EXIT_FAILURE;
    }

    std::vector<hdpart::View> parts;
    std::string disk_label;
    if (!hdpart::list(t, parts, disk_label))
    {
        Serial.printf("partition: cannot read the partition table of '%s'\r\n",
                      t.container.c_str());
        return EXIT_FAILURE;
    }

    // No argument: list. CMD names are PETSCII on disk and are already
    // converted to UTF-8 by hdpart::list(), which is what `ls` shows and what
    // the name lookup matches against.
    if (argc < 2)
    {
        Serial.printf("   #  type   name\r\n");
        for (const auto &p : parts)
        {
            Serial.printf("%c%3u  %-5s  \"%s\"\r\n",
                          p.selected ? '*' : ' ',
                          (unsigned)p.number,
                          p.type_label.c_str(),
                          p.name.c_str());
        }
        return EXIT_SUCCESS;
    }

    std::string arg = argv[1];
    int number = hdpart::resolve(t, arg);
    if (number < 0)
    {
        Serial.printf("partition: no such partition: %s\r\n", arg.c_str());
        return EXIT_FAILURE;
    }

    // Copy what we need before select(), which may dispose cached streams.
    std::string name;
    std::string type_label;
    bool selectable = false;
    for (const auto &p : parts)
    {
        if (p.number == (uint8_t)number)
        {
            name = p.name;
            type_label = p.type_label;
            selectable = p.selectable;
            break;
        }
    }

    // Reachable both by number and by name, since every table entry is listed.
    if (!selectable)
    {
        Serial.printf("partition: %u \"%s\" (%s) cannot be selected\r\n",
                      (unsigned)number, name.c_str(), type_label.c_str());
        return EXIT_FAILURE;
    }

    if (!hdpart::select(t, number))
    {
        Serial.printf("partition: could not select partition %u\r\n", (unsigned)number);
        return EXIT_FAILURE;
    }

    // The old cwd may name a subdirectory that existed only in the previous
    // partition, so drop back to the image root.
    setCurrentPath(MFSOwner::File(t.container));

    Serial.printf("Selected partition %u \"%s\" (%s)\r\n",
                  (unsigned)number, name.c_str(), type_label.c_str());
    return EXIT_SUCCESS;
}
```

- [ ] **Step 5: Build and run the native suite**

```bash
~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/b.log 2>&1; echo "exit=$?"; grep -E "error:|Flash:|RAM:" /tmp/b.log | head -20
~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_hdd_read > /tmp/t.log 2>&1; grep -E "PASSED|FAILED" /tmp/t.log
```

Expected: firmware links; 21 native tests pass. If the build reports "Source not found", delete `.pio/build/lolin-d32-pro/CMakeCache.txt` — adding `partition_select.cpp` changes the source glob, which PlatformIO caches.

- [ ] **Step 6: Hardware verification**

Flash and check each of these, since none is reachable from the native suite:

1. `partition` inside a `.hdd` lists the CFS partitions with `*` on the selected one, and still lists CMD partitions correctly inside a `.dhd`.
2. `partition 1` inside `c64os v1.09-clean.hdd` selects `DISK IMAGES`; `ls` then shows that partition's files, and `partition 0` returns to `C64 OS`.
3. `partition "DISK IMAGES"` selects by name.
4. From the C64: `LOAD"$=P",8` lists the partitions; `CP2` returns `02, PARTITION SELECTED`; `CP0` is refused, as on a CMD HD; `CP99` returns `77, SELECTED PARTITION ILLEGAL`.
5. `LOAD"1/<file>",8` loads from partition 1 while partition 2 is selected, and the selection is unchanged afterwards (`partition` still marks 2).
6. `LOAD"0/<file>",8` loads from the SELECTED partition — `0` is a reference to the selection, not to a table entry.
7. Regression: `CP0` and `partition 0` on a **DHD** image still fail, and DHD's `partition` listing still shows entry 0 as `SYS`.

- [ ] **Step 7: Commit**

```bash
git add lib/meatloaf/media/hd/partition_select.h lib/meatloaf/media/hd/partition_select.cpp lib/device/iec/drive.cpp lib/console/Commands/VFSCommands.cpp
git commit -m "feat: CP<n> and the partition command work on IDE64 CFS images"
```

---

## Task 7: Documentation

**Files:**
- Modify: `AGENTS.md`, `lib/meatloaf/AGENTS.md`, `docs/superpowers/specs/2026-08-13-hdd-partition-pattern-design.md`

- [ ] **Step 1: Record the durable rules in `AGENTS.md`**

Add to the "Important Notes" bullet list, after the existing CMD media images entry:

```markdown
- **IDE64 CFS images (.hdd) follow the same partition model as CMD images.** `HDDImageRegistry` (lib/meatloaf/media/hd/hdd.h/cpp) holds the per-image partition table and selection exactly as `DHDImageRegistry` does; `HDDResolvePartition()` binds a partition to a path without ever calling `select()`; `hdpart::` (media/hd/partition_select.h) is the one surface `CP<n>` and the `partition` console command both use. **Partition 0 means "the currently selected partition" in a path and is refused by `select()`, identical to DHD.** Two things are CFS-specific: (1) **there are two numbering spaces** — the raw table SLOT 0-15, which is how the 16-entry partition directory is laid out and what the boot sector's DP byte holds, and the partition NUMBER 1-N, which counts only VALID entries and is what paths, `CP<n>`, `$=P` and the `partition` command speak. `parse()` converts DP between them exactly once; nothing downstream sees a slot, and the numbering must match `HDDMStream::seekPartitionEntry()` entry for entry. (2) There is **no `cached_part`/`brokerUrl()` machinery and no dispose-on-select**, because `HDDMStream` re-derives its position from `seekDirectory(pathInStream)` on every operation, so a broker-cached stream carries no partition identity that can go stale. Only CFS-type partitions (`type == 1`) are selectable; unformatted, GEOS and reserved types are listed but refused.
- **The CFS boot sector's default-partition byte is `$03`, not `$01`** (`HDDMStream::BootSector`). The spec's table is colspan-encoded — `Unused` spans `$00-$02`, `DP` is `$03`, `@Last disk sector` spans `$04-$07` — and the struct had both `default_partition` and `last_sector` off by two. It was latent because every sample image in `.archive/hdd/` has `$00-$03 = 00 00 00 00`. The corpus invariant that pins it: `@Last disk sector == @Partition directory backup + 1`, since the backup directory lives on the last sector of the disk.
- **`HDDMStream` must never call `HDDImageRegistry`.** The selection is written into `HDDMStream::selected_partition` by `HDDMFile` (`applyPartition()`), at all four sites that touch a stream. A registry lookup from inside the stream would need `MFSOwner::File()`, which `abort()`s under the native test stubs — and `FileContainerStream` sets `url` to a path ending in `.hdd`, so the lookup would fire. `selected_partition == 0` means "fall back to the boot sector's DP" — unambiguous precisely because partitions are numbered from 1 — and is what a directly constructed stream gets.
```

- [ ] **Step 2: Record the stream-layer detail in `lib/meatloaf/AGENTS.md`**

Add a `## Recent Changes (August 13, 2026)` section at the top of the Recent Changes runs:

```markdown
## Recent Changes (August 13, 2026)

- **IDE64 CFS gained the CMD partition model** (`media/hd/hdd.h/.cpp`, new `media/hd/partition_select.h/.cpp`): `HDDImageRegistry` mirrors `DHDImageRegistry` — per-image table and selection keyed on the container URL, `HDDResolvePartition()` binding a partition to a path without calling `select()`, `$=P` listing on `HDDMFile`, and entry URLs that name the partition BY NUMBER. The image root is now the SELECTED partition's directory rather than the partition list; that is the one visible regression, and `partition` / `$=P` are how the list is reached. See the three divergences from DHD in `AGENTS.md` before touching any of it.
- **`HDDMStream::BootSector` had `default_partition` and `last_sector` off by two** — DP is `$03` and `@Last disk sector` is `$04-$07`. Latent on the whole sample corpus, which has DP = 0 either way.
- **The registry's parsing is split from its opening** so it is natively testable: `parseInto(MStream*, Image&)` takes an already-open stream, `parse(url, Image&)` does the `MFSOwner` open with the `s_probing` guard. `hddResolvePartitionIn(const Image&, ...)` is the same split for resolution. `test/native/test_hdd_read` drives both directly — `MFSOwner::File()` and `MFile::getSourceStream()` abort under the native stubs, so anything that reaches them is untestable there.
```

- [ ] **Step 3: Update the spec's open item**

In `docs/superpowers/specs/2026-08-13-hdd-partition-pattern-design.md`, replace the `*To confirm during planning:*` paragraph in the `HDDMStream` section with:

```markdown
**Resolved during planning: the stream does not consult the registry.** The
container URL is recoverable from the stream's `url`, but using it would make
`HDDMStream` call `MFSOwner::File()`, which `abort()`s under the native test
stubs — and `FileContainerStream` sets `url` to a path ending in `.hdd`, so the
lookup would fire and break the existing `test_hdd_read` suite. `HDDMStream`
therefore carries a plain `selected_partition` member (`0xFF` = fall back to
the boot sector's DP) that `HDDMFile::applyPartition()` writes at all four
sites that touch a stream. This is also the better layering: the stream is
about CFS bytes, the registry is about selection policy.
```

- [ ] **Step 4: Commit**

```bash
git add AGENTS.md lib/meatloaf/AGENTS.md docs/superpowers/specs/2026-08-13-hdd-partition-pattern-design.md
git commit -m "docs: record the CFS partition model and its divergences from CMD"
```

---

## Self-Review Notes

**Spec coverage.** Every section maps to a task: §0 boot sector → Task 1; `HDDImageRegistry` + `HDDPartition` → Tasks 2-3; the two numbering spaces and DP conversion → Task 2 (`parseInto`, with `test_registry_numbers_partitions_from_one` and `test_registry_converts_default_partition_from_slot_to_number`); partition 0's DHD semantics → Task 3 (`hddResolvePartitionIn`, `trySelect`, with dedicated tests); the no-`cached_part` divergence → Task 2's class comment, and absent by construction; `HDDMStream` → Task 4; `HDDMFile` → Task 5; `CP<n>` and the console command → Task 6; verification → tests in Tasks 1-4 plus Task 6 Step 6; the breaking change → Task 4 and Task 7.

**Known gap, accepted:** the spec's `HDDResolvePartition()` "hidden partitions are listed, marked, not omitted" rule is implemented (Task 5 sets `file->is_hidden = p.hidden`; Task 6's `View` has no hidden flag because the console lists every entry unconditionally) but is **not** covered by an automated test — neither `HDDMFile` nor the console compiles natively, and no corpus image has a hidden partition. It rests on inspection.

**Type consistency.** `0` is the sentinel in three places and means a consistent thing in each, because partitions are numbered from 1: `HDDMFile::m_part == 0` ("the path named no partition, follow the selection", matching `DHDPartitionMFile::m_part`), `HDDMStream::selected_partition == 0` ("no selection, use the boot sector's DP"), and `hddResolvePartitionIn`'s `v == 0` ("the currently selected partition", matching `DHDResolvePartition`). `Image::default_part` is likewise 0 only when the DP slot is invalid, which `trySelect(0)` correctly rejects.

Distinct from all of those is the raw **slot** index, which is 0-based and appears only in `HDDPartition::slot`, in the DP byte, and inside `parseInto`/`selectPartitionByName("")`. Any code holding a slot must not pass it where a number is expected. `parseInto` takes `MStream*` (raw) while callers hold `shared_ptr`, hence `.get()` at both call sites. `Image::byName` takes `std::string` by value to match `DHDImageRegistry::Image::byName`.

**Numbering agreement is an invariant with no test.** `HDDImageRegistry::parseInto()` and `HDDMStream::selectPartitionByNumber()`/`seekPartitionEntry()` independently walk the same table with the same `isValid()` predicate to derive the same 1-based numbering. Neither corpus image can detect a divergence, because both fill slots contiguously from 0. If the predicate in one is ever changed, change the other.
