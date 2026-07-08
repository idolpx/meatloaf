# Full-Mode HTTP Client — Feature Reference

A guide to using Meatloaf's full-mode HTTP client from Commodore 64 BASIC v2.

## Overview

The full-mode HTTP client lets you make **arbitrary HTTP requests** (GET, POST, PUT) with custom headers and body, and read the response status, headers, and body separately — all from C64 BASIC.

To use it, `OPEN` an HTTP URL with secondary address `2` (read-write mode). This triggers full-mode. Then:

1. Build the request with `PRINT#` commands
2. Send it with `PRINT#1,"s"`
3. Read status, headers, and body with `GET#`

---

## Quick Start — Basic GET

```basic
10 open 1,8,2,"http://example.com/data.json"
20 print#1,"m get"
30 print#1,"s"
40 print#1,"status"
50 get#1,a$:b$=b$+a$:if st=0 then 50
55 print "status: ";b$
60 close 1
```

This fetches `http://example.com/data.json` and prints the HTTP status code (e.g. `200`).

---

## Command Reference

All commands are sent via `PRINT#` to the opened channel. They are **case-insensitive** and dispatched when a `CR` (CHR$(13), the PRINT# line terminator) is received.

### `m <method>`
Set the HTTP method.

```basic
print#1,"m get"    : rem GET request (default)
print#1,"m post"   : rem POST request
print#1,"m put"    : rem PUT request
print#1,"m head"   : rem HEAD request
```

### `h <name>: <value>`
Set a request header (replaces any existing value for the same header name).

```basic
print#1,"h content-type: application/json"
print#1,"h authorization: bearer mytoken123"
```

### `h+ <name>: <value>`
Append a request header (allows multiple values for the same header name).

```basic
print#1,"h+ x-custom: first"
print#1,"h+ x-custom: second"
```

### `b <text>`
Set the request body (replaces any existing body).

```basic
print#1,"b ";chr$(123);chr$(34);"key";chr$(34);":";chr$(34);"value";chr$(34);chr$(125)
```

Note: `chr$(34)` produces `"`, `chr$(123)` produces `{`, `chr$(125)` produces `}`. These function calls must be joined with `;` in BASIC v2.

### `b+ <text>`
Append to the request body.

```basic
print#1,"b ";chr$(123);chr$(34);"key";chr$(34);":";chr$(34);"value";chr$(34)
print#1,"b+ ";chr$(44);chr$(34);"nested";chr$(34);":";chr$(123);chr$(34);"a";chr$(34);":1";chr$(125);chr$(125)
```

Produces body: `{"key":"value","nested":{"a":1}}`

### `s`
Send the HTTP request. This executes the actual HTTP call (TCP connect, TLS handshake if HTTPS, body send, response receive). After `s`, the response is buffered and ready to read via `status`/`r-h`/`r-b`.

```basic
print#1,"s"
```

### `status`
Request the HTTP status code. The next `GET#` reads the decimal status (e.g. `200`, `404`, `500`) followed by `CR`.

```basic
print#1,"status"
get#1,a$:rem reads one digit at a time until CR
```

### `r-h`
Switch response reading to headers mode. The next `GET#` calls read response headers, one line at a time. Each header line ends with `CR`. An empty `CR`-only line marks the end of headers.

```basic
print#1,"r-h"
```

### `r-b`
Switch response reading to body mode. The next `GET#` calls read the response body bytes.

```basic
print#1,"r-b"
```

### `c`
Clear the request context (method, headers, body). Use this between requests on the same open channel to start fresh.

```basic
print#1,"c"
```

---

## Reading the Response

The response is read sequentially using `GET#`. After `s`, all response data is buffered. Commands like `status`, `r-h`, and `r-b` control where in the buffer the next read starts from.

### Status Line
```basic
40 print#1,"status"
50 get#1,a$:if st and 64 then 70 : rem EOI
55 if a$=chr$(13) then print:goto 80 : rem CR = end of status
60 print chr$(asc(a$)); : rem print digit
70 goto 50
80 rem status code complete
```

### Headers
```basic
100 print#1,"r-h"
110 get#1,a$:if st and 64 then 130 : rem EOI
120 if a$=chr$(13) then print:goto 140 : rem CR = end of one header
125 print chr$(asc(a$)); : rem print header character
130 goto 110
140 rem end of headers (empty line)
```

### Body
```basic
200 print#1,"r-b"
210 get#1,a$:if st and 64 then 240 : rem EOI = body done
215 if st and 128 then print:print"error" : end
220 print chr$(asc(a$)); : rem print body character
230 goto 210
240 rem body complete
```

**⚠️ Case when reading body bytes:** `asc(a$)` returns the **raw byte** straight
from the HTTP server — no PETSCII conversion happens on the IEC read path. If
the server sends lowercase JSON (`"content":"hello"`), you get byte 99 for `c`,
not 67. PETSCII case-flip only applies when you **print** characters (`PRINT
chr$(asc(a$))` displays them uppercase). For state machines or byte comparisons:

```basic
if a$=chr$(99) then ... : rem matches lowercase 'c' — correct for most JSON APIs
if a$=chr$(67) then ... : rem matches uppercase 'C' — only if server sends uppercase
```

When in doubt, check server output. Most HTTP APIs (Ollama, OpenAI, REST) return
lowercase JSON — use `chr$(99)`, `chr$(111)`, `chr$(110)` etc.
```

---

## Complete Example — POST with JSON

```basic
10 rem == POST JSON data ==
20 open 1,8,2,"http://httpbin.org/post"
30 print#1,"m post"
40 print#1,"h content-type: application/json"
50 print#1,"h accept: application/json"
60 print#1,"b ";chr$(123);chr$(34);"name";chr$(34);":";chr$(34);"meatloaf";chr$(34);",";chr$(34);"version";chr$(34);":1";chr$(125)
70 print#1,"s"
75 rem == read status ==
80 print#1,"status"
85 b$="":get#1,a$:b$=b$+a$:if st=0 then 85
90 print "status: ";b$
95 rem == read headers ==
100 print#1,"r-h"
105 h$=""
110 get#1,a$:if st and 64 then 130
115 if a$=chr$(13) then print"header: ";h$:h$="":goto 110
120 h$=h$+a$:goto 110
130 print "end of headers"
135 rem == read body ==
140 print#1,"r-b"
145 b$=""
150 get#1,a$:if st and 64 then 170
155 if st and 128 then print"error":end
160 b$=b$+a$:if len(b$)<250 then 150
170 print "body (";len(b$);" bytes)"
175 print left$(b$,200)
180 close 1
```

---

## Complete Example — PUT with body

```basic
10 open 1,8,2,"http://example.com/api/resource"
20 print#1,"m put"
30 print#1,"h content-type: text/plain"
40 print#1,"b this is updated data"
50 print#1,"s"
60 print#1,"status"
70 b$="":get#1,a$:b$=b$+a$:if st=0 then 70
80 print "status: ";b$
90 print#1,"r-b"
100 b$="":get#1,a$:b$=b$+a$:if st=0 then 100
110 print "body: ";left$(b$,200)
120 close 1
```

---

## Multi-Request Cycle

You can make multiple requests on the same open channel by using `c` to clear context between them.

```basic
10 open 1,8,2,"http://example.com/api"
20 rem === first request (POST) ===
30 print#1,"m post"
40 print#1,"b first payload"
50 print#1,"s"
60 print#1,"status"
70 gosub 1000 : rem read status
80 print#1,"r-b"
90 gosub 2000 : rem read body
95 rem === second request (PUT) ===
100 print#1,"c"
110 print#1,"m put"
120 print#1,"b second payload"
130 print#1,"s"
140 print#1,"status"
150 gosub 1000
160 print#1,"r-b"
170 gosub 2000
180 close 1

990 rem helper: read status
1000 b$="":get#1,a$:b$=b$+a$:if st=0 then 1000
1010 print "status: ";b$:return

1990 rem helper: read body (max 250)
2000 b$="":bc=0
2010 get#1,a$:if st and 64 then return
2020 if bc>249 then return
2030 bc=bc+1:b$=b$+a$:goto 2010
```

---

## Error Handling

Errors set the BASIC `ST` variable:

| Bit | Value | Meaning |
|-----|-------|---------|
| 6 | 64 | EOI (end of data) — normal end |
| 7 | 128 | Error occurred |

Check after every `GET#`:

```basic
50 get#1,a$
60 if st and 128 then print"error":end : rem error
70 if st and 64 then print"end of data":end : rem EOI
```

### Status code error map

| HTTP Response | `status` shows | Meaning |
|---------------|----------------|---------|
| 200-299 | e.g. `200` | Success |
| Connection refused | `-1` | Host unreachable |
| DNS failure | `-2` | Host not found |
| 4xx | e.g. `404` | Client error |
| 5xx | e.g. `500` | Server error |

---

## Complete Example — 404 Handling

```basic
10 open 1,8,2,"http://example.com/nonexistent"
20 print#1,"m get"
30 print#1,"s"
40 print#1,"status"
50 b$="":get#1,a$:b$=b$+a$:if st=0 then 50
55 status=val(b$)
60 if status=404 then print"not found!"
70 if status>=400 then print"error ";status:close 1:end
80 rem read body for more details
90 print#1,"r-b"
100 b$="":get#1,a$:b$=b$+a$:if st=0 then 100
110 print"body: ";left$(b$,200)
120 close 1
```

---

## Complete Example — Response Header Mode

```basic
10 open 1,8,2,"http://example.com/echo"
20 print#1,"m get"
30 print#1,"s"
40 print#1,"status"
50 b$="":get#1,a$:b$=b$+a$:if st=0 then 50
55 print "status: ";b$
60 print#1,"r-h"
65 rem read headers one per line
70 get#1,a$:if st and 64 then 90
72 if a$=chr$(13) then print:goto 70 : rem next header
75 print chr$(asc(a$)); : rem print char
80 goto 70
90 print "---end of headers---"
95 print#1,"r-b"
100 get#1,a$:print chr$(asc(a$));:if st=0 then 100
110 close 1
```

---

## Helper Subroutines

Include these in your programs for reusable response reading:

```basic
1000 rem --- read status line into st$ ---
1005 print#ch,"status"
1010 st$=""
1015 get#ch,a$
1020 if st and 64 then return : rem eoi
1025 if st and 128 then return : rem error
1030 if a$=chr$(13) then return : rem cr
1035 st$=st$+a$
1040 goto 1015

2000 rem --- read one header line into r$ ---
2005 print#ch,"r-h"
2010 r$=""
2015 get#ch,a$
2020 if st and 64 then return : rem eoi
2025 if st and 128 then return : rem error
2030 if a$=chr$(13) then return : rem cr = end of this header
2035 r$=r$+a$
2040 goto 2015

3000 rem --- read body into b$ (max 250 bytes) ---
3005 print#ch,"r-b"
3010 b$="":bc=0
3015 get#ch,a$
3020 if st and 64 then return : rem eoi
3025 if st and 128 then return : rem error
3030 if bc>249 then return : rem string too long guard
3035 bc=bc+1:b$=b$+a$
3040 goto 3015
```

Usage with channel 1:

```basic
10 ch=1:open ch,8,2,"http://example.com/data"
20 print#ch,"m get"
30 print#ch,"s":gosub 1000:print"status: ";st$
40 gosub 3000:print"body: ";left$(b$,100)
50 close ch
```

---

## String Search Helper

BASIC v2 has no `INSTR$()` function. Use this to search for substrings:

```basic
900 rem --- search hs$ for nd$, returns in/not-in  ---
905 if len(hs$)=0 or len(nd$)=0 or len(nd$)>len(hs$) then in=0:return
910 for i=1 to len(hs$)-len(nd$)+1
915 if mid$(hs$,i,len(nd$))=nd$ then in=1:return
920 next i
925 in=0:return
```

Usage:

```basic
hs$="hello world":nd$="world":gosub 900
if in then print"found!" : rem prints "found!"
```

---

## String Length Guard

BASIC v2 strings are limited to 255 characters. When reading body data, guard against overflow:

```basic
1000 rem safe body read into b$ (max 250 chars)
1005 b$="":bc=0
1010 get#ch,a$
1015 if st and 64 then return : rem eoi
1020 if bc>249 then return : rem string too long guard
1025 bc=bc+1:b$=b$+a$
1030 goto 1010
```

---

## Test Matrix

The standard test suite (`test/http/http_full_client_test.bas`) exercises all features:

| Test | Feature | What it verifies |
|------|---------|-----------------|
| 1 | Basic GET | Open echo URL, set method, send, read status + headers + body |
| 2 | POST body | Send JSON body, verify echoed response contains "POST" reference |
| 3 | Headers | Set and append custom headers (`h` + `h+`), verify in echoed response |
| 4 | Status code | Read status as decimal text, confirm 200 |
| 5 | Response headers | Use `r-h` to read one header per GET# call |
| 6 | Multi-request | POST then `c` + PUT on same channel, cycle works |
| 7 | 404 handling | Request nonexistent path, confirm 404 status |
| 8 | PUT method | Send PUT with body, verify method echoed |
| 9 | Body append | Use `b+` to build multi-part body |

---

## Limitations

- **String size**: BASIC v2 strings max out at 255 characters. For larger responses, read in chunks.
- **DELETE method**: Not currently supported by `MeatHttpClient`. Sending `m delete` will fail at dispatch.
- **HTTPS**: Works if Meatloaf firmware has TLS configured (cert bundle attached).
- **Multiple channels**: Each channel supports its own independent request context.
- **Timeout**: HTTP operations have a 10-second default timeout configured in `MeatHttpClient`.
