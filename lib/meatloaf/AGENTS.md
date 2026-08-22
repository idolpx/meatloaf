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
│   ├── disk/              # Disk images: d64, d71, d80, d81, d82, d90, g64, g71, g81, m2i, nib/nb2/nbz, p64, p81 (mfm.h shared by g81+p81)
│   ├── archive/           # Archives: zip, rar, tar, lbr, ark, arc/sda
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

## Recent Changes (August 22, 2026)

### SPY containers, and Wraptor's LZSS

Two read-only filesystems: `spyFS` (`media/archive/spy.h/.cpp`, `.spy`) and `wraFS`
(`media/archive/wra.h/.cpp`, `.wra` + `.wr3`). Both are registered only under
`EXTRA_DISK_FORMATS`, for the flash-text reason the ARC entry above describes.

- **SPY is LNX's geometry with a different directory.** Files stored uncompressed, each a whole
  number of 254-byte blocks; 15 blocks of self-extracting code, then the central directory, then the
  data. It inherits every 254-vs-256 hazard LNX had, and `spy.cpp` names the constant once and uses
  it in all three places that need it.
- **The eighth directory entry in each block is 30 bytes, not 32.** `7 * 32 + 30 = 254`: the two
  filler bytes at the end of an entry are simply not written when they would cross the block
  boundary. So the entry offset is `dir_start + (i / 8) * 254 + (i % 8) * 32`, and a flat `i * 32`
  drifts two bytes per block from the ninth entry on. `test_ninth_entry_starts_at_the_next_directory_block`
  pins it by reading the name field out of the container directly and comparing.
- **A SPYne has no signature at all.** `readHeader()` instead requires the first directory entry to
  be structurally one — a CBM type of $81-$83 and a last-file marker of $00 or $FF. The $02A7 load
  address of the extraction code is checked only to log a mismatch; a variant that loaded elsewhere
  would still be read.
- **Wraptor's compression is LZSS with a variable-width code, and two of its details are easy to get
  backwards.** Bits are consumed MSB-first. The dictionary offset is an ABSOLUTE, one-based index
  into a 32768-byte output window that WRAPS — `window[(offset - 1 + i) % 32768]` — not a distance
  back from the write position, and not a sliding window. A code of 0 escapes to end-of-stream or to
  "widen by one bit" (from 8). Both were established from the format document's own worked example,
  which decodes to a valid PRG load address and BASIC link chain one way and to nothing the other.
- **The 32 KB window is heap-allocated**, `malloc` + NULL check + placement new, per the rule the
  ARC entry documents: a stack frame is reserved on function entry, so a 32 KB local faults before
  the decoder runs. With the compressed span and the full output alongside it, decoding one entry
  costs ~100 KB transient — WRA is a PSRAM-board feature.
- **A GEOS payload keeps Wraptor's nine-byte header.** Nothing here reconstructs a GEOS file, which
  matches the codebase's existing position (a VLIR file already reads back as its index block out of
  a D64). Those nine bytes are documented in `wra.h` and in the test's header comment so a later
  revisit is cheap, and the read path does not branch on them.
- **The `.wra`/`.wr3` split needs no code.** It is a GEOS-reconstruction bug-fix lineage only; one
  class handles both, and the corpus's single `.wra` decodes with the same decoder as its six
  `.wr3`s.
- **Host tests**: `test/native/test_spy_read/` (9 cases) and `test/native/test_wra_read/`
  (12 cases), both against real corpora in `.archive/archive/spy` and `.archive/archive/wr3` and
  both skipping cleanly without them. SPY checks all 67 entries against the checksum each directory
  entry stores. WRA has no usable checksum — the format gives no CRC algorithm — so it checks all 28
  entries against the GEOS header's own redundancy and block count, plus byte-identical decoding of
  the same entry out of three independently built archives. **Termination is necessary but not
  sufficient**: an LSB-first bit reader still terminates cleanly on every entry and simply produces
  the wrong bytes, which is why the reconciliation is the load-bearing case and why the suite's
  header says so.

## Recent Changes (August 17, 2026)

### ARC/SDA archives, and a flash-text ceiling that now gates the newer formats

New read-only filesystem `arcFS` (`media/archive/arc.h/.cpp`) for `.arc` and `.sda`, ported
from cbmconvert 2.1.2's `unarc.c` — the reference implementation, and the one the format's
own documentation points at.

- **ARC is not one format but six.** An entry is stored, run-length packed, Huffman
  squeezed, LZW crunched, squeezed AND packed, or crunched in a single pass with its size
  and checksum written at the END of the stream rather than in the header. All six are
  implemented; the corpus exercises five (0-4).
- **Run-length decoding sits ON TOP of the byte producer** for every mode except stored and
  squeezed, so it is applied to whatever Huffman or LZW hands back rather than to the
  container bytes. Getting that layering backwards yields plausible-looking garbage.
- **The directory walk never touches compressed data.** The next entry's header sits exactly
  `blocks * 254` bytes past this one, so listing an archive costs one small read per entry
  and never builds a Huffman table. Only `seekPath()` decompresses, and it does the whole
  entry at once into RAM — streaming it would mean carrying Huffman and LZW state across
  `readFile()` calls for no gain, since the drive reads a file start to end anyway.
- **The decompressors read from a RAM buffer, not the container.** The reference walks the
  file a BIT at a time through stdio; doing that against an MStream is a read call per bit,
  which over a network is hopeless. The entry's compressed bytes are fetched once - plus
  512 bytes of slack, because the reference reads the container as one continuous stream and
  a decoder can legitimately need a few bits past its own block count.
- **`readFile()` must READ `_position` and never write it.** `MMediaStream::read()` advances
  it by whatever `readFile()` returns, so advancing it inside as well moves it twice per call
  and the entry ends at half its length. This is the mirror image of the g64/nib defect,
  where a sector buffer was indexed BY `_position`; the two mistakes look alike and are
  opposite.
- **An `.sda` is the same archive behind a BASIC loader.** `skipBasicLoader()` steps over it:
  the loader's BASIC line number doubles as the count of 254-byte blocks the preamble
  occupies, so the archive starts at `(line - 6) * 254`, less one byte for the SDA232.128
  special case the reference documents.
- **Two entries in the corpus are damaged, and the test names them.** `pcpfonts.arc`'s
  `PRINCETON24 P.24` and `RUTGERS24 PD .24` each declare 3757 bytes while their LZW stream
  reaches its own end marker after 3756. What rules out an off-by-one in the port is the
  same archive: 43 of its other 43 mode-3 entries decode with correct checksums, and both a
  decoder that dropped a final byte and an archiver with a bad size field would have got all
  of them wrong. cbmconvert stops on end-of-stream and reports a checksum error too.
- **Host tests**: `test/native/test_arc_read/` — 6 cases over the real corpus in
  `.archive/archive/arc` and `.archive/archive/sda`: 84 entries across 9 archives plus an
  SDA. The checksum is what makes this worth anything - it is computed over the
  DECOMPRESSED bytes, so an entry that comes out the right length but subtly wrong fails it,
  which nothing else here could detect.

