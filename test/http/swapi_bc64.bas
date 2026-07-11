; Compile with bc.py from VS64 — https://github.com/rolandshacks/vs64
; bc64 -a -o swapi.prg swapi_bc64.bas
; Star Wars API Explorer v2 — bc64 style with labels and @aliases

; === Theme: Star Wars opening crawl ===
POKE 53280,0:POKE 53281,0:POKE 646,7:REM black border, black bg, yellow text
PRINT "{clr}"

ch=1
DIM ur$(9)

GOTO MainInit

; ===================================================================
; SUBROUTINES — networking and helpers
; ===================================================================

; --- Wait for any key press ---
; Reads k$ (key returned)
PressKey:
    GET k$:IF k$="" THEN PressKey
    RETURN

; --- HTTP GET: rs$, id$ → s$ (status) ---
; Sets ur$ to SWAPI URL, opens channel, reads HTTP status
; Called with rs$ and id$ set beforehand
FetchData:
    ur$="https://swapi.dev/api/"+rs$+"/"+id$+"/?format=json"
    OPEN ch,8,2,ur$
    PRINT#ch,"m get"
    PRINT#ch,"s"
    PRINT#ch,"status"
    s$=""

FetchStRead:
    GET#ch,a$
    IF ST AND 64 THEN FetchStCheck
    IF ST AND 128 THEN s$="-1":GOTO FetchStCheck
    IF a$=CHR$(13) THEN FetchStCheck
    s$=s$+a$:GOTO FetchStRead

FetchStCheck:
    IF s$="206" THEN s$="200"
    RETURN

; --- JSON Pointer query: p$ → v$ ---
; Reads one value from the buffered JSON response
QueryJson:
    PRINT#ch,"j "+p$
    v$=""

QueryRead:
    GET#ch,a$
    v$=v$+a$
    IF ST AND 64 THEN QrDone
    IF ST AND 128 THEN v$="(n/a)":GOTO QrDone
    IF LEN(v$)<250 THEN QueryRead

QrDone:
    RETURN

; --- URL parser: ur$ → rt$, ri$ ---
; Extracts resource type and ID from a SWAPI URL (/api/<type>/<id>/)
ParseUrl:
    FOR i=1 TO LEN(ur$)-4
        IF MID$(ur$,i,5)="/api/" THEN ParFound
    NEXT i
    rt$="":ri$="":RETURN

ParFound:
    i=i+5:rt$=""

ParType:
    IF MID$(ur$,i,1)="/" THEN ParId
    rt$=rt$+MID$(ur$,i,1):i=i+1:GOTO ParType

ParId:
    i=i+1:ri$=""

ParIdLoop:
    IF MID$(ur$,i,1)="/" THEN ParDone
    ri$=ri$+MID$(ur$,i,1):i=i+1:GOTO ParIdLoop

ParDone:
    RETURN

; --- Paginated list browser ---
; Caller sets: rs$, ur$, nf$ (field to display, e.g. "name" or "title")
; Returns: sl$ = selected item URL, or "" if user chose menu
BrowseItems:
    pg=1

BrowsePage:
    OPEN ch,8,2,ur$
    PRINT#ch,"m get"
    PRINT#ch,"s"
    PRINT#ch,"status"
    s$=""

BrStRead:
    GET#ch,a$
    IF ST AND 64 THEN BrStCheck
    IF ST AND 128 THEN s$="-1":GOTO BrStCheck
    IF a$=CHR$(13) THEN BrStCheck
    s$=s$+a$:GOTO BrStRead

BrStCheck:
    IF s$="206" THEN s$="200"
    IF s$<>"200" THEN PRINT "HTTP ";s$:CLOSE ch:GOSUB PressKey:sl$="":RETURN
    PRINT "{clr}"
    PRINT rs$;" page ";pg;":"
    PRINT
    FOR i=0 TO 9
        p$="/results/"+STR$(i)+"/"+nf$
        GOSUB QueryJson
        IF v$="" OR v$="(n/a)" THEN BrItemNext
        PRINT i+1;".";v$
        p$="/results/"+STR$(i)+"/url"
        GOSUB QueryJson
        ur$(i)=v$

BrItemNext:
    NEXT i
    PRINT#ch,"j /next"
    @nextUrl$=""

BrRNext:
    GET#ch,a$
    @nextUrl$=@nextUrl$+a$
    IF ST AND 64 THEN BrNxDone
    IF ST AND 128 THEN @nextUrl$="":GOTO BrNxDone
    IF LEN(@nextUrl$)<250 THEN BrRNext

