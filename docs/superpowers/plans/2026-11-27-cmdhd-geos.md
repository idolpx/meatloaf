# GEOS from a CMD HD image — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `GEOS` followed by `M-E $040F` on a CMD HD/FD image starts Meatloaf's existing 1581 GEOS session, so CMD's gateWay drivers (HardDrive, HD1581) boot from `/sd/hdbackup.dhd`.

**Architecture:** CMD's drivers upload no drive code — the turbo is resident in a real CMD HD's DOS. So the `GEOS` command arms the loader detector as though the 1581 GEOS upload had been recognised (`IECFastLoadDetect::setResident()`); the following `M-E $040F` then resolves through the existing previous-loader fallback and handler row (`0x040f -> IEC_FLV_GEOS_S23_1581`) into `runGeosLoader()`. Nothing in the session, the byte layer or the CRC path changes.

**Tech Stack:** C++17, ESP-IDF via PlatformIO, Unity native tests.

**Spec:** `docs/superpowers/specs/2026-11-27-cmdhd-geos-design.md`

## Global Constraints

- Variant `IEC_FLV_GEOS_S23_1581` (14), rx/tx `IEC_FLRX_GEOS_2MHZ` (2) — from `lib/bus/iec/fastload.h`.
- `GEOS` is answered only by `CmdHdDos` (CMD HD / FD images); every other DOS keeps answering `31,INVALID COMMAND`.
- No bus-timing code in `lib/device/iec/drive/dos/`; the DOS reaches the drive only through `DosHost`.
- CRC detection of uploaded loaders is unchanged.
- `IECFastLoadDetect` code that touches the new state lives under `#ifdef IEC_SUPPORT_SOFTLOAD`, and the `#else` stub block at the end of `fastload.cpp` gets a matching no-op.
- Builds: never two at once — check `pgrep -f "[p]io run"` first; use `PATH=~/.platformio/penv/bin:$PATH pio ...`; never pipe pio through `tail`/`head`, redirect to a file and grep it.
- No commit is made without the user asking (harness rule); commit steps below are "stage and propose the message".

## Review Focus

1. **Re-entry after quit.** gateWay sends `GEOS` once, then a bare `M-E $040F` on every later turbo entry. Expected: each re-entry starts the session. Pinned by `test_rearm_survives_repeated_exec` (Task 1).
2. **A real upload after `GEOS`.** Some other program uploads code with `M-W` after GEOS was armed. Expected: the armed state is dropped; that program's own CRC decides. Pinned by `test_mw_after_arm_clears_it` (Task 1).
3. **Drive RESET / `I0`.** gateWay clears its "GEOS sent" flag after `I0:` and re-sends `GEOS`, so nothing needs clearing on `I`; a bus RESET runs `IECFastLoadDetect::reset()` only at `begin()`. Expected: harmless — the next `M-E $040F` after a C64 reset is preceded by a fresh `GEOS`. Pinned by `test_reset_clears_arm` (Task 1) for the detector half.
4. **`GEOS` on a non-CMD image** (D64, D81, DNP). Expected: `31,INVALID COMMAND`, nothing armed. Checked on hardware in Task 3, step 5.
5. **GEOS fast loader switched off by the user.** Expected: `M-E $040F` is reported but not started (the existing `isFastLoaderEnabled()` check in `isFastLoaderRequest()`), the C64 falls back. No new code — noted for the reviewer.

---

### Task 1: Detector can be armed with a resident loader

