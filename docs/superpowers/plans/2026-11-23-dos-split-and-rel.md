# DOS split and native REL files — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the DOS out of `iecDrive::executeData()` into `CbmDos` (+ `CmdHdDos`/`Ide64Dos`) with no behaviour change, then add native REL files on disk images and on plain `.r00` files, plus an opt-in SD2IEC-style header-name lookup.

**Architecture:** `iecDrive` implements a small `DosHost` interface and owns one `CbmDos` chosen by `hdpart::targetFor(cwd).kind`. OPEN-name and `P`-command parsing become pure functions (native-testable). REL is a `RELMStream` doing record semantics over a `RELStorage` adapter supplied by `D64MStream` (side sectors) or `P00MStream` (P00 container).

**Tech Stack:** C++17, ESP-IDF via PlatformIO, Unity native tests, VICE `c1541` as oracle.

**Spec:** `docs/superpowers/specs/2026-11-23-dos-split-and-rel-design.md`

## Global Constraints

- ESP-IDF is `-fno-exceptions`: no throwing `new`, no `std::stoi`; working tables on the heap, never in a stack frame.
- `lib/meatloaf` must not include anything from `lib/device` (native suite stops building). `include/cbm_defines.h` is fine.
- Use the existing status constants: `ST_RECORD_MISSING` (50), `ST_RECORD_OVERFLOW` (51), `ST_FILE_TYPE_MISMATCH` (64), `ST_SYNTAX_INVALID` (31), `ST_WRITE_PROTECT_ON` (26), `ST_DISK_FULL` (72).
- Container streams are opened `in|out`, never plain `out`. A directly constructed media stream must have `mode` set before `seekPath()`.
- `MStream::read()` returns at most one block; callers loop.
- Phase 1 changes NO status code, status text or side effect. When unsure, copy the old code verbatim.
- VDrive routing (`Meatloaf.use_vdrive && m_vdrive`) is untouched; it stays ahead of the DOS in `executeData()` and `open()`.
- REL behaviour follows `lib/vdrive/vdrive-rel.c`, quirks included (spec, "Calls made during review").
- New source files under `lib/device/iec/` are picked up by `GLOB_RECURSE` in `src/CMakeLists.txt`; after adding or removing one, delete `.pio/build/<env>/CMakeCache.txt`.
- Builds: the user runs `pio` themselves. Check `pgrep -f "platformio run"` first, never run two at once, use `~/.platformio/penv/bin/pio`, and redirect output to a file instead of piping it through `tail`. Firmware targets for every phase: `lolin-d32-pro` and `esp32-s3-devkitc-1`. Record `.flash.text` (`xtensa-esp32-elf-size -A`) on `lolin-d32-pro` before Task 1 and after Tasks 4, 9 and 11.
- Native tests: `~/.platformio/penv/bin/pio test -e native -f native/<suite>`. Baseline before Task 1: 5 errored suites (`test_arc_read`, `test_EdUrlParser`, `test_hdd_read`, `test_ps2_keys`, `test_strings`). Anything else erroring is a regression.

## Review Focus

1. **A command that changes the media replaces the DOS object while that same object is still executing** (`CD` → `set_cwd` → DOS kind changes). Expected: no use-after-free; the swap is deferred to the start of the next `executeData()`/`open()`. Test is in Task 3.
2. **A record length of 44 (`,`) or 13 (CR) in `name,L,<len>`.** Expected: parsed as the raw byte, not split on the comma or stripped. Test is in Task 2.
3. **A binary `P` whose record-low or position byte is 13, with or without a trailing CR.** Expected: a trailing CR is stripped only when it is the sixth byte. Test is in Task 2.
4. **Growing a D64 REL past what will fit (720 data blocks on a 1541, or a full disk).** Expected: 52 / 72 status, no blocks leaked, `c1541 -validate` changes nothing. Test is in Task 7.
5. **A CBM name containing `/`, `*`, `?`, `:` or PETSCII graphics (U+E0xx) becomes a host `.r00` name.** Expected: a deterministic, legal host filename that is the same on lookup and on create. Test is in Task 8.

---

## Phase 1 — move the DOS

### Task 1: Hardware baseline transcript

