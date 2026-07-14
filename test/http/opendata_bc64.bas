; Compile with bc.py from VS64 — https://github.com/rolandshacks/vs64
; bc64 -a -o opendata.prg opendata.bas
;
; ADS-B Open Data Explorer for Commodore 64
;
; Fetches live aircraft tracking data from opendata.adsb.fi
; using Meatloaf full-mode HTTP client (device 8, secondary 2).
;
; Features:
;   1. Find by Callsign  — search active aircraft by flight number
;   2. Find by Hex Code  — look up a specific Mode-S transponder code
;   3. Military Aircraft — list all military aircraft currently tracked
;   4. Near Airport      — scan airspace around one of 6 major airports
;
; Data source: https://opendata.adsb.fi (free, no API key, 1 req/s limit)
; Attribution: adsb.fi — https://adsb.fi
;
; Requires: Meatloaf device on IEC bus (default device 8)
;           HTTP (adsb.fi uses plain HTTP, no TLS needed)

; --- Themed palette: radar / dark terminal ---
POKE 53280,0 : POKE 53281,0 : POKE 646,5 : rem black border, black bg, green text
PRINT "{clr}"

; --- JSON builder helpers ---
qu$=chr$(34) : ob$=chr$(123) : cb$=chr$(125)

; ============================================================
; SHARED VARIABLES
; ============================================================

@ch%    = 1    : rem HTTP channel
@url$   = ""   : rem URL to fetch
@sc$    = ""   : rem HTTP status string (output of HttpFetch)
@jv$    = ""   : rem JSON value buffer (output of Jq)
@ptr$   = ""   : rem JSON pointer path
@total  = 0    : rem total aircraft count from API
@ix     = 0    : rem loop / index variable (float, for FOR loops)
@sel%   = 0    : rem user selection
@in$    = ""   : rem keyboard / input buffer
@is$    = ""   : rem index as string (RIGHT$(STR$(x), LEN-1))

; --- Display field buffers ---
@hx$    = ""   : rem hex code
@fl$    = ""   : rem flight / callsign
@rg$    = ""   : rem registration
@tp$    = ""   : rem type code (t field)
@dc$    = ""   : rem description
@ab$    = ""   : rem barometric altitude
@gs$    = ""   : rem ground speed
@tk$    = ""   : rem track angle
@lt$    = ""   : rem latitude
@ln$    = ""   : rem longitude
@sq$    = ""   : rem squawk
@em$    = ""   : rem emergency status
@op$    = ""   : rem operator name
@ds$    = ""   : rem distance (v3 geo only)
@dr$    = ""   : rem direction (v3 geo only)
@yr$    = ""   : rem year

; ============================================================
; MAIN MENU LOOP
; ============================================================

GOTO Begin

Begin:
    GOSUB ShowMenu
MainWait:
    @in$ = ""
    GET @in$
    IF @in$ = "" THEN MainWait
    @sel% = ASC(@in$) - 48
    IF @sel% = 0 THEN QuitProg
    IF @sel% = 1 THEN FindCallsign
    IF @sel% = 2 THEN FindHex
    IF @sel% = 3 THEN FindMilitary
    IF @sel% = 4 THEN NearAirport
    GOTO Begin

QuitProg:
    PRINT "{clr}{down}"
    PRINT "{yel}  Live ADS-B data from adsb.fi{cyn}"
    PRINT "  https://adsb.fi"
    PRINT ""
    PRINT "{green}  Thanks for flying!{cyn}"
    END

; ============================================================
; MENU SCREEN
; ============================================================

ShowMenu:
    PRINT "{clr}{down}"
    PRINT "{yel}  *** ADS-B AIRCRAFT TRACKER ***"
    PRINT "{cyn}  --------------------------------"
    PRINT ""
    PRINT "  1. Find by Callsign"
    PRINT "  2. Find by Hex Code"
    PRINT "  3. Military Aircraft"
    PRINT "  4. Near Airport"
    PRINT ""
    PRINT "  0. Exit"
    PRINT "{down}  Choice? {wht}";
    RETURN

; ============================================================
; 1 — CALLSIGN SEARCH
; ============================================================

FindCallsign:
    PRINT "{clr}{down}"
    PRINT "  Enter callsign or airline"
    PRINT "  (e.g. FIN1LP or BAW)"
    PRINT "{down}  Callsign? {wht}";
    INPUT ""; @in$
    IF @in$ = "" THEN Begin
    @url$ = "http://opendata.adsb.fi/api/v2/callsign/" + @in$
    GOSUB HttpFetch
    IF VAL(@sc$) <> 200 THEN GOSUB HttpError : GOTO AfterCall
    GOSUB FetchTotal
    IF @total = 0 THEN GOSUB ShowZero : GOTO AfterCall
    IF @total > 8 THEN @total = 8
    GOSUB ShowAircraft
    GOSUB SelAircraft
    IF @sel% > 0 AND @sel% <= @total THEN @ix = @sel% - 1 : GOSUB ShowDetail
