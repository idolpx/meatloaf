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

#include "tape_decoder.h"

#include <cstring>
#include <cstdlib>
#include <mutex>

#include "meatloaf.h"
#include "string_utils.h"

#include "../../../../include/debug.h"

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

extern "C" {
#include "tapclean_api.h"
}

// The TAPClean engine is a global-state singleton; one analysis at a time.
static std::mutex s_tapclean_mutex;

// Image buffers are multi-megabyte: PSRAM first, heap fallback
static uint8_t *image_alloc(size_t n)
{
#ifdef ESP_PLATFORM
    uint8_t *p = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == nullptr)
        p = (uint8_t *)malloc(n);
    return p;
#else
    return (uint8_t *)malloc(n);
#endif
}

/********************************************************
 * Windowed access to the container stream
 ********************************************************/

bool TapeDecoder::readBytes(uint32_t p, uint8_t *dst, uint32_t n)
{
    while (n > 0)
    {
        if (p < win_start || p >= win_start + win_len)
        {
            if (p >= container_len || stream == nullptr || !stream->seek(p))
                return false;
            uint32_t got = stream->read(window, sizeof(window));
            if (got == 0)
                return false;
            win_start = p;
            win_len = got;
        }
        uint32_t off = p - win_start;
        uint32_t chunk = win_len - off;
        if (chunk > n)
            chunk = n;
        memcpy(dst, window + off, chunk);
        dst += chunk;
        p += chunk;
        n -= chunk;
    }
    return true;
}

/********************************************************
 * Header parsing / pulse extraction
 ********************************************************/

enum {
    TAPE_KIND_TAP = 0,
    TAPE_KIND_DMP,
    TAPE_KIND_HTAP
};

// Machine clock in Hz (cycles/second) for pulse duration conversion
uint32_t TapeDecoder::machineClock() const
{
    switch (platform) {
        case 1:  return video ? 1022727 : 1108405; // VIC-20
        case 2:  return video ? 894886 : 886724;   // C16/Plus4
        default: return video ? 1022727 : 985248;  // C64
    }
}

bool TapeDecoder::open(MStream *container)
{
    stream = container;
    opened = false;
    win_len = 0;
    entries.clear();
    total_ms = 0;
    fully_scanned = false;
    converting = false;
    free(image);
    image = nullptr;
    image_cap = 0;
    fetched = 0;
    conv_pos = 0;
    conv_eof = false;

    if (stream == nullptr)
        return false;

    container_len = stream->size();
    if (container_len < 20)
        return false;

    uint8_t d[20];
    if (!readBytes(0, d, sizeof(d)))
        return false;

    if (memcmp(d, "C64-TAPE-RAW", 12) == 0)
    {
        kind = TAPE_KIND_TAP;
        version = d[0x0C];
        platform = d[0x0D];
        video = d[0x0E];
        data_start = 20;
        halfwaves = (version == 2);
    }
    else if (memcmp(d, "DC2N-TAP-RAW", 12) == 0)
    {
        kind = TAPE_KIND_DMP;
        version = d[0x0C];
        platform = d[0x0D] & 0x0F;
        video = d[0x0E];
        counter_rate = d[0x10] | (d[0x11] << 8) | (d[0x12] << 16) | ((uint32_t)d[0x13] << 24);
        if (counter_rate == 0)
            counter_rate = 2000000;
        data_start = 20;
        halfwaves = false;
    }
    else if (memcmp(d + 6, "-HIRES", 6) == 0)
    {
        // HTAP (Manosoft, spec V0 sub 2.0): hardware id at 0x00, "-HIRES"
        // at 0x06, version 0x0C, machine 0x0D, video 0x0E, halfwaves at 0x14
        kind = TAPE_KIND_HTAP;
        version = d[0x0C];
        platform = d[0x0D];
        video = d[0x0E];
        counter_rate = 2000000; // pulse halfwaves are 0.5 us ticks
        data_start = 20;
        halfwaves = true;
    }
    else
    {
        Debug_printv("Unrecognized tape image signature");
        return false;
    }

    Debug_printv("Tape image: kind[%d] version[%d] platform[%d] video[%d] size[%lu]",
                 kind, version, platform, video, container_len);

    // Everything is scanned progressively on demand - the first
    // nextProgram() fetches (and for DMP/HTAP/TAP-v2 converts on the
    // fly) a growing prefix, so the first entries list before the whole
    // image has been downloaded
    converting = !(kind == TAPE_KIND_TAP && !halfwaves);
    len = container_len;   // refined as conversion progresses
    opened = true;
    return true;
}

