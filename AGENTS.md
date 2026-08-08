# AGENTS.md

This file provides guidance to coding tools when working with code in this repository.

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

## What is Meatloaf?

Meatloaf is an ESP32-based hardware device that emulates a floppy drive and other peripherals for Commodore computers (C64/C128/VIC20/+4). It connects to the IEC serial port and enables loading software from internal flash, SD cards, or via WiFi from various network protocols (HTTP, SMB, FTP, WebDAV, etc.). It also functions as a WiFi modem for telnet BBS access and provides a JSON parser for web service integration directly from BASIC v2.

## Build System

This project uses **ESP-IDF framework via PlatformIO** for embedded builds.

### Initial Setup

1. Copy `platformio.ini.sample` to `platformio.ini`
2. Edit `platformio.ini`:
   - Uncomment ONE environment under `[meatloaf]` section for your hardware
   - Set `flash_size` (4m, 8m, 16m, or 32m)
   - Set WiFi credentials (`wifi_ssid`, `wifi_pass`)
   - Set `upload_port` and `monitor_port` in `[env]` section

### Build Commands

The `build.sh` script provides the primary build interface:

```bash
# Clean and build
./build.sh -cb

# Clean, build, upload firmware, and monitor
./build.sh -cbum

# Upload filesystem only (WebUI, data files)
./build.sh -f

# Monitor only
./build.sh -m

# Build specific environment
./build.sh -e lolin-d32-pro -cb

# Dev mode build
./build.sh -d -cb
```

Common PlatformIO commands (when not using build.sh):

```bash
# Build for default environment
pio run

# Upload firmware
pio run -t upload

# Upload filesystem
pio run -t uploadfs

# Monitor serial output
pio device monitor -p <port> -b 2000000 --filter esp32_exception_decoder

# Clean
pio run -t clean
```

### Filesystem Upload

The filesystem must be uploaded ONCE before first firmware upload:
- Contains WebDAV server files, web UI, help files, and system configuration
- Located in `data/BUILD_<PLATFORM>.<FLASH_SIZE>/` directories
- Upload via PlatformIO: `Upload Filesystem Image` under Platform section

## Architecture Overview

### Bus System

The core architecture is built around a **bus system** that manages communication between the Commodore computer and virtual devices:

- **`systemBus`** (lib/bus/iec/iec.h): Main IEC bus handler that coordinates all device communication
- **`IECBusHandler`**: Low-level protocol handler for IEC serial bus timing and signals
- **`IECDevice`**: Base class for all virtual devices attached to the bus

Devices can be configured to respond to IDs 4-30 simultaneously, enabling multiple virtual drives, printers, and network adapters.

### Stream Abstraction (Meatloaf Filesystem)

The unique feature of Meatloaf is its **hierarchical stream abstraction** that allows transparent access to nested data sources:

**Stream Types:**

1. **Bottom Streams**: Root-level protocols (HTTP, SMB, FTP, TCP, WebSocket, local flash)
2. **Decoder Streams**: Container formats (D64/D81 disk images, ZIP/RAR archives, TAR files)
3. **Source Streams**: Raw byte streams that feed decoder streams

**URL Resolution**: Paths are parsed **right-to-left** to build nested stream hierarchies. Example:

```
http://server.com/files/game.zip/disk.d64/start.prg
```

Resolves to: `FileStream(D64Stream(ZipStream(HttpStream)))`

**Key Classes:**

- **`MStream`** (lib/meatloaf/meat_media.h): Base stream class with position, size, seek capabilities
- **`MFile`**: File abstraction built on MStream
- Filesystem implementations in `lib/meatloaf/media/` and `lib/meatloaf/network/`

See `docs/filesystems.md` for detailed documentation on implementing new filesystems.

### Device Layer

Located in `lib/device/`:

- **`device.h`**: Base device interface
- **`disk.h`**: Disk drive device abstraction
- **`printer.h`**: Printer device abstraction
- **`lib/device/iec/`**: IEC-specific device implementations (drives, printers, network devices)

Each device responds to CBM-DOS commands and can stream data from any MStream-compatible source.

### Major Components

```
lib/
├── bus/                    # Bus protocol handlers
│   ├── iec/               # IEC serial bus implementation
│   ├── gpib/              # GPIB bus support
│   └── userport/          # Parallel userport interface
├── meatloaf/              # Core stream/filesystem abstraction
│   ├── media/             # Container formats (D64, D81, archives)
│   ├── network/           # Network protocols (HTTP, FTP, SMB, etc.)
│   └── service/           # mDNS Network Service Discovery
├── device/iec/            # Virtual IEC devices
├── www/                   # Web server (WebDAV, REST API, GraphQL)
├── console/               # Command-line interface over serial/TCP
├── fuji/                  # FujiNet compatibility layer
├── config/                # Configuration management
└── wifi/                  # WiFi connection handling
```

### Hardware Abstraction

- Pin mappings in `include/pinmap/` - one header per board variant
- Build defines hardware capabilities via PlatformIO build_flags:
  - `PINMAP_*`: Selects pin configuration
  - `SD_CARD`: Enables SD card support
  - `ENABLE_DISPLAY`: Enables display subsystem
  - `ENABLE_CONSOLE`: Enables serial console
  - `IEC_SPLIT_LINES`, `IEC_INVERTED_LINES`: IEC hardware variations

### Entry Point

`src/main.cpp` initializes:
1. NVS flash storage
2. WiFi subsystem
3. Filesystem (flash + SD)
4. System bus and attached devices
5. Optional services (console, display, WebDAV)

## Build Platforms

Two main build platforms (set in `platformio.ini`):

- **`BUILD_IEC`**: Standard IEC serial bus mode (default)
- **`BUILD_GPIB`**: GPIB bus mode (experimental)

## Common Development Patterns

### Adding a New Network Protocol

1. Create new class in `lib/meatloaf/network/` extending `MStream`
2. Implement `getDecodedStream()` or override `getSourceStream()` for bottom streams
3. Register URL scheme in the stream factory/resolver
4. See detailed architecture documentation in `lib/meatloaf/CLAUDE.md` (includes SMB, HTTP, and other protocol implementations)

### Archive Support (libarchive)

Meatloaf uses libarchive to provide transparent access to compressed and archived files:

**Supported Formats:**
- **Compressed files**: `.gz`, `.bz2`, `.xz`, `.lz`, `.z`, `.zst`, `.lz4`
- **Archives**: `.tar`, `.tar.gz`, `.tgz`, `.tar.bz2`, `.tar.xz`, `.zip`, `.7z`, `.rar`, `.iso`, `.cpio`
- **Disk images**: Can be nested (e.g., `game.d64.gz` → `game.d64`)

**Implementation Details:**
- **Location**: `lib/meatloaf/media/archive/archive.cpp` and `archive.h`
- **Classes**: `Archive`, `ArchiveMSession`, `ArchiveMStream`, `ArchiveMFile`
- **libarchive callbacks**: Custom read, skip, and seek callbacks for stream integration
- **Caching**: `ArchiveMSession` caches extracted entry data via `MSession::CachedFile` (supports HIMEM on ESP32-WROVER)

**Key Behaviors:**
1. **Compressed-only files** (`.gz`, `.bz2`, etc.):
   - Synthesize single entry with filename = archive name minus compression extension
   - Set `m_isCompressedOnly` flag to prevent reading past first entry
   - Size reported as 0 during listing (determined on actual file access)

2. **Compressed archives** (`.tar.gz`, `.zip`, etc.):
   - List all entries by reading headers sequentially
   - Cannot seek in compressed streams - must read/discard data to advance
   - After reading each entry header, consume all file data (including tar padding) before reading next header
   - Use `archive_read_data()` in loop until returns 0 to handle padding automatically

