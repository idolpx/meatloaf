# Meatloaf Networking — C64 HTTP Client Guide

Meatloaf is an ESP32 firmware that plugs into a Commodore 64's **IEC serial bus**
and emulates one or more virtual disk drives. From a programmer's point of view
you are not driving a network interface — you are driving **an IEC device that
happens to accept URLs as filenames**.

The meat of it is the **full-mode HTTP client**: opening a channel with
secondary address `2` switches the device into a mode where each line you
write is a command (`m get`, `h content-type: application/json`, `b {…}`,
`s`, …) and each byte you read back is part of the HTTP response. This lets
a C64 — or any other IEC client — make arbitrary HTTP requests with custom
headers and bodies, and parse the response.

This guide describes the **protocol**, not any one language's API for it.
BASIC, C (cc65), 6502 assembly, and host-side tooling all reduce to the
same primitives.

---

## How it works — the mental model

Think of Meatloaf as a **tiny HTTP server on the IEC bus** that takes
orders line by line and hands you bytes back.

```
Client (your code)                      Meatloaf (ESP32)
────────────────                        ────────────────
OPEN channel, sec=2, name="URL"   ──►  parses URL, opens TCP
WRITE "m get" + CR/LF              ──►  records method
WRITE "h accept: */*" + CR/LF      ──►  records header
WRITE "s" + CR/LF                  ──►  sends request, reads response
WRITE "status" + CR/LF             ──►  positions read cursor
READ  bytes until EOI               ◄──  "200\r"
WRITE "j /path" + CR/LF            ──►  extracts JSON Pointer value
READ  bytes until EOI               ◄──  extracted value (PETSCII-converted)
CLOSE                               ──►  resets
```

That's the whole interface. No driver to install, no library to link.

---

## IEC bus model — the layer below

You don't need deep IEC knowledge, but three things save hours of confusion:

- **Device numbers**: IEC supports up to 30 logical devices (4–30; 8–15 are
  conventional for disk drives). Meatloaf picks its own device number at boot.
- **Secondary addresses**: a "channel" is `(device, secondary_address)`.
  Address `0` = LOAD/SAVE, `2` = full HTTP mode.
- **EOI** (end-of-identification): the bus-level marker that tells you
  "no more bytes" — read it as a boolean flag, not a character.

| Operation                  | What it does                                        |
|----------------------------|-----------------------------------------------------|
| OPEN ch, d, s, name        | Open channel `ch` to device `d`, sec.addr `s`       |
| CLOSE ch                   | Close channel                                       |
| WRITE ch, byte             | Send one byte                                       |
| READ ch                    | Read one byte (returns byte + EOI/error flags)     |

Status flags:

| Flag    | Bit  | Meaning                                          |
|---------|------|--------------------------------------------------|
| EOI     | 6    | This byte is the last one in the current chunk.  |
| ERROR   | 7    | Operation failed (malformed command, lost conn). |

BASIC surfaces these as the `ST` variable (bit mask 64 for EOI, 128 for error);
cc65's `<cbm.h>` returns 0 on EOI; assembly checks `READST` (`$FFB7`).

---

## The full-mode command language

Once a channel is open with secondary address `2`, every byte you write is
part of a CR/LF-terminated command line. Commands are case-insensitive;
**use lowercase** for consistency with existing clients.

| Command       | Argument          | Effect                                                      |
|---------------|-------------------|-------------------------------------------------------------|
| `m <method>`  | get/post/put/head | Set the HTTP method (default `get`).                        |
| `h <name>:<val>` | header line    | Set a request header (replaces existing).                   |
| `h+ <name>:<val>` | header line  | Append a header (allows duplicates).                        |
| `b <text>`    | arbitrary text    | Set the request body (replaces existing).                   |
| `b+ <text>`   | arbitrary text    | Append to the request body.                                 |
| `s`           | (none)            | **Send the request.** Buffers the full response.            |
| `status`      | (none)            | Position read cursor on the HTTP status line.               |
| `r-h`         | (none)            | Position read cursor on the response headers.               |
| `r-b`         | (none)            | Position read cursor on the response body.                  |
| `j <pointer>` | RFC 6901 path    | JSON Pointer query on the captured body.                    |
| `c`           | (none)            | Clear request context (method, headers, body). Reuse channel for next request. |