// Read one duration value (in machine cycles) at *p, advancing it;
// false at end of data
bool TapeDecoder::nextValue(uint32_t *p, uint32_t *cycles)
{
    switch (kind)
    {
        case TAPE_KIND_TAP:
        {
            uint8_t b;
            if (*p >= container_len || !readBytes(*p, &b, 1))
                return false;
            (*p)++;
            if (b != 0)
            {
                *cycles = (uint32_t)b * 8;
                return true;
            }
            if (version == 0)
            {
                *cycles = 20000; // v0 overflow marker: a long pause
                return true;
            }
            uint8_t three[3];
            if (!readBytes(*p, three, 3))
                return false;
            *p += 3;
            *cycles = three[0] | (three[1] << 8) | ((uint32_t)three[2] << 16);
            return true;
        }

        case TAPE_KIND_DMP:
        {
            // 16-bit LE samples; 0xFFFF = counter overflow, accumulate
            uint64_t total = 0;
            while (true)
            {
                uint8_t two[2];
                if (!readBytes(*p, two, 2))
                    return false;
                *p += 2;
                uint16_t sample = two[0] | (two[1] << 8);
                total += sample;
                if (sample != 0xFFFF)
                    break;
            }
            *cycles = (uint32_t)((total * machineClock() + counter_rate / 2) / counter_rate);
            return true;
        }

        case TAPE_KIND_HTAP:
        {
            // One halfwave. Pulses (<= 10 ms) are one signed 16-bit LE
            // value: bit 15 = polarity, bits 0-14 = duration in 0.5 us
            // ticks. Pauses (> 10 ms) are four 16-bit values: 0x0000
            // 0x0000, then duration in us as (word1 << 16) | word2.
            uint8_t two[2];
            if (!readBytes(*p, two, 2))
                return false;
            *p += 2;
            uint16_t w = two[0] | (two[1] << 8);

            uint64_t ticks; // 0.5 us units
            if ((w & 0x7FFF) == 0)
            {
                uint8_t rest[6];
                if (!readBytes(*p, rest, 6))
                    return false;
                *p += 6;
                uint16_t hi = rest[2] | (rest[3] << 8);
                uint16_t lo = rest[4] | (rest[5] << 8);
                uint64_t us = ((uint32_t)hi << 16) | lo;
                ticks = us * 2;
            }
            else
            {
                ticks = w & 0x7FFF;
            }

            *cycles = (uint32_t)((ticks * machineClock() + 1000000) / 2000000);
            return true;
        }
    }
    return false;
}

/********************************************************
 * TAPClean scan
 ********************************************************/

