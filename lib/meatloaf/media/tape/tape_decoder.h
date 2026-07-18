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
// Unlike the previous streaming (wav2prg) engine, TAPClean scans the whole
// image at open(): the image is read into a PSRAM buffer, analyzed once,
// and every recognized program is extracted and kept in 'entries'. The
// multi-megabyte pulse buffer is freed again before open() returns; what
// remains resident is the decoded program data (typically well under 1 MB).
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

    // Read and analyze the image; false if the stream is not a supported
    // tape. All programs are decoded here (one whole-image TAPClean scan).
    bool open(MStream *container);
    bool isOpen() const { return opened; }

    uint32_t dataStart() const { return data_start; }
    uint32_t imageLen() const { return len; }

    // Return the program at/after from_offset into 'out' and true, or
    // false at the end of the tape.
    bool nextProgram(uint32_t from_offset, TapeEntry &out);

    // Kept for interface compatibility with the streaming engine (the
    // TAPClean scan has no carried loader state)
    void resetContinuation() {}

    // Tape counter: byte offset for a counter time in ms (snaps to the
    // program grid - callers follow up with nextProgram)
    uint32_t offsetAtTime(uint32_t ms);

    // Duration of the whole tape in ms (known right after open())
    uint32_t totalMs() { return total_ms; }

private:
    bool readBytes(uint32_t pos, uint8_t *dst, uint32_t n);
    bool nextValue(uint32_t *pos, uint32_t *cycles);  // one (half)wave at *pos
    uint32_t machineClock() const;
    uint8_t *convertToTapV1(uint32_t *out_len);       // DMP/HTAP/v2 -> TAP v1
    bool analyzeImage();                              // run TAPClean, fill entries

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

    // Sliding window over the container stream (used while reading/
    // converting the image at open())
    uint8_t window[4096];
    uint32_t win_start = 0;
    uint32_t win_len = 0;
    uint32_t container_len = 0;

    // Every program on the tape, in tape order, decoded at open()
    std::vector<TapeEntry> entries;
    uint32_t total_ms = 0;
};

#endif /* MEATLOAF_MEDIA_TAPE_DECODER */