**Files:**
- Create: `test/hardware/dos_commands.sh`
- Create: `test/hardware/dos_baseline_lolin-d32-pro.txt` (captured output)

- [ ] **Step 1: Write the console script.** One command per line, run with the console's `run` command (`#` lines are comments). It starts with `use 8`, then `cd /sd/content/d64` (or any SD folder holding a writable scratch `.d64`), then mounts that image, then one `exec` per family. Commands are lowercase, per AGENTS.md. Families: `i0`, `ui`, `pwd`, `cd:<subdir>`, `cd_`, `m-r0x000009`, `m-w0x000003010203`, `b-p 2 5` (expected 31, no block window), `u1 2 0 18 1` after `open 2 "#"`, `b-r 2 0 18 1`, `f-p 2 0`, `n0:test,01` on a scratch copy, `s0:nofile`, `r0:b=a`, `md:x`, `rd:x`, `cp1` (31 on a d64), `t-r`, `xr:dos1541`, `ej+`, `vd-`, `auth:a,b`, `zz` (unknown command), `p` + binary (today's text form `p2 0 0 1`), and a lone `0x00`. Mount a `.dhd` (`cd` into one under `/sd/content/hd/dhd/`) and repeat `cp1`, `cp0`, `cp256`. Do the same for a `.hdd` under `/sd/content/hd/hdd/`.
- [ ] **Step 2: Capture on the current `main` firmware.** The user flashes it. Run `run /sd/dos_commands.sh` over serial and save the full output into the baseline file.
- [ ] **Step 3: Commit** `test(dos): hardware baseline for command-channel behaviour`.

### Task 2: Pure parsers — OPEN spec and binary `P`

**Files:**
- Create: `lib/device/iec/drive/dos/open_spec.h`, `open_spec.cpp` (no device includes, `<string>`/`<ios>` only)
- Test: `test/native/test_dos_parse/test_dos_parse.cpp` (unity-builds `open_spec.cpp` by relative `#include`, like `test_disk_write/engine_sources.cpp`)

**Interfaces — Produces:**
```cpp
enum class DosFileType : char { Default = 0, Prg = 'P', Seq = 'S', Usr = 'U', Rel = 'L', Del = 'D' };
struct OpenRequest {
    std::string name;                 // exactly the string today's open() ends up with
    std::ios_base::openmode mode;     // same values today's open() computes
    bool overwrite = false;
    DosFileType type = DosFileType::Default;
    uint8_t record_length = 0;        // ,L,<len> raw byte; 0 = not given
    bool direct_access = false;       // "#" / "#<n>"
};
OpenRequest parseOpenSpec(uint8_t channel, const std::string& raw);

struct PositionCommand { uint8_t channel; uint16_t record; uint8_t position; };
bool parsePositionCommand(const uint8_t* data, uint8_t len, PositionCommand& out);
```

- [ ] **Step 1: Write the failing tests.** Cover each rule `iecDrive::open()` applies today (`drive.cpp:896-965`), taking expected values from that code:
  - `parseOpenSpec(1,"FOO").mode == out`
  - `(2,"FOO,S,W")` gives `out`
  - `(2,"FOO,S,R")` gives `in`
  - `(2,"http://x/y")` gives `in|out`; `(0,"http://x/y")` gives `in`
  - `"@0:FOO"`, `":FOO"`, `"@:FOO"` give `overwrite`, name `FOO`; `"0:FOO"` gives name `FOO`, not `overwrite`
  - `"/D/:F"` gives `"D/:F"`; `"/sd/x.d64"` is unchanged
  - `"CD:X"` gives `":X"`; `"CDROM"` is unchanged
  - `"AB\xa0CD"` gives `"AB"`
  - `"#"` and `"#3"` give `direct_access`, while `"#A"` does not
  - `"DATA,L,\x0a"` gives type `Rel`, record length 10
  - **Review Focus 2:** `"DATA,L,\x2c"` gives 44, and `"DATA,L,\x0d"` gives 13
  - `"DATA,L"` gives `Rel` with length 0

  For `parsePositionCommand`:
  - `"P\x62\x05\x00\x03"` gives ch 2, record 5, position 3
  - `"P\x62\x05"` gives position 1
  - a missing hi byte gives 0
  - **Review Focus 3:** `"P\x62\x0d\x00\x01\x0d"` gives record 13 (the trailing CR is dropped), and `"P\x62\x01\x00\x0d"` gives position 13
  - `"P"` alone gives false