**`EXTRA_DISK_FORMATS` now gates the newer formats.** Registering `.arc`, `.g71`, `.g81` and
`.p81` alongside everything else puts a plain ESP32 board about 5 KB over its ~3.3 MB
`iram0_2_seg` flash-text window — the constraint the board survey in the 2026-08-12 entry
describes. They are compiled but only registered in `meatloaf.cpp` when the flag is defined,
which `platformio.ini.sample` sets for `esp32-s3-devkitc-1` and not for `lolin-d32-pro`.
`.p64`, `.nib`/`.nb2`/`.nbz` and `.g64` are always registered. **`#pragma GCC optimize("Os")`
was tried first and is NOT the answer here** — on `d64.cpp` and `archive.cpp` it made the
segment 1.8 KB LARGER, because the pragma replaces the build's optimization options rather
than adding to them. That is the opposite of its effect on the C components in
`components/`, where it is applied to a whole library that the PlatformIO builder was
otherwise compiling at the project's global level.

### NIB read path rewritten; NB2 and NBZ actually supported

`nib.cpp` was a near-copy of the old `g64.cpp` and carried every defect that one did, plus
two of its own. `.nb2` and `.nbz` were already listed in `handles()` but nothing in the code
told them apart, so they were read as plain `.nib` and came out wrong.

- **`readContainer()` indexed `sector_buffer` by `_position`** — the same defect g64 had, with
  the same consequence: from the second block of any file onward it read past the buffer and
  returned adjacent memory as file content. Fixed with a bounds-checked `sector_pos` cursor.
- **The track-table search was unbounded**: `do { read 2 bytes; index++ } while (wanted !=
  found && found != 0)`. A track the table does not list walked off the end of the header and
  on through the track data with nothing to stop it. It is now a bounded loop over at most 84
  entries, parsed once in `parseHeader()` into an offset table.
- **The sector search called `readSectorHeader()` before finding any sync at all**, and
  looped on a flag that a failing `findSync()` could leave set. Rewritten as the bounded
  sync → header → validate → data scan g64 now uses.
- **`readSector()` returned true when the data block id was not `$07`**, leaving the previous
  sector's bytes to be served as this one's; and the header checksum was computed and thrown
  away, existing only to be `printf`'d. Both fixed, and the `printf`s and the per-sector
  `util_dump_bytes()` are gone — they ran on the IEC task.
- **`findSync()` read ONE BYTE AT A TIME straight from the container.** Over a network that
  is thousands of range requests per sector. The track window is now pulled into RAM once
  (`loadTrack()`, cached) and scanned there, which is the same shape g81/p81 use.
- **A NIB track has NO length field**, unlike a `.g64` which stores one per track. The window
  is a fixed 8192 bytes and whatever the nibbler did not fill is padding, so the whole window
  is searchable and the header checksum is what rejects the junk. Do not invent a length.
- **`.nb2` needs no format knowledge — the stride is DERIVED.** A `.nb2` holds several passes
  of each track one after another and nothing in the header says how many, so
  `parseHeader()` computes `(size - 0x100) / entries`, rounds it to a whole number of track
  windows and refuses anything that does not divide evenly. One reader handles both, and
  only the first pass is used. Verified with a 4-pass fixture that reads byte-identically to
  the 1-pass one.
- **`.nbz` is a compressed NIB, and it is NOT gzip.** This was first guessed at and then
  settled by measuring the corpus in `.archive/disk/nbz`: a real `.nbz` begins with a
  leading `$05`, then `MNIB-1541-RAW` **one byte in** - which is exactly what
  `MFileSystem::byContent()` has always claimed - then a version byte of 3, then a perfectly
  ordinary track table. What follows the table is compressed to variable lengths:
  `ghostbusters[...].nbz` holds 40 tracks in 70660 bytes, about 1766 each against a plain
  track's 8192. The per-track compression is not implemented, so the container is RECOGNISED
  AND REFUSED with a message saying so, rather than half-read - its table parses fine and
  every track behind it would be garbage.
- **A gzip-compressed `.nib` is still handled**, since that is a thing people make and it
  costs almost nothing: a gzip magic number means inflate the whole thing (gzip has no random
  access) into a buffer capped at what a `.nib` can legitimately be, `0x100 + 84 * 0x2000`,
  so a compressed `.nb2` is refused rather than exhausting the heap. That branch is NOT what
  a real `.nbz` is.
- **zlib needed an include path for the native tests.** `build_libarchive.py` already folds
  zlib into the single archive lib it prepends for every native suite, so the objects link —
  only the headers were missing. `-I components/zlib/zlib` added to `[env:native]` in both
  `platformio.ini` and `platformio.ini.sample`.
- **Host tests**: `test/native/test_nib_read/` — 11 cases over fixtures generated from a real
  `.d64` by `host/make_nib.py` (which writes `.nib` and, with `-p N`, a multi-pass `.nb2`).
  Every block is compared against that `.d64`. As with g64, **most of the suite would pass
  with the old `readContainer()`** — only `test_reading_a_file_through_the_stream` drives the
  path where `_position` advances. Verified by reverting the fix: 6 of the 12 fail.
  Two further cases run against the REAL corpus that now sits in `.archive/disk/nib` (41
  nibbler dumps of commercial disks) and `.archive/disk/nbz`: all 41 real images produce an
  exact whole-track stride, four of five sampled protected originals still give up a CBM
  disk header on track 18 (the fifth is the media, not the reader), and a real `.nbz` is
  refused cleanly.

### G71 and G81, and an MFM layer shared with P81

Two new read-only filesystems, `g71FS` (`media/disk/g71.h`) and `g81FS`
(`media/disk/g81.h/.cpp`). They are much less alike than their names suggest.

- **`.g71` is a `.g64` with a different signature and a 1571's geometry, and nothing else.**
  A 1571 in double-sided mode is two 1541 surfaces written by the same logic at the same
  four speed zones, so `G71MStream` is `G64MStream` plus the `D71MStream` geometry block, a
  `speedZone()` that repeats the progression for tracks 36-70, and `imageSignature()`.
- **Track numbering is FLAT, and this is the one thing that could have been otherwise.**
  Track N is at half track `N * 2` for all 70 tracks, so side 2 (tracks 36-70) needs no
  per-side base and `G64MStream::seekSector()` reads it unchanged. Confirmed from VICE
  rather than assumed: `fsimage-gcr.c` indexes the offset table with `half_track - 2` and
  derives the track back as `half_track / 2` for every image type, with the table sized
  `MAX_GCR_TRACKS = 168 = 84 * 2`.
- **`.g81` is a different format wearing a similar name**: MFM, not GCR. Its container is a
  G64 with three changes — the `MFM-1581` signature, no speed zone table, and a four-byte
  per-track length counting **bits** rather than a two-byte byte count. The bitstream it
  holds is exactly what a `.p81` produces from flux, so everything downstream is shared.
- **`mfm.h/.cpp` is that shared half** — `findSync`, `readBytes`, `crc16`, `readSector`,
  pure functions over `(const uint8_t *bits, uint32_t bytes, …)`. `P81MStream` was
  refactored onto it (`p81.cpp` went from ~360 lines to 204) and its 10 tests re-run
  unchanged before any G81 code was written. **The 1581 logical-to-physical mapping is
  deliberately NOT in there**: `head = block / 20`, `sector = ((block % 20) / 2) + 1` is
  drive geometry, and each container's own head order is its own affair — the `.p81` side
  bit is inverted, which is a P64 quirk and not a 1581 property. Keeping the mapping at each
  call site keeps those assumptions visible where they are made.
