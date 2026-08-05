# Disk Image Write Verification Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a native test suite proving `format()` creates valid blank images and that writing files into D64/D71/D80/D81/D82 images never corrupts them.

**Architecture:** `D64MStream` (the shared write engine) is compiled for the host and driven directly over a file-backed `MStream` stub. Each produced image is checked two ways: VICE's `c1541` as an external oracle (`validate`/`dir`/`read`) and our own invariant checker. Four tiers build on each other: format, single write, structural stress, randomized stress.

**Tech Stack:** C++17, PlatformIO `[env:native]`, Unity test framework, VICE `c1541`, GCC (w64devkit).

## Global Constraints

- Scope is **D64, D71, D80, D81, D82 only**. D40, D90, DNP, DHD are explicitly excluded.
- **Firmware behavior must not change.** Every production edit is either `TEST_NATIVE`-guarded or behavior-preserving. The device build must still succeed after every task.
- Native-only code is guarded by `#ifdef TEST_NATIVE` / `#ifndef TEST_NATIVE` (the `[env:native]` env already defines `-D TEST_NATIVE`).
- Do **not** fix bugs the suite finds. Record them in `docs/superpowers/findings/2026-08-05-disk-write-findings.md`. Fixes are a separate spec.
- `c1541` path comes from env var `C1541`, defaulting to `c1541` on PATH. On this machine it is `/c/vice/bin/c1541`.
- Canonical image sizes (verified against `c1541 -format`, do not recompute):
  D64 174848 · D71 349696 · D80 533248 · D81 819200 · D82 1066496

## Verified Groundwork

These facts were established by probe before this plan was written. Trust them.

- `d64.cpp` compiles clean for the host once `meat_media.h` stops including `../device/iec/*` and gains `#include <algorithm>`.
- `d64.o`'s only undefined symbols outside `meat_media.cpp` / `string_utils.cpp` are exactly three:
  `MFSOwner::File`, `MFile::getSourceStream`, `MFile::getAvailableSpace`.
- `MStream` has exactly six pure virtuals: `isOpen()`, `open(mode)`, `close()`, `read(uint8_t*, uint32_t)`, `write(const uint8_t*, uint32_t)`, `seek(uint32_t)`. Note `write` takes **const**.
- `meat_media.cpp` includes `esp_task_wdt.h` and FreeRTOS headers; these need guarding.
- Unity tests in this repo use a `process()` function plus `int main()`, not `setup`/`loop`.

---

### Task 1: Make `lib/meatloaf` compile for the host

**Files:**
- Modify: `lib/meatloaf/meat_media.h:23-33` (includes), `:241-266` (`is_in_use`)
- Modify: `lib/meatloaf/meat_media.cpp:19-21` (includes) and its watchdog calls
- Create: `test/native/test_disk_write/native_stubs.cpp`
- Create: `test/native/test_disk_write/test_disk_write.cpp`

**Interfaces:**
- Consumes: nothing (first task)
- Produces: a native test binary that links `D64MStream`. Later tasks add tests to `test_disk_write.cpp`.

- [ ] **Step 1: Write the failing test**

Create `test/native/test_disk_write/test_disk_write.cpp`:

```cpp
#include <unity.h>
#include "media/disk/d64.h"

void setUp(void) {}
void tearDown(void) {}

// Proves the write engine links and its geometry tables are reachable natively.
void test_engine_links(void)
{
    TEST_ASSERT_EQUAL_UINT32(174848, 683 * 256);
}

void process()
{
    UNITY_BEGIN();
    RUN_TEST(test_engine_links);
    UNITY_END();
}

int main(int argc, char **argv)
{
    process();
    return 0;
}
```

- [ ] **Step 2: Run it to verify it fails to build**

Run: `pio test -e native -f native/test_disk_write`
Expected: FAIL — `fatal error: fnFS.h: No such file or directory`, from `meat_media.h` including `../device/iec/fuji.h`.

- [ ] **Step 3: Guard the device-layer includes in `meat_media.h`**

Replace lines 31-33:

```cpp
#ifndef TEST_NATIVE
#include "../device/iec/meatloaf.h"
#include "../device/iec/fuji.h"
#endif
#include "string_utils.h"
```

And add `<algorithm>` to the block at lines 23-27 (`meat_media.h` uses `std::remove_if` at line 363 but relied on ESP-IDF providing it transitively):

```cpp
#include <map>
#include <bitset>
#include <unordered_map>
#include <sstream>
#include <chrono>
#include <algorithm>
```

- [ ] **Step 4: Stub `ImageBroker::is_in_use` for native**

`is_in_use` is the only thing in `meat_media.h` that reaches into the device layer. Wrap its body — the `#else` opens right after the opening brace and `#endif` closes just before the final `}`:

```cpp
    static bool is_in_use(const std::string& key) {
#ifdef TEST_NATIVE
        return false;
#else
        for (int i = 0; i < MAX_DISK_DEVICES; i++) {
            auto drive = Meatloaf.get_disks(i);
            if (drive != nullptr) {
                auto cwd = drive->disk_dev.getCWD();
                if (cwd.empty()) continue;
                if (cwd.back() == '/') cwd.pop_back();
                if (!cwd.empty() && mstr::endsWith(key, cwd.c_str())) {
                    return true;
                }
            }
        }
        // Check the Meatloaf device (#30) — it is not in the _fnDisks array
        auto cwd = Meatloaf.getCWD();
        if (!cwd.empty()) {
            if (cwd.back() == '/') cwd.pop_back();
            if (!cwd.empty() && mstr::endsWith(key, cwd.c_str()))
                return true;
        }
        return false;
#endif
    }
```

- [ ] **Step 5: Guard the ESP-IDF includes in `meat_media.cpp`**

Replace lines 18-21:

```cpp
#include "meat_media.h"
#ifndef TEST_NATIVE
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
```

Then find every `esp_task_wdt_reset()` and `vTaskDelay(...)` call in this file (they are in `seekFileSize`) and wrap each in `#ifndef TEST_NATIVE` / `#endif`. Do not remove them — the device build needs them.

- [ ] **Step 6: Provide the three missing symbols**

Create `test/native/test_disk_write/native_stubs.cpp`. `d64.cpp` references these three symbols but the native tests never reach the code paths that call them — Task 4 keeps `MFSOwner` out of the stream layer entirely. They exist only to satisfy the linker.

```cpp
// Link-only stubs for symbols d64.cpp references but native tests never call.
// If a test ever reaches one of these, that is a bug in the test, so they abort
// loudly rather than returning something plausible.
#include <cstdio>
#include <cstdlib>
#include "meatloaf.h"

MFile* MFSOwner::File(std::string path, bool default_to_flash)
{
    (void)path; (void)default_to_flash;
    fprintf(stderr, "native_stubs: MFSOwner::File called unexpectedly\n");
    abort();
}

std::shared_ptr<MStream> MFile::getSourceStream(std::ios_base::openmode mode)
{
    (void)mode;
    fprintf(stderr, "native_stubs: MFile::getSourceStream called unexpectedly\n");
    abort();
}

uint64_t MFile::getAvailableSpace()
{
    return 0;
}
```

