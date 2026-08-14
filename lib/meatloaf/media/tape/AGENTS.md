# AGENTS.md - TAP Format Implementation

This file provides guidance to coding tools when working with the TAP (Commodore tape) format implementation.

## Overview

The TAP format stores raw Commodore datasette tape data as pulse timings. This implementation decodes those pulses into actual program files, extracting filenames, load addresses, and file data from the encoded pulse stream.

## TAP Format Details

### File Structure

**Header (20 bytes)**:
- Bytes 0-11: Signature "C64-TAPE-RAW" or "C16-TAPE-RAW"
- Byte 12: Version (0 or 1)
- Bytes 13-15: Reserved
- Bytes 16-19: Data size (little-endian, size of pulse data)

**Pulse Data** (variable length):
- TAP version 0: Each byte is pulse duration, 0 = special 1000000 value
- TAP version 1: If byte != 0, pulse = byte * 8. If byte == 0, read next 3 bytes as 24-bit duration

### Commodore Tape Encoding (Kernal Loader)

**Pulse Types** (based on duration thresholds):
- Pulse 0 (short): < 426 cycles
- Pulse 1 (medium): 426-616 cycles
- Pulse 2 (long): > 616 cycles

**Bit Encoding** (two pulses per bit):
- Pulse pattern (0,1) → bit 0
- Pulse pattern (1,0) → bit 1

**Byte Encoding**:
- 8 bits (LSB first) + 1 parity bit = 9 pulses total
- Parity: XOR of all bits should equal 1

**Sync Sequence**:
- Long pulse (type 2) followed by medium pulse (type 1)
- Indicates start of byte stream

**Tape Header** (21 bytes at memory address 828-848):
- Byte 0: File type (1=relocatable, 3=non-relocatable)
- Bytes 1-2: Start address (little-endian)
- Bytes 3-4: End address (little-endian)
- Bytes 5-20: Filename (16 bytes, PETSCII, space/null-padded)

**Data Block**:
- Multiple bytes with sync+parity for each
- Followed by checksum byte (XOR of all data bytes)

**File Structure on Tape**:
1. Pilot tone / sync
2. Header block (21 bytes)
3. Pilot tone / sync
4. Data block (variable length)
5. Repeat for additional files

## Implementation Architecture

### TAPMStream (tap.h/cpp)

Inherits from **MStream** (not MMediaStream) because:
- TAP is **sequential access** - can't seek to arbitrary files
- Must decode pulse stream in order
- Uses `seekNextEntry()` pattern instead of `seekPath()`

**Key Members**:
```cpp
std::shared_ptr<MStream> containerStream;  // Source stream
std::vector<TapeFile> tape_files;          // Decoded files
uint32_t tap_position;                     // Current position in pulse data
uint32_t current_file_index;               // Iterator for seekNextEntry()
```

**Decoding Functions** (adapted from wav-prg):
- `readTAPPulse(uint32_t& pulse)` - Read one pulse from TAP file
- `getPulseBit(uint8_t& bit)` - Convert two pulses to one bit using thresholds
- `getByte(uint8_t& byte)` - Read 8 bits + parity to get byte
- `getByteWithSync(uint8_t& byte, bool allow_short_first)` - Read byte with sync detection
- `findSync()` - Search for sync sequence in pulse stream
- `readTapeHeader(...)` - Decode 21-byte tape header
- `readDataBlock(...)` - Decode data block with checksum verification

**File Analysis**:
- `analyzeTapeData()` - Called during construction
- Scans entire pulse stream for files
- Decodes and caches all file data in `TapeFile::cached_data`
- Fallback: provides raw TAP access if no files decoded

**Data Access**:
- `seekNextEntry()` - Sequential iteration through `tape_files` vector
- `read()` - Reads from cached decoded data (not live pulse decoding)

### TAPMFile (tap.h/cpp)

**Key Members**:
```cpp
std::shared_ptr<TAPMStream> cached_stream;  // Cached stream instance
```

**Why Caching**:
- Can't use ImageBroker (designed for MMediaStream)
- TAP decoding is expensive (full pulse analysis)
- Stream must persist across directory iterations
- Cached in member variable instead of broker

**Directory Access**:
- `rewindDirectory()` - Creates/reuses cached_stream, resets file index
- `getNextFileInDir()` - Calls cached_stream->seekNextEntry()

### TapeFile Structure