// Engine PRG database -> entries, with listing filters applied
void TapeDecoder::harvestEntries(int nprg)
{
    // Copy programs out of the engine, in tape order. Loader-internal
    // header blocks (CBM and turbo headers) are folded into the following
    // data program: they contribute their name and are skipped as entries.
    // Duplicate copies (CBM second copy, repeated turbo blocks) are also
    // skipped.
    std::string pending_name;
    uint32_t pending_start = 0;
    bool have_pending = false;
    uint32_t prev_off = 20;
    uint32_t prev_ms = 0;
    std::vector<bool> entry_is_cbm;   // parallel to 'entries'

    for (int i = 0; i < nprg; i++)
    {
        tapclean_prg_t p;
        if (!tapclean_get_prg(i, &p))
            break;

        bool is_header = p.is_cbm_header ||
            (p.type_name != nullptr && strstr(p.type_name, "HEADER") != nullptr);
        if (is_header)
        {
            // remember the header name/position for the data that follows
            if (!have_pending)
            {
                pending_name = p.name ? p.name : "";
                pending_start = p.tap_start;
                have_pending = true;
            }
            continue;
        }

        // Skip repeat copies: same memory range and identical content as
        // the previous program
        if (!entries.empty())
        {
            TapeEntry &prev = entries.back();
            size_t datalen = (size_t)p.size;
            if (prev.start_addr == p.start_addr && prev.end_addr == p.end_addr &&
                prev.prg.size() == datalen + 2 && p.data != nullptr &&
                memcmp(prev.prg.data() + 2, p.data, datalen) == 0)
            {
                prev.tape_end_offset = p.tap_end;
                prev.end_time_ms = prev_ms +
                    tapclean_duration_ms((int)prev_off, p.tap_end);
                prev_ms = prev.end_time_ms;
                prev_off = p.tap_end;
                have_pending = false;
                continue;
            }
        }

        TapeEntry e;
        std::string name = (p.name && p.name[0]) ? p.name : "";
        // drop the engine's "_DATA" filename suffix (turbo data blocks)
        if (name.size() >= 5 && name.compare(name.size() - 5, 5, "_DATA") == 0)
            name.resize(name.size() - 5);
        if (name.empty() && have_pending)
            name = pending_name;
        mstr::rtrim(name);
        if (!name.empty())
            e.name = mstr::toPETSCII2(name);
        e.loader = p.type_name ? p.type_name : "";
        e.start_addr = (uint16_t)p.start_addr;
        e.end_addr = (uint16_t)p.end_addr;
        // The program starts at its header (if one preceded it)
        e.tape_offset = have_pending ? pending_start : (uint32_t)p.tap_start;
        e.tape_end_offset = (uint32_t)p.tap_end;
        e.checksum_ok = (p.read_errors == 0);

        e.start_time_ms = prev_ms + tapclean_duration_ms((int)prev_off, (int)e.tape_offset);
        e.end_time_ms = e.start_time_ms +
            tapclean_duration_ms((int)e.tape_offset, (int)e.tape_end_offset);
        prev_off = e.tape_end_offset;
        prev_ms = e.end_time_ms;

        e.prg.resize((size_t)p.size + 2);
        e.prg[0] = p.start_addr & 0xFF;
        e.prg[1] = (p.start_addr >> 8) & 0xFF;
        if (p.data != nullptr && p.size > 0)
            memcpy(e.prg.data() + 2, p.data, p.size);

        Debug_printv("Tape file: name[%s] loader[%s] addr[%04X-%04X] size[%u] csum[%d] tape[%lu-%lu] time[%lu-%lu ms]",
                     e.name.c_str(), e.loader.c_str(), e.start_addr, e.end_addr,
                     (unsigned)e.prg.size(), e.checksum_ok ? 1 : 0,
                     (unsigned long)e.tape_offset, (unsigned long)e.tape_end_offset,
                     (unsigned long)e.start_time_ms, (unsigned long)e.end_time_ms);

        entries.push_back(std::move(e));
        entry_is_cbm.push_back(p.is_cbm_data != 0);
        have_pending = false;
    }

    // Turbo-loader tapes: the Kernal-loaded block is just the boot stub
    // for the loader system - only the turbo payload is loadable through
    // Meatloaf. Drop each CBM block that a non-CBM program follows,
    // passing its name (the game's name) to that program. Pure Kernal
    // tapes (all blocks CBM) keep every entry.
    for (size_t i = 0; i + 1 < entries.size(); )
    {
        if (entry_is_cbm[i] && !entry_is_cbm[i + 1])
        {
            if (entries[i + 1].name.empty())
                entries[i + 1].name = entries[i].name;
            entries.erase(entries.begin() + i);
            entry_is_cbm.erase(entry_is_cbm.begin() + i);
        }
        else
        {
            i++;
        }
    }
}

/********************************************************
 * Progressive scanning (plain TAP)
 ********************************************************/