### How `s` works

`s` is the only command that touches the network. Everything else queues
state locally. After `s`, the full HTTP response (status + headers + body)
is buffered in Meatloaf's RAM and you read it back by positioning the
read cursor (`status`, `r-h`, `r-b`) and reading until EOI.

There is no streaming. The whole response must fit in RAM
(typically 8 KB+) before you can read any of it.

### How `j` works

`j <pointer>` runs an RFC 6901 JSON Pointer (e.g. `/choices/0/message/content`)
against the captured body and queues just the extracted value for read-back.
This is the **recommended** way to read JSON responses because:

- the C64 has no built-in JSON parser;
- `j` skips parsing the surrounding object/array structure;
- each `j` query is independent — no need to re-send the request;
- results are already PETSCII-transcoded for the C64 display.

If the response isn't valid JSON or the pointer doesn't match, `j` returns `-99`
in the status view.

### Reading the response

The pattern is always the same:

1. Issue a positioning command (`status`, `r-h`, or `r-b`).
2. Read bytes one at a time until EOI.
3. The end is **EOI**, not a special character.

`status` and `r-h` are line-oriented (each line ends with `CR` `0x0D`).
`r-b` is byte-oriented. `j` returns a single value (PETSCII-converted).

### Error codes

| Condition                         | Status value |
|-----------------------------------|--------------|
| HTTP 2xx                          | `200`, …     |
| HTTP 4xx                          | `404`, …     |
| HTTP 5xx                          | `500`, …     |
| Connection refused                | `-1`         |
| DNS failure                       | `-2`         |
| JSON Pointer error                | `-99`        |

Any negative status means "transport or parse error" — close and retry.

### Encoding note

| Path     | Bytes you read                    |
|----------|-----------------------------------|
| `status` | ASCII digits (`200`, `-1`, …)     |
| `r-h`    | Raw server headers (unchanged)    |
| `r-b`    | Raw server body (unchanged)       |
| `j`      | **PETSCII-converted** JSON value  |

`r-h` and `r-b` pass server bytes through unchanged. `j` converts the
extracted value to PETSCII for C64 display.

On the **write path** (sending headers and bodies): command bytes are
decoded from PETSCII to UTF-8 by the firmware before going on the wire.
PETSCII uppercase from BASIC ends up as ASCII lowercase in the HTTP
request — the C64 doesn't need to know this is happening.

---

## Multi-request reuse (`c`)

A single open channel can serve many HTTP requests. Between requests,
send `c` to clear the queued method/headers/body, then queue the next
one. The TCP connection, channel allocation, and any TLS session are
reused — a significant speedup for HTTPS hosts.

```
OPEN url
LOOP:
    m <method>
    h <headers>
    b <body>
    s
    read status, headers, body (via j / r-b)
    c
CLOSE
```

---

## Worked examples

### C (cc65)

cc65 exposes the IEC bus through `<cbm.h>`: `cbm_open`, `cbm_close`,
`cbm_write`, `cbm_read`. Secondary address `2` selects full HTTP mode.

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

    if (cbm_open(CH, DEV, SA, url) != 0) return;

    cbm_write(CH, "h user-agent: my-client/1.0\r\n", 28);
    cbm_write(CH, "m get\r\n", 7);
    cbm_write(CH, "s\r\n", 3);

    cbm_write(CH, "status\r\n", 8);
    if (!ml_read_val(CH, val, sizeof val)) goto done;
    http_status = atoi(val);
    if (http_status != 200) goto done;

    if (!j_str(CH, "/ac/0/flight", val, sizeof val)) goto done;
    printf("flight=%s\n", val);