BrNxDone:
    PRINT#ch,"j /previous"
    @prevUrl$=""

BrRPrev:
    GET#ch,a$
    @prevUrl$=@prevUrl$+a$
    IF ST AND 64 THEN BrPrDone
    IF ST AND 128 THEN @prevUrl$="":GOTO BrPrDone
    IF LEN(@prevUrl$)<250 THEN BrRPrev

BrPrDone:
    CLOSE ch
    PRINT
    PRINT "n=next p=prev #=sel m=menu"

BrInput:
    GET k$:IF k$="" THEN BrInput
    IF k$="m" THEN sl$="":RETURN
    IF k$="n" AND @nextUrl$="" THEN PRINT "end":GOTO BrInput
    IF k$="n" THEN ur$=@nextUrl$:pg=pg+1:GOTO BrowsePage
    IF k$="p" AND @prevUrl$="" THEN PRINT "start":GOTO BrInput
    IF k$="p" THEN ur$=@prevUrl$:pg=pg-1:GOTO BrowsePage
    IF k$>="1" AND k$<="9" THEN sl$=ur$(VAL(k$)-1):RETURN
    IF k$="0" THEN sl$=ur$(9):RETURN
    GOTO BrInput

; ===================================================================
; MAIN MENU
; ===================================================================

MainInit:
    GOTO MainMenu

MainMenu:
    GOSUB ShowMenu

MenuLoop:
    GET k$:IF k$="" THEN MenuLoop
    IF k$="s" THEN GOSUB DoSearch:GOTO MainMenu
    IF k$="q" THEN POKE 53280,14:POKE 53281,6:PRINT "{clr}":END
    IF k$<"1" OR k$>"6" THEN MenuLoop
    ON VAL(k$) GOSUB ViewPeople, ViewPlanets, ViewFilms, ViewSpecies, ViewVehicles, ViewStarships
    GOTO MainMenu

ShowMenu:
    PRINT "{clr}"
    PRINT TAB(7)"*** SWAPI Explorer ***"
    PRINT
    PRINT "1. People"
    PRINT "2. Planets"
    PRINT "3. Films"
    PRINT "4. Species"
    PRINT "5. Vehicles"
    PRINT "6. Starships"
    PRINT
    PRINT "s. Search"
    PRINT "q. Quit"
    RETURN

; ===================================================================
; RESOURCE VIEWERS
; ===================================================================

; --- People viewer ---
ViewPeople:
    PRINT "{clr}"
    PRINT "People Viewer"
    PRINT
    INPUT "Person ID (0=list)";id$
    IF id$="0" THEN rs$="people":nf$="name":ur$="https://swapi.dev/api/people/?format=json":GOSUB BrowseItems:IF sl$<>"" THEN ur$=sl$:GOSUB ParseUrl:id$=ri$
    IF id$="" OR id$="0" THEN RETURN
    rs$="people":GOSUB FetchData
    IF s$<>"200" THEN PRINT "Error ";s$:CLOSE ch:GOSUB PressKey:RETURN
    PRINT "{clr}"
    PRINT "Person #";id$;":"
    PRINT
    p$="/name":GOSUB QueryJson:PRINT "Name      : ";v$
    p$="/height":GOSUB QueryJson:PRINT "Height    : ";v$
    p$="/mass":GOSUB QueryJson:PRINT "Mass      : ";v$
    p$="/hair_color":GOSUB QueryJson:PRINT "Hair      : ";v$
    p$="/skin_color":GOSUB QueryJson:PRINT "Skin      : ";v$
    p$="/eye_color":GOSUB QueryJson:PRINT "Eyes      : ";v$
    p$="/birth_year":GOSUB QueryJson:PRINT "Birth Yr  : ";v$
    p$="/gender":GOSUB QueryJson:PRINT "Gender    : ";v$
    p$="/homeworld":GOSUB QueryJson:PRINT "Homeworld : ";v$
    p$="/films":GOSUB QueryJson:PRINT "Films     : ";v$
    p$="/species":GOSUB QueryJson:PRINT "Species   : ";v$
    PRINT
    PRINT "f=follow link  any=menu"
    GOSUB PressKey
    IF k$="f" THEN GOSUB FlwPeople
    CLOSE ch
    RETURN

