; Wikipedia Explorer v1 — bc64 version
; Meatloaf full-mode HTTP on device 8
; Uses Wikimedia REST API
; Compile: bc64 -o wikipedia_bc64.prg wikipedia_bc64.bas
; No --aliases: all variables are 2-char unique names

cn = 1

; JSON helper constants — " { }
qu$ = chr$(34)
ob$ = chr$(123)
cb$ = chr$(125)

; Screen setup — blue border, black bg, light blue text
poke 53280, 6 : poke 53281, 0 : poke 646, 14
dim t0$(9), u0$(9)

print "{clr}"
print "{5 down}{right}{right}{right}{right}{right}*** wikipedia ***"
print "A FREE ONLINE ENCYCLOPEDIA"
print "ANYONE CAN EDIT. FOUNDED IN"
print "2001 BY JIMMY WALES &"
print "LARRY SANGER."
print
print "KEY FACTS:"
print "- OVER 60 MILLION ARTICLES"
print "- 300+ LANGUAGES"
print "- ONE OF THE MOST VISITED"
print "  WEBSITES IN THE WORLD."
print

ShowMenu:
    print "{clr}"
    print "1. search articles"
    print "2. random article"
    print "3. lookup by name"
    print "q. quit"
    print

LoopKey:
    get ky$
    if ky$ = "" then LoopKey
    if ky$ = "q" then DoQuit
    if ky$ = "1" then gosub SrchArt : goto ShowMenu
    if ky$ = "2" then gosub PickRand : goto ShowMenu
    if ky$ = "3" then gosub LookupArt : goto ShowMenu
    goto LoopKey

DoQuit:
    poke 53280, 14 : poke 53281, 6
    print "{clr}"
    end

AnyKey:
    get ky$
    if ky$ = "" then AnyKey
    return

; --- HTTP GET ---
DoFetch:
    close cn
    open cn, 8, 2, ur$
    print#cn, "m get"
    print#cn, "h user-agent: Meatloaf/1.0 (Commodore 64; meatloaf-firmware)"
    print#cn, "s"
    print#cn, "status"
    st$ = ""

RdSt:
    get#cn, a$
    if st and 64 then return
    if st and 128 then st$ = "-1" : return
    if a$ = chr$(13) then return
    st$ = st$ + a$
    goto RdSt

; --- JSON pointer ---
JqPtr:
    print#cn, "j " + pt$
    v$ = ""

RdJq:
    get#cn, a$
    v$ = v$ + a$
    if st and 64 then return
    if st and 128 then v$ = "(n/a)" : return
    if len(v$) < 250 then RdJq
    return

; --- search articles ---
SrchArt:
    print "{clr}{green}search wikipedia{lightblue}"
    print
    input "search term"; sq$
    if sq$ = "" then return
    ar$ = sq$
    gosub ShowSum
    return

; --- show article summary (uses ar$) ---
ShowSum:
    print "{clr}{green}fetching article...{lightblue}"
    ; Build URL: replace spaces with underscores
    tp$ = ""
    for ii = 1 to len(ar$)
        if mid$(ar$, ii, 1) = " " then tp$ = tp$ + "_" : goto NxtCh
        tp$ = tp$ + mid$(ar$, ii, 1)
NxtCh:
    next ii
    ur$ = "https://en.wikipedia.org/api/rest_v1/page/summary/" + tp$ + "/"
    gosub DoFetch
    if st$ <> "200" then print "error "; st$ : gosub AnyKey : return
    ; Title
    print "{clr}{green}"
    pt$ = "/title" : gosub JqPtr
    print "title: "; v$
    ; Description
    print "{lightblue}"
    pt$ = "/description" : gosub JqPtr
    print v$
    print
    ; Extract
    pt$ = "/extract" : gosub JqPtr
    ex$ = v$
    if len(ex$) > 230 then ex$ = left$(ex$, 230)
    print ex$
    print
    print "any key=menu"
    gosub AnyKey
    close cn
    return

; --- random article ---
PickRand:
    print "{clr}{green}fetching random article...{lightblue}"
    ur$ = "https://en.wikipedia.org/api/rest_v1/page/random/summary"
    gosub DoFetch
    if st$ <> "200" then print "error "; st$ : gosub AnyKey : return
    ; Title
    print "{clr}{green}"
    pt$ = "/title" : gosub JqPtr
    ar$ = v$
    print "title: "; v$
    ; Description
    print "{lightblue}"
    pt$ = "/description" : gosub JqPtr
    print v$
    print
    ; Extract
    pt$ = "/extract" : gosub JqPtr
    ex$ = v$
    if len(ex$) > 230 then ex$ = left$(ex$, 230)
    print ex$
    print
    print "any key=menu"
    gosub AnyKey
    close cn
    return

; --- lookup by name ---
LookupArt:
    print "{clr}{green}article lookup{lightblue}"
    print
    input "article name"; ar$
    if ar$ = "" then return
    gosub ShowSum
    return
