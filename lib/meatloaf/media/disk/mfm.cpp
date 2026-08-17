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

#include "mfm.h"


#include "../../../../include/debug.h"

namespace mfm
{

int findSync(const uint8_t *bits, uint32_t bytes, int p, int limit)
{
    if (bits == nullptr || bytes == 0)
        return -1;

    const int cells = (int)bytes * 8;
    if (p < 0 || p >= cells)
        p = 0;

    // A rolling 16-cell window compared against the sync pattern.
    uint32_t window = 0;
    int filled = 0;

    while (limit-- > 0)
    {
        uint32_t bit = (bits[p >> 3] >> (7 - (p & 7))) & 1;
        window = ((window << 1) | bit) & 0xffff;

        p++;
        if (p >= cells)
            p = 0;

        if (++filled >= 16 && window == SYNC_PATTERN)
            return p;
    }

    return -1;
}

bool readBytes(const uint8_t *bits, uint32_t bytes, int p, uint8_t *buf, int count)
{
    if (bits == nullptr || bytes == 0)
        return false;

    const int cells = (int)bytes * 8;

    for (int i = 0; i < count; i++)
    {
        uint8_t value = 0;
        for (int bit = 0; bit < 8; bit++)
        {
            int index = p + (i * 16) + (bit * 2) + 1;
            while (index >= cells)
                index -= cells;
            value = (uint8_t)((value << 1) | ((bits[index >> 3] >> (7 - (index & 7))) & 1));
        }
        buf[i] = value;
    }

    return true;
}

uint16_t crc16(const uint8_t *data, uint32_t length, uint16_t crc)
{
    while (length--)
    {
        crc ^= (uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

// Steps over any further $A1 syncs immediately following `p`, returning how
// many were seen in total (the caller has already consumed one) and leaving p
// just past the last.
static int skipExtraSyncs(const uint8_t *bits, uint32_t bytes, int &p)
{
    int marks = 1;
    while (true)
    {
        int next = findSync(bits, bytes, p, 16);
        if (next != p + 16)
            break;
        p = next;
        marks++;
    }
    return marks;
}

bool readSector(const uint8_t *bits, uint32_t bytes,
                uint8_t cylinder, uint8_t head, uint8_t sector,
                uint8_t *out, uint32_t size, bool *crc_ok)
{
    if (crc_ok != nullptr)
        *crc_ok = true;

    const int cells = (int)bytes * 8;

    int p = 0;
    int first = -1;

    for (int guard = 0; guard < 256; guard++)
    {
        p = findSync(bits, bytes, p, cells);
        if (p < 0)
            break;
        if (p == first)
            break;  // all the way round
        if (first < 0)
            first = p;

        int marks = skipExtraSyncs(bits, bytes, p);

        uint8_t mark = 0;
        if (!readBytes(bits, bytes, p, &mark, 1))
            break;

        if (mark != MARK_ID || marks < 3)
            continue;

        // FE, cylinder, head, sector, size code, then the CRC over all of it
        // INCLUDING the three $A1 bytes the controller counts but does not
        // store as data.
        uint8_t id[7];
        if (!readBytes(bits, bytes, p, id, sizeof(id)))
            break;

        uint8_t crc_input[8] = { 0xa1, 0xa1, 0xa1, id[0], id[1], id[2], id[3], id[4] };
        if (crc16(crc_input, sizeof(crc_input)) != (uint16_t)((id[5] << 8) | id[6]))
            continue;   // false sync, or a header this drive cannot read

        if (id[1] != cylinder || id[2] != head || id[3] != sector)
        {
            p += 7 * 16;
            continue;
        }

        // The data mark follows within a header gap.
        int d = findSync(bits, bytes, p + (7 * 16), 100 * 16);
        if (d < 0)
        {
            Debug_printv("no data mark for C%d H%d R%d", cylinder, head, sector);
            return false;
        }
        skipExtraSyncs(bits, bytes, d);

        uint8_t data_mark = 0;
        if (!readBytes(bits, bytes, d, &data_mark, 1))
            return false;
        if (data_mark != MARK_DATA && data_mark != MARK_DELETED_DATA)
        {
            Debug_printv("C%d H%d R%d data mark is [%02X]", cylinder, head, sector, data_mark);
            return false;
        }

        if (!readBytes(bits, bytes, d + 16, out, (int)size))
            return false;

        uint8_t tail[2] = { 0 };
        if (!readBytes(bits, bytes, d + 16 + ((int)size * 16), tail, 2))
            return false;

        uint16_t running = crc16((const uint8_t *)"\xa1\xa1\xa1", 3);
        running = crc16(&data_mark, 1, running);
        running = crc16(out, size, running);
        if (running != (uint16_t)((tail[0] << 8) | tail[1]))
        {
            Debug_printv("C%d H%d R%d data CRC error", cylinder, head, sector);
            if (crc_ok != nullptr)
                *crc_ok = false;
        }

        return true;
    }

    Debug_printv("C%d H%d R%d not found", cylinder, head, sector);
    return false;
}

} // namespace mfm
