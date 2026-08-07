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

## Overview

This directory (`lib/meatloaf`) contains the core stream abstraction layer that enables Meatloaf's unique capability to transparently access files nested within containers and across network protocols. This is the foundation that allows loading a PRG from a D64 inside a ZIP on an HTTP server with a single URL.

## Core Architecture

### Three-Layer Stream System

The architecture consists of three complementary abstractions:

1. **MStream** (`meatloaf.h`): Base stream interface providing position, size, seek, read/write operations
2. **MFile** (`meatloaf.h`): File/directory abstraction that wraps MStreams with URL parsing and navigation
3. **MFileSystem** (`meatloaf.h`): Factory interface that knows how to create MFile instances for specific protocols/formats

### Stream Hierarchy

**MStream** (base for all streams)
- `url`: Full path to the resource
- `size()`, `available()`, `position()`: Stream state
- `read()`, `write()`, `seek()`: I/O operations
- `isRandomAccess()`, `isBrowsable()`: Capability flags

**MMediaStream** (`meat_media.h`): Extends MStream for container formats
- `containerStream`: The source stream this decoder reads from
- `readHeader()`, `seekEntry()`: Container-specific navigation
- `readFile()`, `writeFile()`: File extraction from container
- Used by disk images (D64, D81), archives (ZIP, TAR), tapes (T64)

### The Russian Doll Pattern

URLs are parsed **right-to-left** to build nested stream hierarchies:

```
http://server/files/game.zip/disk.d64/start.prg
```

Resolution flow:
1. `start.prg` → needs D64 decoder
2. `disk.d64` → D64MStream wrapping source (ZIP)
3. `game.zip` → ArchiveMStream wrapping source (HTTP)
4. `http://` → HTTPMStream (bottom stream)

Result: `FileStream(D64MStream(ArchiveMStream(HTTPMStream)))`

### MFile System

**MFile** responsibilities:
- Parse URL into protocol, host, path components (via PeoplesUrlParser)
- Navigate filesystem hierarchy (`cd()`, `cdParent()`, `cdRoot()`)
- Provide metadata: `size`, `blocks()`, `isDirectory()`, `exists()`
- Create streams: `getSourceStream()`, `getDecodedStream()`, `createStream()`

**Key pattern**: Each MFile has a `sourceFile` pointer creating a linked chain up to the root filesystem.

### MFileSystem Registration

All filesystems register in `meatloaf.cpp`:

```cpp
std::vector<MFileSystem*> MFSOwner::availableFS = {
    &defaultFS,     // Flash/LittleFS (root)
    &httpFS,        // HTTP protocol
    &smbFS,         // SMB/CIFS protocol
    &d64FS,         // D64 disk images
    &archiveFS,     // ZIP/RAR archives
    // ...
};
```

**MFSOwner** is the factory that:
- Scans registered filesystems to find one that `handles()` a given URL
- Calls `getFile()` on the matching filesystem
- Returns the constructed MFile chain

## Directory Structure

```
lib/meatloaf/
├── meatloaf.h/cpp          # Core: MStream, MFile, MFileSystem, MFSOwner
├── meat_media.h/cpp        # MMediaStream base for container formats
├── meat_broker.h           # Stream/file caching (deprecated, uses ImageBroker)
├── meat_buffer.h           # Buffering utilities
├── meat_session.h/cpp      # SessionBroker, MSession, CachedFile (HIMEM-aware)
├── iec_pipe.h              # IEC device communication pipes
├── media/                  # Container format implementations
│   ├── disk/              # Disk images: d64, d71, d80, d81, d82, d90, g64, m2i, nib
│   ├── archive/           # Archives: zip, rar, tar, lbr, ark
│   ├── tape/              # Tapes: tap/dmp/htap (TAPClean-based, see tape_decoder), t64, tcrt
│   ├── cartridge/         # Cartridge: crt
│   ├── file/              # File wrappers: p00
│   ├── container/         # Containers: d8b, dfi
│   └── hd/                # Hard drives: dnp, dhd (CMD HD), hdd (IDE64 CFS), d90
├── network/               # Network protocol bottom streams
│   ├── http.h/cpp        # HTTP/HTTPS
│   ├── smb.h/cpp         # SMB/CIFS (Windows shares)
│   ├── ftp.h/cpp         # FTP
│   ├── tnfs.h/cpp        # TNFS (Trivial Network File System)
│   ├── webdav.h/cpp      # WebDAV
│   ├── tcp.h             # Raw TCP sockets
│   └── ws.h              # WebSockets
├── device/                # Local storage filesystems
│   ├── flash.h/cpp       # Internal flash (LittleFS/SPIFFS)
│   └── sd.h/cpp          # SD card (FAT32)
├── service/               # Special purpose services
│   ├── ml.h/cpp          # ML: protocol (Meatloaf bookmarks)
│   ├── csip.h/cpp        # CSIP protocol
│   └── mdns.h/cpp         # MDNS: Network Service Discovery (mDNS)
├── codec/                 # Encoding/decoding
│   └── qr.h/cpp          # QR code generation
├── hash/                  # Hashing utilities
├── auth/                  # Authentication helpers
└── crypto/                # Encryption utilities
```

## Implementing a New Filesystem

### Bottom Stream (Network Protocol)

Create a class extending `MFileSystem` and `MStream`:

```cpp
class MyProtocolMFileSystem : public MFileSystem {
    bool handles(std::string path) {
        return (mstr::startsWith(path, "myproto://"));
    }
    MFile* getFile(std::string path) {
        return new MyProtocolMFile(path);
    }
};

class MyProtocolMStream : public MStream {
    // Implement: open(), close(), read(), write(), seek()
    // This is a bottom stream - no containerStream
};
```

Register in `meatloaf.cpp`:
```cpp
MyProtocolMFileSystem myprotoFS;
// Add to MFSOwner::availableFS
```

### Decoder Stream (Container Format)

Create a class extending `MMediaStream`:

```cpp
class MyFormatMStream : public MMediaStream {
public:
    MyFormatMStream(std::shared_ptr<MStream> src)
        : MMediaStream(src) {
        readHeader();  // Parse container header
    }

protected:
    bool readHeader() override {
        // Read container metadata from containerStream
    }

    bool seekEntry(std::string filename) override {
        // Find file in container directory
        // Set _position, _size for the file
    }

    uint32_t readFile(uint8_t* buf, uint32_t size) override {
        // Extract and decompress data from containerStream
    }
};
```

Create corresponding MFileSystem:
```cpp
class MyFormatMFileSystem : public MFileSystem {
    bool handles(std::string path) {
        return mstr::endsWith(path, ".myformat");
    }
    MFile* getFile(std::string path) {
        return new MyFormatMFile(path);
    }
};

class MyFormatMFile : public MFile {
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src) {
        return std::make_shared<MyFormatMStream>(src);
    }
};
```

### Key Methods to Implement

**MStream:**
- `open(mode)`: Initialize stream
- `close()`: Cleanup resources
- `read(buf, size)`: Read bytes
- `write(buf, size)`: Write bytes
- `seek(pos)`: Set position
- `isOpen()`: Check if open
- `isRandomAccess()`: Can seek arbitrarily?
- `isBrowsable()`: Has directory structure?

**MFile:**
- `getSourceStream()`: Returns the stream this container reads from (default implementation walks up sourceFile chain)
- `getDecodedStream(src)`: Given a source stream, return decoder
- `isDirectory()`: Check if path is a directory
- `getNextFileInDir()`: Iterate directory entries

**MFileSystem:**
- `handles(path)`: Does this FS handle this URL/extension?
- `getFile(path)`: Create MFile instance

## ImageBroker Pattern

`ImageBroker` (`meat_media.h`) caches opened container streams to avoid re-parsing:

```cpp
auto stream = ImageBroker::obtain<D64MStream>("d64", url);
```

Key: Built from type + sourceFile->url + pathInStream
- Reuses stream if already open
- LRU eviction with max 50 entries (configurable via `max_entries`)
- Entries not in use by any drive are evicted first when at capacity
- Stale entries (not accessed for 60s, configurable via `cleanup_interval_ms`) are automatically removed
- `touch_entry()` updates LRU position on cache hit
- Protected: entries mounted on drives are never evicted during LRU cleanup

## SessionBroker Pattern

`SessionBroker` (`meat_session.h`) manages persistent network connections with keep-alive:

```cpp
auto session = SessionBroker::obtain<TNFSMSession>(host, port);
```

**MSession Base Class:**
- Virtual methods: `connect()`, `disconnect()`, `keep_alive()`
- Activity tracking with `last_activity` timestamp
- Configurable `keep_alive_interval` (default 30 seconds, 0 = disabled)
- `getIdleTime()` to check time since last activity
- `isConnected()` to check connection state
- `acquireIO()` / `releaseIO()` — atomic ref-count to prevent premature cleanup during active I/O
- File cache: `getCachedFile()`, `cacheFile()`, `clearFileCache()` for in-memory data caching

**MSession::CachedFile:**
- Platform-aware cached file storage for random-access read/write
- On ESP32+SPIRAM: Uses HIMEM page-mapped access for large files (beyond 4MB address space)
- On other platforms: Uses heap-allocated `uint8_t*` buffer
- Constructor: `CachedFile(uint32_t size)` (allocates on demand) or `CachedFile(uint8_t* data, uint32_t size)` (takes ownership)
- `allocate()`: Tries HIMEM first, falls back to heap malloc
- `read(offset, buf, count)` / `write(offset, buf, count)`: Transparent HIMEM page mapping
- `loadFromStream(MStream*, fileSize)`: Reads from any MStream into backing store (page-by-page for HIMEM)
- `loadViaReader(fileSize, reader)`: Generic template — fills backing store via any callable `uint32_t reader(uint8_t* buf, uint32_t n)`; used by `loadFromStream` and by `ArchiveMSession::loadEntryFromArchive`
- `isAllocated()`, `dirty` flag for tracking modifications
- Static `s_range` / `s_rangeUsed` shared across all HIMEM users (refcounted)
- Implementation: `meat_session.cpp`

**SessionBroker:**
- Static `session_repo` map storing sessions by key (typically `scheme://host:port`)
- Template `obtain<T>()` method creates or reuses sessions (uses `T::getScheme()` to build key)
- Template `find<T>()` method looks up existing session by key without creating
- `add()` method registers a pre-created session (used when credentials must be set before connect)
- Runs as a FreeRTOS task on CPU0 (same core as WiFi)
  - Priority 5 (lower than IEC bus task priority 17)
  - Checks every 1 second
  - Skips sessions with `keepAliveInterval == 0` (disabled)
  - **Session lifecycle** (in priority order):
    1. NOT in use by any drive or console → **remove immediately**
    2. busy (active I/O) → **skip**
    3. IS in use → try keep-alive; if fails, try reconnect; if reconnect fails, **remove**
  - `is_session_in_use()` checks drives AND console for session ownership
  - `path_matches_session()` compares paths ignoring port numbers (e.g., `tnfs://host:16384` matches `tnfs://host/`)
  - `session_host_key()` extracts `scheme://host` from session key for comparison
  - Removes disconnected/failed sessions automatically
  - **CRITICAL**: Never call `keep_alive()` on a session that may be in active I/O from another task — libnfs/lwIP are not thread-safe and concurrent socket access corrupts the heap