**Files:**
- Modify: `lib/bus/iec/fastload.h` (class `IECFastLoadDetect`, public section after `reset()`)
- Modify: `lib/bus/iec/fastload.cpp` (definition after `IECFastLoadDetect::reset()`; no-op in the `#else // !IEC_SUPPORT_SOFTLOAD` block)
- Create: `test/native/test_archive_extract/host/soc/gpio_num.h` (host stub; that directory is already on every native suite's `-I` path)
- Create: `test/native/test_fastload_detect/test_fastload_detect.cpp`

**Interfaces:**
- Produces: `void IECFastLoadDetect::setResident(uint8_t variant, uint8_t rxtx);` — makes `variant` the previous-round loader and `rxtx` the current rx/tx, so the next `memExec()` that detects nothing falls back to it. Does not touch `m_crc` or `m_detected`.

- [ ] **Step 1: Create the host GPIO stub**

`test/native/test_archive_extract/host/soc/gpio_num.h`:
```cpp
#pragma once
// Host stub: include/pinmap_defaults.h includes <soc/gpio_num.h>, which is
// what lets lib/bus/iec/fastload.cpp compile in the native suite.
typedef int gpio_num_t;
#define GPIO_NUM_NC (-1)
```

- [ ] **Step 2: Write the failing tests**

`test/native/test_fastload_detect/test_fastload_detect.cpp` — unity-builds the detector with the implementation tables compiled in:
```cpp
#define EXTRA_FASTLOADERS
#include "../../../lib/bus/iec/fastload.cpp"
#include <unity.h>

static IECFastLoadDetect d;
void setUp()    { d.reset(); }
void tearDown() {}

static uint8_t exec040f(bool *matched) { uint8_t p = 0; return d.memExec(0x040f, &p, matched); }

void test_unarmed_exec_matches_nothing() {
    bool m = true;
    TEST_ASSERT_EQUAL_UINT8(IEC_FLV_NONE, exec040f(&m));
    TEST_ASSERT_FALSE(m);
}
void test_armed_exec_starts_geos_1581() {
    d.setResident(IEC_FLV_GEOS_S23_1581, IEC_FLRX_GEOS_2MHZ);
    TEST_ASSERT_EQUAL_UINT8(IEC_FLRX_GEOS_2MHZ, d.rxtx());   // read before memExec, as isFastLoaderRequest() does
    bool m = false;
    TEST_ASSERT_EQUAL_UINT8(IEC_FLV_GEOS_S23_1581, exec040f(&m));
    TEST_ASSERT_TRUE(m);
}
void test_rearm_survives_repeated_exec() {
    d.setResident(IEC_FLV_GEOS_S23_1581, IEC_FLRX_GEOS_2MHZ);
    bool m = false;
    exec040f(&m);
    for (int i = 0; i < 3; i++) {
        m = false;
        TEST_ASSERT_EQUAL_UINT8(IEC_FLV_GEOS_S23_1581, exec040f(&m));
        TEST_ASSERT_TRUE(m);
        TEST_ASSERT_EQUAL_UINT8(IEC_FLRX_GEOS_2MHZ, d.rxtx());
    }
}
void test_mw_after_arm_clears_it() {
    d.setResident(IEC_FLV_GEOS_S23_1581, IEC_FLRX_GEOS_2MHZ);
    const uint8_t junk[4] = { 0xea, 0xea, 0xea, 0x60 };
    d.memWrite(0x0500, junk, sizeof junk);
    bool m = true;
    TEST_ASSERT_EQUAL_UINT8(IEC_FLV_NONE, exec040f(&m));
    TEST_ASSERT_FALSE(m);
}
void test_reset_clears_arm() {
    d.setResident(IEC_FLV_GEOS_S23_1581, IEC_FLRX_GEOS_2MHZ);
    d.reset();
    bool m = true;
    TEST_ASSERT_EQUAL_UINT8(IEC_FLV_NONE, exec040f(&m));
    TEST_ASSERT_FALSE(m);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_unarmed_exec_matches_nothing);
    RUN_TEST(test_armed_exec_starts_geos_1581);
    RUN_TEST(test_rearm_survives_repeated_exec);
    RUN_TEST(test_mw_after_arm_clears_it);
    RUN_TEST(test_reset_clears_arm);
    return UNITY_END();
}
```

- [ ] **Step 3: Run it to verify it fails**

Run: `PATH=~/.platformio/penv/bin:$PATH pio test -e native -f native/test_fastload_detect > /tmp/fl.log 2>&1; grep -E "error|PASS|FAIL" /tmp/fl.log`
Expected: compile error — `no member named 'setResident' in 'IECFastLoadDetect'`. (If the error is instead about headers, fix the include path before continuing; it compiled on the host with `-I lib/meatloaf` plus the stub.)

- [ ] **Step 4: Implement `setResident()`**

Declaration in `fastload.h` with a comment saying why it exists (CMD drives keep their GEOS turbo in DOS ROM, so no upload reaches the CRC). Definition under `IEC_SUPPORT_SOFTLOAD`: `m_previous = variant; m_rxtx = rxtx;`. In the `#else` block: `void IECFastLoadDetect::setResident(uint8_t, uint8_t) {}`.

- [ ] **Step 5: Run it to verify it passes**

Run: same command as Step 3.
Expected: `5 Tests 0 Failures 0 Ignored`, suite PASSED.

- [ ] **Step 6: Mutation check**

Temporarily drop `m_rxtx = rxtx;` → `test_armed_exec_starts_geos_1581` must fail; drop `m_previous = variant;` → the three armed tests must fail. Restore and re-run to green.

- [ ] **Step 7: Stage and propose commit**

`git add lib/bus/iec/fastload.h lib/bus/iec/fastload.cpp test/native/test_fastload_detect test/native/test_archive_extract/host/soc/gpio_num.h` — message `feat(fastload): arm the detector with a resident loader`.

---

### Task 2: `GEOS` command on CMD HD/FD images, plus logging of unmatched M-E

**Files:**
- Modify: `lib/bus/iec/IECFileDevice.h` / `.cpp` (protected `armResidentFastLoader()`, virtual `fastLoaderNotMatched()`, call site in `isFastLoaderRequest()` just before the final `m_uploadCtr = 0; return false;`)
- Modify: `lib/device/iec/drive/dos/dos_host.h` (new pure virtual)
- Modify: `lib/device/iec/drive.h` / `drive.cpp` (implement the `DosHost` method next to `dosEnableFastLoader()` at `drive.cpp:2666`; override `fastLoaderNotMatched()` next to `iecDrive::startFastLoader()`)
- Modify: `lib/device/iec/drive/dos/cbm_dos.h`, `sd2iec_dos.cpp` (virtual `cmdGeos()`, default 31, dispatched from the `case 'G':` at `sd2iec_dos.cpp:63`)
- Modify: `lib/device/iec/drive/dos/cmd_dos.h` / `cmd_dos.cpp` (override)

**Interfaces:**
- Consumes: `IECFastLoadDetect::setResident(uint8_t variant, uint8_t rxtx)` (Task 1).
- Produces:
  - `protected: void IECFileDevice::armResidentFastLoader(uint8_t variant, uint8_t rxtx);` — forwards to `m_fastload.setResident()`; empty without `IEC_SUPPORT_SOFTLOAD`.
  - `protected: virtual void IECFileDevice::fastLoaderNotMatched(uint16_t address, uint16_t crc) {}` — called for an `M-E` that matched no row and no variant (under `IEC_SUPPORT_SOFTLOAD`).
  - `virtual void DosHost::dosArmResidentFastLoader(uint8_t variant, uint8_t rxtx) = 0;`
  - `virtual void CbmDos::cmdGeos();` — default `m_host.dosSetStatus(ST_SYNTAX_INVALID)`.

- [ ] **Step 1: Dispatch `GEOS`**

In `CbmDos::cmdSd2iec()`'s `case 'G':`, before the `G-P` test: if `command` starts with `"GEOS"` and is exactly that (size 4, or size 5 with `command[4] == '\0'` — the driver sends `GEOS\0`, and the CR-strip leaves the NUL), call `cmdGeos()` and `return true`.

- [ ] **Step 2: `CmdHdDos::cmdGeos()`**

`m_host.dosArmResidentFastLoader(IEC_FLV_GEOS_S23_1581, IEC_FLRX_GEOS_2MHZ)` then `m_host.dosSetStatus(ST_OK)`. Comment: a real CMD HD/FD carries a 1581-compatible GEOS turbo in its DOS; CMD's GEOS drivers send `GEOS` and then `M-E $040F` without uploading anything (found by disassembling GEOBOOT's HardDrive driver; HD1581 does the same). Include `fastload.h` for the constants.