- **G81's container layout is UNVERIFIED and must be described that way.** There is no
  `.g81` in `.archive`, VICE has no MFM-1581 support at all, and the P64 reference
  implementation does not know the format; the four-line note at the top of `g81.h` is the
  entire specification. Two of its claims cannot be checked — where the track data starts
  (which follows from "no speed zone table" but is not stated) and the four-byte prefix —
  because `host/make_g81.py` encodes the same reading that `g81.cpp` decodes, so a green
  test proves only that the two agree. The bit-vs-byte claim IS weakly self-checking: the
  generator emits ~101000 cells per track, the right order of magnitude for a 100000-cell
  1581 rotation, where a byte reading would give ~12500 and find no sectors. If a real
  `.g81` ever appears and the base offset is wrong, every track fails at once, which is the
  right failure mode. **The MFM layer underneath is NOT in this caveat** — it is validated
  against a real 1581 flux image by the P81 suite, and G81 reuses it rather than copying it.
- **Fixtures**: `test/native/test_g64_read/host/make_g64.py` now emits a `.g71` too (it
  switches on the output extension), and `test/native/test_g81_read/host/make_g81.py` is a
  full MFM encoder. Both source images are synthesized with every block carrying a pattern
  derived from its own track and sector, so a block served from the wrong place is caught by
  its CONTENT — a CRC would pass either way.
- **Also fixed while in `g64.h`**: `readHeader()` read `gcr_header` from wherever the stream
  happened to be left AFTER delegating to `D64MStream::readHeader()`, rather than from
  offset 0, so the values it logged were never the header's. It now reads and validates the
  signature FIRST — the same ordering trap p64 had — and `writeContainer()` refuses writes,
  which a GCR container needs for the same reason p64 does: `D64MStream`'s write path
  addresses it as a linear `.d64` and `MFile::isWritable` inherits true from the SD card.

### P81 — the same container, a completely different physical format

New read-only filesystem `p81FS` (`media/disk/p81.h/.cpp`) for `.p81`, deriving from
`P64MStream`. The container half really is shared and really is inherited — header, chunk
stream, range decoder, rotation-seam overlap replay, track cache. **Everything downstream
of the pulses is different**, and calling this a geometry override would be wrong:

| | .p64 (1541) | .p81 (1581) |
|---|---|---|
| encoding | GCR | MFM |
| read logic | 1541 clock/counter, per speed zone | fixed 2 µs cell |
| sync | ten or more 1 bits | `$4489` (an `$A1` with a missing clock bit) ×3 |
| integrity | XOR checksum | CRC-16/CCITT |
| sides | one | two |
| stepping | half tracks, `ht = track * 2` | whole cylinders, `cylinder = ht - 2` |
| speed zones | four | one |
| sector | 256 bytes | 512 bytes, split into two CBM blocks |

- **MFM was established by measurement, not by assumption.** The pulse spacings on
  `td1581.p81` are exactly 4.00, 6.00 and 8.00 µs — the 2T/3T/4T of double-density MFM at a
  2 µs cell (32 samples at the P64 16 MHz clock) — and nothing like the 1541's GCR timing.
  Neither the P64 reference implementation nor VICE decodes a P64-1581 at all (both check
  for the literal `P64-1541` signature), so there is no upstream to copy from.
- **The P64 side bit is INVERTED relative to the head the address marks report.** Side 0 of
  the image carries head 1 and side 1 carries head 0, on every chunk of every cylinder.
  This is the one thing here that will silently produce plausible-but-wrong data if it is
  "corrected": both surfaces decode cleanly, so only a block chain or a directory notices.
- **Mapping**: CBM track 1-80 → cylinder `track - 1`; block 0-39 → head `block / 20`,
  physical sector `((block % 20) / 2) + 1`, and the low or high 256-byte half by `block % 2`.
  The chunk key is `(side << 7) | (cylinder + 2)` and the cache id is `cylinder * 2 + head`
  — the two heads of a cylinder are different chunks and must not share a decoded buffer.
- **The MFM buffer is much bigger than the GCR one**: a rotation is 3200000 / 32 = 100000
  cells = 12500 bytes, against a 1541's ~7100. `trackBufferBytes()`/`overlapBytes()` are
  virtual for this reason, and the overlap is 2048 bytes because it has to exceed a whole
  512-byte MFM sector (~1150 bytes of cells), where 512 sufficed for GCR.
- **What P64MStream had to expose to make this work**: `imageSignature()`, `chunkKeyFor()`,
  `trackBufferBytes()`, `overlapBytes()`, `resetEmitState()`, `emitDelta()` and
  `loadSector()` are now virtual, and the chunk table is keyed on the **raw** HTPx signature
  byte (side in bit 7) instead of the masked half track, so both sides can live in it.
  `decodeChunk(key, cache_id)` is the entry point a two-sided format calls directly.
  `emitDelta()` is the seam: one flux gap in, bits out — the 1541 read logic and "round the
  gap to a cell count" are the two implementations of exactly that.
- **Host tests**: `test/native/test_p81_read/` — 10 cases against the real
  `.archive/disk/p81/td1581.p81`. Two are load-bearing and neither can be dropped:
  `test_every_block_of_every_track_reads` sweeps all 80 cylinders × 40 blocks across both
  heads with the CRC checked on every one, and **`test_file_chains_match_their_directory_block_counts`
  is the only thing that constrains the sector ORDER** — the sweep would pass just as
  happily if the logical-to-physical mapping shuffled blocks within a head, since all 3200
  would still decode and pass CRC, just in the wrong order. A chain walk cannot be fooled
  that way: every link names the next block, so it runs long, runs short or dies.
- **A 1581 directory can hold CBM sub-partition entries (type `$85`)**, and their "blocks"
  field is the SIZE of a partition area, not the length of a chain — `td1581.p81` has one,
  `PIC.DIR`, 400 blocks. Anything walking chains out of a 1581 directory must filter to
  SEQ/PRG/USR; that filter is what the first run of the chain test was missing.

## Recent Changes (August 16, 2026)

### G64 read path: three defects, and a test suite that can reach them

- **`readContainer()` returned memory past its sector buffer for any multi-block file.** It
  indexed `sector_buffer` by `_position`, which is the position in the FILE being read, not
  the offset within the decoded sector. `D64MStream::readFile()` calls `readContainer()`
  repeatedly between `seekSector()` calls, so from the second block on it read past the end
  of a 260-byte buffer and handed back adjacent memory as file content. Fixed the way p64
  does it: a `sector_pos` cursor set by `seekSector()` and bounds-checked against
  `block_size`.
- **`seekSector()` hung on a sector that is not on the track.** Its `do { findSync;
  readSectorHeader; findSync; } while (sector != gcr_sector_header.sector);` ignored
  `findSync()`'s result, and when the sector is absent that loop never terminates:
  `findSync()` seeks BACK to the track end and returns false, so the stream position stops
  advancing, the same bytes are re-read forever. On the IEC task. Now a bounded scan that
  checks every `findSync()` and returns false when the sector is not found.
- **`readSector()` returned true when the data block id was not `$07`**, leaving the
  PREVIOUS sector's bytes in `sector_buffer` for the caller to serve as this one's. It now
  fails, and `seekSector()` propagates that.