// Append one pulse in TAP v1 encoding to the converted image
bool TapeDecoder::appendValue(uint32_t cycles)
{
    if (fetched + 4 > image_cap)
    {
        uint32_t ncap = image_cap + image_cap / 2;
        uint8_t *nbuf = image_alloc(ncap);
        if (nbuf == nullptr)
            return false;
        memcpy(nbuf, image, fetched);
        free(image);
        image = nbuf;
        image_cap = ncap;
    }

    if (cycles >= 8 && cycles <= 255 * 8)
    {
        image[fetched++] = (uint8_t)((cycles + 4) / 8);
    }
    else
    {
        // long pulse/pause: v1 zero marker + 24-bit cycle count
        image[fetched++] = 0;
        image[fetched++] = cycles & 0xFF;
        image[fetched++] = (cycles >> 8) & 0xFF;
        image[fetched++] = (cycles >> 16) & 0xFF;
    }
    return true;
}

// 'target' is a CONTAINER offset: raw TAP fetches image bytes up to it;
// conversion formats consume container values up to it, appending the
// converted pulses to 'image'
bool TapeDecoder::fetchTo(uint32_t target)
{
    if (image == nullptr)
    {
        image_cap = container_len + 4096;
        image = image_alloc(image_cap);
        if (image == nullptr)
        {
            Debug_printv("No memory for tape image (%lu bytes)", image_cap);
            return false;
        }
        fetched = 0;

        if (converting)
        {
            // Synthesize the TAP v1 header the engine will parse; the
            // size field is patched per window in scanWindow()
            memset(image, 0, 20);
            memcpy(image, "C64-TAPE-RAW", 12);
            image[0x0C] = 1;
            image[0x0D] = platform;
            image[0x0E] = video;
            fetched = 20;
            conv_pos = data_start;
        }
    }

    if (target > container_len)
        target = container_len;

    if (!converting)
    {
        // Raw TAP: bulk sequential read straight into the buffer
        if (fetched >= target)
            return true;
        if (stream == nullptr || !stream->seek(fetched))
            return false;

        uint32_t next_report = fetched;
        while (fetched < target)
        {
            if (fetched >= next_report)
            {
                Serial.printf("Reading tape image: %lu/%lu KB\r\n",
                              (unsigned long)(fetched / 1024),
                              (unsigned long)(container_len / 1024));
                next_report = fetched + 64 * 1024;
            }
            uint32_t got = stream->read(image + fetched, target - fetched);
            if (got == 0)
            {
                Debug_printv("Tape image read failed at %lu of %lu", fetched, target);
                return false;
            }
            fetched += got;
        }
        return true;
    }

    // Streamed conversion: pull (half)waves from the container until its
    // read cursor passes 'target'
    uint32_t next_report = conv_pos;
    uint32_t cycles;
    while (!conv_eof && conv_pos < target)
    {
        if (conv_pos >= next_report)
        {
            Serial.printf("Converting tape image: %lu/%lu KB\r\n",
                          (unsigned long)(conv_pos / 1024),
                          (unsigned long)(container_len / 1024));
            next_report = conv_pos + 64 * 1024;
        }

        if (!nextValue(&conv_pos, &cycles))
        {
            // Well short of the end this is a read error, not EOF -
            // leave state intact so a later request can resume
            if (container_len - conv_pos >= 8)
            {
                Debug_printv("Tape read failed at %lu of %lu", conv_pos, container_len);
                return false;
            }
            conv_eof = true;
            break;
        }
        // Halfwave sources (HTAP, TAP v2): merge pairs into full waves
        if (halfwaves)
        {
            uint32_t second;
            if (nextValue(&conv_pos, &second))
                cycles += second;
        }
        if (!appendValue(cycles))
            return false;
    }

    if (conv_pos >= container_len)
        conv_eof = true;   // cursor at the end: container exhausted

    len = fetched;   // converted length known so far
    return true;
}

