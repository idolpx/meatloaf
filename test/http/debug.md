# Debug Notes — Full-Mode HTTP Client Issues

## Resolved Issues

### JSON body overflow (>255 chars)
**Problem:** BASIC V2 strings are limited to 255 characters. Building a JSON
POST body with `PRINT# ch,"b " + body$` overflows when `body$` exceeds 255.

**Fix (BASIC level):** Use `b+` (append body) command — no 255-char limit:
```basic
PRINT# ch, "b " + "{\"model\":\"" + model$ + "\""
PRINT# ch, "b+,\"prompt\":\"" + prompt$ + "\""
PRINT# ch, "b+,\"max_tokens\":100}"
```

### Comma in INPUT treated as delimiter
**Problem:** BASIC V2 `INPUT` treats commas as variable separators, causing
`?EXTRA IGNORED` when user input contains commas.

**Fix:** Replace `INPUT` with custom `LineFetch` subroutine using `GET`:
```basic
LineFetch:
  GET a$ : IF a$="" THEN LineFetch
  IF ASC(a$)<>13 THEN ...
```

### Label starting with BASIC keyword
**Problem:** bc64 labels like `ReadLine` start with `READ`, causing the
tokenizer to interpret them as BASIC `READ` statements (`?OUT OF DATA`).

**Fix:** Avoid labels starting with BASIC keywords
(`READ`, `GET`, `INPUT`, `NEXT`, `FOR`, `DATA`, `ON`, `GOTO`, `GOSUB`, etc.).

### JSON injection via user input quotes
**Problem:** User input containing `"` breaks JSON body structure, causing
API parse errors.

**Fix (BASIC level):** `SanitizeStr` subroutine replaces `"` with `'`:
```basic
SanitizeStr:
  FOR @i=1 TO LEN(@input$)
    @c$=MID$(@input$,@i,1)
    IF @c$=CHR$(34) THEN @c$=CHR$(39)
    @result$=@result$+@c$
  NEXT @i
  RETURN
```

### Cursor invisible during custom GET input loop
**Problem:** The cursor disappears when using GET loops because the KERNAL
cursor blink timer (`$00C6`) expires.

**Fix (BASIC level):** `POKE 204, 0` resets the cursor blink timer so the
cursor stays visible during user input.

### `cO` Prefix in JSON Query Responses

**Problem:** `43 6F` bytes (= PETSCII `"cO"` from `Co` in `Content-Type`)
appeared at the start of JSON pointer (`j`) query responses when they
followed a `status` read on the same channel. Only affected JiffyDOS.

**Root cause:** JiffyDOS burst-reads 256 bytes from Meatloaf in one
fast-serial transaction. When BASIC's `GET#` loop for `status` consumed
only `"200\r"` (4 bytes), 252 bytes of headers+body remained cached in
C64 RAM. A subsequent `j` command then saw stale `"Co"` from the cache
before reaching fresh JSON data.

**Fix (firmware level, `http.cpp`):**
1. `handleCommand("status")` stores `_statusEnd` (position after status
   line in `_responseBuffer`).
2. Phase 3's `read()` caps status-serving bytes to `_statusEnd`, then
   advances `_responseBufPos` past ALL remaining response content so the
   JiffyDOS background task can't pre-cache headers/body into C64 RAM.
3. `handleCommand("j ...")` also advances `_responseBufPos` past stale
   content as a safety net.

**Files modified:**
- `lib/meatloaf/network/http.cpp` — status read capping and buffer skip
- `lib/meatloaf/network/http.h` — Phase 2 JSON `available()` override