- **`readSectorHeader()` computed the header checksum and threw it away** — it existed only
  to be `printf`'d. It now validates the block id and the checksum and returns false on
  either, which is what lets the scan tell a real header from a false sync in a gap.
- **The `printf()`s are gone, not converted to `Debug_printv`.** `readSectorHeader()`
  printed three lines per header and `readSector()` called
  `util_dump_bytes(sector_buffer, 260)` on EVERY sector read. These run from the IEC task,
  where printing at all is the hazard — the same rule the `iecClock` entry documents.
  Removing the dump orphaned `#include "utils.h"`, which went with it.
- **Host tests**: `test/native/test_g64_read/` — 6 cases. There is no `.g64` anywhere in
  `.archive` and the firmware has a GCR decoder but no encoder, so the fixture is generated
  from a real `.d64` by `host/make_g64.py` (checked in; the tests skip cleanly when it has
  not been run). Every decoded block is compared byte for byte against the source `.d64`,
  which is an independent reference for the CONTENT even though the encoding is this
  project's own round trip.
- **`test_reading_a_file_through_the_stream_matches_the_source` is the only one of the six
  that actually reproduces the first defect, and that is worth knowing before trusting the
  others.** The rest call `readContainer()` straight after a `seekSector()`, so `_position`
  never advances and the OLD code passes them. The bug needs the real path —
  `MMediaStream::read()` → `readFile()` → `readContainer()` — where `_position` grows with
  every byte returned. Verified by reverting the fix and re-running: 4 of the 6 fail,
  including this one and the full-disk sweep. Note also that `seekPath()` matches against
  `mstr::toUTF8()` of the entry name, so a test passing the raw directory bytes as a name
  matches nothing — PETSCII `$41-$5A` map to lowercase.

### P64 flux-level disk images (`media/disk/p64.h/.cpp`, new; registered as `p64FS`)

- **A P64 stores flux transitions, not sectors** — so reading one CBM block is three
  decodes stacked: range-coded chunk → pulses → GCR bitstream → sector. `P64MStream`
  derives `D64MStream` and overrides exactly the three things that touch the container:
  `readHeader()` (walk the chunk stream first, THEN delegate — `D64MStream::readHeader()`
  immediately seeks 18/0 and needs the half-track table to already exist; g64.h does this
  in the opposite order, do not copy that), `seekSector()` and `readContainer()`.
- **The pulse list is never materialized.** The reference decodes every pulse into a
  linked list and then walks it in `P64PulseStreamConvertToGCRWithLogic`; a single track
  is ~34k pulses × 16 bytes ≈ 0.5 MB, and a whole image would be ~22 MB. The decoder emits
  positions in increasing order and the GCR logic consumes them in that same order, so the
  two passes are folded into one and each pulse is converted as it decodes. **Do not
  "simplify" this back into two passes.**
- **Only strong pulses (`strength >= 0x80000000`) trigger, and a weak one must NOT update
  `last_position`** — that is what makes it a non-event rather than a shifted one. Weak-bit
  protection tracks therefore decode partially or not at all, which is the reference
  algorithm behaving as specified, not a defect. `zetawingii.p64` reads 18/0 fine and its
  18/1 data block carries id `$4F` instead of `$07`; a release is free to do that.
- **A P64 holds ONE rotation, and its position 0 is not a gap — so the rotation must be
  decoded with an OVERLAP.** Position 0 is wherever the imaging hardware started, and a
  sector straddling it is split across the two ends of the bitstream: ends that do not
  join, because the bit cell phase at 0 has nothing to do with the phase where the rotation
  ran out, and the last cell is truncated. Wrapping the sync scan is NOT enough. After the
  rotation, `decodeTrack()` REPLAYS the pulse stream with every position shifted one
  rotation later, continuing the same read-logic state — what the head sees on the next
  revolution — so the straddling sector is contiguous in the overlap
  (`P64_OVERLAP_BYTES`, 512, comfortably more than a header + gap + data block). The replay
  must restart the range decoder from the start of the chunk with FRESH models: they adapt
  as they decode and cannot be seeked into. It stops after the overlap, so it costs a small
  fraction of the first pass. `last_position` deliberately carries ACROSS the two passes,
  which is what makes the join seamless; the gap it spans is small in practice (measured
  393 samples at most, about six bit cells, over all 46 replays in the corpus) because a
  formatted track has flux transitions all the way round, so the replay cannot start with a
  long spin through the read logic. **Symptom if this is ever undone**: a handful of sectors, each
  with its data block starting past ~95% of the rotation, come back with bad data checksums,
  and a sector whose HEADER is on the seam is not found at all — measured on
  wheels64_4.4a as one bad sector on each of tracks 18, 19, 20 and 23 plus a lost sector on
  17 and 24, with the other 670-odd blocks of the disk perfectly fine. Hardware-confirmed:
  the same file extracted before and after differs in exactly three 254-byte blocks.
- **The GCR bitstream is NOT byte-aligned** — this is the one substantial difference from
  g64, where the container already holds aligned GCR bytes. `findSync()`/`decodeBlock()`
  are bit-resolution ports of VICE's `gcr.c` (`lib/vdrive/gcr.c`), wrapping at the end of
  the track because a track is a loop. **`gcr_read_sector_header()`'s `static int p` in
  VICE is a rotational-position simulation, not state you want** — it is a local here,
  with the loop terminating on returning to the first sync found plus an iteration cap.
- **The track buffer must be cleared before every decode.** Bits are OR-ed in, and with a
  one-track cache the previous track's tail would otherwise survive in the wrap-around
  region a sync scan reads.
- **`readContainer()` needs its own cursor into the decoded sector, NOT `_position`.**
  `_position` is the position in the FILE being read; `D64MStream::readFile()` calls
  `readContainer()` repeatedly between `seekSector()` calls, so indexing a 256-byte sector
  buffer by `_position` walks off the end on any multi-block file. `G64MStream::readContainer()`
  does exactly that and is wrong for files larger than one block.
- **Memory: ~1 MB of probability table per track decode**, transient, plus the compressed
  chunk. The table is `uint16_t` rather than the reference's `uint32_t` (a probability
  lives in 15..4080, so 12 bits is the whole range) which halves it. Allocated with
  `malloc` and NULL-checked, never `std::vector`, because ESP-IDF compiles `-fno-exceptions`
  and a throwing allocation is an `abort()`. Effectively requires PSRAM, same trade the
  tape decoder makes.
- **A SAVE into a `.p64` fails messily rather than answering `26,WRITE PROTECT ON`.**
  `drive.cpp`'s guard is `!f->isWritable || f->pathInStream.empty()`, and `MFSOwner::File()`
  copies `isWritable` from the container - so a `.p64` on an SD card inherits true and the
  guard does not fire. `P64MStream::writeContainer()` returning 0 is what actually keeps
  the image intact; the drive then reports whatever the D64 write path makes of writes that
  never land. `g64` and `nib` have the same exposure and no equivalent guard at all.
- **The track range stays at 35 even though the chunk table knows better.** A 1541 BAM holds
  35 four-byte records, so widening it makes `getBAMRecord()` read the disk name as free
  counts - and an image with tracks past 35 is usually a protected release that put
  protection data there, exactly when a listing must not start reporting nonsense. Reaching
  those tracks needs an extended-BAM layout, which is the same open problem the D64
  constructor has for a 40-track `.d64` (its DOLPHIN/SPEED/PrologicDOS records are
  commented out).
