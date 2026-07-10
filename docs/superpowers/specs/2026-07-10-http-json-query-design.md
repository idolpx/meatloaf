# JSON Query Command for Full-Mode HTTP Client

## Status

Approved during brainstorming session (2026-07-10).

## Overview

Add a `j` command to Meatloaf's full-mode HTTP client that lets C64 BASIC programs extract a single value from a JSON response body using an RFC 6901 JSON Pointer query. This eliminates the need for state-machine JSON parsing in BASIC v2.

## Background

The full-mode HTTP client (documented in `http-full-client-features.md`) lets C64 programs make arbitrary HTTP requests and read the response. The response body is captured as a raw buffer. Previously, extracting a specific field (e.g. `response.choices[0].message.content` from an OpenAI-compatible API) required a byte-by-byte state machine in BASIC — tedious, fragile, and limited by BASIC v2's 255-character string capacity.

Meatloaf already includes:

- **cJSON** (`lib/fnjson/`) — a full JSON parser and serializer
- **cJSON_Utils** — RFC 6901 JSON Pointer resolution (`cJSONUtils_GetPointer`)
- A working full-mode HTTP client with a command system (`m`, `h`, `b`, `s`, `r-h`, `r-b`, `status`, `c`)

## Approach

Add a `j <json-pointer>` command to the existing full-mode command set. It parses the already-captured response body through cJSON, resolves the pointer, serializes the matched value, and serves it on the next `GET#` — exactly like the existing `status` command.

## Command Reference

| Command | Description | Example |
|---------|-------------|---------|
| `j <pointer>` | Query JSON response, serve extracted value | `j /choices/0/message/content` |

### Usage

```basic
10 OPEN 1,8,2,"http://example.com/api/chat"
20 PRINT#1,"m post"
30 PRINT#1,"h content-type: application/json"
40 PRINT#1,"b {""query"":""hello""}"
50 PRINT#1,"s"
60 PRINT#1,"j /response/text"
70 GET#1,A$:IF ST AND 64 THEN 90   :REM EOI = done
80 PRINT CHR$(ASC(A$));:GOTO 70    :REM print extracted value
90 CLOSE 1
```

## Architecture

### New Commands

The `j` command follows the exact same pattern as the existing `status` command.

#### Fast Path (write handler, ~1ms max)

`handleCommand("j /path")`:
1. Extract the pointer argument (everything after `j `)
2. Parse `_bodyCapture` through `cJSON_Parse()` — the body was captured during `_queuedSend` execution in `read()`, so it's always populated when `j` is called
3. If parse fails: set `responseStatus = -99` (internal error), `responseConsumed = true`
4. Resolve pointer via `cJSONUtils_GetPointer()`:
   - Pointer found: serialize the matched value using `cJSON_PrintUnformatted()`
   - Pointer not found: set `responseStatus = -99`, `responseConsumed = true`
5. Free the cJSON tree
6. Store serialized result in `_jsonQueryResult`, set `_jsonQueryRequested = true`

#### Slow Path (read handler)

At the top of `read()`, before the response buffer serving logic, check `_jsonQueryRequested`:
- If true, serve bytes from `_jsonQueryResult` string
- Signal EOI (ST bit 6) when the string is fully consumed
- After EOI, `_jsonQueryRequested` remains false for subsequent reads

The user can issue another `j` query on the same body, or use `r-b` to read the full raw body, or `status` to re-read the status code, or `c` to clear and start a new request.

### Error Handling

| Failure | Behavior | ST bit |
|---------|----------|--------|
| Response body empty / not captured | `j` silently ignored, no-op | 0 |
| Body is not valid JSON | `responseStatus = -99` | 128 |
| JSON Pointer not found | `responseStatus = -99` | 128 |

The error is surfaced through the existing `iecStatus` mechanism, same as all other full-mode errors, with `msg = "JSON query failed"`.

### New Internal State (in `HTTPMStream`)

```cpp
bool _jsonQueryRequested = false;
std::string _jsonQueryResult;
```

The cJSON tree is allocated, queried, and freed during `handleCommand()` — no persistent allocation beyond the result string.

### Value Serialization

| JSON type | Serialized form |
|-----------|-----------------|
| String | The string value, raw bytes |
| Number | Decimal text (e.g. `42`, `3.14`) |
| Boolean | `TRUE` or `FALSE` |
| Null | `NULL` |
| Object | JSON text via `cJSON_PrintUnformatted` |
| Array | JSON text via `cJSON_PrintUnformatted` |

When the pointer targets a string, the value is returned without surrounding quotes — the most common case (extracting a response text or field value) gives back clean text ready to print.

### Example Interaction

```basic
REM === Full JSON query example ===
10 OPEN 1,8,2,"http://ollama.local:11434/v1/chat/completions"
20 PRINT#1,"m post"
30 PRINT#1,"h content-type: application/json"
40 BQ$ = "{"
50 BQ$ = BQ$ + CHR$(34) + "model" + CHR$(34) + ":" + CHR$(34) + "qwen2.5:0.5b" + CHR$(34)
60 BQ$ = BQ$ + "," + CHR$(34) + "messages" + CHR$(34) + ":[{"
70 BQ$ = BQ$ + CHR$(34) + "role" + CHR$(34) + ":" + CHR$(34) + "user" + CHR$(34)
80 BQ$ = BQ$ + "," + CHR$(34) + "content" + CHR$(34) + ":" + CHR$(34) + "hi" + CHR$(34)
90 BQ$ = BQ$ + "}]}"
100 PRINT#1,"b ";BQ$
110 PRINT#1,"s"
120 REM query the content field
130 PRINT#1,"j /choices/0/message/content"
140 REM read result
150 GET#1,A$:IF ST AND 64 THEN 190
160 PRINT CHR$(ASC(A$));
170 GOTO 150
190 PRINT:PRINT "---done---"
200 CLOSE 1
```

## Test Plan

### Native Unit Test
- Location: `test/native/test_http_full_client/test_http_full_client.cpp` (new test function)
- Coverage:
  - Parse valid JSON body, query pointer → returns expected string
  - Parse valid JSON body, query nested pointer → returns expected value
  - Parse valid JSON body, pointer not found → error state set
  - Empty body → no-op (no error)
  - Invalid JSON → error state set

### BASIC Integration Test
- Update `test/http/http_full_client_test.bas` with test 10: JSON query after POST
- Companion server echo endpoint sends back known JSON payload
- Verify extracted value matches expected field

### Build Verification
- `pio test -e native` — native unit tests pass
- `pio run -e fujiloaf-rev0` — embedded firmware builds

## Files Changed

### lib/meatloaf/network/http.h
- Add `_jsonQueryRequested` and `_jsonQueryResult` to `HTTPMStream`

### lib/meatloaf/network/http.cpp
- Add `#include <cJSON.h>` and `#include <cJSON_Utils.h>` to compiler flags (already linked)
- Add `j` command handling in `HTTPMStream::handleCommand()`
- Add json query result serving in `HTTPMStream::read()` (Phase 1.5, after `_queuedSend` check, before response buffer serving)

### test/native/test_http_full_client/
- Add JSON query unit tests

## Out of Scope

- **Iterative object/array browsing** — only single-pointer queries. The pointer must target a specific value.
- **Multiple queries before reading** — only one `j` result is buffered at a time; a second `j` replaces the first.
- **PETSCII conversion of results** — bytes are returned as-is from the JSON serialization. String values come back as raw UTF-8.
- **Modifying the response buffer** — `_responseBuffer` is untouched. `r-b` can still read the full body after a `j` query.