- `setup()` creates the service task (called from `main_setup()`)
- `shutdown()` stops the task and clears all sessions (called from shutdown handler)
- `dispose()`, `count()`, `clear()` utility methods

**Example: TNFSMSession**
```cpp
class TNFSMSession : public MSession {
public:
    TNFSMSession(std::string host, uint16_t port);
    bool connect() override;        // Mounts TNFS server
    void disconnect() override;     // Unmounts TNFS server
    bool keep_alive() override;     // Sends getcwd as keep-alive probe
    tnfsMountInfo* getMountInfo();  // Returns mount for TNFS operations
};
```

**Usage in TNFS:**
- **TNFSMFile** uses SessionBroker for all file and directory operations
- **TNFSMStream** uses SessionBroker for streaming file content (read, write, seek)
- All operations share the same TNFS mount via SessionBroker
- Single connection per server:port across all TNFSMFile and TNFSMStream instances
- Each TNFSMFile tracks its own `_dir_handle` to maintain directory iteration state
- Operations are sequential (IEC bus handles one command at a time), preventing handle conflicts
- TNFS library's `transaction_mutex` provides thread safety for the shared mount
- Automatic keep-alive maintains connection during idle periods

**Integration:**
SessionBroker automatically starts its service task during initialization:
```cpp
// In src/main.cpp main_setup():
fnWiFi.start();
SessionBroker::setup();  // Starts FreeRTOS task on CPU0

// In main_shutdown_handler():
SessionBroker::shutdown();  // Cleanup on shutdown
```

**Architecture:**
- SessionBroker task runs on CPU0 (WiFi core) to avoid interfering with IEC bus timing on CPU1
- Lower priority ensures WiFi and network operations don't block critical IEC communication
- Service loop runs every 1 second to check session idle times and send keep-alives

## Stream Examples

### D64 Disk Image

`D64MStream` (`media/disk/d64.h/cpp`):
- Reads D64 header at track 18, sector 0
- Parses BAM (Block Allocation Map)
- Navigates directory entries
- Follows track/sector chains to read files
- Supports GEOS files and relative files

### HTTP Protocol

`HTTPMStream` (`network/http.h/cpp`):
- Opens HTTP/HTTPS connections
- Supports range requests for seeking
- Handles redirects
- Parses headers for content-length
- Bottom stream - no containerStream

### Archive (ZIP/RAR/TAR/7Z)

`ArchiveMStream` (`media/archive/archive.h/cpp`):
- Wraps libarchive for ZIP/RAR/TAR/7Z/GZ/BZ2 support
- Lists entries by reading headers sequentially
- Extracts entry data into `MSession::CachedFile` via `ArchiveMSession` (HIMEM or heap)
- After caching, releases the archive handle and source stream chain to free memory
- `seekPath()` checks session cache (`"archive:" + url`) before re-opening archive (prevents DMA exhaustion)
- Supports nested containers (e.g., `game.zip/disk.d81/file.prg`)

`ArchiveMFile` (`media/archive/archive.h/cpp`):
- Single-file compressions (`.gz`, `.bz2`, etc.) are **transparent** — the compression layer is invisible:
  - `isDirectory()` with empty `pathInStream`: delegates to the inner file (e.g. D81MFile for `game.d81.gz`)
  - `isDirectory()` with non-empty `pathInStream`: returns `false` for single-file; for multi-file archives resolves entry via `MFSOwner::File(pathInStream)` so D64/D81 entries appear as directories, plain files do not
  - `rewindDirectory()` / `getNextFileInDir()`: delegate to inner file for single-file compressions
  - `getDecodedStream()` with empty `pathInStream` + single-file: auto-seeks the ArchiveMStream to the inner entry via `seekPath("*")` so the stream is ready-to-read (`isOpen()==true`) before the caller checks
  - `getDecodedStream()` with non-empty `pathInStream` referencing a file inside the inner container: builds `InnerFormatStream(ArchiveMStream(is))` so the caller's `seekPath()` resolves inside D81/D64/etc.
- `isSingleFileCompression()`: returns true for `.gz`, `.bz2`, `.xz`, `.lz`, `.z`, `.zst`, `.lz4`
- `getInnerFilename()`: strips the outermost compression extension (e.g. `game.d81.gz` → `game.d81`)
- `getInnerFile()`: lazily creates the inner MFile (e.g. D81MFile), copies `isPETSCII` flag

`ArchiveMSession` (`media/archive/archive.h`):
- Extends `MSession` for caching extracted archive entries
- Keyed as `"archive:" + url` in SessionBroker (both `ensureData()` and `seekPath()` use this key)
- `getEntry(path, archive*, size)`: Cache-first extraction — returns cached data or extracts from archive
- Keep-alive disabled (`setKeepAliveInterval(0)`) since no network to maintain
- File cache stores `shared_ptr<CachedFile>` per entry path

`Archive` (`media/archive/archive.h/cpp`):
- Low-level libarchive wrapper managing archive handle and callbacks
- `open(mode, rawOnly=false)`: when `rawOnly=true`, registers only `filter_all + format_raw` — guarantees `archive_read_next_header()` succeeds for single compressed files whose decompressed content isn't recognized by any standard format
- Custom `cb_read`, `cb_skip`, `cb_seek` callbacks for MStream integration
- `m_hasCompressionFilter` flag: when true (gzip/bz2/xz), disables raw seeking in callbacks
- `cb_skip` returns 0 with compression filters to force read-based skipping
- `cb_seek` only allows SEEK_SET to 0 (rewind) with compression filters

**Single-file compression size determination** (`seekEntry()` in archive.cpp):
- For `.gz`: reads ISIZE field from gzip trailer (last 4 bytes of file) — exact decompressed size mod 2^32 without full decompression
- For other formats (`.bz2`, `.xz`, etc.): reads through all compressed data counting bytes, then reopens
- `ensureData()` reopens with `rawOnly=true` for compressed-only files to guarantee `archive_read_next_header()` succeeds for extraction

## Important Patterns

### URL Parsing

Uses `PeoplesUrlParser` base class:
- `scheme`: Protocol (http, ftp, smb, etc.) — **empty for local paths**
- `host`: Server hostname
- `path`: Resource path
- `pathInStream`: Path within container
- `name`, `extension`: File parts

### `MFile::exists()` — Local vs. Network

The base class `exists()` in `meatloaf.cpp` behaves differently by path type:
- **Local paths** (`scheme` is empty): calls `stat(path.c_str(), &st)` — actual filesystem check. This overrides the `_exists = true` default that many media-extension subclasses (D64, archive, etc.) set in their constructors.
- **Network paths** (`scheme` non-empty): returns `_exists`, which is set by the protocol layer (e.g. after a successful HEAD request or directory listing).

**Why this matters**: `MFSOwner::File("/games/game.d64")` returns a `D64MFile` with `_exists = true` even if the `.d64` file doesn't exist on disk. The base-class `stat()` override corrects this without requiring each media subclass to override `exists()`.

### `MFSOwner::File()` and `.config` Redirects

`MFSOwner::File(path)` walks up the directory tree looking for `.config` files containing a `base_url=<remote_url>` line. If found, it redirects the path to the network URL and returns an `MFile` with a **non-empty `scheme`** (e.g. `http`, `ftp`, `smb`).

**Key rule**: after calling `MFSOwner::File()` for a local path, check `mf->scheme`:
- Non-empty → the path was redirected to a network resource; use this `MFile`.
- Empty → a local/media filesystem handled it (e.g. `D64MFile`, `FlashMFile`).

This is the pattern used by `webdav_mfile()` in `webdav_server.cpp` to support both `.config`-based virtual mounts and correct existence checking for media files.

### Error Handling

- Return `nullptr` from `getFile()` if URL not handled
- Set `_error` in MStream for I/O errors
- Check `isOpen()` before operations
- Use `eos()` to detect end-of-stream

### Memory Management

- Use `std::shared_ptr<MStream>` for streams
- `MFile*` raw pointers managed by owner (but deleted in destructor)
- `sourceFile` chain cleaned up recursively
- ImageBroker uses shared_ptr for automatic cleanup

## File Type Detection

`MFileSystem` provides static helpers:
- `byExtension()`: Match file extensions
- `byContent()`: Detect by magic bytes/header
- `bySize()`: Detect D64/D81/etc. by exact size

## Conditional Compilation

`MIN_CONFIG` excludes heavy features:
- Archives (ZIP, RAR)
- Network protocols (TNFS, SMB)
- Services (CSIP)

This reduces binary size for memory-constrained boards.

## Testing Streams

When implementing a new stream:
1. Test standalone access (single file from protocol)
2. Test nested access (file inside container on protocol)
3. Test directory listing (`isDirectory()`, `getNextFileInDir()`)
4. Test seeking (if random access)
5. Test writing (if writable)
6. Verify ImageBroker caching works correctly
7. Check memory leaks with stream chains

## Recent Changes (August 7, 2026)

- **DHD switched partitions part-way through a directory listing** (`media/hd/dhd.h`, `normalizePath()`) — another instance of the `uint8_t` truncation class. Partition numbers are `uint8_t` and `byNumber()` takes one, but the lookup was `img->byNumber(atoi(comp.c_str()))`: `atoi` returns `int`, which was silently truncated at the call. Selecting a partition strips its name from `pathInStream`, so entry URLs built during a listing carry no partition component (`…/hdbackup.dhd/1571`) and `normalizePath()` re-reads the first component as a partition reference — a file named `1571` inside the BIBLE partition resolved to `1571 & 0xFF` = 35 and switched the image to partition 35 ("GW BOOT HD") mid-`ls`, with the rest of the listing coming from the wrong partition (the selection also disposes the ImageBroker entry, so the stream re-opened underneath the running listing). Now parsed with `strtol` + `*end == '\0'` + `0 <= v <= 255` before the cast, per the existing rule against `atoi`/`std::stoi` on C64- or network-sourced input. `iecDrive::changePartition()` was already correct — it range-checks `1..254` before its `(uint8_t)` cast. **Residual, not fixed:** a file whose name exactly matches an existing partition's name or number *in range* still selects it during a listing. The real cause is that a selected partition is stripped from the path (CMD HD's modal design), so listing-generated entry URLs are genuinely ambiguous with partition references; closing that needs a path-model change, not a parse fix.