- [ ] **Step 3: `iecDrive::dosArmResidentFastLoader()`**

Calls `armResidentFastLoader(variant, rxtx)` and logs `Debug_printv("resident fast loader armed: %s", iecFastLoadName(variant))`.

- [ ] **Step 4: Unmatched-M-E logging**

In `IECFileDevice::isFastLoaderRequest()`, under `IEC_SUPPORT_SOFTLOAD`, when `flIsExec && !flMatched && flVariant == IEC_FLV_NONE`, call `fastLoaderNotMatched(flAddress, flCrc)`. `iecDrive` overrides it with `Debug_printv("M-E address[%04X] crc[%04X] - no known fast loader", address, crc)`, and the existing `IEC_FLV_NONE` branch in `iecDrive::startFastLoader()` (now unreachable for that case) is removed. Do NOT widen the `startFastLoader()` dispatch condition — that would offer unmatched `M-E`s to the catch-all claimers (Ultraboot, Spindle).

- [ ] **Step 5: Build**

Run: `pgrep -f "[p]io run"` (must print nothing), then `PATH=~/.platformio/penv/bin:$PATH pio run -e <the user's board env> > /tmp/geos_build.log 2>&1; grep -E "error|SUCCESS|FAILED|overflow" /tmp/geos_build.log`
Expected: `SUCCESS`, no `iram0` overflow. Also `pio test -e native -f native/test_fastload_detect -f native/test_dos_parse` still passes.