AfterCall:
    GOSUB ChannelClose
    GOTO Begin

; ============================================================
; 2 — HEX CODE SEARCH
; ============================================================

FindHex:
    PRINT "{clr}{down}"
    PRINT "  Enter Mode-S hex code"
    PRINT "  (e.g. 461E1A)"
    PRINT "{down}  Hex? {wht}";
    INPUT ""; @in$
    IF @in$ = "" THEN Begin
    @url$ = "http://opendata.adsb.fi/api/v2/hex/" + @in$
    GOSUB HttpFetch
    IF VAL(@sc$) <> 200 THEN GOSUB HttpError : GOTO AfterHex
    GOSUB FetchTotal
    IF @total = 0 THEN GOSUB ShowZero : GOTO AfterHex
    @ix = 0
    GOSUB ShowDetail
AfterHex:
    GOSUB ChannelClose
    GOTO Begin

; ============================================================
; 3 — MILITARY AIRCRAFT
; ============================================================

FindMilitary:
    @url$ = "http://opendata.adsb.fi/api/v2/mil"
    GOSUB HttpFetch
    IF VAL(@sc$) <> 200 THEN GOSUB HttpError : GOTO AfterMil
    GOSUB FetchTotal
    IF @total = 0 THEN GOSUB ShowZero : GOTO AfterMil
    IF @total > 8 THEN @total = 8
    GOSUB ShowAircraft
    GOSUB SelAircraft
    IF @sel% > 0 AND @sel% <= @total THEN @ix = @sel% - 1 : GOSUB ShowDetail
AfterMil:
    GOSUB ChannelClose
    GOTO Begin

; ============================================================
; 4 — NEAR AIRPORT
; ============================================================

NearAirport:
    PRINT "{clr}{down}"
    PRINT "{yel}  Select Airport:{cyn}"
    PRINT "  -----------------"
    PRINT "  1. JFK    New York"
    PRINT "  2. LHR    London Heathrow"
    PRINT "  3. LAX    Los Angeles"
    PRINT "  4. HND    Tokyo Haneda"
    PRINT "  5. HEL    Helsinki"
    PRINT "  6. SYD    Sydney"
    PRINT ""
    PRINT "  0. Back to menu"
    PRINT "{down}  Airport? {wht}";
AirChoice:
    @in$ = ""
    GET @in$
    IF @in$ = "" THEN AirChoice
    @sel% = ASC(@in$) - 48
    IF @sel% = 0 THEN Begin
    IF @sel% = 1 THEN @url$ = "http://opendata.adsb.fi/api/v3/lat/40.64/lon/-73.78/dist/25"
    IF @sel% = 2 THEN @url$ = "http://opendata.adsb.fi/api/v3/lat/51.47/lon/-0.45/dist/25"
    IF @sel% = 3 THEN @url$ = "http://opendata.adsb.fi/api/v3/lat/33.94/lon/-118.41/dist/25"
    IF @sel% = 4 THEN @url$ = "http://opendata.adsb.fi/api/v3/lat/35.55/lon/139.78/dist/25"
    IF @sel% = 5 THEN @url$ = "http://opendata.adsb.fi/api/v3/lat/60.32/lon/24.95/dist/25"
    IF @sel% = 6 THEN @url$ = "http://opendata.adsb.fi/api/v3/lat/-33.95/lon/151.18/dist/25"
    IF @sel% < 1 OR @sel% > 6 THEN NearAirport
    GOSUB HttpFetch
    IF VAL(@sc$) <> 200 THEN GOSUB HttpError : GOTO AfterAir
    GOSUB FetchTotal
    IF @total = 0 THEN GOSUB ShowZero : GOTO AfterAir
    IF @total > 8 THEN @total = 8
    GOSUB ShowAircraft
    GOSUB SelAircraft
    IF @sel% > 0 AND @sel% <= @total THEN @ix = @sel% - 1 : GOSUB ShowDetail
AfterAir:
    GOSUB ChannelClose
    GOTO Begin

; ============================================================
; SUB: Fetch /total from buffered response
; ============================================================

FetchTotal:
    @ptr$ = "/total"
    GOSUB Jq
    @total = VAL(@jv$)
    RETURN

; ============================================================
; SUB: Show "none found" message
; ============================================================

ShowZero:
    PRINT "{down}{red}  No aircraft found.{cyn}"
    PRINT ""
    PRINT "{wht}  Press any key...{cyn}"
    GOSUB AnyKey
    RETURN

; ============================================================
; SUB: Show HTTP error
; ============================================================