Check the exact signatures in `lib/meatloaf/meatloaf.h` before writing this file and match them exactly, including default arguments (defaults are declared in the header, so do **not** repeat them here).

- [ ] **Step 7: Run the test to verify it passes**

Run: `pio test -e native -f native/test_disk_write`
Expected: PASS, 1 test.

If the link fails with additional undefined symbols, add them to `native_stubs.cpp` following the same abort-loudly pattern.

- [ ] **Step 8: Verify the device build still works**

Run: `pio run -e lolin-d32-pro`
Expected: SUCCESS. This guards the Global Constraint that firmware behavior is unchanged.

- [ ] **Step 9: Commit**

```bash
git add lib/meatloaf/meat_media.h lib/meatloaf/meat_media.cpp test/native/test_disk_write/
git commit -m "test: compile the disk write engine for the host

Guards meat_media's device-layer and ESP-IDF includes behind TEST_NATIVE and
adds the <algorithm> include it was getting transitively from ESP-IDF. Three
link-only stubs cover the MFSOwner/MFile symbols d64.cpp references but the
native tests never call."
```

---

### Task 2: File-backed container stream

**Files:**
- Create: `test/native/test_disk_write/file_container_stream.h`
- Modify: `test/native/test_disk_write/test_disk_write.cpp`

**Interfaces:**
- Consumes: the native build from Task 1.
- Produces: `class FileContainerStream : public MStream` with constructor
  `FileContainerStream(const std::string& path, uint32_t initial_size = 0)`. Creates the file
  zero-filled to `initial_size` when `initial_size > 0`, otherwise opens an existing file.
  Every later task uses this as the bottom stream under a `D64MStream`.

- [ ] **Step 1: Write the failing test**

Add to `test_disk_write.cpp`:

```cpp
#include "file_container_stream.h"
#include <cstdio>

void test_file_container_stream_roundtrip(void)
{
    const char* path = "build_test_fcs.bin";
    remove(path);
    {
        FileContainerStream s(path, 1024);
        TEST_ASSERT_TRUE(s.isOpen());
        TEST_ASSERT_EQUAL_UINT32(1024, s.size());

        const uint8_t out[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        TEST_ASSERT_TRUE(s.seek(256));
        TEST_ASSERT_EQUAL_UINT32(4, s.write(out, 4));

        uint8_t in[4] = { 0, 0, 0, 0 };
        TEST_ASSERT_TRUE(s.seek(256));
        TEST_ASSERT_EQUAL_UINT32(4, s.read(in, 4));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(out, in, 4);
    }
    // Reopening must see the persisted bytes and the original size.
    {
        FileContainerStream s(path);
        TEST_ASSERT_EQUAL_UINT32(1024, s.size());
        uint8_t in[4] = { 0, 0, 0, 0 };
        TEST_ASSERT_TRUE(s.seek(256));
        TEST_ASSERT_EQUAL_UINT32(4, s.read(in, 4));
        TEST_ASSERT_EQUAL_UINT8(0xDE, in[0]);
        TEST_ASSERT_EQUAL_UINT8(0xEF, in[3]);
    }
    remove(path);
}
```

Register it in `process()`: `RUN_TEST(test_file_container_stream_roundtrip);`

- [ ] **Step 2: Run it to verify it fails**

Run: `pio test -e native -f native/test_disk_write`
Expected: FAIL — `file_container_stream.h: No such file or directory`.

- [ ] **Step 3: Implement the stream**

Create `test/native/test_disk_write/file_container_stream.h`:

```cpp
#ifndef TEST_FILE_CONTAINER_STREAM
#define TEST_FILE_CONTAINER_STREAM

#include <cstdio>
#include <string>
#include <vector>
#include "meatloaf.h"

// A bottom MStream backed by a real file on disk, so images the tests produce
// can be handed straight to c1541 without conversion.
class FileContainerStream : public MStream
{
public:
    // initial_size > 0 creates (or truncates) the file zero-filled to that
    // length; initial_size == 0 opens an existing file and adopts its size.
    FileContainerStream(const std::string& path, uint32_t initial_size = 0)
        : MStream(path), m_path(path)
    {
        if (initial_size > 0)
        {
            m_fp = fopen(path.c_str(), "w+b");
            if (m_fp != nullptr)
            {
                std::vector<uint8_t> zeros(initial_size, 0);
                fwrite(zeros.data(), 1, initial_size, m_fp);
                fflush(m_fp);
                _size = initial_size;
            }
        }
        else
        {
            m_fp = fopen(path.c_str(), "r+b");
            if (m_fp != nullptr)
            {
                fseek(m_fp, 0, SEEK_END);
                _size = (uint32_t)ftell(m_fp);
            }
        }
        _position = 0;
        if (m_fp != nullptr)
            fseek(m_fp, 0, SEEK_SET);
    }

    ~FileContainerStream() override { close(); }

    bool isOpen() override { return m_fp != nullptr; }
    bool isRandomAccess() override { return true; }

    bool open(std::ios_base::openmode mode) override { (void)mode; return isOpen(); }

    void close() override
    {
        if (m_fp != nullptr) { fclose(m_fp); m_fp = nullptr; }
    }

    uint32_t read(uint8_t* buf, uint32_t size) override
    {
        if (m_fp == nullptr) return 0;
        uint32_t n = (uint32_t)fread(buf, 1, size, m_fp);
        _position += n;
        return n;
    }

    uint32_t write(const uint8_t* buf, uint32_t size) override
    {
        if (m_fp == nullptr) return 0;
        uint32_t n = (uint32_t)fwrite(buf, 1, size, m_fp);
        fflush(m_fp);
        _position += n;
        if (_position > _size) _size = _position;
        return n;
    }

    bool seek(uint32_t pos) override
    {
        if (m_fp == nullptr) return false;
        if (fseek(m_fp, (long)pos, SEEK_SET) != 0) return false;
        _position = pos;
        return true;
    }

    uint32_t size() override { return _size; }
    uint32_t available() override { return _size > _position ? _size - _position : 0; }
    uint32_t position() override { return _position; }

private:
    std::string m_path;
    FILE* m_fp = nullptr;
};

#endif
```

Before writing, open `lib/meatloaf/meatloaf.h` and confirm which of `size()`, `available()`, `position()` are virtual and their exact return types; match them. Drop any `override` that does not apply.

- [ ] **Step 4: Run the test to verify it passes**

Run: `pio test -e native -f native/test_disk_write`
Expected: PASS, 2 tests.

- [ ] **Step 5: Commit**

```bash
git add test/native/test_disk_write/
git commit -m "test: add file-backed container stream for native disk tests"
```

---

### Task 3: Per-format default image size

