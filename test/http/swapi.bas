5 rem == swapi explorer v1 ==
6 rem == meatloaf full-mode http on device 8 ==
7 ch=1
10 poke 53280,6:poke 53281,0:poke 646,14
15 dim ur$(9)
20 print chr$(147)
30 print tab(7)"*** swapi explorer ***"
40 print
50 print "1. people"
60 print "2. planets"
70 print "3. films"
80 print "4. species"
85 print "5. vehicles"
90 print "6. starships"
95 print
100 print "s. search"
110 print "q. quit"
120 get k$:if k$="" then 120
130 if k$="s" then gosub 30000:goto 20
140 if k$="q" then poke 53280,14:poke 53281,6:print chr$(147):end
145 if k$<"1" or k$>"6" then 120
150 on val(k$) gosub 20005,20105,20205,20305,20405,20505
160 goto 20

19990 rem --- wait for any key ---
19991 get k$:if k$="" then 19991
19992 return

20000 rem --- display a person ---
20005 print chr$(147):poke 646,5
20010 print "people viewer":print:poke 646,14
20015 input "person id (or 0=list)";id$
20020 if id$="0" then gosub 21000:if sl$<>"" then u$=sl$:gosub 10300:id$=ri$
20022 if id$="" or id$="0" then return
20030 rs$="people":gosub 10000
20035 if s$<>"200" then print "error ";s$:close ch:gosub 19990:return
20040 print chr$(147):poke 646,5:print "person #";id$;":":print:poke 646,14
20045 p$="/name":gosub 10100:print "name      : ";v$
20050 p$="/height":gosub 10100:print "height    : ";v$
20055 p$="/mass":gosub 10100:print "mass      : ";v$
20060 p$="/hair_color":gosub 10100:print "hair      : ";v$
20065 p$="/skin_color":gosub 10100:print "skin      : ";v$
20070 p$="/eye_color":gosub 10100:print "eyes      : ";v$
20075 p$="/birth_year":gosub 10100:print "birth yr  : ";v$
20080 p$="/gender":gosub 10100:print "gender    : ";v$
20085 p$="/homeworld":gosub 10100:print "homeworld : ";v$
20090 p$="/films":gosub 10100:print "films     : ";v$
20095 p$="/species":gosub 10100:print "species   : ";v$
20097 print:print "f=follow link any=menu"
20098 gosub 19990
20099 if k$="f" then gosub 21500
20101 close ch
20102 return

21000 rem --- list people ---
21005 rs$="people":nf$="name"
21010 ur$="https://swapi.dev/api/people/?format=json"
21015 gosub 10400
21020 if sl$="" then return
21025 u$=sl$:gosub 10300:id$=ri$
21030 return

20100 rem --- display a planet ---
20105 print chr$(147):poke 646,5
20110 print "planet viewer":print:poke 646,14
20115 input "planet id (or 0=list)";id$
20120 if id$="0" then gosub 21100:if sl$<>"" then u$=sl$:gosub 10300:id$=ri$
20122 if id$="" or id$="0" then return
20130 rs$="planets":gosub 10000
20135 if s$<>"200" then print "error ";s$:close ch:gosub 19990:return
20140 print chr$(147):poke 646,5:print "planet #";id$;":":print:poke 646,14
20145 p$="/name":gosub 10100:print "name       : ";v$
20150 p$="/climate":gosub 10100:print "climate    : ";v$
20155 p$="/terrain":gosub 10100:print "terrain    : ";v$
20160 p$="/gravity":gosub 10100:print "gravity    : ";v$
20165 p$="/population":gosub 10100:print "population : ";v$
20170 p$="/diameter":gosub 10100:print "diameter   : ";v$
20175 p$="/surface_water":gosub 10100:print "water      : ";v$
20180 p$="/rotation_period":gosub 10100:print "rotation   : ";v$
20185 p$="/orbital_period":gosub 10100:print "orbit      : ";v$
20190 print:print "f=follow any=menu"
20195 gosub 19990
20196 if k$="f" then gosub 21600
20197 close ch
20199 return

21100 rem --- list planets ---
21105 rs$="planets":nf$="name"
21110 ur$="https://swapi.dev/api/planets/?format=json"
21115 gosub 10400
21120 if sl$="" then return
21125 u$=sl$:gosub 10300:id$=ri$
21130 return