; --- Planets viewer ---
ViewPlanets:
    PRINT "{clr}"
    PRINT "Planet Viewer"
    PRINT
    INPUT "Planet ID (0=list)";id$
    IF id$="0" THEN rs$="planets":nf$="name":ur$="https://swapi.dev/api/planets/?format=json":GOSUB BrowseItems:IF sl$<>"" THEN ur$=sl$:GOSUB ParseUrl:id$=ri$
    IF id$="" OR id$="0" THEN RETURN
    rs$="planets":GOSUB FetchData
    IF s$<>"200" THEN PRINT "Error ";s$:CLOSE ch:GOSUB PressKey:RETURN
    PRINT "{clr}"
    PRINT "Planet #";id$;":"
    PRINT
    p$="/name":GOSUB QueryJson:PRINT "Name       : ";v$
    p$="/climate":GOSUB QueryJson:PRINT "Climate    : ";v$
    p$="/terrain":GOSUB QueryJson:PRINT "Terrain    : ";v$
    p$="/gravity":GOSUB QueryJson:PRINT "Gravity    : ";v$
    p$="/population":GOSUB QueryJson:PRINT "Population : ";v$
    p$="/diameter":GOSUB QueryJson:PRINT "Diameter   : ";v$
    p$="/surface_water":GOSUB QueryJson:PRINT "Water      : ";v$
    p$="/rotation_period":GOSUB QueryJson:PRINT "Rotation   : ";v$
    p$="/orbital_period":GOSUB QueryJson:PRINT "Orbit      : ";v$
    PRINT
    PRINT "f=follow link  any=menu"
    GOSUB PressKey
    IF k$="f" THEN GOSUB FlwPlanet
    CLOSE ch
    RETURN

; --- Films viewer ---
ViewFilms:
    PRINT "{clr}"
    PRINT "Film Viewer"
    PRINT
    INPUT "Film ID (0=list)";id$
    IF id$="0" THEN rs$="films":nf$="title":ur$="https://swapi.dev/api/films/?format=json":GOSUB BrowseItems:IF sl$<>"" THEN ur$=sl$:GOSUB ParseUrl:id$=ri$
    IF id$="" OR id$="0" THEN RETURN
    rs$="films":GOSUB FetchData
    IF s$<>"200" THEN PRINT "Error ";s$:CLOSE ch:GOSUB PressKey:RETURN
    PRINT "{clr}"
    PRINT "Film #";id$;":"
    PRINT
    p$="/title":GOSUB QueryJson:PRINT "Title     : ";v$
    p$="/episode_id":GOSUB QueryJson:PRINT "Episode   : ";v$
    p$="/director":GOSUB QueryJson:PRINT "Director  : ";v$
    p$="/producer":GOSUB QueryJson:PRINT "Producer  : ";v$
    p$="/release_date":GOSUB QueryJson:PRINT "Released  : ";v$
    p$="/opening_crawl":GOSUB QueryJson
    IF LEN(v$)>200 THEN v$=LEFT$(v$,200)
    PRINT "Crawl     : ";v$
    PRINT
    PRINT "f=follow link  any=menu"
    GOSUB PressKey
    IF k$="f" THEN GOSUB FlwFilm
    CLOSE ch
    RETURN

; --- Species viewer ---
ViewSpecies:
    PRINT "{clr}"
    PRINT "Species Viewer"
    PRINT
    INPUT "Species ID (0=list)";id$
    IF id$="0" THEN rs$="species":nf$="name":ur$="https://swapi.dev/api/species/?format=json":GOSUB BrowseItems:IF sl$<>"" THEN ur$=sl$:GOSUB ParseUrl:id$=ri$
    IF id$="" OR id$="0" THEN RETURN
    rs$="species":GOSUB FetchData
    IF s$<>"200" THEN PRINT "Error ";s$:CLOSE ch:GOSUB PressKey:RETURN
    PRINT "{clr}"
    PRINT "Species #";id$;":"
    PRINT
    p$="/name":GOSUB QueryJson:PRINT "Name      : ";v$
    p$="/classification":GOSUB QueryJson:PRINT "Class     : ";v$
    p$="/designation":GOSUB QueryJson:PRINT "Designat. : ";v$
    p$="/average_height":GOSUB QueryJson:PRINT "Avg Height: ";v$
    p$="/average_lifespan":GOSUB QueryJson:PRINT "Avg Life  : ";v$
    p$="/language":GOSUB QueryJson:PRINT "Language  : ";v$
    p$="/homeworld":GOSUB QueryJson:PRINT "Homeworld : ";v$
    PRINT
    PRINT "f=follow link  any=menu"
    GOSUB PressKey
    IF k$="f" THEN GOSUB FlwSpecies
    CLOSE ch
    RETURN

