---
name: meatloaf-networking
description: |
  Reference for the Meatloaf full-mode HTTP client protocol — the
  line-oriented command language (`m`, `h`, `b`, `s`, `status`,
  `r-h`, `r-b`, `j`, `c`) that an ESP32-based Meatloaf device
  accepts over the C64's IEC bus when a channel is opened with
  secondary address 2. Consult this skill whenever the user wants
  to make HTTP requests through a Meatloaf device, talk to a remote
  API from C64 firmware, build a custom Meatloaf client in C or
  assembly, write a host-side tool that drives a Meatloaf over its
  network or serial bridge, or is debugging IEC-bus networking on
  a Commodore. The protocol is the same regardless of the client
  language — BASIC, C, and assembly all reduce to the same
  primitives. For the unattended debug cycle (writing BASIC or
  Meatloaf firmware, deploying, capturing serial, iterating) prefer
  the **c64-meatloaf-debug** skill instead.
---

# Meatloaf Networking — Language-Agnostic Reference

Meatloaf is an ESP32 firmware (project: [idolpx/meatloaf](https://github.com/idolpx/meatloaf))
that plugs into a Commodore 64's **IEC serial bus** and emulates one or
more virtual disk drives. It exposes a WiFi TCP/IP stack to the C64 by
acting as a "device" on the bus. From a programmer's point of view you
are not driving a network interface — you are driving **an IEC device
that happens to accept URLs as filenames**.

The interesting part of Meatloaf is its **full-mode HTTP client**:
opening a channel with secondary address `2` puts the device into a
mode where each line you write to the channel is a command
(`m get`, `h content-type: application/json`, `b {…}`, `s`, …) and
each byte you read back is part of the HTTP response. This is what
lets a C64 — or any other client — make arbitrary HTTP requests with
custom headers and bodies, and parse the response.

This document describes the **protocol**, not any one language's API
for it. BASIC, C, assembly, and host-side tooling all reduce to the
same primitives: open a channel, write command lines, read response
bytes, watch the EOI/error flags.

---

## The mental model

Think of Meatloaf as a **tiny HTTP server on the bus** that takes
orders line by line and hands you bytes back. There is no library to
link. There is no driver to install. There is no "Meatloaf API". The
whole interface is six bytes of IEC protocol and a CR-terminated
command language.

```
   Client (your code)                    Meatloaf (ESP32)
   ────────────────                      ────────────────
   OPEN channel, sec=2, name="URL"  ──►  parses URL, opens TCP
   WRITE "m get" + CR/LF              ──►  records method
   WRITE "h accept: */*" + CR/LF      ──►  records header
   WRITE "s" + CR/LF                  ──►  sends request, reads response
   WRITE "status" + CR/LF             ──►  positions read cursor
   READ  bytes until EOI               ◄──  "200\r"
   WRITE "j /path" + CR/LF            ──►  extracts JSON Pointer
   READ  bytes until EOI               ◄──  extracted value (PETSCII)
   CLOSE                              ──►  resets
```

That's the whole thing. The rest of this document fills in the
details.

---

## IEC bus model (the layer below)

The IEC serial bus is the 6-pin cable that connects a C64 to disk
drives, printers, and (in our case) Meatloaf. You don't need to know
the bus in depth to use the protocol, but knowing three things saves
hours of confusion:

- **Device numbers**: IEC supports up to 30 logical devices
  (numbers 4–30 are valid; 8–15 are conventional for disk drives).
  Meatloaf picks its own device number at boot — you must know which
  one it claimed (the C64 stores the last-used device in a variable
  and you can `LOAD"$",8` to list its catalog; or read Meatloaf's
  boot banner over serial).
- **Secondary addresses**: a "channel" is identified by the pair
  `(device, secondary_address)`. Secondary address `0` and `1` are
  the standard LOAD/SAVE channels. Secondary address `2` is the magic
  value that puts Meatloaf into full HTTP mode. (Other secondary
  addresses either open a disk-like file or are unused.)
- **Three things travel on the bus**: ATN (attention), data
  characters, and an end-of-data marker called EOI. EOI is how the
  device tells you "no more bytes" — read it as a single boolean
  flag, not a character.

The exact wire protocol (ATN low, listen/talk bytes, turn-around,
acknowledgement) is handled by your runtime's IEC kernal calls. From
your code you only ever call:

| Operation    | What it does                              | Returns                      |
|--------------|-------------------------------------------|------------------------------|
| `OPEN ch,d,s,name` | Open channel `ch` to device `d`, secondary address `s`, with filename `name`. | Channel ID; status flag. |
| `CLOSE ch`   | Close channel `ch`.                       | —                            |
| `WRITE ch, byte` | Send one byte to the device on channel `ch`. | —                            |
| `READ ch`    | Read one byte from the device on channel `ch`. | Byte, plus EOI / error flags. |

Status flags (consolidated, regardless of platform):

| Flag           | Bit    | Meaning                                                |
|----------------|--------|--------------------------------------------------------|
| `EOI`          | bit 6  | This byte is the last one in the current chunk.        |
| `ERROR`        | bit 7  | The last operation failed (e.g. malformed command, lost connection). |
| `TIMEOUT`      | varies | The device didn't respond in time.                     |

Every language surfaces these differently (BASIC: `ST` with bit
masks 64 and 128; C: a `cbm_errno_t`; assembly: CIA interrupt
status). The *semantics* are the same. From here on, this document
talks about the protocol in those semantic terms — your language
will give you specific constants.

---

## The Meatloaf full-mode command language

Once a channel is open with secondary address `2`, every byte you
write to the channel is part of a line. A line is terminated by any
of `CR` (`0x0D`), `LF` (`0x0A`), or `CRLF` — Meatloaf treats all
three as command terminators. Each complete line is a command.
Commands are case-insensitive at the firmware level; **always use
lowercase** for consistency with existing clients and the BASIC
convention.

| Command         | Argument           | Effect                                                   |
|-----------------|--------------------|----------------------------------------------------------|
| `m <method>`    | `get`/`post`/`put`/`head`/`delete`* | Set the HTTP method. Default is `get`. |
| `h <name>: <value>` | header line    | Set a request header (replaces existing).               |
| `h+ <name>: <value>` | header line   | Append a header (allows duplicates).                    |
| `b <text>`      | arbitrary text     | Set the request body (replaces existing).               |
| `b+ <text>`     | arbitrary text     | Append to the request body.                             |
| `s`             | (none)             | **Send the request.** Buffers the response.              |
| `status`        | (none)             | Position the read cursor on the HTTP status line.       |
| `r-h`           | (none)             | Position the read cursor on the response headers.        |
| `r-b`           | (none)             | Position the read cursor on the response body.          |
| `j <pointer>`   | RFC 6901 path      | Run a JSON Pointer query on the captured body.           |
| `c`             | (none)             | Clear the request context (method, headers, body).       |

\* `delete` is not currently supported by `MeatHttpClient`
(see [Limitations](#limitations)).

### How `s` works

` s ` is the only command that talks to the network. Everything else
is just queueing state on the device. After `s` returns, the full
HTTP response (status line, all headers, all body bytes) is buffered
in Meatloaf and you can read it back through the channel by issuing
`status`, `r-h`, or `r-b` first, then reading until EOI.

There is no streaming. The whole response fits in Meatloaf's RAM
(typically 8 KB+ depending on ESP32 variant) before you can read any
of it. If the server sends more than Meatloaf can buffer, the
request will hang or fail.

### How `j` works

` j <pointer> ` is a convenience for clients that don't want to
parse JSON themselves. It runs an RFC 6901 JSON Pointer
(see [json_pointer.md](json_pointer.md) for the full syntax reference)
(e.g. `/choices/0/message/content`) against the captured response
body and queues just the extracted value (or `null` if the pointer
doesn't match) for read-back, the same way `r-b` does for the full
body.

**`j` is the recommended way to read JSON responses.** Real
clients (e.g. `c64u_radar.c`) prefer `j` over `r-b` because:

- the C64 has no JSON parser in BASIC and a tiny one in cc65;
- `j` skips parsing the surrounding object/array structure;
- each `j` query can be issued independently on the same channel
  without re-sending the request;
- the result is already PETSCII-transcoded for display.

If the response isn't valid JSON, or the pointer doesn't match,
the `j` path returns `-99` and a short error string in the status
view (e.g. `JSON parse error`, `JSON pointer not found`).

### Reading the response

Reading is a three-step dance every time:

1. Issue a positioning command (`status`, `r-h`, or `r-b`).
2. Read bytes one at a time until EOI.
3. The end-of-chunk is signaled by the EOI flag on the last byte,
   not by a special character. (The HTTP body itself may or may not
   have a trailing newline — Meatloaf passes server bytes through
   unchanged.)

The `status` and `r-h` views are line-oriented: each line ends with
`CR` (`0x0D`). Read until you see `CR`, then start the next line.
For `r-h`, a `CR` followed immediately by `EOI` (or by an empty
line followed by EOI) marks the end of the headers section.

`r-b` is byte-oriented: no special end marker inside the body,
just EOI when Meatloaf has handed you the last byte.

### Error and status mapping

| Condition                         | What you see in `status` view |
|-----------------------------------|-------------------------------|
| HTTP 2xx                          | `200`, `201`, …               |
| HTTP 3xx                          | `301`, `304`, …               |
| HTTP 4xx                          | `404`, …                      |
| HTTP 5xx                          | `500`, …                      |
| Connection refused                | `-1`                          |
| DNS resolution failure            | `-2`                          |
| JSON Pointer error (parse fail, no match) | `-99`                  |

The `-1` and `-2` codes come from `HTTPRequestContext::errorToIecStatus`
in Meatloaf's `http.cpp`; the `-99` is a separate code that the
JSON Pointer path returns when parsing or pointer resolution fails.
Any negative `status` value should be treated as "transport- or
parse-level error" and the connection should be closed and retried
by the caller.

### Encoding note (read this — it differs by command)

| Command  | Bytes you read                              |
|----------|---------------------------------------------|
| `status` | ASCII digits (e.g. `200`, `-1`, `404`).     |
| `r-h`    | Raw response headers from the server.       |
| `r-b`    | Raw response body from the server.          |
| `j`      | **PETSCII-converted** JSON value.           |

The `r-h` and `r-b` paths pass server bytes through unchanged
(useful for byte comparisons and JSON parsers). The `j` path
converts the extracted JSON value from ASCII/UTF-8 to PETSCII
before handing it to the client, because that's what the C64
display expects. Don't try to feed `j` output into a JSON parser
on the C64 — it's already transcoded.

For the **write** path (sending headers and bodies): command
characters are decoded from PETSCII to UTF-8 inside the firmware
before being put on the wire, so PETSCII uppercase letters from a
BASIC program end up as ASCII lowercase in the actual HTTP
header. The C64 doesn't need to know this is happening.

---

## Worked examples

### C (cc65)

cc65 exposes the IEC bus through `<cbm.h>` with four functions:
`cbm_open`, `cbm_close`, `cbm_write`, `cbm_read`. The
`secondary_address` argument selects the protocol mode — `2` puts
Meatloaf into full HTTP mode, anything else opens a disk-like file
emulation. The example below is condensed from
`c64u_radar.c` (an ADS-B radar client that talks to `adsb.fi` over
HTTPS) and shows the full GET/JSON Pointer cycle.

```c
#include <stdio.h>
#include <string.h>
#include <cbm.h>

#define CH   2        /* Meatloaf logical channel          */
#define DEV  8        /* Meatloaf IEC device number        */
#define SA   2        /* secondary address: full HTTP mode */

static unsigned char ml_read_val(unsigned char ch, char* buf,
                                 unsigned char maxlen)
{
    /* Read bytes one at a time, stopping at CR/LF or EOI.
     * cbm_read() returns 0 on EOI; on success it returns 1. */
    unsigned char i = 0;
    while (i < maxlen - 1) {
        if (cbm_read(ch, (unsigned char*)(buf + i), 1) == 0) break;
        if (buf[i] == '\r' || buf[i] == '\n') break;
        ++i;
    }
    buf[i] = 0;
    return i > 0;
}

static unsigned char j_str(unsigned char ch, const char* pointer,
                           char* buf, unsigned char maxlen)
{
    cbm_write(ch, "j ", 2);
    cbm_write(ch, pointer, (unsigned char)strlen(pointer));
    cbm_write(ch, "\r\n", 2);
    return ml_read_val(ch, buf, maxlen);
}

void fetch(const char* url) {
    char val[36];
    int http_status;

    /* Open a URL on Meatloaf.  cbm_open does the IEC OPEN for us;
     * no manual secondary-address marshalling. */
    if (cbm_open(CH, DEV, SA, url) != 0) return;

    /* Queue the GET request.  Any of \r, \n, or \r\n terminates a
     * command line — \r\n is conventional and matches what
     * Meatloaf's test suite sends. */
    cbm_write(CH, "h user-agent: my-client/1.0\r\n", 28);
    cbm_write(CH, "m get\r\n", 7);
    cbm_write(CH, "s\r\n", 3);

    /* Read the HTTP status. */
    cbm_write(CH, "status\r\n", 8);
    if (!ml_read_val(CH, val, sizeof val)) goto done;
    http_status = atoi(val);
    if (http_status != 200) goto done;

    /* Pull a field via JSON Pointer.  The result is PETSCII-
     * converted by the firmware, so the bytes in `val` are
     * already screen-ready. */
    if (!j_str(CH, "/ac/0/flight", val, sizeof val)) goto done;
    printf("flight=%s\n", val);

done:
    cbm_close(CH);
}
```

A few things to note from the real code:

- **`cbm_read` returns 0 on EOI, not a flag you have to mask.**
  That makes the read loop straightforward.
- **There's no `c` between requests** in the radar client. The
  pattern is: open channel → do one full request cycle → close.
  Repeated fetches open a new channel each time, which is cheap.
- **The `j` path is preferred over `r-b`** because the C64 has no
  JSON parser and the field-extraction cost of `j` is much lower
  than reading the whole body and scanning for quotes.
- **Body data is PETSCII on the `j` path** but raw on `r-b`. The
  `c64u_radar.c` code only uses `j`, so it never deals with the raw
  case. If you must use `r-b`, allocate buffers for raw bytes and
  don't print them directly to screen without a PETSCII conversion.

### cc65 character translation (PETSCII gotcha)

cc65 runs every string and character literal through an
**ASCII/ISO-8859-1 → PETSCII** translation table when targeting
Commodore machines. This is normally convenient — you write C
source in ASCII and PETSCII-aware screen output "just works" —
but several characters used in networking and JSON get mangled in
ways that produce wrong bytes on the wire or wrong glyphs on
screen.

#### Full translation table

This is the default mapping (ISO-8859-1 input → PETSCII output):

| Input range | Input chars | → | PETSCII | Meaning on C64 |
|---|---|---|---|---|
| `0x08` | BS (backspace) | → | `0x14` | Delete character |
| `0x0A` | LF (line feed) | → | `0x0D` | Carriage return |
| `0x0B` | VT (vertical tab) | → | `0x11` | Cursor down |
| `0x0C` | FF (form feed) | → | `0x93` | Clear screen |
| `0x0D` | CR (carriage return) | → | `0x0A` | Line feed (Enter) |
| `0x11` | DC1 | → | `0x0B` | Cursor up |
| `0x14` | DC4 | → | `0x08` | Delete line |
| `0x40` | `@` | → | `0x40` | (same — no change) |
| `0x41–0x5A` | `A`–`Z` (upper) | → | `0xC1–0xDA` | PETSCII uppercase |
| **`0x5C`** | **`\` (backslash)** | → | **`0xBF`** | **Pound sign £** |
| **`0x5F`** | **`_` (underscore)** | → | **`0xA4`** | **Pipe ┊** |
| `0x60` | `` ` `` (backtick) | → | `0xAD` | Reverse space |
| `0x61–0x7A` | `a`–`z` (lower) | → | `0x41–0x5A` | PETSCII lowercase |
| `0x7B` | `{` | → | `0x7B` | (same) |
| `0x7C` | `\|` | → | `0x7C` | (same) |
| `0x7D` | `}` | → | `0x7D` | (same) |
| `0x7E` | `~` | → | `0x7E` | (same) |
| `0x7F` | DEL | → | `0x7F` | (same) |
| `0xC0` | À | → | `0x60` | Backtick ` |
| `0xC1–0xCA` | Á–Ê | → | `0x61–0x6A` | Lower a–j |
| `0xCB` | Ë | → | `0x6B` | Lower k |
| `0xCC–0xCF` | Ì–Ï | → | `0x6C–0x6F` | Lower l–o |
| `0xD0–0xDA` | Ð–Ú | → | `0x70–0x7A` | Lower p–z |
| `0xDB` | Û | → | `0x7B` | `{` |
| `0xDC` | Ü | → | `0x7C` | `\|` |
| `0xDD` | Ý | → | `0x7D` | `}` |
| `0xDE` | Þ | → | `0x7E` | `~` |
| `0xDF` | ß | → | `0x7F` | DEL |

#### The dangerous ones for networking and JSON

Two characters in the table above are **actively harmful** when
writing Meatloaf clients that build or parse network data:

| Character | In source | → Becomes | Why it breaks |
|---|---|---|---|
| **Backslash `\`** | `0x5C` | `0xBF` (£) | JSON uses `\"`, `\\`, `\n`, `\t`, `\/`, `\uXXXX` — every escape sequence gets a pound sign where the backslash should be. A JSON body like `{"key":"value with \"quotes\""}` becomes garbage. |
| **Underscore `_`** | `0x5F` | `0xA4` (┊) | Appears in snake_case JSON keys, URL path segments (`/my_endpoint`), and hostnames. The server either rejects the request or the JSON parse fails. |

A few others bear mentioning:

- **Lowercase letters** (`a`–`z`, `0x61–0x7A`) → PETSCII `0x41–0x5A`:
  This *round-trips correctly* through Meatloaf's PETSCII→UTF-8
  decode on the write path, so strings like `"content-type"` and
  `"hello"` arrive on the wire correctly. But on the **read path**
  the PETSCII bytes reach your code as-is — if you're doing
  `strcmp` against an ASCII literal the comparison will fail.
- **Backtick** (`0x60` → `0xAD`): uncommon in networking, but
  could corrupt template literals or debug output.

#### Safe characters — JSON structural braces and `@`

**Verification note:** The character mappings below were verified against
the actual `cbm_petscii_charmap.h` (cc65 include) after an earlier version
of this document incorrectly claimed braces and pipe were safe. Always
verify against the header — secondary sources are unreliable.

| Character | ASCII | → Becomes | Safe? |
|---|---|---|---|
| `{` | `0x7B` | **`0xB3`** | ❌ **Breaks JSON object open** |
| `}` | `0x7D` | **`0xAB`** | ❌ **Breaks JSON object close** |
| `\|` | `0x7C` | **`0xDD`** | ❌ Breaks JSON or pipe usage |
| `[` `]` | `0x5B` `0x5D` | `0x5B` `0x5D` (identity) | ✅ Pass through unchanged |
| `@` | `0x40` | `0x40` (identity) | ✅ Pass through unchanged |
| `\` | `0x5C` | **`0xBF` (£)** | ❌ **Breaks all JSON escapes** |
| `_` | `0x5F` | **`0xA4` (┊)** | ❌ Breaks snake_case and URLs |

The full list of identity-mapped printable ASCII (`0x20–0x7E`) is:

**Safe** (pass through unchanged): space, `!"#$%&'()*+,-./`, digits
`0–9`, `:;<=>?`, `@`, `[`, `]`, `^`, `` ` `` (backtick), `~`

**Unsafe** (get translated): `\` → `£`, `_` → `┊`, `{` → `0xB3`,
`}` → `0xAB`, `\|` → `0xDD`, uppercase `A–Z`, lowercase `a–z`
(encoding change — round-trips through Meatloaf's PETSCII→UTF-8 decode).

**Solution:** Always use `#pragma charmap` overrides for `\`, `{`, `}`,
and `_` when building Meatloaf clients in cc65 (see Workaround 1 below).

#### Workaround 1: `#pragma charmap` (preferred)

Add these lines near the top of any source file that builds
network requests or parses JSON responses. They tell cc65 to
leave the problem bytes alone:

```c
/* Don't translate backslash — needed for JSON escapes */
#pragma charmap (0x5C, 0x5C)

/* Don't translate underscore — needed for URLs and JSON keys */
#pragma charmap (0x5F, 0x5F)
```

This is the cleanest fix: the rest of the translation table stays
active (so screen output still works normally), and only the
characters that break networking are suppressed.

You can also restore individual lowercase letters if you're doing
byte-level comparisons against ASCII literals on the C64 side:

```c
/* Keep lowercase 'h' as actual 0x68, don't map to PETSCII 0x48 */
#pragma charmap (0x68, 0x68)
```

#### Workaround 2: raw hex literals in string data

For one-off uses, bypass the translation by embedding the ASCII
value directly:

```c
/* Instead of:  cbm_write(ch, "{\"key\":\"val\"}\r\n", ...);      */
/*      Write:  cbm_write(ch, "{\x22key\x22:\x22val\x22}\r\n", ...); */
```

This gets tedious fast — `\x5C` for every backslash makes the
code unreadable. Use `#pragma charmap` instead.

#### Workaround 3: build strings at runtime

For characters that you only need a few of (e.g. an arrow glyph
for a UI element), build them from raw byte values so they never
pass through the source-level translator:

```c
char left_arrow = 0x5F;   /* ← on the real C64 */
char right_arrow = 0x5E;  /* → */

/* For a string of underscores that shouldn't be translated */
char divider[5];
memset(divider, 0x5F, 4);
divider[4] = 0;
```

#### Lowercase round-trip is correct on the write path

At first glance the table looks alarming: ASCII lowercase `h`
(`0x68`) maps to PETSCII `0x48`. But in PETSCII, `0x41–0x5A` are
the lowercase letters and `0xC1–0xDA` are uppercase — so `0x48`
is PETSCII **lowercase h**. Meatloaf's firmware decodes PETSCII→
UTF-8 before sending to the server, producing ASCII `0x68` again.
The round-trip is correct for all basic ASCII lowercase letters.

**On the read path** (`j` returns raw PETSCII bytes), the situation
is different. If you `strcmp` against an ASCII string literal like
`"hello"`, the literal will be re-translated by cc65's table —
yielding PETSCII `0x48 0x45 0x4C 0x4C 0x4F` — and the comparison
will match what the `j` command returned. So `strcmp` against
literals works fine on both sides of the read path as long as both
sides go through the same translation. The trap is only when you
manually construct a byte value (e.g. `if (buf[i] == 0x68)`) and
expect it to match a translated character.

### 6502 assembly

The C64 kernal exposes three entry points that together cover the
whole protocol:

| KERNAL call | Address | Purpose                          |
|-------------|---------|----------------------------------|
| `OPEN`      | `$FFC0` | Open a logical file.             |
| `CLOSE`     | `$FFC3` | Close a logical file.            |
| `CKOUT`     | `$FFC9` | Set the active output channel.   |
| `CHKIN`     | `$FFC6` | Set the active input channel.    |
| `BSOUT`     | `$FFD2` | Write one byte to the output channel. |
| `BASIN`     | `$FFCF` | Read one byte from the input channel. |
| `CLRCHN`    | `$FFCC` | Clear the active channel.        |
| `READST`    | `$FFB7` | Read the I/O status byte.        |

Calling convention: `A` is the logical channel number on `OPEN`;
`X` is the device number; `Y` is the secondary address. The
filename (the URL) is pointed to by `$BB/$BC` and is a
null-terminated ASCII string.

```asm
; --- Open "http://example.com/data" on device 8, secondary 2 ---
        LDA #<url
        LDY #>url
        STA $BB
        STY $BC
        LDA #2           ; logical channel 2
        LDX #8           ; device number
        LDY #2           ; secondary address: full HTTP mode
        JSR $FFC0        ; OPEN

        ; --- Send "m get" + CR ---
        LDA #<m_cmd
        LDY #>m_cmd
        STA $BB
        STY $BC
m_loop: LDA ($BB),Y
        BEQ m_done
        JSR $FFD2        ; BSOUT
        INY
        BNE m_loop
m_done: LDA #$0D
        JSR $FFD2        ; send CR (terminates the command)

        ; --- Send "s" + CR ---
        ; (same loop, different string)

        ; --- Read response body ---
        JSR $FFC6        ; CHKIN
body:   JSR $FFCF        ; BASIN: returns byte in A
        STA $D020        ; (or stash to a buffer)
        JSR $FFB7        ; READST
        AND #$40         ; EOI bit (bit 6 of the status byte)
        BEQ body         ; loop until EOI

        JSR $FFC3        ; CLOSE
        JSR $FFCC        ; CLRCHN
        RTS

m_cmd:  .byte "m get",0
s_cmd:  .byte "s",0
url:    .byte "http://example.com/data",0
```

A note on `READST`: it returns the *accumulated* status since the
last call, not a per-byte flag. Calling it after every `BASIN` is
the cheap option and is fine for IEC HTTP — the bus is much slower
than the CPU. A faster option is to drive the bus with a CIA-tied
loop on `CIA2.ICR`, but most clients don't need that speed.

---

## Multi-request reuse (`c`)

A single open channel can serve many HTTP requests. Between
requests, send `c` to clear the queued method/headers/body, then
queue the next request. The TCP connection, the channel allocation,
and any TLS session are reused — for HTTPS hosts this can be a
significant speedup.

This is also useful for keep-alive style APIs: the device is in
"talking to host X" state across the whole session, so a
multi-request flow looks like:

```
open url
loop {
    m <method>
    h ... (or several h / h+)
    b ... (or several b / b+)
    s
    read status, headers, body
    c
}
close
```

---

## Differences from stock IEC disk I/O

If you've used `LOAD` and `SAVE` on a 1541, you'll recognize the
vocabulary (OPEN, CLOSE, secondary address, channel) but the
behaviour is different in two important ways:

- **Filenames are URLs.** For a 1541, the filename is a disk file
  path like `0:FILE,S,W`. For Meatloaf, it's a URL like
  `http://example.com/data` or `tcp://host:port`. The kernal does
  no parsing — it just hands the bytes to Meatloaf.
- **Secondary address `2` is meaningful.** A 1541 ignores most
  secondary addresses (or uses them for PRG/SEQ distinctions);
  Meatloaf uses `2` to switch to full HTTP mode. Pick a different
  secondary address and you get the file-emulation mode (loading
  bytes from a URL path on disk) — useful, but not what you want
  for HTTP requests.

---

## Limitations

These are firmware limits, not protocol limits:

- **String size**: BASIC v2 caps at 255 chars; the firmware itself
  accepts much larger bodies and headers, limited only by ESP32
  RAM. For responses larger than the client's read buffer, read in
  chunks across multiple `r-b` reads — the firmware will keep
  handing you bytes until the buffered response is exhausted.
- **`delete` method**: not currently implemented in
  `MeatHttpClient` (the HTTP class used by full mode).
- **HTTPS**: works only if the Meatloaf firmware is built with TLS
  and a CA bundle. Out-of-the-box builds vary.
- **Timeout**: 10 s default for the full request cycle
  (connect + send + receive). Configurable in firmware; not
  configurable from the client.
- **One in-flight request per channel**: don't open the same
  channel twice in parallel; use separate channels for parallel
  requests.

---

## Cross-references

- For BASIC v2 idioms (`OPEN`, `PRINT#`, `GET#`, `ST` bit masks,
  PETSCII quirks, `chr$(34)` JSON helpers, and building JSON
  request bodies) see the **c64-basic** skill's
  [Network and API access](../commodore64-basic/SKILL.md#network-and-api-access)
  section and the
  [Building JSON bodies](../commodore64-basic/SKILL.md#building-json-bodies)
  subsection. The two documents describe the same protocol from
  different angles; consult whichever matches your client language.
- For the unattended debug cycle (writing BASIC or Meatloaf
  firmware, deploying, capturing serial, iterating) see the
  **c64-meatloaf-debug** skill.
- For JSON Pointer (RFC 6901) syntax - the `/path` argument to the
  `j` command, including examples, serialized types, escaping, and
  error handling - see [json_pointer.md](json_pointer.md).
- For the Meatloaf firmware itself (source, build instructions,
  device ID conventions) see the
  [idolpx/meatloaf](https://github.com/idolpx/meatloaf) repo on
  GitHub.
