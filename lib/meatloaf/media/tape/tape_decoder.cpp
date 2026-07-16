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

#include "meatloaf.h"

#include "../../../../include/debug.h"

#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

extern "C" {
#include "wavprg_types.h"
#include "wav2prg_api.h"
#include "wav2prg_input.h"
#include "wav2prg_core.h"
#include "wav2prg_block_list.h"
#include "wav2prg_display_interface.h"
#include "loaders.h"
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
            if (p >= len || stream == nullptr || !stream->seek(p))
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
    cursor_cycles = 0;
    time_valid = true;
    total_known = false;
    total_ms = 0;
    locked_loader.clear();
    fallback_done = false;
    trial_end = 0;
    resetContinuation();

    if (stream == nullptr)
        return false;

    len = stream->size();
    if (len < 20)
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

    opened = true;
    Debug_printv("Tape image: kind[%d] version[%d] platform[%d] video[%d] size[%lu]",
                 kind, version, platform, video, len);
    return true;
}

// Read one duration value (in machine cycles) at *p, advancing it;
// false at end of data (or the current trial window)
bool TapeDecoder::nextValue(uint32_t *p, uint32_t *cycles)
{
    // Bounded start-loader trials stop at trial_end
    if (trial_end != 0 && *p >= trial_end)
        return false;

    switch (kind)
    {
        case TAPE_KIND_TAP:
        {
            uint8_t b;
            if (*p >= len || !readBytes(*p, &b, 1))
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
 * wav2prg input callbacks
 ********************************************************/

struct tape_io {
    static int32_t get_pos(struct wav2prg_input_object *object)
    {
        TapeDecoder *td = (TapeDecoder *)object->object;
        return (int32_t)td->pos;
    }

    static uint8_t set_pos(struct wav2prg_input_object *object, uint32_t p)
    {
        TapeDecoder *td = (TapeDecoder *)object->object;
        if (p < td->data_start)
            p = td->data_start;
        if (p > td->len)
            p = td->len;
        td->pos = p;
        return 1;
    }

    static enum wav2prg_bool get_pulse(struct wav2prg_input_object *object, uint32_t *pulse)
    {
        TapeDecoder *td = (TapeDecoder *)object->object;
        uint32_t v;

        if (!td->nextValue(&td->pos, &v))
        {
            td->markEofReached();
            return wav2prg_false;
        }
        td->cursor_cycles += v;

        if (td->halfwaves)
        {
            // Combine two halfwaves into one full pulse
            uint32_t v2;
            if (!td->nextValue(&td->pos, &v2))
            {
                td->markEofReached();
                return wav2prg_false;
            }
            td->cursor_cycles += v2;
            v += v2;
        }

        *pulse = v;
        return wav2prg_true;
    }

    static enum wav2prg_bool is_eof(struct wav2prg_input_object *object)
    {
        TapeDecoder *td = (TapeDecoder *)object->object;
        uint32_t end = (td->trial_end != 0 && td->trial_end < td->len) ? td->trial_end : td->len;
        return (td->pos >= end) ? wav2prg_true : wav2prg_false;
    }

    static void invert(struct wav2prg_input_object *object)
    {
        TapeDecoder *td = (TapeDecoder *)object->object;
        // Only meaningful for halfwave formats: consume one halfwave to
        // shift the phase by 180 degrees
        if (td->halfwaves)
        {
            uint32_t v;
            if (td->nextValue(&td->pos, &v))
                td->cursor_cycles += v;
        }
    }

    static void close(struct wav2prg_input_object *object)
    {
        (void)object;
    }
};

static struct wav2prg_input_functions tape_input_functions = {
    tape_io::get_pos,
    tape_io::set_pos,
    tape_io::get_pulse,
    tape_io::is_eof,
    tape_io::invert,
    tape_io::close
};

/********************************************************
 * wav2prg display interface (progress -> task yield)
 ********************************************************/

static void disp_try_sync(struct display_interface_internal *internal, const char *loader_name, const char *observation)
{
    (void)internal; (void)loader_name; (void)observation;
}

static void disp_sync(struct display_interface_internal *internal, uint32_t info_pos, struct program_block_info *info)
{
    (void)internal; (void)info_pos; (void)info;
}

static void disp_progress(struct display_interface_internal *internal, uint32_t pos)
{
    (void)internal; (void)pos;
#if defined(ESP_PLATFORM)
    vTaskDelay(0); // yield so the analysis loop doesn't starve other tasks
#endif
}

static void disp_block_progress(struct display_interface_internal *internal, uint16_t pos)
{
    (void)internal; (void)pos;
}

static void disp_checksum(struct display_interface_internal *internal, enum wav2prg_checksum_state state,
                          uint32_t start, uint32_t end, uint8_t expected, uint8_t computed)
{
    (void)internal; (void)state; (void)start; (void)end; (void)expected; (void)computed;
}

static void disp_end(struct display_interface_internal *internal, unsigned char valid, enum wav2prg_checksum_state state,
                     char has_checksum, uint32_t num_syncs, struct block_syncs *syncs, uint32_t last_valid_pos,
                     uint16_t bytes, enum wav2prg_block_filling filling)
{
    (void)internal; (void)valid; (void)state; (void)has_checksum; (void)num_syncs;
    (void)syncs; (void)last_valid_pos; (void)bytes; (void)filling;
}

static struct wav2prg_display_interface tape_display_interface = {
    disp_try_sync,
    disp_sync,
    disp_progress,
    disp_block_progress,
    disp_checksum,
    disp_end
};

/********************************************************
 * Program extraction
 ********************************************************/

TapeDecoder::~TapeDecoder()
{
    resetContinuation();
}

void TapeDecoder::resetContinuation()
{
    if (cont != nullptr)
    {
        wav2prg_continuation_free(cont);
        cont = nullptr;
    }
    cont_pos = 0;
    last_valid = false;
}

bool TapeDecoder::nextProgram(uint32_t from_offset, TapeEntry &out)
{
    if (!opened)
        return false;

    register_loaders();

    // The carried loader state is only valid when resuming exactly where
    // the previous call stopped
    if (cont != nullptr && from_offset != cont_pos)
        resetContinuation();

    uint32_t target = (from_offset > data_start) ? from_offset : data_start;
    bool rewound = false;
    if (target <= data_start)
    {
        // Rewind to the start of the tape
        pos = data_start;
        cursor_cycles = 0;
        time_valid = true;
        fallback_done = false; // allow the standalone-loader trials again
        rewound = true;
    }
    else if (target != pos)
    {
        // Arbitrary jump (e.g. a .idx offset): counter time unknown here
        pos = target;
        time_valid = false;
    }
    if (pos >= len)
        return false;

    const char *base_loader = (platform == 2) ? "Default C16" : "Default C64";
    const char *start_loader = locked_loader.empty() ? base_loader : locked_loader.c_str();

    if (analyzeChain(start_loader, from_offset, out))
        return true;

    // Fallback: the tape doesn't start with a Kernal boot block. On a fresh
    // scan of the tape start, try the standalone primary loaders (Pavloda,
    // Turbo 220, Microload, ...) as the start loader and lock the first that
    // decodes a program. Only runs once per scan and only when nothing is
    // already locked.
    static const char *kFallbackLoaders[] = {
        "Turbo Tape 64", "Turbo Tape 64 fast", "Turbo 220",
        "Pavloda", "Pavloda Old", "Pavloda Penetrator",
        "Microload", "Nobby", "Atlantis", "Alien", "Ash & Dave",
        "The Edge", "Wizard Development", "Jetload", "Tequila Sunrise",
        "Novaload Normal"
    };

    if (locked_loader.empty() && !fallback_done && rewound)
    {
        fallback_done = true;

        // Each trial scans at most ~1 MB of tape for its first program
        uint32_t limit = data_start + 1024 * 1024;
        for (const char *cand : kFallbackLoaders)
        {
            resetContinuation();
            pos = data_start;
            cursor_cycles = 0;
            time_valid = true;
            trial_end = (limit < len) ? limit : len;

            bool ok = analyzeChain(cand, data_start, out);
            trial_end = 0;

            if (ok)
            {
                Debug_printv("Locked start loader [%s] for this tape", cand);
                locked_loader = cand;
                return true;
            }
        }

        // Nothing matched: leave the tape at EOF
        resetContinuation();
        pos = len;
    }

    return false;
}

bool TapeDecoder::analyzeChain(const char *start_loader, uint32_t from_offset, TapeEntry &out)
{
    struct wav2prg_input_object input_object = { this };

    // Incremental analysis: each call decodes up to the next kept block,
    // carrying the loader/observer chain in 'cont'
    while (true)
    {
        uint64_t iter_start_cycles = cursor_cycles;

        struct block_list_element *blocks = wav2prg_analyse(
            start_loader,
            NULL,
            wav2prg_false,
            &cont,
            &input_object,
            &tape_input_functions,
            &tape_display_interface,
            NULL);

        bool found = false;

        for (struct block_list_element *b = blocks; b != NULL; )
        {
            do
            {
                if (found)
                    break;

                if (b->block_status != block_list_element::block_complete &&
                    b->block_status != block_list_element::block_checksum_expected_but_missing)
                    break;

                // Kernal header chunks only carry metadata for the block
                // that follows them - they are not files themselves
                if (b->loader_name != NULL &&
                    (strcmp(b->loader_name, "Default C64") == 0 || strcmp(b->loader_name, "Default C16") == 0))
                    break;

                if (b->real_end <= b->real_start)
                    break;

                std::string bname = std::string(b->block.info.name, 16);
                while (!bname.empty() && (bname.back() == ' ' || bname.back() == '\0'))
                    bname.pop_back();
                uint32_t blen = (uint32_t)(b->real_end - b->real_start) + 2;

                // Skip repeated blocks (e.g. the Kernal's second copy)
                if (last_valid && last_start == b->real_start && last_end == b->real_end &&
                    last_len == blen && last_name == bname)
                    break;

                out.name = bname;
                out.loader = b->loader_name ? b->loader_name : "unknown";
                out.start_addr = b->real_start;
                out.end_addr = b->real_end;
                out.tape_offset = b->num_of_syncs ? b->syncs[0].start_sync : from_offset;
                out.checksum_ok = (b->state == wav2prg_checksum_state_correct);

                uint16_t plen = b->real_end - b->real_start;
                uint16_t data_off = b->real_start - b->block.info.start;
                out.prg.clear();
                out.prg.reserve(plen + 2);
                out.prg.push_back(b->real_start & 0xFF);
                out.prg.push_back(b->real_start >> 8);
                out.prg.insert(out.prg.end(), &b->block.data[data_off], &b->block.data[data_off] + plen);

                found = true;
            } while (0);

            struct block_list_element *next = b->next;
            free_block_list_element(b);
            b = next;
        }

        // Where this incremental step stopped: the next call resumes here
        cont_pos = pos;

        if (found)
        {
            out.tape_end_offset = pos;

            last_valid = true;
            last_start = out.start_addr;
            last_end = out.end_addr;
            last_len = out.prg.size();
            last_name = out.name;

            // Tape counter times, accumulated while the pulses streamed by
            out.start_time_ms = time_valid ? cyclesToMs(iter_start_cycles) : 0;
            out.end_time_ms = time_valid ? cyclesToMs(cursor_cycles) : 0;

            Debug_printv("Tape file: name[%s] loader[%s] addr[%04X-%04X] size[%u] csum[%d] tape[%lu-%lu] time[%lu-%lu ms]",
                         out.name.c_str(), out.loader.c_str(), out.start_addr, out.end_addr,
                         (unsigned)out.prg.size(), out.checksum_ok, out.tape_offset, out.tape_end_offset,
                         out.start_time_ms, out.end_time_ms);
            return true;
        }

        if (cont == nullptr)
        {
            // End of the tape with nothing more to return
            last_valid = false;
            return false;
        }

        // A header chunk or repeated block was consumed - keep going
        from_offset = pos;
    }
}

/********************************************************
 * Tape counter
 ********************************************************/

uint32_t TapeDecoder::cyclesToMs(uint64_t cycles) const
{
    return (uint32_t)((cycles * 1000) / machineClock());
}

void TapeDecoder::markEofReached()
{
    // Latch the tape duration when streaming naturally reaches the end
    // (a truncated final value still counts as the end)
    if (time_valid && !total_known && pos + 8 >= len)
    {
        total_ms = cyclesToMs(cursor_cycles);
        total_known = true;
    }
}

uint32_t TapeDecoder::offsetAtTime(uint32_t ms)
{
    if (!opened)
        return 0;

    uint64_t target_cycles = ((uint64_t)ms * machineClock()) / 1000;

    // Rewind when the target lies behind the cursor, or the cursor's time
    // is unknown after an arbitrary jump
    if (!time_valid || target_cycles < cursor_cycles)
    {
        pos = data_start;
        cursor_cycles = 0;
        time_valid = true;
    }

    uint32_t value;
    while (cursor_cycles < target_cycles && pos < len)
    {
        if (!nextValue(&pos, &value))
            break;
        cursor_cycles += value;
    }
    markEofReached();

    return pos;
}

uint32_t TapeDecoder::totalMs()
{
    // Only known once the end of the tape has been reached by streaming
    return total_known ? total_ms : 0;
}