; --- Vehicles viewer ---
ViewVehicles:
    PRINT "{clr}"
    PRINT "Vehicle Viewer"
    PRINT
    INPUT "Vehicle ID (0=list)";id$
    IF id$="0" THEN rs$="vehicles":nf$="name":ur$="https://swapi.dev/api/vehicles/?format=json":GOSUB BrowseItems:IF sl$<>"" THEN ur$=sl$:GOSUB ParseUrl:id$=ri$
    IF id$="" OR id$="0" THEN RETURN
    rs$="vehicles":GOSUB FetchData
    IF s$<>"200" THEN PRINT "Error ";s$:CLOSE ch:GOSUB PressKey:RETURN
    PRINT "{clr}"
    PRINT "Vehicle #";id$;":"
    PRINT
    p$="/name":GOSUB QueryJson:PRINT "Name     : ";v$
    p$="/model":GOSUB QueryJson:PRINT "Model    : ";v$
    p$="/manufacturer":GOSUB QueryJson:PRINT "Maker    : ";v$
    p$="/cost_in_credits":GOSUB QueryJson:PRINT "Cost     : ";v$
    p$="/vehicle_class":GOSUB QueryJson:PRINT "Class    : ";v$
    p$="/crew":GOSUB QueryJson:PRINT "Crew     : ";v$
    p$="/passengers":GOSUB QueryJson:PRINT "Pass     : ";v$
    p$="/cargo_capacity":GOSUB QueryJson:PRINT "Cargo    : ";v$
    PRINT
    PRINT "f=follow link  any=menu"
    GOSUB PressKey
    IF k$="f" THEN GOSUB FlwVehicle
    CLOSE ch
    RETURN

; --- Starships viewer ---
ViewStarships:
    PRINT "{clr}"
    PRINT "Starship Viewer"
    PRINT
    INPUT "Starship ID (0=list)";id$
    IF id$="0" THEN rs$="starships":nf$="name":ur$="https://swapi.dev/api/starships/?format=json":GOSUB BrowseItems:IF sl$<>"" THEN ur$=sl$:GOSUB ParseUrl:id$=ri$
    IF id$="" OR id$="0" THEN RETURN
    rs$="starships":GOSUB FetchData
    IF s$<>"200" THEN PRINT "Error ";s$:CLOSE ch:GOSUB PressKey:RETURN
    PRINT "{clr}"
    PRINT "Starship #";id$;":"
    PRINT
    p$="/name":GOSUB QueryJson:PRINT "Name     : ";v$
    p$="/model":GOSUB QueryJson:PRINT "Model    : ";v$
    p$="/manufacturer":GOSUB QueryJson:PRINT "Maker    : ";v$
    p$="/cost_in_credits":GOSUB QueryJson:PRINT "Cost     : ";v$
    p$="/starship_class":GOSUB QueryJson:PRINT "Class    : ";v$
    p$="/hyperdrive_rating":GOSUB QueryJson:PRINT "Hyperdrv : ";v$
    p$="/crew":GOSUB QueryJson:PRINT "Crew     : ";v$
    p$="/passengers":GOSUB QueryJson:PRINT "Pass     : ";v$
    p$="/cargo_capacity":GOSUB QueryJson:PRINT "Cargo    : ";v$
    PRINT
    PRINT "f=follow link  any=menu"
    GOSUB PressKey
    IF k$="f" THEN GOSUB FlwStarship
    CLOSE ch
    RETURN

; ===================================================================
; SEARCH
; ===================================================================

DoSearch:
    PRINT "{clr}"
    PRINT "Search"
    PRINT
    INPUT "Search term";@searchQuery$
    IF @searchQuery$="" THEN RETURN
    PRINT
    PRINT "Search resource:"
    PRINT "1. People    2. Planets    3. Films"
    PRINT "4. Species   5. Vehicles   6. Starships"
    PRINT "7. All"

SrInput:
    GET k$:IF k$="" THEN SrInput
    IF k$<"1" OR k$>"7" THEN SrInput
    PRINT
    PRINT "Searching..."
    ON VAL(k$) GOSUB SrPeople, SrPlanets, SrFilms, SrSpecies, SrVehicles, SrStarships, SrAll
    PRINT
    PRINT "Done"
    GOSUB PressKey
    RETURN