- [ ] **Step 2: Run the test and check that it fails.** Run `pio test -e native -f native/test_dos_parse`. Expected: the build fails because `open_spec.h` does not exist yet.
- [ ] **Step 3: Implement both functions.** Move the rules out of `open()` without changing their order. A `,L,` record length is the single byte right after the second comma, so the third field is found by position, not by tokenising. Every other field is parsed exactly as `util_tokenize(name, ',')` does today.
- [ ] **Step 4: Run the test and check that it passes.**
- [ ] **Step 5: Commit** `feat(dos): pure OPEN-spec and P-command parsers`.

### Task 3: `DosHost` + `CbmDos`, with `executeData()` delegating

**Files:**
- Create: `lib/device/iec/drive/dos/dos_host.h`, `cbm_dos.h`, `cbm_dos.cpp`
- Modify: `lib/device/iec/drive.h` (`class iecDrive : public SystemFileDevice, protected DosHost`, adding `std::unique_ptr<CbmDos> m_dos`, `hdpart::Kind m_dosKind`, `void ensureDos()`)
- Modify: `lib/device/iec/drive.cpp:1689-2685` (body moved), `open()` (uses `parseOpenSpec`)

**Interfaces — Produces:**
```cpp
class DosHost {            // everything the moved code touches; add here, never use friend
public:
  virtual ~DosHost() = default;
  virtual void     dosSetStatus(uint8_t code, uint8_t trk = 0, uint8_t sec = 0) = 0;
  virtual void     dosSetStatusMessage(uint8_t code, const std::string& msg) = 0;
  virtual void     dosSetStatusData(const char* data, uint8_t len) = 0;   // SystemFileDevice::setStatus
  virtual uint8_t  dosStatusCode() = 0;
  virtual MFile*   dosCwd() = 0;
  virtual void     dosSetCwd(const std::string& path) = 0;
  virtual iecChannelHandler* dosChannel(uint32_t n) = 0;                 // nullptr if n>15 or closed
  virtual void     dosClearReadBuffer(uint8_t channel) = 0;
  virtual bool     dosPositionChannel(uint32_t ch, uint32_t pos, bool within_block) = 0;
  virtual bool     dosSeekChannelToBlock(uint8_t ch, uint8_t t, uint8_t s, uint8_t extra, bool for_write) = 0;
  virtual void     dosChangePartition(int pnum) = 0;
  virtual void     dosTapeCommand(const std::string& command) = 0;
  virtual driveMemory& dosMemory() = 0;
  virtual void     dosEnableFastLoader(uint8_t family, bool enable) = 0;
  virtual void     dosReset() = 0;
  virtual uint8_t  dosDeviceNumber() = 0;
};

class CbmDos {
public:
  explicit CbmDos(DosHost& host) : m_host(host) {}
  virtual ~CbmDos() = default;
  virtual hdpart::Kind kind() const { return hdpart::Kind::None; }
  void execute(std::string command, const uint8_t* raw, uint8_t rawLen);  // command is CR-stripped and non-empty
protected:
  DosHost& m_host;
  // Each returns true if it consumed the command (old code's `return`), false to fall through (old `break`).
  virtual bool cmdMeatloafPrefix(std::string& command);                        // CD, E?+/-, Dolphin M-R hack
  virtual bool cmdCbm(std::string& command, uint8_t media, size_t colon);     // first switch
  virtual bool cmdSd2iec(std::string& command, size_t colon);                 // second switch
  virtual bool cmdChangePartition(const std::string& command);                // CP / C<shift-P>, see Task 4
  virtual void cmdMeatloafExtended(std::string& command);                     // auth:, else 31
};
```
Breaking these down further inside `cbm_dos.cpp` is up to the builder.