HttpError:
    PRINT "{down}{red}  HTTP Error: "; @sc$
    PRINT "  Meatloaf on IEC device 8?{cyn}"
    PRINT ""
    PRINT "{wht}  Press any key...{cyn}"
    GOSUB AnyKey
    RETURN

; ============================================================
; SUB: Display compact list of aircraft
; Shows: flight, type, altitude, speed for each up to @total
; ============================================================

ShowAircraft:
    PRINT "{clr}{down}"
    PRINT "{yel}  Results: "; @total; " aircraft{cyn}"
    PRINT "  ----------------------"
    PRINT ""
    FOR @ix = 0 TO @total - 1
        @is$ = STR$(@ix)
        @is$ = RIGHT$(@is$, LEN(@is$) - 1)

        ; Read flight/callsign
        @ptr$ = "/ac/" + @is$ + "/flight"
        GOSUB Jq
        @fl$ = @jv$
        @ts$ = @fl$ : GOSUB Rtrim
        @fl$ = @tr$

        ; Read registration
        @ptr$ = "/ac/" + @is$ + "/r"
        GOSUB Jq
        @rg$ = @jv$
        IF @rg$ = "" THEN @rg$ = "--"

        ; Read type code
        @ptr$ = "/ac/" + @is$ + "/t"
        GOSUB Jq
        @tp$ = @jv$
        IF @tp$ = "" THEN @tp$ = "--"

        ; Read barometric altitude
        @ptr$ = "/ac/" + @is$ + "/alt_baro"
        GOSUB Jq
        @ab$ = @jv$
        IF @ab$ = "" THEN @ab$ = "--"

        ; Read ground speed
        @ptr$ = "/ac/" + @is$ + "/gs"
        GOSUB Jq
        @gs$ = "--"
        IF @jv$ <> "" THEN @gs$ = @jv$ + "kt"

        ; Print: number + flight + type + alt + speed
        PRINT " "; @ix + 1; ". ";
        IF LEN(@fl$) > 7 THEN PRINT LEFT$(@fl$,7);
        IF LEN(@fl$) <= 7 THEN PRINT @fl$;
        PRINT "  "; @tp$; "  "; @ab$; "  "; @gs$
    NEXT @ix
    RETURN

; ============================================================
; SUB: Let user pick an aircraft from the list
; Returns: @sel% (0 = back, 1-8 = selection)
; ============================================================

SelAircraft:
    PRINT ""
    PRINT "  Select aircraft (0=back): {wht}";
    @in$ = ""
    GET @in$
    IF @in$ = "" THEN SelAircraft
    @sel% = ASC(@in$) - 48
    PRINT @sel%
    RETURN

; ============================================================
; SUB: Show full details for aircraft at index @ix
; ============================================================