```cpp
struct TapeFile {
    std::string filename;
    uint8_t file_type;
    uint32_t data_offset;              // Offset in cached_data
    uint32_t data_length;
    uint16_t start_address;
    uint16_t end_address;
    std::vector<uint8_t> cached_data;  // Decoded file data
};
```

**Why Cache Data**:
- Sequential pulse reading is destructive (consumes pulses)
- Can't re-decode on-the-fly during read()
- All files decoded once during analyzeTapeData()
- Data stored in vector for later access

## Code References (from wav-prg)

**libaudiotap/libaudiotap.c:185-219** - `tapfile_get_pulse()`
- TAP file format pulse reading
- Version 0 vs version 1 handling
- 24-bit extended pulse format

**loaders/kernal.c** - Standard C64 Kernal tape loader:
- Lines 35-36: Pulse thresholds {426, 616}
- Lines 37: Pilot sequence {137,136,135,134,133,132,131,130,129}
- Lines 39-55: `kernal_get_bit_func()` - Two pulses to bit
- Lines 57-101: `sync_with_byte_and_get_it()` - Sync + byte + parity
- Lines 113-133: `kernal_headerchunk_get_block_info()` - Parse 21-byte header

**wav2prg_core/get_pulse.c** - Adaptive pulse detection:
- Lines 214-249: `get_pulse_adaptively_tolerant()` - Convert raw pulse to type
- Tolerance tracking for tape speed variations
- Threshold-based classification

**wav2prg_core/wav2prg_core.c** - Core decoding:
- Lines 102-113: `evolve_byte()` - Shift bits into byte (LSB first)
- Lines 94-100: `compute_checksum_step_default()` - XOR checksum

## Important Notes

### Capabilities
- ✓ Read TAP files (version 0 and 1)
- ✓ Decode standard Kernal loader format
- ✓ Extract filenames and addresses
- ✓ Verify checksums and parity
- ✓ Multiple files per tape
- ✓ Sequential browsing (directory listing)
- ✓ File reading from cached data
- ✗ Turbo loaders (would need additional loader plugins)
- ✗ Write support
- ✗ Random access seeking

### Architecture Decisions

**Sequential vs Random Access**:
- TAP format is inherently sequential (tape is linear)
- Implemented `isBrowsable() = true, isRandomAccess() = false`
- Uses `seekNextEntry()` pattern, not `seekPath()`

**Caching Strategy**:
- All files decoded during construction
- Data cached in TapeFile::cached_data
- Avoids re-decoding during read operations
- Memory trade-off for performance

**No ImageBroker**:
- ImageBroker requires MMediaStream inheritance
- TAPMStream inherits from MStream (correct for sequential format)
- Uses TAPMFile::cached_stream member instead

### Error Handling

**Tolerant Decoding**:
- Continues searching for sync on decode errors
- Falls back to raw TAP access if no files found
- Checksum mismatches logged but not fatal
- Multiple file recovery (one bad file doesn't abort all)

**Sync Detection**:
- Limited search (100,000 iterations) prevents infinite loops
- Allows short pulses initially for pilot tone
- Strict sync pattern (long + medium pulse)

## Testing

When testing TAP files:
1. Verify header reading (signature, version, data_size)
2. Test standard C64 tapes (Kernal loader)
3. Check filename extraction (PETSCII, trimming)
4. Verify address parsing (little-endian)
5. Test checksum validation
6. Try multi-file tapes
7. Test directory listing
8. Verify file reading
9. Check memory usage (cached data)
10. Test error recovery (corrupted tapes)

## Future Enhancements

To support additional tape loaders:
1. Add loader detection logic in analyzeTapeData()
2. Implement loader-specific decode functions
3. Different pulse thresholds for turbo loaders
4. Custom sync patterns (e.g., Novaload, Freeload)
5. Reference wav-prg/loaders/ for 40+ loader examples

To add write support:
1. Implement pulse encoding (inverse of readTAPPulse)
2. Add bit/byte encoding with parity
3. Generate sync sequences
4. Write header and data blocks
5. Calculate checksums

## References

- TAP Format: https://ist.uwaterloo.ca/~schepers/formats/TAP.TXT
- VICE TAP docs: https://vice-emu.sourceforge.io/vice_17.html#SEC330
- wav-prg source: https://sourceforge.net/p/tapclean/gitcode/
- C64 tape loaders: https://github.com/binaryfields/zinc64/blob/master/doc/Analyzing%20C64%20tape%20loaders.txt