- [ ] **Step 1: Move the code.** `iecDrive::executeData()` keeps the hotplug sync, LED, `clearStatus`/`ST_OK`, the empty check, `VD+/-` and the whole VDrive branch. It then calls `ensureDos(); m_dos->execute(command, data, dataLen);`. Everything after `//#endif` (`drive.cpp:1768`) moves into `CbmDos`, with member accesses rewritten to `m_host.dos*()`. Replace the unbounded `m_channels[(uint8_t)pti[0]]` in the text `P` with `dosChannel()` (spec call). `iecDrive` implements each `dos*()` as a one-line forward. `iecMeatloaf::executeData()` is not touched.
- [ ] **Step 2: Defer the DOS swap (Review Focus 1).** `selectDos()` only records the wanted kind in `m_dosKind`. `ensureDos()` runs at the top of `executeData()`, after the VDrive branch, and at the top of `open()`, and recreates `m_dos` only when the kind differs. Call `selectDos()` from `set_cwd()`, `unmount()` and `synchronizeSdMedia()`. Nothing outside `ensureDos()` deletes `m_dos`.
- [ ] **Step 3: `open()` uses `parseOpenSpec()`** in place of the inline block at `drive.cpp:896-965`. Every later use reads from the `OpenRequest`.
- [ ] **Step 4: Build both firmware targets.** Expected: both link, and `.flash.text` grows by under 2 KB.
- [ ] **Step 5: Hardware check.** The user flashes `lolin-d32-pro` and reruns `dos_commands.sh`. Diff against the baseline: the only allowed difference is the out-of-bounds `P` case. **Review Focus 1:** in one script, run `exec cd:<path-to.dhd>` then `exec cp1` then `exec cd_` then `exec i0` with no reboot or crash in between.
- [ ] **Step 6: Run the native suites** with `pio test -e native`. Expected: the same 5 errored suites as the baseline and nothing new.
- [ ] **Step 7: Commit** `refactor(dos): move command channel into CbmDos behind DosHost`.

### Task 4: `CmdHdDos` and `Ide64Dos`

**Files:**
- Create: `lib/device/iec/drive/dos/cmd_dos.h/.cpp`, `ide64_dos.h/.cpp`
- Modify: `cbm_dos.cpp` (CP moved into `cmdChangePartition`), `drive.cpp` (`ensureDos()` factory)

- [ ] **Step 1: Split out the partition commands.** `CbmDos::cmdChangePartition()` answers exactly what `changePartition()` produces today for `Kind::None`; read it at `drive.cpp:3098` and copy the code it sets. `CmdHdDos` and `Ide64Dos` override it with today's `CP`/`C\xD0` path, return `Kind::CMD`/`Kind::CFS`, and take any other CMD-only or CFS-only command the builder finds in the moved code.
- [ ] **Step 2: Add the factory.** In `ensureDos()`, `Kind::CMD` gives `CmdHdDos`, `Kind::CFS` gives `Ide64Dos`, and anything else gives `CbmDos`.
- [ ] **Step 3: Write down what is left for VDrive removal.** Add a "Follow-up: remaining VDrive-only features" section to the spec, listing each VDrive command or behaviour that has no native equivalent after this plan (inventory `lib/vdrive/vdrive-command.c` against `CbmDos`). List any callers of the old text-form `P` found in `docs/`, the console and `.claude/skills/`.
- [ ] **Step 4: Build both targets and rerun the hardware baseline.** Expected: identical to Task 3's output.
- [ ] **Step 5: Commit** `refactor(dos): CmdHdDos and Ide64Dos selected by media kind`.

## Phase 2 — REL files

### Task 5: `RELMStream` over `RELStorage`

**Files:**
- Create: `lib/meatloaf/media/file/rel.h`, `rel.cpp`
- Test: `test/native/test_rel_stream/test_rel_stream.cpp` (it includes its own `MemRELStorage`)