ShowDetail:
    @is$ = STR$(@ix)
    @is$ = RIGHT$(@is$, LEN(@is$) - 1)

    ; Read hex
    @ptr$ = "/ac/" + @is$ + "/hex"
    GOSUB Jq : @hx$ = @jv$

    ; Read flight/callsign and trim
    @ptr$ = "/ac/" + @is$ + "/flight"
    GOSUB Jq : @fl$ = @jv$
    @ts$ = @fl$ : GOSUB Rtrim
    @fl$ = @tr$

    ; Read registration
    @ptr$ = "/ac/" + @is$ + "/r"
    GOSUB Jq : @rg$ = @jv$
    IF @rg$ = "" THEN @rg$ = "--"

    ; Read type code
    @ptr$ = "/ac/" + @is$ + "/t"
    GOSUB Jq : @tp$ = @jv$
    IF @tp$ = "" THEN @tp$ = "--"

    ; Read description
    @ptr$ = "/ac/" + @is$ + "/desc"
    GOSUB Jq : @dc$ = @jv$
    IF @dc$ = "" THEN @dc$ = "--"

    ; Read altitude
    @ptr$ = "/ac/" + @is$ + "/alt_baro"
    GOSUB Jq : @ab$ = @jv$
    IF @ab$ = "" THEN @ab$ = "--"

    ; Read ground speed
    @ptr$ = "/ac/" + @is$ + "/gs"
    GOSUB Jq : @gs$ = @jv$
    IF @gs$ = "" THEN @gs$ = "--"
    IF @gs$ <> "--" THEN @gs$ = @gs$ + " kt"

    ; Read track angle
    @ptr$ = "/ac/" + @is$ + "/track"
    GOSUB Jq : @tk$ = @jv$
    IF @tk$ = "" THEN @tk$ = "--"
    IF @tk$ <> "--" THEN @tk$ = @tk$ + chr$(94)

    ; Read lat/lon
    @ptr$ = "/ac/" + @is$ + "/lat"
    GOSUB Jq : @lt$ = @jv$
    IF @lt$ = "" THEN @lt$ = "--"

    @ptr$ = "/ac/" + @is$ + "/lon"
    GOSUB Jq : @ln$ = @jv$
    IF @ln$ = "" THEN @ln$ = "--"

    ; Read squawk
    @ptr$ = "/ac/" + @is$ + "/squawk"
    GOSUB Jq : @sq$ = @jv$
    IF @sq$ = "" THEN @sq$ = "--"

    ; Read emergency status
    @ptr$ = "/ac/" + @is$ + "/emergency"
    GOSUB Jq : @em$ = @jv$
    IF @em$ = "" THEN @em$ = "--"

    ; Read operator
    @ptr$ = "/ac/" + @is$ + "/ownOp"
    GOSUB Jq : @op$ = @jv$
    IF @op$ = "" THEN @op$ = "--"

    ; Read year
    @ptr$ = "/ac/" + @is$ + "/year"
    GOSUB Jq : @yr$ = @jv$
    IF @yr$ = "" THEN @yr$ = "--"

    ; Try distance + direction (v3 geo endpoints only)
    @ptr$ = "/ac/" + @is$ + "/dst"
    GOSUB Jq : @ds$ = @jv$

    @ptr$ = "/ac/" + @is$ + "/dir"
    GOSUB Jq : @dr$ = @jv$

    ; --- Display detail screen ---
    PRINT "{clr}{down}"
    PRINT "{yel}  -- AIRCRAFT DETAILS --{cyn}"
    PRINT ""
    PRINT "  {wht}HEX:{cyn} "; @hx$
    PRINT "  {wht}CALL:{cyn} "; @fl$
    PRINT "  {wht}REG: {cyn}"; @rg$; "  {wht}TYPE:{cyn} "; @tp$
    PRINT "  {wht}ALT: {cyn}"; @ab$
    PRINT "  {wht}SPD: {cyn}"; @gs$; "  {wht}TRK:{cyn} "; @tk$
    PRINT "  {wht}LAT: {cyn}"; @lt$
    PRINT "  {wht}LON: {cyn}"; @ln$
    PRINT "  {wht}SQK: {cyn}"; @sq$; "  {wht}EMRG:{cyn} "; @em$

    ; Show description (truncate to fit)
    IF @dc$ <> "--" THEN PRINT "  {wht}DESC:{cyn} "; LEFT$(@dc$, 30)
    IF @op$ <> "--" THEN PRINT "  {wht}OP:  {cyn}"; LEFT$(@op$, 30)
    IF @ds$ <> "" THEN PRINT "  {wht}DST: {cyn}"; @ds$; " NM  {wht}DIR:{cyn} "; @dr$; chr$(94)

    PRINT ""
    PRINT "{green}  Live from adsb.fi{cyn}"
    PRINT ""
    PRINT "{wht}  Press any key for menu...{cyn}"
    GOSUB AnyKey
    RETURN

; ============================================================
; HTTP SUBROUTINES
; ============================================================

; Subroutine: HttpFetch
; Opens channel, sends GET to @url$, reads status into @sc$.
; Channel stays open for subsequent Jq calls.
HttpFetch:
    OPEN @ch%,8,2,@url$
    PRINT#@ch%,"m get"
    PRINT#@ch%,"s"
    PRINT#@ch%,"status"
    @sc$ = ""
HttpFetch_Read:
    GET#@ch%, A$
    IF ST AND 64 THEN RETURN
    IF ST AND 128 THEN @sc$ = "-1" : RETURN
    IF A$ = CHR$(13) THEN RETURN
    @sc$ = @sc$ + A$
    GOTO HttpFetch_Read

; Subroutine: Jq
; Sends JSON Pointer @ptr$ to the buffered response, reads value into @jv$.
Jq:
    PRINT#@ch%,"j " + @ptr$
    @jv$ = ""
Jq_Read:
    GET#@ch%, A$
    IF ST AND 64 THEN RETURN
    IF ST AND 128 THEN @jv$ = "" : RETURN
    @jv$ = @jv$ + A$
    IF LEN(@jv$) > 250 THEN RETURN
    GOTO Jq_Read

; Subroutine: ChannelClose
ChannelClose:
    CLOSE @ch%
    RETURN

; ============================================================
; UTILITY SUBROUTINES
; ============================================================

; Subroutine: Rtrim
; Strips trailing spaces from @ts$, result in @tr$.
Rtrim:
    @tr$ = @ts$
    IF LEN(@tr$) = 0 THEN RETURN
Rtrim_Loop:
    IF RIGHT$(@tr$, 1) <> " " THEN RETURN
    @tr$ = LEFT$(@tr$, LEN(@tr$) - 1)
    GOTO Rtrim_Loop

; Subroutine: AnyKey
; Waits for any keypress.
AnyKey:
    GET K$
    IF K$ = "" THEN AnyKey
    RETURN