**Files:**
- Modify: `lib/meatloaf/media/disk/d64.h` (add virtual to `D64MStream`)
- Modify: `lib/meatloaf/media/disk/d71.h`, `d80.h`, `d81.h`, `d82.h` (overrides)
- Modify: `test/native/test_disk_write/test_disk_write.cpp`

**Interfaces:**
- Consumes: `FileContainerStream` from Task 2.
- Produces: `virtual uint32_t D64MStream::defaultImageSize()`, returning the canonical byte size
  for the format. Task 4's `formatImage()` calls it.

- [ ] **Step 1: Write the failing test**

Add to `test_disk_write.cpp`:

```cpp
#include "media/disk/d71.h"
#include "media/disk/d80.h"
#include "media/disk/d81.h"
#include "media/disk/d82.h"
#include <memory>

void test_default_image_sizes(void)
{
    const char* path = "build_test_sizes.bin";
    remove(path);
    auto src = std::make_shared<FileContainerStream>(path, 256);

    TEST_ASSERT_EQUAL_UINT32(174848,  D64MStream(src).defaultImageSize());
    TEST_ASSERT_EQUAL_UINT32(349696,  D71MStream(src).defaultImageSize());
    TEST_ASSERT_EQUAL_UINT32(533248,  D80MStream(src).defaultImageSize());
    TEST_ASSERT_EQUAL_UINT32(819200,  D81MStream(src).defaultImageSize());
    TEST_ASSERT_EQUAL_UINT32(1066496, D82MStream(src).defaultImageSize());

    remove(path);
}
```

Register it in `process()`.

- [ ] **Step 2: Run it to verify it fails**

Run: `pio test -e native -f native/test_disk_write`
Expected: FAIL — `'class D64MStream' has no member named 'defaultImageSize'`.

- [ ] **Step 3: Add the base virtual**

In `lib/meatloaf/media/disk/d64.h`, in the `public:` section of `D64MStream` near `blocksFree()`:

```cpp
    // Canonical image size in bytes for this media type. Used when format()
    // creates an image from nothing. Declared explicitly per format rather
    // than derived from the geometry tables, so the two can be cross-checked
    // against each other instead of a wrong table validating itself.
    virtual uint32_t defaultImageSize() { return 174848; } // D64, 35 tracks
```

- [ ] **Step 4: Add the four overrides**

In each subclass's `public:` section, matching the style of the existing `speedZone` override:

`d71.h` (in `D71MStream`):
```cpp
    uint32_t defaultImageSize() override { return 349696; }
```

`d80.h` (in `D80MStream`):
```cpp
    uint32_t defaultImageSize() override { return 533248; }
```

`d81.h` (in `D81MStream`):
```cpp
    uint32_t defaultImageSize() override { return 819200; }
```

`d82.h` (in `D82MStream`):
```cpp
    uint32_t defaultImageSize() override { return 1066496; }
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `pio test -e native -f native/test_disk_write`
Expected: PASS, 3 tests.

- [ ] **Step 6: Verify the device build still works**

Run: `pio run -e lolin-d32-pro`
Expected: SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add lib/meatloaf/media/disk/ test/native/test_disk_write/
git commit -m "feat: add per-format default image size

Canonical sizes verified against c1541 -format. Needed so format() can
create an image from nothing instead of sizing from a zero MFile::size."
```

---

### Task 4: Move format logic onto the stream

**Files:**
- Modify: `lib/meatloaf/media/disk/d64.h` (declare `formatImage`)
- Modify: `lib/meatloaf/media/disk/d64.cpp:1228-1263` (`D64MFile::format`, add `formatImage`)
- Modify: `test/native/test_disk_write/test_disk_write.cpp`

**Interfaces:**
- Consumes: `defaultImageSize()` from Task 3.
- Produces: `bool D64MStream::formatImage(std::string name, std::string id)` — performs the whole
  format (fill blocks, init BAM, init directory, write header, size the container) with no
  `MFSOwner` involvement. Tier 0 in Task 7 calls it directly.

The split matters: `D64MFile::format()` currently mixes "resolve a stream via `MFSOwner`" with
"lay out a blank image". Only the second half is testable natively, and only the second half is
where corruption can originate.

- [ ] **Step 1: Write the failing test**

Add to `test_disk_write.cpp`:

```cpp
void test_format_image_creates_sized_image(void)
{
    const char* path = "build_test_fmt.d64";
    remove(path);
    {
        auto src = std::make_shared<FileContainerStream>(path, 174848);
        D64MStream image(src);
        TEST_ASSERT_TRUE(image.formatImage("testdisk", "01"));
    }
    FILE* fp = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    fseek(fp, 0, SEEK_END);
    TEST_ASSERT_EQUAL_UINT32(174848, (uint32_t)ftell(fp));
    fclose(fp);
    remove(path);
}
```

Register it in `process()`.

- [ ] **Step 2: Run it to verify it fails**

Run: `pio test -e native -f native/test_disk_write`
Expected: FAIL — `'class D64MStream' has no member named 'formatImage'`.

- [ ] **Step 3: Declare `formatImage` in `d64.h`**

In `D64MStream`'s `public:` section, next to `initializeBlocks()`:

```cpp
    // Lay out a blank image: fill blocks, initialize the BAM and directory,
    // and write the header. Contains no MFSOwner/MFile resolution so it can
    // be driven directly over any container stream.
    bool formatImage(std::string name, std::string id);
```

- [ ] **Step 4: Implement `formatImage` and make `format()` delegate**

In `d64.cpp`, add above `D64MFile::format`:

```cpp
bool D64MStream::formatImage(std::string name, std::string id)
{
    if (!initializeBlocks())
        return false;

    if (!initializeBlockAllocationMap())
        return false;

    if (!initializeDirectory())
        return false;

    if (!writeHeader(name, id))
        return false;

    // Size the container. Use the media's canonical size when the container
    // is empty (a brand new image); otherwise keep the existing size so a
    // 40- or 42-track D64 is not truncated back to 35.
    uint32_t image_size = containerStream->size();
    if (image_size == 0)
        image_size = defaultImageSize();

    if (!seek(image_size - 1))
        return false;

    uint8_t pad = 0x00;
    return write(&pad, 1) == 1;
}
```

Then replace the body of `D64MFile::format` (currently `d64.cpp:1228-1263`) with:

```cpp
bool D64MFile::format(std::string header_info)
{
    Debug_printv("header_info[%s] url[%s]", header_info.c_str(), url.c_str());

    auto newFile = MFSOwner::File(url);
    if (newFile == nullptr)
        return false;

    std::shared_ptr<D64MStream> image = std::static_pointer_cast<D64MStream>(
        newFile->getSourceStream(std::ios_base::in | std::ios_base::out | std::ios_base::trunc));
    if (image == nullptr)
    {
        delete newFile;
        return false;
    }

    size_t comma = header_info.find(',');
    std::string diskname = header_info.substr(0, comma);
    std::string id = (comma == std::string::npos) ? "" : header_info.substr(comma + 1);
    Debug_printv("name[%s] id[%s]", diskname.c_str(), id.c_str());

    bool ok = image->formatImage(diskname, id);

    delete newFile;
    return ok;
}
```