- **File sizes inside disk images are estimates — the directory has no byte-size field.** A CBM directory entry stores only a block count, so every byte figure is derived, and the two call sites intentionally differ: `D64MFile::getNextFileInDir()` sets `file->size = entry.blocks * block_size` (256/block — space occupied on disk, what `ls` shows) while `D64MStream::seekPath()` sets `_size = entry.blocks * (block_size - 2)` (254/block — data capacity). The TRUE size needs a walk to the chain's last block: `track == 0` marks it and the sector byte is the last-used byte index, so data = `sector - 1`. **Reads are correct regardless** — the chain-end marker fires before either over-estimate — so a `cat`/`hex` returning far fewer bytes than `ls` advertised is right, not truncated. Verified against `.archive/dnp/ned128test.dnp`: `vdcmania/settings.ihf` is 1 block, `ls` reports 256, the block's link bytes are `(0, 7)` and the real content is 6 bytes (`" 129 \r"`). `seekFileSize()` does the exact walk but stays commented out at the `seekPath()` call site (`d64.cpp`): it costs one read per block per file — hundreds of RPCs over the network, thousands of block reads for a single listing of a directory like this one.
- **`getNextFileInDir()` must honor `rewindDirectory()`'s return value** (`media/disk/d64.cpp`, `media/hd/hdd.cpp`): both now guard with `if (!dirIsOpen && !rewindDirectory()) return nullptr;`. A failed rewind has already called `resetEntryCounter()` on the shared ImageBroker stream while leaving `dirIsOpen` false, so continuing re-rewinds and re-serves entry 0 on every call — an ENDLESS listing rather than an empty one. Trigger: any `pathInStream` that names a file rather than a directory, e.g. `ls sett*`, where the wildcard resolves to a regular file so `enterDirectory()` correctly rejects it on the `file_type != 5 && != 6` check. Only these two implementations were affected — every other format (archive, m2i, t64, tcrt, ark, lbr, lnx) fails before the counter reset and cannot loop. Anything new that adds a post-reset failure path to `rewindDirectory()` must add the same guard.

## Recent Changes (August 5-6, 2026)

- **`format()` was producing structurally invalid images on every format** — found by the new native write suite (`test/native/test_disk_write/`, see its README). Nine fixes: directory sector overrun (258 bytes into 256), the directory sector never being allocated, `writeHeader()`'s control flow inverted so its allocation was dead code, D71 `speedZone()` misclassifying track 35, `initializeBlockAllocationMap()` ignoring bitmap-only records, D80/D82 BAM block headers (link, DOS version, track range) never written, the header sector unallocated on D80/D82, D71's whole reserved track 53, and DNP's 34-sector system area.
- **`uint8_t` truncation on 256-sector tracks** — CMD native (DNP/DHD) tracks hold 256 sectors, one more than a byte. `getSectorCount()` returns `uint16_t` for exactly this reason; anything storing a sector count or per-track free count must match. Two bugs came from this: the BAM initializer marked every block allocated, and `getTrackFreeCount()` made `getNextFreeBlock()` treat every track as full so no write could allocate anything.
- **`D64MStream::allow_grow`** (default **false**) gates DNP growth. Growing a native partition is a Meatloaf extension, not CMD behaviour, and it must stay off for a DNP inside a DHD — that partition is a fixed window and growing it overwrites its neighbour.
- **`formatImage(name, id, track_count, error_info)`** — `track_count == 0` means the media default; non-zero sizes the image from that geometry and must be applied before any layout, since `initializeBlocks()`/`initializeBlockAllocationMap()` both read `end_track`. `error_info` appends one status byte per sector (the area is reserved but nothing reads or writes it yet).
- **`dedicated_directory_track`** (false only for DNP) governs both whether directory blocks are confined to one track and whether file data is kept off it — one question, not two.

## Recent Changes (July 13-15, 2026)

