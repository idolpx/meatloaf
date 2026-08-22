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

## Common Development Patterns

### Adding a New Network Protocol

1. Create new class in `lib/meatloaf/network/` extending `MStream`
2. Implement `getDecodedStream()` or override `getSourceStream()` for bottom streams
3. Register URL scheme in the stream factory/resolver
4. See detailed architecture documentation in `lib/meatloaf/AGENTS.md` (includes SMB, HTTP, and other protocol implementations). `lib/meatloaf/CLAUDE.md` is only a bridge to it and holds no content.

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
   - Session keyed as `"archive:" + archiveUrl` (one colon, no slashes — verified 2026-08-16; the constructor and both `find`/`add` sites agree), managed by SessionBroker with keep-alive disabled. It never goes through `SessionBroker::obtain()`, so its `getScheme()` is unused

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

**`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`** — allocations at or below this threshold stay in internal DRAM; larger ones go to PSRAM. The IDF default is 1024; **every PSRAM board here uses 128** (2026-08-12). It was 512, which captured exactly the allocations a file-heavy workload is made of — the FATFS per-file sector cache (512 B), the LFN working buffer (~512 B at `CONFIG_FATFS_MAX_LFN=255`), newlib `FILE`s, and most `MFile`/`std::string` traffic, about 1.5 KB of internal DRAM per open file — which drove internal free below 2 KB and broke SD I/O (the SDMMC DMA bounce buffer has no PSRAM fallback) and `fopen()` (its newlib lock must be internal). Lowering it pushes those to PSRAM at the cost of cache-miss latency on small allocations. The 7 non-PSRAM configs do not have this key.

**`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`** — must be **32768** (the IDF default) on all PSRAM boards. This carves out an internal-RAM pool that plain `malloc()` cannot touch, reserved for allocations that have no PSRAM fallback: FreeRTOS locks/semaphores, task stacks, DMA buffers. It was set to 512 (and 0 in `sdkconfig.defaults`), which let httpd session buffers and socket structs drain internal heap to zero under web load — the next `fopen()` then aborted in newlib's `lock_init_generic` (a mutex allocation that must be internal returned NULL). With the reserve restored, ordinary allocations spill to PSRAM when internal gets tight and lock/stack allocations always succeed. **Measured again on 2026-08-09** (lolin-d32-pro, hardware): at 512 an `updatedb` scan hit a wall of `sdmmc_read_sectors: not enough mem` the moment the lazy web server started, because the SDMMC driver needs a 512-byte `MALLOC_CAP_DMA` bounce buffer per sector and httpd's ordinary allocations had taken the internal heap. At 32768 those failures vanished entirely and the scan ran to completion. It is now 32768 on all 21 PSRAM boards and in `sdkconfig.defaults`; the 6 non-PSRAM boards do not have the key.

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
105 cases, 100 passing, 5 deliberate scenario skips — write, delete and unscratch, each run once
per format. Nine engine bugs found and fixed — findings in
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
- **A cache must hand back BYTES, never a decoded stream — `MSession::CachedFile::openStream()` resolves its SD path with `MFSOwner::File(path, /*default_fs=*/true)`.** A cache entry is stored under the remote file's own NAME, so resolving it normally re-applies extension sniffing and `getSourceStream()` then wraps it in a decoder: caching `xmasdemo.rp9` produced an `ArchiveMStream` over the cached bytes, with an empty `pathInStream` so it was never seeked to an entry, reporting `size() == 0` and reading nothing. The CALLER's own decoder then bid on zero bytes and reported `Unrecognized archive format` — a failure that names the archive layer while the fault is two levels down in the cache. **The caller is the one that knows what the content is and wraps its own decoder; the cache must be transparent.** This affected EVERY container with a recognised extension (`.rp9`, `.zip`, `.d64`, `.gz` …) on any protocol, and was invisible for extension-less files, which is why FTP `hex wedge` and `cp ccgms` had worked. `default_fs=true` forces `defaultFS` for both target and source and is the established "raw bytes" primitive — `driveMemory::setROM()` reads its ROMs off `/sd` the same way.
- **`Unrecognized archive format` with `srcSize[0]` in the same line means the SOURCE is empty, not that the archive is bad.** `Archive::open()`'s failure line prints the first bytes with the source's size and position for exactly this reason; an empty `first bytes[]` with `srcSize[0]` says nothing ever reached libarchive. A `seekOk[0]` on the preceding line is the same signal one step earlier. Check the source stream before suspecting the format code.
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
- **CMD media images (DHD, D1M/D2M/D4M)**: `DHDImageRegistry` (lib/meatloaf/media/hd/dhd.h/cpp) keeps per-image partition tables + the "currently selected partition" (default partition on first use). `getFile()` (shared `DHDCreatePartitionFile()`, used by `dhdFS` and `dxmFS` in media/disk/dxm.h) returns a `D64MFile`/`D71MFile`/`D81MFile`/`DNPMFile`-based wrapper decoding through a `DHDOffsetStream` window at the partition offset — a D1M thus resolves to D81 or DNP by its partition type. HD images: system partition scanned on 64 KiB boundaries ("CMD HD" boot magic), table at sys+65536, **a maximum of 254 partitions**, numbered 1-254 (table entry 0 is the system partition, which supplies the disk label and holds the partition table; its type byte is `$FF` in every real image). **There is no entry 255** — `vdrive.c:1180` caps at 254 ("CMD HDs can access 254 partitions (255 is system)") and `vdrive.c:1201` remaps physical entry 0 onto *logical* slot 255, so VICE's "255" is that same system partition under a different numbering, not a 255th user partition. Reading entry 255 goes 32 bytes past the real table and can fabricate a phantom partition. **Entry 0 IS listed** — by `LOAD"$=P"` (extension `sys`) and by the `partition` console command (type `SYS`) — because it is a real table entry, but `DHDImageRegistry::select()` refuses partition 0 outright, so it can never be mounted by number, by name, or via `CP0`. Two consequences of listing it: `parts` is never empty, so "is there anything mountable" must count entries with `number != 0` (that is what the "No usable partitions" guard does), and the default-selection fallback must pick the first USER partition, never `parts[0]`. FD images: fixed system partition (0x640/0xC80/0x1900 blocks for D1M/D2M/D4M), "CMD FD SERIES" magic, table at sys+2048, 31 partitions. `CP<n>` (drive) and the `partition` console command are the ONLY ways to change partitions — matching the real CMD HD, which does not switch on LOAD or CD. `LOAD"$=P"` lists them. A partition name or number as the FIRST in-image path component binds THAT PATH to that partition (`DHDResolvePartition()`) so LOAD/SAVE/listing can cross partitions — but it never calls `select()`, so it does not change what the image has selected. Resolution order: in-range number, then `byName()`, else it is a file — **partition wins over a same-named file**, which stays reachable as `<image>/<number>/<file>`. **A partition NUMBER of 0 in a path means "the currently selected partition"** (`vdrive.c:1324`), NOT table entry 0, the system partition — never resolve 0 via `byNumber()`. **Three separate things are called "partition" here and must not be conflated:** `Image::selected` (which partition of the image is current), `Image::cached_part` (which partition the ImageBroker's cached stream decodes), and `D64MStream::partition` (the sub-partition WITHIN a decoded disk — a 1581 CBM / CMD native sub-partition, reached via `curPartition()`, and nothing to do with the CMD HD partition). Two mechanisms are BOTH required and neither works alone: `D64MFile::brokerUrl()` (overridden to append the partition number) makes `ImageBroker`'s rebuild resolve the right partition, and `normalizePath()` disposes a stream cached for a different one — the broker keys on the container and cannot tell partitions apart. Without them the five operations that read the cached stream (`rewindDirectory`, `getNextFileInDir`, `isDirectory`, `exists`, `getCreationTime`) silently answer for the selected partition. `entryUrlFor()` (the other overridable `D64MFile` virtual) names the partition BY NUMBER in emitted entry URLs — names may contain `/`, spaces and PETSCII. No slash-based partition syntax is possible: `util_get_canonical_path()` collapses runs of `/`. Selection changes dispose the ImageBroker entry (`DHDImageRegistry::disposeCachedStream()`, whose key must mirror `ImageBroker::obtain()`) so listings re-decode.
- **IDE64 CFS images (.hdd) follow the same partition model as CMD images.** `HDDImageRegistry` (lib/meatloaf/media/hd/hdd.h/cpp) holds the per-image partition table and selection exactly as `DHDImageRegistry` does; `HDDResolvePartition()` binds a partition to a path without ever calling `select()`; `hdpart::` (media/hd/partition_select.h) is the one surface `CP<n>` and the `partition` console command both use. **Partition 0 means "the currently selected partition" in a path and is refused by `select()`, identical to DHD.** Two things are CFS-specific: (1) **there are two numbering spaces** — the raw table SLOT 0-15, which is how the 16-entry partition directory is laid out and what the boot sector's DP byte holds, and the partition NUMBER 1-N, which counts only VALID entries and is what paths, `CP<n>`, `$=P` and the `partition` command speak. `parse()` converts DP between them exactly once; nothing downstream sees a slot, and the numbering must match `HDDMStream::seekPartitionEntry()` entry for entry. (2) There is **no `cached_part`/`brokerUrl()` machinery and no dispose-on-select**, because `HDDMStream` re-derives its position from `seekDirectory(pathInStream)` on every operation, so a broker-cached stream carries no partition identity that can go stale. Only CFS-type partitions (`type == 1`) are selectable; unformatted, GEOS and reserved types are listed but refused.
- **The CFS boot sector's default-partition byte is `$03`, not `$01`** (`HDDMStream::BootSector`). The spec's table is colspan-encoded — `Unused` spans `$00-$02`, `DP` is `$03`, `@Last disk sector` spans `$04-$07` — and the struct had both `default_partition` and `last_sector` off by two. It was latent because every sample image in `.archive/hdd/` has `$00-$03 = 00 00 00 00`. The corpus invariant that pins it: `@Last disk sector == @Partition directory backup + 1`, since the backup directory lives on the last sector of the disk.
- **`HDDMStream` must never call `HDDImageRegistry`.** The selection is written into `HDDMStream::selected_partition` by `HDDMFile` (`applyPartition()`), at all five sites that touch a stream: `getDecodedStream()`, `rewindDirectory()`, `getNextFileInDir()`, `isDirectory()`, and `exists()`. A registry lookup from inside the stream would need `MFSOwner::File()`, which `abort()`s under the native test stubs — and `FileContainerStream` sets `url` to a path ending in `.hdd`, so the lookup would fire. `selected_partition == 0` means "fall back to the boot sector's DP" — unambiguous precisely because partitions are numbered from 1 — and is what a directly constructed stream gets.
- **CFS stores FILE DATA in a data sector as four interleaved 128-byte columns** — file byte `n` of a sector sits at sector offset `(n % 128) * 4 + (n / 128)`. `HDDMStream::readFile()` must therefore read the sector WHOLE (`loadDataSector()`) and de-interleave it; the requested range cannot be read straight out of the image, and a partial-sector read cannot be de-interleaved at all. **Only file data is stored this way** — boot, partition, directory and tree sectors are all linear, which is exactly why listings, partition parsing and the tree walk worked perfectly for months while every byte of every file came back transposed (a PRG loaded with the right length and LISTed as nothing). The published CFS 0.11 spec documents the tree geometry (8 next-tree + 128 data pointers, 64 KB at depth 1 — which the code already had right) but says nothing about byte order inside a data sector, so linear was the natural wrong guess. Established empirically, three ways: de-interleaving turns a PRG into a BASIC program whose line-link chain walks correctly across sector boundaries (`$0801 → $0828 → $084F → …`), turns the `man` association tables into ordered text, and holds on Soci/Singular's OWN IDE64 image as well as a C64 OS one — two independently authored images, so it is the format and not a quirk of one image. **Do not "simplify" `readFile()` back to a direct read.**
- **A path component made only of `*`/`?` is not a partition reference** (`hddResolvePartitionIn()`, `DHDResolvePartition()`) — `byName()` honours wildcards, so a bare `*` matched the FIRST partition, `normalizePath()` stripped it from the path, and `LOAD"*"` became a listing of that partition's root instead of loading a file. A pattern carrying real name characters (`C64*`) still resolves a partition. Relatedly, `HDDMStream::seekEntry()` skips directories and separators when the pattern is all-wildcard, since the CBM "first entry" idiom means the first loadable FILE — a CFS partition root typically opens with the `%DELETED FILES%` directory. `D64MStream::seekEntry()` skips DEL entries for the same reason but does NOT skip subdirectories, so `LOAD"*"` in a DNP/DHD partition whose first entry is a directory still returns a listing (open).
- **`CP<n>` has three failure codes, split by WHAT failed** (`iecDrive::changePartition()`): **31 INVALID COMMAND** — there is no partition table here at all, so `CP` means nothing for this image and the number is never even examined. **30 SYNTAX ERROR** — the argument is not a partition number: it did not parse, or it exceeds the one-byte range a CBM partition number occupies (`CP256` and up). **77 SELECTED PARTITION ILLEGAL** — it IS a well-formed number (0-255) that this image has no partition for. So `CP0` returns 77, not 30 — 0 is reserved for "the currently selected partition" and is therefore never a real partition — and so do all of 17-255 on a CFS image, whose table only ever holds 16 entries. The per-format bound on which partitions actually exist lives in `hdpart::select()` (CMD 1-254, CFS 1-16); `changePartition()` owns only the is-this-a-number-at-all check. **Beware the constant names in `include/cbm_defines.h`: `ST_SYNTAX_UNKNOWN` is 30 and `ST_SYNTAX_INVALID` is 31** — they do not read in numeric order, and using the wrong one silently emits the wrong status to the C64. Code 30 had no entry in `iecDrive::statusMessage()`'s switch until CP started using it, so it printed `30,UNKNOWN ERROR`; it now prints `30,SYNTAX ERROR`.
- **A `.p81` shares a P64's container and nothing else — it is MFM, not GCR.** `P81MStream` inherits the header, chunk stream, range decoder, seam overlap and track cache from `P64MStream`, and replaces everything downstream of the pulses: a fixed 2 µs cell (32 samples) instead of the 1541 clock/counter, `$4489` sync instead of ten 1 bits, CRC-16/CCITT instead of an XOR checksum, 512-byte sectors split into two CBM blocks, two sides, whole cylinders instead of half tracks (`cylinder = half_track - 2`), one speed zone. This was measured, not assumed — the pulse spacings on a real image are exactly 4/6/8 µs — and neither the P64 reference implementation nor VICE decodes a P64-1581 at all. **The P64 side bit is INVERTED relative to the head in the address marks: side 0 carries head 1.** Both surfaces decode cleanly either way, so only a block chain or a directory listing catches it being wrong; a full-disk CRC sweep will not. Mapping: CBM track 1-80 → cylinder `track-1`, block 0-39 → head `block/20`, physical sector `((block%20)/2)+1`, half `block%2`; chunk key `(side << 7) | (cylinder + 2)`, cache id `cylinder*2 + head`. To make this possible `P64MStream` now keys its chunk table on the RAW HTPx signature byte (side in bit 7) and exposes `imageSignature()`, `chunkKeyFor()`, `trackBufferBytes()`, `overlapBytes()`, `resetEmitState()`, `emitDelta()` and `loadSector()` as virtuals — `emitDelta()` is the seam between the two formats, taking one flux gap and producing bits.
- **A P64 is flux, not sectors, and a flux image cannot be held in RAM.** `P64MStream` decodes ONE track at a time and never materializes the pulse list — the reference implementation builds a linked list of `TP64Pulse` and then walks it, which is ~0.5 MB for one track and ~22 MB for an image, so the decode and the pulse→GCR conversion are folded into a single streaming pass (the range decoder emits positions in increasing order, which is the order the GCR logic consumes them in). **A P64 holds ONE rotation whose position 0 is wherever the imaging hardware started, not a gap, so the rotation is decoded with an OVERLAP** — the pulse stream is replayed with every position shifted a revolution later, continuing the same read-logic state, because a sector straddling position 0 is split across two ends of the bitstream that do not join (the bit cell phase at 0 is unrelated to the phase where the rotation ran out) and wrapping the sync scan is not enough. Remove that and a handful of sectors per disk — each with its data block past ~95% of the rotation — silently come back with bad checksums while the other 670-odd are perfect. Three more consequences that are easy to undo by accident: the track buffer must be CLEARED before each decode because bits are OR-ed into it and the one-track cache would otherwise leave the previous track's tail in the wrap-around region a sync scan reads; a weak pulse (`strength < 0x80000000`) must not update `last_position`, since it is a non-event rather than a shifted one; and `readContainer()` needs its own cursor into the decoded sector rather than `_position`, which is the position in the FILE being read — `G64MStream::readContainer()` indexes its sector buffer by `_position` and is therefore wrong for any file longer than one block. The GCR bitstream a P64 produces is NOT byte-aligned, so sector search is bit-resolution (ported from VICE's `gcr.c`); G64's byte-wise `findSync()` does not apply. Decoding a track costs a transient ~1 MB probability table, so P64 effectively requires PSRAM, and that table is `malloc`'d and NULL-checked rather than a `std::vector` because ESP-IDF is `-fno-exceptions`. Read-only, side 0, `.p64` only — `.p81` is a different geometry.
- **A decoder's working tables belong on the HEAP, never in a stack frame.** Task stacks here are internal-DRAM only and small — `console_exec` is 16 KB, `bus_iec` 20 KB — while a decompressor's tables are measured in tens of KB. `ArcDecoder` (LZW string table + Huffman tables, ~14.4 KB) was a local in `ARCMStream::extractEntry()` and rebooted the board on the first `.arc` entry read. **The frame is reserved on function ENTRY**, so the overflow happens before a line of the decoder runs and the fault lands wherever the next deep call happens to touch — in that case a `LoadProhibited` inside newlib's `memcpy` in `_fread_r`, with a NULL source and a length equal to the request. That signature reads exactly like a corrupted `FILE` and is worth recognising, because it sent a whole investigation into the heap: `_r` reaching the full request size with `_p` NULL is unreachable through newlib's own code, which looks like proof of corruption and is really just a smashed stack. **Reproduce the crash and read the panic line before theorising** — FreeRTOS names it outright (`***ERROR*** A stack overflow in task console_exec has been detected`), and a register-level reading of the earlier dump cannot get there. Allocate with `malloc` + a NULL check and placement-new (ESP-IDF is `-fno-exceptions`, so a throwing `new` is an `abort()`), the same pattern `p64.cpp` uses for its probability table and `components/tapclean` uses for its engine state.
- **Names are UTF-8 everywhere inside Meatloaf; PETSCII exists only at the IEC boundary.** `MFile::name`, `pathInStream`, entry URLs, `MStream::media_header` and `media_id` are UTF-8 whatever produced them. A media filesystem that reads PETSCII off the disk converts it ONCE, in `getNextFileInDir()`, **before the entry URL is built from the name** — so the name a listing shows is the name `seekEntry()` matches and the name that can be typed back. `iecChannelHandlerDir` (`drive.cpp`) converts to PETSCII on the way to the C64, and nothing else does. **`mstr::compareFilename()` is EXACT-match unless the pattern carries a wildcard** (case-insensitive `mstr::compare` is only reached in the wildcard branch), so both sides genuinely have to agree — a `LOAD"GAME"` works because `iecDrive` already `toUTF8`s the incoming PETSCII to `game`.
- **A literal destined for a C64 listing must be written LOWERCASE.** PETSCII `$41-$5A` map to UTF-8 *lowercase* `a-z`, so `toPETSCII2("game")` is `$47 $41 $4D $45` — what the C64 draws as `GAME` — while `toPETSCII2("GAME")` lands in the shifted `$C1-$DA` range and draws as graphics. This is why `media_header`/`media_id` literals (`"i2c bus"`, `"ide64"`, `"end of tape"`, `"cmd hd"`) are lowercase and why `PRODUCT_ID` is NOT used for the directory header — it stays uppercase because the status channel and the serial banner take it raw. Same rule as the `exec` command's PETSCII encoding.
- **`isCBM` (formerly `isPETSCII`) is a property of the CONTAINER, not of an entry.** It no longer describes an encoding; it says the entry came from CBM DOS media, which still governs two things at the IEC boundary: `extension` is already a CBM type field (from `decodeType()`) so the DIR/PRG synthesis must not run, and a CBM name may hold graphics characters that are U+E0xx in UTF-8 and round-trip exactly through `toPETSCII2()` — running them through `U8Char::encodeACE()` first would punycode them to `xn--…`. **Never read it off an entry**: entries are built by `MFSOwner::File(entryUrl)`, so their class is chosen by extension-sniffing their own name — `GAME` inside a D64 became a `D64MFile` while `GAME.PRG` became a `PRGMFile`, and the flag differed within one listing. That bug was found twice (`ark.cpp` patched it by accident in one format; `ls` still had it in 2026-08-17).
- **`toUTF8`, not `util_petscii_to_ascii_str`, for anything that must survive the round trip.** Both render the same glyph, but only `toUTF8`'s lowercase form comes back through `toPETSCII2` as the original byte. TCRT used the latter and had to change.
- **Two internal fields deliberately stay raw**: `HDDMStream::dir_label` and `HDDImageRegistry::Image::disk_label` hold the media's own bytes (native tests assert them), so their consumers — `media_header` and `hdpart::View` — convert. `DHDImageRegistry` converts at parse instead; the asymmetry is intentional and documented at both sites.
- **CBM directory entries store BLOCKS, not bytes** — there is no byte-size field anywhere in a D64/D71/D81/DNP/DHD directory. Every byte figure Meatloaf shows for a file inside an image is a derived over-estimate, and the two call sites deliberately disagree: `ls` uses `entry.blocks * block_size` (256/block, i.e. space occupied on disk) while `D64MStream::seekPath()` uses `entry.blocks * (block_size - 2)` (254/block, the data capacity). The TRUE size is only knowable by walking the block chain to its last block and reading its link bytes: `track == 0` marks the last block and the sector byte is the last-used byte index, so data = `sector - 1` bytes. Reads are correct regardless — the chain-end marker always fires before either over-estimate is reached — so a `cat`/`hex` that returns far fewer bytes than `ls` advertised is CORRECT, not a truncation bug (real example: `settings.ihf`, 1 block, `ls` says 256, actual content is 6 bytes). `seekFileSize()` (`meat_media.cpp`) does the exact walk but is deliberately commented out at the `seekPath()` call site: it costs one read per block per file (hundreds of RPCs over the network, thousands of block reads for one directory listing), which is why the estimate is used instead.
- **`getNextFileInDir()` must honor `rewindDirectory()`'s return value**: `if (!dirIsOpen && !rewindDirectory()) return nullptr;`. A failed rewind has ALREADY called `resetEntryCounter()` on the shared ImageBroker stream, so reading on hands back entry 0 forever while `dirIsOpen` stays false — an endless listing, not an empty one. This bites any implementation whose `rewindDirectory()` can fail AFTER resetting the counter, which is the `seekDirectory(pathInStream)` failure path in `D64MFile` and `HDDMFile` (both fixed). Other formats (archive, m2i, t64, tcrt, ark, lbr, lnx) only fail before the reset and cannot loop.
- **SD card mount/unmount** (`lib/FileSystem/fnFsSD.cpp`): `esp_vfs_fat_mount_config_t` MUST be zero-initialized (`= {}`) — it has five fields, the file's top-of-file `#pragma GCC diagnostic ignored "-Wmissing-field-initializers"` suppresses any warning, and a bare stack declaration left `disk_status_check_enable`/`use_one_fat`/`allocation_unit_size` reading stack garbage. `FileSystemSDFAT::stop()` unmounts (flushing FATFS's cached FAT/directory sectors) and is called LAST in `main_shutdown_handler()`, after the bus and network sessions stop; `ESP.restart()` is `esp_restart()`, which runs registered shutdown handlers, so the console `reboot` is covered — a crash or power cut is NOT. The SDSPI mount retries 3× (dropping to `SDMMC_FREQ_DEFAULT / 2` after the first failure) because a single transient `ESP_ERR_INVALID_CRC` used to disable the card for the whole session. The SDMMC branch has no retry yet.
- **`CONFIG_FATFS_SECTOR_4096=y` is NOT an SD card setting** — it appears in every board sdkconfig and looks alarming, but in this IDF `FF_SS_SDCARD` is hardcoded to 512 and `FF_MIN_SS`/`FF_MAX_SS` come from `MIN`/`MAX(512, CONFIG_WL_SECTOR_SIZE)`. That puts FATFS in variable-sector mode, where it queries `GET_SECTOR_SIZE` and gets the card's real 512. Do not "fix" it.
- **Tape images (.tap/.dmp/.htap)**: decoded by the vendored TAPClean engine (`components/tapclean`, ~90 loader scanners incl. Cyberload, Visiload, US Gold, Novaload, Freeload and a Meatloaf-added Turbotape-64-fast scanner). The image is fetched into PSRAM and scanned PROGRESSIVELY on demand (512 KB prefix, doubling; DMP/HTAP/TAP-v2 converted to TAP v1 on the fly as they stream in); every program is extracted (neighbouring blocks united into loadable PRGs, loader-internal header blocks folded in for their names, CBM boot stubs of turbo tapes dropped with their name transferred to the payload, repeat copies deduped), and once fully scanned the pulse buffer is freed — only the decoded programs stay resident. Sequential datasette semantics unchanged: each directory request returns ONE entry, `no more entries` at tape end, then rewind. A `.idx` sidecar (`<offset>[:<length>] <name>`) switches to a normal full listing with random access. Drive commands: `T-C <ms|MMM:SS>` sets the tape counter (read position by time), `T-I` scans the tape and writes the `.idx`. `TAPMStream` exposes `counterMs()/durationMs()/counterString()`. The tapclean engine is global-state; `TapeDecoder` serializes scans with a static mutex. **Tape decode now effectively requires PSRAM** (whole image + decoded programs in RAM during the scan).