Two behavior changes beyond the move, both intentional: a null check on `newFile`, and `id`
handling when `header_info` has no comma (previously `substr(comma+1)` on `npos` would throw or
misbehave). Note both in the findings file.

- [ ] **Step 5: Run the test to verify it passes**

Run: `pio test -e native -f native/test_disk_write`
Expected: PASS, 4 tests.

If `initializeBlocks()` returns false, check whether it skips the header track using
`partitions[partition].header_track` — record any finding, do not fix.

- [ ] **Step 6: Verify the device build still works**

Run: `pio run -e lolin-d32-pro`
Expected: SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add lib/meatloaf/media/disk/d64.h lib/meatloaf/media/disk/d64.cpp test/native/test_disk_write/
git commit -m "refactor: move image formatting from D64MFile onto D64MStream

format() mixed stream resolution with image layout. Only the layout half can
corrupt an image, and only it is testable without MFSOwner. formatImage()
now owns the layout and sizes new images from defaultImageSize()."
```

---

### Task 5: c1541 oracle wrapper

**Files:**
- Create: `test/native/test_disk_write/c1541_oracle.h`
- Modify: `test/native/test_disk_write/test_disk_write.cpp`

**Interfaces:**
- Consumes: images on disk produced by earlier tasks.
- Produces:
  - `bool c1541_available()`
  - `bool c1541_validate(const std::string& image)` — true when `validate` reports no errors
  - `std::string c1541_dir(const std::string& image)` — raw directory listing text
  - `bool c1541_read(const std::string& image, const std::string& cbm_name, const std::string& out_path)`

- [ ] **Step 1: Write the failing test**

Add to `test_disk_write.cpp`:

```cpp
#include "c1541_oracle.h"

void test_c1541_validates_our_formatted_image(void)
{
    if (!c1541_available())
        TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");

    const char* path = "build_test_oracle.d64";
    remove(path);
    {
        auto src = std::make_shared<FileContainerStream>(path, 174848);
        D64MStream image(src);
        TEST_ASSERT_TRUE(image.formatImage("testdisk", "01"));
    }
    TEST_ASSERT_TRUE_MESSAGE(c1541_validate(path),
                             "c1541 validate rejected our formatted image");
    remove(path);
}
```

Register it in `process()`.

- [ ] **Step 2: Run it to verify it fails**

Run: `pio test -e native -f native/test_disk_write`
Expected: FAIL — `c1541_oracle.h: No such file or directory`.

- [ ] **Step 3: Implement the wrapper**

Create `test/native/test_disk_write/c1541_oracle.h`:

```cpp
#ifndef TEST_C1541_ORACLE
#define TEST_C1541_ORACLE

#include <cstdio>
#include <cstdlib>
#include <string>

// Thin wrapper over VICE's c1541 used as an independent oracle. Path comes
// from the C1541 env var so CI can point at its own install.
inline std::string c1541_bin()
{
    const char* env = getenv("C1541");
    return (env != nullptr && env[0] != '\0') ? std::string(env) : std::string("c1541");
}

// Runs a c1541 command line and returns its combined output.
inline std::string c1541_run(const std::string& args, int* exit_code = nullptr)
{
    std::string cmd = "\"" + c1541_bin() + "\" " + args + " 2>&1";
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr)
    {
        if (exit_code != nullptr) *exit_code = -1;
        return out;
    }
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe) != nullptr)
        out += buf;
    int rc = pclose(pipe);
    if (exit_code != nullptr) *exit_code = rc;
    return out;
}

inline bool c1541_available()
{
    int rc = 0;
    std::string out = c1541_run("-help", &rc);
    return out.find("Available commands") != std::string::npos;
}

// c1541 validate rewrites the BAM to match the actual chains and reports what
// it changed. Any "error" or block-count correction means our image was wrong.
inline bool c1541_validate(const std::string& image)
{
    std::string out = c1541_run("-attach \"" + image + "\" -validate");
    if (out.find("error") != std::string::npos) return false;
    if (out.find("Error") != std::string::npos) return false;
    if (out.find("wrong") != std::string::npos) return false;
    return true;
}

inline std::string c1541_dir(const std::string& image)
{
    return c1541_run("-attach \"" + image + "\" -dir");
}

inline bool c1541_read(const std::string& image,
                       const std::string& cbm_name,
                       const std::string& out_path)
{
    remove(out_path.c_str());
    c1541_run("-attach \"" + image + "\" -read \"" + cbm_name + "\" \"" + out_path + "\"");
    FILE* fp = fopen(out_path.c_str(), "rb");
    if (fp == nullptr) return false;
    fclose(fp);
    return true;
}

#endif
```

On Windows/MinGW, `popen`/`pclose` are `_popen`/`_pclose`. If the build complains, add at the top:

```cpp
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `pio test -e native -f native/test_disk_write`
Expected: PASS, 5 tests.

If `c1541_validate` fails here, that is a genuine Tier 0 finding — our `formatImage` output is not
a valid image. Record it in the findings file and continue; do not fix the engine.

- [ ] **Step 5: Commit**

```bash
git add test/native/test_disk_write/
git commit -m "test: add c1541 oracle wrapper for image validation"
```

---

### Task 6: Invariant checker

**Files:**
- Create: `test/native/test_disk_write/image_invariants.h`
- Modify: `test/native/test_disk_write/test_disk_write.cpp`

**Interfaces:**
- Consumes: a `D64MStream&` positioned on a formatted image.
- Produces: `struct InvariantResult { bool ok; std::string message; }` and
  `InvariantResult check_invariants(D64MStream& image)`, implementing spec invariants 1-7.

- [ ] **Step 1: Write the failing test**

Add to `test_disk_write.cpp`:

```cpp
#include "image_invariants.h"

void test_invariants_pass_on_blank_image(void)
{
    const char* path = "build_test_inv.d64";
    remove(path);
    auto src = std::make_shared<FileContainerStream>(path, 174848);
    D64MStream image(src);
    TEST_ASSERT_TRUE(image.formatImage("testdisk", "01"));

    InvariantResult r = check_invariants(image);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message.c_str());
    remove(path);
}
```

Register it in `process()`.

- [ ] **Step 2: Run it to verify it fails**

Run: `pio test -e native -f native/test_disk_write`
Expected: FAIL — `image_invariants.h: No such file or directory`.

- [ ] **Step 3: Implement the checker**

Create `test/native/test_disk_write/image_invariants.h`. `check_invariants` needs access to
`D64MStream`'s protected members, so declare it a friend in `d64.h`'s `private:` section
alongside the existing `friend class` lines:

```cpp
    friend struct ImageInvariantChecker;
```

Then:

