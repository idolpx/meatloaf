; Compile with bc.py from VS64 — https://github.com/rolandshacks/vs64
POKE 53280,0 : POKE 53281,0 : POKE 646,1 : rem black border, black bg, white text
PRINT "{clr}";CHR$(14)
POKE 204, 0 : rem reset cursor blink timer

; ===== configuration (!! edit your api key below !!) =====
@modelName$ = "qwen2.5:0.5b"
@maxTokens = 100
@apiUrl$ = "http://192.168.1.131:11434/v1/chat/completions"
@apiKey$ = ""  : rem leave empty for ollama; set for openai

; ===== internal vars =====
@sysPrompt$ = "You are a helpful assistant on running on an old hardware. Keep responses brief."
@prompt$ = ""
@resp$ = ""
@errFlag = 0
ch = 1  : rem logical file number

; ===== context tracking =====
DIM @ctxRole$(9), @ctxMsg$(9)
@ctxCount = 0

; ===== helpers for json building =====
q$ = CHR$(34)  : rem double quote "
o$ = CHR$(123) : rem open brace {
c$ = CHR$(125) : rem close brace }
; rem note: [, ], :, and , are standard PETSCII — used directly in strings

; ===== main =====
PRINT "OpenAI Chat on Commodore 64"
PRINT "==========================="
PRINT "model: "; @modelName$
PRINT "max tokens: "; @maxTokens
PRINT "system: "; @sysPrompt$

QuestionLoop:
    PRINT "{red}--- USER ---{lightgrey}"
    GOSUB LineFetch
    IF @prompt$ = "" THEN PRINT "ok bye!" : END

    ; rem --- sanitize against JSON injection ---
    @src$ = @prompt$ : GOSUB SanitizeStr
    @prompt$ = @result$

    ; rem --- store user message in context ---
    GOSUB EvictIfFull
    @ctxRole$(@ctxCount) = "user"
    @ctxMsg$(@ctxCount) = @prompt$
    @ctxCount = @ctxCount + 1

    PRINT
    PRINT "{red}working...";
    GOSUB SendRequestSafe
    IF @errFlag <> 0 THEN PRINT : PRINT "failed" : GOTO QuestionLoop
    PRINT
    PRINT "{red}--- AGENT ---{white}"
    GOSUB StreamAndCapture

    ; rem --- store assistant response in context ---
    GOSUB EvictIfFull
    @ctxRole$(@ctxCount) = "assistant"
    @ctxMsg$(@ctxCount) = @resp$
    @ctxCount = @ctxCount + 1

    CLOSE ch
    GOTO QuestionLoop

; ===== send request using b+ (no 255-char string overflow) =====
SendRequestSafe:
    @errFlag = 0
    OPEN ch, 8, 2, @apiUrl$
    IF (ST AND 128) <> 0 THEN @errFlag = 1 : RETURN
    PRINT# ch, "m post"
    IF @apiKey$ <> "" THEN PRINT# ch, "h authorization: bearer "; @apiKey$
    PRINT# ch, "h content-type: application/json"

    ; rem --- build json body piece by piece with b+ ---
    ; rem each PRINT# line stays well under 255 chars
    PRINT# ch, "b " + o$
    PRINT# ch, "b+ " + q$ + "model" + q$ + ":" + q$ + @modelName$ + q$ + ","
    PRINT# ch, "b+ " + q$ + "messages" + q$ + ":["
    ; rem system message
    PRINT# ch, "b+ " + o$ + q$ + "role" + q$ + ":" + q$ + "system" + q$ + "," + q$ + "content" + q$ + ":" + q$ + @sysPrompt$ + q$ + c$ + ","
    ; rem context history
    @ci = 0
