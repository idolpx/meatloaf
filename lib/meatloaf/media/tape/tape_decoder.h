// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

// Tape image analysis driver built on the TAPClean engine (TAPClean 0.39 /
// Final TAP 2.76 by Stewart Wilson, Subchrist Software and the TC Team,
// GPL; see components/tapclean). Decodes .tap/.dmp/.htap images into PRG
// files, recognizing ~90 commercial loader formats (Cyberload, Visiload,
// US Gold, Novaload, Freeload, Turbotape 250/64-fast, Pavloda, Ocean, ...).
//
// All images are scanned PROGRESSIVELY: a growing prefix (512 KB, then
// doubling) is fetched into PSRAM and analyzed on demand, so the first
// programs list long before a large image has been downloaded. DMP/HTAP/
// TAP-v2 sources are converted to TAP v1 on the fly as they stream in
// (entry offsets then refer to the converted image). An entry found in a
// partial window is only served once a later entry (or the tape end)
// confirms it complete. When the whole image has been analyzed the pulse
// buffer is freed; what remains resident is the decoded program data
// (typically well under 1 MB).
//
// Formats (auto-detected by signature):
//  .tap  - "C64-TAPE-RAW", v0/v1 pulses (v2 = halfwaves, converted)
//  .dmp  - "DC2N-TAP-RAW", 16-bit samples at counter_rate (usually 2 MHz),
//          0xFFFF = counter overflow (https://www.luigidifraia.com/technical-info/)
//  .htap - "-HIRES" at offset 6, signed 16-bit halfwaves at 0.5 us; pauses
//          as 0x0000 0x0000 + 32-bit us (Manosoft HTAP spec V0 sub 2.0)
// DMP/HTAP/v2 images are converted to TAP v1 in memory before analysis, so
// all tape offsets reported for them refer to the converted image.

#ifndef MEATLOAF_MEDIA_TAPE_DECODER
#define MEATLOAF_MEDIA_TAPE_DECODER

#include <cstdint>
#include <string>
#include <vector>

class MStream;

struct TapeEntry {
    std::string name;       // block name (PETSCII, trimmed; may be empty)
    std::string loader;     // detected loader name (TAPClean format name)
    uint16_t start_addr = 0;
    uint16_t end_addr = 0;
    uint32_t tape_offset = 0;     // byte offset of the program's first pulse
    uint32_t tape_end_offset = 0; // byte offset just past the program
    uint32_t start_time_ms = 0;   // tape counter time at program start
    uint32_t end_time_ms = 0;     // tape counter time at program end
    bool checksum_ok = false;
    std::vector<uint8_t> prg;   // 2-byte load address + data
};

class TapeDecoder {
public:
    ~TapeDecoder();

    // Parse the header; false if the stream is not a supported tape.
    // Plain TAP images are scanned PROGRESSIVELY afterwards: a growing
    // prefix of the image is fetched and analyzed on demand, so the first
    // programs list before the whole file has been downloaded. DMP/HTAP/
    // TAP-v2 images (which need conversion) are scanned in full here.
    bool open(MStream *container);
    bool isOpen() const { return opened; }

    uint32_t dataStart() const { return data_start; }
    uint32_t imageLen() const { return len; }

    // Return the program at/after from_offset into 'out' and true, or
    // false at the end of the tape. May fetch + scan more of the image.
    bool nextProgram(uint32_t from_offset, TapeEntry &out);

    // Kept for interface compatibility with the streaming engine (the
    // TAPClean scan has no carried loader state)
    void resetContinuation() {}

    // Tape counter: byte offset for a counter time in ms (snaps to the
    // program grid - callers follow up with nextProgram). May extend the
    // scan.
    uint32_t offsetAtTime(uint32_t ms);

    // Duration of the whole tape in ms; forces the full scan
    uint32_t totalMs();

private:
    bool readBytes(uint32_t pos, uint8_t *dst, uint32_t n);
    bool nextValue(uint32_t *pos, uint32_t *cycles);  // one (half)wave at *pos
    uint32_t machineClock() const;

    // Progressive scanning: fetch/convert a prefix of the image, scan it,
    // rebuild 'entries'. The LAST entry found in a partial window is
    // withheld until a later entry (or the tape end) confirms it
    // complete, so a window-truncated block is never served. DMP/HTAP/
    // TAP-v2 sources are converted to TAP v1 on the fly as they stream
    // in; entry offsets then refer to the converted image.
    bool extendScan();                  // grow the window by one step
    bool fetchTo(uint32_t target);      // raw fetch or streamed conversion
    bool appendValue(uint32_t cycles);  // encode one v1 pulse into 'image'
    bool scanWindow(uint32_t win);
    void harvestEntries(int nprg);      // engine PRG db -> entries (+filter)
    void finishScan();                  // full image analyzed: free it

    MStream *stream = nullptr;
    bool opened = false;
    uint32_t len = 0;          // analyzed (possibly converted) image length

    // Image format (from header)
    uint8_t kind = 0;          // 0=TAP 1=DMP 2=HTAP
    uint8_t version = 0;
    uint8_t platform = 0;      // 0=C64 1=VIC20 2=C16
    uint8_t video = 0;         // 0=PAL 1=NTSC
    uint32_t data_start = 0;
    uint32_t counter_rate = 2000000;
    bool halfwaves = false;

    // Sliding window over the container stream (used by the streamed
    // conversion; 16 KB keeps network round trips low)
    uint8_t window[16384];
    uint32_t win_start = 0;
    uint32_t win_len = 0;
    uint32_t container_len = 0;

    // Every confirmed program so far, in tape order
    std::vector<TapeEntry> entries;
    uint32_t total_ms = 0;

    // Progressive scan state
    bool fully_scanned = false;
    bool converting = false;    // source needs TAP v1 conversion
    uint8_t *image = nullptr;   // (converted) image prefix, PSRAM
    uint32_t image_cap = 0;     // allocated size of 'image'
    uint32_t fetched = 0;       // valid bytes of 'image' (== bytes scanned)
    uint32_t conv_pos = 0;      // container read cursor (converting only)
    bool conv_eof = false;      // container exhausted (converting only)
};

#endif /* MEATLOAF_MEDIA_TAPE_DECODER */
