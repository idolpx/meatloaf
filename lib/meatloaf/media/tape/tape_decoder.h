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

// Tape image analysis driver built on the wav2prg core (WAV-PRG 4.2.1 by
// Fabrizio Gennari, GPL; see components/wav2prg). Decodes .tap/.dmp/.htap
// pulse streams into PRG files one program at a time, detecting the loader
// (Kernal, Turbo Tape 64, Freeload, Novaload, Pavloda, Ocean, ... - all
// wav-prg plugins are statically linked).
//
// The image is NOT buffered in memory: pulses are read through a small
// sliding window over the container stream, so tapes are found, listed and
// loaded sequentially like on a real datasette.
//
// Formats (auto-detected by signature):
//  .tap  - "C64-TAPE-RAW", v0/v1 pulses (v2 = halfwaves)
//  .dmp  - "DC2N-TAP-RAW", 16-bit samples at counter_rate (usually 2 MHz),
//          0xFFFF = counter overflow (https://www.luigidifraia.com/technical-info/)
//  .htap - "-HIRES" at offset 6, signed 16-bit halfwaves at 0.5 us; pauses
//          as 0x0000 0x0000 + 32-bit us (Manosoft HTAP spec V0 sub 2.0,
//          https://drive.google.com/file/d/11IK1m5-k5Jk9-iR9TpWUmxMAX9MC-s9D/view)

#ifndef MEATLOAF_MEDIA_TAPE_DECODER
#define MEATLOAF_MEDIA_TAPE_DECODER

#include <cstdint>
#include <string>
#include <vector>

class MStream;
struct wav2prg_continuation;

struct TapeEntry {
    std::string name;       // block name (PETSCII, trimmed; may be empty)
    std::string loader;     // detected loader name (wav2prg plugin name)
    uint16_t start_addr = 0;
    uint16_t end_addr = 0;
    uint32_t tape_offset = 0;     // byte offset of the program's first sync
    uint32_t tape_end_offset = 0; // byte offset just past the program
    uint32_t start_time_ms = 0;   // tape counter time at program start
    uint32_t end_time_ms = 0;     // tape counter time at program end
    bool checksum_ok = false;
    std::vector<uint8_t> prg;   // 2-byte load address + data
};

class TapeDecoder {
public:
    ~TapeDecoder();

    // Parse the image header; false if the stream is not a supported tape
    bool open(MStream *container);
    bool isOpen() const { return opened; }

    uint32_t dataStart() const { return data_start; }
    uint32_t imageLen() const { return len; }

    // Decode the next program at/after from_offset, streaming pulses from
    // the container. Fills 'out' (including tape counter times) and returns
    // true, or returns false at the end of the tape. The loader/observer
    // state carries over to the next call (turbo loader chains span
    // programs), as long as from_offset continues where the last call
    // stopped; jumping elsewhere restarts detection with the ROM loader.
    bool nextProgram(uint32_t from_offset, TapeEntry &out);

    // Drop the carried loader state (rewind / counter change)
    void resetContinuation();

    // Tape counter: byte offset of the counter time in ms. Streams forward
    // from the current cursor; a target before the current position rewinds
    // and streams from the start (like a real tape deck).
    uint32_t offsetAtTime(uint32_t ms);

    // Duration of the whole tape in ms; only known (non-zero) once the end
    // of the tape has been reached by normal streaming - never walked
    uint32_t totalMs();

private:
    friend struct tape_io;

    bool readBytes(uint32_t pos, uint8_t *dst, uint32_t n);
    bool nextValue(uint32_t *pos, uint32_t *cycles);  // one (half)wave at *pos
    uint32_t machineClock() const;

    MStream *stream = nullptr;
    bool opened = false;
    uint32_t len = 0;

    // Image format (from header)
    uint8_t kind = 0;          // 0=TAP 1=DMP 2=HTAP
    uint8_t version = 0;
    uint8_t platform = 0;      // 0=C64 1=VIC20 2=C16
    uint8_t video = 0;         // 0=PAL 1=NTSC
    uint32_t data_start = 0;
    uint32_t counter_rate = 2000000;
    bool halfwaves = false;

    // Sliding window over the container stream (no image buffering; use a
    // "#cache=..." URL fragment to localize network sources)
    uint8_t window[4096];
    uint32_t win_start = 0;
    uint32_t win_len = 0;

    // wav2prg input cursor
    uint32_t pos = 0;

    // Loader/observer state between incremental analyse calls
    struct wav2prg_continuation *cont = nullptr;
    uint32_t cont_pos = 0;      // input position when 'cont' was saved

    // Start-loader lock: once a non-Kernal primary loader (Pavloda, Turbo
    // 220, ...) is detected for this tape, use it as the start loader for
    // every subsequent program instead of the Kernal ROM loader
    std::string locked_loader;
    bool fallback_done = false; // standalone-loader trials attempted this scan
    uint32_t trial_end = 0;     // 0 = unlimited; else stop input at this offset

    // Decode the next program starting with 'start_loader' (or a saved
    // continuation); fills 'out'. Returns true on a program, false at the
    // end of the tape / trial window.
    bool analyzeChain(const char *start_loader, uint32_t from_offset, TapeEntry &out);

    // Last returned program (to skip repeated blocks, e.g. Kernal 2nd copy)
    bool last_valid = false;
    uint16_t last_start = 0, last_end = 0;
    uint32_t last_len = 0;
    std::string last_name;

    // Tape counter: elapsed cycles at the input cursor, accumulated while
    // pulses stream by (single pass - never re-walked). Invalidated when
    // the cursor jumps to an arbitrary offset (e.g. .idx loads).
    uint64_t cursor_cycles = 0;
    bool time_valid = true;

    void markEofReached();      // latch the total duration at end of tape
    uint32_t cyclesToMs(uint64_t cycles) const;

    uint32_t total_ms = 0;
    bool total_known = false;
};

#endif /* MEATLOAF_MEDIA_TAPE_DECODER */