CtxLoop:
    IF @ci >= @ctxCount THEN GOTO CtxLoopEnd
    ; rem open {"role":"assistant","content":"
    PRINT# ch, "b+ " + o$ + q$ + "role" + q$ + ":" + q$ + @ctxRole$(@ci) + q$ + "," + q$ + "content" + q$ + ":" + q$
    ; rem sanitize and send content in chunks of 100
    @ej = 1 : @echunk = 100
CtxChunk:
    IF @ej > LEN(@ctxMsg$(@ci)) THEN GOTO CtxChunkEnd
    @echunk$ = MID$(@ctxMsg$(@ci), @ej, @echunk)
    @src$ = @echunk$ : GOSUB SanitizeStr
    PRINT# ch, "b+ " + @result$
    @ej = @ej + @echunk
    GOTO CtxChunk
CtxChunkEnd:
    ; rem close "},
    PRINT# ch, "b+ " + q$ + c$ + ","
    @ci = @ci + 1
    GOTO CtxLoop
CtxLoopEnd:
    ; rem current user message
    PRINT# ch, "b+ " + o$ + q$ + "role" + q$ + ":" + q$ + "user" + q$ + "," + q$ + "content" + q$ + ":" + q$
    ; rem send sanitized prompt in chunks of 100
    @pi = 1
    @chunkSize = 100

PromptChunk:
    IF @pi > LEN(@prompt$) THEN GOTO PromptChunkEnd
    @pchunk$ = MID$(@prompt$, @pi, @chunkSize)
    @src$ = @pchunk$ : GOSUB SanitizeStr
    PRINT# ch, "b+ " + @result$
    @pi = @pi + @chunkSize
    GOTO PromptChunk

PromptChunkEnd:
    PRINT# ch, "b+ " + q$
    PRINT# ch, "b+ " + c$ + "]," + q$ + "max_tokens" + q$ + ":" + STR$(@maxTokens) + c$

    PRINT# ch, "s"
    GOSUB PollStatus
    PRINT "status: "; st$
    IF st$ <> "200" THEN @errFlag = 1 : CLOSE ch : PRINT "api error" : RETURN
    PRINT# ch, "j /choices/0/message/content"
    RETURN

; ===== read http status =====
PollStatus:
    PRINT# ch, "status"
    st$ = ""

FetchStatus:
    GET# ch, a$
    IF (ST AND 64) <> 0 THEN RETURN  : rem eoi
    IF (ST AND 128) <> 0 THEN RETURN : rem error
    IF a$ = CHR$(13) THEN RETURN     : rem cr = end of status
    st$ = st$ + a$
    GOTO FetchStatus

; ===== evict oldest 2 when context is full =====
EvictIfFull:
    IF @ctxCount < 10 THEN RETURN
    ; Shift everything down by 2 (drop oldest user+assistant turn)
    @ci = 0
EvictShift:
    IF @ci >= 8 THEN GOTO EvictDone
    @ctxRole$(@ci) = @ctxRole$(@ci + 2)
    @ctxMsg$(@ci) = @ctxMsg$(@ci + 2)
    @ci = @ci + 1
    GOTO EvictShift
EvictDone:
    @ctxCount = 8
    RETURN

; ===== line input via GET (handles commas, good with SHIFT-2 for ") =====
LineFetch:
    @prompt$ = ""
FetchKey:
    GET k$
    IF k$ = "" THEN GOTO FetchKey
    IF ASC(k$) = 13 THEN PRINT : RETURN  : rem enter = done
    IF ASC(k$) = 20 THEN IF LEN(@prompt$) > 0 THEN @prompt$ = LEFT$(@prompt$, LEN(@prompt$) - 1) : PRINT CHR$(20); " "; CHR$(20);  : rem del
    IF ASC(k$) = 20 THEN GOTO FetchKey
    PRINT k$;
    @prompt$ = @prompt$ + k$
    GOTO FetchKey

; ===== sanitize string in @src$, return in @result$ =====
; rem replaces " with ' so JSON strings stay valid
; rem backslash-escaping is impractical here because PETSCII
; rem doesn't have a clean ASCII backslash. Single quote is safe.
SanitizeStr:
    @result$ = ""
    @si = 1
SanitizeLoop:
    IF @si > LEN(@src$) THEN RETURN
    @ch$ = MID$(@src$, @si, 1)
    IF @ch$ = q$ THEN @result$ = @result$ + "'" : GOTO SanitizeNext
    ; rem strip control codes below $20 (except line feed etc.)
    IF ASC(@ch$) < 32 AND ASC(@ch$) <> 10 AND ASC(@ch$) <> 13 THEN GOTO SanitizeNext
    @result$ = @result$ + @ch$
SanitizeNext:
    @si = @si + 1
    GOTO SanitizeLoop

; ===== stream json query result to screen and capture in @resp$ =====
StreamAndCapture:
    @resp$ = ""
    @rc = 0

StreamByte:
    GET# ch, a$
    IF (ST AND 64) <> 0 THEN PRINT : RETURN  : rem eoi
    IF (ST AND 128) <> 0 THEN PRINT : PRINT "err" : RETURN : rem error
    PRINT CHR$(ASC(a$));  : rem print character
    ; rem capture up to 230 chars for context
    IF @rc < 196 THEN @resp$ = @resp$ + a$ : @rc = @rc + 1
    GOTO StreamByte
