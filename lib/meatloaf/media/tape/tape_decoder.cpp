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
 * Pulse input: TAP v0/v1/v2, DC2N DMP, HTAP over a memory buffer
 ********************************************************/

enum tape_kind {
    TAPE_KIND_TAP,
    TAPE_KIND_DMP,
    TAPE_KIND_HTAP,
    TAPE_KIND_UNKNOWN
};

struct tape_input_state {
    const uint8_t *data;
    uint32_t len;
    uint32_t pos;           // current byte offset within the image
    enum tape_kind kind;
    uint8_t version;        // TAP version (0/1/2)
    uint8_t platform;       // 0=C64 1=VIC20 2=C16
    uint8_t video;          // 0=PAL 1=NTSC
    uint32_t data_start;
    uint32_t counter_rate;  // DMP/HTAP source sample rate in Hz
    bool halfwaves;         // TAP v2, HTAP: one value per halfwave
};

// Machine clock in Hz (cycles/second) for pulse duration conversion
static uint32_t machine_clock(const tape_input_state *st)
{
    switch (st->platform) {
        case 1:  return st->video ? 1022727 : 1108405; // VIC-20
        case 2:  return st->video ? 894886 : 886724;   // C16/Plus4
        default: return st->video ? 1022727 : 985248;  // C64
    }
}

static bool tape_parse_header(tape_input_state *st)
{
    const uint8_t *d = st->data;

    if (st->len >= 20 && memcmp(d, "C64-TAPE-RAW", 12) == 0)
    {
        st->kind = TAPE_KIND_TAP;
        st->version = d[0x0C];
        st->platform = d[0x0D];
        st->video = d[0x0E];
        st->data_start = 20;
        st->halfwaves = (st->version == 2);
        return true;
    }
    if (st->len >= 20 && memcmp(d, "DC2N-TAP-RAW", 12) == 0)
    {
        st->kind = TAPE_KIND_DMP;
        st->version = d[0x0C];
        st->platform = d[0x0D] & 0x0F;
        st->video = d[0x0E];
        st->counter_rate = d[0x10] | (d[0x11] << 8) | (d[0x12] << 16) | ((uint32_t)d[0x13] << 24);
        if (st->counter_rate == 0)
            st->counter_rate = 2000000;
        st->data_start = 20;
        st->halfwaves = false;
        return true;
    }
    if (st->len >= 20 && memcmp(d + 6, "-HIRES", 6) == 0)
    {
        // HTAP (Manosoft, spec V0 sub 2.0): hardware id at 0x00, "-HIRES"
        // at 0x06, version 0x0C, machine 0x0D (0=C64/128 1=VIC20/PET
        // 2=C16/+4), video 0x0E, reserved 0x0F-0x13, halfwave data at 0x14
        st->kind = TAPE_KIND_HTAP;
        st->version = d[0x0C];
        st->platform = d[0x0D];
        st->video = d[0x0E];
        st->counter_rate = 2000000; // pulse halfwaves are 0.5 us ticks
        st->data_start = 20;
        st->halfwaves = true;
        return true;
    }

    st->kind = TAPE_KIND_UNKNOWN;
    return false;
}