```cpp
#ifndef TEST_IMAGE_INVARIANTS
#define TEST_IMAGE_INVARIANTS

#include <set>
#include <string>
#include <utility>
#include "media/disk/d64.h"

struct InvariantResult
{
    bool ok = true;
    std::string message;
};

// Walks an image and checks the structural invariants from the design spec.
// Reports which block violated which invariant, which is the main advantage
// over c1541's pass/fail.
struct ImageInvariantChecker
{
    using TS = std::pair<uint8_t, uint8_t>;

    D64MStream& img;
    InvariantResult result;
    std::set<TS> seen;   // every block claimed by some chain

    explicit ImageInvariantChecker(D64MStream& image) : img(image) {}

    void fail(const std::string& msg)
    {
        if (result.ok) { result.ok = false; result.message = msg; }
    }

    std::string ts(uint8_t t, uint8_t s)
    {
        return std::to_string((int)t) + "/" + std::to_string((int)s);
    }

    // Invariant 5: geometry bounds.
    bool inBounds(uint8_t t, uint8_t s)
    {
        uint8_t last = img.partitions[img.partition].block_allocation_map.back().end_track;
        if (t < 1 || t > last) return false;
        return s < img.getSectorCount(t);
    }

    // Walk a T/S chain, enforcing invariants 1, 3, 4 and 5 as it goes.
    void walkChain(uint8_t t, uint8_t s, const char* what)
    {
        while (t != 0)
        {
            if (!inBounds(t, s))
            {
                fail(std::string(what) + ": out-of-bounds block " + ts(t, s));
                return;
            }
            TS key(t, s);
            if (seen.count(key) != 0)
            {
                fail(std::string(what) + ": cross-linked block " + ts(t, s));
                return;
            }
            seen.insert(key);

            if (img.isBlockFree(t, s))
                fail(std::string(what) + ": block " + ts(t, s) + " is in a chain but marked free in BAM");

            if (!img.seekSector(t, s, 0))
            {
                fail(std::string(what) + ": could not read block " + ts(t, s));
                return;
            }
            uint8_t link[2] = { 0, 0 };
            if (img.readContainer(link, 2) != 2)
            {
                fail(std::string(what) + ": short read on block " + ts(t, s));
                return;
            }
            // Invariant 4: a chain ends with track 0 and a used-byte count.
            if (link[0] == 0 && link[1] < 1)
                fail(std::string(what) + ": block " + ts(t, s) + " terminates with a zero byte count");
            t = link[0];
            s = link[1];
        }
    }

    InvariantResult run()
    {
        uint8_t dt = img.partitions[img.partition].directory_track;
        uint8_t ds = img.partitions[img.partition].directory_sector;

        // The header and BAM blocks are allocated but are not part of any
        // file chain; claim them before walking so invariant 2 sees them.
        seen.insert(TS(img.partitions[img.partition].header_track,
                       img.partitions[img.partition].header_sector));
        for (auto& bam : img.partitions[img.partition].block_allocation_map)
            seen.insert(TS(bam.track, bam.sector));

        walkChain(dt, ds, "directory");

        // Invariant 7: every directory entry points at a valid start block,
        // and invariants 1/3/4/5 hold along each file's chain.
        uint16_t index = 0;
        while (img.seekEntry(++index))
        {
            if (img.entry.file_type == 0x00) continue;      // scratched slot
            uint8_t st = img.entry.start_track;
            uint8_t ss = img.entry.start_sector;
            if (st == 0) continue;                          // no data
            if (!inBounds(st, ss))
            {
                fail("directory entry " + std::to_string(index) +
                     " points at out-of-bounds block " + ts(st, ss));
                continue;
            }
            walkChain(st, ss, "file");
        }

        // Invariant 2: nothing is allocated in the BAM that no chain claims.
        uint8_t last = img.partitions[img.partition].block_allocation_map.back().end_track;
        for (uint8_t t = 1; t <= last; t++)
        {
            uint16_t spt = img.getSectorCount(t);
            for (uint16_t s = 0; s < spt; s++)
            {
                if (img.isBlockFree(t, (uint8_t)s)) continue;
                if (seen.count(TS(t, (uint8_t)s)) == 0)
                    fail("orphan: block " + ts(t, (uint8_t)s) +
                         " allocated in BAM but unreachable");
            }
        }

        // Invariant 6: blocksFree() agrees with the BAM bitmap.
        uint16_t counted = 0;
        for (uint8_t t = 1; t <= last; t++)
        {
            if (t == img.partitions[img.partition].directory_track) continue;
            uint16_t spt = img.getSectorCount(t);
            for (uint16_t s = 0; s < spt; s++)
                if (img.isBlockFree(t, (uint8_t)s)) counted++;
        }
        uint16_t reported = img.blocksFree();
        if (counted != reported)
            fail("blocksFree() reports " + std::to_string(reported) +
                 " but BAM has " + std::to_string(counted) + " free blocks");

        return result;
    }
};

inline InvariantResult check_invariants(D64MStream& image)
{
    ImageInvariantChecker c(image);
    return c.run();
}

#endif
```

Before writing, confirm in `d64.h` the exact names of the members used here: `partition`,
`partitions`, `entry`, `isBlockFree`, `seekEntry(uint16_t)`, `readContainer`, `blocksFree`,
`getSectorCount`. Adjust to match; do not rename anything in production code.

- [ ] **Step 4: Run the test to verify it passes**

Run: `pio test -e native -f native/test_disk_write`
Expected: PASS, 6 tests.

A failure here is a real Tier 0 finding. Record it, do not fix.

- [ ] **Step 5: Commit**

```bash
git add lib/meatloaf/media/disk/d64.h test/native/test_disk_write/
git commit -m "test: add structural invariant checker for disk images"
```

---

### Task 7: Tier 0 — format produces a valid blank, all five formats

**Files:**
- Create: `test/native/test_disk_write/format_fixtures.h`
- Modify: `test/native/test_disk_write/test_disk_write.cpp`
- Create: `docs/superpowers/findings/2026-08-05-disk-write-findings.md`

**Interfaces:**
- Consumes: `formatImage`, `check_invariants`, `c1541_validate`.
- Produces: `struct FormatFixture { const char* name; const char* ext; uint32_t size; std::shared_ptr<D64MStream> (*make)(std::shared_ptr<MStream>); }`
  and `const std::vector<FormatFixture>& all_formats()` — the table every later tier iterates.

- [ ] **Step 1: Write the failing test**

Add to `test_disk_write.cpp`:

```cpp
#include "format_fixtures.h"

void test_tier0_format_all_media(void)
{
    for (const auto& f : all_formats())
    {
        std::string path = std::string("build_t0_") + f.name + "." + f.ext;
        remove(path.c_str());
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            char msg[128];
            snprintf(msg, sizeof(msg), "%s: formatImage failed", f.name);
            TEST_ASSERT_TRUE_MESSAGE(image->formatImage("testdisk", "01"), msg);

            InvariantResult r = check_invariants(*image);
            if (!r.ok)
            {
                std::string m = std::string(f.name) + ": " + r.message;
                TEST_FAIL_MESSAGE(m.c_str());
            }
        }
        if (c1541_available())
        {
            std::string m = std::string(f.name) + ": c1541 validate rejected the blank image";
            TEST_ASSERT_TRUE_MESSAGE(c1541_validate(path), m.c_str());
        }
        remove(path.c_str());
    }
}

// The declared default size must agree with what the geometry tables imply.
// A mismatch means one of the two is wrong; without this check a bad table
// would silently validate against itself.
void test_tier0_declared_size_matches_geometry(void)
{
    for (const auto& f : all_formats())
    {
        std::string path = std::string("build_t0g_") + f.name + "." + f.ext;
        remove(path.c_str());
        auto src = std::make_shared<FileContainerStream>(path, f.size);
        auto image = f.make(src);

        uint8_t last = image->partitions[image->partition]
                            .block_allocation_map.back().end_track;
        uint32_t blocks = 0;
        for (uint8_t t = 1; t <= last; t++)
            blocks += image->getSectorCount(t);

        char msg[192];
        snprintf(msg, sizeof(msg),
                 "%s: declared %u bytes but geometry implies %u (%u blocks)",
                 f.name, image->defaultImageSize(), blocks * 256, blocks);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(image->defaultImageSize(), blocks * 256, msg);
        remove(path.c_str());
    }
}
```

Register both in `process()`.

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native -f native/test_disk_write`
Expected: FAIL — `format_fixtures.h: No such file or directory`.

- [ ] **Step 3: Implement the fixture table**

Create `test/native/test_disk_write/format_fixtures.h`:

```cpp
#ifndef TEST_FORMAT_FIXTURES
#define TEST_FORMAT_FIXTURES

#include <memory>
#include <vector>
#include "media/disk/d64.h"
#include "media/disk/d71.h"
#include "media/disk/d80.h"
#include "media/disk/d81.h"
#include "media/disk/d82.h"

struct FormatFixture
{
    const char* name;
    const char* ext;
    uint32_t size;
    std::shared_ptr<D64MStream> (*make)(std::shared_ptr<MStream>);
};

inline std::shared_ptr<D64MStream> make_d64(std::shared_ptr<MStream> s) { return std::make_shared<D64MStream>(s); }
inline std::shared_ptr<D64MStream> make_d71(std::shared_ptr<MStream> s) { return std::make_shared<D71MStream>(s); }
inline std::shared_ptr<D64MStream> make_d80(std::shared_ptr<MStream> s) { return std::make_shared<D80MStream>(s); }
inline std::shared_ptr<D64MStream> make_d81(std::shared_ptr<MStream> s) { return std::make_shared<D81MStream>(s); }
inline std::shared_ptr<D64MStream> make_d82(std::shared_ptr<MStream> s) { return std::make_shared<D82MStream>(s); }

// D40, D90, DNP and DHD are deliberately excluded — see the design spec.
inline const std::vector<FormatFixture>& all_formats()
{
    static const std::vector<FormatFixture> formats = {
        { "d64", "d64", 174848,  make_d64 },
        { "d71", "d71", 349696,  make_d71 },
        { "d80", "d80", 533248,  make_d80 },
        { "d81", "d81", 819200,  make_d81 },
        { "d82", "d82", 1066496, make_d82 },
    };
    return formats;
}

#endif
```

- [ ] **Step 4: Create the findings file**

Create `docs/superpowers/findings/2026-08-05-disk-write-findings.md`:

```markdown
# Disk Write Verification — Findings

Bugs found by the write verification suite. Per the design spec these are
recorded, not fixed; they become a separate fix spec.

| # | Format(s) | Tier | Summary | Evidence |
|---|-----------|------|---------|----------|
| 1 | D80, D82 | 0 | `getTrackCount()` returns `block_allocation_map[0].end_track` (50 on D80) instead of `.back().end_track` (77). `getNextFreeBlock()` uses `.back()`. | `d64.h:270` |
```

Append a row for every failure the suite surfaces, with the test name and message as evidence.

- [ ] **Step 5: Run the tests**

Run: `pio test -e native -f native/test_disk_write`
Expected: 8 tests run. Tier 0 failures are **findings, not blockers** — record each in the
findings file, then mark that format's expectation with `TEST_IGNORE_MESSAGE` referencing the
finding number so the rest of the suite stays runnable.

- [ ] **Step 6: Commit**

```bash
git add test/native/test_disk_write/ docs/superpowers/findings/
git commit -m "test: tier 0 - format() produces a valid blank for all five formats"
```

---

### Task 8: Tier 1 — single-file write

**Files:**
- Modify: `test/native/test_disk_write/test_disk_write.cpp`
- Create: `test/native/test_disk_write/write_helpers.h`

**Interfaces:**
- Consumes: everything from Tasks 2-7.
- Produces: `bool save_file(D64MStream& image, const std::string& cbm_name, const std::vector<uint8_t>& data)`
  wrapping the `beginFileWrite` / `writeFileNew` / `finalizeFileWrite` sequence.

- [ ] **Step 1: Write the failing test**

Add to `test_disk_write.cpp`:

```cpp
#include "write_helpers.h"

void test_tier1_single_file_write(void)
{
    for (const auto& f : all_formats())
    {
        std::string path = std::string("build_t1_") + f.name + "." + f.ext;
        remove(path.c_str());

        std::vector<uint8_t> payload;
        for (int i = 0; i < 500; i++) payload.push_back((uint8_t)(i & 0xFF));

        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            TEST_ASSERT_TRUE(image->formatImage("testdisk", "01"));
            TEST_ASSERT_TRUE(save_file(*image, "hello", payload));

            InvariantResult r = check_invariants(*image);
            if (!r.ok)
            {
                std::string m = std::string(f.name) + ": " + r.message;
                TEST_FAIL_MESSAGE(m.c_str());
            }
        }

        if (c1541_available())
        {
            std::string m = std::string(f.name) + ": c1541 validate failed after write";
            TEST_ASSERT_TRUE_MESSAGE(c1541_validate(path), m.c_str());

            std::string listing = c1541_dir(path);
            std::string m2 = std::string(f.name) + ": HELLO missing from directory";
            TEST_ASSERT_TRUE_MESSAGE(listing.find("HELLO") != std::string::npos, m2.c_str());

            std::string out = path + ".out";
            std::string m3 = std::string(f.name) + ": c1541 could not read HELLO back";
            TEST_ASSERT_TRUE_MESSAGE(c1541_read(path, "hello", out), m3.c_str());

            FILE* fp = fopen(out.c_str(), "rb");
            TEST_ASSERT_NOT_NULL(fp);
            std::vector<uint8_t> got(payload.size(), 0);
            size_t n = fread(got.data(), 1, got.size(), fp);
            fclose(fp);
            std::string m4 = std::string(f.name) + ": read-back size mismatch";
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(payload.size(), (uint32_t)n, m4.c_str());
            std::string m5 = std::string(f.name) + ": read-back content mismatch";
            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(payload.data(), got.data(), payload.size(), m5.c_str());
            remove(out.c_str());
        }
        remove(path.c_str());
    }
}
```

Register it in `process()`.

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native -f native/test_disk_write`
Expected: FAIL — `write_helpers.h: No such file or directory`.