SrPeople:
    rs$="people":nf$="name":ur$="https://swapi.dev/api/people/?search="+@searchQuery$+"&format=json"
    GOSUB BrowseItems
    IF sl$="" THEN RETURN
    ur$=sl$:GOSUB ParseUrl:id$=ri$:GOSUB ViewPeople
    RETURN

SrPlanets:
    rs$="planets":nf$="name":ur$="https://swapi.dev/api/planets/?search="+@searchQuery$+"&format=json"
    GOSUB BrowseItems
    IF sl$="" THEN RETURN
    ur$=sl$:GOSUB ParseUrl:id$=ri$:GOSUB ViewPlanets
    RETURN

SrFilms:
    rs$="films":nf$="title":ur$="https://swapi.dev/api/films/?search="+@searchQuery$+"&format=json"
    GOSUB BrowseItems
    IF sl$="" THEN RETURN
    ur$=sl$:GOSUB ParseUrl:id$=ri$:GOSUB ViewFilms
    RETURN

SrSpecies:
    rs$="species":nf$="name":ur$="https://swapi.dev/api/species/?search="+@searchQuery$+"&format=json"
    GOSUB BrowseItems
    IF sl$="" THEN RETURN
    ur$=sl$:GOSUB ParseUrl:id$=ri$:GOSUB ViewSpecies
    RETURN

SrVehicles:
    rs$="vehicles":nf$="name":ur$="https://swapi.dev/api/vehicles/?search="+@searchQuery$+"&format=json"
    GOSUB BrowseItems
    IF sl$="" THEN RETURN
    ur$=sl$:GOSUB ParseUrl:id$=ri$:GOSUB ViewVehicles
    RETURN

SrStarships:
    rs$="starships":nf$="name":ur$="https://swapi.dev/api/starships/?search="+@searchQuery$+"&format=json"
    GOSUB BrowseItems
    IF sl$="" THEN RETURN
    ur$=sl$:GOSUB ParseUrl:id$=ri$:GOSUB ViewStarships
    RETURN

SrAll:
    FOR si=1 TO 6
        IF si=1 THEN rs$="people":nf$="name"
        IF si=2 THEN rs$="planets":nf$="name"
        IF si=3 THEN rs$="films":nf$="title"
        IF si=4 THEN rs$="species":nf$="name"
        IF si=5 THEN rs$="vehicles":nf$="name"
        IF si=6 THEN rs$="starships":nf$="name"
        ur$="https://swapi.dev/api/"+rs$+"/?search="+@searchQuery$+"&format=json"
        GOSUB BrowseItems
        IF sl$="" THEN SrAllNx
        ur$=sl$:GOSUB ParseUrl
        IF rt$="people" THEN id$=ri$:GOSUB ViewPeople:RETURN
        IF rt$="planets" THEN id$=ri$:GOSUB ViewPlanets:RETURN
        IF rt$="films" THEN id$=ri$:GOSUB ViewFilms:RETURN
        IF rt$="species" THEN id$=ri$:GOSUB ViewSpecies:RETURN
        IF rt$="vehicles" THEN id$=ri$:GOSUB ViewVehicles:RETURN
        IF rt$="starships" THEN id$=ri$:GOSUB ViewStarships:RETURN

SrAllNx:
    NEXT si
    PRINT "No matches found"
    RETURN

; ===================================================================
; FOLLOW LINK HELPERS
; ===================================================================

; --- Follow link from People ---
FlwPeople:
    PRINT "{clr}"
    PRINT "Follow which link?"
    PRINT
    PRINT "1. Homeworld"
    PRINT "2. First film"

FlwPeopleIn:
    GET k$:IF k$="" THEN FlwPeopleIn
    IF k$="1" THEN p$="/homeworld":GOSUB QueryJson:ur$=v$
    IF k$="2" THEN p$="/films/0":GOSUB QueryJson:ur$=v$
    IF ur$="" OR ur$="(n/a)" THEN PRINT "No link":GOSUB PressKey:RETURN
    GOSUB ParseUrl
    CLOSE ch
    IF rt$="planets" THEN id$=ri$:GOSUB ViewPlanets
    IF rt$="films" THEN id$=ri$:GOSUB ViewFilms
    IF rt$="species" THEN id$=ri$:GOSUB ViewSpecies
    IF rt$="vehicles" THEN id$=ri$:GOSUB ViewVehicles
    IF rt$="starships" THEN id$=ri$:GOSUB ViewStarships
    RETURN

