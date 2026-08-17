#!/usr/bin/env python3
"""Encode a .d64 into a .nib, or a multi-pass .nb2, so the NIB read path has
something to test against.

There is no .nib, .nb2 or .nbz anywhere in .archive and the firmware has a GCR
decoder but no encoder, so the fixture is generated here. Layout per the header
comment in lib/meatloaf/media/disk/nib.h:

    0x00  13  "MNIB-1541-RAW"
    0x0D  1   version
    0x0E  2   unused
    0x10  ..  track table, two bytes per stored track: half track, density
    0x100 ..  the tracks, in table order, one fixed-size window each

A .nib stores one pass of each track; a .nb2 stores several, one after another,
and nothing in the header says how many - the reader derives that from the file
length, so the same code reads either. Pass -p N to write N passes.

Unlike a .g64 there is no per-track length: the window is a fixed size and
whatever the nibbler did not fill is padding.

    python3 make_nib.py input.d64 output.nib
    python3 make_nib.py -p 16 input.d64 output.nb2
"""
import struct
import sys

SECTORS_PER_TRACK = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 5
DENSITY           = [3] * 17 + [2] * 7 + [1] * 6 + [0] * 5
TRACK_LENGTH      = 0x2000
HEADER_SIZE       = 0x100

GCR_ENCODE = [
    0x0a, 0x0b, 0x12, 0x13, 0x0e, 0x0f, 0x16, 0x17,
    0x09, 0x19, 0x1a, 0x1b, 0x0d, 0x1d, 0x1e, 0x15,
]


def gcr_encode(block):
    """4 plain bytes -> 5 GCR bytes, repeated over the whole block."""
    out = bytearray()
    for i in range(0, len(block), 4):
        chunk = block[i:i + 4]
        bits = 0
        for b in chunk:
            bits = (bits << 10) | (GCR_ENCODE[b >> 4] << 5) | GCR_ENCODE[b & 0x0f]
        for shift in (32, 24, 16, 8, 0):
            out.append((bits >> shift) & 0xff)
    return bytes(out)


def build_track(track, image, id1=0x30, id0=0x30):
    data = bytearray()
    for sector in range(SECTORS_PER_TRACK[track - 1]):
        offset = sum(SECTORS_PER_TRACK[:track - 1]) * 256 + sector * 256
        payload = image[offset:offset + 256]
        if len(payload) < 256:
            payload = payload + bytes(256 - len(payload))

        checksum = sector ^ track ^ id1 ^ id0
        header = bytes([0x08, checksum, sector, track, id1, id0, 0x0f, 0x0f])

        block = bytearray([0x07]) + payload
        parity = 0
        for b in payload:
            parity ^= b
        block.append(parity)
        block += b"\x00\x00"        # pads 260 bytes out to a multiple of 4

        data += b"\xff" * 5         # header sync
        data += gcr_encode(header)
        data += b"\x55" * 9         # header gap
        data += b"\xff" * 5         # data sync
        data += gcr_encode(bytes(block))
        data += b"\x55" * 8         # inter-sector gap

    if len(data) > TRACK_LENGTH:
        raise SystemExit(f"track {track} overflows: {len(data)} > {TRACK_LENGTH}")

    # The rest of the window is whatever the nibbler saw after the last sector.
    # $55 is a legal GCR byte and carries no sync, so it cannot be mistaken for
    # one - which is what the padding of a real capture amounts to.
    return bytes(data) + b"\x55" * (TRACK_LENGTH - len(data))


def main():
    argv = sys.argv[1:]
    passes = 1
    if len(argv) >= 2 and argv[0] == "-p":
        passes = int(argv[1])
        argv = argv[2:]
    if len(argv) != 2:
        raise SystemExit(__doc__)

    image = open(argv[0], "rb").read()
    tracks = len(SECTORS_PER_TRACK)

    out = bytearray(b"MNIB-1541-RAW")
    out += bytes([0])                   # version
    out += bytes([0, 0])                # unused

    table = bytearray()
    for track in range(1, tracks + 1):
        table += bytes([track * 2, DENSITY[track - 1]])
    table += bytes(HEADER_SIZE - len(out) - len(table))
    out += table

    assert len(out) == HEADER_SIZE, len(out)

    for track in range(1, tracks + 1):
        window = build_track(track, image)
        # Every pass of a track is the same capture here. A real .nb2 would
        # differ between passes wherever the media is marginal, which is the
        # point of storing them - the reader only ever uses the first.
        for _ in range(passes):
            out += window

    open(argv[1], "wb").write(bytes(out))
    print(f"{argv[1]}: {len(out)} bytes, {tracks} tracks, {passes} pass(es), "
          f"stride {TRACK_LENGTH * passes}")


if __name__ == "__main__":
    main()