// Read one duration value (in machine cycles); false at end of data
static bool tape_next_value(tape_input_state *st, uint32_t *cycles)
{
    switch (st->kind)
    {
        case TAPE_KIND_TAP:
        {
            if (st->pos >= st->len)
                return false;
            uint8_t b = st->data[st->pos++];
            if (b != 0)
            {
                *cycles = (uint32_t)b * 8;
                return true;
            }
            if (st->version == 0)
            {
                *cycles = 20000; // v0 overflow marker: a long pause
                return true;
            }
            if (st->pos + 3 > st->len)
                return false;
            *cycles = st->data[st->pos] | (st->data[st->pos + 1] << 8) | ((uint32_t)st->data[st->pos + 2] << 16);
            st->pos += 3;
            return true;
        }

        case TAPE_KIND_DMP:
        {
            // 16-bit LE samples; 0xFFFF = counter overflow, accumulate
            uint64_t total = 0;
            while (true)
            {
                if (st->pos + 2 > st->len)
                    return false;
                uint16_t sample = st->data[st->pos] | (st->data[st->pos + 1] << 8);
                st->pos += 2;
                total += sample;
                if (sample != 0xFFFF)
                    break;
            }
            // Convert from counter units to machine cycles
            *cycles = (uint32_t)((total * machine_clock(st) + st->counter_rate / 2) / st->counter_rate);
            return true;
        }

        case TAPE_KIND_HTAP:
        {
            // One halfwave. Pulses (<= 10 ms) are one signed 16-bit LE
            // value: bit 15 = polarity, bits 0-14 = duration in 0.5 us
            // ticks. Pauses (> 10 ms) are four 16-bit values: 0x0000
            // 0x0000, then duration in us as (word1 << 16) | word2.
            if (st->pos + 2 > st->len)
                return false;
            uint16_t w = st->data[st->pos] | (st->data[st->pos + 1] << 8);
            st->pos += 2;

            uint64_t ticks; // 0.5 us units
            if ((w & 0x7FFF) == 0)
            {
                // Pause marker (0x0000; 0x8000 is illegal but treat alike)
                if (st->pos + 6 > st->len)
                    return false;
                st->pos += 2; // second 0x0000 flag word
                uint16_t hi = st->data[st->pos] | (st->data[st->pos + 1] << 8);
                uint16_t lo = st->data[st->pos + 2] | (st->data[st->pos + 3] << 8);
                st->pos += 4;
                uint64_t us = ((uint32_t)hi << 16) | lo;
                ticks = us * 2;
            }
            else
            {
                ticks = w & 0x7FFF;
            }

            // 0.5 us ticks -> machine cycles
            *cycles = (uint32_t)((ticks * machine_clock(st) + 1000000) / 2000000);
            return true;
        }

        default:
            return false;
    }
}

/********************************************************
 * wav2prg input callbacks
 ********************************************************/

static int32_t tape_get_pos(struct wav2prg_input_object *object)
{
    tape_input_state *st = (tape_input_state *)object->object;
    return (int32_t)st->pos;
}

static uint8_t tape_set_pos(struct wav2prg_input_object *object, uint32_t pos)
{
    tape_input_state *st = (tape_input_state *)object->object;
    if (pos < st->data_start)
        pos = st->data_start;
    if (pos > st->len)
        pos = st->len;
    st->pos = pos;
    return 1;
}

static enum wav2prg_bool tape_get_pulse(struct wav2prg_input_object *object, uint32_t *pulse)
{
    tape_input_state *st = (tape_input_state *)object->object;
    uint32_t v;

    if (!tape_next_value(st, &v))
        return wav2prg_false;

    if (st->halfwaves)
    {
        // Combine two halfwaves into one full pulse
        uint32_t v2;
        if (!tape_next_value(st, &v2))
            return wav2prg_false;
        v += v2;
    }

    *pulse = v;
    return wav2prg_true;
}

static enum wav2prg_bool tape_is_eof(struct wav2prg_input_object *object)
{
    tape_input_state *st = (tape_input_state *)object->object;
    return (st->pos >= st->len) ? wav2prg_true : wav2prg_false;
}

static void tape_invert(struct wav2prg_input_object *object)
{
    tape_input_state *st = (tape_input_state *)object->object;
    // Only meaningful for halfwave formats: consume one halfwave to
    // shift the phase by 180 degrees
    if (st->halfwaves)
    {
        uint32_t v;
        tape_next_value(st, &v);
    }
}

static void tape_close(struct wav2prg_input_object *object)
{
    (void)object;
}