20200 rem --- display a film ---
20205 print chr$(147):poke 646,5
20210 print "film viewer":print:poke 646,14
20215 input "film id (or 0=list)";id$
20220 if id$="0" then gosub 21200:if sl$<>"" then u$=sl$:gosub 10300:id$=ri$
20222 if id$="" or id$="0" then return
20230 rs$="films":gosub 10000
20235 if s$<>"200" then print "error ";s$:close ch:gosub 19990:return
20240 print chr$(147):poke 646,5:print "film #";id$;":":print:poke 646,14
20245 p$="/title":gosub 10100:print "title        : ";v$
20250 p$="/episode_id":gosub 10100:print "episode      : ";v$
20255 p$="/director":gosub 10100:print "director     : ";v$
20260 p$="/producer":gosub 10100:print "producer     : ";v$
20265 p$="/release_date":gosub 10100:print "released     : ";v$
20270 p$="/opening_crawl":gosub 10100
20275 if len(v$)>200 then v$=left$(v$,200)
20280 print "crawl       : ";v$
20290 print:print "f=follow any=menu"
20295 gosub 19990
20296 if k$="f" then gosub 21700
20297 close ch
20299 return

21200 rem --- list films ---
21205 rs$="films":nf$="title"
21210 ur$="https://swapi.dev/api/films/?format=json"
21215 gosub 10400
21220 if sl$="" then return
21225 u$=sl$:gosub 10300:id$=ri$
21230 return

20300 rem --- display a species ---
20305 print chr$(147):poke 646,5
20310 print "species viewer":print:poke 646,14
20315 input "species id (or 0=list)";id$
20320 if id$="0" then gosub 21300:if sl$<>"" then u$=sl$:gosub 10300:id$=ri$
20322 if id$="" or id$="0" then return
20330 rs$="species":gosub 10000
20335 if s$<>"200" then print "error ";s$:close ch:gosub 19990:return
20340 print chr$(147):poke 646,5:print "species #";id$;":":print:poke 646,14
20345 p$="/name":gosub 10100:print "name      : ";v$
20350 p$="/classification":gosub 10100:print "class     : ";v$
20355 p$="/designation":gosub 10100:print "designat. : ";v$
20360 p$="/average_height":gosub 10100:print "avg height: ";v$
20365 p$="/average_lifespan":gosub 10100:print "avg life  : ";v$
20370 p$="/language":gosub 10100:print "language  : ";v$
20375 p$="/homeworld":gosub 10100:print "homeworld : ";v$
20385 print:print "f=follow any=menu"
20390 gosub 19990
20395 if k$="f" then gosub 21800
20397 close ch
20399 return

21300 rem --- list species ---
21305 rs$="species":nf$="name"
21310 ur$="https://swapi.dev/api/species/?format=json"
21315 gosub 10400
21320 if sl$="" then return
21325 u$=sl$:gosub 10300:id$=ri$
21330 return

20400 rem --- display a vehicle ---
20405 print chr$(147):poke 646,5
20410 print "vehicle viewer":print:poke 646,14
20415 input "vehicle id (or 0=list)";id$
20420 if id$="0" then gosub 21400:if sl$<>"" then u$=sl$:gosub 10300:id$=ri$
20422 if id$="" or id$="0" then return
20430 rs$="vehicles":gosub 10000
20435 if s$<>"200" then print "error ";s$:close ch:gosub 19990:return
20440 print chr$(147):poke 646,5:print "vehicle #";id$;":":print:poke 646,14
20445 p$="/name":gosub 10100:print "name     : ";v$
20450 p$="/model":gosub 10100:print "model    : ";v$
20455 p$="/manufacturer":gosub 10100:print "maker    : ";v$
20460 p$="/cost_in_credits":gosub 10100:print "cost     : ";v$
20465 p$="/vehicle_class":gosub 10100:print "class    : ";v$
20470 p$="/crew":gosub 10100:print "crew     : ";v$
20475 p$="/passengers":gosub 10100:print "pass     : ";v$
20480 p$="/cargo_capacity":gosub 10100:print "cargo    : ";v$
20485 print:print "f=follow any=menu"
20490 gosub 19990
20495 if k$="f" then gosub 21900
20497 close ch
20499 return

21400 rem --- list vehicles ---
21405 rs$="vehicles":nf$="name"
21410 ur$="https://swapi.dev/api/vehicles/?format=json"
21415 gosub 10400
21420 if sl$="" then return
21425 u$=sl$:gosub 10300:id$=ri$
21430 return