3. **Callback Implementation**:
   - **`cb_read`**: Provides data blocks from source stream
   - **`cb_skip`**: Returns 0 to force libarchive to use read callback (seeking doesn't work in compressed streams)
   - **`cb_seek`**: Only allows SEEK_SET to position 0 (rewind), fails other seeks gracefully

4. **Error Handling**:
   - Expected EOF errors suppressed: "Truncated input", "decompression failed", "bad header checksum"
   - These occur when libarchive reads past last entry in compressed archives
   - Only unexpected errors are logged

5. **Entry Caching (ArchiveMSession)**:
   - Extracted archive entries are cached in RAM (or HIMEM on ESP32-WROVER) via `ArchiveMSession`
   - `ArchiveMSession` extends `MSession` with `getEntry()` to extract-and-cache archive entries
   - `MSession::CachedFile` provides platform-aware storage: HIMEM page-mapped on ESP32+SPIRAM, heap otherwise
   - `CachedFile` supports `read(offset, buf, count)`, `write(offset, buf, count)`, `loadFromStream(MStream*, size)`, and `loadViaReader(size, reader)`
   - After caching, `ensureData()` releases the archive handle and source stream chain to free memory
   - `seekPath()` checks the session cache first, avoiding re-opening the archive (prevents DMA exhaustion)
   - Session keyed as `"archive://" + archiveUrl`, managed by SessionBroker with keep-alive disabled

**Recent Changes (March 8, 2026):**
- **NFS crash fix** (`lib/meatloaf/network/nfs.h`): Set `keep_alive_interval = 0` in `NFSMSession` constructor to disable SessionBroker keep-alive. NFSv3 is stateless — no server-side session to maintain. The keep-alive was racing with the IEC task (core 1) doing `seekFileSize()` NFS reads; concurrent libnfs socket access from core 0 corrupted the heap, overwriting the SPI3 bus lock interrupt handle with `0x6400` and crashing the display task.
- **`seekFileSize()` fix** (`lib/meatloaf/meat_media.cpp`): Commented out `console.printf()` in the block-chain loop (code comment said to leave it commented but it was active). With `ENABLE_CONSOLE_TCP`, each call invoked `tcp_server.send()` → TCP socket write from IEC task (core 1), concurrent with NFS socket reads — hundreds of unprotected LwIP socket calls per `seekFileSize()` caused the same `0x6400` heap corruption. Both prints converted to `Debug_printv` / removed.

**Recent Changes (March 7, 2026):**
- Rewrote `driveMemory` class in `lib/device/iec/drive.h` for lower internal RAM usage:
  - Shared PSRAM ROM cache: all drives sharing the same ROM filename share one buffer (via `weak_ptr` Meyers-singleton cache); saves 9× ROM file handles and eliminates per-access SD I/O
  - Lazy RAM allocation: `ram` vector stays empty until first `write()` call; saves ~18 KB at boot
  - Direct `memcpy` for ROM reads instead of seek+read on `MStream`
  - Added `#include <esp_heap_caps.h>` for `MALLOC_CAP_SPIRAM` allocation
- Confirmed `SessionBroker` task stack must remain at 8192 bytes (4096 causes stack overflow → FreeRTOS heap corruption → crashes in unrelated tasks)

**Recent Changes (February 19, 2026):**
- Added `ArchiveMSession` for caching extracted archive entries via SessionBroker
- Enhanced `MSession::CachedFile` with HIMEM support, `read()`/`write()` API, and `loadFromStream()`/`loadViaReader()` (archive-specific loading moved to `ArchiveMSession::loadEntryFromArchive`)
- Refactored `ArchiveMStream` to use session-based caching instead of bespoke HIMEM code
- After caching entry data, source chain (Archive + source stream) is released to recover memory
- `seekPath()` checks session cache first to avoid re-opening archives when entry already cached

**Recent Changes (February 17, 2026):**
- Fixed standalone compressed file listing (`.gz`, `.bz2`, etc.) to show entry with extension removed
- Fixed compressed archive listing (`.tar.gz`) to list all entries, not just first
- Implemented proper data consumption between headers to position stream correctly
- Added compressed-only file detection and handling
- Suppressed expected end-of-archive errors during directory listing

**Example Usage:**
```
cd /sd/archives/game.d64.gz    # Access compressed disk image
ls                              # Shows: game.d64
cd game.d64                     # Navigate into decompressed disk image

cd /sd/archives/files.tar.gz   # Access compressed tar archive
ls                              # Lists all files in archive
```

### mDNS Network Service Discovery

Meatloaf includes mDNS-based service discovery for browsing available network services:

**Key Components:**
- **`MDNSMSession`** (`lib/meatloaf/service/mdns.h/cpp`): mDNS client for service discovery
- **`MDNSMFile`** (`lib/meatloaf/service/mdns.h/cpp`): Filesystem interface for MDNS
- **mDNS Integration**: Uses ESP-IDF mDNS stack with custom PTR query handling

**Usage:**
- Access via `mdns://` URL scheme
- Root directory lists available service types (e.g., `_http._tcp`, `_smb._tcp`)
- Service type directories list individual service instances
- Services can be accessed via their network protocols (SMB, HTTP, etc.)

**Implementation Details:**
- Meta-queries (`_services._dns-sd._udp`) discover available service types
- PTR record parsing handles mDNS response format correctly
- Service discovery is cached for performance
- Thread-safe with mutex protection

**Recent Changes (February 7, 2026):**
- Fixed mDNS PTR record parsing for meta-queries
- Corrected instance name construction from PTR data responses  
- Implemented service type discovery without redundant instance querying
- Added proper caching of discovered service types
- Updated directory listing to show complete service type names

### Adding Support for a New Board

1. Create pinmap header in `include/pinmap/<board>.h`
2. Define all GPIO pins: IEC lines, SD card, buttons, LEDs, display
3. Add environment to `platformio.ini.sample` with appropriate build flags
4. Add board definition to `boards/` if custom board config needed

### Working with IEC Bus

The IEC bus runs on FreeRTOS with tight timing requirements. Key points:

- Bus service runs in `systemBus::service()` loop
- Device handlers must respond within microsecond timing windows
- Use `IECBusHandler` methods for low-level line control
- Debug with timing-aware logging (can affect protocol timing)

## Version Management

- Version defined in `include/version.h`
- `FN_VERSION_MAJOR`, `FN_VERSION_MINOR`: Manually updated
- `FN_VERSION_BUILD`: Git commit hash (updated by build_version.py if enabled)
- `FN_VERSION_DATE`: Build timestamp

## Configuration Files

- `data/BUILD_IEC.*/.sys/config.json`: System configuration (WiFi, device IDs)
- `data/BUILD_IEC.*/.sys/secret.json`: Sensitive credentials
- NVS partition stores runtime settings

## Debugging

Build flags in `platformio.ini`:

```ini
-D DEBUG_SPEED=2000000       # Serial debug baud rate
-D VERBOSE_DISK              # Disk operation logging
-D VERBOSE_HTTP              # HTTP protocol logging
-D VERBOSE_PROTOCOL          # IEC protocol logging
-D DEBUG_TIMING              # Timing measurements
-D VERBOSE_MDNS               # MDNS/mDNS service discovery logging
-D RUN_TESTS                 # Enable test code
```

Monitor with exception decoder: `monitor_filters = esp32_exception_decoder`

**mDNS Debugging:**
- ESP-IDF mDNS component includes built-in debug output
- Enable `CONFIG_LWIP_DEBUG` and `CONFIG_MDNS_DEBUG` in sdkconfig for detailed mDNS packet logging
- MDNS uses debug prints for service discovery operations

### Console UART Configuration

`monitor_speed = 2000000` in `platformio.ini`. Every board sdkconfig **must** set both the modern and legacy compat console blocks to match:

```
CONFIG_ESP_CONSOLE_UART_CUSTOM=y
CONFIG_ESP_CONSOLE_UART_BAUDRATE=2000000
CONFIG_CONSOLE_UART_CUSTOM=y
CONFIG_CONSOLE_UART_BAUDRATE=2000000
```

`sdkconfig.defaults` and `sdkconfig.defaults.esp32s3` now include all four lines so newly generated sdkconfigs are correct automatically.

**Critical: UART clock source.** `lib/console/console_settings.c` uses `UART_SCLK_DEFAULT` (APB = 80 MHz on ESP32, XTAL = 40 MHz on S3) rather than the original `UART_SCLK_REF_TICK`. REF_TICK is only 1 MHz and cannot generate baud rates above ~250 kbps — using it at 2 Mbps produces garbage output. `UART_SCLK_DEFAULT` divides cleanly to exactly 2 Mbps on both chip families.

**Checklist when a board console shows garbage or no output:**
1. Verify both `CONFIG_ESP_CONSOLE_UART_BAUDRATE` and `CONFIG_CONSOLE_UART_BAUDRATE` are 2000000 in its sdkconfig.
2. Verify `CONFIG_ESP_CONSOLE_UART_CUSTOM=y` (not `_DEFAULT`).
3. Verify the USB-serial chip supports 2 Mbps (CP2102 maxes at 1 Mbps — use 921600 for those boards).

### Display Subsystem (HAGL/MIPI)

`PIN_TFT_MOSI` is the compile-time guard for the display driver. HAGL is compiled and `hagl_init()` is called **only** when this macro is defined in the board's pinmap header.

- **Boards with a wired display** (`pocket-dongle-s3`, `esp32-1732s019`): define `PIN_TFT_MOSI` and all other `PIN_TFT_*` pins in their pinmap; sdkconfigs carry the correct GPIO numbers.
- **Boards with `ENABLE_DISPLAY` but no physical display** (all others): `PIN_TFT_MOSI` is **not** defined; sdkconfigs set all six HAGL pin Kconfigs to -1.

When a board sdkconfig is regenerated by IDF, the HAGL Kconfig defaults (MOSI=23, CLK=18, CS=14, DC=27, RST=33, BL=32) are ESP32-WROVER generic values. If they survive into a build for a board that lacks `PIN_TFT_MOSI`, `hagl_init()` is skipped by the code guard and no crash occurs. If they somehow reach a board with conflicting GPIO assignments, the -1 defaults in `sdkconfig.defaults` / `sdkconfig.defaults.esp32s3` override them for new boards.

**Stub class pattern:** `lib/display/lcd.h` defines a no-op `DisplayLCD` class in the `#else` branch of `#ifdef PIN_TFT_MOSI`. Call sites guarded by `#ifdef ENABLE_DISPLAY` compile cleanly without HAGL on all boards.

### Internal Heap and PSRAM Allocation

The ESP32's internal DRAM heap is small (~239 KB total on a WROVER) and mostly consumed by WiFi + lwIP at boot. Only ~3–11 KB is typically free after init. Key rules:

**`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`** — allocations at or below this threshold stay in internal DRAM; larger ones go to PSRAM. The lolin-d32-pro uses 512; the default is 1024. Lowering it pushes more small allocations to PSRAM but increases cache-miss latency.

**`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`** — must be **32768** (the IDF default) on all PSRAM boards. This carves out an internal-RAM pool that plain `malloc()` cannot touch, reserved for allocations that have no PSRAM fallback: FreeRTOS locks/semaphores, task stacks, DMA buffers. It was set to 512 (and 0 in `sdkconfig.defaults`), which let httpd session buffers and socket structs drain internal heap to zero under web load — the next `fopen()` then aborted in newlib's `lock_init_generic` (a mutex allocation that must be internal returned NULL). With the reserve restored, ordinary allocations spill to PSRAM when internal gets tight and lock/stack allocations always succeed.

**Task stacks are internal-DRAM only — there is NO PSRAM fallback.** `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y` appears in the sdkconfigs but is not an effective option on this IDF, and `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y` only permits external stacks for `xTaskCreateStatic`/pthread — plain `xTaskCreate`/`xTaskCreatePinnedToCore` always allocates the stack from internal DRAM and **fails** when it can't (proven empirically July 3 2026: REPL task creation failed repeatedly with 3.7 MB PSRAM free). Budget internal RAM accordingly: every 16 KB command-capable stack (REPL, tcp_session, httpd) is a hard internal-RAM cost, task creation can fail at runtime and callers must retry gracefully, and moving stacks to PSRAM is not an option (console commands perform flash operations, which forbid PSRAM stacks anyway).

**`mlConfig` / `psram_json`** — `MeatloafConfig::_data` uses `psram_json` (`nlohmann::basic_json` with `PsramAllocator<T>`), defined in `lib/config-ml/mlConfig.h`. Every JSON node (map entries, array storage, string objects) is allocated from PSRAM. Falls back to internal heap if PSRAM is unavailable. This prevents the parsed config tree from exhausting internal DRAM.

**`Debug_memory()` macro** — defined in `include/debug.h`:
```c
Debug_printv("Heap[%lu] Low[%lu] Task[%u]",
    esp_get_free_heap_size(),           // total free (PSRAM + internal)
    esp_get_free_internal_heap_size(),  // current internal free ("Low" = internal, not a watermark)
    uxTaskGetStackHighWaterMark(NULL)); // current task stack bytes remaining
```
The `Low` field is current internal free, not a watermark. The all-time watermark is `ESP.getMinFreeHeap()`.

### Drive ROM and RAM Memory Management

`driveMemory` (in `lib/device/iec/drive.h`) manages the 6502-mapped memory space for each virtual drive:

**ROM (drive firmware)**:
- ROM bytes are shared across all drives that load the same file via a static `weak_ptr` cache (`getRomCache()`)
- First call to `setROM(filename)` loads the file into PSRAM (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`) with heap fallback
- Subsequent drives get a `shared_ptr` to the same buffer — no re-read, no extra RAM
- Cache entry is a `weak_ptr` so it automatically clears when the last drive releases it
- ROM reads use direct `memcpy` from the in-memory buffer — no per-access file I/O

**RAM (drive scratch memory)**:
- `ram` vector is **lazy-allocated**: zero bytes at startup, `resize()` on first `write()` call
- Saves ~18 KB on boot when drives are created but never write RAM (typical for most sessions)

**Important**: Do **not** reduce `SessionBroker` task stack below 8192 bytes. HTTP/TNFS session servicing calls deep into the network stack; 4096 bytes causes stack overflow, which silently corrupts FreeRTOS heap structures and manifests as crashes in completely unrelated tasks (e.g., the display SPI task).

### Disk Image Write Test Suite (native)

`pio test -e native -f native/test_disk_write` runs the disk-image write verification suite for
D64/D71/D80/D81/D82/DNP on the development host. It needs VICE's `c1541`; set the `C1541` env var if
it is not on PATH.

**Read `test/native/test_disk_write/README.md`** — it is the authoritative reference for how to run
the suite, what it requires, how to read its output, and the two traps that previously produced
green results which meant nothing (c1541's silent BAM repair, and `seekEntry()`'s stream-position
fast path). This entry is only a pointer; do not duplicate the detail here, it will drift.

Status: `format()` produces a valid image on all six formats (D64/D71/D80/D81/D82/DNP); suite is
75 cases, 70 passing, 5 deliberate scenario skips. Nine engine bugs found and fixed — findings in
`docs/superpowers/findings/2026-08-05-disk-write-findings.md`. Tests blocked by an open finding are
`TEST_IGNORE_MESSAGE`'d citing it, with the body intact — lift the guard and re-run to verify a fix.
`N0:name,id[,track_count[,error_info]]` creates 40/42-track D64, 81-track D81 or multi-track DNP.

`lib/meatloaf` compiles for the host because `meat_media.h` guards its `device/iec` includes behind
`TEST_NATIVE`. Keep new `lib/meatloaf` code free of device-layer includes or the native suite stops
building. The `[env:native]` config lives in `platformio.ini.sample` because `platformio.ini` is
gitignored.

### PlatformIO invocation traps (this machine)

- **`pio` is not on PATH** — use `~/.platformio/penv/Scripts/pio.exe`. A bare `pio` fails with
  "command not found" but a wrapping pipeline can still exit 0, so the build looks like it ran.
- **Never pipe `pio` through `| tail`/`| head`** — it discards the error text and leaves only the
  summary, so a failure looks unexplained. Redirect full output to a file, then grep it.
- **"Access is denied" on `program.exe`** at the native build step means a previous test binary is
  still running and holding the file: `Get-Process program | Stop-Process -Force`. It reads as a hang.
- **Adding/removing a component source file needs `.pio/build/<env>/CMakeCache.txt` deleted** — the
  source glob is cached, so a removed file still errors with "Source not found".

## Important Notes

- **IEC timing is critical**: Code changes in bus handlers can break compatibility
- **PSRAM availability**: Some boards have PSRAM (ESP32-WROVER), others don't (ESP32-WROOM32). Adjust memory allocation accordingly.
- **Filesystem first**: Always upload filesystem before testing WebDAV or web features
- **URL parsing**: The right-to-left URL resolution is fundamental to how streams compose
- **Device IDs**: Drive 8 is default, but device can respond to multiple IDs (4-30) simultaneously
- **Network Service Discovery**: Use `mdns://` to browse available network services via mDNS
- **mDNS PTR queries**: Meta-queries for service discovery use `_services._dns-sd._udp` PTR records
- **URL cache fragments**: Network streams can enable SD caching with `#cache=sd`. Add `&refresh=1` or `&force=1` to bypass cache. Cache logic lives in `MFile::openStreamWithCache()` and uses `PeoplesUrlParser::fragmentValue()` for parsing.
- **SessionBroker stack size**: Must remain at 8192 bytes minimum — HTTP/TNFS network operations require the full depth. Stack overflow here causes seemingly unrelated FreeRTOS crashes.
- **Drive count**: `iecFuji` holds 8 drives (`_fnDisks[8]`) plus `iecMeatloaf` is a 9th drive — all call `begin()` → `setROM()`. The shared ROM cache ensures only one copy of each ROM is loaded regardless of drive count.
- **NFS keep-alive disabled**: `NFSMSession` sets `keep_alive_interval = 0`. NFSv3 is stateless so keep-alive is unnecessary, and calling it while the IEC task is doing NFS I/O causes concurrent libnfs socket access → heap corruption. Any stateless protocol session should disable keep-alive the same way.
- **`console.printf()` in I/O loops**: With `ENABLE_CONSOLE_TCP`, `console.printf()` sends data over a TCP socket from whatever task calls it. Calling it from the IEC task (core 1) during network I/O creates unprotected concurrent LwIP socket access → heap corruption. Use `Debug_printv()` (UART only) inside I/O hot paths instead.
- **Console baud rate**: `monitor_speed` is 2,000,000 baud. Every board sdkconfig must have `CONFIG_ESP_CONSOLE_UART_CUSTOM=y` and both baudrate keys set to 2000000 (modern block + legacy compat block). The console UART uses `UART_SCLK_DEFAULT` — never `UART_SCLK_REF_TICK`, which is limited to ~250 kbps.
- **HAGL display guard**: HAGL is only compiled when `PIN_TFT_MOSI` is defined in the board's pinmap. Boards with `ENABLE_DISPLAY` but no wired display must have all six `CONFIG_MIPI_DISPLAY_PIN_*` values set to -1 in their sdkconfig to prevent GPIO conflicts at boot.
- **`esp_ping_new_session()` OOM**: The ping task stack is allocated from internal DRAM by `xTaskCreate`. **There is no PSRAM fallback** — `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y` is set on all PSRAM boards but is not an effective option on this IDF (see "Task stacks are internal-DRAM only" above), so creation genuinely fails when internal heap is too fragmented. Always check the `esp_err_t` return of `esp_ping_new_session` before calling `esp_ping_start` — a NULL handle causes a hard fault.
- **`nlohmann::json` and internal heap**: `nlohmann::json` default-allocates all node objects (map entries, string objects, array slots) from `malloc`, which on PSRAM boards falls into internal DRAM for small objects. Use `psram_json` (defined in `lib/config-ml/mlConfig.h`) for any long-lived JSON trees. ESP-IDF compiles with `-fno-exceptions` — do not use `throw` in allocators; use `abort()` instead. For the same reason never use `std::stoi`/`std::stof` on input from the C64 or the network: they throw on malformed input, which becomes `std::terminate`. Use `strtol` and validate `*end == '\0'`.
- **`MFile::exists()` for local paths**: Uses `stat()` when `scheme` is empty. Media filesystem subclasses (D64, archive, etc.) that set `_exists = true` in their constructors are overridden for local paths — only actual on-disk existence matters. Network paths (non-empty scheme) still use `_exists` since `stat()` doesn't apply to remote resources.
- **Existence checks: use `exists()`, never a trial stream open**: network stream `open` can be lazy — `fsp_fopen` succeeds for ANY name (only `fsp_stat` tells the truth), so `getSourceStream()!=null && isOpen()` is NOT proof a file exists. Filesystems that can check for real must override `exists()` (FSPMFile stats `path` — NOT `pathInStream`, which is empty for plain files).
- **WebDAV local path routing (`webdav_mfile()`)**: WebDAV operations use `MFSOwner::File()` first so `.config` base_url redirects are applied. If the result has a non-empty `scheme`, it is a network redirect — use it. If scheme is empty (e.g. D64MFile, ArchiveMFile from a local path), fall back to `FlashMFile` so existence is checked via `stat()` rather than the media filesystem's always-true `_exists`.
- **Config JSON split**: `config.json` holds all settings except the `"devices"` key. `devices.json` holds `{"devices": {"iec": {...}, "ps2": 0, ...}}`. `mlConfig.save()` detects dirtiness automatically — it hashes the current `config`/`devices` sections and compares against the hash captured at the last load/save, writing (and rehashing) only the file(s) that actually differ, or nothing at all if neither changed. There are no dirty flags to set; just mutate `mlConfig.data()` and call `save()`.
- **HTTP error responses**: `send_http_error()` must call `httpd_resp_set_status()` before sending the body — ESP-IDF defaults to 200 OK if status is never explicitly set.
- **`.config` redirect in HTTP server**: `send_file()` tries flash first, then falls back to `MFSOwner::File(uri)` so paths under mount points (e.g. `/zimmers.net/pub/00INDEX`) are fetched via the network URL from the parent `.config` `base_url` rather than returning 404.
- **Console/task-stack architecture (verified July 4 2026, updated July 22 2026)**: Task stacks are internal-DRAM only — `xTaskCreate` has NO PSRAM fallback (proven: 16 KB and even 6 KB creations failed with 3.7 MB PSRAM free), and creation can fail from FRAGMENTATION even when aggregate free heap looks fine — check `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)`, not just free size. Therefore: (1) all console commands run on one refcounted 16 KB `console_exec` task (`Console::runCommand`/`runOnExecutor`, serialized by mutex), created on first console activation via `execAcquire()` and deleted when the last session releases; `runOnExecutor(std::function<void()>)` is a sibling of `runCommand()` for system code that needs the same deep-stack headroom without a registered command (e.g. the boot-time network-drive retry in `main.cpp`); (2) the I/O shells are small persistent boot-created tasks — `console_repl` 6 KB (dormant in `fgetc()`, REPL loop on first byte, dormant again on `exit`; the task is never re-created), `tcp_session` 4 KB (fed by the listener via task notifications), `console_tcp` listener 2.5 KB (rebinds on any failure, never exits); (3) `exit` AND `reboot` are intercepted directly in the I/O shells (`repl_task()`/`execute()` in `Console.cpp`) so both work even when the executor can't be created or is busy — both commands live in `CoreCommands.cpp`, registered via `registerCoreCommands()`; (4) stdio is per-task — the executor adopts the TCP stdout tee around remote-origin commands; (5) `exit` command logic uses `console.execOrigin()` (ORIGIN_SERIAL vs ORIGIN_REMOTE), never task identity. Budget: `bus_iec` stack is 20480 (ps HWM showed <800 B used; re-check after heavy C64 sessions). `esp_wifi_init()` is the single largest internal-RAM consumer at boot (~66-80 KB, plus it roughly halves the largest contiguous free block) — see the July 22 2026 changelog entry for the full set of tuned `CONFIG_ESP_WIFI_*`/`CONFIG_ESP32_WIFI_*` buffer-count keys (now in every board sdkconfig plus `sdkconfig.defaults`/`.esp32s3`), which recovers ~24 KB with no observed throughput regression. The web server stays fully lazy (`www_lazy` port-80 listener + transparent loopback-proxy handover on first access). IEC (priority 17, core 1) outranks every service task (core 0, priority ≤ 12).
- **Web server structure**: `lib/www/` is organized into subdirectories by concern. The lifecycle class is `HttpServer` (`lib/www/web_server.h`), global instance `httpServer`. Sub-module handlers are plain free functions — not class methods: `proxy_handler`/`proxy_register` in `lib/www/proxy/proxy.h`, `ws_handler`/`ws_send_all`/`ws_register` in `lib/www/ws/ws.h`, `webdav_handler`/`webdav_register` in `lib/www/webdav/handler.h`. `ws_command.h/cpp` lives in `lib/www/ws/`. File-serving helpers (`send_file`, `send_file_parsed`, MIME utils) are file-scope statics in `web_server.cpp` — not class methods. Two members of `HttpServer` are public for use by sub-modules: `s_server` (needed by `ws_send_all`) and `send_http_error()` (needed by webdav and proxy handlers).
- **HTTP proxy**: `/proxy?<url>` endpoint in `lib/www/proxy/proxy.cpp` forwards the request with the same method, probing a known header list and stripping `X-` prefixes (so clients can pass `X-Authorization` to bypass CORS restrictions). `esp_http_server` has no per-header enumeration API — there is no server-side equivalent of the client-side `HTTP_EVENT_ON_HEADER`. The known-header probe list is the correct pattern.
- **SQLite FTS and `locate`** (`lib/console/Commands/VFSCommands.cpp`): The scan `db` handle must be closed before calling `updatedb_fts_rebuild()` — `sqlite3_shutdown()` inside the rebuild returns `SQLITE_BUSY` when a connection is open, silently failing to swap in the PSRAM allocator. `locate` detects leading wildcards or `?` in the search term and uses LIKE directly (FTS5 only supports trailing `*` prefix queries); if FTS returns 0 results it falls back to LIKE. `updatedb_fts_rebuild()` always calls `updatedb_compress_gz()` at the end so `/sd/.locate.gz` stays in sync after every index build.
- **`gzip` / `unzip` commands** (`lib/console/Commands/VFSCommands.cpp`): `gzip <src> [dst.gz]` compresses any file at level 9 with a PSRAM buffer and 512 KB progress intervals. `unzip <archive> [dest]` extracts any libarchive-supported format (zip, tar, gz, 7z, rar…) showing per-entry size and byte progress for large entries. Both work on flash and SD. `unzip` is excluded from `MIN_CONFIG` builds (no libarchive).
- **Writing into disk images**: SAVE into D64/D71/D81/DNP (and DHD partitions) is supported via the MStream path — streamed one block at a time with CBM-style allocation (first block nearest the directory track, file/directory interleave, BAM updated per block, rollback + `72,DISK FULL` on failure, directory extended on the directory track only). The old blanket `media_image` write block in `drive.cpp` is now gated on `!isWritable || pathInStream.empty()`. Write errors surface AFTER close: `iecChannelHandlerFile`'s destructor maps `m_stream->error()` to the drive status.
- **Media stream `mode`**: `MFile::getSourceStream()` sets `decodedStream->mode` (it is otherwise uninitialized) and opens the CONTAINER chain `in|out` ("r+") when writing a file inside an image — plain `out` would truncate the image on flash/SD.
- **PIO lib builder skips `.c` under `lib/meatloaf`**: C sources there are silently not compiled (pre-existing example: `gcr/nibread.c`). Any C code the meatloaf lib needs must live in an ESP-IDF component (that is why the tapclean tape engine is `components/tapclean/`). Remember the CMakeCache delete trick after editing a component CMakeLists.
- **CMD media images (DHD, D1M/D2M/D4M)**: `DHDImageRegistry` (lib/meatloaf/media/hd/dhd.h/cpp) keeps per-image partition tables + the "currently selected partition" (default partition on first use). `getFile()` (shared `DHDCreatePartitionFile()`, used by `dhdFS` and `dxmFS` in media/disk/dxm.h) returns a `D64MFile`/`D71MFile`/`D81MFile`/`DNPMFile`-based wrapper decoding through a `DHDOffsetStream` window at the partition offset — a D1M thus resolves to D81 or DNP by its partition type. HD images: system partition scanned on 64 KiB boundaries ("CMD HD" boot magic), table at sys+65536, partitions numbered 1-255 (entry 0 is the system partition, which supplies the disk label and holds the partition table; its type byte is `$FF` in every real image). **Entry 0 IS listed** — by `LOAD"$=P"` (extension `sys`) and by the `partition` console command (type `SYS`) — because it is a real table entry, but `DHDImageRegistry::select()` refuses partition 0 outright, so it can never be mounted by number, by name, or via `CP0`. Two consequences of listing it: `parts` is never empty, so "is there anything mountable" must count entries with `number != 0` (that is what the "No usable partitions" guard does), and the default-selection fallback must pick the first USER partition, never `parts[0]`. FD images: fixed system partition (0x640/0xC80/0x1900 blocks for D1M/D2M/D4M), "CMD FD SERIES" magic, table at sys+2048, 31 partitions. `CP<n>` (drive) and the `partition` console command are the ONLY ways to change partitions — matching the real CMD HD, which does not switch on LOAD or CD. `LOAD"$=P"` lists them. A partition name or number appearing in a path is NOT a selection: it used to be, and because selecting strips the partition from the path, a file whose name matched a partition switched the image part-way through a directory listing. Selection changes dispose the ImageBroker entry (`"d64" + container source url`) so listings re-decode.
- **CBM directory entries store BLOCKS, not bytes** — there is no byte-size field anywhere in a D64/D71/D81/DNP/DHD directory. Every byte figure Meatloaf shows for a file inside an image is a derived over-estimate, and the two call sites deliberately disagree: `ls` uses `entry.blocks * block_size` (256/block, i.e. space occupied on disk) while `D64MStream::seekPath()` uses `entry.blocks * (block_size - 2)` (254/block, the data capacity). The TRUE size is only knowable by walking the block chain to its last block and reading its link bytes: `track == 0` marks the last block and the sector byte is the last-used byte index, so data = `sector - 1` bytes. Reads are correct regardless — the chain-end marker always fires before either over-estimate is reached — so a `cat`/`hex` that returns far fewer bytes than `ls` advertised is CORRECT, not a truncation bug (real example: `settings.ihf`, 1 block, `ls` says 256, actual content is 6 bytes). `seekFileSize()` (`meat_media.cpp`) does the exact walk but is deliberately commented out at the `seekPath()` call site: it costs one read per block per file (hundreds of RPCs over the network, thousands of block reads for one directory listing), which is why the estimate is used instead.
- **`getNextFileInDir()` must honor `rewindDirectory()`'s return value**: `if (!dirIsOpen && !rewindDirectory()) return nullptr;`. A failed rewind has ALREADY called `resetEntryCounter()` on the shared ImageBroker stream, so reading on hands back entry 0 forever while `dirIsOpen` stays false — an endless listing, not an empty one. This bites any implementation whose `rewindDirectory()` can fail AFTER resetting the counter, which is the `seekDirectory(pathInStream)` failure path in `D64MFile` and `HDDMFile` (both fixed). Other formats (archive, m2i, t64, tcrt, ark, lbr, lnx) only fail before the reset and cannot loop.
- **SD card mount/unmount** (`lib/FileSystem/fnFsSD.cpp`): `esp_vfs_fat_mount_config_t` MUST be zero-initialized (`= {}`) — it has five fields, the file's top-of-file `#pragma GCC diagnostic ignored "-Wmissing-field-initializers"` suppresses any warning, and a bare stack declaration left `disk_status_check_enable`/`use_one_fat`/`allocation_unit_size` reading stack garbage. `FileSystemSDFAT::stop()` unmounts (flushing FATFS's cached FAT/directory sectors) and is called LAST in `main_shutdown_handler()`, after the bus and network sessions stop; `ESP.restart()` is `esp_restart()`, which runs registered shutdown handlers, so the console `reboot` is covered — a crash or power cut is NOT. The SDSPI mount retries 3× (dropping to `SDMMC_FREQ_DEFAULT / 2` after the first failure) because a single transient `ESP_ERR_INVALID_CRC` used to disable the card for the whole session. The SDMMC branch has no retry yet.
- **`CONFIG_FATFS_SECTOR_4096=y` is NOT an SD card setting** — it appears in every board sdkconfig and looks alarming, but in this IDF `FF_SS_SDCARD` is hardcoded to 512 and `FF_MIN_SS`/`FF_MAX_SS` come from `MIN`/`MAX(512, CONFIG_WL_SECTOR_SIZE)`. That puts FATFS in variable-sector mode, where it queries `GET_SECTOR_SIZE` and gets the card's real 512. Do not "fix" it.
- **Tape images (.tap/.dmp/.htap)**: decoded by the vendored TAPClean engine (`components/tapclean`, ~90 loader scanners incl. Cyberload, Visiload, US Gold, Novaload, Freeload and a Meatloaf-added Turbotape-64-fast scanner). The image is fetched into PSRAM and scanned PROGRESSIVELY on demand (512 KB prefix, doubling; DMP/HTAP/TAP-v2 converted to TAP v1 on the fly as they stream in); every program is extracted (neighbouring blocks united into loadable PRGs, loader-internal header blocks folded in for their names, CBM boot stubs of turbo tapes dropped with their name transferred to the payload, repeat copies deduped), and once fully scanned the pulse buffer is freed — only the decoded programs stay resident. Sequential datasette semantics unchanged: each directory request returns ONE entry, `no more entries` at tape end, then rewind. A `.idx` sidecar (`<offset>[:<length>] <name>`) switches to a normal full listing with random access. Drive commands: `T-C <ms|MMM:SS>` sets the tape counter (read position by time), `T-I` scans the tape and writes the `.idx`. `TAPMStream` exposes `counterMs()/durationMs()/counterString()`. The tapclean engine is global-state; `TapeDecoder` serializes scans with a static mutex. **Tape decode now effectively requires PSRAM** (whole image + decoded programs in RAM during the scan).

## Recent Changes (August 7, 2026)

- **`partition` console command; path-based partition selection removed** (`lib/console/Commands/VFSCommands.cpp/.h`, `lib/console/Console.cpp`, `lib/meatloaf/media/hd/dhd.h/.cpp`, `lib/device/iec/drive.cpp`): `partition` lists a CMD HD/FD image's partitions (selected one marked) and switches by number or name/wildcard, resetting the console cwd to the image root. `DHDPartitionMFile::normalizePath()` no longer treats a leading path component as a partition reference — the real CMD HD requires `CP<n>`, and the implicit form switched partitions part-way through a directory listing because selecting strips the partition from the path, leaving listing-generated entry URLs ambiguous with partition references. Also fixed an off-by-one that made partition 255 unreachable: `parse()` capped `maxpart` at 254 (the loop runs `i <= maxpart` with entry 0 consumed as the system partition) and `changePartition()` rejected `pnum > 254`. Partitions are 1-255; 0 is the reserved system partition. Both `maxpart` and the parse loop counter must stay `uint16_t` — as `uint8_t`, `i <= 255` is never false.
- **Endless directory listing fixed** (`lib/meatloaf/media/disk/d64.cpp`, `lib/meatloaf/media/hd/hdd.cpp`): `getNextFileInDir()` discarded `rewindDirectory()`'s return value. Reported as `ls sett*` hanging inside a D64/DNP: `cd("sett*")` wildcard-resolves to a FILE, so `seekDirectory(pathInStream)` fails — but only after `resetEntryCounter()` has already run, and with `dirIsOpen` left false. Every subsequent call therefore re-rewound, reset the counter, and returned entry 0 again, forever. Both now guard with `if (!dirIsOpen && !rewindDirectory()) return nullptr;`. See the Important Note above for which formats can and cannot hit this.
- **`ls` filters on wildcards instead of descending** (`lib/console/Commands/VFSCommands.cpp`): a wildcard in the LAST path component is now a filter over the listing (`ls fb*`, `ls /sd/games/*.d64`); previously the pattern reached `cd()`, which resolved it to the FIRST matching entry and then listed that single file as though it were a directory (`ls fb*` at root silently listed nothing but `/fb128`). Matching reuses `mstr::compare(name, pattern, false)` — the same case-insensitive call `rm`'s wildcard branch uses, so both commands glob identically — and runs on the raw entry name before the PETSCII→UTF-8 rewrite. Root's synthetic `sd`/`network` entries honor the filter too. A wildcard in a MIDDLE component still goes through `cd()` as before. Note this is filter semantics, not Unix's: `ls fb*` matching a directory prints that directory's ENTRY, not its contents.
- **SD card reliability** (`lib/FileSystem/fnFsSD.cpp/.h`, `src/main.cpp`): three independent fixes, see the Important Note above for the durable rules. (1) `mount_config` zero-initialized and `disk_status_check_enable` explicitly set true — three of its five fields were reading stack garbage, so behaviour differed boot to boot. (2) New `FileSystemSDFAT::stop()` calling `esp_vfs_fat_sdcard_unmount()`, wired into `main_shutdown_handler()` after `SessionBroker::shutdown()`/`SYSTEM_BUS.shutdown()`; nothing had EVER unmounted the card, so every reboot left FATFS's cached FAT/directory sectors unwritten — the most likely source of the reported "SD card corruption". `do_reboot()` needed no change since `esp_restart()` runs shutdown handlers. (3) SDSPI mount retried 3× with a 100 ms gap, halving the clock after the first failure; IDF's `esp_vfs_fat_sdspi_mount()` fully unwinds its own allocations on the failure path, so retrying starts clean. Still open (not implemented): raising `CONFIG_ESP_BROWNOUT_DET_LVL` from 0 (the most permissive setting — the ESP32 keeps clocking the card well below the card's ~2.7 V minimum) and adding `fsync` to the write paths; those two are what would cover crash and power-cut, which the unmount cannot.

## Recent Changes (July 29, 2026)

- **Config-save WebSocket notifications** (`lib/config-ml/mlConfig.cpp`): `mlConfig.save()` fires `notify_activity("config", "save", "config.json")` / `"devices.json"` (from `lib/www/ws/activity.h`) after each file that actually changed is written — reuses the existing any-task-safe fire-and-forget WS broadcast path, no-op in `MIN_CONFIG` builds. Only the file(s) whose hash changed are written and notified (auto-dirty-detection, see the July 20 entry).
- **`config.firmware` / `config.hardware` auto-sync** (`lib/hardware/Esp.h/.cpp`, `lib/config-ml/mlConfig.cpp`): new `EspClass::getFirmwareVersion()` / `getHardwareVersion()` split `esp_app_get_description()->version` at the LAST `.` — everything before is firmware, the suffix after is hardware. `mlConfig.load()` compares `config.firmware`/`config.hardware` against those and, on any mismatch, updates `_data` and calls `save()` (which then also emits the config-save WS notification). App version format is thus `<firmware>.<hardware>`; the version string is set by the ESP-IDF app descriptor.
- **Timezone: IANA→POSIX resolution** (`lib/config-ml/mlConfig.h/.cpp`, `lib/device/iec/drive.cpp`): newlib on ESP32 has NO IANA tzdata database — `setenv("TZ", "America/New_York")` + `tzset()` silently fails to parse and leaves the clock at UTC with no DST (symptom: `date` shows unadjusted time and an empty `%Z`). New free function `iana_to_posix_tz()` (declared in `mlConfig.h`, ~60-entry table in `mlConfig.cpp`) maps common IANA zone names to POSIX TZ strings (e.g. `EST5EDT,M3.2.0,M11.1.0/2`); unrecognized input passes through unchanged (may already be POSIX). `mlConfig.load()` and drive.cpp's `T-Z` command both resolve via it then call `setenv`/`tzset` DIRECTLY (they intentionally do NOT go through `fnSystem.update_timezone()`, which now just applies whatever POSIX string it's handed). `T-Z` also persists the raw IANA name into `preferences.timezone` + `mlConfig.save()` so it survives reboot and re-resolves identically.
- **LED brightness double-scaling fix** (`lib/display/led_strip.cpp`): `update()` computes `effective = scale_channel(pixel_brightness[i], brightness)` — a product of per-pixel and global brightness. `pixel_brightness[]` was initialized to the GLOBAL brightness (config default 50) in `init()`/`resize()`, so the global control was permanently capped by the stale per-pixel value and `led brightness 255` could never reach max (looked ~half). Per-pixel brightness must default to `255` (a NEUTRAL multiplier); now `effective = 255 * global/255 = global`. Per-pixel remains a true per-pixel modifier for dimming individual LEDs relative to global.
- **LED `persistConfig()` / `reloadConfig()`** (`lib/display/led_strip.h/.cpp`, `lib/console/Commands/DisplayCommands.cpp`, `lib/device/iec/meatloaf.h`): mirror `iecDrive`. `persistConfig()` writes `devices.led_strip.{enabled,count,brightness}` (preserves an existing `enabled`, prefers a pending `set_count()` value over the not-yet-applied live `n_of_leds`); caller invokes `mlConfig.save()`. `reloadConfig()` applies `brightness`/`count` to the live strip (via `set_brightness`/`set_count`, so the buffer resize still happens on the display task's next pass); `enabled` is honored only at `start()` since there's no clean runtime task teardown. `led count`/`led brightness` console commands now call `LEDS.persistConfig()` (replacing the old `persist_led_setting()` helper), and `iecMeatloaf::reloadAllConfig()` calls `LEDS.reloadConfig()` under `#ifdef ENABLE_DISPLAY` so `config load` / boot apply LED settings to the live strip.
- **`cat` trailing newline** (`lib/console/Commands/VFSCommands.cpp`): `cat` now prints `\r\n` after a file's contents. Files with no trailing newline (e.g. `update.php`) previously left the next shell prompt glued onto the last output line, which read as a hang — the read/EOF loop was always correct (`hex` "worked" only because it always ends with its own `\r\n` + summary).
- **`rx`/`tx` transport-agnostic + raw binary** (`lib/console/Commands/XFERCommands.cpp`): replaced the hardcoded `uart_read_bytes(UART_NUM_0, ...)` (which reads a peripheral the console isn't even using on USB-Serial-JTAG / USB-CDC boards — `CONFIG_ESP_CONSOLE_UART_NUM=-1`) with a `console_read_byte()` helper that reads from `stdin` (same `fcntl(O_NONBLOCK)`+`fgetc` idiom as the REPL), so transfers work on every console transport. A `ConsoleRawIOGuard` (RAII) sets BOTH RX and TX line endings to `ESP_LINE_ENDINGS_LF` for the transfer duration and restores the interactive `CR`/`CRLF` defaults on every exit path — the console driver's interactive translation (RX `\r`→`\n`, TX `\n`→`\r\n`, set in `console_settings.c`) otherwise silently corrupts any `\r`/`\n` byte in a binary payload. `tx()` also now waits for the receiver's per-chunk `+` ack (matching `rx()`'s flow control) instead of blasting the whole file.
- **`wget` saves as the real filename** (`lib/console/Commands/VFSCommands.cpp`, `lib/meatloaf/meatloaf.h`, `lib/meatloaf/network/http.cpp`): new virtual `MFile::getDownloadFilename()` (defaults to `name`); `HTTPMFile` overrides it to return the server's `Content-Disposition` filename when present. `wget` resolves the save name via it AFTER `getSourceStream()` has run the request (so the header is parsed) — a URL like `update.php?freenove` that responds with `Content-Disposition: filename=update.txt` now saves as `update.txt`. `name` itself is intentionally NOT changed (it must stay in sync with `path`).

## Recent Changes (July 22, 2026)

- **Boot-time internal-RAM investigation**: added labeled heap checkpoints (`log_heap_checkpoint()` in `src/main.cpp`, `log_wifi_heap_checkpoint()` inside `WiFiManager::start()` in `lib/hardware/fnWiFi.cpp`) at every major stage of `main_setup()` and every sub-step of WiFi init — debug-build-only (compiles to nothing in release, like the existing `Debug_memory()` macro), currently commented out but left in place for reuse. Findings: IEC bus setup costs ~22 KB (task stack, required for real-time timing), display/LEDs ~10.5 KB, but **`esp_wifi_init()` alone accounted for ~66-80 KB and roughly halved the largest contiguous free internal block** — the dominant cost by far, and the direct cause of later task-stack creation failures (httpd, console executor) under `ESP_ERR_HTTPD_TASK`/"Could not start console exec task" once heap got fragmented.
- **WiFi driver buffer tuning** (all `sdkconfig.*` board files + `sdkconfig.defaults`/`sdkconfig.defaults.esp32s3`, both modern `CONFIG_ESP_WIFI_*` and legacy `CONFIG_ESP32_WIFI_*` keys): `STATIC_RX_BUFFER_NUM=10`, `DYNAMIC_RX_BUFFER_NUM=32`, `STATIC_TX_BUFFER_NUM=10`, `CACHE_TX_BUFFER_NUM=16` (PSRAM boards only — that Kconfig option doesn't exist otherwise), `TX_BA_WIN=6`, `RX_BA_WIN=16` on PSRAM boards / `6` on non-PSRAM (matches ESP-IDF's own PSRAM-conditional default exactly), `MGMT_SBUF_NUM=16`. Verified on hardware (lolin-d32-pro): ~24 KB internal RAM recovered at end of boot with no observed WiFi throughput regression (WebDAV transfer tested). `wt9932p4-tiny` (ESP32-P4) has no WiFi component and was left untouched. Three boards (`esp32-s3-dev-kit-n8r8`, `esp32-s3-rgb`, `freenove-esp32-cam`) already had partial tuning from earlier work; they now match the rest. Only `lolin-d32-pro` is hardware-verified — the other 26 boards are compile-verified only (plus one cross-chip-family build check on `esp32-s3-devkitc-1`).
- **`web_server.cpp` / `Console.cpp` failure diagnostics**: `HttpServer::start_server()`'s `httpd_start()` failure path and all three `console_exec` task-creation failure sites (`Console::execAcquire()`, and the late-creation retries in `runCommand()`/`runOnExecutor()`) now log the `esp_err_t` plus `heap_caps_get_free_size()`/`heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` — this is what revealed the fragmentation (not exhaustion) pattern above. `Console.cpp` centralizes the latter in `log_exec_task_create_failure()`.
- **Boot-time FSP/network-drive mount race fixed** (`lib/device/iec/drive.cpp`, `src/main.cpp`): `iecDrive::reloadConfig()` was attempting network-scheme URL restores (`fsp://`, `http://`, ...) via `set_cwd()` immediately after `fnWiFi.start()`, but that call only *initiates* the WiFi connection asynchronously — no route/DNS exists yet, so the attempt failed fast every boot, and the resulting alloc/free churn (session create/destroy) at a heap-pristine moment was a contributing fragmentation source. `reloadConfig()` now returns `bool` (true = deferred because WiFi isn't connected) and skips the `set_cwd()` call in that case; `main_setup()` spawns a one-shot polling task (`reload_network_drives_task`) that waits for `fnWiFi.connected()` then retries. The deep-stack MFile/JSON work (confirmed via crash trace to need ~16 KB, the same class of stack depth `console_exec` needs) runs via the new `Console::runOnExecutor(std::function<void()>)` — a sibling to `runCommand()` that runs an arbitrary function on the executor task instead of a parsed command line — so the retry doesn't pay for a second dedicated big-stack task. The polling task itself only needs ~3 KB.
- **`iecMeatloaf::reloadAllConfig()`** (`lib/device/iec/meatloaf.h`): single source of truth for "apply mlConfig to the live devices" — loops every disk device's `reloadConfig()` plus device 30's own, returns true if any restore was deferred. Used by `main_setup()`'s boot-time call, the WiFi-retry task, and the new `config load` console command, replacing three copies of the same loop.
- **`config load` console command** (`lib/console/Commands/SystemCommands.cpp`): `config load` calls `mlConfig.load()` (re-reads `config.json`/`devices.json` from disk) then `Meatloaf.reloadAllConfig()` so the reload actually reaches the live devices (enabled flag, mounted URL), not just the in-memory JSON. Runs inline — console commands already execute on the 16 KB executor stack, so no separate executor dance is needed here.
- **`exit`/`reboot` moved to CoreCommands, `reboot` now executes directly** (`lib/console/Commands/CoreCommands.h/cpp`, `Commands/SystemCommands.h/cpp`, `Commands/NetworkCommands.h/cpp`, `Console.cpp`): both commands are registered via `registerCoreCommands()` now (previously `registerSystemCommands()`/`registerNetworkCommands()`), matching how they were already effectively always-available. `Console.cpp` gained a shared `do_reboot()` (save config, print, `ESP.restart()`) intercepted as a raw line in both shell entry points (`repl_task()` for serial, `execute()` for TCP/WS) — the same pattern `"exit"` already used — so typing `reboot` bypasses `runCommand()`/the `console_exec` executor entirely and works even when the executor can't be created (memory pressure) or is busy. The registered command versions still exist for `help` visibility and any indirect invocation path.
- **Per-device `iec sleep`/`iec wake <id>`** (`lib/console/Commands/IECCommands.cpp`): extends the existing whole-bus `iec sleep`/`iec wake` (unchanged when no ID is given) to target one device via `IEC.findDevice(id, true)->setActive(false/true)`. A C64-side RESET now also restores a slept device to its persisted enabled state, mirroring the existing bus-level behavior where RESET already undoes a whole-bus `sleep` (`IECBusHandler::task()`'s RESET-pin handling calling `m_enabled = true`). New `iecDrive::restoreActiveFromConfig()` (`drive.h/cpp`) is the cheap counterpart to `reloadConfig()` — JSON lookup only, never touches `m_cwd`/`set_cwd()` — safe to call from `iecDrive::reset()` on the real-time IEC bus task; a drive persistently disabled via config (`enabled: 0`) correctly stays disabled across a RESET rather than springing back. Currently only wired up for `iecDrive`-family devices (drives 8-15, Meatloaf/30) — printer/network/clock devices can still be slept/woken by ID but aren't restored on RESET.
- **`run <script.sh>`** (`lib/console/Commands/CoreCommands.cpp`): the existing `run` command now recognizes a `.sh` argument as a script of console commands (one per line, blank lines and `#` comments skipped), executed via `esp_console_run()` directly rather than `console.runCommand()` — since `run` is already executing on the executor task, submitting back to itself would deadlock. Reports the first failing line. `run test` (the test suite) is unchanged.

## Recent Changes (July 20, 2026)

- **`mlConfig` auto-dirty-detection** (`lib/config-ml/mlConfig.h/cpp`): Removed `mark_config_dirty()`/`mark_devices_dirty()` and the stored dirty-flag fields. `is_config_dirty()`/`is_devices_dirty()` now hash the current `_extract_config()`/`_extract_devices()` output and compare against the hash captured at the last load/save, computed on demand rather than cached in a bool. `save()` hashes both sections up front and returns immediately (no filesystem access at all) if neither changed; a section that did change is written and its stored hash updated. Every call site that used to pair a `mark_*_dirty()` call with `save()` (the `config` console command, `led count`/`led brightness` persistence, `iecDrive::persistConfig()`) now just mutates `mlConfig.data()` and calls `save()` — a hand-forgotten `mark_*_dirty()` can no longer cause a silently-dropped write. Tradeoff: `is_dirty()`/`is_config_dirty()`/`is_devices_dirty()` each now cost a full `dump()` + MD5 of their section per call (the same cost `save()` already paid), so they're fine to check occasionally but not in a tight loop.
- **LED strip runtime pixel count** (`lib/display/led_strip.h/cpp`, `lib/console/Commands/DisplayCommands.cpp`): New console command `led count {0-255}`. `DisplayLEDs::set_count()` records a pending count; the actual buffer resize (`DisplayLEDs::resize()`) runs on the display task's next `service()` pass, since the pixel/DMA buffers are otherwise only ever touched from that task — resizing directly from the console task would race `rotate()`/`fill_all()`. `resize()` allocates the new pixel + DMA buffers first (leaves the old strip intact on allocation failure), swaps them under the SPI mutex, and repaints the idle pattern at the new size. `init()` now sizes the SPI bus `max_transfer_sz` for the 255-LED maximum up front, since the bus can't be reconfigured later. Fixed two latent out-of-bounds bugs this made reachable: `blink()` looped over the compile-time `RGB_LED_COUNT` instead of live `n_of_leds`, and `rotate()` did an UB `memmove` of size -1 at 0 LEDs (now returns early for `n_of_leds <= 1`).
- **LED strip settings persisted + loaded from mlConfig**: `led count`/`led brightness` (the global 3-arg form) now write `devices.led_strip.count`/`brightness` in `devices.json` via a small `persist_led_setting()` helper in `DisplayCommands.cpp`. `DisplayLEDs::start()` reads `devices.led_strip.enabled`/`count`/`brightness` from mlConfig before calling `init()` — `enabled: 0` skips the strip and its display task entirely; a missing `led_strip` key falls back to the compile-time `RGB_LED_COUNT` default (existing installs unaffected). Nothing else reads these settings at runtime — the per-pixel brightness console forms (`led brightness * {v}` / `led brightness {index} {v}`) intentionally aren't persisted, since the config key represents only the single global brightness.
- **`iecDrive::persistConfig()` / `reloadConfig()`** (`lib/device/iec/drive.h/cpp`): Helpers for a drive's `devices.iec.<devnr>` entry. `persistConfig()` writes `enabled` (from `isActive()`) and `url` (from `m_cwd->url`), and sets `type` only if absent so it doesn't clobber `iecMeatloaf`'s `"meatloaf"` type or a hand-edited value; other keys in the entry (`mode`, `media_stack`, `name`) are left untouched. `reloadConfig()` applies `enabled` via `setActive()` and `url` via `set_cwd()` (skipped if empty or already current) — a missing `devices.iec` section or per-drive entry leaves current state unchanged. `persistConfig()` is not yet called anywhere (deliberate follow-up: after a successful mount, or from a console command). `media_stack` isn't touched since no code implements the autoswap stack yet.
- **`reloadConfig()` boot-time crash fix — must run after `fnWiFi.start()`** (`src/main.cpp`, `lib/device/iec/drive.cpp`): `reloadConfig()` was first wired into `iecDrive::begin()`, called synchronously during `Meatloaf.setup(&SYSTEM_BUS)` → `IECBusHandler::attachDevice()`. On real hardware a drive with a persisted network URL (e.g. `fsp://meatloaf.cc/...`) hit `set_cwd()` → `MFSOwner::File()` → `getaddrinfo()` → `netconn_gethostbyname_addrtype()`, which asserts `tcpip_send_msg_wait_sem ... Invalid mbox` and hard-crashes — the LWIP tcpip task doesn't exist yet because `esp_netif_init()` (called inside `fnWiFi.start()`, itself called well after `SYSTEM_BUS.setup()` in `main_setup()`) hasn't run. `begin()` no longer calls `reloadConfig()`; instead `main_setup()` calls it explicitly right after `fnWiFi.start()`/`SessionBroker::setup()`, looping `Meatloaf.get_disks(i)->disk_dev.reloadConfig()` for `i` in `[0, MAX_DISK_DEVICES)` plus `Meatloaf.reloadConfig()` itself (device 30). **General rule**: any code path that can reach a network-scheme `MFile` (`fsp://`, `http://`, `smb://`, ...) — via `set_cwd()`, `MFSOwner::File()`, or a fresh session `connect()` — must not run before `fnWiFi.start()` in the boot sequence, even if it's only reachable for some device configs (local-only configs won't crash, masking the bug until a network URL is persisted).
- **`IECBusHandler::end()` + `sleep`/`wake` console commands** (`lib/bus/iec/IECBusHandler.h/cpp`, `lib/console/Commands/IECCommands.cpp`): `end()` is the counterpart to `begin()` — releases CLK/DATA to high-Z, disables the CTRL ATN→DATA hardware coupling if present, and detaches the ATN interrupt (clearing the static `s_bushandler` ownership pointer so a later `begin()` re-attaches it). `sleep`/`wake` console commands call `IEC.end()`/`IEC.begin()`. **`m_flags==0xFF` is NOT safe as a "bus disabled" sentinel** even though the constructor originally used it that way and a comment claimed `task()` checked it first: `task()` does many `m_flags|=`/`&=~` updates throughout its body (ATN/transaction state), so a `task()` call already in flight when `end()` writes `m_flags=0xFF` can clobber the sentinel back to a non-`0xFF` value via its own unrelated bitwise ops, silently un-disabling the bus on the next call — this is exactly what caused `sleep` to trigger a device-reset cascade and reconnect a persisted network mount immediately after "IEC bus disabled." printed. Fix: a dedicated `volatile bool m_enabled`, written ONLY by `begin()` (`true`, as its last statement) and `end()` (`false`, as its first statement) — nothing else in the class ever touches it, so it can't be corrupted by task()'s normal flag bookkeeping. `task()`'s very first line is now `if (!m_enabled) return;`, gating the RESET-pin check too (previously that block ran unconditionally on every call regardless of sleep state, calling every device's `reset()` whenever the RESET pin transitioned — the immediate, deterministic trigger behind the reported bug, not just a rare race).

## Recent Changes (July 19, 2026)

- **Class diagram docs (`docs/classes/`)**: complete coverage of ALL 364 project classes, one def file per class, organized into 13 aspect diagrams + 2 inheritance views + an overview. Pipeline: `gen_defs.js` parses every `lib/**/*.h` header into `defs/<Class>.mmd` (marker `%%gen` on generated files — edit a def and remove the marker to make it curated/never-overwritten; 46 curated defs carry ` - description` suffixes on non-obvious publics) plus `defs/_manifest.json` (class → header/bases/area); `gen_templates.js` regenerates `src/<area>.mmd` templates (auto `%%include` per class, inheritance edges, cross-area base stubs, hand content appended from `src/extra/<area>.mmd`) and ASSERTS every class appears in a diagram; `build.js` assembles the committed top-level `.mmd`; `build.js check` for staleness. All 16 diagrams render-validated with mermaid-cli (via Edge + PUPPETEER_EXECUTABLE_PATH, no Chromium download). See docs/classes/README.md.

## Recent Changes (July 18, 2026)

- **M2I support** (`lib/meatloaf/media/disk/m2i.h/cpp`, new; registered as `m2iFS` in meatloaf.cpp): MMC2IEC/sd2iec text index format — line 1 = 16-char disk title, then `T:DOSNAME.EXT :CBMNAME` lines (T = P/S/U file; D = DEL separator line — blank dosname, listed with size 0 but never loadable; '-' free slots dropped). Entry data lives in host files NEXT TO the .m2i; `M2IMStream::seekPath()` resolves the sibling URL (lowercase-filename fallback for case-sensitive filesystems — M2I comes from FAT) and `readFile()` delegates to that file's stream. The whole index is parsed once in `readHeader()` (files are a few hundred bytes): parsing is line-based at the ':' separators, NOT fixed-width — real-world files have unpadded CBM names, a UTF-8 BOM before the title, extension-less dosnames, and '/' inside CBM names (escaped as '\\' in listings like T64). Read-only. All 314 archive samples parse. `resolveEntry()` locates each entry's host file once (exact dosname, then lowercase — the case fallback applies to LISTING and load alike) and caches its URL + size in the entry; listings show real sizes and `seekPath()` reuses the cached URL. Candidates are validated with `MFile::exists()` (the filesystem's REAL check), NOT by opening a stream — network opens can be lazy (`fsp_fopen` succeeds for any name and only `fsp_stat` fails), which silently accepted wrong-case names.
- **`FSPMFile::exists()` fix** (`lib/meatloaf/network/fsp.cpp`): it stat'd `pathInStream` — which is EMPTY for a plain FSP file (only set for paths inside containers) — so existence was never actually checked over FSP. Now stats `path`, matching `isDirectory()`. Affected anything relying on `exists()` over FSP (drive open checks, M2I sibling resolution, WebDAV).

- **Tape engine replaced: wav2prg → TAPClean** (`components/tapclean/` new, `components/wav2prg/` removed, `lib/meatloaf/media/tape/tape_decoder.h/cpp` rewritten): TAPClean 0.39 (Final TAP 2.76 lineage, GPL v2+) vendored from `C:/Users/jjohn/source/tapclean` as an in-memory analysis library. Motivation: corpus test over `.archive/tap/` (35 real tapes) — TAPClean fully recognized ~20 commercial tapes (Cyberload, Visiload, US Gold, Novaload, Freeload, ...) where wav2prg only got the Kernal boot chunk or garbage; wav2prg was better only on the 6 Turbotape-64-fast tapes, so that loader was ported as a new TAPClean scanner (`src/scanners/turbotape_fast.c`, ft entries `TTFAST_HEAD/TTFAST_DATA`, tp/sp/lp = $0E/$0B/$12, accepts header type $61) — extraction verified byte-identical (md5) to wav2prg's.
- **Vendoring diffs** (all under `#ifdef TAPCLEAN_EMBEDDED`): CLI/report/clean/persistence/tap2audio code compiled out of `main.c`; 1.36 MB of static BSS heap-ified (`info` 1 MB→64 KB heap since it is reset per block in `describe_blocks()`, `lin`/`tmp`, `cbm_program`, `prg[]` all allocated PSRAM-first by `tapclean_init()`); `database_make_prg_db()`'s 8 KB stack array heap-ified; `prg_t` gained `blkidend` so the API can report where a united program ends on tape. Public API: `components/tapclean/include/tapclean_api.h` (`tapclean_load_buffer` takes ownership of a malloc'd image, DC2N auto-converted; `tapclean_analyze_tap(unite)`; block/prg accessors; `tapclean_duration_ms`/`offset_at_ms`). Engine is global-state — NOT thread-safe; `TapeDecoder` serializes with a static mutex.
- **`TapeDecoder` rewrite**: whole image → PSRAM buffer (DMP/HTAP/TAP-v2 stream-converted to TAP v1 first, using the old proven pulse-walk; halfwave pairs summed), one TAPClean scan at `open()`, entries copied out (headers folded in for names — a `_DATA` name suffix is stripped; CBM/turbo repeat copies deduped by identical content), then `tapclean_unload()` frees the image before `open()` returns. `nextProgram(from_offset)` serves from the entry list; `offsetAtTime()` snaps to the program grid. Tape decode now effectively requires PSRAM. Corpus result: 27/33 real tapes produce checksum-verified programs (identical CRCs to wav2prg where both worked); failures are tapes neither engine handles (Defender of the Crown, 720 Degrees) or truncated/header-only images (Crystal Castles). NOT hardware-tested yet: the whole scan cost lands on the first directory request (PC times suggest roughly 5–60 s on ESP32 for 0.2–3 MB tapes, one-time per tape open).
- **Payload-only tape listing**: a CBM (Kernal-loaded) entry that a non-CBM entry follows is a turbo-loader boot stub — dropped from the listing, its name (the game's name) transferred to the following payload (e.g. atari collection lists 12 named game payloads instead of 24 boot+payload pairs). Pure Kernal tapes (all entries CBM, e.g. Defender 64) keep every entry. Filter lives in `TapeDecoder::harvestEntries()`; `tapclean_prg_t.is_cbm_data` was added to the API for it. Unnamed entries list under the media file's name with NO load-address suffix (duplicates resolve positionally — loads search forward from the current tape position).
- **Shared tape state (`TapeState`, tap.h/tap.cpp)**: `MFile::getSourceStream()` creates a FRESH decoded stream per open while directory listings use the ImageBroker instance — so the decoder + datasette position (`tape_pos`/`current`) moved into a `TapeState` shared via a weak_ptr registry keyed by container URL; `TAPMStream` members are references aliasing into it (declaration order matters for the ctor init list). This makes `LOAD"*",8` and console `cat *`/`hex *` serve the program at the current tape position after a listing, and prevents a full TAPClean re-scan on every file open.
- **`_MEAT_NO_DATA_AVAIL` sentinel fix (meatloaf.h / meat_buffer.h)**: the "no data available" sentinel for non-blocking stream reads was `std::ios_base::eofbit` (== 2), so ANY read legitimately returning 2 bytes was misread as NDA by `mfilebuf::underflow()` — the bytes were dropped, no `setg()` ran, and the sentinel leaked to `std::istream::uflow()` which dereferenced past `egptr` and pushed `gptr` beyond it; the `gptr() == egptr()` refill guard then never fired again and reads streamed unbounded heap memory (symptom: `hex *` on a tape entry dumped the entry then endless heap contents incl. `BAAD5678`/`ABBA1234` heap canaries). Sentinel is now `0xFFFFFFFEu` (int -2; distinct from EOF -1 and every byte count), `ndabit` stays `eofbit` (stream-state semantics unchanged — `iec_pipe.h` `nda()` checks still work), and both underflow guards are `>=` so a pointer slip can never serve past the buffer.
- **Progressive tape scanning**: no tape format is fully downloaded before listing. `TapeDecoder::open()` only parses the header; `nextProgram()`/`offsetAtTime()`/`totalMs()` fetch and scan a growing prefix window (512 KB of CONTAINER bytes, then doubling, tail merged under 128 KB) on demand via `extendScan()`/`fetchTo()`/`scanWindow()`. DMP/HTAP/TAP-v2 sources are converted to TAP v1 ON THE FLY as they stream in (`fetchTo` pulls values via `nextValue` through a 16 KB stream window and appends v1 pulses via `appendValue`; the synthesized v1 header's size field is patched per window; growable buffer since long pauses expand; entry offsets refer to the converted image). The engine BORROWS the image buffer (`tapclean_load_buffer_ref` + `tapclean_tmem_borrowed` flag in main.c — unload skips the free), so each window re-scan needs no copy; `entries` is rebuilt per window and the LAST entry of a partial window is withheld until a later entry or the tape end confirms it complete (a window-truncated block can never be served wrong — corpus-verified identical results to the full scan, and a synthesized pacman DMP decodes byte-identical through the streamed conversion). Fetch/convert failures well short of EOF leave state intact: the next directory request resumes where the transfer stopped. Once fully scanned the image buffer is freed (same transient memory model).
- **Scan speed + progress**: four mechanisms keep tape listing fast and visibly alive: (1) `idloader()` gained a code-signature match for the Turbo Tape 64 fast stub (`LID_TTFAST`) — identified tapes scan with just pause+CBM+that loader instead of ~90 scanners; (2) early scan exit (embedded-only, in `database_add_blk_def_ex`): when found data blocks + pauses account for ≥97% of the tape, `aborted` is set and `search_tap()` skips the remaining scanners (cleared in `tapclean_analyze_tap`; corpus-verified zero losses); (3) `readttbit()`/`find_pilot()` call `tapclean_scan_yield()` every ~262K reads — `vTaskDelay(1)` feeds the task watchdog (a minutes-long CPU-bound scan otherwise looks locked up) and prints a progress dot; (4) the image is bulk-read straight into the buffer (no 4 KB window hops) with KB progress via `Debug_printv`, and the per-scanner `msgout()` names print as scan progress (`quiet` stays FALSE). For network tapes, `#cache=sd` remains the way to avoid re-fetching the image.
- **Engine memory model (lazy, PSRAM-only, transient)**: nothing is allocated at boot. `tapclean_init()` (called from the first `tapclean_load_buffer`) heap-allocates every engine structure PSRAM-first — `info`/`lin`/`tmp`/`cbm_program`, both databases (`blk` pointer array + `prg`), the `tap` struct (macro `tap` → `(*tapclean_tap)` in mydefs.h) and the writable `ft[]` format table (copied from a const flash `ft_defaults`). mydefs.h also redirects the engine's `malloc()` to `tapclean_psram_malloc()` (tapclean_alloc.c) so the thousands of small scan allocations (2000 blk_t structs ≈ 176 KB, every scanner's `dd` buffer) go to PSRAM instead of exhausting internal DRAM (≤512 B allocations are internal by default). After each scan `TapeDecoder::analyzeImage()` calls `tapclean_shutdown()` — the engine holds ZERO memory between scans; only the decoded programs (in the shared `TapeState`) stay resident while a tape is mounted. This dropped tapclean's static internal-RAM cost from ~21.5 KB to ~4.2 KB (firmware static RAM 123,280 → 105,968 B) — the original static footprint had eaten the boot margin and `execAcquire()` could no longer create the 16 KB console exec task.
- **Flash-text budget + PIO -O gotcha**: adding the engine overflowed `iram0_2_seg` (the ESP32's ~3.3 MB flash-text window) by ~8 KB and DRAM by ~3 KB. Fixes: tapclean globals `tmp`/`debug` renamed via mydefs.h macros (collided with lib/utils and lib/sam), `ldrswt[]` made const in the embedded build (~4.4 KB DRAM → flash), and tapclean + sqlite3 compiled `-Os` — **via `#pragma GCC optimize("Os")`** in `components/tapclean/src/mydefs.h` and `components/sqlite3/private_include/config_ext.h`. The PlatformIO ESP-IDF builder honors `target_compile_definitions` but silently strips per-component `-O` from `target_compile_options` (build.ninja shows the flag; the compile doesn't use it — verified by identical object sizes after a full clean). Pragma savings: sqlite3 622→560 KB, tapclean 161→146 KB text. Result: firmware links with Flash 41.9%, RAM 37.6% on lolin-d32-pro.

## Recent Changes (July 13-15, 2026)

- **Streamed D64-family file writing** (`lib/meatloaf/media/disk/d64.h/cpp`): `seekPath()` in write mode creates files; blocks are claimed one at a time (`beginFileWrite`/`writeFileNew`/`finalizeFileWrite`), each written with its T/S link once the next block is known; the directory entry (first free slot, or a new directory block honoring the directory interleave) is committed at `close()` when the block count is known. `SAVE"@:file"` scratches and reuses the slot. Failures deallocate every claimed block. BAM helpers rewritten generically over `block_allocation_map` (fixes a double-offset bug that pointed all BAM access one track off; supports count-byte and bitmap-only records). `getNextFreeBlock()` implements the physical-disk algorithm (near directory track first, same-track interleave, move outward, other side, full sweep). `blocksFree()` uses `getTrackFreeCount()`. Fixes: D81 side-2 BAM sector 0 → 2 (was the header sector!), DNP BAM byte_count 8 → 32, name lookup skips scratched entries, removed UB `seekFreeEntry()`.
- **Subdirectory navigation core** (`d64.h/cpp`): `seekDirectory()`/`resolvePath()`/`enterDirectory()` walk '/'-separated in-image paths. CMD native `DIR` entries and 1581 `CBM` sub-partitions both enter via their header block (bytes 0/1 = directory chain). Listing entry URLs now include the in-image path. `exists()`/`isDirectory()`/`rewindDirectory()` use the walk.
- **DHD — CMD HD images** (`media/hd/dhd.h/cpp`, new): see Important Notes above. Partition table read from the system partition (64 KiB-boundary scan for the CMD HD boot magic at +$5F0; entries: type +$02, 16-byte name +$05, 3-byte BE start/size LBA +$15/+$1D). `iecDrive::changePartition()` implements `CP`.
- **HDD — IDE64 CFS rewrite** (`media/hd/hdd.h/cpp`): structs corrected against the real CFS 0.11 spec (16-byte names; LBA = `(b0&0x0F)<<24|b1<<16|b2<<8|b3`; file size at $10, data tree at $14, 3-char type at $19; partition root-directory pointer at +$1C). Partitions listed at image root (default partition fallback for bare paths), subdirectories, multi-sector directories via the sliced NEXTS pointer (2-bit slices across the 16 entry pointers), and full balanced data-tree traversal (depth from file size, SLICE-assembled next-tree pointers, holes read as $00, one-sector tree cache). Read-only.
- **Tape support — wav2prg port** (`components/wav2prg/`, `lib/meatloaf/media/tape/`): WAV-PRG 4.2.1 core + all 41 loader plugins + 29 observer modules vendored with static registration (`WAV2PRG_OBSERVER` gained a name argument; 64 KB `program_block` moved off the stack; incremental `wav2prg_continuation` mode added: analysis stops after each kept block and the loader/observer chain resumes on the next call). `TapeDecoder` streams pulses from the container through a 1 KB window — TAP v0/v1/v2, DC2N DMP (16-bit samples, 0xFFFF overflow, counter-rate downsample), HTAP per the Manosoft V0/2.0 spec (signed 16-bit halfwaves in 0.5 µs ticks; pauses `0000 0000` + 32-bit µs). `TAPMStream`/`TAPMFile` implement the sequential listing, `.idx` sidecar (with new optional `:<length>` field — old files still parse), `buildIndex()`, and the settable tape counter. `tapFS` handles `.tap/.dmp/.htap`; the old bespoke `media/tape/loaders/` stubs were removed.
- **Drive status additions** (`drive.cpp`): `ST_DISK_FULL` (72), `ST_DIR_ERROR` (71), `ST_PARTITION_SELECTED` (02), `ST_PARTITION_ILLEGAL` (77) messages; `T-C`/`T-I` tape commands beside the RTC `T-R`/`T-W`.

## Recent Changes (July 4, 2026) — final verified state

All items below were re-landed **one at a time with a hardware test between each** after a full revert to the pre-optimization baseline; the July 2–3 iterations that led here (lazy REPL watcher, 16 KB per-connect session task, 307-redirect handover, TCP window caps, 32 KB SPIRAM reserve, 16 KB httpd stack) are superseded and their sdkconfig experiments reverted (`TCP_WND`/`TCP_SND_BUF` back to 65534, `SPIRAM_MALLOC_RESERVE_INTERNAL` back to 512).

- **Console executor architecture** (`Console.h/cpp`, `tcpsvr.h/cpp`, `NetworkCommands.cpp`, `main.cpp`): see the "Console/task-stack architecture" Important Note. Key facts: task stacks are internal-DRAM only (no PSRAM fallback, proven empirically); all commands run on a refcounted 16 KB `console_exec` task; I/O shells are small persistent boot-created tasks; `exit` is intercepted shell-side so it works without the executor; the executor adopts the TCP stdout tee for remote-origin commands; `runCommand` retries executor creation so consoles self-heal when memory frees.
- **TCP console listener resilience** (`tcpsvr.cpp`): the listener never exits on errors — socket/bind/listen/accept failures close the fd (close-before-rebind prevents a leaked fd holding the port in LISTEN with nobody accepting → `EADDRINUSE` forever), delay 1 s, retry. `start()` is idempotent; `stop()` (WiFi drop) closes sockets but leaves the task alive to rebind. Session hand-off via task notifications to the persistent 4 KB `tcp_session` worker.
- **Internal-RAM budget** (`iec.cpp`, `sdkconfig.lolin-d32-pro`): `bus_iec` stack 32768 → 20480 (ps HWM showed <800 B used; the 32 KB was an unmeasured safety bump from the dhansel-driver switch); `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM` 16 → 10 + `RX_BA_WIN` 32 → 16 (~10 KB internal freed; dynamic RX buffers live in PSRAM via `TRY_ALLOCATE_WIFI_LWIP`). Result: ~24 KB internal free at idle with web + both consoles + IEC coexisting — previously httpd and the console executor could not run simultaneously.
- **WebDAV transfer speed** (each step tested individually):
  1. `TCP_NODELAY` on every accepted httpd socket (`httpd_open_fn`) — Nagle held httpd's separate status/header/chunk writes for the client's delayed ACK, taxing every round trip.
  2. `config.lru_purge_enable = true` — with the pool full of idle keep-alive connections (a browser alone holds ~6), new clients were refused; LRU purge closes the oldest idle session instead. This is what makes browser + WebDAV client coexist.
  3. WebDAV 404 fast path (`webdav_handler`): non-GET errors return the bare status line — the HTML error page cost 3–4 LittleFS opens per PROPFIND miss, and bulk uploads PROPFIND every file before its PUT. Browser GETs keep the HTML page.
  4. `doGet`/`doPut` chunk size 8192 → 16384 (PSRAM buffer).
  5. Persistent connections: removed `Connection: close` from `Response::setDavHeaders()` — no TCP handshake + slow-start per request. Safe now because of (2) and the internal-RAM budget above; when this was first tried on the old memory baseline it exhausted internal heap (lwip queues TCP windows in internal RAM) and newlib aborted in `lock_init_generic` on a failed FILE-lock mutex allocation.
  - Flash (LittleFS) program speed is the remaining bottleneck for uploads to `/`; uploads to `/sd` are faster.
- **Web server lazy start** (unchanged from July 2, still current): `www_lazy` bare listener holds port 80 until first access, then starts httpd and transparently proxies the already-accepted connections to it over loopback (`CONFIG_LWIP_NETIF_LOOPBACK=y`; a 307-redirect handover failed for browsers' parallel connections and redirect-blind WebDAV clients). Requires `SO_REUSEADDR` on the lazy socket + `CONFIG_LWIP_SO_REUSE=y`.

## Recent Changes (June 22, 2026)

- **`gzip` console command** (`lib/console/Commands/VFSCommands.cpp`): New command `gzip <source> [dest.gz]` compresses any file to gzip level 9 using a 32 KB PSRAM-backed buffer. Prints progress every 512 KB. Available on flash and SD; not guarded by `SD_CARD`. Uses `<zlib.h>` (component `components/zlib/`).
- **`unzip` console command** (`lib/console/Commands/VFSCommands.cpp`): New command `unzip <archive> [dest_folder]` extracts any libarchive-supported format (zip, tar, gz, bz2, 7z, rar, etc.) to a destination directory. Prints each entry with its uncompressed size; shows byte-level progress every 256 KB for entries ≥ 512 KB. Reports total entries and total bytes on completion. Uses `archive_read_open_filename` + `archive_write_disk`. Guarded by `#ifndef MIN_CONFIG` (libarchive excluded from slim builds).
- **`updatedb` auto-compress** (`lib/console/Commands/VFSCommands.cpp`): `updatedb_compress_gz()` is now called at the end of `updatedb_fts_rebuild()` so `/sd/.locate.gz` is always regenerated after both `updatedb start` (full scan) and `updatedb fts` (standalone FTS rebuild). Upgraded to a 32 KB PSRAM buffer with 512 KB progress intervals. Forward declaration added so `updatedb_fts_rebuild()` can call it before the definition.
- **Proxy Referer fallback** (`lib/www/proxy/proxy.cpp`): When no `X-Referer` header is supplied, `Referer` is set to the stored proxy base URL if one is active, otherwise falls back to `target_url` itself. `#base=1` URL fragment signals `proxy_handler` to store the origin as the active proxy base; fragment is always stripped before the upstream request is made.

## Recent Changes (June 18, 2026)

- **`lib/www/` restructure**: Renamed `cHttpdServer` → `HttpServer`, `oHttpdServer` → `httpServer`. Deleted all `httpd_*.h/cpp` files. Created `web_server.h/cpp` for the lifecycle class; `proxy/proxy.h/cpp`, `ws/ws.h/cpp`, and `webdav/handler.h/cpp` for sub-modules as free functions. Moved `ws_command.h/cpp` into `lib/www/ws/`. Updated `lib/hardware/fnWiFi.cpp` and `lib/console/Commands/NetworkCommands.cpp` to use the new names and headers.
- **HTTP proxy** (`lib/www/proxy/proxy.cpp`): Added `/proxy?<url>` GET/POST handler. Probes a known set of request headers (preferring `X-<Name>` over `<Name>` to support CORS-restricted clients), forwards them stripped of the `X-` prefix, streams the upstream response body back. Uses `esp_http_client` streaming API: `open()` → optional `write()` for POST body → `fetch_headers()` → `read()` loop.
- **SQLite scan DB fix** (`lib/console/Commands/VFSCommands.cpp`): `sqlite3_close(db)` moved above the `updatedb_fts_rebuild()` call so `sqlite3_shutdown()` inside the rebuild succeeds and the PSRAM allocator swap takes effect.
- **`locate` wildcard fix** (`lib/console/Commands/VFSCommands.cpp`): Patterns with a leading wildcard or `?` skip FTS (FTS5 only supports trailing `*`) and go directly to LIKE. FTS is still tried first for plain terms; LIKE fallback runs when FTS returns 0 results.
- **FTS progress output fix** (`lib/console/Commands/VFSCommands.cpp`): Progress lines changed from `\r` to `\r\n` so serial buffers flush on each line.

## Recent Changes (June 13, 2026)

- **`MFile::exists()` base class fix** (`lib/meatloaf/meatloaf.cpp`): Changed default `exists()` to use `stat()` for local paths (empty `scheme`) so media-extension filesystems (D64, archive, etc.) that set `_exists = true` in their constructors no longer falsely report existence for non-existent files. Network paths (non-empty `scheme`) continue to use `_exists`.
- **WebDAV media file false-existence fix** (`lib/www/webdav/webdav_server.cpp`): Added `webdav_mfile()` helper replacing all `MFSOwner::File()` calls for local paths in WebDAV operations. Helper logic: for paths with `://` use `MFSOwner::File()` directly; for bare paths call `MFSOwner::File()` first — if result has a non-empty `scheme` (i.e. a `.config` base_url redirect resolved to a network URL), return that; otherwise fall back to `FlashMFile` so `.d64`, `.gz`, `.zip`, etc. check real on-disk existence via `stat()`. Applied to `doPropfind`, `doProppatch`, `doGet`, `doCopy`, `doMove`, `doMkcol`, `mfile_copy_recursive`. (`doPut` and `doHead` were fixed earlier with an inline `://` guard.)
- **Config/devices JSON structure change** (`lib/config-ml/mlConfig.cpp`): Split changed — `config.json` stores everything except the `"devices"` key; `devices.json` stores `{"devices": {"iec": {...}, "ps2": 0, ...}}`. Previously the outer key was `"iec"` wrapping `"devices"`. Updated `_extract_config()`, `_extract_devices()`, `load()`, and dirty-flag selection in `SystemCommands.cpp`.
- **HTTP `send_http_error()` status code fix** (`lib/www/web_server.cpp`): Function was never calling `httpd_resp_set_status()`, so all error responses were sent with HTTP 200 OK status. Added switch/set at the top of the function for 404, 500, and generic errors.
- **HTTP server `.config` redirect fallback** (`lib/www/web_server.cpp`, `send_file()`): After a flash open fails, falls back to `MFSOwner::File(uri)` so paths under `.config`-configured mount points (e.g. `/zimmers.net/pub/00INDEX` with `base_url=ftp://zimmers.net`) are fetched from the network URL rather than returning 404.
- **`FlashMFile::isDirectory()` fix** (`lib/meatloaf/device/flash.cpp`): Was ignoring the `stat()` return value; uninitialized `info.st_mode` on failure could be misread as a directory. Fixed to return false when `stat()` fails.
- **`cat` EOF fix** (`lib/console/Commands/VFSCommands.cpp`): Classic C++ `get()`/`eof()` ordering bug — `get()` on the last valid byte does not set eofbit until the next empty read, so the EOF sentinel was printed. Fixed with an `if(!istream.eof())` guard after each `get()`.

## Recent Changes (June 10, 2026)

- **Boot loop fix** (`lib/display/lcd.h`, `lib/display/lcd.cpp`): Added `#ifdef PIN_TFT_MOSI` guard so HAGL is only included and `hagl_init()` only called on boards that define display pins. A no-op stub `DisplayLCD` class is compiled for all other boards, preserving call-site compatibility. Root cause: IDF 5.5→5.4 sdkconfig regeneration injected HAGL Kconfig defaults (CS=14) conflicting with `PIN_ETHERNET_RESET=14` on the freenove board.
- **Systematic console baudrate fix** (all `sdkconfig.*` files, `sdkconfig.defaults`, `sdkconfig.defaults.esp32s3`): All boards were using `CONFIG_ESP_CONSOLE_UART_DEFAULT` at 115200 baud; monitor is configured at 2 Mbps. Fixed all boards to `CONFIG_ESP_CONSOLE_UART_CUSTOM=y` at 2000000 in both the modern and legacy compat blocks. Both defaults files now include the complete four-line console block.
- **UART clock source fix** (`lib/console/console_settings.c`): Changed clock source from `UART_SCLK_REF_TICK` (1 MHz, max ~250 kbps) to `UART_SCLK_DEFAULT` (APB 80 MHz on ESP32, XTAL 40 MHz on S3). `UART_SCLK_REF_TICK` physically cannot generate 2 Mbps — the divider would be 0.5. Root cause of garbage console output on lolin-d32-pro after the baudrate fix.
- **HAGL pin defaults** (`sdkconfig.defaults`, `sdkconfig.defaults.esp32s3`, all non-display board sdkconfigs): All six `CONFIG_MIPI_DISPLAY_PIN_*` defaults set to -1. Prevents GPIO conflicts when IDF regenerates a sdkconfig for a board without a display.
- **`CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y`** (all PSRAM board sdkconfigs, `sdkconfig.defaults`): ⚠️ **SUPERSEDED — this option does not do what this entry claimed.** It was added believing it enables PSRAM fallback for FreeRTOS task stack allocation. It does not: it is not an effective option on this IDF, and `xTaskCreate`/`xTaskCreatePinnedToCore` still allocate stacks from internal DRAM only and still fail with `ESP_ERR_NO_MEM` under fragmentation (proven empirically July 3 2026 — 16 KB *and* 6 KB task creations failed with 3.7 MB PSRAM free). The key is left in the sdkconfigs but must not be relied on. See "Task stacks are internal-DRAM only — there is NO PSRAM fallback" under *Internal Heap and PSRAM Allocation*, and the July 22 2026 console/task-stack entry. The `esp_ping_start(NULL)` hard fault was really fixed by the `esp_err_t` return check in the same June 10 entry below, not by this key.
- **Ping command hardening** (`lib/console/Commands/NetworkCommands.cpp`): Added `esp_err_t` check on `esp_ping_new_session()` with a clean error message on failure. Fixed inverted `esp_ping_delete_session` / `esp_ping_stop` call order (delete before stop caused use-after-free). Removed the 4096-byte task stack override (restores the 2048-byte IDF default).
- **`mlConfig` PSRAM allocator** (`lib/config-ml/mlConfig.h`, `mlConfig.cpp`, `lib/console/Commands/SystemCommands.cpp`): Added `PsramAllocator<T>` and `psram_json` (`nlohmann::basic_json` with PSRAM allocator). `MeatloafConfig::_data` changed from `nlohmann::json` to `psram_json` so all JSON node allocations (map entries, array slots, string objects) land in PSRAM instead of internal DRAM. Falls back to internal heap on boards without SPIRAM.