static struct wav2prg_input_functions tape_input_functions = {
    tape_get_pos,
    tape_set_pos,
    tape_get_pulse,
    tape_is_eof,
    tape_invert,
    tape_close
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
 * Analysis driver
 ********************************************************/

bool TapeDecoder::isTapeImage(const uint8_t *image, uint32_t image_len)
{
    tape_input_state st = {};
    st.data = image;
    st.len = image_len;
    return tape_parse_header(&st);
}

bool TapeDecoder::analyze(const uint8_t *image, uint32_t image_len,
                          uint32_t start_offset, bool single_program,
                          std::vector<TapeEntry> &entries)
{
    tape_input_state st = {};
    st.data = image;
    st.len = image_len;

    if (!tape_parse_header(&st))
    {
        Debug_printv("Unrecognized tape image signature");
        return false;
    }

    st.pos = st.data_start;
    if (start_offset > st.data_start && start_offset < st.len)
        st.pos = start_offset;

    register_loaders();

    const char *start_loader = (st.platform == 2) ? "Default C16" : "Default C64";

    struct wav2prg_input_object input_object = { &st };

    Debug_printv("Analyzing tape: kind[%d] version[%d] platform[%d] video[%d] size[%lu] start[%lu]",
                 st.kind, st.version, st.platform, st.video, st.len, st.pos);

    struct block_list_element *blocks = wav2prg_analyse(
        start_loader,
        NULL,
        wav2prg_false,
        single_program ? wav2prg_true : wav2prg_false,
        &input_object,
        &tape_input_functions,
        &tape_display_interface,
        NULL);

    // Convert the block list into tape entries (PRG data)
    for (struct block_list_element *b = blocks; b != NULL; )
    {
        do
        {
            if (b->block_status != block_list_element::block_complete && b->block_status != block_list_element::block_checksum_expected_but_missing)
                break;

            // Kernal header chunks only carry metadata for the block that
            // follows them - they are not files themselves
            if (b->loader_name != NULL &&
                (strcmp(b->loader_name, "Default C64") == 0 || strcmp(b->loader_name, "Default C16") == 0))
                break;

            if (b->real_end <= b->real_start)
                break;

            TapeEntry e;
            e.name = std::string(b->block.info.name, 16);
            while (!e.name.empty() && (e.name.back() == ' ' || e.name.back() == '\0'))
                e.name.pop_back();
            e.loader = b->loader_name ? b->loader_name : "unknown";
            e.start_addr = b->real_start;
            e.end_addr = b->real_end;
            e.tape_offset = b->num_of_syncs ? b->syncs[0].start_sync : 0;
            e.tape_end_offset = b->num_of_syncs ? b->syncs[b->num_of_syncs - 1].end : e.tape_offset;
            e.checksum_ok = (b->state == wav2prg_checksum_state_correct);

            uint16_t len = b->real_end - b->real_start;
            uint16_t data_off = b->real_start - b->block.info.start;
            e.prg.reserve(len + 2);
            e.prg.push_back(b->real_start & 0xFF);
            e.prg.push_back(b->real_start >> 8);
            e.prg.insert(e.prg.end(), &b->block.data[data_off], &b->block.data[data_off] + len);

            // Skip repeated blocks (e.g. the Kernal's second copy)
            if (!entries.empty())
            {
                TapeEntry &prev = entries.back();
                if (prev.name == e.name && prev.start_addr == e.start_addr &&
                    prev.end_addr == e.end_addr && prev.prg.size() == e.prg.size())
                    break;
            }

            Debug_printv("Tape file: name[%s] loader[%s] addr[%04X-%04X] size[%u] csum[%d] offset[%lu]",
                         e.name.c_str(), e.loader.c_str(), e.start_addr, e.end_addr,
                         (unsigned)e.prg.size(), e.checksum_ok, e.tape_offset);

            entries.push_back(std::move(e));
        } while (0);

        struct block_list_element *next = b->next;
        free_block_list_element(b);
        b = next;
    }

    Debug_printv("Tape analysis found %d file(s)", entries.size());
    return true;
}

// Walk the pulse stream once, accumulating elapsed time; record the tape
// counter time at each entry's start/end offset and return total duration
uint32_t TapeDecoder::computeTimes(const uint8_t *image, uint32_t image_len,
                                   std::vector<TapeEntry> &entries)
{
    tape_input_state st = {};
    st.data = image;
    st.len = image_len;

    if (!tape_parse_header(&st))
        return 0;

    st.pos = st.data_start;

    uint64_t elapsed_cycles = 0;
    uint32_t clock = machine_clock(&st);

    // Entries are in tape order; track the next start/end offsets to stamp
    size_t next_start = 0, next_end = 0;

    uint32_t value;
    while (true)
    {
        uint32_t before = st.pos;
        uint64_t ms = (elapsed_cycles * 1000) / clock;

        while (next_start < entries.size() && entries[next_start].tape_offset <= before)
            entries[next_start++].start_time_ms = (uint32_t)ms;
        while (next_end < entries.size() && entries[next_end].tape_end_offset <= before)
            entries[next_end++].end_time_ms = (uint32_t)ms;

        if (!tape_next_value(&st, &value))
            break;
        elapsed_cycles += value; // each value is one (half)wave duration
    }

    uint32_t total_ms = (uint32_t)((elapsed_cycles * 1000) / clock);

    // Stamp anything left over (offsets at/past EOF)
    while (next_start < entries.size())
        entries[next_start++].start_time_ms = total_ms;
    while (next_end < entries.size())
        entries[next_end++].end_time_ms = total_ms;

    return total_ms;
}
