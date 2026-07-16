# ESP_ERR_HTTP_CONNECT on repeated POST requests

## Symptom

After 2–4 successful POST requests to the same HTTPS server, the next
`esp_http_client_perform()` returns error **28679** (`ESP_ERR_HTTP_CONNECT`)
with `stCode = -1`.  The server is reachable (curl from the host works).
The next request usually succeeds again.

Observed on: `fujiloaf-rev0` (ESP32-WROVER, 8MB PSRAM, 16MB Flash).

## Serial log evidence

```
[lib/meatloaf/network/http.cpp:1023] POST(): POST url[http://192.168.1.222:11434/v1/chat/completions]
[lib/meatloaf/network/http.cpp:1200] close(): Q2-DIAG close: http=0x3fffa064 is_open=1 postBuf=94 postResp=0 lastRC=200
[lib/meatloaf/network/http.cpp:1206] close(): Sending POST body (94 bytes) from buffer
[lib/meatloaf/network/http.cpp:1212] close(): Q2-DIAG perform: result=28679 http=0x3fffa064 stCode=-1 capBytes=0
[lib/meatloaf/network/http.cpp:1214] close(): esp_http_client_perform result: 28679
[lib/meatloaf/network/http.cpp:1227] close(): HTTP Close and Cleanup
[lib/meatloaf/network/http.cpp:1023] POST(): POST url[http://192.168.1.222:11434/v1/chat/completions]
[lib/meatloaf/network/http.cpp:1206] close(): Sending POST body (94 bytes) from buffer
[lib/meatloaf/network/http.cpp:1212] close(): Q2-DIAG perform: result=0 http=0x3fffa064 stCode=200 capBytes=1022   ← retry works
```

## Root cause

**Every POST request allocates a brand-new TCP connection and destroys it
immediately after**, never reusing sockets.  The call chain per request is:

```
open() → init() → esp_http_client_init()        ← allocates new TCP socket
close() → esp_http_client_perform()             ← TCP SYN + TLS handshake + HTTP
        → esp_http_client_close()
        → esp_http_client_cleanup()              ← destroys socket + handle
        → _http = nullptr
```

After 2–4 fast requests the ESP32's lwIP stack runs out of available
socket descriptors because previous sockets are stuck in `TIME_WAIT` and
the pool (typically 4–10 sockets) is exhausted.  `esp_http_client_init()`
creates a new internal socket, but `esp_http_client_perform()` fails with
`ESP_ERR_HTTP_CONNECT` when it tries to connect because no fresh
descriptor is available.

The existing keep-alive settings in `init()` (`keep_alive_enable = true`,
`keep_alive_idle = 5`, `keep_alive_interval = 5`) are **never exercised**
because the handle is destroyed after every request.

## Fixed in commit xxx

Handle reuse is now implemented properly:
- `close()` — skips `esp_http_client_cleanup()` to keep `_http` alive
- `init()` — if origin (scheme://host) matches, flushes state with
  `esp_http_client_flush_response()` + clears all local buffers, then
  calls `esp_http_client_set_url()` instead of recreating the handle
- `processRedirectsAndOpen()` — **always** destroys the handle on 3xx
  redirects regardless of origin match, because the 303 body half-closes
  the connection and the stale data cannot be safely flushed

Tests pass: 3 rapid POSTs, Wikipedia 303 redirect, SWAPI GET.

## What a proper fix needs to address

1. **Keep the `_http` handle alive across requests** so the same TCP
   connection is reused.  This avoids socket exhaustion.

2. **But completely reset response state between requests** so redirect
   bodies don't leak.  Before reusing the handle, the code must clear:
   - `postResponse`
   - `preservedPostResponse`
   - `_size`
   - `_position`
   - The internal ESP response buffer (`raw_data` / `orig_raw_data`)

3. **`init()` should check if the URL scheme/port changed.**  If the
   redirect points to a different host, the old handle must be destroyed
   and a new one created (different TLS context).  For the same host, reuse
   is safe.

4. **On the first request after handle reuse**, the ESP-IDF transport does
   its own internal pool lookup and may still decide to open a fresh TCP
   connection if the keep-alive idle time expired.  That's fine — the
   important thing is avoiding handle creation churn, not guaranteeing
   zero TCP SYNs.

## Files involved

| File | Key functions |
|------|--------------|
| `lib/meatloaf/network/http.cpp` | `MeatHttpClient::init()` (line 1795), `MeatHttpClient::close()` (line 1199), `MeatHttpClient::open()` (line 1044), `MeatHttpClient::processRedirectsAndOpen()` (line 1101), `openAndFetchHeaders()` (line 1439) |
| `lib/meatloaf/network/http.h` | `MeatHttpClient` class fields: `_http`, `_is_open`, `postBuffer`, `postResponse`, `preservedPostResponse`, `_size`, `_position`, `lastRC` |

## Test client

A chat client that sends repeated POST requests to an OpenAI-compatible API
at `/home/qus/dev/_c64/c64-chat-client-c/` (cc65 C64 program using Meatloaf
full HTTP mode).  Replicate by running the C64 client and sending 3–4
messages in quick succession.  The third or fourth message triggers the
`-1` / `ESP_ERR_HTTP_CONNECT` failure.