bool TapeDecoder::scanWindow(uint32_t win)
{
    std::lock_guard<std::mutex> lock(s_tapclean_mutex);

    bool complete = converting ? conv_eof : (win >= container_len);

    int machine = TAPCLEAN_MACHINE_C64;
    if (platform == 1) machine = TAPCLEAN_MACHINE_VIC20;
    if (platform == 2) machine = TAPCLEAN_MACHINE_C16;

    Serial.printf("Analyzing tape: %lu/%lu KB\r\n",
                  (unsigned long)((converting ? conv_pos : win) / 1024),
                  (unsigned long)(container_len / 1024));

    if (converting)
    {
        // Patch the synthesized header's size field for this window
        uint32_t dlen = win - 20;
        image[0x10] = dlen & 0xFF;
        image[0x11] = (dlen >> 8) & 0xFF;
        image[0x12] = (dlen >> 16) & 0xFF;
        image[0x13] = (dlen >> 24) & 0xFF;
    }

    // The engine borrows 'image' - no copy, and it stays ours for the
    // next (larger) window
    if (!tapclean_load_buffer_ref(image, win, machine, video ? 1 : 0))
    {
        tapclean_shutdown();
        return false;
    }

    int nprg = tapclean_analyze_tap(1 /* unite neighbouring blocks */);
    if (nprg < 0)
    {
        tapclean_shutdown();
        return false;
    }

    Debug_printv("TAPClean: recognized[%d%%] programs[%d] window[%lu] complete[%d]",
                 tapclean_detected_percent(), nprg,
                 (unsigned long)win, complete ? 1 : 0);

    entries.clear();
    harvestEntries(nprg);

    // A partial window cannot confirm its last entry (the window edge may
    // have truncated it): withhold it until a later entry or the tape end
    // proves it complete
    if (!complete && !entries.empty())
        entries.pop_back();

    if (complete)
        total_ms = tapclean_tap_time_ms();

    tapclean_shutdown();
    return true;
}

bool TapeDecoder::extendScan()
{
    // Window growth is measured in CONTAINER bytes (what actually gets
    // transferred); for conversion formats that is the read cursor
    uint32_t progress = converting ? conv_pos : fetched;
    uint32_t target = (progress == 0) ? (512u * 1024) : (progress * 2);
    // Don't leave a tiny tail for one more round trip
    if (target > container_len || (container_len - target) < 128u * 1024)
        target = container_len;

    // On failure (e.g. network error) leave state as-is: the next
    // directory request retries from where the fetch stopped
    if (!fetchTo(target) || !scanWindow(fetched))
        return false;

    if (converting ? conv_eof : (fetched >= container_len))
    {
        fully_scanned = true;
        finishScan();
    }
    return true;
}

void TapeDecoder::finishScan()
{
    // The whole image has been analyzed and every entry copied out
    free(image);
    image = nullptr;
}

TapeDecoder::~TapeDecoder()
{
    free(image);
}

/********************************************************
 * Entry serving
 ********************************************************/

bool TapeDecoder::nextProgram(uint32_t from_offset, TapeEntry &out)
{
    if (!opened)
        return false;

    while (true)
    {
        for (auto &e : entries)
        {
            if (e.tape_end_offset > from_offset)
            {
                out = e;
                return true;
            }
        }
        if (fully_scanned)
            return false;
        if (!extendScan())
            return false;
    }
}

uint32_t TapeDecoder::offsetAtTime(uint32_t ms)
{
    if (!opened || ms == 0)
        return data_start;

    // Snap to the program grid: the first program still (partly) ahead of
    // this counter position - exactly what nextProgram() will serve next
    while (true)
    {
        for (auto &e : entries)
        {
            if (e.end_time_ms > ms)
                return e.tape_offset;
        }
        if (fully_scanned)
            break;
        if (!extendScan())
            break;
    }
    return len; // past the last program: end of tape
}

uint32_t TapeDecoder::totalMs()
{
    // Needs the whole tape measured
    while (opened && !fully_scanned)
    {
        if (!extendScan())
            break;
    }
    return total_ms;
}
