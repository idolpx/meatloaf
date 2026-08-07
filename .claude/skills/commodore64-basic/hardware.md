# Hardware PEEK/POKE Reference

Commodore 64 hardware is controlled by writing to specific memory locations. As new needs arise, add entries to the appropriate section below. Each entry documents: address, purpose, value semantics, and a short example.

---

## Screen & Border Color

| Address | Purpose | Example |
|---------|---------|---------|
| 53280 | Border color | `poke 53280, 0` = black, `1` = white, `2` = red |
| 53281 | Screen background color | same palette as border |
| 646 | Current cursor color | `poke 646, 1` = white |
| 53282 | Extended color 1 | |
| 53283 | Extended color 2 | |
| 53284 | Extended color 3 | |
| 53285 | Extended color 4 | |

Color palette (0–15): `0`=black, `1`=white, `2`=red, `3`=cyan, `4`=purple, `5`=green, `6`=blue, `7`=yellow, `8`=orange, `9`=brown, `10`=light red, `11`=grey1, `12`=grey2, `13`=light green, `14`=light blue, `15`=grey3

---

## Character / Screen Mode

| Address | Purpose | Values | Example |
|---------|---------|--------|---------|
| 53272 | Screen bitmap base (bits 0–2) | 1–3: text, 4–6: bitmap H, 7: ECM | `poke 53272, 21` = default |
| 53273 | Screen bitmap base (bits 3–7 of 16-bit addr) | | `poke 53273, 0` |
| 56578 | VIC II control register A | bit 7: raster MSB, bits 0–2: video bank | |
| 56577 | VIC II control register B | bits 0–2: chars H, 3: screen on, 4: bitmap on, 5: ECM | |
| 56576 | Sprite enable | bit 0–7: sprites 0–7 on/off | `poke 56576, 0` = all off |

### Character Set Selection

Switch between the two built-in character ROM sets. Bit 2 of address 53272 controls the selection — use read-modify-write since it's part of the full VIC II register:

- **Standard** (uppercase + PETSCII graphics): `poke 53272, peek(53272) and 251`
- **Shifted** (lowercase + uppercase): `poke 53272, peek(53272) or 4`

---

 / Joystick Input

| Address | Purpose | Read/Write | Example |
|---------|---------|------------|---------|
| 197 | Keyboard matrix (current key) | read | `get k$ : if k$<>"" then p=peek(197)` |
| 650 | Keyboard repeat rate / delay | write | `poke 650, 1` = no repeat |
| 198 | Number of characters in keyboard buffer | read | `peek(198)` gives queue length |

---

## Sound (SID chip)

| Address | Purpose | Notes |
|---------|---------|-------|
| 54272–54296 | SID registers | Voice 1: 54272–54287, Voice 2: 54288–54303, Voice 3: 54304–54319 |
| 54296 | Master volume + filter mode | `poke 54296, 15` = max volume |

---

## Sprite / Graphics

| Address | Purpose | Values | Example |
|---------|---------|--------|---------|
| 53287 | Sprite 0 color | | `poke 53287, 1` |
| 53288 | Sprite 1 color | | |
| 53289 | Sprite 2 color | | |
| 53290 | Sprite 3 color | | |
| 53291 | Sprite 4 color | | |
| 53292 | Sprite 5 color | | |
| 53293 | Sprite 6 color | | |
| 53294 | Sprite 7 color | | |
| 53269 | Sprite X MSB (bit per sprite) | bit N = sprite N X MSB | `poke 53269, 0` |
| 53257 | Sprite Y MSB | bit N = sprite N Y MSB | `poke 53257, 0` |
| 53248 | Sprite 0 X (low byte) | 0–255 | `poke 53248, 100` |
| 53249 | Sprite 0 Y | | |
| 53264 | Interrupt flags | bit 7: raster IRQ | |

---

## Adding New Entries

When you discover a new hardware register:

1. Pick the right section above
2. Add a row: `| address | purpose | values | example |`
3. Keep the address in decimal (BASIC v2 uses decimal by default)
4. Add a brief note on the value semantics — what 0 means, what -1 means, which bits do what
5. If a section doesn't exist yet, add it at the appropriate place

For VIC II registers (graphics), SID registers (sound), or CIA registers (timing/I/O), check [project64.c64.org](http://project64.c64.org) for the full reference.