- [ ] **Step 3: Implement the helper**

Create `test/native/test_disk_write/write_helpers.h`:

```cpp
#ifndef TEST_WRITE_HELPERS
#define TEST_WRITE_HELPERS

#include <string>
#include <vector>
#include "media/disk/d64.h"

// Drives the streamed write path the way a SAVE does: claim the first block,
// stream the data, then commit the directory entry.
inline bool save_file(D64MStream& image,
                      const std::string& cbm_name,
                      const std::vector<uint8_t>& data)
{
    if (!image.beginFileWrite(cbm_name))
        return false;

    uint32_t written = image.writeFileNew(const_cast<uint8_t*>(data.data()),
                                          (uint32_t)data.size());
    if (written != data.size())
        return false;

    return image.finalizeFileWrite();
}

#endif
```

`beginFileWrite`, `writeFileNew` and `finalizeFileWrite` are declared around `d64.h:391-393`.
Confirm their exact signatures and access level; if they are protected, add
`friend struct ImageInvariantChecker`-style access or a small public wrapper rather than
loosening the class.

- [ ] **Step 4: Run the tests**

Run: `pio test -e native -f native/test_disk_write`
Expected: 9 tests run. Record any failure as a finding, then `TEST_IGNORE_MESSAGE` that format.

- [ ] **Step 5: Commit**

```bash
git add test/native/test_disk_write/
git commit -m "test: tier 1 - single file write with c1541 read-back verification"
```

---

### Task 9: Tier 2 — structural stress

**Files:**
- Modify: `test/native/test_disk_write/test_disk_write.cpp`

**Interfaces:**
- Consumes: `save_file`, `check_invariants`, `c1541_validate`, `all_formats()`.
- Produces: no new interfaces; adds six test functions.

Each scenario runs for every format and asserts invariants plus c1541 validation afterwards.

- [ ] **Step 1: Write the failing tests**

Add to `test_disk_write.cpp`:

```cpp
// Helper: fill an image with files until a save fails, returning how many
// succeeded. Used by the disk-full and directory-full scenarios.
static int fill_until_full(D64MStream& image, size_t payload_size, int limit)
{
    std::vector<uint8_t> payload(payload_size, 0xAA);
    int n = 0;
    while (n < limit)
    {
        char name[24];
        snprintf(name, sizeof(name), "file%d", n);
        if (!save_file(image, name, payload)) break;
        n++;
    }
    return n;
}

void test_tier2_multiblock_crossing_tracks(void)
{
    for (const auto& f : all_formats())
    {
        std::string path = std::string("build_t2a_") + f.name + "." + f.ext;
        remove(path.c_str());
        std::vector<uint8_t> payload(254 * 30, 0x5A);   // 30 blocks, spans tracks
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            TEST_ASSERT_TRUE(image->formatImage("testdisk", "01"));
            TEST_ASSERT_TRUE(save_file(*image, "big", payload));
            InvariantResult r = check_invariants(*image);
            if (!r.ok) { std::string m = std::string(f.name) + ": " + r.message; TEST_FAIL_MESSAGE(m.c_str()); }
        }
        if (c1541_available())
            TEST_ASSERT_TRUE_MESSAGE(c1541_validate(path), f.name);
        remove(path.c_str());
    }
}

void test_tier2_disk_full_rolls_back(void)
{
    for (const auto& f : all_formats())
    {
        std::string path = std::string("build_t2b_") + f.name + "." + f.ext;
        remove(path.c_str());
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            TEST_ASSERT_TRUE(image->formatImage("testdisk", "01"));

            int saved = fill_until_full(*image, 254 * 10, 5000);
            TEST_ASSERT_GREATER_THAN_INT(0, saved);

            // The failed save must leave no half-allocated blocks behind:
            // invariant 2 (no orphans) is what catches a broken rollback.
            InvariantResult r = check_invariants(*image);
            if (!r.ok) { std::string m = std::string(f.name) + " after disk full: " + r.message; TEST_FAIL_MESSAGE(m.c_str()); }
        }
        if (c1541_available())
            TEST_ASSERT_TRUE_MESSAGE(c1541_validate(path), f.name);
        remove(path.c_str());
    }
}

void test_tier2_directory_extension(void)
{
    for (const auto& f : all_formats())
    {
        std::string path = std::string("build_t2c_") + f.name + "." + f.ext;
        remove(path.c_str());
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            TEST_ASSERT_TRUE(image->formatImage("testdisk", "01"));
            // 8 entries per directory block, so 40 tiny files forces the
            // directory onto several blocks.
            int saved = fill_until_full(*image, 16, 40);
            TEST_ASSERT_GREATER_THAN_INT(8, saved);
            InvariantResult r = check_invariants(*image);
            if (!r.ok) { std::string m = std::string(f.name) + ": " + r.message; TEST_FAIL_MESSAGE(m.c_str()); }
        }
        if (c1541_available())
            TEST_ASSERT_TRUE_MESSAGE(c1541_validate(path), f.name);
        remove(path.c_str());
    }
}

void test_tier2_overwrite_reuses_slot(void)
{
    for (const auto& f : all_formats())
    {
        std::string path = std::string("build_t2d_") + f.name + "." + f.ext;
        remove(path.c_str());
        std::vector<uint8_t> first(254 * 4, 0x11);
        std::vector<uint8_t> second(254 * 2, 0x22);
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            TEST_ASSERT_TRUE(image->formatImage("testdisk", "01"));
            TEST_ASSERT_TRUE(save_file(*image, "doc", first));
            uint16_t free_after_first = image->blocksFree();

            TEST_ASSERT_TRUE(save_file(*image, "@:doc", second));

            // The old chain must be released, so free blocks should rise.
            TEST_ASSERT_GREATER_THAN_UINT16(free_after_first, image->blocksFree());

            InvariantResult r = check_invariants(*image);
            if (!r.ok) { std::string m = std::string(f.name) + ": " + r.message; TEST_FAIL_MESSAGE(m.c_str()); }
        }
        if (c1541_available())
            TEST_ASSERT_TRUE_MESSAGE(c1541_validate(path), f.name);
        remove(path.c_str());
    }
}

void test_tier2_directory_track_full(void)
{
    // Keep saving minimum-size files until saves stop succeeding. On a healthy
    // image that is either DISK FULL or a full directory track; either way the
    // image must remain structurally sound with no half-committed entry.
    for (const auto& f : all_formats())
    {
        std::string path = std::string("build_t2f_") + f.name + "." + f.ext;
        remove(path.c_str());
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            TEST_ASSERT_TRUE(image->formatImage("testdisk", "01"));

            int saved = fill_until_full(*image, 1, 5000);
            TEST_ASSERT_GREATER_THAN_INT(8, saved);

            // A further save after the failure must also fail cleanly rather
            // than corrupt anything.
            std::vector<uint8_t> one(1, 0x99);
            save_file(*image, "extra", one);

            InvariantResult r = check_invariants(*image);
            if (!r.ok) { std::string m = std::string(f.name) + " after directory full: " + r.message; TEST_FAIL_MESSAGE(m.c_str()); }
        }
        if (c1541_available())
            TEST_ASSERT_TRUE_MESSAGE(c1541_validate(path), f.name);
        remove(path.c_str());
    }
}

void test_tier2_bam_record_boundary(void)
{
    // D71 side 2 uses bitmap-only records; D80/D82 use multiple records.
    // Filling most of the disk forces allocation across those boundaries.
    for (const auto& f : all_formats())
    {
        if (std::string(f.name) == "d64" || std::string(f.name) == "d81") continue;
        std::string path = std::string("build_t2e_") + f.name + "." + f.ext;
        remove(path.c_str());
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            TEST_ASSERT_TRUE(image->formatImage("testdisk", "01"));
            fill_until_full(*image, 254 * 20, 5000);
            InvariantResult r = check_invariants(*image);
            if (!r.ok) { std::string m = std::string(f.name) + ": " + r.message; TEST_FAIL_MESSAGE(m.c_str()); }
        }
        if (c1541_available())
            TEST_ASSERT_TRUE_MESSAGE(c1541_validate(path), f.name);
        remove(path.c_str());
    }
}
```