**Interfaces — Produces:**
```cpp
class RELStorage {
public:
  virtual ~RELStorage() = default;
  virtual uint32_t size() = 0;                                          // bytes of record data
  virtual uint32_t readAt(uint32_t off, uint8_t* buf, uint32_t n) = 0;
  virtual uint32_t writeAt(uint32_t off, const uint8_t* buf, uint32_t n) = 0;
  virtual uint8_t  grow(uint32_t new_size) = 0;   // ST_OK, or the CBM status (52/72/26); content of new bytes unspecified
  virtual bool     writable() = 0;
};
class RELMStream : public MStream {
public:
  RELMStream(std::unique_ptr<RELStorage> storage, uint8_t record_length);
  uint8_t  recordLength() const;
  uint32_t recordCount();                                  // storage size / record length
  uint8_t  position(uint16_t record, uint8_t pos);         // CBM P: 1-based, 0==1; returns ST_OK / 50 / 51
  uint32_t read(uint8_t* buf, uint32_t size) override;     // current record only; 0 = record end
  uint32_t write(const uint8_t* buf, uint32_t size) override;
  void     endRecord();                                    // end of a PRINT#: pad, advance
  bool     recordExhausted() const;
  uint8_t  status() const;                                 // last CBM status
  // plus the remaining MStream pure virtuals (isOpen/open/close/seek) as trivial forwards
};
```