- **CSM tape images (.csm) are decoded blocks, not pulses — but they BEHAVE as a datasette, exactly like TAP.** Layout is `[192-byte header block][data block]` repeated, ended by a type-`$05` header that has **no data block after it** (its address fields are NOT a length — the jsvic20 reference decoder reads them as one and overruns four of the twelve corpus samples). The data block holds **raw program bytes with no load-address prefix**; the two bytes a PRG starts with are synthesized from the header's start address. There is no directory, so `readHeader()` WALKS the container — entry *n*'s offset is the sum of every preceding block — but no pulse decoding, TAPClean or PSRAM is involved, which is the one way it differs from TAP internally. A listing returns ONE entry per request, shows `END OF TAPE` at the end and rewinds for the next; loads search forward from the head and wrap once. Read-only. **`serveCurrent()` clears `have_current`** — serving moves the head past the entry, and without that a repeated `LOAD"NAME"` re-serves the same one, so a tape whose loader and payload share a name (Abductor.csm, and the norm for multi-part tapes) could never reach the payload. Names are `mstr::rtrimPad()`-trimmed; an entry the tape leaves unnamed lists under the media file's name, and duplicates resolve positionally rather than ambiguously.
- **`seekNextEntry()` + `isBrowsable()` are how sequential media resolve a name — not `seekPath()`.** That branch of `MFile::getSourceStream()` was unreachable dead code until 2026-08-15: nothing overrode `seekNextEntry()` and nothing returned `isBrowsable() == true`, since `MMediaStream::isRandomAccess()` is unconditionally true. `CSMMStream` and `TAPMStream` now implement it; TAP is idx-aware (a `.idx` sidecar keeps it random-access on `seekPath()`, without one it is a datasette). The contract: advance one entry, SERVE it, return its name; **wrap at the end of the media** and return `""` only after a full lap, so a name behind the head is reachable and a miss still terminates. Lap detection is per-stream, never in the shared tape state — a fresh stream is built per open, so it starts clean with no reset to get wrong.
- **The datasette position must live in state shared per container URL** (`CSMState`/`TapeState`, weak_ptr registry) — `MFile::getSourceStream()` builds a FRESH stream per open while directory listings use the ImageBroker instance, so a per-instance position would make `LOAD"*"` after a listing rewind to entry 0. The aliasing reference members must be declared after `state`; they are bound in the constructor's init list.
- **`MFile::url` is NOT the file's path — use `fullUrl()`**: for anything inside a container, `url` is the CONTAINER's path and the position within it is held separately in `pathInStream`. Any code that flattens an `MFile` back into a path string, or joins a child name onto it, must use `fullUrl()` (which rejoins the two) or it silently addresses the container instead. This is a data-loss bug, not a cosmetic one: `rm fb` inside `hdbackup.dhd` passed `target->url` onward and **deleted the whole disk image** from the SD card. The same mistake was independently present in `rm`'s wildcard branch, in `resolve_path()` (so `cp`/`mv`/`unzipx`/`wget`/`gzip` all resolved relative names against the container root rather than the current subdirectory inside it), in `wget`'s destination and in `mount`'s default filename. `url` is only correct when you specifically want the container — e.g. `DHDImageRegistry::containerOf(getCurrentPath()->url)`.
- **`MMediaStream::mode` is uninitialised on a directly constructed stream** — only `MFile::getSourceStream()` sets it. If the garbage happens to have the `out` bit set, `seekPath()` takes the WRITE branch and **creates** the entry it was asked to find, so lookups report deleted files as present and reads return 0 bytes. It surfaces as *intermittent* failures that move between formats from run to run. Any code building a stream directly (tests using `f.make()`, engine helpers) must set `mode` before calling `seekPath()`.
- **`MStream::read()` returns at most one block** — a caller wanting a whole file must loop until it returns 0. A single call reading exactly 254 of 800 bytes looks like corruption and is not.
- **CBM scratch/unscratch semantics** (`d64.cpp`): a scratch zeroes ONLY the entry's type byte (offset +2). The name, start track/sector and block count all survive, which is what makes recovery possible — a file is recoverable until its directory slot is reused or its blocks are handed to a new file. `unremoveFile()` walks the chain, refuses if ANY block has since been allocated (recovering anyway cross-links two files onto the same blocks), and only then reallocates and sets the type to PRG. The original type is genuinely lost, since that is the byte a scratch destroys. The walk is a separate collecting pass that modifies nothing, because a half-finished recovery is worse than the delete it was undoing, and it is bounded by the entry's block count because the links live in blocks that are currently free and may be overwritten garbage.
- **HTTP reads that return bytes nobody wrote are an `esp_http_client` defect, not your code**: if a network read hands back ESP-IDF heap canaries (`0xBAAD5678`), stray ASCII, or crashes in `memcpy` inside `esp_http_client_read()`, suspect a re-request on a handle whose previous response was not fully consumed. `esp_http_client_prepare()` did not reset the response buffer (esp-idf#18359), so the next request parsed the previous response's leftovers. `patch_framework.py` applies the fix as a pre-build step — **delete it once the framework package ships the fix**. `Archive::open()`'s failure line prints the first bytes it was handed, which is how to tell this apart from a genuinely corrupt file in one line.
- **An MFile's identity does not survive `getSourceStream()`**: for a single-file compression `ArchiveMFile::getDecodedStream()` ends with `resetURL(base())` (repointing it at its containing directory so the CWD is right after a `LOAD`), which leaves `name` EMPTY. Capture any name you need BEFORE opening the stream, or ask `getDownloadFilename()` afterwards — `ArchiveMFile` answers it with the entry it resolved (gzip FNAME, or the URL basename percent-decoded), and `HTTPMFile` with the `Content-Disposition` name. Reading `name` after the open has produced a write to `<dest>/` (a directory) and a file named after the raw URL basename.
- **Console file commands go through `MFile`, not POSIX**: `cat`, `hex`, `ls`, `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `wget` and `unzipx` all resolve via `MFSOwner`, so they work inside disk images and archives and over the network. `cp`/`mv` were POSIX (`fopen`/`rename`) until 2026-08-09 and could only see real files on flash and SD. `mv` is a true rename only within one directory — `MFile::rename()` takes a NAME relative to the file's own directory, not a path — and is copy-then-delete for anything else, unlinking the source only after the copy fully succeeds.
- **`use <id>` selects the device the console drives; `exec` sends it a DOS command** (`lib/console/Commands/IECCommands.cpp`). Drives are resolved through `Meatloaf.get_disks()` plus device 30, NOT `IEC.findDevice()` — that returns an `IECDevice*` which cannot be narrowed to `iecDrive*` without RTTI. Selecting takes the device to the console's cwd and it FOLLOWS from then on: the hook is inside `setCurrentPath()` (`Helpers/PWDHelpers.cpp`), so every cwd change propagates (`cd`, `partition`, …), not just `cd`. It calls `iecDrive::consoleSetCwd()`, which uses `set_cwd(url, verified=true)` because the console already validated the path — the unverified path re-runs `isDirectory()`/`exists()`/`getSourceStream()` and costs redundant network round trips. `%dev%` in the console prompt expands to the bare selected id (empty when none); the separator lives in the prompt template, not the token.
- **`exec` PETSCII-encodes and does nothing else — so DOS commands must be TYPED IN LOWERCASE.** `mstr::toPETSCII2()` reverse-maps through `U8Char::utf8map`, where PETSCII `$41-$5A` map to UTF-8 *lowercase* `a-z`; lowercase input therefore yields `$41-$5A`, exactly what an unshifted C64 sends and what `executeData()` dispatches on ("CD", "N0", "T-Z"). **Uppercase input is not equivalent** — it maps to the SHIFTED range, valid PETSCII that matches no command; not a silent trap, since `executeData()` falls through to `ST_SYNTAX_INVALID` and `exec M-R` answers `31,INVALID COMMAND`. Measured on the real map: `m-r` → `4D 2D 52` ✅, `M-R` → `CD 2D D2` ✗, `i0:` → `49 30 3A` ✅, `I0:` → `C9 30 3A` ✗, `cd:games` → `43 44 3A 47 41 4D 45 53` ✅. **Nothing is case-folded, deliberately**: a `toLower()` was tried and removed, because commands and their parameters can be mixed case and lowercasing the line to make the VERB match corrupts every filename and path in it. Binary bytes (`M-R`/`M-W`/`B-P` carry addresses and data that are not text) are written `0x` followed by an EVEN number of hex digits, and pass through unconverted. **One `0x` introduces a RUN of bytes, not a single one** — `0x000009` is `00 00 09`, the same three bytes as `0x000x000x09`; the run stops at the first character that is not a hex digit, which is what keeps the per-byte form working. It was one byte per `0x` until 2026-08-19, and the failure was silent and confusing: `exec m-r0x000005` left `0005` as TEXT, so the drive read `00 30 30 30 35` and answered `address[3000] size[48]` — a plausible-looking read of the wrong place, not an error. The ambiguity the run form accepts, deliberately: an escape followed by text starting with two hex digits swallows them (`0x00cd` is `00 CD`), tolerable because every command carrying binary is all binary after the verb. Spacing is sent exactly as typed, because `B-P 2 0` needs its separators while `M-R` needs its address bytes butted against the verb (`exec m-r0x000009`).
- **`M-W` takes a COUNT byte: `M-W <addr lo> <addr hi> <count> <data…>`.** Omitting it is the other half of the confusion above — `exec m-w0x0000010203040506070809` is not "write 9 bytes at $0000", it is count `01` and one byte of data. The full line is `exec m-w0x000009010203040506070809`. `M-R` is `<lo> <hi> <count>` with no data, so `exec m-r0x000009` reads 9 bytes at $0000.
- **`esp_console_run()` splits a line into at most `CONSOLE_MAX_CMDLINE_ARGS` arguments and SILENTLY DROPS the rest** (`lib/console/console_settings.c`). There is no error and no truncation marker — the command simply never sees the tail. It was **8** (the IDF example's value, which reserves one slot for the NULL, so seven words), and `write 2 hello from the console 0x0D0x00 second line` wrote everything up to `console` and reported `00, OK`. Now 32; the cost is one `calloc` of that many pointers per line. `exec` had the same trap and nobody had reached it. Related: the splitter only strips a LEADING quote, so `"a b"0x0D"c d"` yields `a b`, `0x0D"c`, `d"` — quoting keeps spaces inside one argument but is not a general escape, and `joinArgs()` collapses runs of whitespace to one space because that is all the splitter leaves.
- **The console's file channels are driven by `open`/`read`/`write`/`close`, and the channel number IS the secondary address** (`lib/console/Commands/IECCommands.cpp`). **All three of `open 15`, `read 15` and `write 15` are special-cased, because channel 15 NEVER reaches `iecDrive::open()`/`read()`/`write()` on the bus** — `IECFileDevice` intercepts it, answering a read from `getStatusData()` and routing a write to `executeData()` (`IFD_EXEC` is set only when `m_channel == 15`). So `open 15 i0:` and `write 15 i0:` both run the command, as `OPEN 15,8,15,"I0"` and `PRINT#15,"I0"` do, and `read 15` returns the status line, as `INPUT#15,A$` does. Without the special cases the file path is taken and all three answer `61,FILE NOT OPEN`. `read 15` prints the status ONCE and returns — falling through to the shared `reportStatus()` would consume a second one. Five public wrappers on `iecDrive` — `consoleOpen`/`consoleRead`/`consoleWrite`/`consoleClose`/`consoleStatus` — are the only console-facing surface; `open()`/`read()`/`write()`/`close()` stay protected and are called VIRTUALLY so device 30 still reaches `iecMeatloaf`. `consoleStatus()` is the status-consuming tail factored out of `consoleExecDos()`. `close` with no argument sweeps channels 0-15, which is safe because `close()` is a no-op on a channel that is not open — VDrive has no per-channel query to ask instead.
- **A block command (`B-R`/`B-W`, `U1`/`U2`) is a SEEK here, not a buffer transfer.** There is no separate 256-byte block buffer the way a real drive has one — a channel IS a stream at an offset — so all four resolve through `iecDrive::seekChannelToBlock()`, which points the channel at the block and lets the data move through ordinary reads and writes on that channel. **`B-R`/`B-W` land on byte 1 and `U1`/`U2` on byte 0**, which is the real CBM distinction: B-R/B-W reserve byte 0 as the count of valid bytes, U1/U2 give the whole 256. The seek matters beyond the data — some fast loaders expect the track/sector registers updated by a block command and then issue reads that name no block. Three things the old code got wrong and the helper now centralises: `m_channels[pti[0]]` was indexed with a full byte against a 16-entry array (**out-of-bounds read**, in B-R, U1 and U2 alike); a directory channel's `getStream()` is nullptr and was dereferenced; and the seek never synced the CHANNEL, so `channels` reported a stale position and the next read started mid-block. `B-R` additionally used to consume byte 0 as a length and dump the result to the STATUS channel, losing it. **`seekSector()` cannot report the byte offset** the channel needs — when a file is selected `_position` is the offset within the FILE and `readFile()` calls `seekSector()` while walking the chain, so it must not touch `_position`; the new `MStream::sectorByteOffset()` supplies it separately, and `D64MStream` shares one geometry walk (`linearBlock()`) between the two.
- **`OPEN <ch>,8,<sa>,"#"` is DIRECT ACCESS: the channel gets the CONTAINER's own byte stream instead of a file inside it — and it is a window on ONE BLOCK at a time, not on the whole image.** The mechanism is that a fresh `MFile` on the cwd's own url carries an empty `pathInStream`, so `getSourceStream()` selects no entry, `seekCalled` stays false, and `MMediaStream::read()` takes its `readContainer()` branch — the same path that already existed for copying an image verbatim. It stays a `D64MStream`, so `seekSector()` remains available for the block commands, and `getSourceStream()` builds a FRESH stream per open so this cannot disturb the ImageBroker instance a directory listing is using. A trailing buffer number (`"#3"`) is accepted and ignored — a real drive allocates one of its buffers, here there is one stream per channel.
  - **The window.** It opens as block 0, 256 bytes. `B-R`/`U1` move it, `B-P` addresses within it, `F-P` leaves it. The window is expressed to the stream by `setSize(start + length)`, because that is what stops a read at the block's end — the reader asks for `BUFFER_SIZE` (512) at a time, so nothing shorter bounds it. `U1`/`U2` give the whole 256; **`B-R`/`B-W` shorten it to the block's own count byte** (byte 0), which is why `B-R` on a directory sector reports SIZE 18 — the link byte `$12` read as a count. `iecChannelHandler` carries `m_block_base`/`m_block_len`, and `channels` reports the block's length and the pointer within it rather than the stream's size and absolute offset, which for a window would be an end-offset in the image and not a size at all.
  - **`F-P` LEAVES block mode** and restores the container's full extent, so the image can be read end to end from any offset; `B-R`/`U1` put the channel back into it. That is why the container's real length is captured at open — `MFSOwner::File(url, /*default_fs=*/true)`, the established "give me the bytes" primitive, so the name is not extension-sniffed back into another `D64MFile` — and remembered on the channel as `m_full_size`: the block window overwrites the stream's size and nothing else can put it back.
  - **Two traps.** The container is opened `in|out` and **never plain `out`**, which would truncate the image. And **nothing sets `_size` for the no-entry view**, which leaves `eos()` true from the very first byte so a reader stops refilling almost at once — hence `MStream::setSize()`, used both for the window and for the full extent.
  - **`MMediaStream::read()`'s non-`seekCalled` branch had no `_size` cap** — only the file branch did — so a block window would have been read straight past. Both branches cap now, which makes the bound apply to every raw-container reader, not just this one.
- **`B-P` and `F-P` are different commands and must not be conflated.** `B-P <ch> <pos>` moves the pointer WITHIN the block `B-R`/`U1` selected — 0-255, counted from the block's byte 0 even when `B-R` left the readable window starting at byte 1, because it addresses the block, not the window. `F-P <ch> <pos>` (File Position, a Meatloaf addition) is an ABSOLUTE offset into the file or container, and on a direct-access channel it leaves block mode. `B-P` on a channel with no block window answers `31,INVALID COMMAND`: it is not a file-channel command, `F-P` is. Both go through `iecDrive::positionChannel()`. **`B-P` was briefly implemented as the absolute seek, before `F-P` existed** — a commit that treats it as a file offset is using the old meaning.
- **Repositioning a channel needs THREE things, and doing only the obvious one leaves it silently wrong.** (1) The channel's buffer must be invalidated (`iecChannelHandler::repositioned()`) — `read()` serves from `m_data` before it touches the stream, and `readBufferData()` has filled up to `BUFFER_SIZE` AHEAD, so a seek back to 0 followed by a read returned the second half of the stale buffer and looked exactly like a continuation. (2) The position must be parsed as **32-bit**: `util_tokenize_uint8()` is `atoi` truncated to `uint8_t`, so `300` silently WRAPPED to 44 and answered OK. (3) `D64MStream::seek()` must **walk the block chain** from `entry.start_track/start_sector`, 254 data bytes per block, and land with `sector_offset = 2 + remainder` so `readFile()` does not re-read the link it was just handed — `MMediaStream::seek()` seeks the CONTAINER, which is meaningless for a file scattered across blocks and left every later read returning zeros. Note `m_channels` has 16 entries and was indexed with an unbounded byte, so `200` as a channel read past the object; every command that takes a channel bounds it now, and refuses a directory channel, whose `getStream()` is nullptr and was dereferenced unchecked.
- **A `case` in `executeData()`'s command switch without a `break` reaches `I` and RESETS THE DRIVE.** `case 'F'` was added without one, so any unmatched `F-*` reinitialised the device. The switch is long enough that the fall-through is not visible at the point of editing.
- **`channels` reports the CHANNEL's position, never `m_stream->position()` — the two are not the same number and the gap differs per path.** A read fills a whole `BUFFER_SIZE` (512) ahead, so the stream LEADS: reading 256 bytes off a fresh channel leaves the stream at 512, which is what `channels` printed before this was fixed. `iecChannelHandler::write()` buffers until full, so there the stream LAGS. `iecChannelHandlerFile::write()` passes straight through, so those agree. The correction cannot be derived from the buffer occupancy because it needs to know which of the three applies, and the direction is not reliably knowable — `m_stream->mode` is uninitialised for a stream inside a disk image, which is exactly why `writeBufferData()`'s own mode check is commented out. So `iecChannelHandler` counts `m_position` as bytes cross the boundary. **Every override that moves bytes must advance it**: `iecChannelHandlerFile::write()` returns before reaching the base and does so itself — missing that showed a write channel frozen at 0 while bytes were landing on disk. A directory channel has a real position but no size (`-`), since its listing is generated rather than read.
- **A channel's name is captured at open from `MFile::fullUrl()`, not from the stream.** `MStream` carries only `url`, which for anything inside a container is the CONTAINER's path — `handler->name()` would otherwise show `goonies.d64` for every channel open inside it instead of `goonies.d64/the goonies+`. Set at both handler construction sites in `iecDrive::open()`.
- **`exec` no longer refuses while channels are open.** The guard existed because a C64 owns the drive while it has channels open, but the console's own `open` leaves one open and reading status mid-transfer is most of the point. The race it guarded against is real and accepted: it is the same exposure `mount` and `partition` already carry.
- **ESC cancel picks its transport from `console.execOrigin()`** (`lib/console/console_cancel.h/.cpp`), checked every `DOS_CANCEL_INTERVAL` (256) bytes by `read`, `cat` and `hex`. Serial polls `stdin` non-blocking; TCP calls `TCPServer::pollCancel()`, a `recv(MSG_DONTWAIT)` on the client socket. **That poll is safe from the console executor task and ONLY from there** — while a command runs, `session_task()` is blocked inside `console.execute()` and is not reading the socket, so there is no second reader. Branching on the origin matters: polling unconditionally would make a serial-origin command steal an idle TCP client's type-ahead. **Bytes that are not ESC are discarded** on both transports; type-ahead entered during a long command is lost, as it already is for `rx`/`tx`. A cancelled `read` leaves its channel OPEN and positioned — verified: cancelling at 5120 bytes and reading again resumed at `pos[5120]`.
- **`driveMemory::read()`'s return value is what `M-R` publishes, not the requested count.** It answers 0 for an unmapped address (anything from `$1000` to `$7FFF`), and `executeData()` sets a status of exactly what it got — so an out-of-range `M-R` yields an EMPTY status and the canned DOS status is served instead, rather than 48 bytes of stack. RAM below `$1000` reads as zeros before the lazy allocation has happened, since a drive always has RAM and an `M-R` before any `M-W` is ordinary. Note `read()`/`write()` both test `addr < 0x0FFF`, which excludes `$0FFF` itself — pre-existing, unfixed.
- **`IECFileDevice::peekStatus()` / `GPIBFileDevice::peekStatus()`** return the UNREAD part of the status buffer — what the bus master would receive if it read the status channel now — without consuming it; `clearStatus()` consumes. Returning 0 means empty, which is the same test `read()` uses to decide whether to call `getStatusData()`. `iecDrive::consoleExecDos()` follows that exact order, so a reply pushed by `setStatus()` (how `iecMeatloaf` answers FujiNet commands) wins over the canned DOS status instead of being left queued for the C64. Both bus classes must keep this in step — `drive.cpp` compiles against either.
- **A CBM status can carry raw binary** (`M-R` answers with drive memory), so anything rendering one to a human must hex-dump it when a byte falls outside `0x20-0x7E` rather than printing it as text. `printStatus()` in `IECCommands.cpp` is the reference.
- **A Lynx block is 254 bytes, and `MStream::block_size` is 256 — never use the inherited member for LNX geometry.** `lnx.cpp` used it for all three of the entry size, the data start offset and the per-entry stride, so **every entry came out two bytes late per block**: the first byte a caller saw was the third byte of the file, and a PRG lost its `01 08` load address, appearing to start at its BASIC link pointer. A Lynx archive is a CBM file laid out in disk blocks with the two link bytes stripped, so a block carries 254; `LSU` is the INDEX of the last used byte within the 256-byte sector, where data begins at index 2, so the last block contributes `lsu - 1`. Correct: `data_start = directory_blocks * 254`, `stride = block_count * 254`, `size = (block_count - 1) * 254 + (lsu - 1)`. **Derive the stride from the declared block count, not from the byte size rounded up** — the two differ in the last block, so deriving it from the size compounds any error in the size.
- **The invariant that settles LNX arithmetic against media nobody here wrote: entries tile the archive, so the last one ends at EOF** (within its final partly-used block). On `Tetrix.lnx` that is exact — `254 + 77*254 = 19812`, `+ (254 + 176) = 20242 = EOF` — and it is what `test_lnx_real_archive_entries_tile_the_file` asserts over a real sample. With 256-byte blocks the walk overshoots and the last entry runs past the end, which is the cheapest possible detector.
- **A SPYne is the LNX geometry again, and gets the 254 rule with it** (`media/archive/spy.h/.cpp`, read-only). Files are stored uncompressed, each occupying a whole number of 254-byte blocks; the extraction code is the first 15 blocks, the central directory starts at block 15 ($0EE2), and file data starts in the block after the directory ends — so `data_start = (15 + ceil(entries/8)) * 254`. Size is `(blocks - 1) * 254 + (lsu - 1)`, the same LSU convention LNX uses. **Eight entries fit one directory block, and the eighth is 30 bytes rather than 32**: `7 * 32 + 30 = 254` exactly, because the eighth entry's two filler bytes would cross the boundary and so are never written. The stride WITHIN a block and the stride BETWEEN blocks are therefore different numbers, and a flat `i * 32` walks two bytes further adrift with every block. **There is no signature** — the format has none — so `readHeader()` validates that the first directory entry is structurally one (type $81-$83, last-file marker $00 or $FF); the $02A7 load address is logged when it differs but not required. The walk ends at the entry whose marker is $00.
- **A SPYne's computed end misses EOF in BOTH directions, by under one block, and that is the tiling invariant to assert** — not LNX's exact-tiling one, which fails here. Over when the archiver stored no padding after the last file's real bytes (three of the five corpus containers), under when the container carries a few trailing bytes (NTSC4K-2 by 42, TURBO-MP by 20). Reads clamp to `_size` and a short return at the end is correct, not truncation.
- **Wraptor (`.wra`/`.wr3`) is ONE implementation for both extensions** (`media/archive/wra.h/.cpp`, read-only). The version difference is entirely in how Wraptor 1/2 versus 3 reconstruct GEOS files — a bug-fix lineage, not a container or compression change — and since nothing here reconstructs a GEOS file there is nothing to switch on. Layout is a bare concatenation: `FF 42 4C FF`, a NUL-terminated name, a type byte (1 SEQ, 2 PRG, 3 USR, 4 GEOS), then LZSS data running to two bytes before the next signature. Those two bytes are a CRC **whose algorithm the format documentation does not give**, so nothing verifies it. There is no directory, no stored size and no next-entry offset — the entry list is a buffered signature scan, and a hit not followed by a plausible name and a valid type byte is dropped as a false positive so the entry before it keeps running to the next real signature.
- **Wraptor's LZSS is MSB-first, and its dictionary offset is an ABSOLUTE index into the 32 KB output window, not a distance back.** One flag bit selects a literal (8 bits) or a code; a code of 0 escapes to either end-of-stream or "widen the code by one bit", starting at 8. The copy source is `window[(offset - 1 + i) % 32768]` — one-based, and the window wraps rather than sliding. **Bit order was established by measurement**, from the worked example in the format document: MSB-first yields `01 08` and then a BASIC link pointer that walks correctly, LSB-first yields nothing that parses. Do not "fix" it by symmetry with another format.
- **A Wraptor GEOS payload carries nine bytes Wraptor invented, and they are NOT stripped.** Layout, established by inspection since the format document does not describe it: structure (0 sequential, 1 VLIR), GEOS file type, year, month, day, hour, minute, block count low/high — then the GEOS info sector at offset 9, whose first three bytes are always `03 15 BF`. **The payload is served verbatim**, matching what D64 already does for VLIR (`AGENTS.md`: there is no VLIR support anywhere in the codebase); stripping would need a content heuristic that the corpus, being 100% GEOS, cannot validate the negative case of. Two of those fields are duplicated inside the info sector 254 bytes further on, which is what makes the decode self-checking — see the native suite.
- **The Wraptor type byte does not mean what the format document implies, so never use it to detect a GEOS payload.** It tracks the C64 file type, not GEOS-ness: every type-4 entry in the corpus reports PRG in its info sector and every type-3 one reports USR, while BOTH carry the nine-byte GEOS header. So `.decodeType()` maps 4 to `prg`, and anything wanting to know whether a payload is GEOS must look at the payload.
- **A Wraptor name's encoding is decided PER NAME, not per archive** (`WRAMStream::decodeName()`). Wraptor writes the name exactly as the CBM directory held it and flags nothing, and one archive can hold both kinds: a plain CBM name is PETSCII, a GEOS name is the ASCII GEOS itself uses (`ReadPaint`, `M.Randall`). PETSCII has no ASCII lowercase range — $61-$7A are graphics there — so a name carrying any of those bytes is GEOS ASCII and passes through untouched; anything else is read as PETSCII and `toUTF8()`d, which lowercases it (`GEOS` is held as `geos`) per the repository-wide convention. Running `toUTF8()` over a GEOS name instead maps its lowercase bytes to graphics characters. Both sides — the listing and the lookup — must call this one function.
- **A Wraptor listing reports the COMPRESSED span, and the true size only exists after decoding.** Nothing in the container states the decompressed length, and the format document calls decode-to-list "very slow"; `seekPath()` sets the real `_size`. The 32 KB window is heap-allocated with `malloc` + placement new (`-fno-exceptions`, and a 32 KB stack frame is reserved on function ENTRY — the defect `ArcDecoder` shipped with), and decoding one entry needs the window plus the compressed span plus the whole output at once, so **WRA is effectively a PSRAM-board feature**. A stream that runs out of input before its own end marker FAILS rather than serving a short file that looks whole.
- **The `.archive` corpus moved into `.archive/archive/<format>/` and five `test_container_entries` cases still pointed at the old paths**, so they had been silently `TEST_IGNORE`-skipping rather than testing anything — including `test_ark_real_archive_data_offsets`, which covers the same class of defect as the LNX one. Paths fixed; the suite now runs 19/19 with none skipped. **A skipped corpus test reads as a pass in the summary line** — check the skip count, not just the status, after any corpus reorganisation.
- **A CBM `.sfx` is a PRG wrapped round an LHA archive, and libarchive will not find it without being told.** `archive_read_format_lha_bid()` enters its self-extracting scan only when the file starts with `MZ` — a DOS/PE executable — and a Commodore self-extractor starts with a two-byte load address (`01 1C` on the sample), so the bidder saw no header, bid 0, and **`raw` won with a single bogus entry spanning the whole file**: `Archive format: (null)`, `filter none`, one `d`-flagged entry the exact size of the container. New `lha:sfx` option sets `lha->opt_sfx`, and both gates — the bidder and `read_header`'s call to `lha_skip_sfx()` — now accept `MZ || opt_sfx`. `Archive::open()` sets it for `.sfx` and registers lha alone. **The scan is opt-in on purpose**: it reads up to 20 KB ahead, and the unknown-extension fallback registers lha speculatively for every file it cannot name, so making it unconditional would put a 20 KB probe in front of every `.gz`. `lha_skip_sfx()` itself was already MZ-agnostic and needed no change — it just scans for a header, and consumes 0 when the archive starts at offset 0, so a plain `.lha` renamed `.sfx` still works.
- **A bidder CAN reach its own format data**: `choose_format()` sets `a->format` to the slot being bid before calling it ("Set up a->format for convenience of bidders"), even though `archive_read_open1()` only assigns `a->format` for real after the winner is chosen. That is what lets the bidder read `opt_sfx`.
- **An ARC-based `.sfx` is still not handled.** `.sfx` now resolves to lha only, so a self-extracting ARC fails the open instead of producing raw garbage — which is the better failure, but it is not support. `arcFS` handles `.sda` (the ARC equivalent) by a completely separate path, keyed on its own extension, and its BASIC-loader skip is ARC-specific — the line-number arithmetic `(line - 6) * 254` gives 503936 for this LHA sample, so it does not transfer.
- **A decoder whose end of entry is an OUTPUT byte count must reconcile `entry_bytes_remaining`** (`archive_read_support_format_lha.c`). `-lh5-`/`-lh6-`/`-lh7-` end when their input runs out, so libarchive's bookkeeping balances by construction. `-lh1-` ends when it has emitted `origsize` bytes and can leave compressed bytes behind — and nothing else ever takes them, because `archive_read_format_lha_read_data_skip()` consumes `entry_unconsumed` and then returns early once the entry is finished. The leftovers must be folded into `entry_unconsumed` on EOF or **every later header is read from the wrong offset**: with the fold disabled, a 25-entry `.lzh` walks as 1 entry. Normally there is nothing to fold, since the bit reader fills a 64-bit cache greedily and has already taken the encoder's few trailing flush bytes — it bites when the two sizes genuinely disagree (an entry whose `origsize` is 0, or a header that under-reports it), which is why the regression test forges a short `origsize` rather than trusting a real archive to reach it.
- **The 128 KiB window all the lzh decoders share must be pre-filled one byte BELOW the real window size.** A match may reach a full window back, and at `w_pos == 0` a distance of `w_size` selects `ds->w_size - w_size - 1` — one byte below what `memset(w_buff + w_size - real, 0x20, real)` filled. The original decoders answer `0x20` there (a circular buffer of exactly `real` bytes, pre-filled with spaces); the old code returned whatever the PREVIOUS entry left at that byte, so the defect was nondeterministic across entries rather than reproducible. Upstream had this edge for `-lh5-`/`-lh6-`/`-lh7-` too; it is now `real + 1`. A wrong byte here is a CRC failure that looks like a Huffman bug. Note the arithmetic assumes `ds->w_size > real` — true for every method (12-16 bits against a 17-bit buffer), but it underflows the buffer if the expanded window is ever set equal to the real one.
- **`seekEntry()`'s size probe must only run for a compressed STREAM, never for an entry inside a container** (`lib/meatloaf/media/archive/archive.cpp`). When `archive_entry_size()` is 0 the probe re-opens the archive and counts decompressed bytes — and that re-open re-reads from the FIRST entry, which **resets a name walk already in progress**. Inside a real container a size of 0 means the file IS empty, so the probe is both unnecessary and fatal: any name sitting past an empty entry can never be reached, and the lookup re-opens the archive about once a second forever. Found on hardware — entry 66 of the 997 in `mce.lha` is an empty file, and `hex mce.lha/MCE.info` never returned. The gate is `isRawCompressedEntry || (entry.size == 0 && hasCompressionFilter())`: a `.gz`/`.bz2` has one entry and a meaningless size field, which is exactly when re-reading from the first entry is correct. Note a regression here presents as a HANG, not an error — the native test that pins it hangs the suite rather than failing it.
- **`archive_read_support_format_all()` is deliberately NOT called in `Archive::open()`'s unknown-extension fallback** (`lib/meatloaf/media/archive/archive.cpp`). That one reference is the only thing linking the cab, mtree, warc and ar readers — **~31 KB of flash text** for formats a Commodore device does not meet, and a plain ESP32 links within ~1 KB of its `iram0_2_seg` window. The fallback registers every format Meatloaf can name by extension, so any of them is still recognized from content alone; adding a format to the extension list means adding it here too. **`empty` is in that list on purpose** — it is what gives a zero-byte file a clean empty listing, and without it that file falls to `raw`, which bids 1 on anything and synthesizes one entry named `data`.

- **`ftps://` asks for TLS up front; `ftp://` gets it opportunistically, on a retry.** With `ftp://`, `fnFTP::login()` connects in the clear and only reconnects with `AUTH TLS` when the server refuses a command with a 4xx/5xx whose text names TLS or SSL (`_tls_required`, set at the banner, at USER and at PASS — servers differ on which one they refuse), so a plaintext server pays nothing. With `ftps://`, `FileSystemFTP::start()` calls `fnFTP::require_tls()` from the parsed scheme and the first attempt is already the TLS one, saving the wasted round trip. **The retry must be capped by whether THIS attempt used TLS (`used_tls`), not by `_tls_required`** — which `ftps://` sets before the first attempt, so testing it would retry a TLS login that had already failed.
- **The PORT decides implicit vs explicit FTPS, not the scheme.** `implicit_tls()` is `control_port == 990` and nothing else, so `ftp://host:990` is implicit too — 990 means implicit whatever the URL called itself. Implicit handshakes the moment the socket is up and reads the banner already encrypted; there is no `AUTH TLS` and no plaintext phase to fall back from, so a failure there is fatal rather than a retry. **`ftps://host` with no port stays EXPLICIT on 21** — deliberate, decided 2026-08-18: RFC 4217 deprecates the separate-port form, modern servers deploy explicit, and meatloaf.cc has no 990 listener, so defaulting to 990 would break the most likely URL. (curl maps `ftps://` to implicit-990; Python ftplib, WinSCP and FileZilla do not. The ecosystem is genuinely split.)
- **`PBSZ`/`PROT` are gated on `control->is_tls()`, NOT on the `AUTH TLS` branch.** Implicit FTPS never enters that branch, and it still needs a protected data channel; keeping them inside it silently left `_data_protected` false for every implicit connection. The `AUTH TLS` branch itself is additionally gated on `!control->is_tls()` so implicit does not try to upgrade a connection that is already encrypted.
- **Implicit FTPS is NOT hardware-verified.** meatloaf.cc has no 990 listener, and a local stub server could not be reached (Windows Firewall blocks inbound, and elevation was unavailable) — and would have failed the CA-bundle check with a self-signed certificate anyway. Explicit FTPS on both `ftp://` and `ftps://` was re-verified on hardware after the restructure. To close this, point it at a real implicit-FTPS server whose certificate chains to a public CA.
- **`FTPSMSession` exists ONLY so SessionBroker keys ftps:// separately.** `obtain<T>()` builds the key from `T::getScheme()`, and explicit FTPS shares port 21 with plain FTP — so unlike http/https, the port cannot distinguish them, and without a distinct class a server accepting both would hand an `ftps://` caller whatever session an `ftp://` caller opened first. It overrides `getScheme()` and `urlScheme()` and nothing else. `FTPMSession`'s scheme-taking constructor exists for the same reason: the `key` member must be spelled exactly as `obtain()` spells it. Both `obtain<>` call sites (`FTPMFile` ctor, `FTPMStream::open()`) must branch on the scheme identically.
- **The control socket is UPGRADED IN PLACE, which means the descriptor changes owner.** The banner and `AUTH TLS` are exchanged in the clear, so esp-tls has to adopt an already-connected socket: `esp_tls_init()` → `esp_tls_set_conn_sockfd()` → `esp_tls_set_conn_state(ESP_TLS_CONNECTING)` → `esp_tls_conn_new_sync()`, which then skips its own TCP connect. **`esp_tls_conn_destroy()` closes that descriptor**, so `fnTcpClient` must let go of it or it is closed twice — the second close landing on whatever unrelated fd has since taken the number. That is what `fnTcpClient::detach()` (new) is for: it returns the descriptor and clears the handle without closing. Capture `localIP()` BEFORE detaching; esp-tls does not offer it, and PORT (active mode) needs it.
- **`fnFtpControl` is the control connection AND the data connection.** It presents exactly the surface `fnFTP` asks of a socket and holds either an `fnTcpClient` or a TLS session, so all ~70 call sites read the same in both modes. Three things in it are not obvious: `available()`/`peek()` need a buffer (`_rx`) because TLS has no `FIONREAD`, and a fill must ask `esp_tls_get_bytes_avail()` BEFORE polling the socket — mbedTLS can already hold a decrypted record while the socket reads empty; the descriptor is set `O_NONBLOCK` *after* the handshake (the handshake itself must block, and `fnTcpClient::connect()` already restores blocking mode when it returns), so every read and write has to treat `ESP_TLS_ERR_SSL_WANT_READ`/`WANT_WRITE` as "retry", not as failure; and `start_tls()` refuses if anything is already waiting on the socket, since a server has nothing to say between its `234` and the handshake.
- **The data channel needs its own TLS session, and `PROT P` is asked for unconditionally once the control channel is protected.** A refusal needs no follow-up — clear is the default when no `PROT` is in force — so `_data_protected` simply stays false. **This costs a SECOND concurrent TLS session for the duration of every transfer**, which at `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384` is ~32 KB of RX buffers; on a PSRAM board `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` puts those in PSRAM, and on a board without PSRAM the handshake will simply fail and the transfer reports an error. **FTPS is a PSRAM-board feature.** The handshake runs AFTER the `150`, in `start_data_tls()` at the one point both `open_file()` and `open_directory()` pass through, because that is when the server is listening for it.
- **Do NOT assume the data channel can stay in the clear because the server's control-channel refusal said "control channel".** ProFTPD answers `550 SSL/TLS required on the control channel` and then `522 SSL/TLS required on the data channel` separately, and `PROT C` is refused with `534 Unwilling to accept security parameters`. Both had to be observed. Note the 534 was first seen on an ANONYMOUS session and could have been a per-user policy — it was re-confirmed against an authenticated one before `PROT P` was scoped.
- **TLS session reuse is NOT implemented and the server must not require it.** RFC 4217 data connections conventionally resume the control session, and many servers enforce that (ProFTPD's `TLSOptions NoSessionReuseRequired` is the escape hatch); esp-tls cannot resume here because `CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS` is not set on any board. meatloaf.cc accepts a fresh session — **verified before writing any code** with a Python `ftplib.FTP_TLS` subclass that deliberately wraps the data socket without passing `session=`, listing 43 entries either way. A server that does require reuse will refuse every transfer no matter how correct the rest is; that is a sdkconfig change plus `esp_tls_get_client_session()`/`cfg.client_session`, not a bug in this code.
- **The certificate CHAIN is verified against the CA bundle; the NAME in it is not** (`cfg.skip_common_name = true`). FTPS is routinely served under a certificate issued for the machine rather than the name dialled — meatloaf.cc answers with one for `vpsl.techknowpro.com` — and refusing those makes the feature unusable against real servers. The cost is real and worth stating: credentials on that channel are protected from eavesdroppers but not from an active attacker holding any CA-issued certificate. For a server you control, adding the FTP hostname to the certificate's SAN is the actual fix.
- **Credentials in an FTP URL never reached the client** (`network/ftp.h`, `network/ftp.cpp`) — `FTPMSession::connect()` built `ftp://host[:port]` and called `_fs->start(base)` with both credential arguments defaulted, so `fnFsFTP.cpp` substituted `anonymous` / `fujinet@fujinet.online` for EVERY connection. Fixed with SMB's pattern: `ftpSessionHost()` embeds `user[:password]@` in the SessionBroker host key (so the credentials reach the constructor before `connect()` runs, and two users of one server get two sessions), the constructor splits them back off, and `connect()` passes them — **as `nullptr`, not `""`, when absent**, since `start()` tests the pointer and an empty string would send a literal empty `USER`. Both `obtain<FTPMSession>()` call sites (`FTPMFile` ctor and `FTPMStream::open()`) must build the key the same way.
- **An FTP transfer must have OUR end of the data socket closed, promptly and unconditionally.** The server half-closes when it has sent everything, then waits for our FIN before emitting its `226`; it gives up only after its own timeout, measured at a constant **10396 ms** on zimmers.net. So every operation stalls ~10 s AFTER the last byte, and it presents as a hang rather than an error. This bit three times in one session: `close_directory()` guarded `data->stop()` with `if (data->connected())` — which is never true by then, because `connected()` has already peeked and seen the FIN, which is what prints `fnTcpClient disconnected because no bytes to read`; `next_directory_line()` ended a listing without closing; and `FileHandlerFTP::read()` left the socket for `close()` instead of ending the transfer the moment the file was complete. **Never guard the close on `connected()`**, and end the transfer at end-of-data, not at handle destruction.
- **`MALLOC_CAP_SPIRAM` on its own can NEVER be satisfied on a board without PSRAM** — `heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` in `FileHandlerMem::grow()` therefore failed unconditionally there, whatever the heap looked like, so every memory-cached file (i.e. every file read over FTP or HTTP, since `FileCache` keeps them in RAM until its 200 KB threshold and only ever spills to SD) failed on those boards. Always follow a SPIRAM request with a `MALLOC_CAP_8BIT`/`MALLOC_CAP_DEFAULT` retry, the way `PSRAMAllocator` does. A capability request is a hard filter, not a preference.
- **`PSRAMAllocator::allocate()` aborts on failure rather than returning NULL** (`include/PSRAMAllocator.h`). ESP-IDF builds `-fno-exceptions`, so a container that gets NULL from its allocator uses it anyway: `std::vector`'s `_M_realloc_append` wrote through it and produced `StoreProhibited` at `EXCVADDR 0x1100` — 16 `fsdir_entry`s past address zero — which reads as memory corruption rather than exhaustion. It now logs the size it could not satisfy plus free and largest-block, then aborts.
- **`fnTcpClient::available()` PULLS data into a `std::string`; it is not a counter.** `updateFIFO()` used to resize that string to the whole `FIONREAD` count in one go, which during a fast transfer is several KB and aborts in `operator new` on a small heap. It is now capped at `FNTCP_RX_CHUNK_SIZE` (2048) per call — every caller loops on `available()`, so the remainder simply stays in the socket. The old `for (count = res; count; count -= result)` loop also spun forever if `recv()` returned 0 (peer closed), since `count` never decreased.
- **Free heap and largest CONTIGUOUS block are different questions, and only the second one explains an allocation failure.** `meminfo` prints both. The numbers that settled this session, on an `esp32-wroom32` (no PSRAM): 27 KB free / 14324 largest at idle, and **5960 free / 1972 largest during an FTP transfer** — the data socket and in-flight data cost ~17 KB. Nothing on that board can allocate 8 KB while a transfer is running, which is why the whole-file cache had to go.
- **`CONFIG_LWIP_TCP_WND_DEFAULT` must be small on a board without PSRAM.** It was 65534 on `esp32-wroom32` — a 64 KB receive window advertised by a device with ~23 KB of 8-bit heap, so lwIP accepted far more than the heap could hold and the abort landed in whatever allocated next. Now 5760 (4×MSS), with `SND_BUF` to match and `RECVMBOX` 64 → 16. This is the change that turned a board reset into a clean failure. PSRAM boards do not need it — an earlier experiment on `lolin-d32-pro` was reverted for that reason.
- **FTP file reads STREAM; they are not cached first** (`lib/FileSystem/fnFileFTP.h/.cpp`). `FileHandlerFTP` reads straight off the live RETR connection through a 512-byte buffer, so the RAM cost is the same whatever the file's size. **The transfer starts LAZILY, on the first read** — that is what lets `FTPMStream::open()` keep its seek-to-end/tell/seek-to-start size probe with no network traffic, and is why that code needed no change. Backwards seeks (and forward seeks past 8 KB) restart the transfer at the new offset with `REST`; shorter forward seeks read and discard. `FileCache::open()` is still tried first, so `#cache=sd` is unaffected, and a server that will not answer `SIZE` falls back to the old caching path. **`REST` itself is not hardware-verified** — every read so far has been forward-only.
- **`DISABLE_DIRCACHE` (undefined by default) makes `FileSystemFTP` stream its directory listing** instead of caching it. `DirCache` holds a `fsdir_entry` per entry — `char filename[256]` plus size and flags, **272 bytes** — from the internal heap on a board without PSRAM, which is the largest single cost of an `ls` over FTP. With the flag set, one entry is held at a time: no sorting (`FTPMFile` passes `DIR_OPTION_UNSORTED` anyway, so only a fuji host slot notices), no listing re-use, and `dir_seek()` re-lists and skips forward. Only `FileSystemFTP` honours the flag; SD, NFS, SMB, HTTP and TNFS still cache unconditionally.
- **`FTPMFile::readEntry()`'s `dir_open` test was inverted** (`if (!fs->dir_open(...))`) — `dir_open` returns true on success, so the scan loop only ever ran on a listing that had FAILED to open, and an FTP wildcard could never match. Fixed; note the same call must `dir_close()` on both exits now that streaming leaves the data connection open.

## Recent Changes (August 22, 2026)

### SPY and WRA/WR3 filesystems

Two new read-only filesystems, `spyFS` (`.spy`) and `wraFS` (`.wra` + `.wr3`), both registered
behind `EXTRA_DISK_FORMATS` alongside arc/g71/g81/p81. Durable rules are in the Important Notes
above; format detail is in `lib/meatloaf/AGENTS.md`.

- **Both formats were decoded from their specifications and then validated against the real corpus
  before any firmware code was written.** That is what made the implementation cheap: the layouts
  were already known-good, and both native suites went green on their first run bar one wrong
  assertion of mine (see the SPY tiling note above).
- **SPY's oracle is the checksum every directory entry carries** — a 16-bit sum without carry over
  the file's own bytes. All **67 entries across the five corpus containers** read through the stream
  and match, and they match over exactly the derived size rather than over the padded block, which
  is what pins the `(blocks - 1) * 254 + (lsu - 1)` size formula as well as the offsets.
- **WRA has no usable checksum** — the format document gives no CRC algorithm — so the suite uses
  two other oracles: clean termination inside the span the container frames, and the fact that
  Wraptor's nine-byte GEOS header duplicates two fields into the info sector 254 bytes further on
  and states a block count that reconciles with the decoded length. All **28 entries across seven
  archives** satisfy both. A third check decodes the same entry out of three independently built
  archives and compares byte for byte.
- **Termination alone is NOT sufficient, and the suite says so because it was measured.** Reversing
  the bit reader to LSB-first still terminates cleanly on every entry in the corpus — it just
  produces 176 bytes where 511 belong. Only the reconciliation catches it. The header comment in
  `test_wra_read.cpp` originally claimed termination would catch a wrong bit order; the mutation run
  disproved that and the comment was corrected rather than left flattering.
- **Both suites were mutation-checked.** Changing SPY's block size 254 → 256 fails 6 of its 9 cases;
  reversing WRA's bit order fails 5 of its 12. Worth repeating after any change to either decoder —
  a suite that cannot reach the defect it exists for is the trap the g64 and nib entries document.
- **An off-by-one over-read in WRA's name scan was found by re-reading the code, not by the tests**:
  the type byte is read out of the same buffer as the name, so a window sized to the name alone
  over-reads it by one when the terminator lands on the last byte. The corpus never reaches it
  (names are ≤ 13 bytes against a 32-byte window), which is exactly why no test caught it.
- **Builds**: `lolin-d32-pro` (feature compiled but not registered) Flash 42.4%, RAM 32.4%;
  `esp32-s3-devkitc-1` (feature registered) Flash 82.0%, RAM 30.8%. Neither overflows
  `iram0_2_seg`, and `lolin-d32-pro` is an ESP32-WROVER PSRAM board — the same small-window
  exposure class as `fujiloaf-rev0` — so the flash-window question is answered for that class.
  Native suites `test_spy_read` 9/9 and `test_wra_read` 14/14, **no skips**; full suite 308 cases,
  295 succeeded, 10 skipped (all pre-existing), 3 errored (`test_EdUrlParser`, `test_hdd_read`,
  `test_strings` — the documented baseline trio). 274 + 21 new = 295, so nothing regressed.
- **`fujiloaf-rev0` and `iec-nugget` do NOT build, for a reason unrelated to this work.**
  `lib/bus/iec/IECConfig.h` has an unbalanced preprocessor chain — line 67 closes the
  `#ifndef IECCONFIG_H` include guard early, so line 106 is `#endif without #if` and everything
  from line 83 down falls outside the guard. `iec-nugget` reports that directly; on
  `fujiloaf-rev0` it surfaces one level on, as `IEC_SUPPORT_FASTLOAD` never being defined and
  `m_buffer`/`m_bufferSize` therefore being undeclared across `IECBusHandler.cpp`. That whole
  subsystem is mid-edit (304 uncommitted lines adding AR6/Hypraload fast loaders) and was not
  touched here. Re-check both boards once it balances.
- **NOT hardware-tested, and currently not reachable on the board this repo is usually tested with.**
  `EXTRA_DISK_FORMATS` is set only for `esp32-s3-devkitc-1`, while every hardware verification in
  this file was done on `lolin-d32-pro` — so exercising either format on real hardware means using
  the S3, or adding the flag to another board and checking it still links. Nothing has been driven
  from a real C64 or from the device console; the whole of the evidence above is the native suites
  plus the corpus. The `MFile` half (`rewindDirectory`/`getNextFileInDir`) is not natively testable
  at all, since `MFSOwner::File()` aborts under the stubs — the same limitation the M2I, HDD and
  CSM suites carry.
- **The one non-GEOS Wraptor sample that exists anywhere is the format document's own worked
  example, and it is now a fixture.** The corpus is 100% GEOS, so nothing in it exercises a plain
  file — and "a plain file carries no nine-byte header", which is what the serve-verbatim decision
  rests on, had no test behind it. That 128-byte dump decodes to 80 bytes of real C64 BASIC
  (`$0801`, link to `$0823`, `LOAD "POOYAN.LOADER",8,1`) with the load address at offset 0 and no
  header, terminating exactly at its CRC. It also re-establishes the bit order independently of the
  GEOS corpus: a BASIC link pointer that walks correctly cannot come out of a decoder reading its
  bits the other way round.
- **The buffered signature scan's chunk-boundary carry has its own fixture**, because no corpus
  archive happens to put a signature across a 1024-byte boundary and the carry would otherwise be
  untested.
- **GEOS files are not reconstructed, deliberately.** A `.wr3` entry reads back as Wraptor's nine
  bytes plus the GEOS info sector plus the file's data, which is the honest content and consistent
  with how a VLIR file already reads out of a D64. Emitting CVT would make `wra.cpp` the only place
  in the tree that understands GEOS file structure, and there is no known-good CVT for these entries
  to verify it against.

## Recent Changes (August 19-20, 2026)

### Console file channels, ESC cancel, and the block/direct-access commands

`open`/`read`/`write`/`close`/`channels` drive the selected device's FILE channels the way a C64
does, so a read or a write inside any mounted media can be exercised without a Commodore attached —
the channel number IS the secondary address. That then made the block commands reachable and worth
finishing: `B-R`/`B-W`, `U1`/`U2`, `B-P`, the new `F-P`, and `"#"` direct access. Durable rules are
in the Important Notes above; design spec for the console half in
`docs/superpowers/specs/2026-08-19-console-file-io-design.md`.

**The command set, since three of these changed meaning during the work:** `F-P` is the absolute
seek (any offset in a file or container). `B-P` is the pointer within the block `B-R`/`U1` selected.
`"#"` opens a one-block window that `F-P` widens to the whole container and `B-R`/`U1` narrow again.

- **Hardware-verified on lolin-d32-pro**, console over serial and over TCP:
  - `open 0 $` + `read 0` inside `zetawingii.d64` returns the directory as a PRG — `01 08`, disk
    header `"MEGA ZETA WING 2" -LXT-`, 1007 bytes, exactly what `LOAD"$",8` gets.
  - `open 0 zw2set.dat` + `read 0` returns 94 bytes **byte-for-byte identical to `hex zw2set.dat`**,
    so the channel path and the MFile path agree. Note the directory says 1 block and `seekPath()`
    reports 254; the chain end fires at 94 — the documented over-estimate, not a truncation.
  - `open 2 consolewrite.seq,s,w` + `write` + `close` + `hex` round-trips 42 bytes including an
    embedded `0x0D0x00`.
  - **Writing INSIDE a disk image**, which is the capability the feature exists for:
    `open 2 scratch,s,w` + `write` + `close` in `blueangels-xiphoid.d64` logs
    `beginFileWrite(): Creating [scratch] first block track[10] sector[0]` and
    `finalizeFileWrite(): Created [scratch] start[10/0] blocks[1] entry at [18/7] slot[6]`;
    reopening `,s,r` reads the 24 bytes back exactly. `write 15 s0:scratch` then answers
    `01,FILES SCRATCHED,01,00` and **the container is still 174848 bytes on SD** — the check that
    matters, because `getSourceStream()` opening the container `out` instead of `in|out` would
    truncate the image rather than fail.
  - Channel 15: `read 15` answers `00, OK,00,00` and `write 15 i0:` answers
    `73,MEATLOAF CBM 20260819.22` — the status and the DOS banner a C64 gets.
  - **Direct access, cross-validated against the image's own structures rather than against itself.**
    On a `"#"` channel over `goonies.d64`, seeking to 91392 (17 tracks x 21 sectors x 256) reads
    `12 01 41 00 15 FF FF 1F` — the D64 BAM — and 91648 reads track 18 sector 1,
    `12 04 82 11 00 "THE GOONIES+"` ending `9B 00` = 155 blocks; 155 x 254 = 39370, the size
    `seekPath()` reports for that same file opened normally. Two independent paths agreeing.
  - **Block windows**: `open 2 "#"` gives SIZE 256 POS 0 and `read` returns exactly 256 bytes;
    `U1 2 0 18 1` SIZE 256 POS 0 reads the whole directory sector; `B-R 2 0 18 1` SIZE **18** POS 1
    reads exactly 18 bytes — the count byte `$12` — starting `04 82 11 00 "THE GOONIES+"`;
    `B-P 2 5` POS 5 reads `"THE GOON"`, block byte 5, counted from the block base and not from
    B-R's byte-1 start.
  - **The mode switch, both ways**: `F-P 2 91648` leaves block mode (SIZE 174848, POS 91648
    absolute) and `read 2 300` returns 300 bytes ACROSS the block boundary, where the same read in
    block mode is capped at 18; `B-R` then puts it straight back to SIZE 18 POS 1.
  - **On a FILE channel**: `B-P` answers `31,INVALID COMMAND`, while `F-P 3 254` seeks absolutely
    and reads `59 1A 4A 16 09 BF 90 A9` — matching the linear dump at that offset. Seeking to 0
    repeats the first bytes exactly, and 254 and 508 match byte for byte at both block boundaries,
    which is where a wrong `data_per_block` or a re-read link would show.
  - Block-command errors: bad track and bad sector both answer `66,ILLEGAL TRACK OR SECTOR`
    carrying the offending track/sector; an unopened channel `61,FILE NOT OPEN`; a write command on
    a read-only channel `26,WRITE PROTECT`.
  - **`U2` writes**, verified non-destructively: `DE AD BE EF 01 02 ... 0D` written to track 1
    sector 0 of `blueangels-xiphoid.d64`, read back byte-exact, then the saved original written
    back and re-verified.
  - `channels` tracks the reader: `read 2 256` then `read 2 100` inside `goonies.d64` gives
    POS 0 → 256 → 356 against SIZE 39370; a write channel gives 10 → 20 as the bytes go in; a
    directory channel gives 0 → 64 with SIZE `-`. Two channels list side by side.
  - Error paths: a missing name answers `62,FILE NOT FOUND` on open and the command exits non-zero;
    reading an unopened channel answers `61,FILE NOT OPEN`.
  - ESC cancel on both transports: serial `read` stopped at 5120 bytes of 149486 (20 x 256, on the
    poll boundary) and `read 0 16` afterwards resumed at `pos[5120]`, so a cancelled read leaves its
    channel open and positioned; `cat` stopped at 21307 of 153701. Over TCP the same `cat` cancelled
    and returned to a prompt, and a **control run with no ESC did not cancel** — which is what makes
    it evidence rather than a coincidence.
  - **Double-opening a channel does not leak**: `iecDrive::open()` closes an occupied channel first
    (`drive.cpp:947`), so 20 opens with one close left internal free and largest-block bit-identical
    at 24 KB / 15860. Worth knowing because the console reaches this in a way the bus never did.
  - Noted, not fixed: every `open` calls `set_cwd()`, which persists `devices.json` — a LittleFS
    write per open. Pre-existing to `open()`, but it makes scripting hundreds of them expensive.
  - Also noted, not fixed: `cp` refuses to copy a `.d64` (it resolves as a directory), so there is
    no way to make a scratch image from the console.
- **`CONSOLE_MAX_CMDLINE_ARGS` was 8 and silently truncated every command line** — see the Important
  Notes entry. Found by `write` losing the tail of a sentence with an OK status.
- **`dos_read_loop`/`dos_write_loop` take FORWARDING references.** Taking the callables by value
  compiles and runs, and silently accumulates into a copy the caller never sees — the sink reported
  zero bytes while the return value was right. The native suite caught it on the first run; nothing
  about the firmware behaviour would have.
- **A `case` without a `break` in `executeData()`'s switch reaches `I` and resets the drive.** `case
  'F'` was added without one, so any unmatched `F-*` reinitialised the device. The switch is long
  enough that the fall-through is invisible at the point of editing.
- **The `_size` cap added to `MMediaStream::read()`'s raw-container branch reaches every reader of
  that path**, not just direct access — image copying included. It is the correct bound (the file
  branch already had it, and `_size` is now set for that view), but it is the one change here that
  is not confined to the new commands. The full native suite passing is the evidence.
- **Native suite**: `test/native/test_console_dos/` (21 cases) covers `encodeDosCommand` and both
  transfer loops. Full suite 274 passed, 10 skipped; `test_EdUrlParser`, `test_hdd_read` and
  `test_strings` error identically at baseline.
- **Not covered.** The C64-side path: every one of these commands reaches the same `iecDrive` entry
  points the bus does, but nothing here was driven from a real Commodore. And nothing below the
  console — channels, block commands, `B-P`/`F-P`, direct access, `D64MStream::seek()` — has a
  native regression test, because `lib/console` and `lib/device` are not compiled in that
  environment; all of it is hardware-verified only, so a refactor has no net under it.

### Debug-loop note

- **The serial capture daemon dies on a binary `cat`.** `cat` of a `.prg` writes raw bytes to the
  daemon's stdout, which on Windows is cp1252 and raises `UnicodeEncodeError`; the daemon logs
  "Unexpected error ... Retrying" and reconnects, losing the tail of the run. Use `hex` for binary,
  or a text file, when testing through the skill. Tooling bug, not firmware.

## Recent Changes (August 18, 2026)

### FTP over a board with no PSRAM: listings, file reads, and four memory defects

Reported as `cd` into an FTP directory rebooting the board, then `ls` taking 11-13 s, then a file
read failing outright. All on `esp32-wroom32` — 4 MB flash, **no PSRAM**, `MIN_CONFIG` plus console,
now also carrying FTP/FSP/TNFS. The durable rules are in the Important Notes above; what follows is
what the work touched and how each conclusion was reached, because several of the obvious readings
were wrong.

- **`is_dir()` downloaded the whole directory to answer a yes/no.** It ran `LIST` and compared the
  first entry's name against the path. Now one `CWD`: the server accepts it only for a directory and
  answers 550 for a file or a missing path, which are both "not a directory" here. That also
  replaced a fragile basename-matching heuristic. Every path `fnFTP` is given is absolute (checked
  across `fnFsFTP.cpp` and `lib/meatloaf/network/ftp.cpp`), so leaving the server's working
  directory where `CWD` put it changes nothing.
- **The listing streams.** `open_directory()` now returns after the 150 and `next_directory_line()`
  pulls bytes on demand, holding only a partial-line remainder; `close_directory()` ends a listing a
  caller abandons. `exists()` reads one entry and stops, so it must call it, and `ensure_connected()`
  closes any stray listing before anything else runs — without that a later `RETR` reads the wrong
  response off the control channel.
- **The 11-13 s `ls` was measured, not guessed, and the measurement is what found it.** Temporary
  probes gave `data port 95 ms, LIST+150 51 ms, end of listing 1 ms, closing response 10396 ms` —
  identical to the millisecond across runs, which is a timeout rather than server jitter, and
  pointed straight at the unclosed socket. Every other phase was already fast, which ruled out DNS,
  EPSV and the streaming loop in one line. After the fix: 95-101 ms typical, one run at 1945 ms
  (genuine server variance now — it varies, where the bug was constant).
- **The file-read failure took four attempts to reach, and the first three were the wrong layer.**
  A 4 KB copy buffer in `cache_file()` failed to allocate, so it now halves until something fits;
  that changed nothing. `updateFIFO()`'s unbounded slurp was aborting in `operator new`; bounding it
  changed nothing on its own. Capping `TCP_WND` stopped the board resetting but the read still
  failed. Only then did the diagnostic print the number that mattered —
  `want 2048, had 0, free8 5960 largest8 1972` — and the fix that actually worked was removing the
  whole-file cache from the path.
- **Both the `heap_caps_realloc` fallback and the streaming handler were needed.** The PSRAM-only
  allocation made `FileHandlerMem` fail on every non-PSRAM board unconditionally; fixing it let the
  transfer start, at which point the 8 KB it wanted was simply not there. The cache had to go.
- **Verified on hardware**: `hex Zauberland.prg` (7298 bytes) returns exactly 7298 bytes ending at
  `0x1C82`, starting `01 08 10 08 C4 07 9E 32 30 36 36` — load address `$0801`, `SYS 2066`, a valid
  PRG — with the `226` consumed cleanly and heap steady at 21 KB free / 8180 largest across repeated
  reads. `hex Tomb.readme` (492 bytes) likewise, and its trailing ~10 s stall is what produced the
  third instance of the close-our-end rule.
- **Two defects found by reading code around the ones being fixed**: `dir_open`'s hidden-file skip
  was `if (filename[0] == '.') continue;` with the `read_directory()` call at the BOTTOM of the
  loop, so any dotfile re-tested the same entry forever — it hangs the board on any FTP directory
  containing one, and zimmers has `.message` files. And `readEntry()`'s inverted `dir_open` test,
  above. Neither was part of the reported problem.
- **Not done, deliberately**: `FileCache` still spills only to SD, so a `#cache=sd` request on a
  board without an SD card still has nowhere to go. Streaming made that moot for reads; a write path
  would need it.

### Debug-loop notes (`.claude/skills/commodore64-debugging`)

- **Stop the serial capture when a session ends** — it holds the port, and on Windows that is
  exclusive, so it blocks both the user's own monitor and the next `esptool` run
  (`Could not open COM7, the port is busy`). The Windows flash sequence is therefore **stop capture,
  flash, start capture**. Documented in `SKILL.md` as its own step.
- **`pio` is not on PATH** — the skill's `build --flash` fails with `FileNotFoundError`; use
  `~/.platformio/penv/Scripts/pio.exe` directly. Already noted for builds generally; it applies to
  the skill's scripts too.
- **The console REPL eats the first byte after boot** (it is dormant in `fgetc()` until then), so a
  throwaway `send ""` is needed before the first real command or it arrives with its first character
  missing — `cd ftp://...` becomes `d ftp://...`, "Unrecognized command".
- **Decode a backtrace against the ELF that produced it, immediately.** Two crashes here were
  decoded a build too late and pointed at the wrong line; the second had to be reproduced just to
  read it. `xtensa-esp32-elf-addr2line -pfiaC -e .pio/build/<env>/firmware.elf <addrs>`.
- **A build-flag or sdkconfig change invalidates the whole tree** — ~9.5 minutes for this board
  against ~2 for a source-only change. Adding a new source file does the same, per the CMake source
  glob. On Windows the invalidation can also leave the tree wedged with
  `CMake Error: Permission denied` writing `CMakeCXXCompiler.cmake.tmp`; `rm -rf .pio/build/<env>`
  and rebuild.

## Recent Changes (August 17, 2026)

### -lh1- (LHarc 1.x) support in libarchive's lha reader

`hex menu` inside `/sd/content/archive/lzh/games.lzh` answered
`Unsupported lzh compression method -lh1-` while the listing was perfect — sizes and names come
out of the entry headers, so only reading was affected. libarchive supports `-lh0-` (stored),
`-lh5-`, `-lh6-` and `-lh7-`; `-lh1-` is LHarc 1.x's method and shares nothing with them above the
LZSS layer. All three `.lzh` samples in `.archive/archive/lzh/` are `-lh1-`; the six `.lha` ones are
`-lh5-`/`-lh0-`.

What `-lh1-` is, and what that cost: a 4 KiB LZSS window whose literal/length alphabet (314 symbols:
256 literals plus lengths 3-60) is coded with an **adaptive** Huffman tree that both sides mutate
after every symbol, and whose match positions come from a **fixed** table rather than one sent in
the stream. So there are no blocks, no transmitted code lengths and no end marker. Ported from
`dhuf.c`/`shuf.c` of *LHa for UNIX* (`start_c_dyn`, `swap_inc`, `reconst`, `update_c`, `ready_made`)
as a resumable state machine — the existing decoder is fed input in chunks, and an adaptive code has
no length bound the way a table-driven one does, so the tree walk consumes **one bit at a time** and
suspends on the node it reached (`ST_LH1_TREE`, `dyn->walk`) instead of asking for a fixed number of
bits up front. Everything from the match copy onward is the shared `ST_COPY_DATA` path, reached by
making the state a finished match returns to a variable (`ds->literal_state`).

- **`ready_made(0)`'s code lengths are 3-8 with a Kraft sum of exactly 1**, so one 8-bit lookahead
  always resolves a position symbol and no tree walk is needed for positions. It is expanded into
  the 256-entry lookahead table it is only ever used through.
- **The tree state is ~8 KB and is allocated only for `-lh1-`** (`struct lzh_dyn`, heap, so PSRAM on
  a PSRAM board). `struct lzh_dec` is allocated per stream and every other method leaves `dyn` NULL.
  The position-tree arrays are deliberately absent: only `-lh2-` (still unsupported) codes positions
  adaptively.
- **Verified by the archives' own CRCs.** Every entry carries a CRC-16 of its *decompressed* bytes,
  written by the original compressor, and the reader checks it at end of entry — so decoding a real
  archive is a bit-exactness test. `test_archive_extract` gained 5 cases: 100 entries across
  `games.lzh` (25), `Taboo.lzh` (4) and `Tomb.lzh` (71) all decode to exactly their declared size
  with no CRC error, a listing walk and a decoding walk agree entry for entry, a forged short
  `origsize` still leaves the walk aligned, and a forged `-lh2-` is still refused. Full native suite
  262 cases, 243 pass, 16 skip; `test_EdUrlParser`, `test_strings` and `test_hdd_read` error
  identically at baseline.
- **`-lh5-` decode coverage was added at the same time, and is not optional.** The edits above touch
  code every method runs through — the window pre-fill, `ds->literal_state`, the EOF accounting —
  and nothing in the suite decoded through the lha reader at all (the zip cases skip without their
  sample; the gz cases go through gzip). `test_lh5_entries_still_decode_and_pass_their_crc` walks
  the `.lha` samples twice, listing then decoding, and checks every entry against its CRC.
  `rasm.lha` is the one that matters: 135 entries including one of 851804 bytes, which flushes the
  128 KiB window six times.
- **`reconst()` is NOT exercised by the corpus** — verified with a temporary probe: it never fires.
  It runs when the tree root's frequency reaches `0x8000`, i.e. after ~32450 symbols in ONE entry,
  and the largest `-lh1-` entry available is 41343 bytes (the tree resets per entry, so many small
  entries never accumulate). A mis-port there would surface as a CRC failure, not a crash — every
  index in it is structurally bounded. Finding a large `-lh1-` entry, or diffing the port against
  `dhuf.c` step by step over a synthetic symbol stream, is what would close it.
- **The window flush/suspend paths were covered by shrinking the window, not by a big sample.**
  `ds->w_size` is expanded to 128 KiB purely for decode speed, so setting it to twice the real
  window (8 KiB for `-lh1-`) makes every entry over 8 KiB cross the boundary repeatedly. The whole
  corpus still decodes CRC-clean that way, which is what exercises `-lh1-`'s literal flush, the
  mid-copy suspension returning through `ds->literal_state`, and an EOF with a partial window. Worth
  repeating as a one-off after any change to that path.
- **`mce.lha` exposed a second, older defect — see the Amiga bidder entry below.** It could not be
  read at all, and that predated `-lh1-` support.
- **Hardware-verified on lolin-d32-pro** (console over serial; the C64 LOAD path is still untested).
  The method: dump an entry with `hex`, parse the dump back out of the serial log, CRC-16 it and
  compare against the CRC the compressor stored in that entry's header — the same oracle the native
  suite uses, run end to end through the device.
  - `games.lzh/menu` 895 bytes CRC `0xc193`, `Taboo.lzh/readme` 377 bytes CRC `0x573d`,
    `Tomb.lzh/tiamat's tomb` 17214 bytes CRC `0x2120` — all three `-lh1-` archives, all exact.
  - `unzipx /sd/content/archive/lzh/games.lzh` walked all 25 entries and wrote 24 (163312 bytes)
    with no decode or CRC error. The one that did not write is a **filename** failure, not a decode
    one: its name holds PETSCII graphics characters the filesystem cannot represent.
  - This is the run the alignment rule above protects, and it is why it is worth doing over
    `hex menu`: it reads each entry to its end and then continues to the next header.

**The feature cost 2071 bytes of flash text and `lolin-d32-pro` had 1086 bytes of `iram0_2_seg`
headroom**, so it overflowed by 985. Two levers made it *worse*, both worth not retrying:
`#pragma GCC optimize("Oz")` on the file (985 → 1013, the same inversion the ARC work saw with
`-Os`) and `__attribute__((noinline))` on the tree helpers (985 → 1001; everything in the decode
path is inlined into `archive_read_format_lha_read_data`, 5472 bytes at baseline). The space came
from narrowing the `format_all` fallback instead — see the Important Notes entry. Both boards build
*smaller* than before: `lolin-d32-pro` Flash 42.5% → 42.2%, `esp32-s3-devkitc-1` 82.0% → 81.5%.
Note PlatformIO's `build_flags` do **not** reach `components/` compiles (checked in `build.ninja`:
no `EXTRA_DISK_FORMATS`, no `PINMAP_*`), so a per-board gate inside libarchive would need CMake or
sdkconfig plumbing — which is why the budget was solved rather than the feature gated.

### Amiga level-1 headers are no longer rejected by the lha bidder

Found while adding the `-lh5-` coverage above: `mce.lha` could not be read at all — libarchive
declined it outright, "Unrecognized archive format", zero entries listed, and this predated `-lh1-`
support. `lha_check_header_format()` required `H_ATTR_OFFSET == 0x20` for header levels 1-3. That
byte is the **MS-DOS** attribute; Amiga LhA writes AmigaOS protection bits there (`0x02` in this
archive), so every Amiga level-1 archive failed the bid before a single entry was read. Level 0 was
already accepted with no such check at all.

The test is now `0x20` **or** a self-consistent level-1 header
(`lha_level1_header_is_plausible()`): the header must be long enough for its own fixed part plus its
name, with a name length `lha_read_file_header_1()` would accept. Only the first `H_SIZE` (22) bytes
may be read there — that is all the bidder and the SFX scan guarantee — which is why the check is
structural rather than a checksum over the whole header. Two things make relaxing it safe: nothing
downstream reads the attribute byte (`lha->dos_attr` comes from extended headers only), and
`lha_read_file_header_1()` verifies the header checksum, so a false positive fails the read instead
of fabricating entries. **Levels 2 and 3 keep the `0x20` requirement** — their first 22 bytes carry
no name length to check against, and no failing sample exists.

Verified: `mce.lha` now lists and decodes all **997** entries, each to its declared size with its
CRC-16 accepted — 3.4 MB, level-1 headers with extended headers, `-lh5-`. The entry count is
asserted in the test, so it cannot pass by finding the first few and stopping.

**Hardware-verified too, and doing so found a second defect this fix exposed.** On the device,
`hex mce.lha/MCE.info` re-opened the archive about once a second and never returned. Cause: entry 66
of the 997 is an empty file, and `seekEntry()`'s size probe re-reads from the first entry, which
restarts the name walk — see the Important Notes entry on the probe. That bug is older than any of
this work; it was simply unreachable while no Amiga archive could be opened, and every `.lzh`
sample happens to have no empty entry. With the probe gated, `MCE.info` decodes in ONE archive open
(4886 bytes, CRC `0xb122`) and `game1` — entry 67, immediately past the empty one — decodes too
(1564 bytes, CRC `0x9e25`).

### ArcDecoder overflowed the console_exec stack

`hex boot` on `/sd/content/archive/arc/WARGAMES.ARC` rebooted the board (`7aa47c20`).
`ARCMStream::extractEntry()` declared `ArcDecoder d;` as a local — ~14.4 KB of LZW and Huffman
tables in a 16 KB task stack, under `mfilebuf` / `getSourceStream` / `fread`. Now heap-allocated.
The durable rule is in Important Notes above; two things worth keeping here:

- **The first dump pointed hard at the wrong subsystem.** It was a `LoadProhibited` inside newlib's
  `memcpy` from `_fread_r`, `EXCVADDR` exactly 0 and the length equal to the full request — i.e. the
  `FILE` claimed the bytes buffered while its buffer pointer was NULL, which newlib's own code
  cannot produce. That is a genuinely compelling case for heap corruption, and it was wrong. What
  settled it was reproducing on hardware and reading the panic line, which says
  `A stack overflow in task console_exec has been detected` in as many words. A probe was added to
  `FlashMStream::read` to catch the "corrupt FILE" and **never fired** — that negative result is
  what redirected the search.
- **`ls` was never implicated.** The listing reads sizes out of the entry headers and constructs no
  decoder, which is why listing worked and the first read crashed; and the crash reproduces with no
  `ls` at all. The native suite passes because a host stack is megabytes.

Hardware-verified on lolin-d32-pro across three archives and every compression mode present
(`WARGAMES.ARC` boot/guerilla warfare/war games/sam, `zos.arc`, `pcpfonts.arc`), each extracting to
exactly the size its directory entry declares with no checksum errors; `console_exec` high-water
went from overflow to 12684 bytes free. `test_arc_read` still 6/6.

### Names are UTF-8 internally; PETSCII only at the IEC boundary

Meatloaf stored container entry names as raw PETSCII and left each consumer to convert. The flag
declaring that, `isPETSCII`, is a property of the CONTAINER — but `ls` read it off the ENTRY, and
entries are built by `MFSOwner::File(entryUrl)`, so their class is chosen by extension-sniffing
their own name. `GAME` inside a D64 resolved to a `D64MFile` (flag set) while `GAME.PRG` resolved to
a `PRGMFile` (flag clear), so half of one listing came out converted and half raw. The comment at
`ark.cpp:207` documents the same bug patched by accident in a different format.

Inverted rather than patching the read, in four commits (`aaea3c30`, `5ac4b3e1`, `728eec91`,
`8184006f`). The durable rules are in the Important Notes above; what follows is what the work
touched and what it is worth.

- **The lookup side already assumed UTF-8 in.** d64, ark, lbr, lnx, arc, t64, m2i and tap all
  `toUTF8()` the stored name before comparing; hdd does the mirror with `toPETSCII2()` on the
  incoming one. The LISTING side was the outlier. That is what made the inversion the cheap
  direction — 14 producers gained one conversion each, in the right place.
- **`isPETSCII` is renamed `isCBM`** across 15 files. It no longer describes an encoding.
- **Every `media_header`/`media_id` producer** now yields UTF-8, and the literals are lowercase.
  This part is inherently global — `drive.cpp` converts the header line in one place — so it all
  landed in the first commit even for formats whose entry names came later.
- **`MFile::getSourceStream()`'s browsable branch also converted** the name `seekNextEntry()`
  returned; with the tape formats converting at source that became a double conversion. **The CSM
  suite is the only reason this was not shipped** — its `loadByName()` helper mirrors that branch
  deliberately rather than taking a shortcut, so it failed the moment the producer changed. Test
  helpers that duplicate a production path earn their keep exactly here.
- **`test_csm_read`'s expectations pinned the old internal representation** and were updated: the
  synthesized images still hold uppercase PETSCII, which now decodes to lowercase UTF-8. Its `q()`
  query helper already converted the way the drive does and needed no change.
- **Not converted, deliberately**: the status channel, `exec`, fuji and network paths, which convert
  at their own edges; and `EFCRTMFile::getNextFileInDir()`, which is declared in `easyfs.h` with no
  definition anywhere, so EasyFS has no working listing to convert.
- **The `[DIR]` extra-INFO line is FIXED as a side effect, and a hardware comparison will show it
  changing.** `iecChannelHandlerDir`'s constructor already ran `toPETSCII2` over all six values it
  composes those lines from (`drive.cpp:446-451`), `inImage = m_dir->pathInStream` among them. That
  was a DOUBLE conversion while `pathInStream` held raw PETSCII — raw PETSCII read as UTF-8 maps
  into the shifted `$C1-$DA` range, i.e. graphics characters — so a subdirectory listing inside a
  container announced itself in garbage. Now `pathInStream` is UTF-8 and the conversion is the only
  one. The line's TITLES (`"NFO ["`, `"PATH"`, `"IMAGE"`, `"DIR"`) are raw uppercase ASCII and stay
  that way: they are never converted, and raw uppercase ASCII already IS unshifted PETSCII capitals.
  Every one of those six values is UTF-8 by construction — `media_archive`/`media_image` are only
  ever assigned from an MFile `name` or propagated from `sourceFile`, never from raw media bytes.

Builds on `lolin-d32-pro` (Flash 42.5%) and `esp32-s3-devkitc-1` (Flash 82.0%). Full native suite
17 pass; `test_EdUrlParser`, `test_strings` and `test_hdd_read` error identically at baseline
(verified by stashing). **None of it is hardware-tested** — the console `ls` path and the whole C64
listing path need a real device, and `lib/console` is not compiled natively.

### ARC/SDA archives, NIB/NB2/NBZ, G71, G81, P81 and G64 read fixes

New read-only filesystem `arcFS` (`lib/meatloaf/media/archive/arc.h/.cpp`) for `.arc` and `.sda`,
ported from cbmconvert 2.1.2's `unarc.c`. ARC is six formats in one — stored, run-length packed,
Huffman squeezed, LZW crunched, squeezed+packed, and crunched-in-one-pass with its size and
checksum at the END of the stream — and run-length decoding layers on top of the byte producer for
all but two of them. The directory walk never touches compressed data (the next header is exactly
`blocks * 254` bytes on), and only `seekPath()` decompresses, doing the whole entry at once into
RAM. `test/native/test_arc_read/` verifies 84 entries across 9 real archives plus an SDA against
the checksum each entry carries over its DECOMPRESSED bytes; two entries in one archive are
genuinely damaged and are named in the test rather than waved through.

**`EXTRA_DISK_FORMATS` gates the newer formats.** `.arc`, `.g71`, `.g81` and `.p81` together put a
plain ESP32 about 5 KB over its ~3.3 MB `iram0_2_seg` flash-text window, so they are compiled but
only registered when that flag is defined — set for `esp32-s3-devkitc-1`, not for `lolin-d32-pro`.
`.p64`, `.nib`/`.nb2`/`.nbz` and `.g64` are always registered. Note `#pragma GCC optimize("Os")`
made the segment BIGGER here and is not the lever it is for the C components; see
`lib/meatloaf/AGENTS.md`.

### NIB/NB2/NBZ, G71, G81, P81 and G64 read fixes

`nib.cpp` was a near-copy of the old `g64.cpp` and had every defect that one did — including
`readContainer()` indexing its sector buffer by `_position` — plus an unbounded track-table search
and a sector scan that read a header before finding any sync. All fixed, the per-sector `printf`s
removed, and the sync scan moved off the container (it read ONE BYTE AT A TIME, which over a network
is thousands of range requests per sector) into a cached track buffer. `.nb2` and `.nbz` were
already routed to it by `handles()` but read as plain `.nib`; both are now supported, identified by
CONTENT rather than extension — a `.nb2`'s per-track stride is derived from the file length so no
format knowledge is needed, and a real `.nbz` (signature one byte in, behind a leading `$05`, with variable-length
compressed tracks) is recognised and refused, since its per-track compression is not
implemented; a gzip-compressed `.nib` is inflated and read. `test/native/test_nib_read/` covers it in 11 cases; reverting the
`readContainer()` fix fails 6 of the 12.

### G71, G81, P81 (1581 flux images) and G64 read fixes

Three new read-only filesystems. `g71FS` (`lib/meatloaf/media/disk/g71.h`) is a `.g64` with a
different signature and a 1571's geometry — nothing else differs, because a 1571 in double-sided
mode is two 1541 surfaces written by the same logic, and track numbering is flat (track N at half
track N*2 for all 70 tracks, confirmed from VICE's own reader rather than assumed).

`g81FS` (`media/disk/g81.h/.cpp`) and `p81FS` (`media/disk/p81.h/.cpp`) are both 1581 images and
both MFM, arriving at the same cell bitstream by different routes — `.g81` reads it out of the
container, `.p81` decodes it from flux pulses inherited from `P64MStream`. The shared half is
`media/disk/mfm.h/.cpp` (sync search, clock/data de-interleave, address marks, CRC-16); the 1581
logical-to-physical mapping is deliberately kept out of it, since each container has its own head
order.

**Verification differs sharply between them, and the difference matters.** `test_p81_read` (10
cases) runs against a REAL 1581 flux image, `.archive/disk/p81/td1581.p81` — exact disk header and
name, both heads of the directory track, a sweep of all 80 cylinders x 40 blocks with the CRC
checked on every one, and every file's block chain walked to exactly the block count its directory
entry claims. `test_g71_read` (6) and `test_g81_read` (8) run against generated fixtures, since no
`.g71` or `.g81` exists anywhere. **G81's container layout is therefore unverified**: no real image,
no VICE support, no reference implementation - only a four-line note in the header comment, which
the fixture generator and the decoder both encode the same reading of. See `lib/meatloaf/AGENTS.md`
for exactly which claims that does and does not cover. The MFM layer underneath is not in that
caveat, being validated by the P81 suite against real media.

All five suites pass — `test_g64_read` 6, `test_g71_read` 6, `test_g81_read` 8, `test_p64_read` 13,
`test_p81_read` 10 — and the firmware builds on `lolin-d32-pro` (Flash 42.5%) and
`esp32-s3-devkitc-1`. **None of the three new filesystems has been hardware-tested.**

Also fixed in `g64.cpp`/`g64.h`, which `.g71` inherits: `readContainer()` returned memory past the
end of its
sector buffer for any file longer than one block (it indexed by `_position`, the position in the
FILE, instead of an offset within the decoded sector); `seekSector()` spun forever on a sector that
is not on the track, on the IEC task; and `readSector()` returned success when the data block id was
wrong, serving the previous sector's bytes. The unconditional `printf`/`util_dump_bytes` on every
sector read are gone, and the header checksum — computed and discarded before — is now checked.
New `test/native/test_g64_read/` (6 cases) covers all of it against a fixture generated from a real
`.d64` by its `host/make_g64.py`, since no `.g64` exists in `.archive`; reverting the first fix
fails 4 of the 6. See `lib/meatloaf/AGENTS.md` for which test reaches which defect — most of them
cannot reach the first one. `readHeader()` also read the container header from wherever the stream
was left AFTER delegating to `D64MStream::readHeader()` rather than from offset 0, so the values it
logged were never the header's; it now reads and validates the signature first, which is what lets
`.g71` be told from `.g64`.

## Recent Changes (August 16, 2026)

### P64 flux-level disk images

New read-only filesystem `p64FS` (`lib/meatloaf/media/disk/p64.h/.cpp`) for `.p64`, built on
`D64MStream` the way `g64` is. A P64 stores magnetic flux transitions rather than sectors, so a
CBM block costs three decodes — range-coded chunk → pulses → GCR bitstream → sector — and the
GCR it produces is not byte-aligned, which is the one substantial difference from G64. Durable
rules are in the Important Notes entry below and, in full, in `lib/meatloaf/AGENTS.md`.

Verified by `test/native/test_p64_read/` (13 cases, all passing) against the real images in
`.archive/p64`: a blank disk's BAM free count and bitmap for all 35 tracks across all four speed
zones, Wheels 4.4a's exact disk name, id, eight directory entries and two file block chains, and a
sweep of all 683 sectors of a full disk with header and data checksum checked on every one.
Builds clean on `lolin-d32-pro` (Flash 42.4%) and `esp32-s3-devkitc-1` (Flash 81.6%).

**Hardware-verified on a lolin-d32-pro** (console `ls` and `cp` against images on SD): directory
listing and file extraction both work through the `P64MFile` / ImageBroker path, and a listing
completes in well under two seconds. Not yet driven from a real C64 over IEC.

**The rotation seam was found by that hardware run.** Three sectors of an 87-block file came back
with bad data checksums — reproduced natively afterwards, root-caused to sectors straddling the
image's single-rotation boundary, and fixed by decoding an overlap of the next revolution. See the
Important Notes entry below. The regression test that pins it (`test_every_sector_of_every_track_decodes`)
is the sweep mentioned above; every earlier test passed with those sectors already wrong.

Two IDE64 CFS (`.hdd`) read bugs, both found from one report — `LOAD"*"` returning
a directory listing — and both hardware-verified on a fujiloaf-rev0 against real
images on SD.

- **Every byte of every file inside a `.hdd` was transposed** (`media/hd/hdd.h/.cpp`): CFS stores file data as four interleaved 128-byte columns per sector and `readFile()` read it linearly. New `loadDataSector()` reads and caches the whole sector (a sector must be read entire even for a few bytes) and `readFile()` de-interleaves out of it. See the Important Notes entry above for the layout, why only file data is affected, and the three independent confirmations — do not undo it. Symptom to recognise: correct file SIZE and correct transfer, content garbage; a PRG that LOADs and LISTs empty, a text file that comes back as readable fragments at stride 4.
- **The tree walk was NOT at fault and should not be suspected first.** Instrumenting `dataSectorForPos()`/`readFile()` showed a 939-byte file resolving to exactly two data pointers with consecutive LBAs and contiguous 512-byte reads at the right offsets — right sectors, right offsets, wrong byte order. That trace is what turned a wide search into a one-line permutation; add it back temporarily if a CFS read ever looks wrong again.
- **The `S`cratch command threw away its PATH** (`iecDrive::executeData()`): it kept only `command.substr(colon_position + 1)`, so `S/TEMPORARY/:*` matched the pattern against the CURRENT directory — C64 OS clearing its temp directory instead matched `os/BOOTER`. Nothing was lost only because `MFile::remove()` refuses an entry with a non-empty `pathInStream`; on a writable image it would have deleted the wrong files. Now the part between the command letter and the colon is resolved as a directory (`m_cwd->cd(dirPath)`), with the same CBM conventions as `open()`: a `0` drive prefix is dropped, one leading `/` descends from the current directory, `//` starts at the image root. Note `S` IS a command and this is not a truncated filename: `IFD_EXEC` is set only when `m_channel == 15` (`IECFileDevice.cpp:510`), and the logged length matches the string exactly.
- **CFS free blocks come from the usage bitmaps** (`HDDMStream::countFreeBlocks()`): a partition is laid out in repeating groups of 4096 sectors — a bitmap sector, its backup, then 4094 data sectors — and one bitmap sector describes its whole group (byte 0 carries 6 bits, bytes 1-511 carry 8 each). **A SET bit means FREE.** The spec does not give the bit order within a byte, and it does not matter: unused and out-of-partition bits must be recorded as used (0), so a plain popcount of all 512 bytes is correct either way. Two things keep this affordable: the result is cached per partition (CFS is read-only here), and the walk **stops once the count passes 65535**, because the CBM "BLOCKS FREE" line is 16-bit — without it a 1 GB partition costs ~500 seek+read round trips on SD, about 25 s on the first listing. Verified against the 8 MB `ide20201227.hdd`: 4 groups covering exactly 16,384 sectors, 13,332 free.
- **A subdirectory listing announced the wrong directory**: `dir_label` was only ever written when the directory WALK found a label entry, so a subdirectory without one — an empty directory never has one — kept the parent's or the partition's label, and a listing of `os/temporary` claimed to be `os`. `enterDirectory()` now names the directory it just entered; a real label entry found during the walk still wins.
- **The directory listing shows `pathInStream` as a `[DIR]` NFO line** after `[IMAGE]` (`iecChannelHandlerDir`): `path` is the CONTAINER's path and never showed where inside the image the listing came from, so every subdirectory looked like the image root.
- **An empty command channel write crashed the drive** (`iecDrive::executeData()`): the C64 sends a lone `0x00` on the command channel — C64 OS does it on every boot — which arrives as an empty string, and `util_tokenize("", ':')` returns NO tokens, so the `pt[0] == "auth"` test read a `std::string` at address 0 (`LoadProhibited`, EXCVADDR 4). Guarded with `if (pt.empty()) return;`, which leaves the status the command dispatch already set rather than answering `31 SYNTAX ERROR` to what is effectively a no-op. **Four more instances of the same defect** were in the same function — `util_tokenize_uint8()` results indexed `pti[0..3]` unguarded in `B-P`, `B-R`, `B-W`/`U1` and the position command — all reachable from any C64 program that sends a truncated command; they now check `size()` and answer `30 SYNTAX ERROR`.
- **`listen()`/`unlisten()`/`talk()`/`untalk()` must not print** (`iecClock::set_timestamp_format()`, reached from `unlisten()`): `IECDevice.h` requires them to return within 1 ms and the bus handler calls them with interrupts disabled. **With `ENABLE_CONSOLE`, `Debug_printf` AND `Debug_printv` both expand to `console.printf`**, whose `fwrite` takes a newlib lock — and `lock_acquire_generic()` calls `abort()` outright when it cannot yield, rebooting the ESP32 every time the C64 addressed the clock device. Note this makes the "use `Debug_printv` in hot paths" advice elsewhere in this file WRONG for interrupt-disabled context: neither macro is safe there, only no print at all. `iecClock::task()` keeps its prints because `task()` runs in normal task context, which is exactly why `talk()` defers its work to it.
- **CBM DOS path syntax `dir/:file` is normalised in TWO places, and both are needed.** `PeoplesUrlParser::processPath()` drops the colon from `path` (keeping the name as the last component, the shape the no-colon case already had), and `MFSOwner::File()` strips `/:` from the incoming string BEFORE it splits it — because `pathInStream` is built by joining those raw components and never passes through the parser, so a colon left there survives into the in-image path and matches no entry. Only the portion before `?`/`#` is touched. Related: **not every colon is a scheme** — `PeoplesUrlParser::hasScheme()` (new, public static) applies RFC 3986's `ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )`, which a path can never satisfy since `/` is excluded. `resetURL()` and `MFile::cd()` both use it; before, `/settings/:components.t` parsed as scheme `/settings/` and rebuilt as `/settings/:/components.t`, and `cd()` treated it as "cd into another url scheme" and discarded the current directory.
- **A leading `/` on a name from the C64 is RELATIVE to the current directory** (CMD DOS: `/DIR/:FILE` descends from here, `//DIR/:FILE` starts at the image root). `iecDrive::open()` strips that single leading slash alongside the existing `@`, `:`, `0:` and `CD` handling — **keyed on the presence of `/:`**, because Meatloaf also accepts real host paths from the C64 and `LOAD"/sd/games/x.d64",8` must still resolve at the SD root. A bare `CD/DIR/` with no filename carries no `/:` and is therefore still read as host-absolute (open).
- **`iecDrive::read()` must not back out of a container on FILE NOT FOUND**: it called `set_cwd(m_cwd->base())` when a read followed a failed open ("Subdir Change Directory Here!"), and `base()` knows nothing of `pathInStream`, so it returned the image's PARENT DIRECTORY. C64 OS probes for an optional `/TEMPORARY/:UPDATER`; that miss silently moved the drive from inside `ide64CF1GB.hdd` to `/sd/content/hd/hdd/` and every subsequent open failed. Now skipped when the cwd is container-hosted; plain paths keep the old behaviour.
- **`mount <id> <path>` appended an absolute argument to the cwd** (`VFSCommands.cpp`), building `"/" + "/sd/x.hdd"` = `"///sd/x.hdd"`. It only resolved by accident when the drive was not already in an image; once it was, `cd()`'s `//` branch appended the whole absolute path to the image. `mount` now uses an argument that is already absolute as-is, and `cdLocalRoot()` resolves an absolute `plus` directly rather than appending it to anything.
- **`CD//DIR` inside a container image resolved against the image's PARENT directory** (`MFile::cdLocalRoot()`, `meatloaf.cpp`): `//` means "the root of the stream", which for a container-hosted file is the IMAGE — but the code used `sourceFile->path`, and for a container the source file is the image's *containing directory* (source url `/sd/content/hd/hdd` + pathInStream `ide64CF1GB.hdd`). So `CD//OS` in `ide64CF1GB.hdd` produced `/sd/content/hd/hdd/os` and answered `62,FILE NOT FOUND` — which is what stopped C64 OS booting (its boot sequence is `CP1` then `CD//OS` then `LOAD"BOOTER"`). Now: when `sourceFile->pathInStream` is non-empty the file is container-hosted, and `path` (which excludes `pathInStream`) already names the image, so the new component is appended to it. **`sourceFile->pathInStream` non-empty is the test for "hosted by a container"** — `MFSOwner::File()` sets it only on that branch; a plain VFS path takes the `isRootFS` branch and never sets it. Plain and network paths keep the old behaviour exactly.
- **`LOAD"*"` inside a `.hdd` or `.dhd` listed a partition instead of loading a file** (`media/hd/hdd.cpp`, `media/hd/dhd.cpp`): a wildcard-only path component was resolved as a partition NAME. Fixed in both resolvers, plus an all-wildcard file filter in `HDDMStream::seekEntry()`. See the Important Notes entry for the rule and for the D64-family gap that is still open.
- **Not covered by the native suite**: `pio test -e native -f native/test_hdd_read` does not build on macOS (`unity.h` and `include/utils.h` are off the include path, pre-existing). Both fixes are compile-verified plus hardware-verified; neither has a regression test.

### `use` / `exec` console commands

New: `use [device id]` selects the device the console drives (0 clears it, no argument reports it), and `exec {DOS command}` sends a command channel string to it and prints the resulting status. See the Important Notes entries above for the durable rules — device resolution, the cwd-follow hook, PETSCII encoding, `0xNN` binary escapes, and hex rendering of binary status.

- **The device follows the console cwd from `setCurrentPath()`, not from `cd`.** Putting the hook in the `cd` command would have missed `partition` and everything else that moves the working directory. The url is captured BEFORE the new path is published, since once `currentPath` owns it another task could replace and delete it; the sync itself runs outside `s_path_mutex` because it can do network work.
- **`exec` refuses while the drive has channels open.** The IEC task owns the drive then, and `executeData()` from the console task would race it. This is a guard, not a fix — no other console command that mutates drive state (`mount`, `partition`) has one.
- **New public wrappers on `iecDrive`**: `consoleSetCwd()` and `consoleExecDos()`. `set_cwd()`, `executeData()` and `getStatus()` are all protected and stay that way; these are the only console-facing surface. `consoleExecDos()` calls `executeData()` VIRTUALLY so device 30 still reaches `iecMeatloaf`'s FujiNet handling, and samples `hasError()` BEFORE consuming the status — `getStatusData()` resets the code to OK exactly as a channel-15 read does, so sampling afterwards always reads false.
- **Registered in `registerIECCommands()`**, and the getters live in `namespace ESP32Console::Commands` while the two helpers the console core calls (`iecSelectedDeviceId()`, `iecSyncSelectedDeviceCwd()`) live in `namespace ESP32Console`. `IECCommands.h` declares both namespaces; putting a getter in the wrong one links cleanly against nothing and fails only at final link.
- **Verified**: builds on `lolin-d32-pro` and `esp32-s3-devkitc-1`. NOT hardware-tested against a C64. The GPIB copy of `peekStatus()` is compile-unverified.

### A network session was disposed mid-transfer

`wget` of a large file over HTTPS had its session disconnected while the request was still in flight. Root cause and the durable rules are in `lib/meatloaf/AGENTS.md`; in short, `HTTPMStream::open()` acquired the session's I/O refcount only AFTER `client.GET()` returned, leaving the longest I/O in the operation (TLS handshake + redirect chain) unprotected against `SessionBroker::service()`'s 1 Hz sweep. Fixed by holding it across the request. Separately, `HTTPMSession`'s own `key` member was spelled `https://host:443` while the repo filed it under `http://host:443`, which made every log line about the session name a key no lookup could find; now aligned. Neither is hardware-verified — this path cannot be tested on the host (`MeatHttpClient` needs `esp_http_client`, and `lib/console` is not compiled natively).

## Recent Changes (August 12, 2026)

Every archive failure over HTTPS reported this day — and several that had been
tolerated for a while — came from ONE upstream defect. It is described first
because recognising its signature is what saves the next investigation; the
genuine Meatloaf bugs found while chasing it follow.

### Root cause: esp-idf#18359

- **`esp_http_client_prepare()` did not reset the response buffer**, so `raw_data` stayed wherever the PREVIOUS response stopped and any re-request on the same handle parsed the previous response's leftovers as its own. Fixed by **esp-idf PR #18359 / IDFGH-17389**: `esp_http_client_cached_buf_cleanup(client->response->buffer)` + `raw_len = 0` in `esp_http_client_prepare()`. **Hardware-verified 2026-08-12** on lolin-d32-pro: `unzipx` of both a multi-entry `.zip` and a single-file `.gz` over HTTPS now succeed on the first attempt with correct sizes and names.
- **Its signature, so it is recognised faster next time.** All of these were the same bug:
  - read buffers handed back **unwritten** — ESP-IDF heap canaries (`0xBAAD5678`) or stray ASCII (`.com`, `core`) where file content belonged;
  - libarchive finding **no format** and falling through to its `raw` reader;
  - a **compression filter lost on re-open** (`filter count: 1 / none`), so a byte count measured the compressed stream — a 174848-byte D64 reported as its 52223-byte compressed length;
  - `LoadProhibited` / **NULL-pointer `memcpy` inside `esp_http_client_read()`**;
  - **"fails the first time, works the second"** — the second attempt only recovered because its re-request failed and the error path called `init()`, which rebuilds the handle.
- **Applied by `patch_framework.py`** (new; wired into `[esp32_base]` as `pre:`). The framework lives in `~/.platformio/packages/`, OUTSIDE this repo — a hand-edit there is invisible to git, lost on package reinstall or update, and absent for every other checkout. The script is idempotent (marker `MEATLOAF-PATCH esp-idf#18359`) and refuses to patch a file that does not match the expected shape rather than guessing. **DELETE it once the framework package ships the fix.**
- **Two build traps it exposes.** Touching a framework file makes PlatformIO invalidate every environment's build tree; on Windows that cleanup can fail half-way and leave CMake reporting *"the C compiler is not able to compile a simple test program"* — `rm -rf .pio/build` and rebuild. The first build after that wipe can also fail to link with `cannot find .../libtapclean.a`, the archive still being written when `ld` runs; simply build again.

### Archive-layer bugs (independent of the above)

- **libarchive's `raw` reader must never compete with a real container format** (`media/archive/archive.cpp`): it bids 1 on ANY byte stream and synthesises one entry named `data` spanning the whole input, so every format-detection failure became a silent success with fabricated content — `unzipx` of a `.zip` reported "extracted 1 entries, 303509 bytes" and wrote a copy of the archive named `data`. It is now registered only for unknown/ambiguous extensions, where a single compressed file legitimately has no directory. A container that cannot be read now FAILS the open, which is what made every bug above visible.
- **`Archive::open()` reports the first bytes it was handed** when no format bids, with the source's size and position. That line identified the root cause twice and disproved a theory twice; keep it. `cb_read()` captures them at no extra I/O cost.
- **The gzip FNAME header is preferred over the URL** (`Archive::gzipNameFromHeader()`): a `.gz` stores the ORIGINAL filename, which the URL cannot reproduce — zimmers' `ordeal%2b2100p.d64.gz` is `ordeal +2 100% (ntsc pal) wanderer.d64` inside. libarchive exposes FNAME as the entry pathname but ONLY when a format reader yields an entry; a compressed-only file returns `ARCHIVE_EOF`, so it is parsed straight from the header (RFC 1952) out of the bytes `cb_read` already captured — no extra read, no extra seek. A name running past the captured bytes is rejected rather than truncated, and any directory part is stripped.
- **URL percent-encoding must not reach a filename** (`compressedEntryNameFromUrl()`): `ordeal%2b2100p.d64.gz` extracted as a file literally called `ordeal%2b2100p.d64`. Decoded **only when the path contains `://`** — a LOCAL file genuinely named with a `%` keeps it — and with `alter_pluses = false`, because `+` in a path is a literal plus rather than the form-encoded space it means in a query string (the same call `fnFsHTTP.cpp` makes for HTTP directory listings).
- **An MFile's identity does not survive `getSourceStream()`.** For a single-file compression `ArchiveMFile::getDecodedStream()` ends with `resetURL(base())` — it repoints the MFile at its containing directory so the CWD is right after a `LOAD` — which leaves `name` EMPTY. Two bugs came from reading `name` afterwards: `unzipx` wrote to `<dest>/` (a directory, `fopen` → EACCES), then later wrote the file under the raw URL basename. Either capture the name BEFORE opening, or ask `getDownloadFilename()` after — `ArchiveMFile` now answers that with the entry it resolved (FNAME or decoded URL basename), the same hook `wget` uses for `Content-Disposition`.
- **The size fallback refuses to measure a stream that lost its compression filter** (`seekEntry()`): counting bytes off an archive that re-opened WITHOUT its gzip filter measures the compressed stream and reports it as the decompressed size, so the caller extracts exactly that many bytes — a silently truncated file. Gated on whether the archive HAD a filter before the probe re-opened it, which is NOT the same question as `isRawCompressedEntry`: a gzip stream carrying an FNAME header yields a real pathname, so that flag is false and only the `entry.size == 0` arm reaches the size determination. Gating on it meant the first version of this guard never fired. **Keep this** — it turns a recurrence into a loud failure instead of a truncated file.
- **`MeatHttpClient::seek()` guards, kept as belt-and-braces**: it returns early when already positioned exactly where the caller wants on a live response (`_is_open && pos == _position && !complete()`) instead of draining and re-requesting the same bytes for nothing; and it rebuilds the handle via `init()` when a response cannot be drained (`flush(0)` now returns `esp_http_client_flush_response()`'s result instead of discarding it). `init()` is the right hammer because it RECREATES the handle for a GET — ESP-IDF leaves `first_line_prepared` set after one, so re-opening does not regenerate the request line, which its own comment documents. Both guards are cheap and neither is load-bearing now.
- **The gzip ISIZE trailer probe is enabled for all sources.** It was briefly disabled for network sources on the theory that its seek to EOF-4 corrupted them; it did not — esp-idf#18359 did. Without it a `.gz` served over HTTP has no known size until its content is read, so a listing shows 0.

### Stream contract

- **`MStream::seek(pos, mode)` no longer leaves `_position` lying after a refused seek.** It assigned the target before delegating and never took it back on failure, so every later read was attributed to an offset the stream was not at. Implementations that override only `seek(uint32_t)` set `_position` themselves on success. Note that a subclass declaring only `seek(uint32_t)` HIDES the inherited two-argument form from anything holding a derived pointer; add `using MStream::seek;` if a derived-typed caller needs it.
- **One `MeatHttpClient` is shared per host:port — its `_position` is not yours.** Its `seek()` compares the target against that counter and returns "already there" when they match, so a stale counter turns a rewind into a silent no-op. `HTTPMStream::seek()` hands the client its own offset first, and overrides the two-argument seek so the base class cannot clobber `_position` before the one-argument seek runs.

### Memory and build size

- **`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` is 128 on all 21 PSRAM boards** (was 512; `esp32-s3-rgb` was on the IDF default 1024; `freenove-esp32-cam` was already 128). At 512 it captured exactly what a file-heavy workload is made of — the FATFS per-file sector cache (512 B), the LFN working buffer (~512 B at `CONFIG_FATFS_MAX_LFN=255`), newlib `FILE`s and most `MFile`/`std::string` traffic, roughly 1.5 KB of internal DRAM per open file. Free internal fell below 2 KB, SD DMA allocations failed (`sdmmc_read_sectors: not enough mem` — that bounce buffer has no PSRAM fallback) and `fopen()` aborted in `lock_init_generic`. Measured during `unzipx`: 1.5 KB → 11.3 KB free internal, SD errors gone. mbedtls was NOT the culprit; `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` already puts TLS buffers in PSRAM. **Hardware-verified on lolin-d32-pro only**; the other 20 are propagated by inference — 512 is the one-line revert. The 7 non-PSRAM configs do not have the key and must not get it. `CONFIG_FATFS_MAX_LFN` 255 → 128 is the next lever if more is needed.
- **libarchive compiles `-Os`** (`components/libarchive/config.h`, `#pragma GCC optimize` guarded by `ESP_PLATFORM && __GNUC__`) — the third use of this pattern after `sqlite3` and `tapclean`, and for the same reason: the PlatformIO ESP-IDF builder silently strips per-component `-O` compile options. Every libarchive source includes `archive_platform.h`, which includes `config.h` first, so one pragma reaches all of them. It was the largest component still at the project's global `-Og` (523 KB of text). **Recovered 63 KB of flash text** plus ~4 KB static RAM, which took `fujiloaf-rev0` from 241 bytes over the `iram0_2_seg` limit to linking with real headroom, and made every board ~33-35 KB smaller.
- **`unzipx` no longer re-walks every path prefix per entry.** Each `MFSOwner::File()` probes for a `.config` at every level above it, and that cache holds 64 entries and CLEARS WHOLESALE on overflow — which a deep archive triggers, so the probes fell through to real `fopen()`s on SD. Remembering the previous entry's directory cut ~450 config probes to ~50 for a 30-entry archive. (A `std::set` of every created segment would save marginally more but costs ~1 KB of flash text — enough to push `fujiloaf-rev0` over its limit.)
- **Board build survey.** ESP32-family PSRAM boards carry the small ~3.3 MB `iram0_2_seg` window and are the exposed set; the S3s have room (`esp32-s3-devkitc-1` links at Flash 81.7%). Building: `lolin-d32-pro`, `iec-nugget`, `fujiloaf-rev0`, `esp32-s3-devkitc-1`. Broken **before** this work and untouched: `fujiapple-rev0` (does not define `ENABLE_CONSOLE`, but `CoreCommands.cpp` calls `console.requestExit()` outside the `#ifdef`) and `freenove-esp32-cam` (4,351,704-byte app against a 2,949,120-byte partition). `build_type = debug` in `platformio.ini` defines `DEBUG` for every board, so all `Debug_printv` text is compiled in — a large part of why these images have no margin.

### Tests

- **`test/native/test_archive_extract/`** (new) makes the archive layer testable off-device: `host/build_libarchive.py` builds libarchive + zlib + bzip2 + lz4 + the `lib/compat` pieces mingw lacks for the host and links them, and `host/config.h` defers to the real `components/libarchive/config.h` while switching off only what the host cannot provide, so the registered format and filter set matches the firmware. Covers format selection, the raw fallback, gzip FNAME parsing, URL decoding (both directions), the trailer probe, and the resolved download filename. `meat_session.h`/`.cpp` gained `#ifndef TEST_NATIVE` guards on their `device/iec` and `fnFsSD.h` includes, the same pattern `meat_media.h` already used. See that directory's README.
- **`test/native/test_mstream_seek/`** (new) pins the `MStream::seek(pos, mode)` contract.
- **Artifacts are removed in `tearDown()`, never inline.** On Windows `remove()` fails while the stream still holds the file open — and test locals live until the body returns — and a failed assertion skips any cleanup below it. `test_container_entries` already did this; copy it.
- **`SessionBroker` state outlives a single test.** `seekPath()` caches the resolved entry in an `ArchiveMSession` keyed on the container URL, so two tests sharing a URL have the second read the first's cached entry. Give them distinct URLs.
- **What the native suite cannot reach**: `lib/console` is not compiled there (so `unzipx` itself is untested), and `MeatHttpClient` needs `esp_http_client` (so every HTTP fix in this section is reasoned and hardware-checked, not unit-tested).

## Recent Changes (August 11, 2026)

- **`unzipx` over HTTP "extracted" one file called `data` that was a copy of the archive** (`lib/meatloaf/media/archive/archive.cpp/.h`): `Archive::open()` registered `archive_read_support_format_raw()` alongside the format chosen by extension. raw bids 1 on ANY byte stream and synthesizes a single entry named `data` spanning the whole input, so whenever the real format's bidder declined, raw won and the caller SUCCEEDED with fabricated content — `unzipx https://…/Donnie_Russell_II_d64.zip` reported "extracted 1 entries, 303509 bytes" (the size of the .zip). raw is now registered only in the unknown/ambiguous-extension branch, where it is the right answer for a single compressed file (`.gz`/`.bz2`) that has no directory. A container that cannot be read now fails the open, and `unzipx` reports `cannot read archive`.
- **Root cause is NOT in the archive layer and is still open.** The same zip walks correctly from a local file (30 entries); reproduced on the host only by making the source stream serve bytes from a non-zero offset, which yields exactly the one-`data`-entry symptom. So the HTTP source hands libarchive bytes that are not the archive's first bytes at format-bid time — while extraction afterwards reads from 0 (the observed byte count is the file's exact size). `MeatHttpClient::seek()`/`read()`/`openAndFetchHeaders()` were traced end to end without pinning it. `cb_read()` now records the first 16 bytes it hands over and the failing open logs them with the source size/position, so the next occurrence names the offset instead of implying it.
- **`lib/meatloaf` archive code is now testable on the host** (`test/native/test_archive_extract/`, new): `pio test -e native -f native/test_archive_extract`. Needed libarchive built for the host — `host/build_libarchive.py`, wired into `[env:native]` as an `extra_script`, builds libarchive + zlib + bzip2 + lz4 + the `lib/compat` pieces mingw lacks and links them; `host/config.h` defers to the real `components/libarchive/config.h` and switches off only what the host cannot provide, so the registered format/filter set matches the firmware. `meat_session.h`/`.cpp` gained `#ifndef TEST_NATIVE` guards on their `device/iec` and `fnFsSD.h` includes, the same pattern `meat_media.h` already used. See that directory's README.
- **Pre-existing, untouched**: `pio test -e native` also errors in `test_EdUrlParser` and `test_strings` — repo-root-relative includes (`"lib/utils/U8Char.h"`) with no matching `-I`. Verified present before this work.

## Recent Changes (August 10, 2026)

- **`rm` deleted the whole disk image instead of the file inside it** (`lib/console/Commands/VFSCommands.cpp`) — `rm fb` inside `hdbackup.dhd` unlinked `hdbackup.dhd` from the SD card. `rm` resolved its argument to an `MFile` and then passed `target->url` to `rm_path()`; for anything inside a container that is the CONTAINER's path, so the string handed on named the image, not the file, and `rm_path()` (which re-resolves from a string) can only act on what the string carries. Not a regression from the new `D64MFile::remove()` — the base `MFile::remove()` would have unlinked the container identically; in-image deletion only made the surrounding bug reachable. Three more sites had the same mistake and are fixed the same way: `rm`'s wildcard branch (`cwd->url + name`), `resolve_path()` (so `cp`/`mv`/`unzipx`/`wget`/`gzip` resolved relative names against the container root instead of the current subdirectory inside it), and `wget`'s destination / `mount`'s default filename. See the `fullUrl()` note under Important Notes. NOT covered by the native suite — `lib/console` is not compiled there.
- **File/directory commands work inside disk images** (`VFSCommands.cpp`): `cp` and `mv` were POSIX (`fopen`/`rename`) and could only see real files on flash and SD; both now go through `MFile`, as `rm`/`mkdir`/`rmdir` already did. `cp` and `mv` share `copy_via_mfile()`, which takes the destination basename from the SOURCE PATH — `srcFile->name` is the CONTAINER's name for an in-image path, so `cp bible/read.me .` wrote a file called `hdbackup.dhd`. `mv` is `rename()` within one directory and copy-then-delete across directories, unlinking the source only after the copy fully succeeds.
- **`ls` hides nameless and hidden entries** (`VFSCommands.cpp`): entries with an empty name are never listed (deleted D64 slots surface as blank rows), and `is_hidden` entries are listed only with the new `-a` flag. The type column shows `d`/`h`/`-`.
- **Delete and unscratch inside D64-family images** (`lib/meatloaf/media/disk/d64.h/cpp`): `removeFile()`/`unremoveFile()` on the stream, `D64MFile::remove()` routing an in-image path to them and disposing the ImageBroker entry afterwards so later listings re-read the directory. Scratch/unscratch follow CBM DOS — see the Important Notes entry. The native suite grew 30 cases (5 scenarios × 6 formats) covering BAM restoration, directory-entry state, recovery of file CONTENTS byte-for-byte, and refusal when the blocks have been reused.

## Recent Changes (August 9, 2026)

- **`updatedb` could not build its index on SD; three separate causes, all internal-DRAM pressure.** The SDMMC driver needs a 512-byte `MALLOC_CAP_DMA` bounce buffer for **every sector** it transfers (`sdmmc_cmd.c`, `sdmmc_write_sectors` — one sector at a time, not the whole write), and that allocation has no PSRAM fallback. Anything that drains internal DRAM therefore breaks SD I/O. (1) **`CONFIG_WL_SECTOR_SIZE` was 4096** — FatFs sizes its buffers by `FF_MAX_SS = MAX(FF_SS_SDCARD, FF_SS_WL)`, `FF_SS_SDCARD` is hardcoded 512, and this project never uses wear levelling (LittleFS on flash, no FAT partition anywhere), so a setting with no effect on storage was making every FatFs buffer 8x larger than an SD card needs — and with `CONFIG_FATFS_PER_FILE_CACHE=y` that is a per-OPEN-FILE cost. Now 512 on all boards. Note this compiles out `FATFS::ssize`, so read the sector size through `fatfs_sector_size()` in `fnFsSD.h`, never the member. (2) **The scan did not use SQLite's PSRAM allocator.** FTS5's token hash is thousands of sub-512-byte allocations and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=512` puts every one in internal DRAM; `updatedb_fts_rebuild()` already guarded against this with `sqlite3_esp32_psram_malloc_enter()` but the scan task did not. (3) **`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` was 0 or 512**, now 32768 — see the Important Note above for the measurement.
- **`updatedb` runs on the console executor, not its own task** (`lib/console/Commands/VFSCommands.cpp`, `lib/console/Console.cpp`): console commands already execute on `console_exec`'s 16 KB internal stack, so spawning a second 8 KB task claimed another big internal-DRAM block ON DEMAND — and that is what left the lazily-started web server unable to find a contiguous stack (`ESP_ERR_HTTPD_TASK`, `free_internal=17116` but `largest_internal_block=8180`: fragmentation, not exhaustion). The console now blocks for the scan's duration, so **`updatedb stop` is intercepted shell-side** in both `repl_task()` and `execute()` via `updatedb_request_stop()`, the same pattern `exit`/`reboot` use — otherwise the cancel would queue behind the scan it cancels. `format_sd` keeps its own task.
- **`ls` in a non-existent directory crashed** (`lib/meatloaf/device/flash.h/.cpp`): `FlashMFile::dir` was uninitialised and `openDir()` returns early without assigning it when the path is not a directory, so `rewindDirectory()`'s NULL guard passed on garbage and `rewinddir()` faulted. Separately `MFile::cdLocalRoot()` mutated `this`, so a `cd` the console REJECTED still moved the working directory — which is how a shell reached a non-existent directory in the first place.
- **`unzipx` recreates the archive's directory structure; `-j` flattens it** (`lib/meatloaf/media/archive/archive.h/.cpp`, `lib/console/Commands/VFSCommands.cpp`): `unzip_write_entry()` already created parent directories, but the code was dead because `nextEntrySimple()` assigned `basename(pathname)`. `Entry` now carries both `filename` (basename, for flat CBM listings) and `pathname` (as stored). Entry paths are sanitised against zip-slip — `unzipx` extracts from `http://` and `smb://` as readily as from local files.

## Recent Changes (August 8, 2026)

- **Partitions can be referenced by path again — without changing the selection** (`lib/meatloaf/media/hd/dhd.h/.cpp`, `lib/meatloaf/media/disk/d64.h/.cpp`): the 2026-08-07 work removed path-based partition references entirely, which fixed a mid-listing partition switch but also removed any way to LOAD/SAVE across partitions. A leading in-image path component naming a partition now binds only that path, via the shared `DHDResolvePartition()`; it never calls `select()`. `D64MFile` gained two overridable virtuals a container subclass needs — `entryUrlFor()` (emitted entry URLs, so a listing of a non-selected partition names its own partition) and `brokerUrl()` (the URL `ImageBroker::obtain()` rebuilds from) — and `DHDImageRegistry::Image::cached_part` records which partition the cached stream decodes so `normalizePath()` can dispose it on a mismatch. **Both mechanisms are required**; an earlier attempt put the partition on the MFile alone, where only `getDecodedStream()` saw it, and listing a non-selected partition silently showed the selected one. `D64MStream::partition` is NOT the CMD partition — it is the sub-partition within a decoded disk, and geometry now reads through `curPartition()`.
- **`ls` in a non-existent directory crashed; a failed `cd` moved the cwd** (`lib/meatloaf/device/flash.h/.cpp`, `lib/meatloaf/meatloaf.cpp`): `FlashMFile::dir` was uninitialised and `openDir()` returns early without assigning it when the path is not a directory, so `rewindDirectory()`'s NULL guard passed on garbage and `rewinddir()` faulted. `openDir()`/`closeDir()` now clear the handle as well as the flag. Separately `MFile::cdLocalRoot()` mutated `this` — it assigned the member `path` and called `rebuildUrl()` — so a `cd` the console *rejected* still moved the working directory, which is how a shell ended up in a directory that then crashed `ls`.

## Recent Changes (August 7, 2026)

- **`partition` console command; path-based partition selection removed** (`lib/console/Commands/VFSCommands.cpp/.h`, `lib/console/Console.cpp`, `lib/meatloaf/media/hd/dhd.h/.cpp`, `lib/device/iec/drive.cpp`): `partition` lists a CMD HD/FD image's partitions (selected one marked) and switches by number or name/wildcard, resetting the console cwd to the image root. `DHDPartitionMFile::normalizePath()` no longer treats a leading path component as a partition reference — the real CMD HD requires `CP<n>`, and the implicit form switched partitions part-way through a directory listing because selecting strips the partition from the path, leaving listing-generated entry URLs ambiguous with partition references. A CMD HD holds a **maximum of 254 partitions** (1-254); table entry 0 is the system partition. `maxpart` was briefly raised to 255 during this work on the belief that entry 255 was an unreachable user partition — that was wrong and has been reverted: `vdrive.c:1180` caps at 254 and `vdrive.c:1201` remaps physical entry 0 onto *logical* 255, so there is no 255th user slot and reading one goes past the table. Both `maxpart` and the parse loop counter must still stay `uint16_t` — as `uint8_t`, an `i <= 255` bound would never be false. The system partition IS shown in both listings (`sys` / `SYS`) but `select()` refuses it.
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

All items below were re-landed **one at a time with a hardware test between each** after a full revert to the pre-optimization baseline; the July 2–3 iterations that led here (lazy REPL watcher, 16 KB per-connect session task, 307-redirect handover, TCP window caps, 32 KB SPIRAM reserve, 16 KB httpd stack) are superseded and their sdkconfig experiments reverted (`TCP_WND`/`TCP_SND_BUF` back to 65534, `SPIRAM_MALLOC_RESERVE_INTERNAL` back to 512). **The reserve was reinstated at 32768 on 2026-08-09** after it was measured to be the difference between a working and a failing SD write path — see the August 9 entry; that part of this revert no longer holds.

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