20500 rem --- display a starship ---
20505 print chr$(147):poke 646,5
20510 print "starship viewer":print:poke 646,14
20515 input "starship id (or 0=list)";id$
20520 if id$="0" then gosub 21580:if sl$<>"" then u$=sl$:gosub 10300:id$=ri$
20522 if id$="" or id$="0" then return
20530 rs$="starships":gosub 10000
20535 if s$<>"200" then print "error ";s$:close ch:gosub 19990:return
20540 print chr$(147):poke 646,5:print "starship #";id$;":":print:poke 646,14
20545 p$="/name":gosub 10100:print "name     : ";v$
20550 p$="/model":gosub 10100:print "model    : ";v$
20555 p$="/manufacturer":gosub 10100:print "maker    : ";v$
20560 p$="/cost_in_credits":gosub 10100:print "cost     : ";v$
20565 p$="/starship_class":gosub 10100:print "class    : ";v$
20570 p$="/hyperdrive_rating":gosub 10100:print "hyperdrv : ";v$
20575 p$="/crew":gosub 10100:print "crew     : ";v$
20580 p$="/passengers":gosub 10100:print "pass     : ";v$
20585 p$="/cargo_capacity":gosub 10100:print "cargo    : ";v$
20590 print:print "f=follow any=menu"
20595 gosub 19990
20596 if k$="f" then gosub 22000
20597 close ch
20599 return

21580 rem --- list starships ---
21585 rs$="starships":nf$="name"
21590 ur$="https://swapi.dev/api/starships/?format=json"
21595 gosub 10400
21596 if sl$="" then return
21597 u$=sl$:gosub 10300:id$=ri$
21598 return

30000 rem --- search across resources ---
30005 print chr$(147):poke 646,5
30010 print "search":print:poke 646,14
30015 input "search term";sq$
30020 if sq$="" then return
30025 print:print "search resource:"
30030 print "1. people  2. planets  3. films"
30035 print "4. species 5. vehicles 6. starships"
30040 print "7. all"
30045 get k$:if k$="" then 30045
30050 if k$<"1" or k$>"7" then 30045
30055 print:print "searching..."
30060 if k$="1" then gosub 30100
30065 if k$="2" then gosub 30200
30070 if k$="3" then gosub 30300
30075 if k$="4" then gosub 30400
30080 if k$="5" then gosub 30500
30085 if k$="6" then gosub 30600
30090 if k$="7" then gosub 30700
30095 print:print "done":gosub 19990:return

30100 rem --- search people ---
30105 rs$="people":nf$="name"
30110 ur$="https://swapi.dev/api/people/?search="+sq$+"&format=json"
30115 gosub 10400
30120 if sl$="" then return
30125 u$=sl$:gosub 10300:id$=ri$:gosub 20000
30130 return

30200 rem --- search planets ---
30205 rs$="planets":nf$="name"
30210 ur$="https://swapi.dev/api/planets/?search="+sq$+"&format=json"
30215 gosub 10400
30220 if sl$="" then return
30225 u$=sl$:gosub 10300:id$=ri$:gosub 20100
30230 return

30300 rem --- search films ---
30305 rs$="films":nf$="title"
30310 ur$="https://swapi.dev/api/films/?search="+sq$+"&format=json"
30315 gosub 10400
30320 if sl$="" then return
30325 u$=sl$:gosub 10300:id$=ri$:gosub 20200
30330 return

30400 rem --- search species ---
30405 rs$="species":nf$="name"
30410 ur$="https://swapi.dev/api/species/?search="+sq$+"&format=json"
30415 gosub 10400
30420 if sl$="" then return
30425 u$=sl$:gosub 10300:id$=ri$:gosub 20300
30430 return

30500 rem --- search vehicles ---
30505 rs$="vehicles":nf$="name"
30510 ur$="https://swapi.dev/api/vehicles/?search="+sq$+"&format=json"
30515 gosub 10400
30520 if sl$="" then return
30525 u$=sl$:gosub 10300:id$=ri$:gosub 20400
30530 return

30600 rem --- search starships ---
30605 rs$="starships":nf$="name"
30610 ur$="https://swapi.dev/api/starships/?search="+sq$+"&format=json"
30615 gosub 10400
30620 if sl$="" then return
30625 u$=sl$:gosub 10300:id$=ri$:gosub 20500
30630 return

