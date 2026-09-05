#!/usr/bin/env python3
"""Encode a .d81 into a .g81 so the G81 read path has something to test against.

READ THIS BEFORE TRUSTING A PASSING TEST. There is no .g81 in .data/media, VICE has
no MFM-1581 support, and the P64 reference implementation does not know the
format either. The only specification is the four-line note at the top of
lib/meatloaf/media/disk/g81.h, and this script encodes the SAME reading of it
that g81.cpp decodes:

    0x00  8   "MFM-1581"
    0x08  1   version
    0x09  1   number of tracks (cylinder * 2 + head entries)
    0x0A  2   maximum track size
    0x0C  N*4 offset table - NO speed zone table follows the header
    ...   4   per track: length in BITS, then that many cells

So a passing round trip proves the decoder and this generator agree, not that
either agrees with a real .g81. What it does check independently is the MFM
layer itself, which is shared with the .p81 reader and is validated there
against a real 1581 flux image.

    python3 make_g81.py input.d81 output.g81
"""
import struct
import sys

CELL_BITS = 16          # one MFM byte is 16 cells
SECTOR_BYTES = 512
SECTORS_PER_TRACK = 10
CYLINDERS = 80
HEADS = 2

# An $A1 with its clock bit between bits 4 and 3 suppressed. This pattern cannot
# occur in encoded data, which is what makes it a sync.
SYNC_A1 = 0x4489


def crc16(data, crc=0xffff):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xffff if crc & 0x8000 else (crc << 1) & 0xffff
    return crc


class Bits:
    """Accumulates MFM cells, MSB first, tracking the previous data bit so the
    clock bits can be filled in per the MFM rule: a clock is written only when
    both the previous and current data bits are zero."""

    def __init__(self):
        self.bits = bytearray()
        self.last = 0

    def raw(self, value, count):
        for i in range(count - 1, -1, -1):
            self.bits.append((value >> i) & 1)
        self.last = value & 1

    def byte(self, value):
        for i in range(7, -1, -1):
            bit = (value >> i) & 1
            clock = 0 if (self.last or bit) else 1
            self.bits.append(clock)
            self.bits.append(bit)
            self.last = bit

    def bytes_(self, data):
        for b in data:
            self.byte(b)

    def sync(self):
        # The $A1 mark is written raw so the missing clock survives.
        self.raw(SYNC_A1, 16)
        self.last = 1       # $A1's low bit

    def pack(self):
        out = bytearray((len(self.bits) + 7) // 8)
        for i, bit in enumerate(self.bits):
            if bit:
                out[i >> 3] |= 0x80 >> (i & 7)
        return bytes(out), len(self.bits)


def build_track(cylinder, head, image):
    t = Bits()

    t.bytes_(b"\x4e" * 32)          # post-index gap

    for sector in range(1, SECTORS_PER_TRACK + 1):
        # Where this physical sector's 512 bytes live in the .d81: two CBM
        # blocks per sector, twenty blocks per head, forty per cylinder.
        block = (head * 20) + ((sector - 1) * 2)
        offset = (cylinder * 40 + block) * 256
        payload = image[offset:offset + SECTOR_BYTES]
        if len(payload) < SECTOR_BYTES:
            payload = payload + bytes(SECTOR_BYTES - len(payload))

        t.bytes_(b"\x00" * 12)
        for _ in range(3):
            t.sync()
        header = bytes([0xFE, cylinder, head, sector, 0x02])
        t.bytes_(header)
        t.bytes_(struct.pack(">H", crc16(b"\xa1\xa1\xa1" + header)))

        t.bytes_(b"\x4e" * 22)      # header gap
        t.bytes_(b"\x00" * 12)
        for _ in range(3):
            t.sync()
        t.byte(0xFB)
        t.bytes_(payload)
        t.bytes_(struct.pack(">H", crc16(b"\xa1\xa1\xa1" + b"\xfb" + payload)))

        t.bytes_(b"\x4e" * 54)      # inter-sector gap

    return t.pack()


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)

    image = open(sys.argv[1], "rb").read()

    entries = CYLINDERS * HEADS
    tracks = []
    longest = 0

    for cylinder in range(CYLINDERS):
        for head in range(HEADS):
            cells, bits = build_track(cylinder, head, image)
            tracks.append((cells, bits))
            longest = max(longest, len(cells))

    out = bytearray(b"MFM-1581")
    out += bytes([0])                       # version
    out += bytes([entries])                 # one entry per cylinder/head
    out += struct.pack("<H", min(longest, 0xffff))

    base = len(out) + entries * 4
    offsets = bytearray()
    data = bytearray()

    for cells, bits in tracks:
        offsets += struct.pack("<I", base + len(data))
        data += struct.pack("<I", bits)
        data += cells

    out += offsets + data
    open(sys.argv[2], "wb").write(out)
    print(f"{sys.argv[2]}: {len(out)} bytes, {entries} tracks, "
          f"longest {longest} bytes ({longest * 8} cells)")


if __name__ == "__main__":
    main()