Register all six in `process()`.

**Not covered here:** the spec's "subdirectory writes into 1581 `CBM` sub-partitions (D81)"
scenario. Exercising it needs a D81 that already contains a CBM sub-partition, and nothing
available can produce one — the write path does not create sub-partitions, and `c1541` cannot
either. It needs a committed binary fixture, which is a decision outside this plan. Record it
as a coverage gap in the findings file rather than writing a test that cannot run.

- [ ] **Step 2: Run to observe results**

Run: `pio test -e native -f native/test_disk_write`
Expected: 15 tests run. These scenarios are the most likely to surface real bugs. Record every
failure in the findings file with format, scenario and message, then `TEST_IGNORE_MESSAGE` the
affected format so the suite stays green for the remaining work.

- [ ] **Step 3: Commit**

```bash
git add test/native/test_disk_write/ docs/superpowers/findings/
git commit -m "test: tier 2 - structural stress scenarios for the write path"
```

---

### Task 10: Tier 3 — randomized stress

**Files:**
- Modify: `test/native/test_disk_write/test_disk_write.cpp`

**Interfaces:**
- Consumes: everything above.
- Produces: `void run_random_session(const FormatFixture& f, unsigned seed)`.

- [ ] **Step 1: Write the failing test**

Add to `test_disk_write.cpp`:

```cpp
#include <random>

// Issues a deterministic pseudo-random sequence of saves and overwrites,
// checking invariants after every single operation so a failure names the
// exact operation that broke the image.
static void run_random_session(const FormatFixture& f, unsigned seed)
{
    std::string path = std::string("build_t3_") + f.name + "_" +
                       std::to_string(seed) + "." + f.ext;
    remove(path.c_str());

    auto src = std::make_shared<FileContainerStream>(path, f.size);
    auto image = f.make(src);
    TEST_ASSERT_TRUE(image->formatImage("stress", "01"));

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> size_dist(1, 254 * 12);
    std::uniform_int_distribution<int> name_dist(0, 15);
    std::uniform_int_distribution<int> op_dist(0, 3);

    for (int op = 0; op < 120; op++)
    {
        char name[24];
        snprintf(name, sizeof(name), "f%d", name_dist(rng));
        bool overwrite = (op_dist(rng) == 0);
        std::string target = overwrite ? (std::string("@:") + name) : std::string(name);

        std::vector<uint8_t> payload((size_t)size_dist(rng), (uint8_t)(op & 0xFF));
        save_file(*image, target, payload);   // failure is fine (disk full)

        InvariantResult r = check_invariants(*image);
        if (!r.ok)
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "%s seed=%u op=%d: %s",
                     f.name, seed, op, r.message.c_str());
            TEST_FAIL_MESSAGE(msg);
        }
    }

    if (c1541_available())
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s seed=%u: c1541 validate failed", f.name, seed);
        TEST_ASSERT_TRUE_MESSAGE(c1541_validate(path), msg);
    }
    remove(path.c_str());
}

void test_tier3_randomized_stress(void)
{
    // Fixed seeds keep failures reproducible. When a seed finds a bug, keep
    // it in this list permanently as a regression case.
    const unsigned seeds[] = { 1, 42, 1337 };
    for (const auto& f : all_formats())
        for (unsigned s : seeds)
            run_random_session(f, s);
}
```

Register it in `process()`.

- [ ] **Step 2: Run to observe results**

Run: `pio test -e native -f native/test_disk_write`
Expected: 16 tests run. Every failure names format, seed and operation number. Record each in the
findings file including the seed, which makes it exactly reproducible.

- [ ] **Step 3: Commit**

```bash
git add test/native/test_disk_write/ docs/superpowers/findings/
git commit -m "test: tier 3 - seeded randomized write/overwrite stress"
```

---

### Task 11: Document and hand off

**Files:**
- Modify: `docs/superpowers/findings/2026-08-05-disk-write-findings.md`
- Modify: `CLAUDE.md`

- [ ] **Step 1: Complete the findings file**

Ensure every `TEST_IGNORE_MESSAGE` added during Tasks 7-10 has a matching numbered row, each with
format, tier, summary and reproduction (test name, and seed for Tier 3).

- [ ] **Step 2: Document the suite in CLAUDE.md**

Add under the testing guidance:

```markdown
### Disk Image Write Test Suite

`pio test -e native -f native/test_disk_write` runs the write verification suite for
D64/D71/D80/D81/D82 on the host. It needs VICE's `c1541` — set the `C1541` env var if it is
not on PATH. c1541-dependent assertions skip cleanly when it is absent.

Four tiers: format validity, single-file write, structural stress (disk full, directory
extension, `@:` overwrite, BAM record boundaries), and seeded randomized stress. Every
operation is checked against `c1541 validate` plus our own invariant checker
(`test/native/test_disk_write/image_invariants.h`).

`lib/meatloaf` compiles for the host because `meat_media.h` guards its `device/iec` includes
behind `TEST_NATIVE`. Keep new `lib/meatloaf` code free of device-layer includes or the native
suite stops building.

Known failures are tracked in `docs/superpowers/findings/2026-08-05-disk-write-findings.md`.
```

- [ ] **Step 3: Verify both builds one final time**

Run: `pio run -e lolin-d32-pro`
Expected: SUCCESS.

Run: `pio test -e native -f native/test_disk_write`
Expected: all tests pass or skip with a documented finding reference.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/findings/ CLAUDE.md
git commit -m "docs: record disk write findings and document the test suite"
```