- **Scope**: read-only, side 0, `.p64` only. `.p81` (P64-1581) is a different geometry and
  `MFileSystem::byContent()` already maps it separately. `vdrive_compatible` is left unset:
  the vdrive layer wants a whole `TP64Image` in RAM, which is the thing ruled out above.
- **A GEOS VLIR file reads back as its 254-byte index block, on P64 and D64 alike.** There
  is no VLIR support anywhere in the codebase (`meat_media.cpp` only names the type in a
  listing), so `readFile()` walks the entry's block chain — which for a VLIR file is the
  single index block, not the records it points at. Wheels' `SYSTEM2` is 69 blocks in the
  directory and copies out as 254 bytes of record pointers. Pre-existing and
  format-agnostic; do not chase it as a P64 defect.
- **Host tests**: `test/native/test_p64_read/` — 13 cases over the real images in
  `.archive/p64` (gitignored, so they skip cleanly without it). They assert on CBM
  structures rather than intermediate shapes, because each decode stage is silent about
  the next being wrong: a blank disk's BAM free count and bitmap for all 35 tracks across
  all four speed zones, and Wheels 4.4a's exact disk name, id and eight directory entries
  with their block counts. Note the block count a GEOS entry carries includes its
  single-block info record, so a data chain walks one block shorter than the directory says.
  **`test_every_sector_of_every_track_decodes` is the load-bearing one** — all 683 sectors
  of a full disk, header found and data checksum valid. It is what found the seam bug, and
  nothing smaller would have: the directory, the disk header and every file read tested
  before it all passed while four sectors were quietly wrong.

- **A CFS data sector is FOUR interleaved 128-byte columns, not 512 linear bytes** (`media/hd/hdd.h/.cpp`): file byte `n` of a sector lives at sector offset `(n % 128) * 4 + (n / 128)`. `readFile()` now reads the sector whole via the new `loadDataSector()` (cached, since a caller asking for a few bytes still needs all 512) and de-interleaves out of it. Only FILE DATA is interleaved — boot, partition, directory and tree sectors are linear, which is why every listing and the whole tree walk were correct while every file came back scrambled. The CFS 0.11 spec pins the tree geometry but is silent on byte order inside a data sector. Two independently authored images agree, and a de-interleaved PRG's BASIC line-link chain walks correctly across sector boundaries. Full reasoning is in the comment above `loadDataSector()` and in the root `AGENTS.md`.
- **Diagnostic that settled it, worth repeating**: temporary prints in `dataSectorForPos()` (pos, tree LBA, pointer index, raw pointer bytes, first 16 bytes of the node) and `readFile()` (pos, chunk, LBA, absolute image offset, bytes returned). They showed the pointers and offsets were already right, which is what localised the fault to a byte permutation instead of the tree.
- **`MFile::cdLocalRoot()` ("the root of the stream", `CD//DIR`) must land on the CONTAINER, not its parent directory**: it used `sourceFile->path`, but for a container-hosted file the source file is the directory the image lives in (`/sd/games` + pathInStream `game.d64`), so `CD//OS` inside an image escaped it entirely. `path` excludes `pathInStream` and therefore already names the image; append to that. The test for "hosted by a container" is `sourceFile != nullptr && !sourceFile->pathInStream.empty()` — `MFSOwner::File()` sets the source's `pathInStream` only on the container branch, never on the `isRootFS` one — so plain VFS and network paths are untouched. Note `cdLocalParent()` (`CD:..DIR`) has the same container blindness and was NOT changed.
- **A wildcard-only path component is not a partition reference** (`hddResolvePartitionIn()`, and the same fix in `media/hd/dhd.cpp`): `byName()` honours wildcards, so `LOAD"*"` matched the first partition by name and turned into a listing. `HDDMStream::seekEntry()` additionally skips directories and separators for an all-wildcard pattern, so `*` reaches the first loadable file past `%DELETED FILES%`.

### A session must be held BUSY across the request, not after it

- **`acquireIO()` must bracket the REQUEST, not just its result** (`network/http.cpp`, `HTTPMStream::open()`): it was called only after `client.GET()/POST()/PUT()` returned, so DNS, the TLS handshake, the redirect chain and the response headers — by far the longest I/O in the operation — ran with `io_active == 0`. `SessionBroker::service()` sweeps once a second and disposes any session for which all three of: not in use by a drive/console cwd, `!isBusy()`, and `getIdleTime() >= idle_grace_period` (default **5000 ms**, set in `MSession`) hold. **A `wget` is nobody's cwd**, so `isBusy()` is the ONLY thing protecting it — and a GET slower than the grace period got `disconnect()` called on it mid-request. Reported as a `wget` from archive.org (TLS + a 302 to a CDN node) dying part-way; the log ordering is diagnostic, with `Session not in use, removing:` landing between `GET():` and `open()`'s return.
- **The rule generalises**: any operation that is not reflected in a drive or console cwd — `wget`, `cp` to a network destination, `unzipx` from a URL — is invisible to `is_session_in_use()` BY DESIGN, and `isBusy()` is its whole defence. Hold the refcount before the first byte goes out, not once the answer is back. Do NOT try to fix this by widening `is_session_in_use()`; `isBusy()` is already the correct mechanism.
- **The refcount is per-SESSION and shared by every stream on the same host:port**, so each stream must contribute at most one count. `HTTPMStream` now gates acquire/release behind an `_io_held` flag (`acquireSessionIO()`/`releaseSessionIO()` in `network/http.h`): an unbalanced release would silently decrement ANOTHER stream's hold and expose it to the same sweep. This also closed a pre-existing hole where a failed `open()` acquired nothing while `close()` released unconditionally. A failed `open()` must release its own hold, since a stream that never opened is never closed.
- **A session's `key` member must be spelled exactly as `SessionBroker::obtain()` spells it** — `T::getScheme() + "://" + host + ":" + port`. `HTTPMSession`'s constructor used `(port == 443 ? "https://" : "http://")`, so an HTTPS session was CREATED as `https://host:443` and FILED as `http://host:443`: one object under two names, which makes the logs read as two sessions and would defeat any lookup by `session->key`. HTTPS is deliberately not a separate scheme — one session is pooled per host:port, the port distinguishes them, and `service/ml.h` disposes `"http://api.meatloaf.cc:443"` by that exact spelling. **HTTP was the only protocol that had drifted**; the other 15 already matched. `ArchiveMSession` is self-consistent on `"archive:" + archiveUrl` but never goes through `obtain()`, so its `getScheme()` is unused (the root `AGENTS.md` line calling that key `"archive://" + archiveUrl` is stale).
- **Untestable on the host**: `MeatHttpClient` needs `esp_http_client`, so none of this is covered by the native suite — same limitation as the 2026-08-12 HTTP fixes. Compile-verified only; the reproduction is the real test.

## Recent Changes (August 15, 2026)

- **CSM cassette images** (`media/tape/csm.h/cpp`, new; registered as `csmFS`): a CSM holds
  **already-decoded** CBM tape blocks — there are no pulses, so no TAPClean, no decoding and no
  PSRAM requirement, which is the ONLY way it differs from TAP internally. Its BEHAVIOUR is a
  datasette exactly as TAP's is, and the file layer is a direct copy of TAP's shape. Image layout is
  `[192-byte header block][data block]` repeated, terminated by a type-`$05` header with **no data
  block after it**; the header carries type at 0, start/end address LE at 1-4 and a 16-byte
  space-padded PETSCII name at 5-20. **The data block is raw program bytes with NO load-address
  prefix** — the two bytes a PRG begins with are synthesized from the header's start address, the
  same thing `T64MStream::readFile()` does. The high byte is `(start >> 8) & 0xFF` — see the T64
  entry below for why that spelling matters.
