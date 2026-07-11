; Compile with bc.py from VS64 — https://github.com/rolandshacks/vs64
POKE 53280,0 : POKE 53281,0 : POKE 646,1 : rem black border, black bg, white text
PRINT "{clr}";CHR$(14)

; ===== configuration (!! edit your api key below !!) =====
@modelName$ = "qwen2.5:0.5b"
@maxTokens = 100
@apiUrl$ = "http://192.168.1.131:11434/v1/chat/completions"
@apiKey$ = ""  : rem leave empty for ollama; set for openai

; ===== internal vars =====
@sysPrompt$ = "Pose as Commodore 64 computer. Keep responses brief."
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
    INPUT @prompt$
    IF @prompt$ = "" THEN PRINT "ok bye!" : END

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
    PRINT# ch, "b+ " + o$ + q$ + "role" + q$ + ":" + q$ + @ctxRole$(@ci) + q$ + "," + q$ + "content" + q$ + ":" + q$ + @ctxMsg$(@ci) + q$ + c$ + ","
    @ci = @ci + 1
    GOTO CtxLoop
CtxLoopEnd:
    ; rem current user message
    PRINT# ch, "b+ " + o$ + q$ + "role" + q$ + ":" + q$ + "user" + q$ + ","
    PRINT# ch, "b+ " + q$ + "content" + q$ + ":" + q$
    ; rem send prompt in <200 byte chunks to stay under 255
    @pi = 1
    @chunkSize = 200

PromptChunk:
    IF @pi > LEN(@prompt$) THEN GOTO PromptChunkEnd
    @pchunk$ = MID$(@prompt$, @pi, @chunkSize)
    PRINT# ch, "b+ " + @pchunk$
    @pi = @pi + @chunkSize
    GOTO PromptChunk

PromptChunkEnd:
    PRINT# ch, "b+ " + q$
    PRINT# ch, "b+ " + c$ + "]," + q$ + "max_tokens" + q$ + ":" + STR$(@maxTokens) + c$

    PRINT# ch, "s"
    GOSUB ReadStatus
    PRINT "status: "; st$
    IF st$ <> "200" THEN @errFlag = 1 : CLOSE ch : PRINT "api error" : RETURN
    PRINT# ch, "j /choices/0/message/content"
    RETURN

; ===== read http status =====
ReadStatus:
    PRINT# ch, "status"
    st$ = ""

ReadStatusByte:
    GET# ch, a$
    IF (ST AND 64) <> 0 THEN RETURN  : rem eoi
    IF (ST AND 128) <> 0 THEN RETURN : rem error
    IF a$ = CHR$(13) THEN RETURN     : rem cr = end of status
    st$ = st$ + a$
    GOTO ReadStatusByte

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
