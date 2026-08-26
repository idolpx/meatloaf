# Building the Meatloaf firmware

**You almost certainly do not need this page.**

The [web flasher at flash.meatloaf.cc](https://flash.meatloaf.cc) installs a current, tested build
onto your device from a browser in about a minute. No toolchain, no clone, no compiler.

Build from source when you want to:

* bring up a dev board that has no environment in `platformio.ini.sample` yet
* change which features are compiled in (see [Feature gates](#feature-gates) below)
* work on Meatloaf itself

---

## Prerequisites

* [Visual Studio Code](https://code.visualstudio.com/)
* The [PlatformIO IDE](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)
  extension, installed from the VS Code marketplace

PlatformIO downloads the ESP-IDF toolchain on the first build. Expect that first build to take a
while.

---

## 1. Set up the project

1. Clone this repository.
2. Copy `platformio.ini.sample` to `platformio.ini`. The sample is the template; `platformio.ini` is
   yours and is git-ignored, so your WiFi credentials and board choice never get committed.
3. Edit `platformio.ini`:
   * Find the `;environment = …` line for the board you have and uncomment it by removing the
     leading semicolon. For a new build that is `esp32-s3-devkitc-1`.
   * Uncomment the `;flash_size = …` line matching your module — `4m`, `8m`, `16m` or `32m`. An
     ESP32-S3-WROOM-1-**N16**R8 is `16m`.
   * Optionally set `wifi_ssid = "…"` and `wifi_pass = "…"`. You can also set these later from BASIC
     with the `OPEN` command — see
     [Using your Meatloaf](https://github.com/idolpx/meatloaf/wiki/Using-Your-Meatloaf#connect-to-your-wifi).
   * Set `upload_port` and `monitor_port` in the `[env]` section if PlatformIO does not find your
     device automatically.
   * If your board is not one of the documented ones, review the rest of its settings and consider
     adding a new environment and a pin map under `include/pinmap/`.

## 2. Load the board's project tasks

PlatformIO needs two clicks to populate the task list for a board:

1. Click the PlatformIO alien head in the left panel.
2. Under **PROJECT TASKS**, click your board — for example `esp32-s3-devkitc-1`. The section expands
   briefly with default tasks, then collapses once "PlatformIO: Loading tasks…" disappears.
3. Click your board a second time. It now stays expanded with the correct tasks loaded.

## 3. Upload the filesystem image — once

The filesystem partition holds the Meatloaf Manipulator web app, the WebDAV files, the help files and
the default configuration. It has to be written once before the first firmware upload.

1. Under **PROJECT TASKS**, your board → **Platform** → **Build Filesystem Image**
2. Then **Platform** → **Upload Filesystem Image**

## 4. Build and upload the firmware

Under **PROJECT TASKS**, your board → **General** → **Upload and Monitor**.

Meatloaf should now be running on the device.

---

## Building from the command line

`build.sh` wraps the common PlatformIO invocations:

```bash
./build.sh -cb                        # clean and build
./build.sh -cbum                      # clean, build, upload firmware, monitor
./build.sh -f                         # upload filesystem only
./build.sh -m                         # monitor only
./build.sh -e esp32-s3-devkitc-1 -cb  # build a specific environment
```

Or drive PlatformIO directly:

```bash
pio run                    # build the default environment
pio run -t upload          # upload firmware
pio run -t uploadfs        # upload filesystem
pio run -t clean
pio device monitor -p <port> -b 2000000 --filter esp32_exception_decoder
```

The console runs at **2,000,000 baud**. A CP2102-based board tops out at 1 Mbps — use 921600 there.

---

## Feature gates

Meatloaf has more features than a plain ESP32 can hold. The binding constraint is the flash-text
window (`iram0_2_seg`), which is **3,342,304 bytes** on every non-S3 ESP32 board; the ESP32-S3 and
ESP32-P4 windows are large enough that nothing has to be given up.

Gates are set in the `[env]` block of `platformio.ini` to apply everywhere, or in an `[env:...]`
section for one board. Each one drops a whole library from the link. Figures are `.flash.text` bytes
measured on `fujiloaf-rev0`; all ten together free **1,067,152 bytes**.

| Flag | Frees | What you lose |
|---|---:|---|
| `DISABLE_LOCATEDB` | 406,890 | sqlite3 — the `updatedb` and `locate` commands |
| `DISABLE_SSH` | 139,761 | libssh — `sftp://`, the `N:` SSH protocol, mDNS `_sftp-ssh` discovery |
| `DISABLE_TAPE` | 119,284 | tapclean — `.tap`/`.dmp`/`.htap` and the `T-C`/`T-I` commands |
| `DISABLE_NFS` | 85,287 | libnfs — `nfs://` |
| `DISABLE_WEBDAV_CLIENT` | 78,796 | expat — an `N:` PROPFIND listing returns nothing. **The WebDAV server is unaffected.** |
| `DISABLE_RETROPIXELS` | 75,494 | the `retropixels:` image encoder |
| `DISABLE_ISCSI` | 52,633 | libiscsi — `iscsi://` |
| `DISABLE_AFP` | 48,810 | afpfs-ng — `afp://` |
| `DISABLE_ARCHIVE_7Z` | 15,779 | libarchive's `.7z` reader |
| `DISABLE_ARCHIVE_XAR` | 12,071 | libarchive's `.xar` reader |

The shipped `platformio.ini.sample` sets `DISABLE_RETROPIXELS`, `DISABLE_ISCSI` and
`DISABLE_ARCHIVE_XAR` globally — the three that pay off on every board.

Two flags go the other way and *cost* flash text:

| Flag | Adds |
|---|---|
| `EXTRA_DISK_FORMATS` | registers the `.arc`/`.sda`, `.g71`, `.g81` and `.p81` filesystems |
| `EXTRA_FASTLOADERS` | compiles the software fast-loader transfer code. Detection is always built — without this a recognised loader is logged and declined, and the computer falls back to the standard protocol. Also costs IRAM. |

Both are on by default in the sample.

`MIN_CONFIG` is the big one: it drops the entire archive layer, the tape engine, SMB/SFTP/NFS/AFP,
the flux and CMD/IDE64 image formats and the JSON/QR/hash transforms. The CBM disk images,
HTTP/FTP/TNFS/FSP and the fast loaders all stay. Several `DISABLE_*` gates are no-ops on a
`MIN_CONFIG` board because the feature is already excluded.

Every flag is documented inline in `platformio.ini.sample` — read it there first, it is the
authoritative list.

---

## Notes and traps

* **Adding or removing a source file** invalidates PlatformIO's cached source glob. Delete
  `.pio/build/<env>/CMakeCache.txt`, or the build fails with "Source not found" for a file you
  removed.
* **Changing a build flag or an sdkconfig** invalidates the whole tree — expect a full rebuild.
* **PSRAM matters.** Tape decoding, `.p64`/`.p81` flux decoding, Wraptor extraction and FTPS all
  need it. A board without PSRAM will fail those at runtime, not at link time.
* **Task stacks come from internal DRAM only** — there is no PSRAM fallback. If you add a task, that
  is a hard internal-RAM cost.

For deeper architectural detail, see [AGENTS.md](../AGENTS.md) in the repository root.