; --- Follow link from Planets ---
FlwPlanet:
    PRINT "{clr}"
    PRINT "Follow which link?"
    PRINT
    PRINT "1. First resident"
    PRINT "2. First film"

FlwPlanetIn:
    GET k$:IF k$="" THEN FlwPlanetIn
    IF k$="1" THEN p$="/residents/0":GOSUB QueryJson:ur$=v$
    IF k$="2" THEN p$="/films/0":GOSUB QueryJson:ur$=v$
    IF ur$="" OR ur$="(n/a)" THEN PRINT "No link":GOSUB PressKey:RETURN
    GOSUB ParseUrl
    CLOSE ch
    IF rt$="people" THEN id$=ri$:GOSUB ViewPeople
    IF rt$="films" THEN id$=ri$:GOSUB ViewFilms
    RETURN

; --- Follow link from Films ---
FlwFilm:
    PRINT "{clr}"
    PRINT "Follow which link?"
    PRINT
    PRINT "1. First character"
    PRINT "2. First planet"
    PRINT "3. First starship"
    PRINT "4. First vehicle"
    PRINT "5. First species"

FlwFilmIn:
    GET k$:IF k$="" THEN FlwFilmIn
    IF k$="1" THEN p$="/characters/0":GOSUB QueryJson:ur$=v$
    IF k$="2" THEN p$="/planets/0":GOSUB QueryJson:ur$=v$
    IF k$="3" THEN p$="/starships/0":GOSUB QueryJson:ur$=v$
    IF k$="4" THEN p$="/vehicles/0":GOSUB QueryJson:ur$=v$
    IF k$="5" THEN p$="/species/0":GOSUB QueryJson:ur$=v$
    IF ur$="" OR ur$="(n/a)" THEN PRINT "No link":GOSUB PressKey:RETURN
    GOSUB ParseUrl
    CLOSE ch
    IF rt$="people" THEN id$=ri$:GOSUB ViewPeople
    IF rt$="planets" THEN id$=ri$:GOSUB ViewPlanets
    IF rt$="starships" THEN id$=ri$:GOSUB ViewStarships
    IF rt$="vehicles" THEN id$=ri$:GOSUB ViewVehicles
    IF rt$="species" THEN id$=ri$:GOSUB ViewSpecies
    RETURN

; --- Follow link from Species ---
FlwSpecies:
    PRINT "{clr}"
    PRINT "Homeworld"
    PRINT
    p$="/homeworld":GOSUB QueryJson:ur$=v$
    IF ur$="" OR ur$="(n/a)" THEN PRINT "No link":GOSUB PressKey:RETURN
    GOSUB ParseUrl
    CLOSE ch
    IF rt$="planets" THEN id$=ri$:GOSUB ViewPlanets
    RETURN

; --- Follow link from Vehicles ---
FlwVehicle:
    PRINT "{clr}"
    PRINT "Follow which link?"
    PRINT
    PRINT "1. First pilot"
    PRINT "2. First film"

FlwVehicleIn:
    GET k$:IF k$="" THEN FlwVehicleIn
    IF k$="1" THEN p$="/pilots/0":GOSUB QueryJson:ur$=v$
    IF k$="2" THEN p$="/films/0":GOSUB QueryJson:ur$=v$
    IF ur$="" OR ur$="(n/a)" THEN PRINT "No link":GOSUB PressKey:RETURN
    GOSUB ParseUrl
    CLOSE ch
    IF rt$="people" THEN id$=ri$:GOSUB ViewPeople
    IF rt$="films" THEN id$=ri$:GOSUB ViewFilms
    RETURN

; --- Follow link from Starships ---
FlwStarship:
    PRINT "{clr}"
    PRINT "Follow which link?"
    PRINT
    PRINT "1. First pilot"
    PRINT "2. First film"

FlwStarshipIn:
    GET k$:IF k$="" THEN FlwStarshipIn
    IF k$="1" THEN p$="/pilots/0":GOSUB QueryJson:ur$=v$
    IF k$="2" THEN p$="/films/0":GOSUB QueryJson:ur$=v$
    IF ur$="" OR ur$="(n/a)" THEN PRINT "No link":GOSUB PressKey:RETURN
    GOSUB ParseUrl
    CLOSE ch
    IF rt$="people" THEN id$=ri$:GOSUB ViewPeople
    IF rt$="films" THEN id$=ri$:GOSUB ViewFilms
    RETURN
