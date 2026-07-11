; Compile with bc.py from VS64 — https://github.com/rolandshacks/vs64
POKE 53280,0 : POKE 53281,0 : POKE 646,1 : rem black border, black bg, white text
PRINT "{clr}";CHR$(14)

; ===== configuration (!! edit your api key below !!) =====
@modelName$ = "qwen2.5:0.5b"
@maxTokens = 100
@apiUrl$ = "http://192.168.1.131:11434/v1/chat/completions"
@apiKey$ = ""  : rem leave empty for ollama; set for openai

; ===== internal vars =====
@prompt$ = ""
@errFlag = 0
ch = 1  : rem logical file number

; ===== helpers for json building =====
q$ = CHR$(34)  : rem double quote "
o$ = CHR$(123) : rem open brace {
c$ = CHR$(125) : rem close brace }
; rem note: [, ], :, and , are standard PETSCII — used directly in strings

; ===== main =====
PRINT "OpenAI Chat on Commodore 64"
PRINT "==========================="
PRINT "selected model model: "; @modelName$
PRINT "max tokens: "; @maxTokens
PRINT

QuestionLoop:
    PRINT "your prompt (empty=quit):"
    INPUT @prompt$
    IF @prompt$ = "" THEN PRINT "ok bye!" : END
    PRINT
    PRINT "working..."
    GOSUB SendRequestSafe
    IF @errFlag <> 0 THEN PRINT : PRINT "failed" : GOTO QuestionLoop
    PRINT
    PRINT "---"
    GOSUB StreamResponse
    PRINT "---"
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
    PRINT# ch, "b+ " + q$ + "messages" + q$ + ":[" + o$
    PRINT# ch, "b+ " + q$ + "role" + q$ + ":" + q$ + "user" + q$ + ","
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

; ===== stream json query result to screen =====
StreamResponse:
    ; rem reads bytes via GET# and prints them
    ; rem until EOI (ST bit 64) or error (ST bit 128)

StreamByte:
    GET# ch, a$
    IF (ST AND 64) <> 0 THEN PRINT : RETURN  : rem eoi
    IF (ST AND 128) <> 0 THEN PRINT : PRINT "err" : RETURN : rem error
    PRINT CHR$(ASC(a$));  : rem print character
    GOTO StreamByte