- [ ] **Step 6: Update AGENTS.md**

Correct the Important Notes line claiming `startFastLoader()` "is called for every `M-E` that reaches the dispatch, including the ones that matched nothing" — unmatched ones now go to `fastLoaderNotMatched()`. Add one Important Notes entry: CMD drives' GEOS turbo is resident; `GEOS` arms `IEC_FLV_GEOS_S23_1581`/`IEC_FLRX_GEOS_2MHZ` via `setResident()`; native partitions over 127 tracks are an unverified limit (track bit 7 = link-only read).

- [ ] **Step 7: Stage and propose commit**

Message: `feat(dos): GEOS command starts the resident 1581 GEOS turbo on CMD images`.

---

### Task 3: Hardware verification on the C64 Ultimate

No code. Flash the Task 2 build (`pio run -e <env> -t upload`, after `pgrep`), keep the serial log.

- [ ] **Step 1:** Drive 8 → `/sd/hdbackup.dhd`, `CP20`, `LOAD"GEOBOOT",8,1` / `RUN` (or the usual boot). Expected log: `resident fast loader armed: ...`, then `M-E address[040F]` starting the GEOS 1581 session; gateWay's desktop appears.
- [ ] **Step 2:** Open a file in gateWay (e.g. `Notes`, or `note pad`). Expected: it opens, no read errors.
- [ ] **Step 3:** Save a change (e.g. edit `Notes`). Then on the host: fetch the image and confirm the partition still lists (`partition` / `ls` from the console after `exec cp20`), and the file's block count changed plausibly.
- [ ] **Step 4:** Repeat Steps 1-2 on partition 38 (1581-type, `HD1581` driver): `CP38`.
- [ ] **Step 5:** On a D64, `exec geos` from the console → `31,INVALID COMMAND`.
- [ ] **Step 6:** If Step 1 shows the session starting but the desktop never appears or reads return garbage, change `IEC_FLRX_GEOS_2MHZ` to `IEC_FLRX_GEOS_1581_21` in `CmdHdDos::cmdGeos()` and repeat Step 1 — that is the one value the spec leaves open.
- [ ] **Step 7:** Record the result (which steps passed, which rx/tx variant worked) in the AGENTS.md entry from Task 2, Step 6.