30700 rem --- search all resources ---
30705 for si=1 to 6
30710 if si=1 then rs$="people":nf$="name"
30715 if si=2 then rs$="planets":nf$="name"
30720 if si=3 then rs$="films":nf$="title"
30725 if si=4 then rs$="species":nf$="name"
30730 if si=5 then rs$="vehicles":nf$="name"
30735 if si=6 then rs$="starships":nf$="name"
30740 ur$="https://swapi.dev/api/"+rs$+"/?search="+sq$+"&format=json"
30745 gosub 10400
30750 if sl$<>"" then u$=sl$:gosub 10300
30755 if sl$="" then 30770
30760 if rt$="people" then id$=ri$:gosub 20000:return
30761 if rt$="planets" then id$=ri$:gosub 20100:return
30762 if rt$="films" then id$=ri$:gosub 20200:return
30763 if rt$="species" then id$=ri$:gosub 20300:return
30764 if rt$="vehicles" then id$=ri$:gosub 20400:return
30765 if rt$="starships" then id$=ri$:gosub 20500:return
30770 next si
30775 print "no matches found"
30780 return

10000 rem --- http get: rs$,id$ -> s$=status ---
10005 ur$="https://swapi.dev/api/"+rs$+"/"+id$+"/?format=json"
10010 open ch,8,2,ur$
10015 print#ch,"m get"
10020 print#ch,"s"
10025 print#ch,"status"
10030 s$=""
10035 get#ch,a$
10040 if st and 64 then 10060
10045 if st and 128 then s$="-1":goto 10060
10050 if a$=chr$(13) then 10060
10055 s$=s$+a$:goto 10035
10060 if s$="206" then s$="200"
10065 return

10100 rem --- json pointer: p$ -> v$ ---
10105 print#ch,"j "+p$
10110 v$=""
10112 rem append byte first, then check st -- last byte sets eoi
10115 get#ch,a$
10120 v$=v$+a$
10122 if st and 64 then 10140
10125 if st and 128 then v$="(n/a)":goto 10140
10130 if len(v$)<250 then 10115
10140 return

10300 rem --- url parser: u$ -> rt$,ri$ ---
10305 for i=1 to len(u$)-4
10310 if mid$(u$,i,5)="/api/" then 10320
10315 next i
10316 rt$="":ri$="":return
10320 i=i+5
10325 rt$=""
10330 if mid$(u$,i,1)="/" then 10340
10335 rt$=rt$+mid$(u$,i,1):i=i+1:goto 10330
10340 i=i+1
10345 ri$=""
10350 if mid$(u$,i,1)="/" then 10360
10355 ri$=ri$+mid$(u$,i,1):i=i+1:goto 10350
10360 return

10400 rem --- listing: ur$,nf$ -> sl$=url or "" ---
10403 pg=1
10406 open ch,8,2,ur$
10409 print#ch,"m get"
10412 print#ch,"s"
10415 print#ch,"status"
10418 s$=""
10421 get#ch,a$
10422 if st and 64 then 10426
10423 if st and 128 then s$="-1":goto 10426
10424 if a$=chr$(13) then 10426
10425 s$=s$+a$:goto 10421
10426 if s$="206" then s$="200"
10427 if s$<>"200" then print "http ";s$:close ch:gosub 19990:sl$="":return
10428 print chr$(147):poke 646,5
10431 print rs$;" page";pg;":" :print:poke 646,14
10433 for i=0 to 9
10436 p$="/results/"+str$(i)+"/"+nf$
10439 gosub 10100
10442 if v$="" or v$="(n/a)" then 10454
10445 print i+1;".";v$
10448 p$="/results/"+str$(i)+"/url"
10451 gosub 10100:ur$(i)=v$
10454 next i
10457 print#ch,"j /next"
10460 v$=""
10463 get#ch,a$
10464 v$=v$+a$
10465 if st and 64 then 10475
10466 if st and 128 then v$="":goto 10475
10469 v$=v$+a$:if len(v$)<250 then 10463
10475 nn$=v$
10478 print#ch,"j /previous"
10481 v$=""
10484 get#ch,a$
10485 v$=v$+a$
10486 if st and 64 then 10496
10487 if st and 128 then v$="":goto 10496
10490 v$=v$+a$:if len(v$)<250 then 10484
10496 pv$=v$
10499 close ch
10502 print:print "n=next p=prev #=sel m=menu"
10505 get k$:if k$="" then 10505
10508 if k$="m" then sl$="":return
10511 if k$="n" and nn$="" then print "end":goto 10502
10514 if k$="n" then ur$=nn$:pg=pg+1:goto 10406
10517 if k$="p" and pv$="" then print "start":goto 10502
10520 if k$="p" then ur$=pv$:pg=pg-1:goto 10406
10523 if k$>="1" and k$<="9" then sl$=ur$(val(k$)-1):return
10526 if k$="0" then sl$=ur$(9):return
10529 goto 10505