- **A CSM has no directory — entry *n*'s offset is the sum of every preceding block**, so
  `readHeader()` WALKS the container to build the entry list. It is idempotent via the shared
  `walked` flag, which is set even when the walk yields nothing (a broken image would otherwise be
  re-walked on every listing) — and each step of the walk is a seek plus a read, i.e. one HTTP range
  request per entry over the network.
- **Sequential media resolves a name through `seekNextEntry()`, NOT `seekPath()`** — the browsable
  branch of `MFile::getSourceStream()`. Before 2026-08-15 that branch was unreachable dead code:
  nothing overrode `seekNextEntry()` (`MMediaStream` hard-returned `""`) and nothing anywhere
  returned `isBrowsable() == true`, because `MMediaStream::isRandomAccess()` is unconditionally
  true, so every media format took the `seekPath()` branch — TAP included. `CSMMStream` and
  `TAPMStream` now override `isBrowsable()`/`isRandomAccess()` and implement `seekNextEntry()`.
  **TAP is idx-aware**: with a `.idx` sidecar it stays random-access and `seekPath()` still does the
  work; without one it is a datasette and `seekNextEntry()` does. `drive.cpp` tests
  `isRandomAccess() || isBrowsable()`, so the cwd-setting behaviour is unchanged either way.
- **The `seekNextEntry()` contract**: advance the head by one, SERVE that entry (so the stream is
  ready to read the instant the caller's name matches), return its display name. **At the end of the
  media it WRAPS** — the end of the tape is not the end of a scan. It returns `""` only once it has
  been all the way round, which is what lets a name behind the head still be found while a miss
  still terminates. The generic loop only matches names; all tape semantics live in the stream.
  Lap detection is per-STREAM (`scan_steps` on CSM, `scan_*` on TAP), deliberately not in the shared
  state: a fresh decoded stream is built for every open, so it starts clean each search with no
  reset to get wrong — which is also why the tape POSITION must be shared and this must not be.
- **`serveCurrent()` clears `have_current` in both CSM and TAP, and that is load-bearing.** The
  entry a listing left under the head is offered first so `LOAD"*"` after a listing gets what was
  just listed; serving it moves the head past it, so the flag must be consumed or a repeated
  `LOAD"NAME"` re-serves the same entry forever. That is precisely the case these have to get right:
  `Abductor.csm` carries a BASIC loader and its payload BOTH named `ABDUCTOR`, which is the norm for
  multi-part tapes, and the payload is unreachable without it. **`TAPMStream::readFile()` guarded on
  `have_current` and had to change** — testing it there made every read of a served file return 0.
  It now guards on `current.prg` being non-empty. `serveCurrent()` also sets `seekCalled`, which
  `seekPath()` used to do and which `MMediaStream::read()` needs to route through `readFile()` at
  all.
- **The datasette position lives in `CSMState`, shared per container URL via a weak_ptr registry**,
  copied from `TapeState` and for the same reason: `MFile::getSourceStream()` builds a FRESH stream
  on every open while directory listings use the ImageBroker instance, so a per-instance position
  would rewind to entry 0 on every `LOAD`. The aliasing reference members must stay declared after
  `state` — they are bound in the constructor's init list, in declaration order.
- **The `$05` end-of-tape block's address fields are not a length.** The jsvic20 reference decoder
  (`.reference/jsvic20-csm/`, minified line 1303, class `Kbe`) has no case for it and reads them as
  one, running off the end — four of the twelve corpus samples end this way. The walk also stops on
  `end < start`, a short read, a header block that does not fit in what remains, and a 256-entry cap;
  a final entry whose data runs past EOF is clamped to the bytes that exist.
- **Names are padding-trimmed with `mstr::rtrimPad()`** ($A0 *and* trailing spaces — its own comment
  cites CBM tape headers). An entry the tape leaves unnamed lists under the media file's name
  (`entryDisplayName()`), as TAP does, and duplicates need no disambiguation because the sequential
  model resolves them positionally. Real tapes carry both cases: `Wacky Waiters` has an unnamed
  entry, `Abductor` has two `ABDUCTOR` entries.
- **Every T64 entry loaded to the wrong address** (`media/tape/t64.cpp`, `seekPath()`): the load
  address high byte was `entry.start_address & 0xFF00` assigned into `_load_address`, which is
  `uint8_t[2]`. That mask clears exactly the eight bits the assignment keeps, so it stored **0 for
  every possible address** — a PRG at `$0801` loaded to `$0001`. Now `(start_address >> 8) & 0xFF`.
  The low byte was always correct, which is what made this survive: the file loaded, just into the
  wrong page. Pinned by `test/native/test_t64_read/`.
- **OPEN: T64 never trims the 16-byte filename field, so exact-name lookup fails on `$20`-padded
  images.** `seekEntry(std::string)` compares the query against the raw field (the `rtrimA0()` call
  is commented out), and `std::string entryFilename = entry.filename` on a `char[16]` with no
  terminator also over-reads past the struct. For a `$00`-padded T64 the string constructor stops at
  the NUL and the name comes out clean — which is why this goes unnoticed, most files in the wild
  being `$00`-padded — but the T64 spec specifies `$20`, and on those images the entry compares as
  `"himem           "` and only `LOAD"*"` or a wildcard can reach it. Not fixed because it is a
  behaviour change with a real trade-off: a trailing space can be part of a CBM name, which is
  exactly why `mstr::rtrimA0()` strips only `$A0` and `mstr::rtrimPad()` exists as the opt-in for
  fixed-width fields (CSM uses the latter). `test_space_padded_name_lookup` is
  `TEST_IGNORE_MESSAGE`'d with its body intact — lift the guard and re-run to verify a fix.
- **Host tests**: `test/native/test_csm_read/` — 24 synthesized-image cases (the walk, the datasette
  behaviour, reading, corrupt input) plus one that walks every sample in `.archive/csm` and asserts
  each consumes to exactly EOF, which is the invariant pinning the layout. Three gotchas that cost
  time: `compareFilename()` matches against `mstr::toUTF8()` of the entry name, so a test passing a
  bare ASCII literal asks a question the drive never asks; `MMediaStream::write()` only reaches
  `writeFile()` once `seekCalled` is set, so a read-only assertion needs a `seekPath()` first; and
  **each test must use its own container path**, because Unity's `TEST_ASSERT` failure path is a
  `longjmp` that does not unwind C++ destructors — a failing test therefore leaks its stream, the
  `CSMState` never expires from the registry, and every later test on that URL inherits a stale
  tape, turning one real failure into a dozen fictitious ones. `CSMMFile` itself is out of reach
  natively: `MFSOwner::File()` aborts under the stubs, as for the M2I and HDD suites.

## Recent Changes (August 13, 2026)

- **IDE64 CFS gained the CMD partition model** (`media/hd/hdd.h/.cpp`, new `media/hd/partition_select.h/.cpp`): `HDDImageRegistry` mirrors `DHDImageRegistry` — per-image table and selection keyed on the container URL, `HDDResolvePartition()` binding a partition to a path without calling `select()`, `$=P` listing on `HDDMFile`, and entry URLs that name the partition BY NUMBER. The image root is now the SELECTED partition's directory rather than the partition list; that is the one visible regression, and `partition` / `$=P` are how the list is reached. See the three divergences from DHD in `AGENTS.md` before touching any of it.
- **`HDDMStream::BootSector` had `default_partition` and `last_sector` off by two** — DP is `$03` and `@Last disk sector` is `$04-$07`. Latent on the whole sample corpus, which has DP = 0 either way.
- **The registry's parsing is split from its opening** so it is natively testable: `parseInto(MStream*, Image&)` takes an already-open stream, `parse(url, Image&)` does the `MFSOwner` open with the `s_probing` guard. `hddResolvePartitionIn(const Image&, ...)` is the same split for resolution. `test/native/test_hdd_read` drives both directly — `MFSOwner::File()` and `MFile::getSourceStream()` abort under the native stubs, so anything that reaches them is untestable there.

## Recent Changes (August 12, 2026)

- **A seek to where the stream already is must not re-request** (`network/http.cpp`, `MeatHttpClient::seek()`): the `isFriendlySkipper` path drained the live response and re-requested the same bytes even when `pos == _position`, and re-using the handle for a fresh request immediately after flushing the previous one desynchronises it — the reads that follow return the buffer **unwritten** (ESP-IDF heap canaries `0xBAAD5678`, stray strings) while still reporting a positive count. Symptom: `unzipx` over HTTPS failed on the FIRST attempt and worked on the second, because `Archive::open()`'s `seek(0)` on a freshly opened stream is a seek to where it already is, and the second attempt only recovered when its re-request FAILED (`httpCode=-1`) and the failure path tore the handle down. Guarded with `_is_open && pos == _position && !complete()`; `_position` is the offset of the next byte the current response will yield, so that comparison is exactly "a read would already return what you asked for".
- **A container that fails to open over HTTP is not necessarily misaligned — check the free internal heap first.** `Archive::open()`'s failure line prints the first bytes handed to libarchive; if they are neither the format's signature nor plausible file content but look like freed heap (a `0x3FFF….` DRAM pointer, fragments of `__FILE__` strings such as `lib/meat…`), the read reported a positive count without ever filling the buffer, and the machine is out of internal DRAM. That is what the reported `unzipx` failure actually was — the misaligned-source theory fit the symptom but was the wrong mechanism. Both produce "no format bid", which `raw` used to swallow silently.
- **`MeatHttpClient` is SHARED per host:port — its `_position` is not yours** (`network/http.cpp/.h`). Every `HTTPMStream` on the same server talks to one client object, so the client's `_position` reflects whichever stream (or `processRedirectsAndOpen()`) last touched it. `MeatHttpClient::seek()` uses that counter to decide whether a real re-request is needed and returns "already there" when the target matches it — so against a stale counter a rewind silently does nothing and the caller reads on from the previous response. `HTTPMStream::seek()` now assigns the client this stream's own offset before delegating; it is skipped in full mode, where `_position` indexes `_responseBuffer`/the JSON result instead of the HTTP body. Symptom that found it: an `ArchiveMStream` re-opened from the ImageBroker was handed bytes 524 into a zip, so libarchive bid the format on mid-file bytes and no format matched.
- **`MStream::seek(pos, mode)` no longer commits `_position` before delegating, and restores it when the delegate refuses** (`meatloaf.h`). It used to assign the target up front, which (a) left `_position` lying after a failed seek and (b) destroyed the pre-seek offset that an implementation needs to know where it actually was — `HTTPMStream` depends on exactly that, and overrides the two-argument form so nothing touches `_position` before its one-argument `seek()` runs. Any implementation that overrides only `seek(uint32_t)` must set `_position` itself on success (they all do). Contract pinned by `test/native/test_mstream_seek/`.
- **Note on overriding `seek`**: a subclass that declares only `seek(uint32_t)` HIDES the inherited `seek(uint32_t, int)` from anything holding a derived pointer. Call sites hold `MStream*` so this is currently harmless, but add `using MStream::seek;` if a derived-typed caller ever needs the two-argument form.

## Recent Changes (August 11, 2026)

- **libarchive's `raw` reader is a catch-all and must never compete with a real container format** (`media/archive/archive.cpp`): it bids 1 on any byte stream and synthesizes ONE entry named `data` spanning the whole input. Registering it alongside the extension-selected format turned every format-detection failure into a silent success — the caller "extracted" a byte-for-byte copy of the container named `data`. `Archive::open()` now registers it only in the unknown/ambiguous-extension branch, which is where single compressed files (`.gz`, `.bz2`, …) land and where a synthetic single entry IS the correct answer; `rawOnly=true` (used by `ensureData()`) is unchanged. Consequence to keep in mind: a container whose bytes don't match its extension now FAILS to open instead of returning one bogus entry, and that failure is what callers report.
- **A container that opens locally but not over the network is a source-stream fault.** The archive layer only ever reads what the source hands it, and libarchive bids the format from the first bytes — so a stream that is not positioned at the container's first byte produces exactly the raw-fallback symptom above, indistinguishable from a corrupt file. `cb_read()` records the first 16 bytes of each open and the failing-open path logs them with `size()`/`position()` for this reason; check that line before suspecting the format code. Verified for the D64/PRG-bearing zip case where a local walk yields 30 entries and the HTTP walk yielded one `data`.
- **Host tests**: `test/native/test_archive_extract/` builds libarchive for the development host and drives `Archive::open()` + `nextEntrySimple()` against a real zip, a misaligned source, and a `.gz`. See its README before changing `Archive::open()`'s format registration.

## Recent Changes (August 10, 2026)

- **Deleting and recovering files inside D64-family images** (`media/disk/d64.h/cpp`): `D64MStream::removeFile(path)` walks the block chain freeing each block in the BAM and then scratches the directory entry; `unremoveFile(path)` is the inverse. `D64MFile::remove()` routes a non-empty `pathInStream` to `removeFile()` and disposes the ImageBroker entry (`brokerUrl()`) afterwards, since the cached stream holds the directory state a later listing would otherwise re-serve. An empty `pathInStream` still means the CONTAINER itself and falls through to `MFile::remove()`.
- **Scratch is type-byte-only, and that is what makes recovery possible.** A scratch zeroes ONLY offset +2 of the entry; the name, start track/sector and block count all survive, per CBM DOS. A file is therefore recoverable until its directory slot is reused or its blocks are handed to a new file. `unscratchEntry()` reflects that in two halves: pass 1 walks the chain and proves every block is still free, collecting them and modifying nothing; pass 2 reallocates and sets the type to PRG. The split is deliberate — a half-finished recovery is worse than the delete it was undoing. The walk is bounded by the entry's recorded block count because the links live in blocks that are currently FREE and may already be overwritten garbage, and it refuses outright if any block has been reallocated, since recovering anyway would cross-link two files onto the same blocks. The original file type is genuinely unrecoverable: it is the byte the scratch destroyed.
- **`seekScratchedEntry()` must reject empty names**: a never-used directory slot and a scratched entry both have `file_type == 0`, and the only thing separating them is that the scratched one still carries its name.
- **`MMediaStream::mode` is uninitialised on a directly constructed stream** — only `MFile::getSourceStream()` ever sets it. If the garbage has the `out` bit set, `seekPath()` takes the WRITE branch and CREATES the entry it was asked to find: lookups then report deleted files as present and reads return nothing. This produced test failures that moved between formats from run to run and looked like an engine bug. Anything constructing a stream directly — tests, engine helpers — must set `mode` before calling `seekPath()`.
- **`MStream::read()` returns at most one block.** A caller wanting a whole file loops until it returns 0. A single call yielding 254 of 800 bytes is correct behaviour, not truncation.

## Recent Changes (August 7, 2026)

- **DHD switched partitions part-way through a directory listing** (`media/hd/dhd.h`, `normalizePath()`) — another instance of the `uint8_t` truncation class. Partition numbers are `uint8_t` and `byNumber()` takes one, but the lookup was `img->byNumber(atoi(comp.c_str()))`: `atoi` returns `int`, which was silently truncated at the call. Selecting a partition strips its name from `pathInStream`, so entry URLs built during a listing carry no partition component (`…/hdbackup.dhd/1571`) and `normalizePath()` re-reads the first component as a partition reference — a file named `1571` inside the BIBLE partition resolved to `1571 & 0xFF` = 35 and switched the image to partition 35 ("GW BOOT HD") mid-`ls`, with the rest of the listing coming from the wrong partition (the selection also disposes the ImageBroker entry, so the stream re-opened underneath the running listing). Now parsed with `strtol` + `*end == '\0'` + a `0 <= v <= 254` range check before the cast, per the existing rule against `atoi`/`std::stoi` on C64- or network-sourced input. `iecDrive::changePartition()` was already correct — it range-checks `1..254` before its `(uint8_t)` cast. **Resolved 2026-08-07** by removing path-based selection entirely (`partition` console command + `CP<n>` are now the only ways to switch), which eliminates the ambiguity by construction rather than by validating the parse. The `strtol` guard described above was deleted along with the branch it guarded.

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
- **DHD (CMD HD) and D1M/D2M/D4M (CMD FD)** (`media/hd/dhd.h/cpp`, `media/disk/dxm.h`): `DHDImageRegistry` parses the partition table once per image (HD: system partition found by scanning 64 KiB boundaries for the boot magic at +$5F0, table at sys+65536, a MAXIMUM OF 254 partitions numbered 1-254 (table entry 0 is the system partition — listed but never selectable; there is no entry 255); FD: fixed system partition at 0x640/0xC80/0x1900 512-byte blocks for D1M/D2M/D4M, "CMD FD SERIES" magic, table at sys+2048 since FD track 0 is 8 sectors, 31 partitions; probing guard makes the CMD filesystems' `handles()` decline so the raw filesystem serves the bytes) and tracks the selected partition (default on first use). `DHDMFileSystem::getFile()` returns `DHDPartitionMFile<D64MFile|D71MFile|D81MFile|DNPMFile>` per the selected partition's type; `getDecodedStream()` builds the matching stream over a `DHDOffsetStream` window (offset+size of the partition), so the disk classes are unaware of the DHD. `LOAD"$=P"` lists partitions; `CP<n>` (drive) and the `partition` console command are the ONLY things that change the SELECTED partition. A partition named as the first in-image path component binds only that path (`DHDResolvePartition()`, `m_part` on `DHDPartitionMFile`, 0 = follow the selection) and never calls `select()`. Because `ImageBroker` caches ONE stream per image keyed on the container, `Image::cached_part` records which partition that stream decodes and `normalizePath()` disposes it on a mismatch; `brokerUrl()` names the partition so the rebuild resolves the right one. Both are required — see the 2026-08-08 entry. Selection changes dispose the ImageBroker key `"d64" + <container source url>[/<pathInStream>]`.
- **HDD (IDE64 CFS)** (`media/hd/hdd.h/cpp`): rewritten against the actual CFS 0.11 spec — 16-byte names, `LBA = (b0&0x0F)<<24|b1<<16|b2<<8|b3`, file size at $10 (LE), pointer at $14 (carries 2-bit NEXTS slices; HIDDEN bit 7), attributes $18, 3-char type $19, times $1C. Partition entries hold the root/deleted directory pointers at +$1C/+$18; TYPE is the end-pointer high nibble sans the LBA bit. Root of the image lists partitions (default-partition fallback for bare paths); subdirectories; multi-sector directories via the NEXTS pointer assembled from entry-pointer slices (byte 3 ← entries 1-4 … byte 0 ← entries 13-16, MSB-first per group of 4); data read via the balanced tree (node = 128 data pointers + 8 next-tree pointers in SLICE bits; coverage(d) = 64K + 8·coverage(d-1); depth chosen minimal for the file size; zero pointers = holes → $00). Read-only.
- **Tape images .tap/.dmp/.htap** (`media/tape/`, `components/tapclean/`): the TAPClean 0.39 engine (Final TAP 2.76 lineage) is vendored as an ESP-IDF component (the PIO lib builder does not compile `.c` under `lib/meatloaf`) with ~90 loader scanners (Cyberload, Visiload, US Gold, Novaload, Freeload, Turbotape 250/263/526, plus the Meatloaf-added `turbotape_fast.c` for 4x-speed TT64, ~88/144-cycle pulses). `TapeDecoder::open()` only parses the header; the image is fetched into PSRAM and scanned PROGRESSIVELY on demand (512 KB prefix, then doubling — DMP/HTAP/TAP-v2 are converted to TAP v1 on the fly as they stream in; halfwave pairs summed; the engine borrows the buffer, no copies). Each window scan (`tapclean_analyze_tap(unite=1)` joins neighbouring blocks into loadable PRGs) rebuilds the `entries` vector (loader-internal HEADER blocks folded in for their names, `_DATA` suffix stripped, repeat copies deduped, CBM boot stubs dropped when a turbo payload follows — their name transfers to it); the LAST entry of a partial window is withheld until confirmed complete. Once fully scanned the engine state and image are freed — only decoded programs stay resident. `nextProgram(from_offset)` serves the first entry with `tape_end_offset > from_offset` (may extend the scan); `offsetAtTime()` snaps to the program grid. The engine is global-state (NOT thread-safe): scans are serialized by a static mutex inside `TapeDecoder`. `TAPMStream`/`TAPMFile` are unchanged: datasette-style sequential listing (ONE entry per directory request, `no more entries` at tape end, rewind on the next request; loads search forward and wrap once), `.idx` sidecar (`<offset>[:<length>] <name>`, `#`/`;`/`'` comments) for full random-access listing, `TAPMFile::buildIndex()` / drive command `T-I` to generate it, `T-C <ms|MMM:SS>` to set the counter. Entry names are UTF-8 like every other name in Meatloaf — TAPClean hands back the tape header's bytes as "ASCII", but they are the PETSCII the header holds, so `harvestEntries()` runs them through `toUTF8()` (a conversion, not a case fold) and the IEC boundary converts back. Unnamed entries take the media file's name with no load-address suffix. Tape decode effectively requires PSRAM (image prefix + decoded programs in RAM during the scan). The decoder + datasette position live in a `TapeState` shared across all TAPMStream instances of an image (weak_ptr registry keyed by container URL), so `LOAD"*",8` / console `cat *` serve the current listing entry and opens never re-scan.
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