- **Streamed writes into disk images** (`media/disk/d64.h/cpp`): `D64MStream::seekPath()` in write mode (`mode == std::ios_base::out`, set by `MFile::getSourceStream()`) creates a new file: the first free block is claimed near the directory track, data is streamed one 254-byte block at a time (each block written with its T/S link once the next block is allocated with the file interleave), and the directory entry is committed at `close()` (first free slot; directory extended on its own track with the directory interleave; `72,DISK FULL` if neither is possible). Failures roll back every BAM allocation. `SAVE"@:file"` scratches the old chain and reuses its slot. The BAM helpers (`getBAMRecord`/`readBAMRecord`/`setBlockAllocation`/`getTrackFreeCount`/`findFreeSectorOnTrack`) are generic over `block_allocation_map` — count-byte records (D64/D81) and bitmap-only records (D71 side 2, CMD native 32-byte records) both work. `MFile::getSourceStream()` opens the container chain `in|out` ("r+") for in-image writes and sets `decodedStream->mode`.
- **Subdirectory navigation** (`media/disk/d64.h/cpp`): `seekDirectory()`/`resolvePath()`/`enterDirectory()` walk '/'-separated paths inside images; CMD native `DIR` entries and 1581 `CBM` sub-partitions enter via their header block (bytes 0/1 → directory chain). Directory-listing entry URLs include the in-image path (`url + "/" + pathInStream + "/" + name`).
- **DHD (CMD HD) and D1M/D2M/D4M (CMD FD)** (`media/hd/dhd.h/cpp`, `media/disk/dxm.h`): `DHDImageRegistry` parses the partition table once per image (HD: system partition found by scanning 64 KiB boundaries for the boot magic at +$5F0, table at sys+65536, 254 partitions; FD: fixed system partition at 0x640/0xC80/0x1900 512-byte blocks for D1M/D2M/D4M, "CMD FD SERIES" magic, table at sys+2048 since FD track 0 is 8 sectors, 31 partitions; probing guard makes the CMD filesystems' `handles()` decline so the raw filesystem serves the bytes) and tracks the selected partition (default on first use). `DHDMFileSystem::getFile()` returns `DHDPartitionMFile<D64MFile|D71MFile|D81MFile|DNPMFile>` per the selected partition's type; `getDecodedStream()` builds the matching stream over a `DHDOffsetStream` window (offset+size of the partition), so the disk classes are unaware of the DHD. `LOAD"$=P"` lists partitions; CD/LOAD of a partition name/number selects it; `CP<n>` (drive command) selects by number. Selection changes dispose the ImageBroker key `"d64" + <container source url>[/<pathInStream>]`.
- **HDD (IDE64 CFS)** (`media/hd/hdd.h/cpp`): rewritten against the actual CFS 0.11 spec — 16-byte names, `LBA = (b0&0x0F)<<24|b1<<16|b2<<8|b3`, file size at $10 (LE), pointer at $14 (carries 2-bit NEXTS slices; HIDDEN bit 7), attributes $18, 3-char type $19, times $1C. Partition entries hold the root/deleted directory pointers at +$1C/+$18; TYPE is the end-pointer high nibble sans the LBA bit. Root of the image lists partitions (default-partition fallback for bare paths); subdirectories; multi-sector directories via the NEXTS pointer assembled from entry-pointer slices (byte 3 ← entries 1-4 … byte 0 ← entries 13-16, MSB-first per group of 4); data read via the balanced tree (node = 128 data pointers + 8 next-tree pointers in SLICE bits; coverage(d) = 64K + 8·coverage(d-1); depth chosen minimal for the file size; zero pointers = holes → $00). Read-only.
- **Tape images .tap/.dmp/.htap** (`media/tape/`, `components/tapclean/`): the TAPClean 0.39 engine (Final TAP 2.76 lineage) is vendored as an ESP-IDF component (the PIO lib builder does not compile `.c` under `lib/meatloaf`) with ~90 loader scanners (Cyberload, Visiload, US Gold, Novaload, Freeload, Turbotape 250/263/526, plus the Meatloaf-added `turbotape_fast.c` for 4x-speed TT64, ~88/144-cycle pulses). `TapeDecoder::open()` only parses the header; the image is fetched into PSRAM and scanned PROGRESSIVELY on demand (512 KB prefix, then doubling — DMP/HTAP/TAP-v2 are converted to TAP v1 on the fly as they stream in; halfwave pairs summed; the engine borrows the buffer, no copies). Each window scan (`tapclean_analyze_tap(unite=1)` joins neighbouring blocks into loadable PRGs) rebuilds the `entries` vector (loader-internal HEADER blocks folded in for their names, `_DATA` suffix stripped, repeat copies deduped, CBM boot stubs dropped when a turbo payload follows — their name transfers to it); the LAST entry of a partial window is withheld until confirmed complete. Once fully scanned the engine state and image are freed — only decoded programs stay resident. `nextProgram(from_offset)` serves the first entry with `tape_end_offset > from_offset` (may extend the scan); `offsetAtTime()` snaps to the program grid. The engine is global-state (NOT thread-safe): scans are serialized by a static mutex inside `TapeDecoder`. `TAPMStream`/`TAPMFile` are unchanged: datasette-style sequential listing (ONE entry per directory request, `no more entries` at tape end, rewind on the next request; loads search forward and wrap once), `.idx` sidecar (`<offset>[:<length>] <name>`, `#`/`;`/`'` comments) for full random-access listing, `TAPMFile::buildIndex()` / drive command `T-I` to generate it, `T-C <ms|MMM:SS>` to set the counter. Entry names are PETSCII internally (TAPClean returns ASCII; converted via `toPETSCII2`); unnamed entries take the media file's name with no load-address suffix. Tape decode effectively requires PSRAM (image prefix + decoded programs in RAM during the scan). The decoder + datasette position live in a `TapeState` shared across all TAPMStream instances of an image (weak_ptr registry keyed by container URL), so `LOAD"*",8` / console `cat *` serve the current listing entry and opens never re-scan.
- **M2I text index .m2i** (`media/disk/m2i.h/cpp`): MMC2IEC/sd2iec format — title line, then `T:DOSNAME.EXT :CBMNAME` lines (P/S/U files; `D` = DEL separator line, listed but never loadable; `-` free slots dropped). Entry data lives in sibling host files next to the .m2i: `resolveEntry()` finds each host file once (exact dosname, then lowercase — M2I comes from FAT; validated via `MFile::exists()`, never a lazy network open) and caches URL + size; `readFile()` delegates to the host file's stream. Line-based parsing (unpadded names, UTF-8 BOM, extension-less dosnames all occur in the wild). Read-only.

## Recent Changes (June 13, 2026)

- **`MFile::exists()` stat fix** (`meatloaf.cpp`): Base class `exists()` now calls `stat()` for local paths (`scheme` empty) instead of returning `_exists`. Media subclasses (D64, archive, etc.) that set `_exists = true` by default no longer falsely report existence for files that don't exist on disk.
- **`FlashMFile::isDirectory()` fix** (`device/flash.cpp`): Was ignoring the `stat()` return value; uninitialized `info.st_mode` on failure could be misread. Fixed to return false when `stat()` fails.
- **`webdav_mfile()` pattern** (`lib/www/webdav/webdav_server.cpp`): New helper that calls `MFSOwner::File()` for all local paths, inspects the returned `MFile::scheme`, and falls back to `FlashMFile` when scheme is empty. This preserves `.config` base_url virtual-mount behaviour while fixing false-existence reports from media-extension filesystems. See the "Important Patterns" section above for the general principle.

## SMB (Samba) Network Protocol

The SMB implementation uses a **session-based pooling pattern** with per-share context ownership:

### Components

- **`SMBMSession`** (`network/smb.h` lines 49-226): Manages one SMB connection per server (host:port)
  - Maintains IPC$ context for share enumeration and operations
  - Creates and caches SMB2 contexts on-demand via `getShareContext(share)` - reuses contexts for same share
  - Stores credentials and propagates to all contexts
  - Enumerates server shares once via `getShares()` and caches the list
  - Share enumeration happens asynchronously with polling, result cached for subsequent queries

- **`SessionBroker`** (`network/smb.h`): Singleton connection pool
  - Keys sessions by host:port
  - Reuses session instances across multiple files/streams
  - Lazy initialization via `obtain<SMBMSession>(host, port)`

- **`SMBMFile`** (`network/smb.h` lines 191-310): File abstraction for SMB paths
  - Obtains session from SessionBroker
  - Gets exclusive share context on construction
  - Destroys share context in destructor
  - Supports share enumeration when no specific share given

- **`SMBMStream`** (`network/smb.h` lines 355-420): Stream for file I/O
  - Obtains session from SessionBroker
  - Gets exclusive share context in `open()` method
  - Destroys share context in destructor
  - Implements MStream read/write/seek interface

### Path Format

- Server root: `smb://host/` → enumerate shares
- Share listing: `smb://host/share` → list files in share
- File access: `smb://[user:pass@]host[:port]/share/path/file` → open file

### libsmb2 API Requirements

- Share root directory: use "." in `smb2_opendir()`
- File paths: use "/" prefix (e.g., "/filename")
- Credentials: must be set before `smb2_connect_share()`
- SMB version: set to SMB2_VERSION_ANY for compatibility

### Context Ownership Model

Each SMBMFile and SMBMStream owns an **exclusive** SMB2 context for its share:
- Context created on-demand from session
- Stored as member variable (`_share_context`)
- Destroyed in destructor via `smb2_destroy_context()`
- Prevents use-after-free and simplifies lifecycle management
- Trade-off: Higher file descriptor usage (can't reuse contexts across files)

### Known Limitations

- ESP32 file descriptor limit (~20-30) constrains concurrent open files
- No context reuse across SMBMFile instances
- May need application-level limiting for many simultaneous share accesses

### Recent Changes (Dec 25, 2025)

- Removed context caching from `SMBMSession::getShareContext()` - was causing hanging via use-after-free
- Added exclusive context ownership to SMBMFile and SMBMStream destructors
- Each file/stream now manages its own context lifetime
- Fixed compilation errors and verified clean build

## NFS (Network File System) Protocol

The NFS implementation follows the **session-based pooling pattern** with per-export context management:

### Components

- **`NFSMSession`** (`network/nfs.h` lines 55-157): Manages NFS connections per server (host:port)
  - Root context (`_nfs`) for server-level operations
  - Per-export contexts cached in `_export_contexts` map
  - Export enumeration via `mount_getexports()` from libnfs-raw-mount.h
  - Automatic "/" prefix added to export paths for proper mounting
  - Directory caching disabled via `nfs_set_dircache(nfs, 0)` to prevent memory leaks on ESP32

- **`SessionBroker`**: Reuses NFS sessions across files/streams

- **`NFSMFile`** (`network/nfs.h` lines 159-365): File abstraction for NFS paths
  - Obtains session from SessionBroker
  - Parses URL into export_path and file_path components
  - Maintains directory handle (`_handle_dir`) for directory iteration
  - Destructor calls `closeDir()` to free directory handle
  - Gets export context via `session->getExportContext(export_path)`

- **`NFSMStream`** (`network/nfs.h` lines 311-365): Stream for file I/O
  - Obtains session from SessionBroker
  - Opens file handle (`nfsfh`) via `nfs_open()`
  - Supports random access seeking with `nfs_lseek()`
  - Closes file handle in destructor via `nfs_close()`

### Path Format

- Server root: `nfs://host/` → enumerate exports
- Export listing: `nfs://host/export` → list files in export
- File access: `nfs://host/export/path/file` → open file

### libnfs API Requirements

- Export paths: must be prefixed with "/" for `nfs_mount()`
- Directory operations: use `.` for share root in `nfs_opendir()`
- Separate contexts: each mounted export needs its own `nfs_context`
- Directory entries: returned by `nfs_readdir()`, owned by `nfsdir`, freed by `nfs_closedir()`

### Memory Management

**Directory Caching Issue:**
- libnfs enables directory caching by default (MAX_DIR_CACHE = 128)
- `nfs_closedir()` adds directories to cache instead of freeing them
- On ESP32 with limited memory, this caused ~700-1000 byte leaks per directory read
- **Solution**: Disable caching via `nfs_set_dircache(nfs, 0)` after context creation

**Resource Cleanup:**
- Directory handles freed via `nfs_closedir()` in `NFSMFile::closeDir()`
- File handles freed via `nfs_close()` in `NFSMStream` destructor
- Export contexts freed via `nfs_destroy_context()` in `NFSMSession::disconnect()`
- Directory entries automatically freed when `nfs_closedir()` called (owned by nfsdir structure)

### Recent Memory Optimizations (June 2026)

**Thread Safety:**
- `CachedFile::s_rangeUsed` changed from `int` to `std::atomic<int>` — prevents race conditions between SessionBroker (core 0) and IEC bus task (core 1) during HIMEM range allocation/deallocation

**SessionBroker Session Lifecycle:**
- Sessions NOT in use by any drive or console are removed immediately (no keep-alive)
- Sessions IN use: keep-alive attempted; if fails, reconnect is tried before removal
- `is_session_in_use()` checks both drives AND console current path
- `path_matches_session()` compares paths ignoring port numbers (e.g., `tnfs://host:16384` matches `tnfs://host/`)
- `session_host_key()` extracts `scheme://host` from session key for comparison
- Console path checked via `ESP32Console::getCurrentPath()`

**ImageBroker LRU Eviction:**
- Max 50 entries (`max_entries`)
- Stale cleanup every 60s (`cleanup_interval_ms`)
- `is_in_use()` checks if entry matches any active drive's CWD
- Protected entries (mounted on drives) are never evicted during LRU cleanup
- `evict_lru_if_needed()` only evicts entries that are NOT in use

**mfilebuf PSRAM Allocation:**
- `gbuffer[2049]` and `pbuffer[513]` now use `heap_caps_malloc()` with `MALLOC_CAP_SPIRAM`
- Falls back to heap if PSRAM exhausted
- Saves ~2.5KB internal DRAM per open file

**MSession File Cache LRU:**
- Max 10 entries per session (`max_file_cache_entries`)
- `cache_order` list tracks LRU order (front = most recent)
- `getCachedFile()` moves accessed entries to front
- `cacheFile()` evicts oldest when at capacity

**CachedFile Static Buffers:**
- `loadFromStream()` uses `thread_local` static buffers instead of per-call malloc/free
- Reduces allocation churn during stream loading

### ESP32 Compatibility Layer

**Missing POSIX Functions** (`components/libnfs/esp32/esp32_compat.h`):
- `major()`, `minor()`, `makedev()`: Device number macros (return 0)
- `getuid()`, `getgid()`: User/group IDs (return 0)
- `signal()`: Signal handling (returns SIG_ERR)
- `dup2()`: File descriptor duplication (returns -1)
- `getservbyport()`: Service lookup (returns NULL)

**NULL Safety Fixes** (`components/libnfs/lib/nfs_v4.c`):
- Added NULL checks in `check_nfs4_error()` for:
  - `data->path` → fallback to "unknown path"
  - `op_name` → fallback to "unknown operation"
  - `nfsstat4_to_str()` → fallback to "unknown status"
  - `nfs_get_error()` → fallback to "unknown error"

### Archive Support Integration

**libarchive .gz Support** (`components/libarchive/lib/archive_read_support_filter_gzip.c`):
- Reduced decompression buffer from 64KB to 256 bytes for ESP32 (`out_block_size`)
- Added `avail_in <= 0` validation before calling `inflate()` to prevent UINT_MAX overflow
- Wrapped debug prints in `#ifdef DEBUG_ZLIB` to avoid crashes from NULL `strm->msg`

**Raw Format Support** (`lib/meatloaf/media/archive/archive.cpp`):
- Added `archive_read_support_format_raw()` for single compressed files (.gz without tar)
- Size detection by reading through compressed data when format is RAW

**Archive Entry Caching** (February 2026, updated March 2026):
- `ArchiveMSession` caches extracted archive entries in `MSession::CachedFile`
- `CachedFile` uses HIMEM page-mapped access on ESP32+SPIRAM, heap otherwise
- `ensureData()` extracts entry once, then releases archive handle + source stream to recover memory
- `seekPath()` checks session cache first — avoids re-opening archive (prevents DMA exhaustion)
- Session keyed as `"archive:" + url` (both `ensureData()` and `seekPath()` use this exact key), keep-alive disabled
- ImageBroker caches `ArchiveMStream` instances (stream objects) — orthogonal to ArchiveMSession (data cache)

### Context Ownership Model

- **Root context**: Created in `connect()`, destroyed in `disconnect()`
- **Export contexts**: Created on-demand in `getExportContext()`, cached in map, destroyed in `disconnect()`
- **Directory handles**: Owned by NFSMFile, freed in `closeDir()` called from destructor
- **File handles**: Owned by NFSMStream, freed in destructor via `nfs_close()`

### Recent Changes (Jan 6, 2026)

- Implemented full NFS client using libnfs synchronous API
- Created ESP32 compatibility layer for missing POSIX functions
- Fixed multiple NULL pointer crashes in nfs_v4.c and inflate
- Added export enumeration and per-export context management
- Integrated gzip decompression support for .gz files
- **Fixed memory leak**: Disabled libnfs directory caching to prevent ~700-1000 byte leaks per directory read
- **Reduced buffer sizes**: Set nfs_set_readmax/writemax to 8192 (from default 65536) for ESP32 memory constraints
- Verified stable memory usage on ESP32 with directory caching disabled

### Known Issues

- **Memory leak (~560 bytes per 15-file listing, ~37 bytes/file) - CRITICAL UNIVERSAL ISSUE**: Affects ALL filesystem types (flash, NFS, SMB, HTTP, etc.) during directory listing operations.
  
  **Root cause**: Heap fragmentation from the MFile architecture combined with ESP32's heap allocator behavior:
  - Each temporary MFile object inherits from PeoplesUrlParser (13 std::string members)
  - Additional MFile strings (type, media_header, media_id, media_archive, media_image)
  - URL parsing creates many temporary string allocations
  - Multiple string operations during filename formatting (toPETSCII2, replaceAll, substr)
  - ESP32's TLSF allocator fragments with many small allocations/deallocations
  - Even with explicit clear() and shrink_to_fit(), memory is not recovered
  
  **Investigation results**:
  - ✓ Confirmed MFile destructors are being called
  - ✓ Confirmed ImageBroker streams count stays at 0
  - ✓ Added explicit string cleanup - NO EFFECT
  - ✓ Reproduced with flash storage (rules out network protocol issues)
  - ✗ Issue persists regardless of cleanup efforts
  
  **Impact**: System can handle ~6000 directory listings before running out of memory (from ~3.7MB starting heap). This is sufficient for normal usage but could be problematic for applications that heavily browse directories.
  
  **Potential solutions explored**:
  1. **string_view refactor** - Attempted to refactor PeoplesUrlParser to use std::string_view instead of 13 std::string members. This would reduce from 13 allocations to 1-3 per MFile. However, this breaks 91+ call sites that use `.c_str()` and assignment operators, requiring extensive changes. Additionally, string_view's `.data()` is not null-terminated, making it incompatible with C APIs without creating temporary std::string objects anyway.
  
  2. **Other potential solutions** (not yet implemented):
     - Object pooling: Pre-allocate MFile object pool, reuse instead of new/delete
     - Arena allocator: Use custom allocator that allocates in large blocks
     - Stack allocation: Return MFile by value instead of pointer where feasible
     - Different heap strategy: Use SPIRAM for temporary objects if available
  
  **Decision**: Accept the current behavior as tolerable. The 560-byte leak per 15-file listing allows ~6000 listings before exhaustion, which exceeds normal usage patterns. Extensive refactoring for marginal improvement is not justified at this time.
### Recent Work (Jan 2026)

**NFS Directory Sorting** (Jan 6):
- Fixed descending directory order by adding `std::sort()` to `NFSMSession::enumerateExports()`
- Exports now display alphabetically ascending

**SMB Authentication Improvements** (Jan 6-7):
- Changed from forced `SMB2_SEC_NTLMSSP` to `SMB2_SEC_UNDEFINED` (auto-negotiate)
- Allows server to choose authentication method instead of forcing NTLMSSP
- Improves compatibility with macOS SMB servers

**Kerberos Stub Implementation** (Jan 7):
- Created `components/krb5/` stub component for macOS SMB compatibility
- Implemented minimal GSSAPI/Kerberos headers and function stubs
- Functions return `GSS_S_UNAVAILABLE` allowing libsmb2 to fall back to NTLM
- Avoids ~2MB+ full MIT Kerberos implementation unsuitable for ESP32
- Includes:
  - `gssapi_stub.c`: ~300 lines of stub implementations
  - `include/gssapi/gssapi.h`: Full GSSAPI type definitions
  - `include/gssapi/gssapi_krb5.h`: Kerberos extensions
  - `include/krb5/krb5.h`: Basic Kerberos types
  - Key types: `gss_const_OID`, `gss_buffer_set_t`, `GSS_C_INQ_SSPI_SESSION_KEY`
  - Key functions: `gss_set_neg_mechs()`, `gss_inquire_sec_context_by_oid()`, `gss_release_buffer_set()`

**Critical Crash Fixes in seekFileSize()** (Jan 7-9):
- **Root cause**: High-frequency `printf()` calls causing UART interrupt conflicts with `display_task()` running on same core
- **Symptoms**: Crash at address `0x000063a0` during `vPortYieldFromInt` (FreeRTOS interrupt handler)
- **Fixes applied**:
  1. Removed all `printf()` from seekFileSize() loop - eliminated UART contention
  2. Added `vTaskDelay(1)` every 10 blocks - yields to FreeRTOS scheduler
  3. Added watchdog reset every 100 blocks - prevents timeout during long operations
  4. Added NULL checks for `containerStream` - prevents NULL pointer crashes
  5. Added read validation - checks that `readContainer()` returns expected byte count
  6. Added MAX_BLOCKS limit (10,000) - prevents infinite loops
  7. Added error messages for stream failures and corrupted block chains
- **Technical details**:
  - `display_task()` runs on core 0, priority 4, with UART interrupts
  - Simultaneous printf from multiple tasks corrupts UART driver state
  - ESP-IDF printf is not fully thread-safe across tasks
  - Solution: Eliminate printf from hot paths, use Debug_printv() sparingly

**TAP File Indexing Optimization** (Jan 10):
- Changed `analyzeTapeData()` from full decode to fast indexing
- Added `skipDataBlock()` to skip over data without reading into memory
- Index now stores: filename, type, offset, length, start_address, end_address
- Modified `read()` to decode data on-demand when first accessed
- **Benefits**:
  - Much faster tape analysis (skips all data bytes)
  - Minimal memory usage during indexing (no cached_data)
  - Same functionality for users (transparent on-demand decode)
- Files only decoded when actually read, reducing memory footprint

### Known Issues

- **Memory leak (~560 bytes per 15-file listing, ~37 bytes/file) - CRITICAL UNIVERSAL ISSUE**: Affects ALL filesystem types (flash, NFS, SMB, HTTP, etc.) during directory listing operations.
  
  **Root cause**: Heap fragmentation from the MFile architecture combined with ESP32's heap allocator behavior:
  - Each temporary MFile object inherits from PeoplesUrlParser (13 std::string members)
  - Additional MFile strings (type, media_header, media_id, media_archive, media_image)
  - URL parsing creates many temporary string allocations
  - Multiple string operations during filename formatting (toPETSCII2, replaceAll, substr)
  - ESP32's TLSF allocator fragments with many small allocations/deallocations
  - Even with explicit clear() and shrink_to_fit(), memory is not recovered
  
  **Investigation results**:
  - ✓ Confirmed MFile destructors are being called
  - ✓ Confirmed ImageBroker streams count stays at 0
  - ✓ Added explicit string cleanup - NO EFFECT
  - ✓ Reproduced with flash storage (rules out network protocol issues)
  - ✗ Issue persists regardless of cleanup efforts
  
  **Impact**: System can handle ~6000 directory listings before running out of memory (from ~3.7MB starting heap). This is sufficient for normal usage but could be problematic for applications that heavily browse directories.
  
  **Potential solutions explored**:
  1. **string_view refactor** - Attempted to refactor PeoplesUrlParser to use std::string_view instead of 13 std::string members. This would reduce from 13 allocations to 1-3 per MFile. However, this breaks 91+ call sites that use `.c_str()` and assignment operators, requiring extensive changes. Additionally, string_view's `.data()` is not null-terminated, making it incompatible with C APIs without creating temporary std::string objects anyway.
  
  2. **Other potential solutions** (not yet implemented):
     - Object pooling: Pre-allocate MFile object pool, reuse instead of new/delete
     - Arena allocator: Use custom allocator that allocates in large blocks
     - Stack allocation: Return MFile by value instead of pointer where feasible
     - Different heap strategy: Use SPIRAM for temporary objects if available
  
  **Decision**: Accept the current behavior as tolerable. The 560-byte leak per 15-file listing allows ~6000 listings before exhaustion, which exceeds normal usage patterns. Extensive refactoring for marginal improvement is not justified at this time.

## AFP (Apple Filing Protocol)

The AFP implementation provides access to AFP file servers (macOS file sharing, Netatalk) using the **afpfs-ng** library via the session-based pooling pattern.

### Components

- **`AFPMSession`** (`network/afp.h` lines 71-115): Manages one AFP server connection (host:port)
  - One-time global init of the afpfs-ng DSI event loop via `afp_main_quick_startup()` (lazy, first connect)
  - `connect()`: calls `afp_server_full_connect()` + `afp_getsrvrparms()` to refresh volume list
  - `getVolume(name)`: finds volume in server's array, mounts via `afp_connect_volume()`, caches in `_mounted_volumes` map
  - `getVolumes()`: returns alphabetically sorted list from `_server->volumes[]`; cached after first call
  - `disconnect()`: `afp_unmount_all_volumes()` → `afp_logout()` → `afp_free_server()`
  - `keep_alive_interval = 0`: AFP FPZzzzz keepalive handled by afpfs-ng if needed; SessionBroker keep-alive disabled to prevent concurrent socket access

- **`AFPMFile`** (`network/afp.h` lines 122-198): File/directory abstraction
  - Obtains session from SessionBroker; calls `parseAFPPath()` to split URL into `volume_name` + `file_path`
  - At server root (no volume): lists volumes via `_session->getVolumes()`
  - In volume: uses `ml_readdir()` for directory listing (returns linked list of `afp_file_info*`)
  - Directory listing freed with `afp_ml_filebase_free(&_dir_base)` in `closeDir()`
  - `readEntry()`: searches parent directory via `ml_readdir()` for wildcard/glob name matching
  - `isDirectory()`, `exists()`, `getLastWrite()`, `getCreationTime()`: via `ml_getattr()` → `struct stat`
  - `getAvailableSpace()`: via `ml_statfs()` → `f_bavail * f_bsize`
  - `mkDir()`, `remove()`, `rename()`: via `ml_mkdir()`, `ml_unlink()`/`ml_rmdir()`, `ml_rename()`

- **`AFPMStream`** (`network/afp.h` lines 226-266): File I/O stream
  - `open()`: resolves URL, obtains session, mounts volume, calls `ml_open()`, gets size via `ml_getattr()`
  - `read()` / `write()`: `ml_read()` / `ml_write()` with explicit `_position` offset (AFP mid-level API is offset-based, not stateful)
  - `seek()`: updates `_position` locally — no network call needed since ml_read/write take explicit offsets
  - `close()`: calls `ml_close()`, releases session I/O lock

- **`AFPMFileSystem`** (`network/afp.h` lines 273-293): Factory
  - `handles("afp:")` returns true
  - Empty host → redirects to `mdns://_afpovertcp._tcp` for mDNS service discovery
  - Registered in `meatloaf.cpp` alongside other network filesystems

### Path Format

- Server root: `afp://host/` → list volumes
- Volume root: `afp://host/VolumeName` or `afp://host/VolumeName/` → list volume contents
- File access: `afp://host/VolumeName/path/to/file.prg`
- With credentials: `afp://user:pass@host/VolumeName/path`
- Default port: 548

### afpfs-ng API

- **Global init** (once): `init_uams()` → `libafpclient_register()` → `afp_main_quick_startup()` → `afp_wait_for_started_loop()`
- **Connection**: `afp_default_url()` → fill `afp_connection_request` → `afp_server_full_connect()`
- **Volume list**: `afp_getsrvrparms()` populates `_server->volumes[]` / `_server->num_volumes`
- **Volume mount**: `find_volume_by_name()` → `afp_connect_volume()`
- **File ops**: `ml_open()`, `ml_read()`, `ml_write()`, `ml_close()`, `ml_readdir()`, `ml_getattr()`, `ml_statfs()`, `ml_mkdir()`, `ml_unlink()`, `ml_rmdir()`, `ml_rename()`
- **Directory listing**: `ml_readdir()` returns `struct afp_file_info*` linked list (via `->next`); freed with `afp_ml_filebase_free(&base)`
- **Callbacks**: `struct libafpclient` registered via `libafpclient_register()`; `log_for_client` bridges to `Debug_printv`

### ESP32 Compatibility Layer (`components/afpfs-ng/esp32/`)

afpfs-ng requires POSIX functions missing from ESP32/newlib. Six weak stubs in `esp32_compat.c`:

| Function | Reason needed | ESP32 stub behavior |
|---|---|---|
| `signal()` | `loop.c` installs `SIGUSR2`/`SIGTERM` handlers | Returns `SIG_DFL` (no-op) |
| `sigprocmask()` | `loop.c` blocks `SIGUSR2` before `pselect` | Returns 0 (no-op) |
| `pthread_kill()` | `signal_main_thread()` wakes pselect | Returns 0 (no-op) |
| `pselect()` | Main event loop wait | Delegates to `select()`, ignores signal mask |
| `geteuid()` | `afp_server_init` passes to `getpwuid` | Returns 0 (root) |
| `getpwuid()` | `afp_server_init` memcpys result without NULL check | Returns static `struct passwd` with dummy values |

- `esp32_compat.h` forward-declares only `pselect` (absent from all newlib headers; others are in system headers)
- Force-included via `target_compile_options(-include esp32_compat.h)` — no library source modifications
- CMakeLists: `file(GLOB_RECURSE SRCS "lib/*.c" "esp32/*.c")` ensures compat file is compiled

### struct statvfs Conflict

Both afpfs-ng and libnfs define `struct statvfs` for ESP32. Resolved with `#ifndef _STATVFS_DEFINED` / `#define _STATVFS_DEFINED` guard in both:
- `components/afpfs-ng/include/afp.h`
- `components/libnfs/include/nfsc/libnfs.h`

### Context Ownership Model

- **Server**: created in `connect()`, destroyed in `disconnect()` via `afp_free_server()`
- **Volumes**: mounted on-demand in `getVolume()`, unmounted in `disconnect()` via `afp_unmount_all_volumes()`
- **Directory listing**: owned by `AFPMFile`, freed in `closeDir()` via `afp_ml_filebase_free()`
- **File forks**: owned by `AFPMStream`, closed in `close()` via `ml_close()`

### Implementation Notes (March 2026)

- `pthread_t s_afp_loop_thread` is file-local in `afp.cpp` (not a class member) — the `afp_global_init()` static function can't access private class members
- AFP epoch (Jan 1 2000) offset `AFP_EPOCH_OFFSET 946684800` defined but not needed — `ml_getattr()` returns `struct stat` with Unix timestamps directly
- `afp_protocol.h` had a Unicode en-dash (U+2013) in `#define kFPDiskQuotaExceeded –5047`; fixed to ASCII hyphen `-5047`
- Build cache issue: if linker errors persist for `geteuid`/`getpwuid`/`pselect` etc. after editing CMakeLists.txt, delete `.pio/build/<env>/esp-idf/afpfs-ng/` and `CMakeCache.txt` to force cmake reconfiguration

## FSP (File Service Protocol) Protocol

The FSP implementation provides UDP-based file transfer capabilities through the Meatloaf filesystem abstraction, following the **session-based pooling pattern** similar to TNFS.

### Components

- **`FSPMSession`** (`network/fsp.h` lines 49-120): Manages FSP client connections per server (host:port)
  - Extends `MSession` for connection lifecycle management
  - Uses `fsp_open_session()` from fsplib for UDP-based connections
  - Default port is 21 (FSP_DEFAULT_PORT)
  - Session pooling via `SessionBroker` for connection reuse

- **`FSPMFile`** (`network/fsp.h` lines 123-145): Directory entry representation
  - Extends `MFile` for file metadata and attributes
  - Supports FSP-specific file attributes and timestamps

- **`FSPMStream`** (`network/fsp.h` lines 148-200): File I/O operations
  - Extends `MStream` for read/write/seek operations
  - Uses `fsp_fopen()`, `fsp_fread()`, `fsp_fwrite()`, `fsp_fclose()` from fsplib
  - URL parsing in `open()` method to extract host, port, and path
  - Supports binary file transfers over UDP

- **`FSPMFileSystem`** (`network/fsp.h` lines 203-230): Filesystem factory and operations
  - Extends `MFileSystem` for filesystem registration
  - Handles "fsp://" URL scheme
  - Directory listing via `fsp_opendir()` and `fsp_readdir_native()`
  - Path format: `fsp://host[:port]/path`

### Path Format

```
fsp://host[:port]/path
```

- **host**: FSP server hostname or IP address
- **port**: Optional port number (default: 21)
- **path**: File or directory path on the FSP server

### API Requirements

- **fsplib**: FSP protocol library (components/fsplib)
  - Provides UDP-based file transfer functions
  - Requires extern "C" linkage for C++ integration
  - ESP32-compatible configuration in config.h

### Integration Points

- **meatloaf.cpp**: Registered in `availableFS` vector under MIN_CONFIG
- **SessionBroker**: Manages FSP session pooling and keep-alive
- **MFileSystem registry**: Recognizes "fsp://" URLs and routes to FSPMFileSystem

### Known Issues

- **C/C++ linkage**: Requires extern "C" block around fsplib.h include to prevent name mangling
- **URL parsing**: FSPMStream implements custom URL parsing since it doesn't inherit from PeoplesUrlParser
- **Port handling**: Must convert string ports to uint16_t for SessionBroker compatibility

## HTTP/HTTPS Protocol

The HTTP implementation provides web-based file transfer capabilities through the Meatloaf filesystem abstraction, following the **session-based pooling pattern** for connection reuse and keep-alive support.

### Components

- **`HTTPMSession`** (`network/http.h`): Manages HTTP/HTTPS client connections per server (host:port)
  - Extends `MSession` for connection lifecycle management
  - Uses ESP-IDF `esp_http_client` for HTTP/HTTPS operations
  - Supports both HTTP (port 80) and HTTPS (port 443) with automatic port detection
  - Session pooling via `SessionBroker` for connection reuse
  - `keep_alive_interval = 0`: HTTP connection reuse is managed by esp_http_client internally; SessionBroker keep-alive disabled

- **`MeatHttpClient`** (`network/http.h`): Wraps `esp_http_client_handle_t`; one instance per `HTTPMSession`
  - `_size`: tracks total bytes received so far (grows during chunked responses via `HTTP_EVENT_ON_DATA`)
  - `_position`: tracks bytes consumed by `read()` calls
  - `available()`: returns `_size - _position`
  - `complete()`: delegates to `esp_http_client_is_complete_data_received()` — true once all body bytes are consumed
  - `read()`: calls `esp_http_client_read()`; for non-chunked range responses only, re-opens for next range when a partial read occurs

- **`HTTPMFile`** (`network/http.h`): File abstraction for HTTP paths
  - Extends `MFile` for file metadata and HTTP headers
  - Supports HTTP-specific attributes like content-type and content-length

- **`HTTPMStream`** (`network/http.h`): File I/O stream
  - Extends `MStream` for read/write operations over HTTP
  - `open()`: executes GET/PUT/POST; sets `_size` from Content-Length or from first `HTTP_EVENT_ON_DATA` for chunked
  - `available()` override: returns `_size - _position` when positive; falls back to `HTTP_BLOCK_SIZE` hint when `isOpen() && !client->complete()` (chunked in-flight); returns 0 when complete
  - `read()`: caps by `available()` when size is known; syncs `_size = _position` after each read to track consumed bytes for chunked responses

- **`HTTPMFileSystem`** (`network/http.h`): Filesystem factory
  - Handles "http://" and "https://" URL schemes
  - Empty host redirects to `mdns://_http._tcp` for service discovery

### Path Format

```
http://host[:port]/path
https://host[:port]/path
```

- **host**: HTTP/HTTPS server hostname or IP address
- **port**: Optional port number (default: 80 for HTTP, 443 for HTTPS)
- **path**: File or directory path on the web server

### Chunked Transfer Encoding

Servers that don't send `Content-Length` use chunked encoding (`Transfer-Encoding: chunked`). Handling differs from fixed-size responses:

**Size tracking:**
- `HTTP_EVENT_ON_DATA` fires for each data event during `esp_http_client_read()`
- When chunked, `meatClient->_size += evt->data_len` accumulates received bytes
- `HTTPMStream::_size` is initialized from `client._size` at `open()` time (may be 0 initially if all data arrived during `fetch_headers()`, or may already reflect the first chunk)
- After each `HTTPMStream::read()`, `_size` is synced up to `_position` to keep `available()` accurate

**EOF detection:**
- `available()` checks `complete()` after `_size == _position` — returns `HTTP_BLOCK_SIZE` hint while in-flight, 0 when done
- `MeatHttpClient::read()` does NOT call `openAndFetchHeaders` for chunked responses (checked via `esp_http_client_is_chunked_response()`), preventing infinite re-requests on EOF
- `MeatHttpClient::read()` also does NOT call `openAndFetchHeaders` when `bytesRead == 0` (EOF signal), even for non-chunked responses

**Range requests vs chunked:**
- `openAndFetchHeaders` in `read()` is only called when `bytesRead > 0 && bytesRead < size && !chunked` — this handles paging through range-based (non-chunked) responses where a partial read signals "range exhausted, need next range"

### API Requirements

- **esp_http_client**: ESP-IDF HTTP client library
  - `esp_http_client_is_chunked_response()`: detect chunked encoding
  - `esp_http_client_is_complete_data_received()`: true after final chunk consumed
  - `HTTP_EVENT_ON_DATA`: fires with `evt->data_len` bytes per data event

### Integration Points

- **meatloaf.cpp**: Registered in `availableFS` vector
- **SessionBroker**: Manages HTTP session pooling
- **MFileSystem registry**: Recognizes "http://" and "https://" URLs and routes to HTTPMFileSystem

### Known Issues / Notes

- **C/C++ linkage**: Requires extern "C" block around esp_http_client includes to prevent name mangling
- **URL parsing**: HTTPMStream uses PeoplesUrlParser for URL parsing
- **Session management**: HTTPMSession handles both HTTP and HTTPS with appropriate port defaults

### Recent Changes (March 15, 2026)

- **Single-file compressed archive transparency** (`media/archive/archive.h/cpp`):
  - `.gz`, `.bz2`, `.xz`, etc. are now fully transparent — directory listing, `LOAD "*"`, and `LOAD "filename"` all work as if the compression layer doesn't exist
  - `ArchiveMFile::isDirectory()`: restructured to handle all cases correctly:
    - Non-empty `pathInStream` + single-file compression → always `false`
    - Non-empty `pathInStream` + multi-file archive → resolves via `MFSOwner::File(pathInStream)` so `.d64`/`.d81` entries return `true`, plain PRG entries return `false`
    - Empty `pathInStream` + single-file → delegates to inner file's `isDirectory()`
  - `ArchiveMFile::rewindDirectory()` / `getNextFileInDir()`: delegate to inner MFile for single-file archives; copy media metadata (`media_header`, `media_id`, etc.) back after rewind
  - `ArchiveMFile::getDecodedStream()`: for single-file archives
    - Empty `pathInStream` (e.g. `LOAD "game.prg.gz"`): creates ArchiveMStream and calls `seekPath("*")` immediately so `isOpen()==true` when drive checks; also calls `resetURL(base())` to fix the CWD after load
    - Non-empty `pathInStream` referencing a file inside the inner container (e.g. `LOADER` inside `mars saga.d81.gz`): builds `InnerFormatStream(ArchiveMStream)` so caller's `seekPath()` resolves inside D81/D64
  - `Archive::open(rawOnly=true)`: registers only `filter_all + format_raw` — guarantees synthetic header for extraction when decompressed content isn't recognized by standard format detectors
  - `seekEntry()`: reads gzip ISIZE trailer (last 4 bytes) for `.gz` size without full decompression; `ensureData()` reopens with `rawOnly=true` for compressed-only extraction
  - Session key unified: both `ensureData()` and `seekPath()` use `"archive:" + url` — cache lookup in `seekPath()` now correctly hits entries stored by `ensureData()`

### Recent Changes (July 29, 2026)

- **Range-request regression fix — restore the position-0 Range probe** (`network/http.cpp`, `MeatHttpClient::openAndFetchHeaders`): a July-17 change (commit `1b8b16e3`) had narrowed the Range header condition from `method == HTTP_METHOD_GET` to `method == HTTP_METHOD_GET && position > 0`, which broke range-support DETECTION for seek-heavy media over HTTP (D64/D81 directory listing and file loads). The FIRST GET of a container happens at position 0, and it is the `Range: bytes=0-…` on THAT request whose `206` + `Content-Range` response sets `isFriendlySkipper` and `_range_size` (full file size). Without it a range-capable server answers `200` with the whole body, range support is never detected, and every subsequent `seek()` degrades to restart-and-flush (forward-only; a backward seek reopens the GET while the prior full-body response is still draining on the reused handle, desyncing the connection). Fix: send the Range header on EVERY GET including position 0 again (restores the exact pre-regression code from `061cdb07`), and add a one-shot retry-without-Range if the position-0 probe returns `416` (empty file / range-hostile server — the case `1b8b16e3` was trying to guard). Status is read BEFORE the `_size` assignment so a 416 error body's length can't pollute `_size`. Note: on a 206 the client's `_size` holds only the partial chunk length; `HTTPMStream::open()` already uses `_range_size` (the full size) when it is set. Verified working on hardware against `https://c64.meatloaf.cc/test/m.u.l.e.d64`.
- **`MFile::getDownloadFilename()`** (`meatloaf.h`, `network/http.cpp`): new virtual (default returns `name`) overridden by `HTTPMFile` to return the server's `Content-Disposition` filename when present, WITHOUT reassigning `name` (which must stay in sync with `path`). Used by the `wget` console command. `Content-Disposition` is captured only in `contentDispositionFilename`, never applied to `client.url` (only a real `Location` redirect updates the URL), so a server-supplied filename is only recoverable via this accessor.

### Recent Changes (March 12, 2026)

- **Chunked transfer encoding support** (`network/http.h`, `network/http.cpp`):
  - `HTTP_EVENT_ON_DATA`: accumulates `meatClient->_size += evt->data_len` for chunked responses
  - `HTTPMStream::available()` override: returns `HTTP_BLOCK_SIZE` hint when `isOpen() && !complete()` so the drive keeps reading after `_size == _position`; returns 0 only when `complete()` is true
  - `HTTPMStream::read()`: syncs `_size = _position` after each read for chunked tracking; only caps by `available()` when size is known (`avail > 0`)
  - `MeatHttpClient::read()`: `openAndFetchHeaders` now guarded by `bytesRead > 0 && !chunked` — prevents restarting the request on EOF (bytesRead=0) or for chunked responses where partial reads are buffering artefacts

## MDNS (Network Service Discovery) Protocol

The MDNS implementation provides automatic discovery of network services using DNS-SD (DNS Service Discovery) via mDNS (Multicast DNS), SSDP (Simple Service Discovery Protocol), and DIAL (Discovery and Launch) protocols. This enables Meatloaf to discover and connect to network services without manual configuration.

### Components

- **`MDNSMSession`** (`service/mdns.h` lines 61-87): Manages service discovery sessions
  - Extends `MSession` for connection lifecycle management
  - Uses ESP-IDF mDNS library for DNS-SD service discovery
  - Maintains cache of discovered services with mutex protection
  - Supports filtered discovery by service type (e.g., _http._tcp, _smb._tcp)
  - Session pooling via `SessionBroker` (uses dummy "mdns" host since discovery is local)

- **`DiscoveredService`** (`service/mdns.h` lines 33-54): Service metadata structure
  - Contains instance_name, service_type, proto, hostname, port
  - Stores IP addresses (both IPv4 and IPv6)
  - Holds TXT records (key-value pairs) for service-specific metadata
  - Provides getKey() for unique identification and getDisplayName() for user-friendly names

- **`MDNSMFile`** (`service/mdns.h` lines 92-131): Directory entry for discovered services
  - Extends `MFile` for file metadata and directory iteration
  - Supports listing all discovered services or filtering by type
  - Uses URL format: `mdns://[service_type]/[instance_name]`
  - Implements getNextFileInDir() for directory-style browsing of services

- **`MDNSMStream`** (`service/mdns.h` lines 136-173): Stream for service information
  - Extends `MStream` for reading service details
  - Generates formatted text output with service information
  - Displays instance name, hostname, port, IP addresses, and TXT records
  - Non-browsable, non-writable (read-only service information)

- **`MDNSMFileSystem`** (`service/mdns.h` lines 178-197): Filesystem factory
  - Extends `MFileSystem` for filesystem registration
  - Handles "mdns://" URL scheme
  - Creates MDNSMFile instances for service discovery operations

### Path Format

```
mdns://                          List all discovered services
mdns://[service_type]/           List services of specific type
mdns://[service_type]/[instance] Get info about specific service instance
```

- **service_type**: Optional mDNS service type (e.g., `_http._tcp`, `_smb._tcp`, `_ssh._tcp`)
  - When empty: discover all available services
  - Format: `_service._proto` (e.g., `_http._tcp`, `_printer._tcp`)
- **instance**: Optional service instance name or hostname
  - When empty: list all instances of the service type
  - When specified: show detailed information about that service

### Examples

- `mdns://` → List all discovered services on the network
- `mdns://_http._tcp/` → List all HTTP services
- `mdns://_http._tcp/MyWebServer` → Get detailed info about "MyWebServer" HTTP service
- `mdns://_smb._tcp/` → List all SMB/CIFS file shares
- `mdns://_ssh._tcp/raspberrypi` → Get SSH service details for Raspberry Pi

### API Requirements

- **mdns.h**: ESP-IDF mDNS library
  - `mdns_init()`: Initialize mDNS service
  - `mdns_query_ptr()`: Query for services by type
  - `mdns_result_t`: Structure containing discovered service information
  - `mdns_query_results_free()`: Free result structures
  - Supports both IPv4 and IPv6 addresses
  - Parses TXT records for service metadata

### Integration Points

- **meatloaf.cpp**: Registered in `availableFS` vector as `mdnsFS`
- **SessionBroker**: Manages MDNS session pooling (uses "mdns" as dummy host identifier)
- **MFileSystem registry**: Recognizes "mdns://" URLs and routes to MDNSMFileSystem

### Discovery Process

**Service Query Flow**:
1. `MDNSMFile::rewindDirectory()` calls `refreshServiceList()`
2. `refreshServiceList()` calls `MDNSMSession::discoverServices()`
3. `discoverServices()` uses `mdns_query_ptr()` to query mDNS network
4. Results parsed by `parseResults()` and cached in `discovered_services` vector
5. Cache protected by mutex for thread-safe access
6. `getNextFileInDir()` iterates cached results

**Service Information**:
- Each discovered service includes:
  - Instance name (friendly name)
  - Service type and protocol
  - Hostname and port
  - IP addresses (v4 and v6)
  - TXT records (metadata)
  - TTL (time to live)

### Supported Service Types

Common service types discoverable via DNS-SD:
- `_http._tcp` - HTTP web servers
- `_https._tcp` - HTTPS secure web servers
- `_smb._tcp` - SMB/CIFS file shares
- `_afpovertcp._tcp` - Apple Filing Protocol
- `_ftp._tcp` - FTP file servers
- `_sftp-ssh._tcp` - SSH File Transfer Protocol
- `_ssh._tcp` - SSH secure shell
- `_printer._tcp` - Network printers
- `_ipp._tcp` - Internet Printing Protocol
- `_airplay._tcp` - AirPlay devices
- `_googlecast._tcp` - Chromecast devices

### Session Management

**Connection Lifecycle**:
- `connect()`: Initializes mDNS service with `mdns_init()`
- `disconnect()`: Frees mDNS resources with `mdns_free()`
- `keep_alive()`: No-op for MDNS (discovery is stateless)
- `clearCache()`: Clears cached discovered services

**Thread Safety**:
- Mutex protection for `discovered_services` vector
- Safe concurrent access from multiple files/streams
- Session shared across all MDNS operations via SessionBroker

### Output Format

**Directory Listing** (when browsing `mdns://` or `mdns://_type._proto/`):
- Each service appears as a file entry
- Filename is the service instance name or hostname
- Can be opened to read detailed service information

**Service Details** (when reading `mdns://_type._proto/instance`):
```
Service Details: MyWebServer
==========================================

Instance Name: MyWebServer
Service Type:  _http._tcp
Hostname:      webserver.local
Port:          8080
TTL:           120 seconds

IP Addresses:
  192.168.1.100
  fe80::1234:5678:90ab:cdef

TXT Records:
  path = /
  version = 1.0
```

### Recent Changes (March 8, 2026)

- **`NFSMSession` (`network/nfs.h`)**: Set `keep_alive_interval = 0` in constructor to disable SessionBroker keep-alive. NFSv3 is stateless — the server holds no session state between RPCs, so periodic pings are unnecessary. More importantly, the keep-alive was racing with the IEC task (core 1) doing `seekFileSize()` block-chain traversal via libnfs — concurrent socket access on the same `nfs_context` from two cores corrupted the heap, overwriting the SPI3 bus lock's interrupt handle with `0x6400` and crashing the display task.
- **`MMediaStream::seekFileSize()` (`meat_media.cpp`)**: Commented out `console.printf()` inside the block-chain loop (and changed post-loop size print to `Debug_printv`). With `ENABLE_CONSOLE_TCP` defined, `console.printf()` calls `tcp_server.send()` → `write(socket, ...)` — a LwIP TCP socket write from the IEC task (core 1) with no synchronization. Hundreds of TCP sends per `seekFileSize()` call competed with NFS socket reads, causing the same `0x6400` heap corruption pattern. The comment in the code already said "Leave commented unless used for debugging" but it was not followed.

### Recent Changes (February 7, 2026)

- Fixed mDNS PTR record parsing for meta-queries (`_services._dns-sd._udp`)
- Corrected instance name construction from PTR data responses
- Implemented proper service type discovery without "Stage 2" instance querying
- Added thread-safe caching of discovered service types
- Updated directory listing to show service types with protocols (e.g., `_http._tcp`)
- Fixed ESP-IDF mDNS search matching logic for DNS-SD meta-queries

### Known Limitations

- Only DNS-SD via mDNS currently implemented (SSDP and DIAL planned)
- Service cache does not auto-refresh (requires manual refresh via rewindDirectory)
- No wildcard filtering (must specify exact service type or discover all)
- Limited to services advertising on local network segment
- TXT record parsing depends on service implementation
- No support for service registration (discovery only)

### Future Enhancements

- SSDP (Simple Service Discovery Protocol) for UPnP devices
- DIAL (Discovery and Launch) for smart TV discovery
- WSD (Web Service for Devices)
- Automatic cache refresh with configurable TTL
- Service filtering by TXT record values
- Integration with mDNS service advertising (Meatloaf as discoverable service)

## MQTT (Message Queuing Telemetry Transport) Protocol

The MQTT implementation provides pub/sub messaging capabilities through the Meatloaf filesystem abstraction, allowing files to be read from and written to MQTT topics.

### Components

- **`MQTTMSession`** (`service/mqtt.h` lines 49-120): Manages MQTT client connections per broker (host:port)
  - Extends `MSession` for connection lifecycle management
  - Handles ESP-IDF MQTT client initialization and event callbacks
  - Maintains message queues for pub/sub operations
  - Supports keep-alive and automatic reconnection

- **`SessionBroker`**: Reuses MQTT sessions across files/streams by broker host:port

- **`MQTTMFile`** (`service/mqtt.h` lines 122-180): File abstraction for MQTT topic paths
  - Obtains session from SessionBroker
  - Represents MQTT topics as filesystem entries
  - Supports directory listing (topic enumeration not implemented - topics are virtual)

- **`MQTTMStream`** (`service/mqtt.h` lines 182-280): Stream for MQTT message I/O
  - Obtains session from SessionBroker
  - `read()`: Subscribes to topic and receives messages
  - `write()`: Publishes messages to topic
  - Supports QoS levels and retained messages
  - Message queuing for asynchronous pub/sub operations

- **`MQTTMFileSystem`** (`service/mqtt.h` lines 282-300): Factory for MQTT filesystem
  - Handles URLs starting with `mqtt://`
  - Creates MQTTMFile instances for topic access

### Path Format

- Topic access: `mqtt://broker[:port]/topic` → read/write messages to/from topic
- Default port: 1883 (MQTT), 8883 (MQTTS)
- Examples:
  - `mqtt://test.mosquitto.org/sensor/temperature` → read temperature sensor data
  - `mqtt://broker.local:1883/home/status` → publish home status updates

### ESP-IDF MQTT Integration

**Event-Driven Architecture**:
- Uses `esp_mqtt_client` with event loop callbacks
- Handles connection, disconnection, subscription, and message events
- Message queuing prevents blocking operations
- Thread-safe message handling with mutex protection

**QoS Support**:
- QoS 0: At most once (fire and forget)
- QoS 1: At least once (acknowledged delivery)
- QoS 2: Exactly once (highest reliability)

**Security**:
- Supports TLS/SSL for secure connections (MQTTS)
- Username/password authentication
- Client certificates (future enhancement)

### Message Handling

**Publishing** (`write()` operation):
- Messages written to stream are published to the topic
- Supports retained messages for persistent state
- Configurable QoS level

**Subscribing** (`read()` operation):
- Stream subscribes to topic on first read
- Messages received asynchronously via event callbacks
- Buffered for sequential read operations
- Supports wildcard subscriptions (#, +)

### Session Management

**Connection Lifecycle**:
- `connect()`: Initializes ESP-IDF MQTT client and establishes connection
- `disconnect()`: Closes MQTT connection and cleans up resources
- `keep_alive()`: Sends ping to maintain connection (handled by ESP-IDF client)

**Automatic Reconnection**:
- ESP-IDF client handles reconnection automatically
- Session remains valid across network interruptions
- Message queues maintain state during disconnections

### Usage Examples

```cpp
// Publish a message to a topic
auto file = MFSOwner::File("mqtt://broker/sensor/temperature");
auto stream = file->getSourceStream();
std::string message = "25.5";
stream->write((uint8_t*)message.c_str(), message.length());

// Subscribe and read messages from a topic
auto file = MFSOwner::File("mqtt://broker/sensor/#");
auto stream = file->getSourceStream();
uint8_t buffer[256];
size_t bytes = stream->read(buffer, sizeof(buffer));
// Process received MQTT messages
```

### Integration with Meatloaf

**Filesystem Registration**:
- `MQTTMFileSystem` registered in `MFSOwner::availableFS`
- URLs starting with `mqtt://` automatically routed to MQTT filesystem
- Works with nested streams (e.g., MQTT over secure connections)

**Stream Capabilities**:
- `isRandomAccess()`: false (MQTT is message-oriented, not seekable)
- `isBrowsable()`: false (topics are virtual, no directory structure)
- `size()`: Returns size of next available message
- `available()`: Returns number of queued messages

### Recent Changes (Jan 23, 2026)

- Implemented complete MQTT client using ESP-IDF MQTT library
- Added session-based connection pooling via SessionBroker
- Integrated pub/sub messaging with MStream interface
- Fixed ESP-IDF MQTT API compatibility issues (removed unavailable `esp_mqtt_client_get_state()`)
- Resolved ESP32 POSIX compatibility issues in fsplib.c for clean compilation
- Verified successful build and integration with Meatloaf filesystem

### Known Limitations

- Topic enumeration not implemented (topics are virtual filesystem entries)
- No support for MQTT v5 features (retained only MQTT v3.1.1)
- Message persistence limited by ESP32 memory constraints
- Wildcard subscriptions may impact performance with high message volumes

## RetroPixels Component Integration

**RetroPixels** is a C++ image-to-C64 graphics converter integrated as an ESP-IDF component and MStream codec.

**Component Structure** (`components/retropixels/`):
- **CMakeLists.txt**: ESP-IDF component definition listing all source files
- **Source modules**:
  - `conversion/`: Converter, Quantizer, OrderedDither - core conversion logic
  - `io/`: Format readers/writers - KoalaPicture, ArtStudioPicture, FLIPicture, AFLIPicture, SpritePad
  - `model/`: Data structures - IImageData, PixelImage, ColorMap, GraphicMode, Palette
  - `prepost/`: Image loading/scaling - uses stb_image for reading PNG/JPG/etc
  - `profiles/`: C64 format definitions - GraphicModes, Palettes, ColorSpaces
- **Dependencies**: STB image libraries (stb_image.h, stb_image_resize2.h) in `include/`

**MStream Codec** (`lib/meatloaf/codec/retropixels.h/cpp`):
- **RetroPixelsMStream**: Wraps any image-bearing MStream (HTTP, NFS, SMB, Flash, etc.)
  - Constructor takes source stream + configuration
  - Lazy conversion: Image converted on first `size()` or `read()` call
  - Output buffered in memory for subsequent reads
  - `write()` method: Accepts URL string to load and convert image from any source
  - Supports: Koala (.kla), Art Studio (.art), FLI (.fli), AFLI (.afli), SpritePad (.spd)
  
- **RetroPixelsMFile**: File wrapper that creates RetroPixelsMStream from source
  - Registered with scheme `retropixels:`
  - Parses configuration from URL fragment (after `#`) to avoid conflicts with target URL query strings
  - Example: `retropixels:http://server/photo.jpg#format=koala&palette=colodore&dither=bayer4x4`
  - Example with query: `retropixels:http://server/photo.jpg?size=large#format=koala&palette=colodore`

- **RetroPixelsMFileSystem**: Factory for creating RetroPixelsMFile instances
  - Handles URLs starting with `retropixels:`
  - Registered in `MFSOwner::availableOther` (codec category)
  - Auto-configured from URL fragment parameters

**Configuration** (RetroPixelsConfig):
- `format`: KOALA, ARTSTUDIO, FLI, AFLI, SPRITEPAD, PRG
- `palette`: PALETTE, COLODORE, PEPTO, DEEKAY (C64 color palettes)
- `dither`: NONE, BAYER2X2, BAYER4X4, BAYER8X8
- `scale`: NONE, FILL, FIT
- `outputPrg`: Boolean - wrap output in executable PRG with viewer
- `viewerPath`: String - path to viewer binaries (default: `/components/retropixels/bin/target/c64/`)

**URL Fragment Parameters** (after `#`):
- `format=koala|artstudio|art|fli|afli|spritepad|spd|prg`
  - When `format=prg`, creates executable PRG (wraps koala by default)
  - Use `baseformat=` to specify underlying format for PRG (koala/fli/afli/artstudio)
- `palette=palette|colodore|pepto|deekay`
- `dither=none|bayer2x2|bayer4x4|bayer8x8`
- `scale=none|fill|fit`

Note: Using fragment (`#`) instead of query string (`?`) prevents conflicts with target URL parameters.

**PRG Format** (Executable with Viewer):
- Combines viewer program with graphics data into single executable
- Viewer binaries located in `components/retropixels/bin/target/c64/`
- Supported viewers: koala.prg, artstudio.prg, fli.prg, afli.prg
- Output is ready-to-run PRG that displays the image on C64
- Example: `retropixels:http://server/photo.jpg#format=prg` creates koala viewer + data
- Example: `retropixels:http://server/photo.jpg#format=prg&baseformat=fli` creates FLI viewer + data

**Conversion Pipeline**:
1. Source image read from wrapped MStream (any protocol/container)
2. Decoded with stb_image (PNG, JPG, BMP, etc.)
3. Loaded into IImageData (RGBA pixel buffer)
4. Quantized to C64 palette with optional dithering
5. Converted to PixelImage with ColorMaps (format-specific)
6. Encoded to binary format (.kla, .art, etc.)
7. Output buffered for MStream read operations

**Usage Pattern**:
```cpp
// Method 1: Direct stream wrapping (programmatic)
auto httpStream = MFSOwner::File("http://server/image.png")->getSourceStream();
RetroPixelsConfig config;
config.format = RetroPixelsFormat::KOALA;
config.palette = RetroPixelsPalette::COLODORE;
config.dither = RetroPixelsDither::BAYER4X4;
auto retroStream = std::make_shared<RetroPixelsMStream>(httpStream, config);
uint8_t buffer[10003];
retroStream->read(buffer, 10003);

// Method 2: Using retropixels: scheme with fragment parameters
// Fragment (#) doesn't conflict with target URL query params (?size=large)
auto file = MFSOwner::File("retropixels:http://server/image.png?size=large#format=koala&palette=colodore&dither=bayer4x4");
auto stream = file->getSourceStream();
stream->read(buffer, 10003);

// Method 3: Write URL to stream for dynamic conversion
auto retroStream = std::make_shared<RetroPixelsMStream>(nullptr);
std::string url = "http://server/image.png#format=koala&palette=colodore";
retroStream->write((uint8_t*)url.c_str(), url.length());
retroStream->read(buffer, 10003);
```

**Technical Details**:
- Thread-safe: Conversion done once, output cached
- Memory efficient: Source image freed after conversion
- Format detection: Uses stb_image (automatic format detection)
- Error handling: Returns 0 size on conversion failure
- **URL scheme**: `retropixels:` registered in factory (availableOther)
- **Dynamic loading**: `write()` method accepts URLs for on-demand source changes
- **Fragment parsing**: Automatic configuration from URL fragment (after `#`, case-insensitive)
- **No query conflicts**: Configuration in fragment preserves target URL query strings intact
- **Integration points**: 
  - Direct instantiation with MStream wrapper
  - Factory pattern via `retropixels:` URLs
  - Dynamic URL loading via `write()` method