- [ ] **Step 1: Write the failing tests.** Each one cites the `vdrive-rel.c` function whose behaviour it pins:
  - `position(0,0)` equals `position(1,1)`
  - `position(1,len+1)` gives 51
  - `position(n>count, 1)` gives 50, then a `write` grows the file to `n` records and the new records read back as `$FF` followed by `$00`s (`vdrive_rel_fillrecord`)
  - on an empty new file, `position(1,1)` gives `ST_OK`
  - reading a record `AB\0\0` returns `AB` and then 0 (`vdrive_rel_position_internal`'s trailing-zero scan)
  - reading past the last record returns a single `0x0D` with status 50
  - writing `len+3` bytes keeps `len` bytes and gives 51
  - `endRecord()` after 2 bytes pads with `$00` and the next write goes to the next record
  - `grow` returning 72 leaves `recordCount()` unchanged
  - `writable()==false` makes `write` return 0 with status 26
- [ ] **Step 2: Run the test and check that it fails.** Run `pio test -e native -f native/test_rel_stream`. Expected: the build fails.
- [ ] **Step 3: Implement it.** Hold one record buffer, sized to the record length, on the heap.
- [ ] **Step 4: Run the test and check that it passes.**
- [ ] **Step 5: Commit** `feat(rel): RELMStream record layer`.

### Task 6: Reading existing REL files in D64/D71/D81/DNP

**Files:**
- Modify: `lib/meatloaf/meatloaf.h` (`MStream`: `virtual uint8_t relRecordLength() { return 0; }`, `virtual std::unique_ptr<RELStorage> relStorage() { return nullptr; }`)
- Modify: `lib/meatloaf/media/disk/d64.h/.cpp` (adds a `D64RelStorage` and side-sector loading)
- Test: `test/native/test_rel_d64/` (`engine_sources.cpp` as in `test_disk_write` plus `rel.cpp`, test file, reusing `../test_disk_write/c1541_oracle.h`, `file_container_stream.h` and `image_invariants.h` by relative include)

**Interfaces — Produces:** for a selected entry with `file_type & 0x07 == 4`, `D64MStream::relRecordLength()` returns `entry.rel_record_length`, and `relStorage()` returns a `RELStorage` whose `readAt` maps a data offset to its block through the side sectors (D81/DNP: through the super side sector) — **never** by walking the chain.

- [ ] **Step 1: Write the failing tests.** Fixtures are made at test time with `c1541 -format "t,01" <d64|d81> <img> -write <host> "data,l,<len>"`, where the host file holds `N*len` bytes of a known pattern. Cases:
  - a D64 with len 10, 3 records
  - a D64 with len 254 and 130 records, which crosses one side-sector group
  - a D81 with len 100 and enough records to need a second super-side-sector group (over 720 data blocks)

  For each: `relRecordLength()` equals len, every record reads back the pattern, and the read count of the storage matches the size `c1541 -list` reports.
- [ ] **Step 2: Run the test and check that it fails.**
- [ ] **Step 3: Implement it.** Side-sector layout per `REL.TXT` (cited in `d64.h:23`): bytes 0-1 next T/S, 2 the side-sector number, 3 the record length, 4-15 the six side-sector T/S, 16-255 120 data T/S. The 1581 super side sector: bytes 0-1 the first side sector, 2 `$FE`, then group T/S. Load the side sectors on the heap when the REL entry is selected.
- [ ] **Step 4: Run the test and check that it passes.**
- [ ] **Step 5: Commit** `feat(rel): read REL files in D64-family images via side sectors`.

### Task 7: Creating, growing and scratching REL files in images

**Files:**
- Modify: `d64.h/.cpp` (`D64RelStorage::grow`, creating a REL entry, `removeFile` freeing side sectors), `lib/meatloaf/meatloaf.h` (`MFile`: `virtual std::shared_ptr<MStream> createRelStream(uint8_t record_length) { return nullptr; }`), `D64MFile` override
- Test: `test/native/test_rel_d64/`

- [ ] **Step 1: Write the failing tests.** For D64, D71, D81 and DNP:
  - create `data` with len 20, write 5 records, and check that `c1541 -read` returns the same bytes, `c1541 -list` shows `rel`, and `c1541_validate` leaves the BAM unchanged (as in `test_disk_write`)
  - grow from 5 records to 1000, which crosses side-sector groups, then run the same three checks
  - scratch it, after which blocks free equal the count before it was created
  - **Review Focus 4:** on a D64, grow past 720 data blocks, which gives 52 with the BAM unchanged from before the attempt; and fill the disk and grow, which gives 72 with no leaked blocks (`check_invariants`)
- [ ] **Step 2: Run the tests and check that they fail.**
- [ ] **Step 3: Implement it.** Port `vdrive_rel_grow`, `vdrive_rel_add_sector` and `vdrive_rel_open_new` (`vdrive-rel.c:157-850`). Allocate with `getNextFreeBlock()`. The directory entry is type `$84` with `rel_start_*` and `rel_record_length`, and the block count includes the side sectors. If a grow fails, deallocate every block it claimed.
- [ ] **Step 4: Run the tests and check that they pass.**
- [ ] **Step 5: Commit** `feat(rel): create, grow and scratch REL files in images`.

### Task 8: `.r00` files on plain filesystems

**Files:**
- Modify: `lib/meatloaf/media/file/p00.h` (+ `p00.cpp` if it gets long): random access, write, `relRecordLength()` = header `rel_flag`, `relStorage()` at offset 26, and `P00MFile::createRelStream`, which writes the header (`"C64File\0"`, the 16-byte name padded with `$00`, `rel_flag`)
- Modify: `rel.h/.cpp`: `std::string relHostFilename(const std::string& cbm_name_utf8);`
- Test: `test/native/test_rel_stream/` (host name mapping), `test/native/test_rel_p00/` (container round trip on a temp file)

- [ ] **Step 1: Write the failing tests.**
  - `relHostFilename("SCORES")` gives `"SCORES.r00"`
  - **Review Focus 5:** each of `/ \ : * ? " < > |`, each control byte and each code point ≥ U+E000 maps to `_`; the result is the same on every call; `""` gives `"_.r00"`
  - a P00 round trip: create with len 30, write 4 records, reopen, and check that `relRecordLength()` is 30 and the records match byte for byte
  - the header name holds the CBM name
  - a storage whose container is not writable gives 26 on write
- [ ] **Step 2: Run the tests and check that they fail.**
- [ ] **Step 3: Implement it.**
- [ ] **Step 4: Run the tests and check that they pass.**
- [ ] **Step 5: Commit** `feat(rel): read/write REL files as P00 .r00 containers`.

### Task 9: Drive wiring — `,L` OPEN, REL channel, binary `P`

**Files:**
- Modify: `lib/device/iec/drive.h/.cpp` (adds `iecChannelHandlerRel` and the REL branch in `open()`), `cbm_dos.cpp` (`P` → `parsePositionCommand`)

- [ ] **Step 1: Wrap REL streams in `open()`.** After the stream resolves: if `stream->relRecordLength() != 0`, wrap it in `RELMStream(stream->relStorage(), len)` and give it an `iecChannelHandlerRel`. If `req.type == Rel`, the file does not exist and `req.record_length != 0`, use `cwdEntry->createRelStream(len)`. On a plain filesystem, also try `relHostFilename(name)` before answering 62. An existing file opened with `,L` that is not REL, and a length mismatch, give the status `vdrive_rel_open` gives (`vdrive-rel.c:851`).
- [ ] **Step 2: Add `iecChannelHandlerRel`.** `readBufferData()` fills only the current record, and the drive's `read()` raises EOI on its last byte, as in `vdrive_rel_read`. `write()` calls `RELMStream::endRecord()` when the bus write carries `eoi`. Confirm that this matches the end of a `PRINT#` by checking where `VDriveClass` calls `vdrive_rel_listen`, and record what you find in the commit message. The status from a REL operation goes through `dosSetStatus`.
- [ ] **Step 3: Replace the text `P`.** Decode the raw `data`/`dataLen` (not the CR-stripped string) with `parsePositionCommand`, call `RELMStream::position()` on that channel, then `repositioned()` the handler. On a non-REL channel, answer as `vdrive_rel_position` does for a non-REL buffer.
- [ ] **Step 4: Build both targets** and record `.flash.text`.
- [ ] **Step 5: Hardware check with VDrive off** (`exec vd-`), from the console:
  - `open 2 data,l,` + `0x14`, then `write 15 p` + `0x620x050x000x01`, then `write 2 hello`, `close 2`
  - reopen with `open 2 data`, then `write 15 p` + `0x620x050x000x01` and `read 2`, which should give `hello`
  - check with `c1541 -read` on a copy of the image pulled off SD
  - repeat in an SD folder (expect `data.r00` to be created)
  - then a C64 BASIC REL program: write 10 records, read record 7
- [ ] **Step 6: Commit** `feat(rel): REL channels, ,L OPEN and binary P on iecDrive`.

## Phase 3 — header-name option

### Task 10: `preferences.x00_header_names`

**Files:**
- Modify: `lib/device/iec/drive.cpp` (open fallback and `iecChannelHandlerDir` listing), `data/BUILD_IEC.*/.sys/config.json` templates (add `"x00_header_names": 0` under `preferences`)
- Test: hardware only. The drive layer is not native-built, so record that limitation in the commit.

- [ ] **Step 1: Read the option once per OPEN or listing** with `json_int(mlConfig["preferences"], "x00_header_names", 0)`.
- [ ] **Step 2: Listing.** When the option is on and the entry is on a plain filesystem (empty `pathInStream`, no container) with extension `.p00/.s00/.u00/.r00` (case-insensitive), read 26 bytes of its header. If the signature is `C64File`, show the header name with type PRG/SEQ/USR/REL.
- [ ] **Step 3: Lookup.** When the option is on and the plain lookup misses, scan the cwd for those extensions and match the header name with `mstr::compareFilename()` (wildcards honoured). The first match wins.
- [ ] **Step 4: Hardware check.** Copy in `x.r00` whose header name is `SCORES`. With the option off, `LOAD"$"` shows `x.r00`; with it on, it shows `scores` (REL) and `open 2 scores` works. Time `ls` of an SD folder of 500 files with the option on and off, and put both numbers in the commit.
- [ ] **Step 5: Commit** `feat(dos): optional x00 lookup and listing by header name`.

### Task 11: Docs and final verification

**Files:**
- Modify: `AGENTS.md` (a Recent Changes entry plus Important Notes: the DOS layer and `DosHost` rule, the deferred DOS swap, REL storage seams, `.r00` naming, the config key), `lib/meatloaf/AGENTS.md` (`RELMStream`/`RELStorage`, side-sector rules)

- [ ] **Step 1: Run the full native suite.** Expected: the same 5 baseline errors, and the new suites (`test_dos_parse`, `test_rel_stream`, `test_rel_d64`, `test_rel_p00`) pass with no skips.
- [ ] **Step 2: Rerun the Phase 1 hardware baseline.** Expected: unchanged apart from `P`, which now answers in its binary form. Record the final `.flash.text` and IRAM headroom for `lolin-d32-pro`.
- [ ] **Step 3: Update the docs** with what was measured, not what was expected, and list everything not hardware-verified.
- [ ] **Step 4: Commit** `docs: DOS layer and native REL files`.