21500 rem --- follow link from people ---
21505 print chr$(147):poke 646,5
21510 print "follow which link?":print:poke 646,14
21515 print "1. homeworld":print "2. first film"
21520 get k$:if k$="" then 21520
21525 if k$="1" then p$="/homeworld":gosub 10100:u$=v$
21530 if k$="2" then p$="/films/0":gosub 10100:u$=v$
21535 if u$="" or u$="(n/a)" then print "no link":gosub 19990:return
21540 gosub 10300
21541 close ch
21545 if rt$="planets" then id$=ri$:gosub 20100
21550 if rt$="films" then id$=ri$:gosub 20200
21555 if rt$="species" then id$=ri$:gosub 20300
21560 if rt$="vehicles" then id$=ri$:gosub 20400
21565 if rt$="starships" then id$=ri$:gosub 20500
21570 return

21600 rem --- follow link from planets ---
21605 print chr$(147):poke 646,5
21610 print "follow which link?":print:poke 646,14
21615 print "1. first resident":print "2. first film"
21620 get k$:if k$="" then 21620
21625 if k$="1" then p$="/residents/0":gosub 10100:u$=v$
21630 if k$="2" then p$="/films/0":gosub 10100:u$=v$
21635 if u$="" or u$="(n/a)" then print "no link":gosub 19990:return
21640 gosub 10300
21641 close ch
21645 if rt$="people" then id$=ri$:gosub 20000
21650 if rt$="films" then id$=ri$:gosub 20200
21655 return

21700 rem --- follow link from films ---
21705 print chr$(147):poke 646,5
21710 print "follow which link?":print:poke 646,14
21715 print "1. first character":print "2. first planet"
21720 print "3. first starship":print "4. first vehicle"
21725 print "5. first species"
21730 get k$:if k$="" then 21730
21735 if k$="1" then p$="/characters/0":gosub 10100:u$=v$
21740 if k$="2" then p$="/planets/0":gosub 10100:u$=v$
21745 if k$="3" then p$="/starships/0":gosub 10100:u$=v$
21750 if k$="4" then p$="/vehicles/0":gosub 10100:u$=v$
21755 if k$="5" then p$="/species/0":gosub 10100:u$=v$
21760 if u$="" or u$="(n/a)" then print "no link":gosub 19990:return
21765 gosub 10300
21766 close ch
21770 if rt$="people" then id$=ri$:gosub 20000
21775 if rt$="planets" then id$=ri$:gosub 20100
21780 if rt$="starships" then id$=ri$:gosub 20500
21785 if rt$="vehicles" then id$=ri$:gosub 20400
21790 if rt$="species" then id$=ri$:gosub 20300
21795 return

21800 rem --- follow link from species ---
21801 print chr$(147):poke 646,5
21802 print "homeworld":print:poke 646,14
21805 p$="/homeworld":gosub 10100:u$=v$
21810 if u$="" or u$="(n/a)" then print "no link":gosub 19990:return
21815 gosub 10300
21816 close ch
21820 if rt$="planets" then id$=ri$:gosub 20100
21825 return

21900 rem --- follow link from vehicles ---
21905 print chr$(147):poke 646,5
21910 print "follow which link?":print:poke 646,14
21915 print "1. first pilot":print "2. first film"
21920 get k$:if k$="" then 21920
21925 if k$="1" then p$="/pilots/0":gosub 10100:u$=v$
21930 if k$="2" then p$="/films/0":gosub 10100:u$=v$
21935 if u$="" or u$="(n/a)" then print "no link":gosub 19990:return
21940 gosub 10300
21941 close ch
21945 if rt$="people" then id$=ri$:gosub 20000
21950 if rt$="films" then id$=ri$:gosub 20200
21955 return

22000 rem --- follow link from starships ---
22005 print chr$(147):poke 646,5
22010 print "follow which link?":print:poke 646,14
22015 print "1. first pilot":print "2. first film"
22020 get k$:if k$="" then 22020
22025 if k$="1" then p$="/pilots/0":gosub 10100:u$=v$
22030 if k$="2" then p$="/films/0":gosub 10100:u$=v$
22035 if u$="" or u$="(n/a)" then print "no link":gosub 19990:return
22040 gosub 10300
22041 close ch
22045 if rt$="people" then id$=ri$:gosub 20000
22050 if rt$="films" then id$=ri$:gosub 20200
22055 return