done:
    cbm_close(CH);
}
```

For multi-request flows, add `c` between requests:

```c
cbm_write(CH, "c\r\n", 3);
```

### 6502 Assembly

The C64 kernal exposes:

| Call    | Address | Purpose                |
|---------|---------|------------------------|
| OPEN    | `$FFC0` | Open a logical file    |
| CLOSE   | `$FFC3` | Close a logical file   |
| CHKIN   | `$FFC6` | Set input channel      |
| CKOUT   | `$FFC9` | Set output channel     |
| BSOUT   | `$FFD2` | Write one byte         |
| BASIN   | `$FFCF` | Read one byte          |
| READST  | `$FFB7` | Read I/O status byte   |
| CLRCHN  | `$FFCC` | Clear active channel   |

```asm
; --- Open URL on dev 8, sec.addr 2 ---
        LDA #<url
        LDY #>url
        STA $BB
        STY $BC
        LDA #2           ; logical channel 2
        LDX #8           ; device number
        LDY #2           ; secondary address: full HTTP mode
        JSR $FFC0        ; OPEN

; --- Write command line ---
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
        JSR $FFD2        ; CR terminates command

; --- Read response ---
        JSR $FFC6        ; CHKIN
body:   JSR $FFCF        ; BASIN → A
        STA $D020        ; or buffer
        JSR $FFB7        ; READST
        AND #$40         ; EOI bit
        BEQ body

        JSR $FFC3        ; CLOSE
        JSR $FFCC        ; CLRCHN
        RTS

m_cmd:  .byte "m get",0
s_cmd:  .byte "s",0
url:    .byte "http://example.com/data",0
```

`READST` returns the accumulated status since the last call. Calling it
after every `BASIN` is fine — IEC is far slower than the CPU.

---

## PETSCII pitfalls for cc65 (C) clients

cc65 translates ASCII/ISO-8859-1 characters to PETSCII at compile time.
Most of the time this is helpful, but **several characters used in
networking get mangled**.

### The dangerous characters

| Char | ASCII | Becomes     | Why it breaks                               |
|------|-------|-------------|---------------------------------------------|
| `\`  | `0x5C` | `0xBF` (£) | Breaks every JSON escape: `\"`, `\\`, `\n`  |
| `_`  | `0x5F` | `0xA4` (┊) | Breaks snake_case keys, URL path segments    |
| `{`  | `0x7B` | `0xB3`      | Breaks JSON object open                     |
| `}`  | `0x7D` | `0xAB`      | Breaks JSON object close                    |
| `\|` | `0x7C` | `0xDD`      | Breaks pipe usage                           |

### The fix: `#pragma charmap`

Add these near the top of any source file that builds network requests
or parses JSON:

```c
/* Don't translate backslash — needed for JSON escapes */
#pragma charmap (0x5C, 0x5C)

/* Don't translate underscore — needed for URLs and JSON keys */
#pragma charmap (0x5F, 0x5F)
```

You can also restore individual characters. The rest of the translation
table stays active (screen output still works normally).

### Safe characters (pass through unchanged)

Space, `!"#$%&'()*+,-./`, digits `0–9`, `:;<=>?`, `@`, `[`, `]`, `^`,
`` ` `` (backtick), `~`

---

## Differences from stock IEC disk I/O

- **Filenames are URLs**, not disk file paths. The kernal just hands the
  bytes to Meatloaf.
- **Secondary address `2`** switches to full HTTP mode. Without it you get
  file-emulation mode (loading bytes from a URL path on disk).

---

## Limitations

| Limit         | Detail |
|---------------|--------|
| BASIC strings | 255 chars max. Firmware itself accepts larger bodies (RAM-limited) |
| `delete`      | Not implemented in `MeatHttpClient` |
| HTTPS         | Works only if built with TLS + CA bundle |
| Timeout       | 10 s default for full request cycle (not client-configurable) |
| Concurrency   | One in-flight request per channel. Use separate channels for parallel requests. |

---

## See also

- [howisitpossible.md](howisitpossible.md) — the story behind Meatloaf
- [filesystems.md](filesystems.md) — how data streams and directories work
- [http-full-client-features.md](http-full-client-features.md) — HTTP client reference
- [connection-reuse-problem.md](connection-reuse-problem.md) — TCP socket management
