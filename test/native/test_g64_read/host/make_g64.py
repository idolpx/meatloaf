#!/usr/bin/env python3
"""Encode a .d64 into a .g64, or a .d71 into a .g71, so the GCR read paths have
something to test against.

There is no .g64 in .data/media and no way for the firmware to produce one - it has
a GCR decoder, not an encoder - so the fixture is generated here. Layout per the
G64 spec: an 8-byte signature, version, track count and track size, then a table
of 32-bit track offsets and one of speed zones, then each track as a 16-bit
length followed by that many GCR bytes.

Each sector is written the way a 1541 writes one: sync, header block, gap, sync,
data block, gap.

    python3 make_g64.py input.d64 output.g64
    python3 make_g64.py input.d71 output.g71

The .g71 differs only in its signature and in having 70 tracks rather than 35 -
a 1571 in double-sided mode is two 1541 surfaces written by the same logic, and
the half track of track N is N * 2 on both sides with no per-side base.
"""
import struct
import sys

SECTORS_PER_TRACK = ([21] * 17 + [19] * 7 + [18] * 6 + [17] * 5) * 2
SPEED_ZONE        = ([3] * 17 + [2] * 7 + [1] * 6 + [0] * 5) * 2
TRACK_SIZE        = 7928

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


def build_track(track, sectors, image, id1=0x30, id0=0x30):
    data = bytearray()
    for sector in range(sectors):
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
        block += b"\x00\x00"        # pads the 260 bytes out to a multiple of 4

        data += b"\xff" * 5         # header sync
        data += gcr_encode(header)
        data += b"\x55" * 9         # header gap
        data += b"\xff" * 5         # data sync
        data += gcr_encode(bytes(block))
        data += b"\x55" * 8         # inter-sector gap

    if len(data) > TRACK_SIZE:
        raise SystemExit(f"track {track} overflows: {len(data)} > {TRACK_SIZE}")
    return bytes(data)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)

    image = open(sys.argv[1], "rb").read()

    two_sided = sys.argv[2].lower().endswith(".g71")
    tracks_total = 70 if two_sided else 35
    half_tracks = tracks_total * 2 + 14      # 84 for a 1541, 154 for a 1571

    out = bytearray(b"GCR-1571" if two_sided else b"GCR-1541")
    out += bytes([0])                                # version
    out += bytes([half_tracks])                      # half track count
    out += struct.pack("<H", TRACK_SIZE)

    offsets = bytearray()
    speeds = bytearray()
    tracks = bytearray()

    # The tables are one entry per half track each; data starts after both.
    base = len(out) + (half_tracks * 4) + (half_tracks * 4)

    for half in range(half_tracks):
        if half % 2 or (half // 2) >= tracks_total:
            offsets += struct.pack("<I", 0)          # no half tracks
            speeds += struct.pack("<I", 0)
            continue

        track = (half // 2) + 1
        encoded = build_track(track, SECTORS_PER_TRACK[track - 1], image)

        offsets += struct.pack("<I", base + len(tracks))
        speeds += struct.pack("<I", SPEED_ZONE[track - 1])

        tracks += struct.pack("<H", len(encoded))
        tracks += encoded
        tracks += b"\x00" * (TRACK_SIZE - len(encoded))

    out += offsets + speeds + tracks
    open(sys.argv[2], "wb").write(out)
    print(f"{sys.argv[2]}: {len(out)} bytes")


if __name__ == "__main__":
    main()
